#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>

// --- CẤU HÌNH ---
const char* WIFI_SSID = "!!!";
const char* WIFI_PASSWORD = "0907803189";
#define API_KEY "AIzaSyA7ZzZUnjSVivNX_0XsCdrKFIDRjquC2ww"
#define DATABASE_URL "https://kc326e-hki-25-25-nhom8-default-rtdb.asia-southeast1.firebasedatabase.app"
#define USER_EMAIL "xe@gmail.com"
#define USER_PASSWORD "123456"

// --- PIN MAP ---
#define IN1 25
#define IN2 26
#define ENA 32
#define IN3 27
#define IN4 14
#define ENB 33
#define TRIG 5
#define ECHO 18
#define BUZZER 19

// --- BIẾN ĐIỀU KHIỂN ---
int carSpeed = 255;
int turnSpeed = 70;
int dist = 100;
String currentCmd = "S";
unsigned long lastDistMillis = 0;
int lastSentDist = -1;
unsigned long lastHeartbeatSend = 0;

FirebaseData stream;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ==========================================
// 1. CÁC HÀM HỖ TRỢ
// ==========================================
int getQuickDistance() {
  digitalWrite(TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG, LOW);
  long duration = pulseIn(ECHO, HIGH, 15000); 
  if (duration == 0) return 400;
  return (int)(duration * 0.034 / 2);
}

void stopMotors() {
  digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
  ledcWrite(ENA, 0); ledcWrite(ENB, 0);
}

// ==========================================
// 2. NHẬN LỆNH TỪ APP (STREAM)
// ==========================================
void moveCar(String cmd) {
  // Kiểm tra vật cản trước khi đi tiến
  if (dist > 0 && dist <= 20 && (cmd == "F" || cmd == "FL" || cmd == "FR")) {
    stopMotors();
    currentCmd = "S";
    Serial.println(">>> CHẶN LỆNH TIẾN: Có vật cản quá gần!");
    return;
  }

  if (cmd == "F") { ledcWrite(ENA, carSpeed); ledcWrite(ENB, carSpeed); digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH); digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW); } 
  else if (cmd == "B") { ledcWrite(ENA, carSpeed); ledcWrite(ENB, carSpeed); digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW); digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH); }
  else if (cmd == "R") { ledcWrite(ENA, carSpeed); ledcWrite(ENB, carSpeed); digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW); digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW); } 
  else if (cmd == "L") { ledcWrite(ENA, carSpeed); ledcWrite(ENB, carSpeed); digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH); digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH); } 
  else if (cmd == "FL") { ledcWrite(ENA, carSpeed); ledcWrite(ENB, turnSpeed); digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH); digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW); }
  else if (cmd == "FR") { ledcWrite(ENA, turnSpeed); ledcWrite(ENB, carSpeed); digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH); digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW); }
  else { stopMotors(); }
}

void streamCallback(FirebaseStream data) {
  if (data.dataType() == "json") {
    FirebaseJson &json = data.jsonObject();
    FirebaseJsonData jsonData;
    
    // Nhận lệnh di chuyển
    json.get(jsonData, "cmd");
    if (jsonData.success) {
      currentCmd = jsonData.stringValue;
      Serial.print("[Firebase] Lệnh mới: "); Serial.println(currentCmd);
      moveCar(currentCmd);
    }
    
    // Nhận lệnh còi
    json.get(jsonData, "horn");
    if (jsonData.success) {
      int hornState = jsonData.intValue;
      digitalWrite(BUZZER, hornState == 1 ? HIGH : LOW);
      Serial.print("[Firebase] Còi: "); Serial.println(hornState == 1 ? "BẬT" : "TẮT");
    }
  }
}

