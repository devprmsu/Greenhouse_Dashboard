#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include "DHT.h"

// --- WiFi Credentials ---
#define WIFI_SSID "Net101"
#define WIFI_PASSWORD "1234567899"

// --- Firebase REST Setup ---
#define FIREBASE_HOST "greenmind-5f7f3-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "AIzaSyD-gYivB1ye0UYiCKMSimVNaJm8dwWVHsE"

// --- Pin Mapping ---
#define WIFI_LED 2 
#define SOIL_PIN 34
#define DHTPIN 5      // DHT22 on GPIO 5
#define DHTTYPE DHT22
#define FAN_PIN  12 
#define MIST_PIN 13 
#define PUMP_PIN 14 

// Initialize DHT
DHT dht(DHTPIN, DHTTYPE);

unsigned long lastUpdate = 0;

void setup() {
  Serial.begin(115200);
  
  // Pin Modes
  pinMode(WIFI_LED, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  pinMode(MIST_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  
  // Initialize Relays (OFF = HIGH)
  digitalWrite(FAN_PIN, HIGH);
  digitalWrite(MIST_PIN, HIGH);
  digitalWrite(PUMP_PIN, HIGH);

  // Start DHT22
  dht.begin();

  // Connect WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    digitalWrite(WIFI_LED, !digitalRead(WIFI_LED));
    delay(500);
    Serial.print(".");
  }
  digitalWrite(WIFI_LED, HIGH);
  Serial.println("\nSystem Online");
}

void loop() {
  // Sync with Firebase every 5 seconds
  if (millis() - lastUpdate > 5000) {
    lastUpdate = millis();
    syncWithFirebase();
  }
}

void syncWithFirebase() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure(); // Bypass SSL check
    HTTPClient http;

    // 1. READ ACTUAL SENSORS
    int rawSoil = analogRead(SOIL_PIN);
    int soilPercent = map(rawSoil, 4095, 1500, 0, 100);
    soilPercent = constrain(soilPercent, 0, 100);

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    // Verify DHT data
    if (isnan(h) || isnan(t)) {
      Serial.println("DHT22 Error: Failed to read from sensor!");
      // We still continue to send Soil data even if DHT fails
      t = 0; 
      h = 0;
    }

    // 2. SEND SENSOR DATA (PATCH)
    String url = "https://" + String(FIREBASE_HOST) + "/greenhouse.json?auth=" + String(FIREBASE_AUTH);
    
    StaticJsonDocument<200> doc;
    doc["soil"] = soilPercent;
    doc["temp"] = t;
    doc["hum"] = h;

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    http.begin(client, url);
    int httpResponseCode = http.PATCH(jsonPayload); 

    if (httpResponseCode > 0) {
      Serial.printf("Update Success [%d]: T:%.1f H:%.1f S:%d%%\n", httpResponseCode, t, h, soilPercent);
    } else {
      Serial.println("Error sending data: " + String(httpResponseCode));
    }
    http.end();

    // 3. GET CONTROL STATES (GET)
    http.begin(client, url);
    int getCode = http.GET();

    if (getCode == 200) {
      String payload = http.getString();
      StaticJsonDocument<500> controlDoc;
      deserializeJson(controlDoc, payload);

      // Read states from your GitHub Dashboard
      bool fanState = controlDoc["fan"];
      bool mistState = controlDoc["mist"];
      bool pumpState = controlDoc["pump"];

      // Control Relays (Active Low logic: LOW = ON)
      digitalWrite(FAN_PIN, fanState ? LOW : HIGH);
      digitalWrite(MIST_PIN, mistState ? LOW : HIGH);
      digitalWrite(PUMP_PIN, pumpState ? LOW : HIGH);
      
      Serial.println("Relay states synchronized.");
    }
    http.end();
  }
}