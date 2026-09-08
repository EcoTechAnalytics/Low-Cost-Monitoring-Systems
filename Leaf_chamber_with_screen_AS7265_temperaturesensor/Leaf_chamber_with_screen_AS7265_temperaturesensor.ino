/*
  UNO-optimized measurement system with OLED, AS7265x Triad and MLX90614

  IMPORTANT: The numeric constants below define the actual behavior.

  Current button behavior:

  A) Short press, less than 3 seconds
     Type = T
     1-second buzzer countdown
     MOSFET ON for 120 seconds
     CO2 / temperature / humidity / leaf temperature recorded every 5 seconds
     One spectroscopy sequence runs at the beginning

  B) Hold for 3 seconds to less than 5 seconds
     Type = D
     1-second buzzer countdown
     MOSFET ON for 5 minutes
     CO2 / temperature / humidity / leaf temperature recorded every 5 seconds
     One spectroscopy sequence runs at the beginning

  C) Hold for 5 seconds or longer
     FLUSH mode
     MOSFET turns ON when button is released
     MOSFET stays ON for 60 seconds
     No DATA.CSV or SPECTRA.CSV values are saved

  Spectroscopy sequence for T and D:
     1. DARK: all illumination LEDs OFF, 3 spectra averaged
     2. UV: UV LED ON for 20 seconds, repeated spectra averaged
     3. WHITE: white LED ON for 20 seconds, repeated spectra averaged
     4. IR: IR LED ON for 20 seconds, repeated spectra averaged
     5. All illumination LEDs OFF

  Spectral correction:
     corrected = illuminated mean - dark mean

  DATA.CSV columns:
     Date,Time,CO2_ppm,Temperature_C,Humidity_pct,LeafTemp_C,Type

  SPECTRA.CSV columns:
     Date,Time,Type,Light,Samples,
     A410,B435,C460,D485,E510,F535,G560,H585,R610,
     I645,S680,J705,T730,U760,V810,W860,K900,L940

  Serial debugging removed to fit Arduino Uno flash.

  Pin connections:
     D2  = Arduino RX from K30 TX
     D3  = Arduino TX to K30 RX
     D7  = MOSFET module IN
     D6  = Push button to GND
     D8  = Active buzzer positive
     D10 = SD card chip select

  I2C devices:
     OLED, SHT41 and RTC remain on the Arduino I2C bus.
     AS7265x and MLX90614 SDA/SCL share the 3.3 V side of the PCA9306.
     MLX90614 I2C address = 0x5A.
*/

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include <Adafruit_SHT4x.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>
#include "SparkFun_AS7265X.h"
#include "kSeries.h"

// ----------------------------------------------------
// RTC selection
// ----------------------------------------------------

#define USE_PCF8523 0

// ----------------------------------------------------
// OLED settings
// ----------------------------------------------------

const byte OLED_I2C_ADDRESS = 0x3C;
SSD1306AsciiWire oled;

// MLX90614 single-point IR thermometer
const byte MLX90614_I2C_ADDRESS = 0x5A;
const byte MLX90614_OBJECT_TEMP_REG = 0x07;

// ----------------------------------------------------
// Pin definitions
// ----------------------------------------------------

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

// ----------------------------------------------------
// Main timing settings
// ----------------------------------------------------

const unsigned long SAMPLE_INTERVAL_MS = 5000UL;
const unsigned long BUZZER_TIME_MS = 1000UL;

const unsigned long T_MEASUREMENT_TIME_MS = 120000UL;
const unsigned long D_MEASUREMENT_TIME_MS = 300000UL;
const unsigned long FLUSH_TIME_MS = 60000UL;

const unsigned long D_PRESS_TIME_MS = 3000UL;
const unsigned long FLUSH_PRESS_TIME_MS = 5000UL;
const unsigned long DEBOUNCE_TIME_MS = 50UL;
const unsigned long HOLD_DISPLAY_INTERVAL_MS = 100UL;

// ----------------------------------------------------
// Spectroscopy timing/settings
// ----------------------------------------------------

