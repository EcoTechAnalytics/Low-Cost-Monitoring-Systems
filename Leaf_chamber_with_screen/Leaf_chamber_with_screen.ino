/*
  Manual measurement system with 0.96-inch I2C OLED

  Pin connections:
  D2  = Arduino RX from K30 TX
  D3  = Arduino TX to K30 RX
  D7  = MOSFET module IN
  D6  = Push button to GND
  D8  = Active buzzer positive
  D10 = SD card chip select

  I2C devices, all connected in parallel:
  OLED SDA = Arduino SDA / A4
  OLED SCL = Arduino SCL / A5
  SHT41 SDA = Arduino SDA / A4
  SHT41 SCL = Arduino SCL / A5
  RTC SDA = Arduino SDA / A4
  RTC SCL = Arduino SCL / A5

  Default I2C addresses:
  OLED SSD1306 = 0x3C
  SHT41        = 0x44
  DS1307 RTC   = 0x68

  Button behavior:
  1. Press button
  2. Buzzer sounds for 5 seconds
  3. Buzzer stops
  4. MOSFET switches ON
  5. Data is recorded every 5 seconds for 60 seconds
  6. MOSFET switches OFF
  7. Sensors continue being read every 5 seconds
*/

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <RTClib.h>
#include <Adafruit_SHT4x.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>
#include "kSeries.h"

// ----------------------------------------------------
// RTC selection
// ----------------------------------------------------

// Use 0 if your shield has a DS1307 RTC.
// Use 1 if your shield has a PCF8523 RTC.

#define USE_PCF8523 0

// ----------------------------------------------------
// OLED settings
// ----------------------------------------------------

const byte OLED_I2C_ADDRESS = 0x3C;

SSD1306AsciiWire oled;

// ----------------------------------------------------
// Pin definitions
// ----------------------------------------------------

const byte K30_RX_PIN = 2;
const byte K30_TX_PIN = 3;

const byte MOSFET_PIN = 7;
const byte BUTTON_PIN = 6;
const byte BUZZER_PIN = 8;
const byte SD_CS_PIN = 10;

// Change these only if your modules use inverted logic.

const byte MOSFET_ON = HIGH;
const byte MOSFET_OFF = LOW;

const byte BUZZER_ON = HIGH;
const byte BUZZER_OFF = LOW;

// ----------------------------------------------------
// Timing settings
// ----------------------------------------------------

const unsigned long SAMPLE_INTERVAL_MS = 5000;
const unsigned long BUZZER_TIME_MS = 1000;
const unsigned long MEASUREMENT_TIME_MS = 120000;
const unsigned long DEBOUNCE_TIME_MS = 50;

// ----------------------------------------------------
// Data file
// ----------------------------------------------------

const char LOG_FILE[] = "DATA.CSV";

// ----------------------------------------------------
// Sensor objects
// ----------------------------------------------------

kSeries K30(K30_RX_PIN, K30_TX_PIN);

Adafruit_SHT4x sht41;

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
  COUNTDOWN,
  MEASURING
};

SystemState currentState = IDLE;

// ----------------------------------------------------
// Timing variables
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

// ----------------------------------------------------
// SD-card state
// ----------------------------------------------------

bool sdReady = false;

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

  Serial.begin(115200);

  Serial.println();
  Serial.println(F("System starting..."));

  Wire.begin();

  initializeOLED();
  showStartupMessage(F("System starting"), F("Checking hardware"));

  initializeSHT41();
  initializeRTC();
  initializeSD();

  lastButtonReading = digitalRead(BUTTON_PIN);
  stableButtonState = lastButtonReading;

  nextSampleMs = millis();

  Serial.println(F("System ready."));
  Serial.println(F("Press the button to begin a measurement."));
  Serial.println();

  showReadyScreen();
}

// ----------------------------------------------------
// Main loop
// ----------------------------------------------------

void loop() {
  unsigned long currentMs = millis();

  checkButton(currentMs);
  updateCountdown(currentMs);
  checkMeasurementEnd(currentMs);
  readSensorsWhenDue();
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
  Serial.print(F("Checking SHT41... "));
  showStartupMessage(F("Checking SHT41"), F("Please wait"));

  if (!sht41.begin()) {
    Serial.println(F("FAILED"));
    Serial.println(F("SHT41 was not detected."));

    showCriticalError(F("SHT41 FAILED"), F("Check SDA/SCL"));
    stopSystem();
  }

  sht41.setPrecision(SHT4X_HIGH_PRECISION);
  sht41.setHeater(SHT4X_NO_HEATER);

  Serial.println(F("OK"));
}

