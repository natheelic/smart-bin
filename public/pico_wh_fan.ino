// ============================================================
//  IoT Servo Controller — Raspberry Pi Pico WH
//  Mode: AUTO (ultrasonic → servo) | MANUAL (cloud command)
//  Board: "Raspberry Pi Pico W" (arduino-pico by Earle Philhower)
// ============================================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <Servo.h>

// ================= PIN CONFIG =================
#define SERVO1_PIN  2   // GP2
#define SERVO2_PIN  3   // GP3
#define SERVO3_PIN  4   // GP4
#define SERVO4_PIN  5   // GP5

// Ultrasonic (TRIG, ECHO) — ECHO ต้องต่อ Voltage Divider 1.8k+3.3k
const uint8_t TRIG_PINS[] = { 6,  8, 10, 12 };
const uint8_t ECHO_PINS[] = { 7,  9, 11, 13 };

// ================= WIFI & API =================
const char* ssid         = ".@LICEC-Student-WiFi";
const char* password     = "";
const char* apiUrl       = "https://iot-fan-enlic.vercel.app/api/device";
const char* deviceSecret = "LL9NtHPH3T3RapSBSlh_4hsq";

// ================= SERVOS =================
Servo servos[4];
const uint8_t SERVO_PINS[] = { SERVO1_PIN, SERVO2_PIN, SERVO3_PIN, SERVO4_PIN };

// ================= STATE =================
String mode = "auto";   // "auto" | "manual"

struct SensorData { float distance_cm; float threshold_cm; };
struct ServoData  { int command_angle; bool enabled; };

SensorData sensors[4] = {{0,100},{0,100},{0,100},{0,100}};
ServoData  servoCmd[4] = {{90,false},{90,false},{90,false},{90,false}};

WiFiClientSecure client;
unsigned long lastSyncMs = 0;
const unsigned long SYNC_INTERVAL = 2000;

// ================= ULTRASONIC =================
float measureDistance(int idx) {
  uint8_t trig = TRIG_PINS[idx], echo = ECHO_PINS[idx];
  digitalWrite(trig, LOW); delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long dur = pulseIn(echo, HIGH, 30000);
  if (dur == 0) return -1;
  return (dur * 0.0343f) / 2.0f;
}

