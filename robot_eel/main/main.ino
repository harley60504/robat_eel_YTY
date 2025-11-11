#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include "esp_wifi.h"
#include <FS.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_ADS1X15.h>
#include <PL_ADXL355.h>

#include "config.h"
#include "utils.h"
#include "wifi_mgr.h"
#include "index_html.h"
#include "web_ui.h"
#include "logging.h"
#include "sensors_ads.h"
#include "sensors_adxl.h"
#include "cpg.h"
#include "servo.h"

WebServer server(80);

// WiFi
const char* ssid1 = "YTY_2.4g";
const char* password1 = "weareytylab";
const char* ssid2 = "Sunday";
const char* password2 = "qwer1234";
const char* AP_SSID = "ESP32_Controller_AP";
const char* AP_PASS = "12345678";
const char* HOSTNAME = "esp32-controller";
String connectedSSID = "未連接";

// Servo / params
float servoDefaultAngles[bodyNum] = {120,120,120,120,120,120};
float angleDeg[bodyNum];
float Ajoint = 20.0f;
float frequency = 0.7f;
float lambda = 0.7f;
float L = 0.85f;
float adsMinValidVoltage = 0.6f;
bool  isPaused = false;
int   controlMode = 0;
bool  useFeedback = false;
float feedbackGain = 1.0f;

// CPG
HopfOscillator cpg[bodyNum];

// ADS1115
Adafruit_ADS1115 ads1;
Adafruit_ADS1115 ads2;
float adsVoltage1[4] = {0,0,0,0};
float adsVoltage2[4] = {0,0,0,0};
float ads1Diff[3]    = {0,0,0};

// Logging
unsigned long g_lastLogTime = 0;

// ADXL355
PL::ADXL355 adxl355;
volatile float adxlX = 0.0f, adxlY = 0.0f, adxlZ = 0.0f;
volatile float pitchDeg = 0.0f, rollDeg = 0.0f;

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("✅ 原生 USB 已啟動");

  Serial1.begin(115200, SERIAL_8N1, -1, SERVO_TX_PIN);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setTimeout(50);
  if (!ads1.begin(0x48, &Wire)) Serial.println("❌ 找不到 ADS1115 #1 (0x48)");
  else { ads1.setGain(GAIN_TWOTHIRDS); Serial.println("✅ ADS1115 #1 初始化完成"); }
  if (!ads2.begin(0x49, &Wire)) Serial.println("❌ 找不到 ADS1115 #2 (0x49)");
  else { ads2.setGain(GAIN_TWOTHIRDS); Serial.println("✅ ADS1115 #2 初始化完成"); }

  initADXL();
  initLogFile();
  connectToWiFi();

  setupWebServer();
  Serial.printf("🌐 Web 伺服器啟動：AP http://%s  ",
                WiFi.softAPIP().toString().c_str());
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("|  STA http://%s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("|  STA 未連線");
  }

  initCPG();

  xTaskCreatePinnedToCore(servoTask, "ServoTask", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(i2cTask,  "I2CTask",   4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(adxlTask, "ADXLTask",  4096, NULL, 1, NULL, 1);
}

void loop() {
  server.handleClient();
}