// ----------------------------------------------------
// Initialize RTC
// ----------------------------------------------------

void initializeRTC() {
  Serial.print(F("Checking RTC... "));
  showStartupMessage(F("Checking RTC"), F("Please wait"));

  if (!rtc.begin()) {
    Serial.println(F("FAILED"));
    Serial.println(F("RTC was not detected."));

    showCriticalError(F("RTC FAILED"), F("Check SDA/SCL"));
    stopSystem();
  }

#if USE_PCF8523

  if (!rtc.initialized() || rtc.lostPower()) {
    Serial.println();
    Serial.println(F("RTC lost power. Setting compile time."));

    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

  rtc.start();

#else

  if (!rtc.isrunning()) {
    Serial.println();
    Serial.println(F("RTC was stopped. Setting compile time."));

    rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
  }

#endif

  Serial.println(F("OK"));
}

// ----------------------------------------------------
// Initialize SD card
// ----------------------------------------------------

void initializeSD() {
  Serial.print(F("Checking SD card... "));
  showStartupMessage(F("Checking SD card"), F("Please wait"));

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println(F("FAILED"));
    Serial.println(F("Serial monitoring will continue."));
    Serial.println(F("Data will not be saved."));

    sdReady = false;
    showStartupMessage(F("SD card FAILED"), F("No data saving"));
    delay(1500);
    return;
  }

  sdReady = true;

  Serial.println(F("OK"));

  File dataFile = SD.open(LOG_FILE, FILE_WRITE);

  if (!dataFile) {
    Serial.println(F("Could not open DATA.CSV."));
    sdReady = false;

    showStartupMessage(F("DATA.CSV error"), F("No data saving"));
    delay(1500);
    return;
  }

  if (dataFile.size() == 0) {
    dataFile.println(
      F("Date,Time,Elapsed_s,CO2_ppm,Temperature_C,Humidity_pct,Status")
    );
  }

  dataFile.close();
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

  if (currentMs - debounceStartMs >= DEBOUNCE_TIME_MS) {
    if (rawReading != stableButtonState) {
      stableButtonState = rawReading;

      if (stableButtonState == LOW && currentState == IDLE) {
        startCountdown(currentMs);
      }
    }
  }
}

// ----------------------------------------------------
// Start five-second buzzer period
// ----------------------------------------------------

void startCountdown(unsigned long currentMs) {
  currentState = COUNTDOWN;
  stateStartMs = currentMs;

  digitalWrite(MOSFET_PIN, MOSFET_OFF);
  digitalWrite(BUZZER_PIN, BUZZER_ON);

  Serial.println();
  Serial.println(F("Button pressed."));
  Serial.println(F("Five-second buzzer period started."));

  oled.clear();
  oled.println(F("STATUS: COUNTDOWN"));
  oled.println();
  oled.println(F("Buzzer: ON"));
  oled.println(F("Pump:   OFF"));
  oled.println();
  oled.println(F("Measurement starts"));
  oled.println(F("after 5 seconds"));
}

// ----------------------------------------------------
// Finish countdown and start measurement
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

  // Force an immediate sensor reading at measurement start.
  nextSampleMs = currentMs;

  Serial.println(F("Buzzer stopped."));
  Serial.println(F("MOSFET switched ON."));
  Serial.println(F("Data logging started."));

  oled.clear();
  oled.println(F("STATUS: MEASURING"));
  oled.println();
  oled.println(F("Buzzer: OFF"));
  oled.println(F("Pump:   ON"));
  oled.println(F("Logging: STARTED"));
  oled.println();
  oled.println(F("Reading sensors..."));
}

// ----------------------------------------------------
// Stop measurement after 60 seconds
// ----------------------------------------------------

