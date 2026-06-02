// PAR sensor with 16-channel analog multiplexer
// Reads PAR with 4 different resistor channels
// Saves time and PAR values to SD card

#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include "RTClib.h"

// SD card
const int chipSelect = 10;

// RTC
RTC_DS1307 rtc;
String timestring;

// PAR sensor
const int photodiodePin = A2;
const float responsivity_microA_per_lux = 0.0075 / 1000000.0;

// Multiplexer control pins
const int S0 = 2;
const int S1 = 3;
const int S2 = 4;
const int S3 = 5;

// Select multiplexer channel
void selectMuxChannel(int channel) {
  digitalWrite(S0, bitRead(channel, 0));
  digitalWrite(S1, bitRead(channel, 1));
  digitalWrite(S2, bitRead(channel, 2));
  digitalWrite(S3, bitRead(channel, 3));
}

// Read PAR using selected resistor channel
float readPAR(int channel, float resistorValue) {
  selectMuxChannel(channel);
  delay(20);

  int rawAnalogValue = analogRead(photodiodePin);
  float voltage = rawAnalogValue * (5.0 / 1023.0);
  float photocurrent_A = voltage / resistorValue;
  float illuminance_lux = photocurrent_A / responsivity_microA_per_lux;
  float illuminance_PAR = illuminance_lux / 1000.0 * 18.0;

  return illuminance_PAR;
}

void setup() {
  Serial.begin(9600);
  analogReference(DEFAULT);

  // Multiplexer pins
  pinMode(S0, OUTPUT);
  pinMode(S1, OUTPUT);
  pinMode(S2, OUTPUT);
  pinMode(S3, OUTPUT);

  // I2C and RTC
  Wire.begin();
  rtc.begin();

  // SD card
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    while (1);
  }
  Serial.println("card initialized.");
}

void loop() {
  // Read PAR for each resistor
  float par_1_5k = readPAR(4, 1500.0);
  float par_2_2k = readPAR(6, 2200.0);
  float par_4_7k = readPAR(8, 4700.0);
  float par_10k  = readPAR(10, 10000.0);

  // Time
  DateTime now = rtc.now();
  timestring = String(now.day()) + "-" +
               String(now.month()) + "-" +
               String(now.year()) + " " +
               String(now.hour()) + ":" +
               String(now.minute()) + ":" +
               String(now.second());

  // Serial monitor
  Serial.print(timestring);
  Serial.print(";");
  Serial.print(par_1_5k, 2);
  Serial.print(";");
  Serial.print(par_2_2k, 2);
  Serial.print(";");
  Serial.print(par_4_7k, 2);
  Serial.print(";");
  Serial.println(par_10k, 2);

  // Save to SD card
  File dataFile = SD.open("parlog.txt", FILE_WRITE);

  if (dataFile) {
    dataFile.print(timestring);
    dataFile.print(";");
    dataFile.print(par_1_5k, 2);
    dataFile.print(";");
    dataFile.print(par_2_2k, 2);
    dataFile.print(";");
    dataFile.print(par_4_7k, 2);
    dataFile.print(";");
    dataFile.println(par_10k, 2);
    dataFile.close();
  } else {
    Serial.println("error opening parlog.txt");
  }

  delay(5000);
}