const unsigned long SPEC_LIGHT_TIME_MS = 20000UL;
const unsigned long SPEC_SAMPLE_INTERVAL_MS = 2000UL;
const unsigned long DARK_SAMPLE_INTERVAL_MS = 250UL;
const byte DARK_SAMPLE_TARGET = 3;
const byte SPECTRAL_CHANNELS = 18;

// ----------------------------------------------------
// Data files
// ----------------------------------------------------

const char LOG_FILE[] = "DATA.CSV";
const char SPECTRA_FILE[] = "SPECTRA.CSV";

// ----------------------------------------------------
// Sensor objects
// ----------------------------------------------------

kSeries K30(K30_RX_PIN, K30_TX_PIN);
Adafruit_SHT4x sht41;
AS7265X spectralSensor;

#if USE_PCF8523
RTC_PCF8523 rtc;
#else
RTC_DS1307 rtc;
#endif

// ----------------------------------------------------
// Operating states
// ----------------------------------------------------

enum SystemState {
  IDLE,
  HOLDING,
  COUNTDOWN,
  MEASURING,
  FLUSHING
};

SystemState currentState = IDLE;

// ----------------------------------------------------
// Spectroscopy states
// ----------------------------------------------------

enum SpectroscopyState {
  SPEC_IDLE,
  SPEC_DARK,
  SPEC_UV,
  SPEC_WHITE,
  SPEC_IR,
  SPEC_DONE
};

SpectroscopyState specState = SPEC_IDLE;

// ----------------------------------------------------
// Measurement mode
// ----------------------------------------------------

char measurementType = '-';
unsigned long activeMeasurementTimeMs = 0;

// ----------------------------------------------------
// Main timing variables
// ----------------------------------------------------

unsigned long stateStartMs = 0;
unsigned long measurementStartMs = 0;
unsigned long nextSampleMs = 0;

// ----------------------------------------------------
// Button variables
// ----------------------------------------------------

bool lastButtonReading = HIGH;
bool stableButtonState = HIGH;

unsigned long debounceStartMs = 0;
unsigned long buttonPressStartMs = 0;
unsigned long lastHoldDisplayMs = 0;

// ----------------------------------------------------
// SD-card state
// ----------------------------------------------------

bool sdReady = false;
bool spectraFileReady = false;
bool spectralSensorReady = false;

// ----------------------------------------------------
// Spectroscopy variables
// ----------------------------------------------------

unsigned long specPhaseStartMs = 0;
unsigned long nextSpecSampleMs = 0;
unsigned long spectroscopyTimestampUnix = 0;

byte specSamplesTaken = 0;

// darkMean stores the 3-reading dark baseline.
float darkMean[SPECTRAL_CHANNELS];

// specSum stores the current phase sum.
float specSum[SPECTRAL_CHANNELS];

// During illuminated phases, isolated zero readings are ignored only
// when that wavelength also produced one or more positive readings.
byte specNonZeroCount[SPECTRAL_CHANNELS];

// ----------------------------------------------------
// Setup
// ----------------------------------------------------

void setup() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(MOSFET_PIN, OUTPUT);
  pinMode(SD_CS_PIN, OUTPUT);

  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  digitalWrite(MOSFET_PIN, MOSFET_OFF);



  Wire.begin();

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

// ----------------------------------------------------
// Main loop
// ----------------------------------------------------

void loop() {
  unsigned long currentMs = millis();

  checkButton(currentMs);
  updateHoldingDisplay(currentMs);
  updateCountdown(currentMs);
  checkRunEnd(currentMs);

  // Keep the existing gas/SHT41 schedule as the priority task.
  readSensorsWhenDue();

  // Spectroscopy runs as a state machine between normal loop passes.
  updateSpectroscopy(millis());
}

// ----------------------------------------------------
// Initialize OLED
// ----------------------------------------------------

void initializeOLED() {
  oled.begin(&Adafruit128x64, OLED_I2C_ADDRESS);
  oled.setFont(System5x7);
  oled.clear();
  oled.set1X();
}

// ----------------------------------------------------
// Initialize SHT41
// ----------------------------------------------------

