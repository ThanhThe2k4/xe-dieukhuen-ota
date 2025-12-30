#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPUpdate.h>
#include <Firebase_ESP_Client.h>

// --- CẤU HÌNH WIFI & FIREBASE ---
const char* WIFI_SSID = "!!!"; 
const char* WIFI_PASSWORD = "0907803189";
#define API_KEY "AIzaSyA7ZzZUnjSVivNX_0XsCdrKFIDRjquC2ww"
#define DATABASE_URL "https://kc326e-hki-25-25-nhom8-default-rtdb.asia-southeast1.firebasedatabase.app/"
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

// --- CẤU HÌNH LEDC (PWM CHO CORE < 3.0) ---
#define LEDC_CHANNEL_A 0
#define LEDC_CHANNEL_B 1
#define LEDC_TIMER_13_BIT 8
#define LEDC_BASE_FREQ 5000

// --- BIẾN ĐIỀU KHIỂN ---
int carSpeed = 255;    // Tốc độ tối đa
int turnSpeed = 120;   // Tốc độ khi bẻ lái (giảm tốc bánh bên trong)
int dist = 100;
String currentCmd = "S";
unsigned long lastHeartbeatSend = 0;
unsigned long lastDistMillis = 0;

FirebaseData stream;
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;

// ==========================================
// HÀM ĐIỀU KHIỂN MOTOR CHI TIẾT
// ==========================================
void stopMotors() {
    digitalWrite(IN1, LOW); digitalWrite(IN2, LOW);
    digitalWrite(IN3, LOW); digitalWrite(IN4, LOW);
    ledcWrite(LEDC_CHANNEL_A, 0); ledcWrite(LEDC_CHANNEL_B, 0);
}

void moveCar(String cmd) {
    // Chống va chạm khi tiến (bao gồm cả tiến rẽ)
    if (dist > 0 && dist <= 20 && (cmd == "F" || cmd == "FL" || cmd == "FR")) {
        stopMotors();
        currentCmd = "S";
        return;
    }

    if (cmd == "F") { // Tiến thẳng
        digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH); 
        digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW); 
        ledcWrite(LEDC_CHANNEL_A, carSpeed); ledcWrite(LEDC_CHANNEL_B, carSpeed); 
    } 
    else if (cmd == "B") { // Lùi thẳng
        digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW); 
        digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH); 
        ledcWrite(LEDC_CHANNEL_A, carSpeed); ledcWrite(LEDC_CHANNEL_B, carSpeed); 
    } 
    else if (cmd == "L") { // Xoay trái tại chỗ
        digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW); 
        digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW); 
        ledcWrite(LEDC_CHANNEL_A, carSpeed); ledcWrite(LEDC_CHANNEL_B, carSpeed); 
    } 
    else if (cmd == "R") { // Xoay phải tại chỗ
        digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH); 
        digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH); 
        ledcWrite(LEDC_CHANNEL_A, carSpeed); ledcWrite(LEDC_CHANNEL_B, carSpeed); 
    } 
    else if (cmd == "FL") { // Tiến rẽ trái (Bánh trái chậm, bánh phải nhanh)
        digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH); 
        digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW); 
        ledcWrite(LEDC_CHANNEL_A, turnSpeed); ledcWrite(LEDC_CHANNEL_B, carSpeed); 
    } 
    else if (cmd == "FR") { // Tiến rẽ phải (Bánh trái nhanh, bánh phải chậm)
        digitalWrite(IN1, LOW); digitalWrite(IN2, HIGH); 
        digitalWrite(IN3, HIGH); digitalWrite(IN4, LOW); 
        ledcWrite(LEDC_CHANNEL_A, carSpeed); ledcWrite(LEDC_CHANNEL_B, turnSpeed); 
    } 
    else if (cmd == "BL") { // Lùi rẽ trái
        digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW); 
        digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH); 
        ledcWrite(LEDC_CHANNEL_A, turnSpeed); ledcWrite(LEDC_CHANNEL_B, carSpeed); 
    } 
    else if (cmd == "BR") { // Lùi rẽ phải
        digitalWrite(IN1, HIGH); digitalWrite(IN2, LOW); 
        digitalWrite(IN3, LOW); digitalWrite(IN4, HIGH); 
        ledcWrite(LEDC_CHANNEL_A, carSpeed); ledcWrite(LEDC_CHANNEL_B, turnSpeed); 
    } 
    else { stopMotors(); }
}

