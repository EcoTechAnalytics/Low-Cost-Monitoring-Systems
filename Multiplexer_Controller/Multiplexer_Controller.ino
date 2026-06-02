/*
 * ============================================================
 *  Multiplexer Controller — Incubation Experiment
 * ============================================================
 *  Cycles measurement chambers via relays, reads DS18B20 temp,
 *  controls Peltier cooling relay, and streams data over Serial
 *  (and HC-05 Bluetooth uplink) for remote logging.
 *
 *  Output format : Channel X;countdown;temperature
 *  Temp control  : Peltier relay ON when temp > setpoint
 *                  stable for 10 s (anti-chatter)
 *  Remote control: Send "SET 27" over Bluetooth to change
 *                  the temperature setpoint live (default is 25)
 *  Hardware      : Arduino Mega 2560, 16-ch relay module,
 *                  2-relay Peltier module, hd44780 I2C LCD,
 *                  DS18B20 (pin 3), HC-05 Bluetooth (pins 0,1)
 *  Author        : Wael Al Hamwi
 *  Date          : 02.06.2026
 * ============================================================
 */

#include <Wire.h>
#include <hd44780.h>
#include <hd44780ioClass/hd44780_I2Cexp.h>
#include <OneWire.h>
#include <DallasTemperature.h>

hd44780_I2Cexp lcd;

#define ONE_WIRE_BUS 3
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

// Relay pins, indexed 1..17 (index 0 unused)
const int RELAY_PIN[18] = {
  0,
  34, 35, 32, 33, 30, 31, 28, 29,   // Channels 1-8
  42, 43, 40, 41, 38, 39, 36, 37,   // Channels 9-16
  27                                // Channel 17 (reference)
};

const int PELTIER_RELAY = 37;       // RELAY_PIN_16 drives the Peltier 2-relay module

// Timing (seconds)
const int MeasurementTimeStartCycl = 240;   // Channel 1
const int MeasurementTime          = 180;   // Channels 2-15
const int MeasurementTimeRef       = 120;   // Channel 17 (reference air)
const int RecFreq                  = 1000;  // 1 second per tick

// Temperature control
float     tempRef        = 25.0;   // threshold °C — changeable via "SET" command
const int STABLE_SECONDS = 10;     // must stay above setpoint for this long

int aboveCount = 0;                // counts consecutive seconds above tempRef

void setup() {
  Serial.begin(9600);
  Serial.setTimeout(50);           // don't stall waiting for commands
  sensors.begin();
  lcd.begin(16, 2);
  lcd.setCursor(0, 0);
  lcd.print("MonksHillLab");
  lcd.setCursor(0, 1);
  lcd.print("Incubation System");

  delay(5000);

  for (int i = 1; i <= 17; i++) {
    pinMode(RELAY_PIN[i], OUTPUT);
    digitalWrite(RELAY_PIN[i], HIGH);   // all relays off
  }
  digitalWrite(PELTIER_RELAY, HIGH); 
  
    // --- Wait once for Bluetooth to connect before starting ---
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting BT...");
  for (int s = 30; s > 0; s--) {        // 30-second countdown
    lcd.setCursor(0, 1);
    lcd.print("Wait: ");
    lcd.print(s);
    lcd.print("s  ");
    delay(1000);
  }
  lcd.clear();   // Peltier off at start
}

// Check for an incoming "SET <temp>" command over Bluetooth
void checkForCommand() {
  while (Serial.available()) {                 // read everything waiting
    String cmd = Serial.readStringUntil('\n');
    cmd.trim();                                // removes spaces AND trailing \r

    if (cmd.startsWith("SET")) {               // tolerate "SET 23" or "SET23"
      // grab the number after "SET"
      String numPart = cmd.substring(3);
      numPart.trim();
      float newTemp = numPart.toFloat();

      if (newTemp > 0 && newTemp < 60) {
        tempRef = newTemp;
        aboveCount = 0;                          // reset counters (use your names)

        // confirm back — sent over the same Serial/BT link
        Serial.print("Setpoint now ");
        Serial.print(tempRef);
        Serial.println("C");
      }
    }
  }
}

// Run one Channel for the given number of seconds
void runChannel(int Channel, int seconds) {
  for (int t = seconds; t > 0; t--) {

    // --- Read temperature ---
    sensors.requestTemperatures();
    float temp = sensors.getTempCByIndex(0);

    // --- Check for a new setpoint command ---
    checkForCommand();

    // --- Stable-above-setpoint logic (anti-chatter) ---
    if (temp > tempRef) {
      if (aboveCount < STABLE_SECONDS) aboveCount++;
    } else {
      aboveCount = 0;   // reset as soon as it drops to/below threshold
    }

    // Peltier ON only after 10 stable seconds above setpoint
    if (aboveCount >= STABLE_SECONDS) {
      digitalWrite(PELTIER_RELAY, LOW);   // ON (cooling)
    } else {
      digitalWrite(PELTIER_RELAY, HIGH);  // OFF
    }

    // --- One clean line: Channel X;countdown;temp ---
    Serial.print("Channel ");
    Serial.print(Channel);
    Serial.print(";");
    Serial.print(t);
    Serial.print(";");
    Serial.println(temp);

    // --- LCD ---
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("Channel: ");
    lcd.print(Channel);
    lcd.setCursor(0, 1);
    lcd.print(t);
    lcd.print(" T:");
    lcd.print(temp, 1);
    lcd.print(" S:");
    lcd.print(tempRef, 0);

    // --- Open this Channel & wait 1 second ---
    digitalWrite(RELAY_PIN[Channel], LOW);
    unsigned long time_now = millis();
    while (millis() < time_now + RecFreq) { /* wait */ }
  }
  digitalWrite(RELAY_PIN[Channel], HIGH);   // close Channel
  delay(50);
}

void loop() {
  // Channel 1 (longer start cycle), then reference air
  runChannel(1, MeasurementTimeStartCycl);
  runChannel(17, MeasurementTimeRef);

  // Channels 2-5, reference air after Channel 5
  for (int c = 2; c <= 5; c++) runChannel(c, MeasurementTime);
  runChannel(17, MeasurementTimeRef);

  // Channels 6-10, reference air after Channel 10
  for (int c = 6; c <= 10; c++) runChannel(c, MeasurementTime);
  runChannel(17, MeasurementTimeRef);

  // Channels 11-15, reference air after Channel 15
  for (int c = 11; c <= 15; c++) runChannel(c, MeasurementTime);
  runChannel(17, MeasurementTimeRef);
}