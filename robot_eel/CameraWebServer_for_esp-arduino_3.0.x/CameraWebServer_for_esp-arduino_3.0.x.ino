#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>

#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"

// ===========================
// Wi-Fi 設定（雙組備援）
// ===========================
const char *ssid1 = "YTY_2.4g";
const char *password1 = "weareytylab";
const char *ssid2 = "TP-Link_9BD8_2.4g";
const char *password2 = "qwer4321";

String connectedSSID = "未連接";
WebServer server(80);

// ===========================
// Wi-Fi 自動連線
// ===========================
void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  
  // ✅ 固定 IP 設定
  IPAddress local_IP(192, 168, 0, 199);
  IPAddress gateway(192, 168, 0, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(local_IP, gateway, subnet);

  WiFi.begin(ssid1, password1);
  Serial.print("WiFi 連線中");

  for (int i = 0; i < 10 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    connectedSSID = WiFi.SSID();
    Serial.printf("✅ 已連線至 %s\nIP 位址: %s\n", connectedSSID.c_str(), WiFi.localIP().toString().c_str());
    return;
  }

  Serial.println("❌ 第一組 WiFi 失敗，改用第二組...");
  WiFi.begin(ssid2, password2);
  for (int i = 0; i < 10 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    connectedSSID = WiFi.SSID();
    Serial.printf("✅ 已連線至 %s\nIP 位址: %s\n", connectedSSID.c_str(), WiFi.localIP().toString().c_str());
  } else {
    Serial.println("❌ 無法連線任何 WiFi，將不啟動 Web 伺服器");
  }
}

// ===========================
// 主畫面 HTML
// ===========================
void handleRoot() {
  String html = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
    <meta charset="UTF-8">
    <title>XIAO ESP32S3 高速相機</title>
    <style>
      body {
        background:#0a0a0a;
        color:#fff;
        font-family:"Segoe UI",sans-serif;
        text-align:center;
      }
      h1 { color:#00e5ff; margin-top:10px; }
      #stream {
        width:800px; max-width:95%;
        margin-top:20px;
        border-radius:10px;
        box-shadow:0 0 25px rgba(0,255,255,0.4);
      }
    </style>
  </head>
  <body>
    <h1>⚡ XIAO ESP32S3 MJPEG 串流伺服器</h1>
    <div class="info">(320×240 @ ~25 FPS 高效模式)</div>
    <img id="stream" src="/stream"/>
  </body>
  </html>
  )rawliteral";
  server.send(200, "text/html", html);
}

// ===========================
// MJPEG 串流處理
// ===========================
void handleStream() {
  WiFiClient client = server.client();
  String response = "HTTP/1.1 200 OK\r\n"
                    "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n";
  client.print(response);

  while (client.connected()) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) continue;

    client.printf("--frame\r\nContent-Type: image/jpeg\r\n\r\n");
    client.write(fb->buf, fb->len);
    client.printf("\r\n");
    esp_camera_fb_return(fb);
    delay(3);  // 控制串流速度，減少延遲
  }
}

// ===========================
// 相機初始化
// ===========================
void setup() {
  Serial.begin(115200);
  Serial.println("\n🚀 啟動 XIAO ESP32S3 相機...");

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  config.xclk_freq_hz = 24000000;
  config.pixel_format  = PIXFORMAT_JPEG;
  config.frame_size    = FRAMESIZE_QVGA;   // 320x240 高 FPS 模式
  config.jpeg_quality  = 12;
  config.fb_count      = 2;
  config.fb_location   = CAMERA_FB_IN_PSRAM;
  config.grab_mode     = CAMERA_GRAB_LATEST;

  if (!psramFound()) {
    Serial.println("⚠️ 未偵測 PSRAM，改用 DRAM 模式");
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ 相機初始化失敗 (錯誤碼: 0x%x)\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, 1);
    s->set_hmirror(s, 0);
    s->set_brightness(s, 1);
    s->set_saturation(s, 1);
  }

  Serial.println("✅ 相機初始化成功");

  connectToWiFi();

  if (WiFi.status() == WL_CONNECTED) {
    server.on("/", handleRoot);
    server.on("/stream", handleStream);
    server.begin();
    Serial.printf("🌐 網頁伺服器啟動完成 → http://%s/\n", WiFi.localIP().toString().c_str());
  }
}

void loop() {
  server.handleClient();
}