void initializeSHT41() {
  if (!sht41.begin()) {
    showCriticalError(F("SHT41"));
    stopSystem();
  }

  sht41.setPrecision(SHT4X_HIGH_PRECISION);
  sht41.setHeater(SHT4X_NO_HEATER);

}

// ----------------------------------------------------
// Initialize RTC
// ----------------------------------------------------

void initializeRTC() {
  if (!rtc.begin()) {
    showCriticalError(F("RTC"));
    stopSystem();
  }

#if USE_PCF8523

  if (!rtc.initialized() || rtc.lostPower()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  rtc.start();

#else

  if (!rtc.isrunning()) {
    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

#endif

}

// ----------------------------------------------------
// Initialize AS7265x
// ----------------------------------------------------

void initializeAS7265x() {
  if (!spectralSensor.begin()) {
    showCriticalError(F("AS7265x"));
    stopSystem();
  }

  spectralSensorReady = true;

  // Keep indicator LED off so it cannot influence optical measurements.
  spectralSensor.disableIndicator();

  // Conservative current that is suitable for UV, white and IR LEDs.
  spectralSensor.setBulbCurrent(
    AS7265X_LED_CURRENT_LIMIT_12_5MA,
    AS7265x_LED_WHITE
  );

  spectralSensor.setBulbCurrent(
    AS7265X_LED_CURRENT_LIMIT_12_5MA,
    AS7265x_LED_IR
  );

  spectralSensor.setBulbCurrent(
    AS7265X_LED_CURRENT_LIMIT_12_5MA,
    AS7265x_LED_UV
  );

  // Same settings that passed your individual AS7265x test.
  spectralSensor.setGain(AS7265X_GAIN_37X);
  spectralSensor.setMeasurementMode(
    AS7265X_MEASUREMENT_MODE_6CHAN_ONE_SHOT
  );
  spectralSensor.setIntegrationCycles(49);

  allSpectralLightsOff();

}

// ----------------------------------------------------
// Initialize MLX90614 leaf-temperature sensor
// ----------------------------------------------------

void initializeMLX90614() {
  Wire.beginTransmission(MLX90614_I2C_ADDRESS);

  if (Wire.endTransmission() != 0) {
    showCriticalError(F("MLX90614"));
    stopSystem();
  }
}

// ----------------------------------------------------
// Initialize SD card and both CSV files
// ----------------------------------------------------

void initializeSD() {
  if (!SD.begin(SD_CS_PIN)) {
    sdReady = false;
    spectraFileReady = false;
    return;
  }

  sdReady = true;

  // DATA.CSV
  File dataFile = SD.open(LOG_FILE, FILE_WRITE);

  if (!dataFile) {
    sdReady = false;
  } else {
    if (dataFile.size() == 0) {
      dataFile.println(
        F("Date,Time,CO2_ppm,Temperature_C,Humidity_pct,LeafTemp_C,Type")
      );
    }
    dataFile.close();
  }

  // SPECTRA.CSV
  File spectraFile = SD.open(SPECTRA_FILE, FILE_WRITE);

  if (!spectraFile) {
    spectraFileReady = false;
  } else {
    spectraFileReady = true;

    if (spectraFile.size() == 0) {
      spectraFile.println(
        F("Date,Time,Type,Light,Samples,A410,B435,C460,D485,E510,F535,G560,H585,R610,I645,S680,J705,T730,U760,V810,W860,K900,L940")
      );
    }

    spectraFile.close();
  }

}

// ----------------------------------------------------
// Button handling
// ----------------------------------------------------

void checkButton(unsigned long currentMs) {
  bool rawReading = digitalRead(BUTTON_PIN);

  if (rawReading != lastButtonReading) {
    debounceStartMs = currentMs;
    lastButtonReading = rawReading;
  }

  if (currentMs - debounceStartMs < DEBOUNCE_TIME_MS) {
    return;
  }

  if (rawReading == stableButtonState) {
    return;
  }

  stableButtonState = rawReading;

  if (stableButtonState == LOW && currentState == IDLE) {
    startButtonHold(currentMs);
    return;
  }

  if (stableButtonState == HIGH && currentState == HOLDING) {
    finishButtonHold(currentMs);
  }
}

// ----------------------------------------------------
// Start timing button hold
// ----------------------------------------------------

void startButtonHold(unsigned long currentMs) {
  currentState = HOLDING;
  buttonPressStartMs = currentMs;
  lastHoldDisplayMs = 0;

  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  digitalWrite(MOSFET_PIN, MOSFET_OFF);
  allSpectralLightsOff();


  showHoldScreen(0);
}

// ----------------------------------------------------
// Update OLED while button is held
// ----------------------------------------------------

void updateHoldingDisplay(unsigned long currentMs) {
  if (currentState != HOLDING) {
    return;
  }

  if (currentMs - lastHoldDisplayMs < HOLD_DISPLAY_INTERVAL_MS) {
    return;
  }

  lastHoldDisplayMs = currentMs;

  unsigned long heldMs = currentMs - buttonPressStartMs;
  showHoldScreen(heldMs);
}

// ----------------------------------------------------
// Select mode when button is released
// ----------------------------------------------------

void finishButtonHold(unsigned long currentMs) {
  unsigned long heldMs = currentMs - buttonPressStartMs;


  if (heldMs >= FLUSH_PRESS_TIME_MS) {
    startFlush(currentMs);
  } else if (heldMs >= D_PRESS_TIME_MS) {
    startCountdown(currentMs, 'D', D_MEASUREMENT_TIME_MS);
  } else {
    startCountdown(currentMs, 'T', T_MEASUREMENT_TIME_MS);
  }
}

// ----------------------------------------------------
// Start buzzer countdown for T or D
// ----------------------------------------------------

void startCountdown(
  unsigned long currentMs,
  char selectedType,
  unsigned long selectedDurationMs
) {
  currentState = COUNTDOWN;
  stateStartMs = currentMs;

  measurementType = selectedType;
  activeMeasurementTimeMs = selectedDurationMs;
  specState = SPEC_IDLE;

  digitalWrite(MOSFET_PIN, MOSFET_OFF);
  digitalWrite(BUZZER_PIN, BUZZER_ON);
  allSpectralLightsOff();


  showCountdownScreen(currentMs);
}

// ----------------------------------------------------
// Finish countdown and start T or D
// ----------------------------------------------------

void updateCountdown(unsigned long currentMs) {
  if (currentState != COUNTDOWN) {
    return;
  }

  if (currentMs - stateStartMs < BUZZER_TIME_MS) {
    return;
  }

  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  digitalWrite(MOSFET_PIN, MOSFET_ON);

  currentState = MEASURING;
  measurementStartMs = currentMs;

  // Force an immediate gas/SHT41 reading.
  nextSampleMs = currentMs;

  // Start one spectroscopy sequence for this T or D run.
  startSpectroscopy(currentMs);

}

// ----------------------------------------------------
// Start FLUSH
// ----------------------------------------------------

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
  oled.println(F("0/60s"));
}

// ----------------------------------------------------
// Stop T, D, or FLUSH when finished
// ----------------------------------------------------

void checkRunEnd(unsigned long currentMs) {
  if (currentState != MEASURING && currentState != FLUSHING) {
    return;
  }

  if (currentMs - measurementStartMs < activeMeasurementTimeMs) {
    return;
  }

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
    oled.print(F("DATA:"));
    oled.println(sdReady ? F("OK") : F("FAIL"));
    oled.print(F("SPEC:"));
    oled.println(spectraFileReady ? F("OK") : F("FAIL"));
  }
  oled.println(F("READY"));
}