// ==========================================
// 3. HÀM CẬP NHẬT OTA
// ==========================================
void handleOTA() {
  bool runUpdate = false;
  Firebase.RTDB.getBool(&fbdo, "/update/trigger", &runUpdate);

  if (runUpdate) {
    String downloadUrl = "";
    Firebase.RTDB.getString(&fbdo, "/update/url", &downloadUrl);

    if (downloadUrl != "" && downloadUrl != "null") {
      Serial.println("\n--- BẮT ĐẦU CẬP NHẬT OTA ---");
      stopMotors();
      Firebase.RTDB.endStream(&stream);
      delay(500);

      WiFiClientSecure client;
      client.setInsecure();
      httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

      Serial.println("Đang tải file từ GitHub...");
      t_httpUpdate_return ret = httpUpdate.update(client, downloadUrl);

      if (ret == HTTP_UPDATE_OK) {
        Serial.println("THÀNH CÔNG! Đang khởi động lại...");
        Firebase.RTDB.setBool(&fbdo, "/update/trigger", false);
        delay(1000);
        ESP.restart();
      } else {
        Serial.printf("THẤT BẠI: %s\n", httpUpdate.getLastErrorString().c_str());
        Firebase.RTDB.setBool(&fbdo, "/update/trigger", false);
        Firebase.RTDB.beginStream(&stream, "/rc_car");
        Firebase.RTDB.setStreamCallback(&stream, streamCallback, [](bool t){});
      }
    }
  }
}

// ==========================================
// 4. SETUP & LOOP
// ==========================================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n\n============================");
  Serial.println("HỆ THỐNG XE RC KHỞI ĐỘNG");
  
  pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
  pinMode(BUZZER, OUTPUT);
  pinMode(TRIG, OUTPUT); pinMode(ECHO, INPUT);

  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);

  // THAY THẾ ledcAttach bằng đoạn này:
  ledcAttachPin(ENA, 0); // Gắn chân ENA vào kênh 0
  ledcSetup(0, 30000, 8); // Cấu hình kênh 0: 30kHz, 8-bit
  
  ledcAttachPin(ENB, 1); // Gắn chân ENB vào kênh 1
  ledcSetup(1, 30000, 8); // Cấu hình kênh 1: 30kHz, 8-bit 
  stopMotors();

  Serial.print("Đang kết nối WiFi");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\n[OK] WiFi đã kết nối!");

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  Serial.println("[OK] Firebase đã sẵn sàng!");

  Firebase.RTDB.beginStream(&stream, "/rc_car");
  Firebase.RTDB.setStreamCallback(&stream, streamCallback, [](bool t){});
  Serial.println("[OK] Đang chờ lệnh từ App...");
}

void loop() {
  // 1. Đo khoảng cách
  dist = getQuickDistance();

  // 2. Dừng khẩn cấp nếu đang đi tiến mà gặp vật cản
  if (dist > 0 && dist <= 20) {
    if (currentCmd == "F" || currentCmd == "FL" || currentCmd == "FR") {
      stopMotors();
      currentCmd = "S"; 
      Serial.print("!!! DỪNG KHẨN CẤP: Vật cản cách "); Serial.print(dist); Serial.println(" cm");
    }
  }

  // 3. Gửi dữ liệu lên Firebase
  if (Firebase.ready()) {
    // Heartbeat 5s/lần
    if (millis() - lastHeartbeatSend > 5000) {
      Firebase.RTDB.setIntAsync(&fbdo, "/rc_car/heartbeat", millis());
      lastHeartbeatSend = millis();
    }

    // Gửi cảnh báo khoảng cách (Warning)
    if (dist > 0 && dist <= 25) {
      if (abs(dist - lastSentDist) >= 2 || (millis() - lastDistMillis > 300)) {
        Firebase.RTDB.setIntAsync(&fbdo, "/rc_car/distance", dist);
        lastDistMillis = millis();
        lastSentDist = dist;
        Serial.print("--> Gửi cảnh báo lên App: "); Serial.print(dist); Serial.println(" cm");
      }
    } else if (lastSentDist != 0) {
      if (Firebase.RTDB.setInt(&fbdo, "/rc_car/distance", 0)) {
        lastSentDist = 0; 
        Serial.println("--> Đường trống: Đã xóa cảnh báo trên App");
      }
    }
  }

  // 4. Kiểm tra OTA mỗi 10s
  static unsigned long lastOTA = 0;
  if (millis() - lastOTA > 10000) {
      handleOTA();
      lastOTA = millis();
  }

  delay(10);
}