// ================= SYNC =================
void syncWithCloud() {
  if (WiFi.status() != WL_CONNECTED) return;

  // Build request
  StaticJsonDocument<256> req;
  JsonArray arr = req.createNestedArray("sensors");
  for (int i = 0; i < 4; i++) {
    JsonObject s = arr.createNestedObject();
    s["id"]          = i + 1;
    s["distance_cm"] = sensors[i].distance_cm;
  }
  String body; serializeJson(req, body);

  HTTPClient http;
  http.begin(client, apiUrl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Secret", deviceSecret);
  int code = http.POST(body);

  if (code > 0) {
    StaticJsonDocument<512> resp;
    DeserializationError err = deserializeJson(resp, http.getString());
    if (!err) {
      mode = resp["mode"] | "auto";

      // อัปเดต thresholds
      JsonArray thr = resp["thresholds"];
      for (JsonObject t : thr) {
        int id = (t["id"] | 1) - 1;
        if (id >= 0 && id < 4) sensors[id].threshold_cm = t["threshold_cm"] | 100.0f;
      }

      // อัปเดต servo commands (สำหรับ manual mode)
      JsonArray srvs = resp["servos"];
      for (JsonObject sv : srvs) {
        int id = (sv["id"] | 1) - 1;
        if (id >= 0 && id < 4) {
          servoCmd[id].command_angle = sv["command_angle"] | 90;
          servoCmd[id].enabled       = sv["enabled"]       | false;
        }
      }

      Serial.print("Mode: "); Serial.println(mode);
    }
  }
  http.end();
}

// ================= SETUP =================
void setup() {
  Serial.begin(115200);

  for (int i = 0; i < 4; i++) {
    pinMode(TRIG_PINS[i], OUTPUT);
    pinMode(ECHO_PINS[i], INPUT);
    digitalWrite(TRIG_PINS[i], LOW);
    servos[i].attach(SERVO_PINS[i]);
    servos[i].write(90);
  }

  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nConnected: " + WiFi.localIP().toString());
  client.setInsecure();
}

// ================= LOOP =================
void loop() {
  // วัดระยะทางทุกตัว
  for (int i = 0; i < 4; i++) {
    float d = measureDistance(i);
    sensors[i].distance_cm = (d > 0) ? d : 0;
  }

  // Sync ทุก 2 วินาที
  if (millis() - lastSyncMs > SYNC_INTERVAL) {
    lastSyncMs = millis();
    syncWithCloud();
  }

  // Actuate Servos ตาม mode
  for (int i = 0; i < 4; i++) {
    if (mode == "manual") {
      // Manual: ทำตามคำสั่งจาก cloud
      servos[i].write(servoCmd[i].enabled ? servoCmd[i].command_angle : 90);
    } else {
      // Auto: ถ้า distance < threshold → servo หมุนไป command_angle, ถ้าไม่มีสิ่งกีดขวาง → 90°
      float d   = sensors[i].distance_cm;
      bool  hit = (d > 0 && d < sensors[i].threshold_cm);
      servos[i].write(hit ? servoCmd[i].command_angle : 90);
    }
  }

  delay(50);
}

//  Arduino-C++ | Board: "Raspberry Pi Pico W" (arduino-pico)
// ============================================================

#include <WiFi.h>              // arduino-pico built-in
#include <HTTPClient.h>        // arduino-pico built-in
#include <WiFiClientSecure.h>  // arduino-pico built-in
#include <ArduinoJson.h>
#include <Servo.h>             // arduino-pico built-in (RP2040 PWM servo)

// ================= PIN CONFIG =================
// Servo (PWM)
#define SERVO1_PIN  2   // GP2  — พัดลมหลัก (หมุน vs หยุด)
#define SERVO2_PIN  3   // GP3  — ส่าย
#define SERVO3_PIN  4   // GP4  — ส่าย
#define SERVO4_PIN  5   // GP5  — ส่าย

// Ultrasonic HC-SR04
//   ⚠️  ECHO ต้องต่อผ่าน Voltage Divider (1.8kΩ + 3.3kΩ) — Pico รับ 3.3V เท่านั้น
#define TRIG1  6   // GP6
#define ECHO1  7   // GP7
#define TRIG2  8   // GP8
#define ECHO2  9   // GP9
#define TRIG3  10  // GP10
#define ECHO3  11  // GP11
#define TRIG4  12  // GP12
#define ECHO4  13  // GP13

// ================= WIFI & API =================
const char* ssid         = ".@LICEC-Student-WiFi";
const char* password     = "";
const char* apiUrl       = "https://iot-fan-enlic.vercel.app/api/device?id=1";
const char* deviceSecret = "LL9NtHPH3T3RapSBSlh_4hsq";

// ================= SERVO OBJECTS =================
Servo servoFan;    // Servo 1 — ควบคุมพัดลมหลัก
Servo servoSwing1; // Servo 2 — ส่าย
Servo servoSwing2; // Servo 3 — ส่าย
Servo servoSwing3; // Servo 4 — ส่าย

// มุมสำหรับพัดลม
#define FAN_ON_ANGLE   180
#define FAN_OFF_ANGLE   90

// ========= Ultrasonic Helper Tables =========
const uint8_t TRIG_PINS[] = { TRIG1, TRIG2, TRIG3, TRIG4 };
const uint8_t ECHO_PINS[] = { ECHO1, ECHO2, ECHO3, ECHO4 };

// ================= VARIABLES =================
WiFiClientSecure client;
HTTPClient       http;

unsigned long lastMotionTime = 0;
const unsigned long fanOffDelay   = 120000UL; // 2 นาที
unsigned long lastSyncTime        = 0;
const unsigned long syncInterval  = 2000UL;

bool  fanOn     = false;
bool  manualMode = false;
float targetTemp = 28.0;
bool  swingCmd  = false;

// ============================================================
//  SWING STATE MACHINE — ส่ายไปมาแบบ Non-blocking
// ============================================================
int  swingAngle    = 45;    // มุมปัจจุบัน
int  swingDir      = 1;     // 1=เพิ่ม, -1=ลด
const int  SWING_MIN   = 45;
const int  SWING_MAX   = 135;
const int  SWING_STEP  = 5;
unsigned long lastSwingMs = 0;
const unsigned long swingInterval = 30; // ms per step

// ============================================================
//  HELPER: วัดระยะทาง HC-SR04 (cm)  — คืน -1 ถ้า timeout
// ============================================================
float measureDistance(uint8_t trigPin, uint8_t echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000); // timeout 30 ms
  if (duration == 0) return -1;
  return (duration * 0.0343f) / 2.0f;
}

