#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h> 
#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"

// ===========================
// Wi-Fi 設定（雙組備援）
// ===========================
const char *ssid1 = "YTY_2.4g";
const char *password1 = "weareytylab";
const char *ssid2 = "Sunday";
const char *password2 = "qwer1234";

String connectedSSID = "未連接";
WebServer server(80);

// ===========================
// Wi-Fi 自動連線
// ===========================

const char* HOSTNAME = "esp32-cam";  // 之後可用 http://esp32-cam.local 連線

void connectToWiFi() {
  WiFi.mode(WIFI_STA);

  // 確保用 DHCP（清掉任何舊的靜態設定）
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(HOSTNAME);

  auto tryConnect = [](const char* ssid, const char* pass) -> bool {
    WiFi.begin(ssid, pass);
    Serial.printf("WiFi 連線中（%s）", ssid);
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; ++i) {  // ~12s
      delay(300);
      Serial.print(".");
    }
    Serial.println();
    return WiFi.status() == WL_CONNECTED;
  };

  if (!tryConnect(ssid1, password1)) {
    Serial.println("❌ 第一組 WiFi 失敗，改用第二組...");
    WiFi.disconnect(true, true);
    delay(200);
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE); // 再確認 DHCP
    if (!tryConnect(ssid2, password2)) {
      Serial.println("❌ 無法連線任何 WiFi，將不啟動 Web 伺服器");
      // （可選）最後保底：開 AP 模式方便維護
      // WiFi.mode(WIFI_AP);
      // WiFi.softAP("ESP32_AP", "12345678");
      // Serial.printf("📶 AP 啟動，IP：%s\n", WiFi.softAPIP().toString().c_str());
      return;
    }
  }

  // 成功連線
  connectedSSID = WiFi.SSID();
  Serial.printf("✅ 已連線至 %s\nIP 位址: %s\n",
                connectedSSID.c_str(), WiFi.localIP().toString().c_str());

  // 啟用 mDNS，之後用 http://<HOSTNAME>.local 存取
  MDNS.end();  // 先清一次避免殘留
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80); // 你的 Web 伺服器若不是 80，改成對應埠
    Serial.printf("🌐 以名稱連線： http://%s.local\n", HOSTNAME);
  } else {
    Serial.println("⚠️ mDNS 啟動失敗");
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
