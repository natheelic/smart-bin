/*  ═══════════════════════════════════════════════════════════
 *  Smart Trash Bin — Pico WH Firmware
 *  ถังขยะอัจฉริยะ 4 ใบ  (Ultrasonic + Servo × 4)
 *  Board: Raspberry Pi Pico W  (Arduino‑Pico core)
 *  ═══════════════════════════════════════════════════════════ */

#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Servo.h>

// ─── WiFi ────────────────────────────────────────────────
const char* WIFI_SSID = ".@LICEC-Student-WiFi";
const char* WIFI_PASS = "";

// ─── Backend ─────────────────────────────────────────────
const char* SERVER_URL    = "https://smart-bin.vercel.app";   // เช่น https://smart-bin.vercel.app
const char* DEVICE_SECRET = "B_IoulTaElgIHet3qWMtC3sa";        // ตรงกับ .env DEVICE_SECRET

// ─── Pin Map (ถัง 4 ใบ) ─────────────────────────────────
//            Bin1  Bin2  Bin3  Bin4
const int TRIG[4]  = { 2,  6, 10, 14};
const int ECHO[4]  = { 3,  7, 11, 15};
const int SRVP[4]  = { 4,  8, 12, 16};

// ─── Servo Angles ────────────────────────────────────────
const int OPEN_ANGLE  = 90;
const int CLOSE_ANGLE = 0;

// ─── Timing ──────────────────────────────────────────────
const unsigned long POLL_INTERVAL     = 500;   // ms — ส่ง/รับ API
const unsigned long LID_OPEN_DURATION = 3000;  // ms — เปิดฝาค้างกี่วิ (auto)

// ─── Runtime State ───────────────────────────────────────
Servo servos[4];

String  binMode[4]     = {"auto","auto","auto","auto"};
float   threshold[4]   = {30,30,30,30};
bool    manualOpen[4]  = {false,false,false,false};
bool    lidOpen[4]     = {false,false,false,false};
float   distance[4]    = {999,999,999,999};
unsigned long lidTime[4] = {0,0,0,0};

unsigned long lastPoll = 0;

// ═════════════════════════════════════════════════════════
//  Ultrasonic — วัดระยะ (cm)
// ═════════════════════════════════════════════════════════
float measureCm(int trig, int echo) {
  digitalWrite(trig, LOW);  delayMicroseconds(2);
  digitalWrite(trig, HIGH); delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long us = pulseIn(echo, HIGH, 30000);   // timeout 30 ms
  if (us == 0) return 999.0;
  return (us * 0.0343) / 2.0;
}

// ═════════════════════════════════════════════════════════
//  เปิด / ปิด ฝา
// ═════════════════════════════════════════════════════════
void openLid(int i)  { if (!lidOpen[i]) { servos[i].write(OPEN_ANGLE);  lidOpen[i]=true;  lidTime[i]=millis(); Serial.printf("Bin%d OPEN\n",i+1); } }
void closeLid(int i) { if ( lidOpen[i]) { servos[i].write(CLOSE_ANGLE); lidOpen[i]=false; Serial.printf("Bin%d CLOSE\n",i+1); } }

// ═════════════════════════════════════════════════════════
//  POST สถานะ → Backend  &  รับคำสั่งกลับ
// ═════════════════════════════════════════════════════════
void syncWithServer() {
  if (WiFi.status() != WL_CONNECTED) return;

  HTTPClient http;
  http.begin(String(SERVER_URL) + "/api/device");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("x-device-secret", DEVICE_SECRET);

  // สร้าง JSON body
  JsonDocument doc;
  JsonArray arr = doc["bins"].to<JsonArray>();
  for (int i = 0; i < 4; i++) {
    JsonObject b = arr.add<JsonObject>();
    b["id"]          = i + 1;
    b["distance_cm"] = round(distance[i] * 10.0) / 10.0;
    b["lid_open"]    = lidOpen[i];
  }
  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  if (code == 200) {
    String resp = http.getString();
    JsonDocument rdoc;
    if (!deserializeJson(rdoc, resp)) {
      JsonArray bins = rdoc["bins"].as<JsonArray>();
      for (JsonObject b : bins) {
        int id = b["id"].as<int>() - 1;        // 0-based
        if (id < 0 || id >= 4) continue;
        binMode[id]    = b["mode"].as<String>();
        threshold[id]  = b["threshold_cm"].as<float>();
        manualOpen[id] = b["manual_open"].as<bool>();
      }
    }
    Serial.println("Sync OK");
  } else {
    Serial.printf("Sync fail: %d\n", code);
  }
  http.end();
}

// ═════════════════════════════════════════════════════════
//  Setup
// ═════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== Smart Trash Bin (Pico WH) ===");

  for (int i = 0; i < 4; i++) {
    pinMode(TRIG[i], OUTPUT);
    pinMode(ECHO[i], INPUT);
    servos[i].attach(SRVP[i]);
    servos[i].write(CLOSE_ANGLE);
  }

  // WiFi connect
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("WiFi");
  for (int t = 0; t < 40 && WiFi.status() != WL_CONNECTED; t++) { delay(500); Serial.print('.'); }
  if (WiFi.status() == WL_CONNECTED)
    Serial.printf("\nConnected  IP: %s\n", WiFi.localIP().toString().c_str());
  else
    Serial.println("\nWiFi FAIL — running offline");
}

// ═════════════════════════════════════════════════════════
//  Loop
// ═════════════════════════════════════════════════════════
void loop() {
  unsigned long now = millis();

  // 1) อ่านระยะทุกถัง
  for (int i = 0; i < 4; i++)
    distance[i] = measureCm(TRIG[i], ECHO[i]);

  // 2) Sync กับ server ตาม interval
  if (now - lastPoll >= POLL_INTERVAL) {
    syncWithServer();
    lastPoll = now;
  }

  // 3) ประมวลผลแต่ละถัง
  for (int i = 0; i < 4; i++) {
    if (binMode[i] == "auto") {
      /* AUTO: เช็คระยะ < threshold → เปิด, เกิน LID_OPEN_DURATION → ปิด */
      if (distance[i] > 2.0 && distance[i] < threshold[i]) {
        openLid(i);
        lidTime[i] = millis();             // รีเซ็ตเวลาขณะยังตรวจจับ
      } else if (lidOpen[i] && (millis() - lidTime[i] >= LID_OPEN_DURATION)) {
        closeLid(i);
      }
    } else {
      /* MANUAL: ทำตามคำสั่ง manual_open จาก server */
      if (manualOpen[i]) openLid(i);
      else               closeLid(i);
    }
  }

  // 4) Debug
  for (int i = 0; i < 4; i++)
    Serial.printf("B%d[%s] %.0fcm %s | ", i+1, binMode[i].c_str(), distance[i], lidOpen[i]?"O":"C");
  Serial.println();

  delay(50);
}