// ตรวจจับการเคลื่อนไหว (ระยะ < threshold cm จากเซ็นเซอร์ใดก็ได้)
bool isMotionDetected(float threshold = 100.0f) {
  for (int i = 0; i < 4; i++) {
    float d = measureDistance(TRIG_PINS[i], ECHO_PINS[i]);
    if (d > 0 && d < threshold) return true;
  }
  return false;
}

// ============================================================
//  SETUP
// ============================================================
void setup() {
  Serial.begin(115200);

  // Ultrasonic pins
  for (int i = 0; i < 4; i++) {
    pinMode(TRIG_PINS[i], OUTPUT);
    pinMode(ECHO_PINS[i], INPUT);
    digitalWrite(TRIG_PINS[i], LOW);
  }

  // Attach Servos
  servoFan.attach(SERVO1_PIN);
  servoSwing1.attach(SERVO2_PIN);
  servoSwing2.attach(SERVO3_PIN);
  servoSwing3.attach(SERVO4_PIN);

  // ตำแหน่งเริ่มต้น
  servoFan.write(FAN_OFF_ANGLE);
  servoSwing1.write(90);
  servoSwing2.write(90);
  servoSwing3.write(90);

  // WiFi
  WiFi.begin(ssid, password);
  Serial.print("Connecting WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nConnected: " + WiFi.localIP().toString());

  client.setInsecure(); // ข้าม SSL verify (self-signed / Vercel)
}

// ============================================================
//  SYNC WITH CLOUD
// ============================================================
void syncWithCloud(bool motion) {
  if (WiFi.status() != WL_CONNECTED) return;

  StaticJsonDocument<200> docSend;
  docSend["temp"]       = 0.0;   // TODO: ต่อ DHT11/DS18B20 ที่ขาว่าง เช่น GP22
  docSend["motion"]     = motion;
  docSend["fan_status"] = fanOn;

  String body;
  serializeJson(docSend, body);

  http.begin(client, apiUrl);
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Secret", deviceSecret);
  int code = http.POST(body);

  if (code > 0) {
    String resp = http.getString();
    StaticJsonDocument<400> docResp;
    DeserializationError err = deserializeJson(docResp, resp);

    if (!err) {
      bool  cloudManual  = docResp["manual_mode"]   | false;
      bool  cloudFanCmd  = docResp["fan_command"]   | false;
      float cloudTarget  = docResp["target_temp"]   | 0.0f;
      bool  cloudSwing   = docResp["swing_command"] | false;

      if (cloudTarget > 0) targetTemp = cloudTarget;
      manualMode = cloudManual;
      swingCmd   = cloudSwing;

      if (manualMode) fanOn = cloudFanCmd;

      Serial.print("Mode: ");   Serial.print(manualMode ? "MANUAL" : "AUTO");
      Serial.print(" | Fan: "); Serial.print(fanOn);
      Serial.print(" | Swing: "); Serial.println(swingCmd);
    }
  }
  http.end();
}

// ============================================================
//  LOOP
// ============================================================
void loop() {
  bool motionDetected = isMotionDetected(100.0f);
  unsigned long now = millis();

  // --- Sync ---
  if (now - lastSyncTime > syncInterval) {
    lastSyncTime = now;
    syncWithCloud(motionDetected);
  }

  // --- Auto Mode Logic ---
  if (!manualMode) {
    if (motionDetected) {
      lastMotionTime = now;
      fanOn = true; // เปิดพัดลมเมื่อตรวจพบการเคลื่อนไหว
    }
    if (fanOn && (now - lastMotionTime > fanOffDelay)) {
      fanOn = false;
    }
  }

  // --- Hardware Actuation ---
  // พัดลมหลัก (Servo 1)
  servoFan.write(fanOn ? FAN_ON_ANGLE : FAN_OFF_ANGLE);

  // ส่าย (Servo 2-4) — Non-blocking sweep
  if (swingCmd && fanOn) {
    if (now - lastSwingMs >= swingInterval) {
      lastSwingMs = now;
      swingAngle += swingDir * SWING_STEP;
      if (swingAngle >= SWING_MAX) { swingAngle = SWING_MAX; swingDir = -1; }
      if (swingAngle <= SWING_MIN) { swingAngle = SWING_MIN; swingDir =  1; }
      servoSwing1.write(swingAngle);
      servoSwing2.write(swingAngle);
      servoSwing3.write(swingAngle);
    }
  } else {
    // หยุดที่กลาง
    servoSwing1.write(90);
    servoSwing2.write(90);
    servoSwing3.write(90);
    swingAngle = 90;
    swingDir   = 1;
  }
}
