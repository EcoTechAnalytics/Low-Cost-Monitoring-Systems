#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include <Adafruit_SHT4x.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>
#include "SparkFun_AS7265X.h"
#include "kSeries.h"

#define USE_PCF8523 0
#define LOG_RAW_COUNTS 0

const byte OLED_I2C_ADDRESS = 0x3C;
const byte MLX90614_I2C_ADDRESS = 0x5A;
const byte MLX90614_OBJECT_TEMP_REG = 0x07;

const byte K30_RX_PIN = 2;
const byte K30_TX_PIN = 3;
const byte MOSFET_PIN = 7;
const byte BUTTON_PIN = 6;
const byte BUZZER_PIN = 8;
const byte SD_CS_PIN = 10;

const byte MOSFET_ON = HIGH;
const byte MOSFET_OFF = LOW;
const byte BUZZER_ON = HIGH;
const byte BUZZER_OFF = LOW;

const unsigned long SAMPLE_INTERVAL_MS = 5000UL;
const unsigned long BUZZER_TIME_MS = 1000UL;
const unsigned long T_MEASUREMENT_TIME_MS = 120000UL;
const unsigned long D_MEASUREMENT_TIME_MS = 300000UL;
const unsigned long FLUSH_TIME_MS = 60000UL;
const unsigned long D_PRESS_TIME_MS = 3000UL;
const unsigned long FLUSH_PRESS_TIME_MS = 5000UL;
const unsigned long DEBOUNCE_TIME_MS = 50UL;
const unsigned long HOLD_DISPLAY_INTERVAL_MS = 100UL;

const byte SPECTRAL_CHANNELS = 18;
const byte SPEC_DARK_SAMPLES = 3;
const byte SPEC_LIT_SAMPLES = 10;
const unsigned long SPEC_LIGHT_TIME_MS = 20000UL;
const unsigned long SPEC_SAMPLE_INTERVAL_MS = 2000UL;
const unsigned long SPEC_LED_SETTLE_MS = 100UL;
const unsigned long SPEC_DISPLAY_INTERVAL_MS = 500UL;

#if LOG_RAW_COUNTS
const uint16_t SPEC_SATURATION_LEVEL = 65000U;
#endif

const char LOG_FILE[] = "DATA.CSV";
const char SPECTRA_FILE[] = "SPECTRA.CSV";

SSD1306AsciiWire oled;
kSeries K30(K30_RX_PIN, K30_TX_PIN);
Adafruit_SHT4x sht41;
AS7265X spectralSensor;

#if USE_PCF8523
RTC_PCF8523 rtc;
#else
RTC_DS1307 rtc;
#endif

enum SystemState { IDLE, HOLDING, COUNTDOWN, MEASURING, FLUSHING };
SystemState currentState = IDLE;

enum SpectroscopyState { SPEC_IDLE, SPEC_DARK, SPEC_SETTLE, SPEC_LIT, SPEC_DONE };
SpectroscopyState specState = SPEC_IDLE;

char measurementType = '-';
unsigned long activeMeasurementTimeMs = 0;
unsigned long stateStartMs = 0;
unsigned long measurementStartMs = 0;
unsigned long nextSampleMs = 0;

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;
unsigned long debounceStartMs = 0;
unsigned long buttonPressStartMs = 0;
unsigned long lastHoldDisplayMs = 0;

bool sdReady = false;
bool spectraFileReady = false;
bool spectralSensorReady = false;

byte specLedIndex = 0;
byte specSamplesTaken = 0;
unsigned long specLightStartMs = 0;
unsigned long nextSpecSampleMs = 0;
unsigned long lastSpecDisplayMs = 0;
float darkMean[SPECTRAL_CHANNELS];
float specSum[SPECTRAL_CHANNELS];
bool phaseSaturated = false;
bool sequenceSaturated = false;

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MOSFET_PIN, OUTPUT);
  pinMode(SD_CS_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  digitalWrite(MOSFET_PIN, MOSFET_OFF);

  Wire.begin();
  Wire.setClock(100000UL);

  initializeOLED();
  initializeSHT41();
  initializeRTC();
  initializeAS7265x();
  initializeMLX90614();
  initializeSD();

  lastButtonReading = digitalRead(BUTTON_PIN);
  stableButtonState = lastButtonReading;
  nextSampleMs = millis();
  showReadyScreen();
}

