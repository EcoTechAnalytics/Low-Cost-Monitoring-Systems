/*
 * AS7265x Calibrated Vegetation Monitor
 * FAST version - ~3 seconds per measurement
 * RAM-optimized for Arduino Uno
 * Commands: m=measure  c=calibrate  s=show cal
 */

#include "SparkFun_AS7265X.h"
#include <EEPROM.h>

AS7265X sensor;

#define EEPROM_MAGIC 0xAB
#define EEPROM_ADDR  0

struct CalData {
  uint8_t magic;
  float   drk[18];
  float   wht[18];
};
CalData cal;

float raw[18];
float refl[18];

const int WL[18] PROGMEM = {
  410,435,460,485,510,535,
  560,585,610,645,680,705,
  730,760,810,860,900,940
};

#define iB   1
#define iG   5
#define iR   9
#define iRE  11
#define iNIR 13

// ── EEPROM ────────────────────────────────────────────────────

void saveCal() {
  cal.magic = EEPROM_MAGIC;
  EEPROM.put(EEPROM_ADDR, cal);
  Serial.println(F("  Saved to EEPROM."));
}

bool loadCal() {
  EEPROM.get(EEPROM_ADDR, cal);
  return cal.magic == EEPROM_MAGIC;
}

// ── Fast sensor config ────────────────────────────────────────
// Integration time: value * 2.8ms
// 1  =   2.8ms  (fastest, more noise)
// 10 =  28ms    (good balance)
// 50 = 140ms    (slow but accurate)
#define INTEG_TIME  10   // 28ms — fast and clean enough
#define GAIN        1    // 0=1x  1=3.7x  2=16x  3=64x

void configureSensor() {
  sensor.setIntegrationCycles(INTEG_TIME);
  sensor.setGain(GAIN);
}

// ── Sensor reading ────────────────────────────────────────────

void readInto(float* t, bool withLEDs) {
  if (withLEDs) {
    sensor.enableBulb(AS7265x_LED_WHITE);
    sensor.enableBulb(AS7265x_LED_UV);
    sensor.enableBulb(AS7265x_LED_IR);
    sensor.takeMeasurementsWithBulb();
    sensor.disableBulb(AS7265x_LED_WHITE);
    sensor.disableBulb(AS7265x_LED_UV);
    sensor.disableBulb(AS7265x_LED_IR);
  } else {
    sensor.takeMeasurements();
  }
  t[0] =sensor.getCalibratedA(); t[1] =sensor.getCalibratedB();
  t[2] =sensor.getCalibratedC(); t[3] =sensor.getCalibratedD();
  t[4] =sensor.getCalibratedE(); t[5] =sensor.getCalibratedF();
  t[6] =sensor.getCalibratedG(); t[7] =sensor.getCalibratedH();
  t[8] =sensor.getCalibratedI(); t[9] =sensor.getCalibratedJ();
  t[10]=sensor.getCalibratedK(); t[11]=sensor.getCalibratedL();
  t[12]=sensor.getCalibratedR(); t[13]=sensor.getCalibratedS();
  t[14]=sensor.getCalibratedT(); t[15]=sensor.getCalibratedU();
  t[16]=sensor.getCalibratedV(); t[17]=sensor.getCalibratedW();
}

void calcRefl() {
  for (int i = 0; i < 18; i++) {
    float d = cal.wht[i] - cal.drk[i];
    refl[i] = (d < 0.001) ? 0 :
              constrain((raw[i] - cal.drk[i]) / d, 0.0f, 1.5f);
  }
}

// ── Wait for Enter ────────────────────────────────────────────

void waitEnter() {
  Serial.println(F("  --> Type anything + Enter"));
  while (Serial.available()) Serial.read();
  while (!Serial.available());
  while (Serial.available()) Serial.read();
  delay(100);
}

// ── Calibration ───────────────────────────────────────────────

