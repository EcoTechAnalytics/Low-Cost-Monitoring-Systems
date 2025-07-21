// Greenhouse Coffens script-INDEPENDENT MODE
// Written by Wael Al Hamwi & Dr.Mathias Hoffmann 
// Version 0.2


// Import the required libraries
#include "Wire.h"
#include <SD.h>
#include <SPI.h>
#include "RTClib.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "kSeries.h" 
#include "Adafruit_SHT31.h"
#include <Arduino.h>


// Define the input PIN
const int chipSelect = 10; // define the PIN for datalogger shield
#define RELAY_PIN 4 // define the PIN for Relay
#define RELAY_PIN_2 5 // define the PIN for Relay
#define Mosfet_PIN 7 // define the PIN for Mosfet
// The closing and opening time is dteremined by multiplying the "Close_time" or "Open_time" by "Measurement_freq". 
//exp --> 60 * 5000 = 300000 Miliseconds = 5 Minutes for opening , 300 * 5000 = 1500000 Miliseconds = 25 Minutes for closing
int Close_time=60;
int Open_time=300;
int Measurement_freq=5000;


RTC_DS1307 rtc; // Initialize the real-time clock object
String timestring; // String to store the formatted timestamp
Adafruit_SHT31 sht31 = Adafruit_SHT31(); // Create an object for the SHT31 sensor
kSeries K_30_Serial(3,2);  // Sets up a virtual serial port for K-30 CO2 sensor


int valMultiplier = 1; // Multiplier for value. Default is 1. Set to 3 for K-30 3% and 10 for K-33 ICB
unsigned long previousMillis = 0;
const long interval = 3000; // The interval in milliseconds between CO2 readings and data logging

void setup()
{
  Serial.begin(9600);// Initialize the Serial communication with a baud rate of 9600

  delay(1000); // Wait for 1 second to allow devices to initialize
  Serial.print("Initializing SD card...");
  rtc.begin(); // Initialize the real-time clock

  // Check if the SD card is present and can be initialized:
  if (!SD.begin(chipSelect)) {
    Serial.println("Card failed, or not present");
    // If the SD card cannot be initialized, enter an endless loop:
    while (1);
  }
  Serial.println("card initialized.");
  
  Wire.begin(); // Initialize I2C communication
  sht31.begin(0x44); // Initialize the SHT31 sensor with the I2C address 0x44


  Wire.setClock(100000); // Set I2C clock speed to 100kHz
  pinMode(flowMeterPin, INPUT_PULLUP); // Set flow meter pin as INPUT_PULLUP
  pinMode(RELAY_PIN, OUTPUT); // Set RELAY_PIN as an output pin
  pinMode(RELAY_PIN_2, OUTPUT); // Set RELAY_PIN_2 as an output pin
  pinMode(Mosfet_PIN, OUTPUT); // Set Mosfet_PIN as an output pin
}

