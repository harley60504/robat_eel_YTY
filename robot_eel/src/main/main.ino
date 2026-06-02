#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WebServer.h>
#include "esp_wifi.h"
#include <FS.h>
#include <SPIFFS.h>
#include <Wire.h>
#include <SPI.h>
#include "driver/uart.h"

#include "config.h"
#include "utils.h"
#include "wifi_mgr.h"
#include "index_html.h"
#include "web_ui.h"
#include "logging.h"
#include "cpg.h"
#include "servo.h"

WebServer server(80);

// WiFi
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


// Logging
unsigned long g_lastLogTime = 0;


void setup() {
  Serial.begin(115200);
  delay(300);

  Serial1.begin(115200, SERIAL_8N1, SERVO_RX_PIN, SERVO_TX_PIN);

  uart_set_mode(UART_NUM_1, UART_MODE_RS485_HALF_DUPLEX);
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
  initLogFile();

  xTaskCreatePinnedToCore(
    servoTask,
    "servoTask",
    4096,
    nullptr,
    1,        // servo 優先度高
    nullptr,
    1
  );

}

void loop() {
  server.handleClient();
  logServoErrorAvgPerMinute();   // ★ 每分鐘寫平均誤差
}