void runCal() {
  Serial.println(F("\n=== CALIBRATION ==="));

  Serial.println(F("\nSTEP 1: DARK"));
  Serial.println(F("Cover sensor - NO light!"));
  waitEnter();
  Serial.println(F("  Reading..."));
  readInto(cal.drk, false);
  Serial.println(F("  Done."));

  Serial.println(F("\nSTEP 2: WHITE REFERENCE"));
  Serial.println(F("Place WHITE BOARD on sensor."));
  waitEnter();
  Serial.println(F("  Reading..."));
  readInto(cal.wht, true);
  Serial.println(F("  Done."));

  saveCal();
  Serial.println(F("=== CALIBRATION SAVED ==="));
}

// ── Indices ───────────────────────────────────────────────────

float nd(float a, float b){ float d=a+b; return d==0?0:(a-b)/d; }
float NDVI() { return nd(refl[iNIR],refl[iR]); }
float EVI()  { float d=refl[iNIR]+6*refl[iR]-7.5*refl[iB]+1; return d==0?0:2.5*(refl[iNIR]-refl[iR])/d; }
float SAVI() { float d=refl[iNIR]+refl[iR]+0.5; return d==0?0:((refl[iNIR]-refl[iR])/d)*1.5; }
float GNDVI(){ return nd(refl[iNIR],refl[iG]); }
float CIre() { return refl[iRE]==0?0:(refl[iNIR]/refl[iRE])-1; }
float NDRE() { return nd(refl[iNIR],refl[iRE]); }
float ARVI() { float rb=2*refl[iR]-refl[iB]; float d=refl[iNIR]+rb; return d==0?0:(refl[iNIR]-rb)/d; }
float ARI()  { return (refl[iG]==0||refl[iRE]==0)?0:(1.0/refl[iG])-(1.0/refl[iRE]); }
float SIPI() { float d=refl[iNIR]-refl[iR]; return d==0?0:(refl[iNIR]-refl[iB])/d; }
float CVI()  { return refl[iG]==0?0:refl[iNIR]*(refl[iR]/(refl[iG]*refl[iG])); }
float WI()   { return refl[17]==0?0:refl[16]/refl[17]; }
float NDWI() { return nd(refl[iG],refl[iNIR]); }

// ── Display ───────────────────────────────────────────────────

void prow(const __FlashStringHelper* n, float v,
          const __FlashStringHelper* interp) {
  Serial.print(F("  "));
  Serial.print(n);
  Serial.print(F("\t| "));
  Serial.print(v, 4);
  Serial.print(F("\t| "));
  Serial.println(interp);
}

void printSpectrum() {
  float mx = 0;
  for (int i=0;i<18;i++) if(refl[i]>mx) mx=refl[i];
  if (mx<0.001) mx=1;
  Serial.println(F("\n  REFLECTANCE (0=absorb 1=reflect)"));
  Serial.println(F("  nm   | Refl  | Bar"));
  Serial.println(F("  -----+-------+--------------------"));
  for (int i=0;i<18;i++) {
    int w = pgm_read_word(&WL[i]);
    Serial.print(F("  "));
    if(w<1000) Serial.print(' ');
    Serial.print(w);
    Serial.print(F(" | "));
    Serial.print(refl[i],3);
    Serial.print(F(" | "));
    int b=constrain((int)(refl[i]/mx*20),0,20);
    for(int j=0;j<b;j++)  Serial.print('#');
    for(int j=b;j<20;j++) Serial.print('.');
    Serial.println();
  }
}