void loop()
{
 int i=0;
 while(i<Close_time) {
    digitalWrite(RELAY_PIN, LOW); // Turn the RELAY_PIN on (low state)
    digitalWrite(RELAY_PIN_2, HIGH); // Turn the RELAY_PIN_2 off (high state)
    digitalWrite(Mosfet_PIN, LOW); // Turn the Mosfet_PIN on (low state)

    unsigned long currentMillis = millis(); // Get the current time in milliseconds
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis; 
      double co2 = K_30_Serial.getCO2('p'); // Read CO2 concentration from the K-30 CO2 sensor ('p' for ppm)
      (co2 < 0) ? co2 = K_30_Serial.getCO2('p') : co2; // Re-read if negative value (error check)
      (co2 < 0) ? co2 = K_30_Serial.getCO2('p') : co2; // Re-read if negative value (error check)
    
      float t = sht31.readTemperature(); // Read temperature from the SHT31 sensor
      float h = sht31.readHumidity(); // Read humidity from the SHT31 sensor

      DateTime now = rtc.now(); // Get the current date and time from the real-time clock
      timestring = now.day();
      timestring += "-";
      timestring += now.month();
      timestring += "-";
      timestring += now.year();
      timestring += " ";
      timestring += now.hour();
      timestring += ":";
      timestring += now.minute();
      timestring += ":";
      timestring += now.second();
      Serial.print(timestring); // Print the formatted timestamp to the Serial monitor
      Serial.print(";");
      Serial.print("1"); // Indicator for the coffen being open (0 for open, 1 for closed)
      Serial.print(";");
      Serial.print(t); // Print temperature to the Serial monitor
      Serial.print(";");
      Serial.print(h); // Print humidity to the Serial monitor
      Serial.print(";");  
  	  Serial.print(co2); // Print CO2 concentration to the Serial monitor

   
  
      File dataFile = SD.open("datalog.txt", FILE_WRITE);

     // If the file is available, write the data to it:
      if (dataFile) {

    
    
        dataFile.print(timestring); // Write the timestamp to the file
        dataFile.print(";");
        dataFile.print(t); // Write the temperature to the file
        dataFile.print(";");
        dataFile.print(h); // Write the humidity to the file
        dataFile.print(";");
        dataFile.print("1"); // Write the indicator for coffen open to the file
        dataFile.print(";");
        dataFile.println(co2); // Write the CO2 concentration to the file

        dataFile.close();
        }

      
       // If the file isn't open, print an error message to the Serial monitor:
      else {
        Serial.println("error opening datalog.txt");
      }


    }
  Serial.print(";"); 
  Serial.println(i); 
  i++;
  delay(Measurement_freq); // Delay for 3 seconds between iterations
  }
  
 int j=0;
 while(j<Open_time) {
    digitalWrite(RELAY_PIN, HIGH);  // Turn the RELAY_PIN off (high state)
    digitalWrite(RELAY_PIN_2, LOW); // Turn the RELAY_PIN_2 on (low state)
    digitalWrite(Mosfet_PIN, HIGH); // Turn the Mosfet_PIN off (high state)
    //digitalWrite(Mosfet_CO2, HIGH); // Turn the Mosfet_CO2 off (high state)

    static int negativeCO2Count = 0;
    
    unsigned long currentMillis = millis(); // Get the current time in milliseconds
    if (currentMillis - previousMillis >= interval) {
      previousMillis = currentMillis; 
      double co2 = K_30_Serial.getCO2('p'); // Read CO2 concentration from the K-30 CO2 sensor ('p' for ppm)
      (co2 < 0) ? co2 = K_30_Serial.getCO2('p') : co2; // Re-read if negative value (error check)
      (co2 < 0) ? co2 = K_30_Serial.getCO2('p') : co2; // Re-read if negative value (error check) 

      if (co2 < 0) { // securety Loop for resarting the CO2 sensor in case of the sensor faild 
        negativeCO2Count++; // Increment the negative CO2 counter
        if (negativeCO2Count >= 3) { // if there are 3 negative CO2 value,do a restart for the sensor
          delay(1000); // delay for 1 seconds
          //digitalWrite(Mosfet_CO2, HIGH);
          double co2 = K_30_Serial.getCO2('p'); // Read CO2 concentration from the K-30 CO2 sensor ('p' for ppm)
          (co2 < 0) ? co2 = K_30_Serial.getCO2('p') : co2; // Re-read if negative value (error check)
          (co2 < 0) ? co2 = K_30_Serial.getCO2('p') : co2; // Re-read if negative value (error check) 
        }
      } else {
        //digitalWrite(Mosfet_CO2, HIGH); // Turn on the Mosfet_PIN (high state)
        negativeCO2Count = 0; // Reset the negative CO2 counter
      }   

      float t = sht31.readTemperature(); // Read humidity from the SHT31 sensor
      float h = sht31.readHumidity(); // Read humidity from the SHT31 sensor

      DateTime now = rtc.now(); // Get the current date and time from the real-time clock
      timestring = now.day();
      timestring += "-";
      timestring += now.month();
      timestring += "-";
      timestring += now.year();
      timestring += " ";
      timestring += now.hour();
      timestring += ":";
      timestring += now.minute();
      timestring += ":";
      timestring += now.second();
      Serial.print(timestring); // Print the formatted timestamp to the Serial monitor
      Serial.print(";");
      Serial.print("0"); // Indicator for the coffen being closed (0 for open, 1 for closed)
      Serial.print(";");
      Serial.print(t); // Print temperature to the Serial monitor
      Serial.print(";");
      Serial.print(h); // Print humidity to the Serial monitor
      Serial.print(";");
      Serial.print(co2); // Print CO2 concentration to the Serial monitor


   
  
      File dataFile = SD.open("datalog.txt", FILE_WRITE); // Open the data log file in write mode

     // if the file is available, write to it:
      if (dataFile) {

        dataFile.print(timestring); // Write the timestamp to the file
        dataFile.print(";");
        dataFile.print(t); // Write the temperature to the file
        dataFile.print(";");
        dataFile.print(h); // Write the humidity to the file
        dataFile.print(";");
        dataFile.print("0"); // Write the indicator for coffen Open to the file
        dataFile.print(";");
        dataFile.println(co2); // Write the CO2 concentration to the file


        dataFile.close(); // Close the file
        }

      
      // If the file isn't open, print an error message to the Serial monitor:
      else {
        Serial.println("error opening datalog.txt");
      }

    }
  Serial.print(";"); 
  Serial.println(j); 
  j++;
  delay(Measurement_freq);  // Delay between iterations
  }

}