// ----------------------------------------------------
// Read gas/SHT41 every five seconds
// ----------------------------------------------------

void readSensorsWhenDue() {
  unsigned long currentMs = millis();

  if ((long)(currentMs - nextSampleMs) < 0) {
    return;
  }

  nextSampleMs += SAMPLE_INTERVAL_MS;

  if ((long)(millis() - nextSampleMs) >= 0) {
    nextSampleMs = millis() + SAMPLE_INTERVAL_MS;
  }

  readAndProcessSensors();
}

// ----------------------------------------------------
// Read, display, print and save gas/SHT41
// ----------------------------------------------------

void readAndProcessSensors() {
  sensors_event_t humidityEvent;
  sensors_event_t temperatureEvent;

  bool shtReadingOK =
    sht41.getEvent(&humidityEvent, &temperatureEvent);

  double co2ppm = K30.getCO2('p');
  if (co2ppm < 0) co2ppm = K30.getCO2('p');
  if (co2ppm < 0) co2ppm = K30.getCO2('p');

  float leafTemp = readLeafTemperature();

  DateTime timestamp = rtc.now();

  unsigned long elapsedMs = 0;
  if (currentState == MEASURING || currentState == FLUSHING) {
    elapsedMs = millis() - measurementStartMs;
  }

  updateOLED(
    timestamp,
    elapsedMs,
    co2ppm,
    temperatureEvent.temperature,
    humidityEvent.relative_humidity,
    shtReadingOK,
    leafTemp
  );

  if (currentState == MEASURING && sdReady) {
    saveMeasurement(
      timestamp,
      co2ppm,
      temperatureEvent.temperature,
      humidityEvent.relative_humidity,
      shtReadingOK,
      leafTemp,
      measurementType
    );
  }
}