// ==========================================
// XỬ LÝ STREAM & OTA
// ==========================================
void streamCallback(FirebaseStream data) {
    if (data.dataType() == "json") {
        FirebaseJson &json = data.jsonObject();
        FirebaseJsonData jsonData;
        if (json.get(jsonData, "cmd")) currentCmd = jsonData.stringValue;
        if (json.get(jsonData, "horn")) digitalWrite(BUZZER, jsonData.intValue == 1 ? HIGH : LOW);
        moveCar(currentCmd);
    }
}

void handleOTA() {
    bool trigger = false;
    if (Firebase.RTDB.getBool(&fbdo, "/update/trigger") && fbdo.boolData()) {
        String url = "";
        if (Firebase.RTDB.getString(&fbdo, "/update/url")) {
            url = fbdo.stringData();
            stopMotors();
            Firebase.RTDB.endStream(&stream);
            
            WiFiClientSecure client;
            client.setInsecure();
            httpUpdate.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
            
            t_httpUpdate_return ret = httpUpdate.update(client, url);
            if (ret == HTTP_UPDATE_OK) {
                Firebase.RTDB.setBool(&fbdo, "/update/trigger", false);
                ESP.restart();
            } else {
                Firebase.RTDB.setBool(&fbdo, "/update/trigger", false);
                Firebase.RTDB.beginStream(&stream, "/rc_car");
            }
        }
    }
}

// ==========================================
// SETUP & LOOP
// ==========================================
void setup() {
    Serial.begin(115200);

    pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT);
    pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT);
    pinMode(BUZZER, OUTPUT);
    pinMode(TRIG, OUTPUT); pinMode(ECHO, INPUT);

    // Cấu hình LEDC PWM (Core < 3.0)
    ledcSetup(LEDC_CHANNEL_A, LEDC_BASE_FREQ, LEDC_TIMER_13_BIT);
    ledcAttachPin(ENA, LEDC_CHANNEL_A);
    ledcSetup(LEDC_CHANNEL_B, LEDC_BASE_FREQ, LEDC_TIMER_13_BIT);
    ledcAttachPin(ENB, LEDC_CHANNEL_B);

    stopMotors();

    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }

    config.api_key = API_KEY;
    config.database_url = DATABASE_URL;
    auth.user.email = USER_EMAIL;
    auth.user.password = USER_PASSWORD;

    Firebase.begin(&config, &auth);
    Firebase.RTDB.beginStream(&stream, "/rc_car");
    Firebase.RTDB.setStreamCallback(&stream, streamCallback, [](bool t){});
}

void loop() {
    // Đo khoảng cách vật cản
    digitalWrite(TRIG, LOW); delayMicroseconds(2);
    digitalWrite(TRIG, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG, LOW);
    long duration = pulseIn(ECHO, HIGH, 20000);
    dist = (duration == 0) ? 400 : (int)(duration * 0.034 / 2);

    // Tự động dừng nếu lệnh hiện tại là Tiến mà gặp vật cản
    if (dist > 0 && dist <= 20 && (currentCmd == "F" || currentCmd == "FL" || currentCmd == "FR")) {
        stopMotors();
        currentCmd = "S";
    }

    if (Firebase.ready()) {
        // Heartbeat để App biết xe đang sống
        if (millis() - lastHeartbeatSend > 5000) {
            Firebase.RTDB.setIntAsync(&fbdo, "/rc_car/heartbeat", millis());
            lastHeartbeatSend = millis();
        }
        // Gửi khoảng cách lên App
        if (millis() - lastDistMillis > 500) {
            Firebase.RTDB.setIntAsync(&fbdo, "/rc_car/distance", dist);
            lastDistMillis = millis();
        }
        // Kiểm tra OTA mỗi 10 giây
        static unsigned long otaTimer = 0;
        if (millis() - otaTimer > 10000) {
            handleOTA();
            otaTimer = millis();
        }
    }
}