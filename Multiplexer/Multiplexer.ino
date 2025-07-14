// Greenhouse Coffens script-Multiplexer
// Written by Wael Al Hamwi & Dr.Mathias Hoffmann 
// Version 0.1



#include <Wire.h>
#include <ThreeWire.h>
// #include <LiquidCrystal_I2C.h>
#include <RtcDS1302.h>
#include <SoftwareSerial.h>

LiquidCrystal_I2C lcd(0x27, 16, 2); // Set the LCD address and dimensions
SoftwareSerial bluetoothSerial(2, 3); // RX, TX pins for HC-05

const int relayCount = 10;  // Number of relays on the board
const int relayPins[] = {22,23,24,25,26,27,28,29,30,31,32,33};  // Digital pins connected to the relays


void setup() {
  Serial.begin(9600);
  bluetoothSerial.begin(38400); // Bluetooth communication
  

  Rtc.Begin();
  lcd.begin();
  lcd.backlight();
  lcd.print("Bluetooth connecting...");
  delay(10000); // wait a 10 seconds 
  lcd.clear();
  delay(30000);
  // Set all relay pins as OUTPUT
  for (int i = 0; i < relayCount; i++) {
    pinMode(relayPins[i], OUTPUT);
    digitalWrite(relayPins[i], HIGH); // Turn OFF all relays at the beginning (active LOW)
  }
}


void loop() {
  
  for (int i = 0; i < relayCount; i++) {

    // Send the active channel information over Bluetooth
    // bluetoothSerial.print("Channel: ");
    bluetoothSerial.println(i + 1);

    digitalWrite(relayPins[i], LOW);
     lcd.setCursor(0, 0);  // Turn ON the relay (active LOW)
     lcd.print("Channel: ");
    Serial.println(i + 1);
     lcd.print(i + 1);     
     lcd.setCursor(0, 1);
    delay(300000);  // Keep the relay ON for 5 minutes
    digitalWrite(relayPins[i], HIGH);  // Turn OFF the relay (active LOW)
    //delay(90000);// Delay of 1 second before turning ON the next relay
     lcd.clear(); 
  }
}