// ----------------------------------------------------
// Read MLX90614 object temperature with small retry logic
// ----------------------------------------------------

float readLeafTemperature() {
  for (byte attempt = 0; attempt < 3; attempt++) {
    Wire.beginTransmission(MLX90614_I2C_ADDRESS);
    Wire.write(MLX90614_OBJECT_TEMP_REG);

    if (Wire.endTransmission(false) == 0) {
      byte received = Wire.requestFrom(
        MLX90614_I2C_ADDRESS,
        (byte)3
      );

      if (received == 3 && Wire.available() >= 3) {
        byte lowByte = Wire.read();
        byte highByte = Wire.read();
        Wire.read(); // PEC byte

        uint16_t raw = ((uint16_t)highByte << 8) | lowByte;

        if ((raw & 0x8000) == 0) {
          return (raw * 0.02f) - 273.15f;
        }
      } else {
        while (Wire.available()) Wire.read();
      }
    }

    delay(10);
  }

  return NAN;
}

// ----------------------------------------------------
// Start spectroscopy sequence
// ----------------------------------------------------

void startSpectroscopy(unsigned long currentMs) {
  allSpectralLightsOff();

  spectroscopyTimestampUnix = rtc.now().unixtime();

  clearDarkMean();
  resetSpectrumAccumulator();

  specState = SPEC_DARK;
  specPhaseStartMs = currentMs;
  nextSpecSampleMs = currentMs;

}

// ----------------------------------------------------
// Spectroscopy state machine
// ----------------------------------------------------

void updateSpectroscopy(unsigned long currentMs) {
  if (currentState != MEASURING) {
    return;
  }

  if (specState == SPEC_IDLE || specState == SPEC_DONE) {
    return;
  }

  // DARK: exactly three measurements with all LEDs off.
  if (specState == SPEC_DARK) {
    if ((long)(currentMs - nextSpecSampleMs) >= 0) {
      takeAndAccumulateSpectrum(false);

      if (specSamplesTaken >= DARK_SAMPLE_TARGET) {
        finishDarkPhase();
        startUVPhase(millis());
        return;
      }

      nextSpecSampleMs += DARK_SAMPLE_INTERVAL_MS;

      if ((long)(millis() - nextSpecSampleMs) >= 0) {
        nextSpecSampleMs = millis() + DARK_SAMPLE_INTERVAL_MS;
      }
    }

    return;
  }

  // Illuminated phases finish after 20 seconds.
  if (currentMs - specPhaseStartMs >= SPEC_LIGHT_TIME_MS) {
    if (specState == SPEC_UV) {
      finishIlluminatedPhase(F("UV_CORR"));
      startWhitePhase(millis());
      return;
    }

    if (specState == SPEC_WHITE) {
      finishIlluminatedPhase(F("WHITE_CORR"));
      startIRPhase(millis());
      return;
    }

    if (specState == SPEC_IR) {
      finishIlluminatedPhase(F("IR_CORR"));
      finishSpectroscopy();
      return;
    }
  }

  // Take repeated spectra during UV, WHITE and IR.
  if ((long)(currentMs - nextSpecSampleMs) >= 0) {
    takeAndAccumulateSpectrum(true);

    nextSpecSampleMs += SPEC_SAMPLE_INTERVAL_MS;

    // Prevent a burst of catch-up samples if another task delayed the loop.
    if ((long)(millis() - nextSpecSampleMs) >= 0) {
      nextSpecSampleMs = millis() + SPEC_SAMPLE_INTERVAL_MS;
    }
  }
}