void printIndices() {
  float ndvi=NDVI(),evi=EVI(),savi=SAVI(),gndvi=GNDVI();
  float cire=CIre(),ndre=NDRE(),arvi=ARVI(),ari=ARI();
  float sipi=SIPI(),cvi=CVI(),wi=WI(),ndwi=NDWI();

  Serial.println(F("\n  VEGETATION INDICES"));
  Serial.println(F("  Index\t\t| Value\t| Interpretation"));
  Serial.println(F("  --------------+-------+------------------"));
  prow(F("NDVI"),     ndvi, ndvi>0.6?F("Dense healthy"):ndvi>0.4?F("Moderate veg"):ndvi>0.2?F("Sparse veg"):F("Non-vegetation"));
  prow(F("EVI"),      evi,  evi>0.5?F("High biomass"):evi>0.2?F("Moderate biomass"):F("Low/non-veg"));
  prow(F("SAVI"),     savi, savi>0.5?F("Dense veg"):savi>0.2?F("Moderate veg"):F("Sparse/bare"));
  prow(F("GNDVI"),    gndvi,gndvi>0.3?F("Good chlorophyll"):F("Low chlorophyll"));
  prow(F("CIrededge"),cire, cire>2.0?F("High chlorophyll"):F("Low-mod chl"));
  prow(F("NDRE"),     ndre, ndre>0.2?F("Good N content"):F("Low N content"));
  prow(F("ARVI"),     arvi, arvi>0.3?F("Healthy veg"):F("Stressed/sparse"));
  prow(F("ARI"),      ari,  ari<0.001?F("Low anthocyanin"):ari<0.003?F("Mod anthocyanin"):F("High anthocyanin"));
  prow(F("SIPI"),     sipi, sipi<0.8?F("Very healthy"):sipi<1.2?F("Normal"):sipi<1.8?F("Mild stress"):F("High stress"));
  prow(F("CVI"),      cvi,  cvi>2.0?F("High chl"):cvi>1.0?F("Moderate chl"):F("Low chl"));
  Serial.println(F("  --- WATER ---"));
  prow(F("WI"),       wi,   wi>1.1?F("Well watered"):wi>0.9?F("Mild stress"):wi>0.7?F("Moderate stress"):F("Severe stress"));
  prow(F("NDWI"),     ndwi, ndwi>0.3?F("High water"):ndwi>0?F("Moderate water"):F("Dry/stressed"));
}

void printMenu() {
  Serial.println(F("\n=== COMMANDS ==="));
  Serial.println(F("m = Measure leaf"));
  Serial.println(F("c = Recalibrate"));
  Serial.println(F("s = Show calibration"));
  Serial.println(F("================"));
}

// ─────────────────────────────────────────────────────────────

void setup() {
  Serial.begin(115200);
  while (!Serial);

  Serial.println(F("=== AS7265x Monitor ==="));

  if (!sensor.begin()) {
    Serial.println(F("ERROR: Sensor not found!"));
    while(1);
  }

  // ── KEY FIX: set fast integration time ───────────────────
  configureSensor();

  Serial.println(F("Sensor OK!"));
  Serial.print(F("Integration time: "));
  Serial.print(INTEG_TIME * 2.8);
  Serial.println(F(" ms per device (~3s total)"));

  if (loadCal()) {
    Serial.println(F("Calibration loaded from EEPROM."));
  } else {
    Serial.println(F("No calibration - starting now..."));
    runCal();
  }
  printMenu();
}

void loop() {
  if (!Serial.available()) return;

  char cmd = Serial.read();
  while (Serial.available()) Serial.read();

  if (cmd=='m'||cmd=='M') {
    Serial.println(F("\nPlace leaf under sensor."));
    waitEnter();
    Serial.println(F("Measuring..."));
    readInto(raw, true);
    calcRefl();
    printSpectrum();
    printIndices();
    printMenu();
  }
  else if (cmd=='c'||cmd=='C') {
    runCal();
    printMenu();
  }
  else if (cmd=='s'||cmd=='S') {
    Serial.println(F("\n  nm  | Dark  | White"));
    Serial.println(F("  ----+-------+-------"));
    for (int i=0;i<18;i++) {
      int w=pgm_read_word(&WL[i]);
      Serial.print(F("  "));
      if(w<1000) Serial.print(' ');
      Serial.print(w);
      Serial.print(F(" | "));
      Serial.print(cal.drk[i],1);
      Serial.print(F("\t| "));
      Serial.println(cal.wht[i],1);
    }
    printMenu();
  }
  else {
    Serial.println(F("Unknown. Use m/c/s"));
    printMenu();
  }
}