void loop() {
  unsigned long currentMs = millis();
  checkButton(currentMs);
  updateHoldingDisplay(currentMs);
  updateCountdown(currentMs);
  checkRunEnd(currentMs);

  // Give a due spectroscopy sample priority, then run normal sensors.
  updateSpectroscopy(millis());
  readSensorsWhenDue();
}

void initializeOLED() {
  oled.begin(&Adafruit128x64, OLED_I2C_ADDRESS);
  oled.setFont(System5x7);
  oled.clear();
  oled.set1X();
}

void initializeSHT41() {
  if (!sht41.begin()) {
    showCriticalError(F("SHT41"));
    stopSystem();
  }
  sht41.setPrecision(SHT4X_HIGH_PRECISION);
  sht41.setHeater(SHT4X_NO_HEATER);
}

void initializeRTC() {
  if (!rtc.begin()) {
    showCriticalError(F("RTC"));
    stopSystem();
  }
#if USE_PCF8523
  if (!rtc.initialized() || rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  rtc.start();
#else
  if (!rtc.isrunning()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
#endif
}

void initializeAS7265x() {
  if (!spectralSensor.begin()) {
    showCriticalError(F("AS7265x"));
    stopSystem();
  }

  spectralSensorReady = true;
  spectralSensor.disableIndicator();

  // Keep the safe settings used in the successful tests.
  spectralSensor.setBulbCurrent(AS7265X_LED_CURRENT_LIMIT_12_5MA, AS7265x_LED_UV);
  spectralSensor.setBulbCurrent(AS7265X_LED_CURRENT_LIMIT_12_5MA, AS7265x_LED_WHITE);
  spectralSensor.setBulbCurrent(AS7265X_LED_CURRENT_LIMIT_12_5MA, AS7265x_LED_IR);

  // AS7265X_GAIN_37X means 3.7x.
  spectralSensor.setGain(AS7265X_GAIN_37X);
  spectralSensor.setMeasurementMode(AS7265X_MEASUREMENT_MODE_6CHAN_ONE_SHOT);
  spectralSensor.setIntegrationCycles(49);
  allSpectralLightsOff();
}

void initializeMLX90614() {
  Wire.beginTransmission(MLX90614_I2C_ADDRESS);
  if (Wire.endTransmission() != 0) {
    showCriticalError(F("MLX90614"));
    stopSystem();
  }
}

void initializeSD() {
  if (!SD.begin(SD_CS_PIN)) {
    sdReady = false;
    spectraFileReady = false;
    return;
  }

  sdReady = true;
  File dataFile = SD.open(LOG_FILE, FILE_WRITE);
  if (!dataFile) {
    sdReady = false;
  } else {
    if (dataFile.size() == 0) {
      dataFile.println(F("Date,Time,CO2_ppm,Temperature_C,Humidity_pct,LeafTemp_C,Type"));
    }
    dataFile.close();
  }

  File spectraFile = SD.open(SPECTRA_FILE, FILE_WRITE);
  if (!spectraFile) {
    spectraFileReady = false;
  } else {
    spectraFileReady = true;
    if (spectraFile.size() == 0) {
      spectraFile.println(F("Date,Time,Type,Light,Samples,Sat,A410,B435,C460,D485,E510,F535,G560,H585,R610,I645,S680,J705,T730,U760,V810,W860,K900,L940"));
    }
    spectraFile.close();
  }
}

void checkButton(unsigned long currentMs) {
  bool rawReading = digitalRead(BUTTON_PIN);

  if (rawReading != lastButtonReading) {
    debounceStartMs = currentMs;
    lastButtonReading = rawReading;
  }

  if (currentMs - debounceStartMs < DEBOUNCE_TIME_MS) return;
  if (rawReading == stableButtonState) return;

  stableButtonState = rawReading;

  if (stableButtonState == LOW && currentState == IDLE) {
    startButtonHold(currentMs);
    return;
  }

  if (stableButtonState == HIGH && currentState == HOLDING) finishButtonHold(currentMs);
}

void startButtonHold(unsigned long currentMs) {
  currentState = HOLDING;
  buttonPressStartMs = currentMs;
  lastHoldDisplayMs = 0;
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  digitalWrite(MOSFET_PIN, MOSFET_OFF);
  allSpectralLightsOff();
  showHoldScreen(0);
}

void updateHoldingDisplay(unsigned long currentMs) {
  if (currentState != HOLDING) return;
  if (currentMs - lastHoldDisplayMs < HOLD_DISPLAY_INTERVAL_MS) return;
  lastHoldDisplayMs = currentMs;
  showHoldScreen(currentMs - buttonPressStartMs);
}

void finishButtonHold(unsigned long currentMs) {
  unsigned long heldMs = currentMs - buttonPressStartMs;
  if (heldMs >= FLUSH_PRESS_TIME_MS) startFlush(currentMs);
  else if (heldMs >= D_PRESS_TIME_MS) startCountdown(currentMs, 'D', D_MEASUREMENT_TIME_MS);
  else startCountdown(currentMs, 'T', T_MEASUREMENT_TIME_MS);
}

void startCountdown(unsigned long currentMs, char selectedType, unsigned long selectedDurationMs) {
  currentState = COUNTDOWN;
  stateStartMs = currentMs;
  measurementType = selectedType;
  activeMeasurementTimeMs = selectedDurationMs;
  specState = SPEC_IDLE;

  digitalWrite(MOSFET_PIN, MOSFET_OFF);
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  allSpectralLightsOff();
  showCountdownScreen();
}

void updateCountdown(unsigned long currentMs) {
  if (currentState != COUNTDOWN) return;
  if (currentMs - stateStartMs < BUZZER_TIME_MS) return;

  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  digitalWrite(MOSFET_PIN, MOSFET_ON);

  currentState = MEASURING;
  measurementStartMs = currentMs;

  // Immediate normal measurement, then spectroscopy starts.
  nextSampleMs = currentMs;
  readAndProcessSensors();
  startSpectroscopy(millis());
  nextSampleMs = millis() + SAMPLE_INTERVAL_MS;
}

void startFlush(unsigned long currentMs) {
  currentState = FLUSHING;
  measurementType = '-';
  activeMeasurementTimeMs = FLUSH_TIME_MS;
  measurementStartMs = currentMs;
  specState = SPEC_IDLE;

  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  digitalWrite(MOSFET_PIN, MOSFET_ON);
  allSpectralLightsOff();
  nextSampleMs = currentMs;

  oled.clear();
  oled.println(F("FLUSH"));
  oled.println(F("PUMP ON"));
  oled.println(F("NO SAVE"));
}

void checkRunEnd(unsigned long currentMs) {
  if (currentState != MEASURING && currentState != FLUSHING) return;
  if (currentMs - measurementStartMs < activeMeasurementTimeMs) return;

  bool finishedFlush = (currentState == FLUSHING);
  char finishedType = measurementType;

  digitalWrite(MOSFET_PIN, MOSFET_OFF);
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  allSpectralLightsOff();

  currentState = IDLE;
  measurementType = '-';
  activeMeasurementTimeMs = 0;
  specState = SPEC_IDLE;
  nextSampleMs = currentMs + SAMPLE_INTERVAL_MS;

  oled.clear();
  if (finishedFlush) {
    oled.println(F("FLUSH DONE"));
  } else {
    oled.print(F("TYPE "));
    oled.print(finishedType);
    oled.println(F(" DONE"));
    oled.print(F("DATA "));
    oled.println(sdReady ? F("OK") : F("FAIL"));
    oled.print(F("SPEC "));
    oled.println(spectraFileReady ? F("OK") : F("FAIL"));
#if LOG_RAW_COUNTS
    if (sequenceSaturated) oled.println(F("RAW SAT SEEN"));
#endif
  }
  oled.println(F("READY"));
}

void readSensorsWhenDue() {
  unsigned long currentMs = millis();
  if ((long)(currentMs - nextSampleMs) < 0) return;

  nextSampleMs += SAMPLE_INTERVAL_MS;
  if ((long)(millis() - nextSampleMs) >= 0) nextSampleMs = millis() + SAMPLE_INTERVAL_MS;

  readAndProcessSensors();
}

void readAndProcessSensors() {
  sensors_event_t humidityEvent;
  sensors_event_t temperatureEvent;

  bool shtReadingOK = sht41.getEvent(&humidityEvent, &temperatureEvent);

  double co2ppm = K30.getCO2('p');
  if (co2ppm < 0) co2ppm = K30.getCO2('p');
  if (co2ppm < 0) co2ppm = K30.getCO2('p');

  float leafTemp = readLeafTemperatureMedian();
  DateTime timestamp = rtc.now();

  unsigned long elapsedMs = 0;
  if (currentState == MEASURING || currentState == FLUSHING) {
    elapsedMs = millis() - measurementStartMs;
  }

  updateOLED(timestamp, elapsedMs, co2ppm,
             temperatureEvent.temperature,
             humidityEvent.relative_humidity,
             shtReadingOK, leafTemp);

  if (currentState == MEASURING && sdReady) {
    saveMeasurement(timestamp, co2ppm,
                    temperatureEvent.temperature,
                    humidityEvent.relative_humidity,
                    shtReadingOK, leafTemp,
                    measurementType);
  }
}

float readMLX90614Once() {
  Wire.beginTransmission(MLX90614_I2C_ADDRESS);
  Wire.write(MLX90614_OBJECT_TEMP_REG);

  if (Wire.endTransmission(false) != 0) return NAN;

  byte received = Wire.requestFrom((uint8_t)MLX90614_I2C_ADDRESS, (uint8_t)3);
  if (received != 3 || Wire.available() < 3) {
    while (Wire.available()) Wire.read();
    return NAN;
  }

  byte lowByte = Wire.read();
  byte highByte = Wire.read();
  Wire.read(); // PEC byte

  uint16_t raw = ((uint16_t)highByte << 8) | lowByte;
  if (raw & 0x8000) return NAN;

  return (raw * 0.02f) - 273.15f;
}

float readLeafTemperatureMedian() {
  float values[5];
  byte n = 0;

  for (byte attempt = 0; attempt < 5; attempt++) {
    float t = readMLX90614Once();
    if (!isnan(t)) values[n++] = t;
    delay(15);
  }

  if (n == 0) return NAN;

  for (byte i = 1; i < n; i++) {
    float key = values[i];
    byte j = i;
    while (j > 0 && values[j - 1] > key) {
      values[j] = values[j - 1];
      j--;
    }
    values[j] = key;
  }

  if (n & 1) return values[n / 2];
  return (values[(n / 2) - 1] + values[n / 2]) * 0.5f;
}

void startSpectroscopy(unsigned long currentMs) {
  allSpectralLightsOff();
  specLedIndex = 0;
  sequenceSaturated = false;
  lastSpecDisplayMs = 0;
  beginDarkPhase(currentMs);
}

void beginDarkPhase(unsigned long currentMs) {
  (void)currentMs;
  allSpectralLightsOff();
  resetSpectrumAccumulator();
  clearDarkMean();
  specState = SPEC_DARK;
}

void beginLightPhase(unsigned long currentMs) {
  resetSpectrumAccumulator();

  if (specLedIndex == 0) spectralSensor.enableBulb(AS7265x_LED_UV);
  else if (specLedIndex == 1) spectralSensor.enableBulb(AS7265x_LED_WHITE);
  else spectralSensor.enableBulb(AS7265x_LED_IR);

  // The 20 s interval starts when the LED is switched on.
  specLightStartMs = currentMs;
  nextSpecSampleMs = currentMs + SPEC_LED_SETTLE_MS;
  specState = SPEC_SETTLE;
}

void updateSpectroscopy(unsigned long currentMs) {
  if (currentState != MEASURING) return;
  if (specState == SPEC_IDLE || specState == SPEC_DONE) return;

  updateSpectroscopyDisplay(currentMs);

  if (specState == SPEC_DARK) {
    takeAndAccumulateSpectrum();

    if (specSamplesTaken >= SPEC_DARK_SAMPLES) {
      finishDarkPhase();
      if (spectraFileReady) saveSpectrumRow(lightLabel(specLedIndex, false), true);
      beginLightPhase(millis());
    }
    return;
  }

  if (specState == SPEC_SETTLE) {
    if ((long)(currentMs - nextSpecSampleMs) < 0) return;
    specState = SPEC_LIT;
  }

  if (specState == SPEC_LIT) {
    if (specSamplesTaken < SPEC_LIT_SAMPLES &&
        (long)(currentMs - nextSpecSampleMs) >= 0) {

      takeAndAccumulateSpectrum();

      // Target sample times: 0.1, 2.1, 4.1 ... 18.1 s after LED ON.
      nextSpecSampleMs = specLightStartMs + SPEC_LED_SETTLE_MS +
                         ((unsigned long)specSamplesTaken * SPEC_SAMPLE_INTERVAL_MS);
    }

    // LED stays on for at least 20 seconds, and all 10 samples must exist.
    if (currentMs - specLightStartMs >= SPEC_LIGHT_TIME_MS &&
        specSamplesTaken >= SPEC_LIT_SAMPLES) {

      allSpectralLightsOff();

      if (spectraFileReady) saveSpectrumRow(lightLabel(specLedIndex, true), false);
      sequenceSaturated = sequenceSaturated || phaseSaturated;

      if (specLedIndex >= 2) {
        finishSpectroscopy();
      } else {
        specLedIndex++;
        beginDarkPhase(millis());
      }
    }
  }
}

void finishDarkPhase() {
  if (specSamplesTaken == 0) return;

  for (byte i = 0; i < SPECTRAL_CHANNELS; i++) {
    darkMean[i] = specSum[i] / specSamplesTaken;
  }
  sequenceSaturated = sequenceSaturated || phaseSaturated;
}

void finishSpectroscopy() {
  allSpectralLightsOff();
  specState = SPEC_DONE;
}

void takeAndAccumulateSpectrum() {
  spectralSensor.takeMeasurements();
  specSamplesTaken++;

  for (byte i = 0; i < SPECTRAL_CHANNELS; i++) {
#if LOG_RAW_COUNTS
    uint16_t value = readRawSpectralChannel(i);
    if (value >= SPEC_SATURATION_LEVEL) phaseSaturated = true;
    specSum[i] += (float)value;
#else
    specSum[i] += readCalibratedSpectralChannel(i);
#endif
  }
}

#if !LOG_RAW_COUNTS
float readCalibratedSpectralChannel(byte i) {
  switch (i) {
    case 0:  return spectralSensor.getCalibratedA();
    case 1:  return spectralSensor.getCalibratedB();
    case 2:  return spectralSensor.getCalibratedC();
    case 3:  return spectralSensor.getCalibratedD();
    case 4:  return spectralSensor.getCalibratedE();
    case 5:  return spectralSensor.getCalibratedF();
    case 6:  return spectralSensor.getCalibratedG();
    case 7:  return spectralSensor.getCalibratedH();
    case 8:  return spectralSensor.getCalibratedR();
    case 9:  return spectralSensor.getCalibratedI();
    case 10: return spectralSensor.getCalibratedS();
    case 11: return spectralSensor.getCalibratedJ();
    case 12: return spectralSensor.getCalibratedT();
    case 13: return spectralSensor.getCalibratedU();
    case 14: return spectralSensor.getCalibratedV();
    case 15: return spectralSensor.getCalibratedW();
    case 16: return spectralSensor.getCalibratedK();
    case 17: return spectralSensor.getCalibratedL();
    default: return 0.0f;
  }
}
#endif

#if LOG_RAW_COUNTS
uint16_t readRawSpectralChannel(byte i) {
  switch (i) {
    case 0:  return spectralSensor.getA();
    case 1:  return spectralSensor.getB();
    case 2:  return spectralSensor.getC();
    case 3:  return spectralSensor.getD();
    case 4:  return spectralSensor.getE();
    case 5:  return spectralSensor.getF();
    case 6:  return spectralSensor.getG();
    case 7:  return spectralSensor.getH();
    case 8:  return spectralSensor.getR();
    case 9:  return spectralSensor.getI();
    case 10: return spectralSensor.getS();
    case 11: return spectralSensor.getJ();
    case 12: return spectralSensor.getT();
    case 13: return spectralSensor.getU();
    case 14: return spectralSensor.getV();
    case 15: return spectralSensor.getW();
    case 16: return spectralSensor.getK();
    case 17: return spectralSensor.getL();
    default: return 0;
  }
}
#endif

void resetSpectrumAccumulator() {
  specSamplesTaken = 0;
  phaseSaturated = false;
  for (byte i = 0; i < SPECTRAL_CHANNELS; i++) specSum[i] = 0.0f;
}

void clearDarkMean() {
  for (byte i = 0; i < SPECTRAL_CHANNELS; i++) darkMean[i] = 0.0f;
}

const __FlashStringHelper *lightLabel(byte ledIndex, bool lit) {
  if (lit) {
    if (ledIndex == 0) return F("UV_CORR");
    if (ledIndex == 1) return F("WHITE_CORR");
    return F("IR_CORR");
  }

  if (ledIndex == 0) return F("DARK_UV");
  if (ledIndex == 1) return F("DARK_WHITE");
  return F("DARK_IR");
}

void allSpectralLightsOff() {
  if (!spectralSensorReady) return;
  spectralSensor.disableBulb(AS7265x_LED_WHITE);
  spectralSensor.disableBulb(AS7265x_LED_IR);
  spectralSensor.disableBulb(AS7265x_LED_UV);
}

void saveSpectrumRow(const __FlashStringHelper *lightName, bool darkRow) {
  File f = SD.open(SPECTRA_FILE, FILE_WRITE);
  if (!f) {
    spectraFileReady = false;
    return;
  }

  DateTime timestamp = rtc.now();
  printDate(f, timestamp);
  f.print(',');
  printTime(f, timestamp);
  f.print(',');
  f.print(measurementType);
  f.print(',');
  f.print(lightName);
  f.print(',');
  f.print(specSamplesTaken);
  f.print(',');

#if LOG_RAW_COUNTS
  f.print(phaseSaturated ? '1' : '0');
#else
  f.print('0');
#endif

  for (byte i = 0; i < SPECTRAL_CHANNELS; i++) {
    float value;

    if (darkRow) {
      value = darkMean[i];
    } else {
      float illuminatedMean =
        (specSamplesTaken > 0) ? specSum[i] / specSamplesTaken : 0.0f;
      value = illuminatedMean - darkMean[i];
    }

    f.print(',');
    f.print(value, 3);
  }

  f.println();
  f.close();
}

void updateOLED(const DateTime &timestamp,
                unsigned long elapsedMs,
                double co2ppm,
                float temperature,
                float humidity,
                bool shtReadingOK,
                float leafTemp) {
  if (currentState == HOLDING) {
    showHoldScreen(millis() - buttonPressStartMs);
    return;
  }

  oled.clear();

  if (currentState == MEASURING) {
    oled.print(measurementType);
    oled.println(F(" MEASURE"));
  } else if (currentState == FLUSHING) {
    oled.println(F("FLUSH"));
  } else if (currentState == COUNTDOWN) {
    oled.println(F("COUNTDOWN"));
  } else {
    oled.println(F("IDLE"));
  }

  oled.print(F("CO2 "));
  if (co2ppm < 0) oled.println(F("ERR"));
  else {
    oled.print(co2ppm, 0);
    oled.println(F("ppm"));
  }

  oled.print(F("T "));
  if (shtReadingOK) {
    oled.print(temperature, 1);
    oled.print(F(" RH "));
    oled.print(humidity, 1);
    oled.println(F("%"));
  } else {
    oled.println(F("SHT ERR"));
  }

  oled.print(F("LEAF "));
  if (isnan(leafTemp)) oled.println(F("ERR"));
  else {
    oled.print(leafTemp, 1);
    oled.println(F("C"));
  }

  printTime(oled, timestamp);
  oled.println();

  if (currentState == MEASURING) {
    oled.print(F("RUN "));
    oled.print(elapsedMs / 1000UL);
    oled.print('/');
    oled.print(activeMeasurementTimeMs / 1000UL);
    oled.println(F("s"));
    printSpectroscopyOLEDLine();
  } else if (currentState == FLUSHING) {
    oled.print(F("RUN "));
    oled.print(elapsedMs / 1000UL);
    oled.println(F("/60s"));
    oled.println(F("SPEC OFF"));
  } else {
    oled.println(F("HOLD BUTTON"));
  }

  oled.print(F("SD "));
  oled.print(sdReady ? F("OK") : F("NO"));
  oled.print(F(" SP "));
  oled.println(spectraFileReady ? F("OK") : F("NO"));
}

void updateSpectroscopyDisplay(unsigned long currentMs) {
  if (currentMs - lastSpecDisplayMs < SPEC_DISPLAY_INTERVAL_MS) return;
  lastSpecDisplayMs = currentMs;

  oled.clear();
  oled.print(measurementType);
  oled.println(F(" SPECTRA"));

  oled.print(F("LIGHT "));
  if (specLedIndex == 0) oled.println(F("UV"));
  else if (specLedIndex == 1) oled.println(F("WHITE"));
  else oled.println(F("IR"));

  oled.print(F("PHASE "));
  if (specState == SPEC_DARK) oled.println(F("DARK"));
  else if (specState == SPEC_SETTLE) oled.println(F("SETTLE"));
  else oled.println(F("LIT"));

  oled.print(F("N "));
  oled.print(specSamplesTaken);
  oled.print('/');
  oled.println(specState == SPEC_DARK ? SPEC_DARK_SAMPLES : SPEC_LIT_SAMPLES);

  if (specState == SPEC_LIT || specState == SPEC_SETTLE) {
    unsigned long elapsed = millis() - specLightStartMs;
    oled.print(F("LIGHT "));
    oled.print(elapsed / 1000UL);
    oled.println(F("/20s"));
  }

  oled.println(F("TEMP RUNNING"));
}

void printSpectroscopyOLEDLine() {
  oled.print(F("SPEC "));

  if (specState == SPEC_DARK) oled.println(F("DARK"));
  else if (specState == SPEC_SETTLE) oled.println(F("SETTLE"));
  else if (specState == SPEC_LIT) {
    oled.print(F("LIT "));
    oled.println(specSamplesTaken);
  } else if (specState == SPEC_DONE) oled.println(F("DONE"));
  else oled.println(F("WAIT"));
}

void showHoldScreen(unsigned long heldMs) {
  oled.clear();
  oled.println(F("HOLD"));
  oled.print(heldMs / 1000.0, 1);
  oled.println(F("s"));

  if (heldMs >= FLUSH_PRESS_TIME_MS) oled.println(F("RELEASE FLUSH"));
  else if (heldMs >= D_PRESS_TIME_MS) oled.println(F("RELEASE D"));
  else oled.println(F("RELEASE T"));
}

void showCountdownScreen() {
  oled.clear();
  oled.print(measurementType);
  oled.println(F(" COUNTDOWN"));
  oled.println(F("BUZZER ON"));
  oled.println(F("PUMP OFF"));
}

void showReadyScreen() {
  oled.clear();
  oled.println(F("READY"));
  oled.println(F("<3s T 120s"));
  oled.println(F("3-5s D 5m"));
  oled.println(F(">=5s FLUSH"));
  oled.print(F("SD "));
  oled.print(sdReady ? F("OK") : F("NO"));
  oled.print(F(" SP "));
  oled.println(spectraFileReady ? F("OK") : F("NO"));
}

void showCriticalError(const __FlashStringHelper *name) {
  oled.clear();
  oled.println(F("ERROR"));
  oled.println(name);
  oled.println(F("STOPPED"));
}

void saveMeasurement(const DateTime &timestamp,
                     double co2ppm,
                     float temperature,
                     float humidity,
                     bool shtReadingOK,
                     float leafTemp,
                     char type) {
  File dataFile = SD.open(LOG_FILE, FILE_WRITE);
  if (!dataFile) {
    sdReady = false;
    return;
  }

  printDate(dataFile, timestamp);
  dataFile.print(',');
  printTime(dataFile, timestamp);
  dataFile.print(',');

  if (co2ppm < 0) dataFile.print(F("ERROR"));
  else dataFile.print(co2ppm, 0);
  dataFile.print(',');

  if (shtReadingOK) dataFile.print(temperature, 2);
  else dataFile.print(F("ERROR"));
  dataFile.print(',');

  if (shtReadingOK) dataFile.print(humidity, 2);
  else dataFile.print(F("ERROR"));
  dataFile.print(',');

  if (isnan(leafTemp)) dataFile.print(F("ERROR"));
  else dataFile.print(leafTemp, 2);

  dataFile.print(',');
  dataFile.println(type);
  dataFile.close();
}

void printTwoDigits(Print &output, byte value) {
  if (value < 10) output.print('0');
  output.print(value);
}

void printDate(Print &output, const DateTime &timestamp) {
  output.print(timestamp.year());
  output.print('-');
  printTwoDigits(output, timestamp.month());
  output.print('-');
  printTwoDigits(output, timestamp.day());
}

void printTime(Print &output, const DateTime &timestamp) {
  printTwoDigits(output, timestamp.hour());
  output.print(':');
  printTwoDigits(output, timestamp.minute());
  output.print(':');
  printTwoDigits(output, timestamp.second());
}

void stopSystem() {
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  digitalWrite(MOSFET_PIN, MOSFET_OFF);
  allSpectralLightsOff();
  while (true) delay(100);
}