// ----------------------------------------------------
// Finish DARK and calculate the baseline
// ----------------------------------------------------

void finishDarkPhase() {
  if (specSamplesTaken == 0) {
    return;
  }

  for (byte i = 0; i < SPECTRAL_CHANNELS; i++) {
    darkMean[i] = specSum[i] / specSamplesTaken;
  }

  if (spectraFileReady) {
    saveSpectrumRow(F("DARK"), true);
  }

}

// ----------------------------------------------------
// Start UV phase
// ----------------------------------------------------

void startUVPhase(unsigned long currentMs) {
  allSpectralLightsOff();
  resetSpectrumAccumulator();

  spectralSensor.enableBulb(AS7265x_LED_UV);

  specState = SPEC_UV;
  specPhaseStartMs = currentMs;
  nextSpecSampleMs = currentMs;

}

// ----------------------------------------------------
// Start WHITE phase
// ----------------------------------------------------

void startWhitePhase(unsigned long currentMs) {
  allSpectralLightsOff();
  resetSpectrumAccumulator();

  spectralSensor.enableBulb(AS7265x_LED_WHITE);

  specState = SPEC_WHITE;
  specPhaseStartMs = currentMs;
  nextSpecSampleMs = currentMs;

}

// ----------------------------------------------------
// Start IR phase
// ----------------------------------------------------

void startIRPhase(unsigned long currentMs) {
  allSpectralLightsOff();
  resetSpectrumAccumulator();

  spectralSensor.enableBulb(AS7265x_LED_IR);

  specState = SPEC_IR;
  specPhaseStartMs = currentMs;
  nextSpecSampleMs = currentMs;

}

// ----------------------------------------------------
// Finish one illuminated phase and save corrected mean
// ----------------------------------------------------

void finishIlluminatedPhase(const __FlashStringHelper *lightName) {
  allSpectralLightsOff();

  if (spectraFileReady) {
    saveSpectrumRow(lightName, false);
  }

}

// ----------------------------------------------------
// Finish complete spectroscopy sequence
// ----------------------------------------------------

void finishSpectroscopy() {
  allSpectralLightsOff();
  specState = SPEC_DONE;

}

// ----------------------------------------------------
// Take one calibrated 18-channel spectrum
// ----------------------------------------------------

void takeAndAccumulateSpectrum(bool ignoreIsolatedZeros) {
  spectralSensor.takeMeasurements();
  specSamplesTaken++;

  for (byte i = 0; i < SPECTRAL_CHANNELS; i++) {
    float value = readSpectralChannel(i);

    if (!ignoreIsolatedZeros) {
      specSum[i] += value;
    } else if (value > 0.0f) {
      // Ignore isolated exact-zero glitches during illuminated phases.
      // If a channel remains zero for the entire phase, its mean stays zero.
      specSum[i] += value;
      specNonZeroCount[i]++;
    }
  }
}

// ----------------------------------------------------
// Read one calibrated AS7265x channel by wavelength order
// ----------------------------------------------------