void checkMeasurementEnd(unsigned long currentMs) {
  if (currentState != MEASURING) {
    return;
  }

  if (currentMs - measurementStartMs < MEASUREMENT_TIME_MS) {
    return;
  }

  digitalWrite(MOSFET_PIN, MOSFET_OFF);

  currentState = IDLE;

  // Wait five seconds before the next idle reading.
  nextSampleMs = currentMs + SAMPLE_INTERVAL_MS;

  Serial.println(F("Measurement finished."));
  Serial.println(F("MOSFET switched OFF."));
  Serial.println(F("Waiting for the next button press."));
  Serial.println();

  oled.clear();
  oled.println(F("MEASUREMENT DONE"));
  oled.println();
  oled.println(F("Pump: OFF"));

  if (sdReady) {
    oled.println(F("Data: SAVED"));
  } else {
    oled.println(F("Data: NOT SAVED"));
  }

  oled.println();
  oled.println(F("Press button for"));
  oled.println(F("new measurement"));
}

// ----------------------------------------------------
// Read sensors every five seconds
// ----------------------------------------------------

void readSensorsWhenDue() {
  unsigned long currentMs = millis();

  if ((long)(currentMs - nextSampleMs) < 0) {
    return;
  }

  nextSampleMs += SAMPLE_INTERVAL_MS;

  // Prevent rapid repeated readings if the program was delayed.
  if ((long)(millis() - nextSampleMs) >= 0) {
    nextSampleMs = millis() + SAMPLE_INTERVAL_MS;
  }

  readAndProcessSensors();
}

// ----------------------------------------------------
// Read, display, print, and save sensor values
// ----------------------------------------------------

void readAndProcessSensors() {
  sensors_event_t humidityEvent;
  sensors_event_t temperatureEvent;

  bool shtReadingOK =
    sht41.getEvent(&humidityEvent, &temperatureEvent);

  double co2ppm = K30.getCO2('p');
  (co2ppm < 0) ? co2ppm = K30.getCO2('p') : co2ppm; // Re-read if negative value (error check)
  (co2ppm < 0) ? co2ppm = K30.getCO2('p') : co2ppm; // Re-read if negative value (error check)

  DateTime timestamp = rtc.now();

  unsigned long elapsedMs = 0;

  if (currentState == MEASURING) {
    elapsedMs = millis() - measurementStartMs;
  }

  // Update the OLED with the newest values.
  updateOLED(
    timestamp,
    elapsedMs,
    co2ppm,
    temperatureEvent.temperature,
    humidityEvent.relative_humidity,
    shtReadingOK
  );

  // Serial output remains available for troubleshooting.
  printDateTime(Serial, timestamp);

  Serial.print(F(" | CO2: "));

  if (co2ppm < 0) {
    Serial.print(F("ERROR "));
    Serial.print(co2ppm);
  } else {
    Serial.print(co2ppm, 0);
    Serial.print(F(" ppm"));
  }

  Serial.print(F(" | Temperature: "));

  if (shtReadingOK) {
    Serial.print(temperatureEvent.temperature, 2);
    Serial.print(F(" C"));
  } else {
    Serial.print(F("ERROR"));
  }

  Serial.print(F(" | Humidity: "));

  if (shtReadingOK) {
    Serial.print(humidityEvent.relative_humidity, 2);
    Serial.print(F(" %"));
  } else {
    Serial.print(F("ERROR"));
  }

  Serial.print(F(" | Status: "));
  Serial.print(getStateName());

  if (currentState == MEASURING) {
    Serial.print(F(" | Elapsed: "));
    Serial.print(elapsedMs / 1000.0, 1);
    Serial.print(F(" s"));
  }

  Serial.println();

  if (currentState == MEASURING && sdReady) {
    saveMeasurement(
      timestamp,
      elapsedMs,
      co2ppm,
      temperatureEvent.temperature,
      humidityEvent.relative_humidity,
      shtReadingOK
    );
  }
}

// ----------------------------------------------------
// Show current data on OLED
// ----------------------------------------------------

