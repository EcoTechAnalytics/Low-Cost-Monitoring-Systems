/*
  ============================================================
  Project: Environmental Monitoring Device
  Board:   ESP32 
  Sensor:  SHT41 (Temperature and Humidity)
  Author:  Wael Al Hamwi
  Date:    21.10.2025
  ============================================================

  Description:
  This program reads temperature and humidity data from an SHT41 sensor
  using the I2C interface and sends the readings via Bluetooth every
  3 seconds. The data can be received on any Bluetooth terminal app.

  Connections:
  SHT41 → ESP32
    VCC  → 3.3V
    GND  → GND
    yellow  → GPIO21 (D21)
    white  → GPIO22 (D22)

  Example output (every 3 seconds):
    Temperature: 24.6 °C, Humidity: 48.3 %

  ============================================================
*/

#include <Wire.h>
#include <Adafruit_SHT4x.h>
#include "BluetoothSerial.h"

Adafruit_SHT4x sht4 = Adafruit_SHT4x();
BluetoothSerial SerialBT;

void setup() {
  Serial.begin(115200);
  SerialBT.begin("Kernecker"); // Bluetooth name
  Serial.println("Bluetooth device is ready to pair");
  Wire.begin(22, 21);
  if (!sht4.begin()) {
    Serial.println("Couldn't find SHT4x sensor!");
    while (1) delay(1);
  }

  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  // sht4.setHeater(SHT4X_NO_HEATER);
}

void loop() {
  sensors_event_t humidity, temp;
  sht4.getEvent(&humidity, &temp); // get readings

  float t = temp.temperature;
  float h = humidity.relative_humidity;

  // Print to Serial Monitor
  Serial.print("Temperature: ");
  Serial.print(t);
  Serial.print(" °C, Humidity: ");
  Serial.print(h);
  Serial.println(" %");

  // Send to Bluetooth
  SerialBT.print("Temperature: ");
  SerialBT.print(t);
  SerialBT.print(" °C, Humidity: ");
  SerialBT.print(h);
  SerialBT.println(" %");

  delay(3000); // every 3 seconds
}
