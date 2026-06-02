/*
 * ============================================================
 *  Uplink Logger / Bridge — Incubation Experiment
 * ============================================================
 *  Receives "Canal X;countdown;temp" from the multiplexer over
 *  HC-05 Bluetooth, adds an RTC timestamp, then:
 *    - saves to SD card  (log.csv)
 *    - forwards to the Pi over USB as a clean CSV line
 *
 *  Line sent to Pi : timestamp,channel,countdown,temp
 *  Also forwards    : any "SET <temp>" command from the Pi
 *                     back to the multiplexer over Bluetooth
 *
 *  Hardware : Arduino Uno/Nano
 *             HC-05 Bluetooth (RX=pin 2, TX=pin 3)
 *             Logger shield: RTC (DS1307) + SD (CS=pin 10)
 *  Author   : Wael Al Hamwi
 *  Date     : 02.06.2026 
 * ============================================================
 */

#include <SoftwareSerial.h>
#include <SD.h>
#include <RTClib.h>

SoftwareSerial btSerial(2, 3);   // RX=2, TX=3 (to HC-05)
RTC_DS1307 rtc;
const int chipSelect = 10;

void setup() {
  Serial.begin(9600);     // USB to the Pi
  btSerial.begin(9600);   // HC-05 to the multiplexer

  SD.begin(chipSelect);
  rtc.begin();
  // rtc.adjust(DateTime(F(__DATE__), F(__TIME__))); // run ONCE to set the clock
}

void loop() {
  // --- Forward "SET" commands from the Pi to the multiplexer ---
  if (Serial.available()) {
    String userCmd = Serial.readStringUntil('\n');
    userCmd.trim();
    if (userCmd.length() > 0) {
      btSerial.println(userCmd);   // pass through to multiplexer over BT
    }
  }

  // --- Receive data from the multiplexer, timestamp, log, forward ---
  if (btSerial.available()) {
    String received = btSerial.readStringUntil('\n');
    received.trim();
    if (received.length() == 0) return;

    // Expect "Canal X;countdown;temp"
    int sep1 = received.indexOf(';');
    int sep2 = received.indexOf(';', sep1 + 1);
    if (sep1 == -1 || sep2 == -1) return;   // ignore malformed / confirmations

    String channel   = received.substring(0, sep1);
    String countdown = received.substring(sep1 + 1, sep2);
    String temp      = received.substring(sep2 + 1);
    channel.trim(); countdown.trim(); temp.trim();

    DateTime now = rtc.now();
    String stamp = String(now.year()) + "-"
                 + twoDigit(now.month()) + "-"
                 + twoDigit(now.day()) + " "
                 + twoDigit(now.hour()) + ":"
                 + twoDigit(now.minute()) + ":"
                 + twoDigit(now.second());

    // Clean CSV line: timestamp,channel,countdown,temp
    String logLine = stamp + "," + channel + "," + countdown + "," + temp;

    // Send to the Pi over USB
    Serial.println(logLine);

    // Save to SD card as backup
    File f = SD.open("log.csv", FILE_WRITE);
    if (f) {
      f.println(logLine);
      f.close();
    }
  }
}

String twoDigit(int n) {
  return (n < 10 ? "0" : "") + String(n);
}