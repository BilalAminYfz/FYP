#include <Wire.h>
#include <WiFi.h>
#include <OneWire.h>
#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <driver/adc.h>
#include "EmonLib.h"
#include <LiquidCrystal_I2C.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"

#define WIFI_PASSWORD "pleasetype"
#define WIFI_SSID "no_internet"
#define DATA_BASE_URL "https://espdata-5b5f9-default-rtdb.europe-west1.firebasedatabase.app/"
#define DATA_BASE_API "AIzaSyCPwt7_Vx5tl1UztNM862BmX7A-QaO4RIM"
#define CT_INPUT 34      // GPIO pin for CT sensor
#define VOLTAGE_INPUT 33 // GPIO pin for voltage sensor

EnergyMonitor emon1;
LiquidCrystal_I2C lcd(0x27, 16, 2); // LCD address 0x27, 16 chars, 2 lines

FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

const unsigned long SEND_INTERVAL = 15000; // Interval to send data to Firebase (15 sec)
unsigned long lastDataSendTime = 0;
const float vCalibration = 41.5;
const float currCalibration = 0.15;
bool signupOK = false;

double totalPower = 0; // Variable to track total power over 24 hours
unsigned long startTime = 0; // To track when 24-hour calculation starts

// Function to connect to WiFi
void connectToWifi() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int retries = 0;
  lcd.setCursor(0, 0);
  lcd.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED && retries < 15) {
    delay(500);
    lcd.print(".");
    Serial.print(".");
    retries++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("CONNECTED WITH IP: " + WiFi.localIP().toString());
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("WiFi Connected");
  } else {
    Serial.println("WiFi connection failed");
    lcd.clear();
    lcd.print("WiFi connection Failed");
  }
}

// Function to connect to Firebase
void connectToDatabase() {
  config.api_key = DATA_BASE_API;
  config.database_url = DATA_BASE_URL;
  if (Firebase.signUp(&config, &auth, "", "")) {
    Serial.print("Connected to DataBase");
    lcd.clear();
    lcd.print("DB Connected");
    signupOK = true;
  } else {
    Serial.printf("%s\n", config.signer.signupError.message.c_str());
    Serial.print("DB connection failed ");
    lcd.clear();
    lcd.print("DB FAILED");
  }
  config.token_status_callback = tokenStatusCallback;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
}

// Function to calculate and store 24-hour power consumption
void calculate24HourPower(double watt) {
  // Add the current power to the total
  totalPower += watt;

  // Get current time (in milliseconds since the device started)
  unsigned long currentTime = millis();

  // Check if 24 hours (86400000 milliseconds) have passed
  if (currentTime - startTime >= 86400000) {
    // Store the total power consumed in the last 24 hours in the database
    if (Firebase.RTDB.setDouble(&fbdo, "path/24_hour_power", totalPower)) {
      Serial.println("Stored 24-hour power: " + String(totalPower));
    } else {
      Serial.println("Failed to store 24-hour power: " + fbdo.errorReason());
    }

    // Reset the total power and startTime for the next 24 hours
    totalPower = 0;
    startTime = currentTime;
  }
}

// Function to send power data to Firebase
void sendDataToFirebase(double voltage, double amps, double watt) {
  if (Firebase.RTDB.setDouble(&fbdo, "path/current", amps)) {
    Serial.println("Current data written");
  } else {
    Serial.println("Failed to write current data: " + fbdo.errorReason());
  }

  if (Firebase.RTDB.setDouble(&fbdo, "path/power", watt)) {
    Serial.println("Power data written");
  } else {
    Serial.println("Failed to write power data: " + fbdo.errorReason());
  }

  if (Firebase.RTDB.setDouble(&fbdo, "path/voltage", voltage)) {
    Serial.println("Voltage data written");
  } else {
    Serial.println("Failed to write voltage data: " + fbdo.errorReason());
  }

  // Call the 24-hour power calculation function
  calculate24HourPower(watt);
}

void setup() {
  Serial.begin(115200);
  lcd.init();
  lcd.backlight();

  connectToWifi();
  emon1.voltage(VOLTAGE_INPUT, vCalibration, 1.7);
  emon1.current(CT_INPUT, currCalibration);
  connectToDatabase();
  
  // Initialize the start time for 24-hour power calculation
  startTime = millis();
}

void loop() {
  if (Firebase.ready() && signupOK && (millis() - lastDataSendTime > SEND_INTERVAL || lastDataSendTime == 0)) {
    lastDataSendTime = millis();

    // Calculate RMS values
    emon1.calcVI(20, 2000);
    double voltage = emon1.Vrms;
    double amps = emon1.Irms;
    double watt = amps * voltage;

    lcd.clear();
    lcd.print("V:" + String(voltage) + "  C:" + String(amps));
    lcd.setCursor(0, 1);
    lcd.print("P:" + String(watt));

    // Send data to Firebase
    sendDataToFirebase(voltage, amps, watt);
  }
}
