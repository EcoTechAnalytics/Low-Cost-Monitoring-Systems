//Script for manual chmaber minion with K30FR, SHT31 and PAR sensor by Mathias Hoffmann 
//Version 1.0
//30.10.2024

// Import the required libraries
#include "Wire.h"
#include <SD.h>
#include <SPI.h>
#include "RTClib.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "kSeries.h" 
#include <Adafruit_SHT4x.h>
#include <Adafruit_BMP280.h>
#include <Arduino.h>
#include <SSD1306Ascii.h>
#include <SSD1306AsciiWire.h>

SSD1306AsciiWire oled;
const int chipSelect = 10; // define the PIN for datalogger shield
String timestring; // String to store the formatted timestamp

int valMultiplier = 1; // Multiplier for value. Default is 1. Set to 3 for K-30 3% and 10 for K-33 ICB
unsigned long previousMillis = 0;
const long interval = 3000; // The interval in milliseconds between CO2 readings and data logging


RTC_DS1307 rtc; // Initialize the real-time clock object
Adafruit_SHT4x sht4 = Adafruit_SHT4x(); // Create an object for the SHT31 sensor
kSeries K_30_Serial(3,2);  // Sets up a virtual serial port for K-30 CO2 sensor
Adafruit_BMP280 bmp;  //I2C

void setup()
{
  Serial.begin(9600);// Initialize the Serial communication with a baud rate of 9600
  analogReference(DEFAULT);
 

  // Initialize the real-time clock
  rtc.begin();
  Wire.setClock(100000); // Set I2C clock speed to 100kHz
  Wire.begin(); // Initialize I2C communication
  bmp.begin(0x76);


  // Check if the SD card is present and can be initialized: 
  if (!SD.begin(chipSelect)) {
  Serial.println("Card failed, or not present");
  // If the SD card cannot be initialized, enter an endless loop:
    while (1);
  }
  Serial.println("card initialized.");
  

  Wire.begin();// Initialize I2C commmunication 
  if (!sht4.begin()) {
    Serial.println("Couldn't find SHT4x sensor!");
    while (1) delay(10);
  }

  Serial.println("SHT4x sensor initialized.");
  
  sht4.setPrecision(SHT4X_HIGH_PRECISION);
  oled.begin(&Adafruit128x64, 0x3C);  // 0x3C is common I2C address
  oled.setFont(fixed_bold10x15);            // Light font, fits more text
  oled.clear();
  oled.setCursor(0, 0); 
  oled.println("");            // Set cursor to top-left
  oled.println(" MonksHill");       // First line
  oled.println("    Lab");          // Second line
  delay(1000);
}

void loop()
{
      // digitalWrite(Mosfet, HIGH);  // Turn the Mosfet on 
      //get CO2 data from K30FR including error check
      double co2 = K_30_Serial.getCO2('p'); // Read CO2 concentration from the K-30 CO2 sensor ('p' for ppm)
      (co2 < 0) ? co2 = K_30_Serial.getCO2('p') : co2; // Re-read if negative value (error check)
      (co2 < 0) ? co2 = K_30_Serial.getCO2('p') : co2; // Re-read if negative value (error check)

      //get temperature and RH data from SHT4x
      sensors_event_t humidity, temp;
      float p = bmp.readPressure();  

      sht4.getEvent(&humidity, &temp);

      float pressure =  bmp.readPressure();

      //get date and time
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
  
      Serial.print(temp.temperature); // Print temperature to the Serial monitor
      Serial.print(";");
      Serial.print(humidity.relative_humidity); // Print humidity to the Serial monitor
      Serial.print(";");  
      Serial.print(co2); // Print CO2 concentration to the Serial monitor
      Serial.print(";");  
 
      Serial.print(pressure); 
      Serial.print(";");
      Serial.println("Minion 1");


      oled.clear();
      oled.setCursor(0, 0);   // First line
      oled.print("CO2:");
      oled.print(co2, 0);
      oled.println(" ppm");

      oled.setCursor(0, 2);   // Second line
      oled.print("RH:");
      oled.print(humidity.relative_humidity, 1);
      oled.println(" %");

      oled.setCursor(0, 4);   // Second line
      oled.print("Temp:");
      oled.print(temp.temperature, 1);
      oled.println(" C");
  

      //open SD card file and wirte data to SD card
      File dataFile = SD.open("datalog.txt", FILE_WRITE);
     // If the file is available, write the data to it:
      if (dataFile) {    
        dataFile.print(timestring); // Write the timestamp to the file
        dataFile.print(";");
        dataFile.print(temp.temperature); // Write the temperature to the file
        dataFile.print(";");
        dataFile.print(humidity.relative_humidity); // Write the humidity to the file
        dataFile.print(";");
        dataFile.print(co2); // Write the CO2 concentration to the file
        dataFile.print(";");
        // dataFile.print(illuminance_PAR, 2); // Display illuminance with 2 decimal places
        // dataFile.print(";");
        dataFile.print(pressure); // Display illuminance with 2 decimal places
        dataFile.print(";");
        dataFile.println("Minion 1");
        dataFile.close();
        }

       // If the file isn't open, print an error message to the Serial monitor:
      else {
        Serial.println("error opening datalog.txt");
      }
 
delay(2700); // Delay for 3 seconds between iterations
}