#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <DHT.h>

// ================= PIN CONFIG =================
#define FAN_PIN D1      // รีเลย์พัดลม
#define SWING_PIN D2    // *** เปลี่ยนเป็น D3 ตามที่ขอ ***
#define PIR D5
#define DHTPIN 12 
#define DHTTYPE DHT11

// ================= WIFI & API =================
const char* ssid = ".@LICEC-Student-WiFi";
const char* password = "";
// เติม ?id=1 ต่อท้าย
const char* apiUrl = "https://iot-fan-enlic.vercel.app/api/device?id=1";
// *** ต้องตรงกับค่า DEVICE_SECRET ใน Environment Variables บน Vercel ***
const char* deviceSecret = "LL9NtHPH3T3RapSBSlh_4hsq";

// ================= OBJECTS =================
DHT dht(DHTPIN, DHTTYPE);
WiFiClientSecure client;
HTTPClient http;

// ================= VARIABLES =================
unsigned long lastMotionTime = 0;
const unsigned long fanOffDelay = 120000; 
unsigned long lastSyncTime = 0;
const unsigned long syncInterval = 2000; 

float currentTemp = 0.0;
bool fanOn = false;
bool manualMode = false;
float targetTemp = 28.0;

// *** เพิ่มตัวแปรคำสั่งส่าย ***
bool swingCmd = false; 

// ================= SETUP =================
void setup() {
  Serial.begin(115200);
  pinMode(FAN_PIN, OUTPUT); pinMode(SWING_PIN, OUTPUT); pinMode(PIR, INPUT);
  
  // ปิด Relay ทั้งคู่ไว้ก่อน (Active LOW -> HIGH คือปิด)
  digitalWrite(FAN_PIN, HIGH); 
  digitalWrite(SWING_PIN, HIGH); 
  
  dht.begin();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nConnected");
  client.setInsecure();
}

// ================= LOOP =================
void loop() {
  float t = dht.readTemperature();
  if (!isnan(t)) currentTemp = t;
  
  int pirState = digitalRead(PIR);
  bool motionDetected = (pirState == HIGH);

  if (millis() - lastSyncTime > syncInterval) {
    lastSyncTime = millis();
    syncWithCloud(motionDetected);
  }

  // === Logic Control ===
  
  // 1. Auto Mode Logic (คุมเฉพาะการเปิดปิดพัดลม)
  if (!manualMode) { 
    if (motionDetected) {
       if (currentTemp >= targetTemp || fanOn) {
           lastMotionTime = millis(); 
           if (!fanOn && currentTemp >= targetTemp) { 
             fanOn = true; // ให้ Fan ทำงาน
           }
       }
    }
    
    // Auto Off Check
    if (fanOn) {
      long timeLeft = (fanOffDelay - (millis() - lastMotionTime)) / 1000;
      if (timeLeft <= 0) {
         fanOn = false; // สั่งดับ
      }
    }
  }

  // 2. สั่ง Hardware ทำงานจริง (Hardware Actuation)
  // Logic: ส่ายจะทำงานได้ ก็ต่อเมื่อ "คำสั่งส่าย ON" และ "พัดลมต้องหมุนอยู่"
  
  // คุมพัดลม
  if (fanOn) digitalWrite(FAN_PIN, LOW); // ON
  else digitalWrite(FAN_PIN, HIGH);      // OFF

  // คุมส่าย (แยกเป็นอิสระ แต่เช็คเงื่อนไขพัดลมด้วย)
  if (swingCmd && fanOn) {
    digitalWrite(SWING_PIN, LOW); // ส่ายทำงาน
  } else {
    digitalWrite(SWING_PIN, HIGH); // หยุดส่าย
  }
}

// ================= HELPER FUNCTIONS =================

void syncWithCloud(bool motion) {
  if (WiFi.status() == WL_CONNECTED) {
    StaticJsonDocument<200> docToSend;
    docToSend["temp"] = currentTemp;
    docToSend["motion"] = motion;
    docToSend["fan_status"] = fanOn;
    
    String jsonString;
    serializeJson(docToSend, jsonString);

    http.begin(client, apiUrl);
    http.addHeader("Content-Type", "application/json");
    http.addHeader("X-Device-Secret", deviceSecret);
    int httpResponseCode = http.POST(jsonString);

    if (httpResponseCode > 0) {
      String response = http.getString();
      StaticJsonDocument<400> docResponse;
      DeserializationError error = deserializeJson(docResponse, response);

      if (!error) {
        bool cloudManual = docResponse["manual_mode"];
        bool cloudFanCmd = docResponse["fan_command"];
        float cloudTarget = docResponse["target_temp"];
        
        // *** รับค่าคำสั่งส่าย ***
        bool cloudSwing = docResponse["swing_command"];

        if (cloudTarget > 0) targetTemp = cloudTarget;

        manualMode = cloudManual;
        swingCmd = cloudSwing; // อัปเดตคำสั่งส่าย

        // ถ้า Manual ให้เชื่อปุ่มพัดลมจาก Cloud ทันที
        if (manualMode) {
          fanOn = cloudFanCmd;
        }
        
        Serial.print("Mode: "); Serial.print(manualMode ? "MANUAL" : "AUTO");
        Serial.print(" | Fan: "); Serial.print(fanOn);
        Serial.print(" | Swing: "); Serial.println(swingCmd);
      }
    }
    http.end();
  }
}