float readSpectralChannel(byte i) {
  switch (i) {
    case 0:  return spectralSensor.getCalibratedA(); // 410
    case 1:  return spectralSensor.getCalibratedB(); // 435
    case 2:  return spectralSensor.getCalibratedC(); // 460
    case 3:  return spectralSensor.getCalibratedD(); // 485
    case 4:  return spectralSensor.getCalibratedE(); // 510
    case 5:  return spectralSensor.getCalibratedF(); // 535
    case 6:  return spectralSensor.getCalibratedG(); // 560
    case 7:  return spectralSensor.getCalibratedH(); // 585
    case 8:  return spectralSensor.getCalibratedR(); // 610
    case 9:  return spectralSensor.getCalibratedI(); // 645
    case 10: return spectralSensor.getCalibratedS(); // 680
    case 11: return spectralSensor.getCalibratedJ(); // 705
    case 12: return spectralSensor.getCalibratedT(); // 730
    case 13: return spectralSensor.getCalibratedU(); // 760
    case 14: return spectralSensor.getCalibratedV(); // 810
    case 15: return spectralSensor.getCalibratedW(); // 860
    case 16: return spectralSensor.getCalibratedK(); // 900
    case 17: return spectralSensor.getCalibratedL(); // 940
    default: return 0.0f;
  }
}

// ----------------------------------------------------
// Reset current spectroscopy accumulator
// ----------------------------------------------------

void resetSpectrumAccumulator() {
  specSamplesTaken = 0;

  for (byte i = 0; i < SPECTRAL_CHANNELS; i++) {
    specSum[i] = 0.0f;
    specNonZeroCount[i] = 0;
  }
}

// ----------------------------------------------------
// Clear dark baseline
// ----------------------------------------------------

void clearDarkMean() {
  for (byte i = 0; i < SPECTRAL_CHANNELS; i++) {
    darkMean[i] = 0.0f;
  }
}

// ----------------------------------------------------
// Turn UV, WHITE and IR LEDs off
// ----------------------------------------------------

void allSpectralLightsOff() {
  if (!spectralSensorReady) {
    return;
  }

  spectralSensor.disableBulb(AS7265x_LED_WHITE);
  spectralSensor.disableBulb(AS7265x_LED_IR);
  spectralSensor.disableBulb(AS7265x_LED_UV);
}

// ----------------------------------------------------
// Save one spectroscopy row
// ----------------------------------------------------

void saveSpectrumRow(
  const __FlashStringHelper *lightName,
  bool darkRow
) {
  File f = SD.open(SPECTRA_FILE, FILE_WRITE);

  if (!f) {
    spectraFileReady = false;
    return;
  }

  DateTime timestamp(spectroscopyTimestampUnix);

  printDate(f, timestamp);
  f.print(',');
  printTime(f, timestamp);
  f.print(',');
  f.print(measurementType);
  f.print(',');
  f.print(lightName);
  f.print(',');
  f.print(specSamplesTaken);

  for (byte i = 0; i < SPECTRAL_CHANNELS; i++) {
    float value;

    if (darkRow) {
      value = darkMean[i];
    } else {
      float illuminatedMean = 0.0f;
      if (specNonZeroCount[i] > 0) {
        illuminatedMean = specSum[i] / specNonZeroCount[i];
      }
      value = illuminatedMean - darkMean[i];
    }

    f.print(',');
    f.print(value, 3);
  }

  f.println();
  f.close();
}

// ----------------------------------------------------
// OLED main data screen
// ----------------------------------------------------

void updateOLED(
  const DateTime &timestamp,
  unsigned long elapsedMs,
  double co2ppm,
  float temperature,
  float humidity,
  bool shtReadingOK,
  float leafTemp
) {
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
    oled.print(F("C RH "));
    oled.print(humidity, 1);
    oled.println(F("%"));
  } else {
    oled.println(F("SHT ERR"));
  }

  oled.print(F("LEAF "));
  if (isnan(leafTemp)) {
    oled.println(F("ERR"));
  } else {
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
  } else if (currentState == COUNTDOWN) {
    oled.println(F("PUMP WAIT"));
  } else {
    oled.println(F("HOLD BUTTON"));
    oled.println(F("SPEC READY"));
  }

  oled.print(F("SD "));
  oled.print(sdReady ? F("OK") : F("NO"));
  oled.print(F(" SP "));
  oled.println(spectraFileReady ? F("OK") : F("NO"));
}

