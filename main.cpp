#include <Arduino.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <DHT.h>


// --- CONFIGURATION ---
//#define WIFI_SSID "YOUR_WIFI_NAME"
//#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#define API_KEY "AIzaSyD-gYivB1ye0UYiCKMSimVNaJm8dwWVHsE"
#define DATABASE_URL "https://greenmind-5f7f3-default-rtdb.firebaseio.com"

// --- HARDWARE PINS ---
#define DHTPIN 4           // DHT22 Data Pin
#define DHTTYPE DHT22
#define SOIL_PIN 34        // Analog Soil Moisture Pin
#define PUMP_PIN 26        // Relay 1
#define MIST_PIN 27        // Relay 2
#define FAN_PIN 14         // Relay 3

DHT dht(DHTPIN, DHTTYPE);
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

unsigned long sendDataPrevMillis = 0;
bool signupOK = false;

// --- DATABASE CALLBACK ---
// This function triggers whenever you flip a switch on the Web Dashboard
void streamCallback(FirebaseStream data) {
  String path = data.dataPath();
  bool value = data.boolData();

  if (path == "/pump") digitalWrite(PUMP_PIN, value ? LOW : HIGH); // LOW is usually ON for many relay modules
  if (path == "/mist") digitalWrite(MIST_PIN, value ? LOW : HIGH);
  if (path == "/fan")  digitalWrite(FAN_PIN, value ? LOW : HIGH);
  
  Serial.printf("Command Received: %s set to %d\n", path.c_str(), value);
}

void streamTimeoutCallback(bool timeout) {
  if (timeout) Serial.println("Stream timed out, resuming...");
}

void setup() {
  Serial.begin(115200);

  // Initialize Pins
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(MIST_PIN, OUTPUT);
  pinMode(FAN_PIN, OUTPUT);
  // Set relays to OFF initially
  digitalWrite(PUMP_PIN, HIGH); 
  digitalWrite(MIST_PIN, HIGH);
  digitalWrite(FAN_PIN, HIGH);

  dht.begin();

  // WiFi Setup
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(300); Serial.print(".");
  }
  Serial.println("\nConnected to WiFi");

  // Firebase Setup
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  if (Firebase.signUp(&config, &auth, "", "")) {
    signupOK = true;
  }

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Start listening for dashboard commands
  if (!Firebase.RTDB.beginStream(&fbdo, "/greenhouse")) {
    Serial.printf("Stream begin error, %s\n", fbdo.errorReason().c_str());
  }
  Firebase.RTDB.setStreamCallback(&fbdo, streamCallback, streamTimeoutCallback);
}

void loop() {
  // Update sensors every 5 seconds
  if (Firebase.ready() && signupOK && (millis() - sendDataPrevMillis > 5000 || sendDataPrevMillis == 0)) {
    sendDataPrevMillis = millis();

    float h = dht.readHumidity();
    float t = dht.readTemperature();
    int soilRaw = analogRead(SOIL_PIN);
    int soilPercent = map(soilRaw, 4095, 1200, 0, 100); // Calibrate based on your sensor
    if (soilPercent > 100) soilPercent = 100;
    if (soilPercent < 0) soilPercent = 0;

    if (isnan(h) || isnan(t)) {
      Serial.println("Failed to read from DHT sensor!");
      return;
    }

    // Push data to Firebase
    FirebaseJson json;
    json.set("temp", t);
    json.set("hum", h);
    json.set("soil", soilPercent);

    if (Firebase.RTDB.updateNode(&fbdo, "/greenhouse", &json)) {
      Serial.println("Dashboard Updated Successfully");
    } else {
      Serial.println("Update Failed: " + fbdo.errorReason());
    }
  }
}