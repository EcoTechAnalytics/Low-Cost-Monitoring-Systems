/*
 * SparkFun Spectral Triad AS7265x
 * Using official SparkFun library
 * SDA → A4, SCL → A5, 3V3 → 3.3V, GND → GND
 */

#include "SparkFun_AS7265X.h"

AS7265X sensor;

const int WAVELENGTHS[18] = {
  410, 435, 460, 485, 510, 535,
  560, 585, 610, 645, 680, 705,
  730, 760, 810, 860, 900, 940
};

void printSpectrum(const char* label) {
  float ch[18] = {
    sensor.getCalibratedA(), sensor.getCalibratedB(),
    sensor.getCalibratedC(), sensor.getCalibratedD(),
    sensor.getCalibratedE(), sensor.getCalibratedF(),
    sensor.getCalibratedG(), sensor.getCalibratedH(),
    sensor.getCalibratedI(), sensor.getCalibratedJ(),
    sensor.getCalibratedK(), sensor.getCalibratedL(),
    sensor.getCalibratedR(), sensor.getCalibratedS(),
    sensor.getCalibratedT(), sensor.getCalibratedU(),
    sensor.getCalibratedV(), sensor.getCalibratedW()
  };

  // Find max for auto-scaling
  float maxVal = 0;
  for (int i = 0; i < 18; i++)
    if (ch[i] > maxVal) maxVal = ch[i];
  if (maxVal < 1) maxVal = 1;

  Serial.println();
  Serial.print("-- "); Serial.print(label); Serial.println(" --");
  Serial.println("  nm  |   Value   | Bar");
  Serial.println("------+-----------+--------------------");

  for (int i = 0; i < 18; i++) {
    Serial.print("  ");
    if (WAVELENGTHS[i] < 1000) Serial.print(" ");
    Serial.print(WAVELENGTHS[i]);
    Serial.print(" | ");
    if (ch[i] < 1000) Serial.print(" ");
    if (ch[i] < 100)  Serial.print(" ");
    if (ch[i] < 10)   Serial.print(" ");
    Serial.print(ch[i], 1);
    Serial.print("  | ");
    int bars = (int)(ch[i] / maxVal * 20.0f);
    bars = constrain(bars, 0, 20);
    for (int b = 0; b < bars;  b++) Serial.print('#');
    for (int b = bars; b < 20; b++) Serial.print('.');
    Serial.println();
  }

  // Find and print peak
  float peak = -1; int peakWl = 0;
  for (int i = 0; i < 18; i++)
    if (ch[i] > peak) { peak = ch[i]; peakWl = WAVELENGTHS[i]; }
  Serial.print("  Peak: "); Serial.print(peakWl); Serial.println(" nm");
}

void setup() {
  Serial.begin(115200);
  while (!Serial);
  delay(500);

  Serial.println("=== SparkFun Spectral Triad AS7265x ===");
  Serial.println();

  if (sensor.begin() == false) {
    Serial.println("ERROR: Sensor not found. Check wiring.");
    while (1);
  }
  Serial.println("Sensor OK!");
  Serial.println();
}

void loop() {
  // TEST 1: White LED
  Serial.println("[TEST 1] White LED");
  sensor.enableBulb(AS7265x_LED_WHITE);
  sensor.takeMeasurementsWithBulb();
  sensor.disableBulb(AS7265x_LED_WHITE);
  printSpectrum("White LED");
  delay(500);

  // TEST 2: UV LED
  Serial.println("[TEST 2] UV LED");
  sensor.enableBulb(AS7265x_LED_UV);
  sensor.takeMeasurementsWithBulb();
  sensor.disableBulb(AS7265x_LED_UV);
  printSpectrum("UV LED");
  delay(500);

  // TEST 3: IR LED
  Serial.println("[TEST 3] IR LED");
  sensor.enableBulb(AS7265x_LED_IR);
  sensor.takeMeasurementsWithBulb();
  sensor.disableBulb(AS7265x_LED_IR);
  printSpectrum("IR LED");
  delay(500);

  // TEST 4: All LEDs
  Serial.println("[TEST 4] All LEDs");
  sensor.enableBulb(AS7265x_LED_WHITE);
  sensor.enableBulb(AS7265x_LED_UV);
  sensor.enableBulb(AS7265x_LED_IR);
  sensor.takeMeasurementsWithBulb();
  sensor.disableBulb(AS7265x_LED_WHITE);
  sensor.disableBulb(AS7265x_LED_UV);
  sensor.disableBulb(AS7265x_LED_IR);
  printSpectrum("All LEDs");

  Serial.println();
  Serial.println("Cycle done. Next in 5s...");
  delay(5000);
}