void updateOLED(
  const DateTime &timestamp,
  unsigned long elapsedMs,
  double co2ppm,
  float temperature,
  float humidity,
  bool shtReadingOK
) {
  oled.clear();

  oled.print(F("STATE: "));
  oled.println(getStateName());

  oled.print(F("CO2: "));

  if (co2ppm < 0) {
    oled.println(F("ERROR"));
  } else {
    oled.print(co2ppm, 0);
    oled.println(F(" ppm"));
  }

  oled.print(F("TEMP: "));

  if (shtReadingOK) {
    oled.print(temperature, 1);
    oled.println(F(" C"));
  } else {
    oled.println(F("ERROR"));
  }

  oled.print(F("RH:   "));

  if (shtReadingOK) {
    oled.print(humidity, 1);
    oled.println(F(" %"));
  } else {
    oled.println(F("ERROR"));
  }

  oled.print(F("TIME: "));
  printTime(oled, timestamp);
  oled.println();

  if (currentState == MEASURING) {
    oled.print(F("RUN:  "));
    oled.print(elapsedMs / 1000UL);
    oled.print(F("/"));
    oled.print(MEASUREMENT_TIME_MS / 1000UL);
    oled.println(F(" s"));
  } else if (currentState == COUNTDOWN) {
    oled.println(F("Buzzer ON, pump OFF"));
  } else {
    oled.println(F("Press button to start"));
  }

  oled.print(F("SD:   "));

  if (sdReady) {
    oled.println(F("OK"));
  } else {
    oled.println(F("NOT READY"));
  }
}

// ----------------------------------------------------
// Simple OLED startup message
// ----------------------------------------------------

void showStartupMessage(
  const __FlashStringHelper *line1,
  const __FlashStringHelper *line2
) {
  oled.clear();
  oled.println(line1);
  oled.println();
  oled.println(line2);
}

// ----------------------------------------------------
// OLED ready screen
// ----------------------------------------------------

void showReadyScreen() {
  oled.clear();
  oled.println(F("SYSTEM READY"));
  oled.println();
  oled.println(F("Press button to"));
  oled.println(F("start measurement"));
  oled.println();
  oled.print(F("SD card: "));

  if (sdReady) {
    oled.println(F("OK"));
  } else {
    oled.println(F("FAILED"));
  }
}

// ----------------------------------------------------
// OLED critical-error screen
// ----------------------------------------------------

void showCriticalError(
  const __FlashStringHelper *line1,
  const __FlashStringHelper *line2
) {
  oled.clear();
  oled.println(F("CRITICAL ERROR"));
  oled.println();
  oled.println(line1);
  oled.println(line2);
  oled.println();
  oled.println(F("System stopped"));
}

// ----------------------------------------------------
// Save one measurement to SD card
// ----------------------------------------------------

void saveMeasurement(
  const DateTime &timestamp,
  unsigned long elapsedMs,
  double co2ppm,
  float temperature,
  float humidity,
  bool shtReadingOK
) {
  File dataFile = SD.open(LOG_FILE, FILE_WRITE);

  if (!dataFile) {
    Serial.println(F("ERROR: Could not open DATA.CSV."));
    sdReady = false;
    return;
  }

  printDate(dataFile, timestamp);
  dataFile.print(',');

  printTime(dataFile, timestamp);
  dataFile.print(',');

  // dataFile.print(elapsedMs / 1000.0, 1);
  // dataFile.print(',');

  dataFile.print(co2ppm, 0);
  dataFile.print(',');

  if (shtReadingOK) {
    dataFile.print(temperature, 2);
  } else {
    dataFile.print(F("ERROR"));
  }

  dataFile.print(',');

  if (shtReadingOK) {
    dataFile.println(humidity, 2);
  } else {
    dataFile.println(F("ERROR"));
  }

  // dataFile.print(',');
  // dataFile.println(F("MEASURING"));

  dataFile.close();
}

// ----------------------------------------------------
// Return current state as text
// ----------------------------------------------------

const char *getStateName() {
  switch (currentState) {
    case IDLE:
      return "IDLE";

    case COUNTDOWN:
      return "COUNTDOWN";

    case MEASURING:
      return "MEASURING";

    default:
      return "UNKNOWN";
  }
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

void printDateTime(Print &output, const DateTime &timestamp) {
  printDate(output, timestamp);
  output.print(' ');
  printTime(output, timestamp);
}

// ----------------------------------------------------
// Stop after a critical hardware error
// ----------------------------------------------------

void stopSystem() {
  digitalWrite(BUZZER_PIN, BUZZER_OFF);
  digitalWrite(MOSFET_PIN, MOSFET_OFF);

  while (true) {
    delay(100);
  }
}