// ----------------------------------------------------
// OLED spectroscopy status line
// ----------------------------------------------------

void printSpectroscopyOLEDLine() {
  oled.print(F("SPEC "));

  switch (specState) {
    case SPEC_DARK:
      oled.print(F("DARK "));
      oled.print(specSamplesTaken);
      oled.print('/');
      oled.println(DARK_SAMPLE_TARGET);
      break;
    case SPEC_UV:
      oled.print(F("UV "));
      oled.print((millis() - specPhaseStartMs) / 1000UL);
      oled.println(F("/20"));
      break;
    case SPEC_WHITE:
      oled.print(F("WHITE "));
      oled.print((millis() - specPhaseStartMs) / 1000UL);
      oled.println(F("/20"));
      break;
    case SPEC_IR:
      oled.print(F("IR "));
      oled.print((millis() - specPhaseStartMs) / 1000UL);
      oled.println(F("/20"));
      break;
    case SPEC_DONE:
      oled.println(F("DONE"));
      break;
    default:
      oled.println(F("WAIT"));
      break;
  }
}

// ----------------------------------------------------
// OLED screen while holding button
// ----------------------------------------------------

void showHoldScreen(unsigned long heldMs) {
  oled.clear();
  oled.println(F("HOLD"));
  oled.print(heldMs / 1000.0, 1);
  oled.println(F("s"));

  if (heldMs >= FLUSH_PRESS_TIME_MS) {
    oled.println(F("RELEASE FLUSH"));
  } else if (heldMs >= D_PRESS_TIME_MS) {
    oled.println(F("RELEASE D"));
  } else {
    oled.println(F("RELEASE T"));
  }
}

// ----------------------------------------------------
// OLED countdown screen
// ----------------------------------------------------

void showCountdownScreen(unsigned long currentMs) {
  oled.clear();
  oled.print(measurementType);
  oled.println(F(" COUNTDOWN"));
  oled.println(F("BUZZER ON"));
  oled.println(F("PUMP OFF"));
}

// ----------------------------------------------------
// OLED ready screen
// ----------------------------------------------------

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

// ----------------------------------------------------
// OLED critical-error screen
// ----------------------------------------------------

void showCriticalError(const __FlashStringHelper *name) {
  oled.clear();
  oled.println(F("ERROR"));
  oled.println(name);
  oled.println(F("STOPPED"));
}

// ----------------------------------------------------
// Save one T or D gas measurement to DATA.CSV
// ----------------------------------------------------

void saveMeasurement(
  const DateTime &timestamp,
  double co2ppm,
  float temperature,
  float humidity,
  bool shtReadingOK,
  float leafTemp,
  char type
) {
  File dataFile = SD.open(LOG_FILE, FILE_WRITE);

  if (!dataFile) {
    sdReady = false;
    return;
  }

  printDate(dataFile, timestamp);
  dataFile.print(',');

  printTime(dataFile, timestamp);
  dataFile.print(',');

  if (co2ppm < 0) {
    dataFile.print(F("ERROR"));
  } else {
    dataFile.print(co2ppm, 0);
  }

  dataFile.print(',');

  if (shtReadingOK) {
    dataFile.print(temperature, 2);
  } else {
    dataFile.print(F("ERROR"));
  }

  dataFile.print(',');

  if (shtReadingOK) {
    dataFile.print(humidity, 2);
  } else {
    dataFile.print(F("ERROR"));
  }

  dataFile.print(',');

  if (isnan(leafTemp)) {
    dataFile.print(F("ERROR"));
  } else {
    dataFile.print(leafTemp, 2);
  }

  dataFile.print(',');
  dataFile.println(type);

  dataFile.close();
}

// ----------------------------------------------------
// Date and time printing
// ----------------------------------------------------

void printTwoDigits(Print &output, byte value) {
  if (value < 10) {
    output.print('0');
  }

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



// ----------------------------------------------------
// Stop after critical hardware error
// ----------------------------------------------------

void stopSystem() {
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  digitalWrite(MOSFET_PIN, MOSFET_OFF);
  allSpectralLightsOff();

  while (true) {
    delay(100);
  }
}
