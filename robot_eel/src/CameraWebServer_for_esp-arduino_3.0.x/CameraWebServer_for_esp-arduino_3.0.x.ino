#include "esp_camera.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ctype.h>   // isdigit

#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"

// ===== Wi-Fi（固定連線到控制 AP）=====
static const char* CTRL_AP_SSID = "ESP32_Controller_AP";
static const char* CTRL_AP_PASS = "12345678";

static const IPAddress STATIC_IP(192, 168, 4, 201);
static const IPAddress GATEWAY  (192, 168, 4, 1);
static const IPAddress SUBNET   (255, 255, 255, 0);
static const IPAddress DNS1     (192, 168, 4, 1);

static const char* HOSTNAME = "esp32-cam";

AsyncWebServer server(80);

// ===== 互斥與串流限制 =====
static SemaphoreHandle_t s_cam_mutex = nullptr;
static volatile int s_stream_clients = 0;
static const int MAX_STREAM_CLIENTS = 1;

// ===== 解析度設定：支援「數字 index」或「文字名稱」=====
static bool setFrameSizeByName(const String& name) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return false;

  // 名稱順序與 Web 下拉選單對齊
  if (name.equalsIgnoreCase("QQVGA")) return s->set_framesize(s, FRAMESIZE_QQVGA) == ESP_OK;
  if (name.equalsIgnoreCase("QVGA"))  return s->set_framesize(s, FRAMESIZE_QVGA ) == ESP_OK;
  if (name.equalsIgnoreCase("VGA"))   return s->set_framesize(s, FRAMESIZE_VGA  ) == ESP_OK;
  if (name.equalsIgnoreCase("SVGA"))  return s->set_framesize(s, FRAMESIZE_SVGA ) == ESP_OK;
  if (name.equalsIgnoreCase("XGA"))   return s->set_framesize(s, FRAMESIZE_XGA  ) == ESP_OK;
  if (name.equalsIgnoreCase("SXGA"))  return s->set_framesize(s, FRAMESIZE_SXGA ) == ESP_OK;
  if (name.equalsIgnoreCase("UXGA"))  return s->set_framesize(s, FRAMESIZE_UXGA ) == ESP_OK;
  if (name.equalsIgnoreCase("HD"))    return s->set_framesize(s, FRAMESIZE_HD   ) == ESP_OK; // 1280x720
  if (name.equalsIgnoreCase("FHD"))   return s->set_framesize(s, FRAMESIZE_FHD  ) == ESP_OK; // 1920x1080（若感測器支援）

  return false;
}

static bool setFrameSizeByAny(const String& v) {
  if (v.length() && isdigit((unsigned char)v[0])) {
    // 數字 index（官方 /control 風格）
    sensor_t* s = esp_camera_sensor_get();
    if (!s) return false;
    int idx = v.toInt();
    return s->set_framesize(s, (framesize_t)idx) == ESP_OK;
  }
  // 文字名稱
  return setFrameSizeByName(v);
}

// ===== 從查詢參數套用（供 /stream 進場時用）=====
static void applyFromRequest(AsyncWebServerRequest* req) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) return;
  if (!xSemaphoreTake(s_cam_mutex, pdMS_TO_TICKS(500))) return;

  if (req->hasParam("framesize"))  setFrameSizeByAny(req->getParam("framesize")->value());
  if (req->hasParam("quality"))    s->set_quality(s,    constrain(req->getParam("quality")->value().toInt(),    10, 63));
  if (req->hasParam("brightness")) s->set_brightness(s, constrain(req->getParam("brightness")->value().toInt(),  -2, 2));
  if (req->hasParam("contrast"))   s->set_contrast(s,   constrain(req->getParam("contrast")->value().toInt(),    -2, 2));
  if (req->hasParam("saturation")) s->set_saturation(s, constrain(req->getParam("saturation")->value().toInt(),  -2, 2));
  if (req->hasParam("hmirror"))    s->set_hmirror(s,    req->getParam("hmirror")->value().toInt() ? 1 : 0);
  if (req->hasParam("vflip"))      s->set_vflip(s,      req->getParam("vflip")->value().toInt()   ? 1 : 0);

  xSemaphoreGive(s_cam_mutex);
}

// ===== MJPEG 非阻塞回應 =====
class AsyncJpegStreamResponse : public AsyncAbstractResponse {
public:
  AsyncJpegStreamResponse() {
    _code = 200;
    _contentType = "multipart/x-mixed-replace; boundary=frame";
    _sendContentLength = false;
    _state = S_BOUNDARY;
    _fb = nullptr;
    _idx = 0; _hOff = 0; _tOff = 0;
  }
  ~AsyncJpegStreamResponse() override {
    if (_fb) esp_camera_fb_return(_fb);
    s_stream_clients--;
  }
  bool _sourceValid() const override { return true; }

  size_t _fillBuffer(uint8_t *buf, size_t maxLen) override {
    size_t out = 0;
    while (out < maxLen) {
      switch (_state) {
        case S_BOUNDARY: {
          const char *b = "--frame\r\n"; size_t bl = 9;
          size_t n = min(bl - _hOff, maxLen - out);
          memcpy(buf + out, b + _hOff, n);
          _hOff += n; out += n;
          if (_hOff == bl) { _hOff = 0; _state = S_HEADER; } else return out;
        } break;
        case S_HEADER: {
          if (!_fb) {
            if (!xSemaphoreTake(s_cam_mutex, 0)) return out;
            _fb = esp_camera_fb_get();
            xSemaphoreGive(s_cam_mutex);
            if (!_fb) return out;
            _idx = 0;
            _headerLen = snprintf(_header, sizeof(_header),
                                  "Content-Type: image/jpeg\r\nContent-Length: %u\r\n\r\n", _fb->len);
          }
          size_t n = min((size_t)_headerLen - _hOff, maxLen - out);
          memcpy(buf + out, _header + _hOff, n);
          _hOff += n; out += n;
          if (_hOff == (size_t)_headerLen) { _hOff = 0; _state = S_DATA; } else return out;
        } break;
        case S_DATA: {
          size_t left = _fb->len - _idx;
          size_t n = min(left, maxLen - out);
          memcpy(buf + out, _fb->buf + _idx, n);
          _idx += n; out += n;
          if (_idx == _fb->len) { _state = S_TAIL; _tOff = 0; } else return out;
        } break;
        case S_TAIL: {
          const char *t = "\r\n"; size_t tl = 2;
          size_t n = min(tl - _tOff, maxLen - out);
          memcpy(buf + out, t + _tOff, n);
          _tOff += n; out += n;
          if (_tOff == tl) {
            esp_camera_fb_return(_fb); _fb = nullptr; _state = S_BOUNDARY;
            // 提早返回，讓出 CPU，避免壅塞
            return out;
          } else return out;
        } break;
      }
    }
    return out;
  }

private:
  enum State { S_BOUNDARY, S_HEADER, S_DATA, S_TAIL } _state;
  camera_fb_t* _fb; size_t _idx;
  char _header[96]; int _headerLen = 0;
  size_t _hOff, _tOff;
};

// ===== Wi-Fi 連線 =====
static void connectToWiFi() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.config(STATIC_IP, GATEWAY, SUBNET, DNS1);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(CTRL_AP_SSID, CTRL_AP_PASS);

  Serial.printf("📡 連線到 AP：%s", CTRL_AP_SSID);
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("❌ 連線失敗");
    return;
  }

  WiFi.setSleep(false);
  WiFi.setTxPower(WIFI_POWER_15dBm);   // 高畫質先保守一點，避免回壓

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  }
  Serial.printf("✅ IP: %s\n", WiFi.localIP().toString().c_str());
}

// ===== 相機初始化 =====
static bool initCam() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer   = LEDC_TIMER_0;
  config.pin_d0       = Y2_GPIO_NUM;
  config.pin_d1       = Y3_GPIO_NUM;
  config.pin_d2       = Y4_GPIO_NUM;
  config.pin_d3       = Y5_GPIO_NUM;
  config.pin_d4       = Y6_GPIO_NUM;
  config.pin_d5       = Y7_GPIO_NUM;
  config.pin_d6       = Y8_GPIO_NUM;
  config.pin_d7       = Y9_GPIO_NUM;
  config.pin_xclk     = XCLK_GPIO_NUM;
  config.pin_pclk     = PCLK_GPIO_NUM;
  config.pin_vsync    = VSYNC_GPIO_NUM;
  config.pin_href     = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn     = PWDN_GPIO_NUM;
  config.pin_reset    = RESET_GPIO_NUM;

  // 穩定取向（利於高畫質）
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_VGA;     // 起手 VGA，之後可 /control 調到 HD/FHD
  config.jpeg_quality = 10;                // 數值越小畫質越好
  config.fb_count     = psramFound() ? 2 : 1; // 高畫質先少一點 buffer 比較穩
  config.fb_location  = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.grab_mode    = CAMERA_GRAB_WHEN_EMPTY; // 和官方一致，避免回壓

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("❌ camera init failed: 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, 1);
    s->set_hmirror(s, 0);
    s->set_brightness(s, 1);
    s->set_saturation(s, 1);
    s->set_dcw(s, 1);
    s->set_lenc(s, 1);
    s->set_bpc(s, 1);
    s->set_wpc(s, 1);
  }
  return true;
}

// ===== 啟動 Web（含 /status、/control）=====
static void startWeb() {
  // 根頁（簡單顯示）
  server.on("/", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/plain", "ESP32-CAM ready. Try /stream /snapshot /status /control /cam");
  });

  // 顯示 IP
  server.on("/ip", HTTP_GET, [](AsyncWebServerRequest* req){
    req->send(200, "text/plain", WiFi.localIP().toString());
  });

  // 單張快照
  server.on("/snapshot", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!xSemaphoreTake(s_cam_mutex, pdMS_TO_TICKS(500))) {
      req->send(503, "text/plain", "Camera Busy");
      return;
    }
    camera_fb_t* fb = esp_camera_fb_get();
    xSemaphoreGive(s_cam_mutex);
    if (!fb) {
      req->send(503, "text/plain", "Camera Busy");
      return;
    }
    AsyncWebServerResponse* res = req->beginResponse_P(200, "image/jpeg", fb->buf, fb->len);
    req->send(res);
    esp_camera_fb_return(fb);
  });

  // 串流（可帶參數 framesize/quality...）
  auto streamHandler = [](AsyncWebServerRequest* req){
    if (s_stream_clients >= MAX_STREAM_CLIENTS) {
      req->send(503, "text/plain", "Too many clients");
      return;
    }
    applyFromRequest(req);
    auto* res = new AsyncJpegStreamResponse();
    s_stream_clients++;
    req->send(res);
  };

  server.on("/stream", HTTP_GET, streamHandler);

  // 新增 /cam：給前端 <img src="/cam"> 使用（與 /stream 同行為）
  server.on("/cam", HTTP_GET, streamHandler);

  // 相機狀態（JSON）
  server.on("/status", HTTP_GET, [](AsyncWebServerRequest* req){
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
      req->send(500, "text/plain", "no sensor");
      return;
    }
    if (!xSemaphoreTake(s_cam_mutex, pdMS_TO_TICKS(200))) {
      req->send(503, "text/plain", "Camera Busy");
      return;
    }
    char json[256];
    snprintf(json, sizeof(json),
      "{\"framesize\":%d,\"quality\":%d,\"brightness\":%d,"
      "\"contrast\":%d,\"saturation\":%d,\"hmirror\":%d,\"vflip\":%d}",
      s->status.framesize, s->status.quality, s->status.brightness,
      s->status.contrast,  s->status.saturation,
      s->status.hmirror,   s->status.vflip);
    xSemaphoreGive(s_cam_mutex);
    req->send(200, "application/json", json);
  });

  // 控制（官方風格：/control?var=...&val=...）
  server.on("/control", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!req->hasParam("var") || !req->hasParam("val")) {
      req->send(400, "text/plain", "missing 'var' or 'val'");
      return;
    }
    sensor_t* s = esp_camera_sensor_get();
    if (!s) {
      req->send(500, "text/plain", "no sensor");
      return;
    }

    const String var = req->getParam("var")->value();
    const String val = req->getParam("val")->value();
    const int    vi  = val.toInt();

    if (!xSemaphoreTake(s_cam_mutex, pdMS_TO_TICKS(300))) {
      req->send(503, "text/plain", "Camera Busy");
      return;
    }

    esp_err_t err = ESP_OK;
    if      (var == "framesize")  err = setFrameSizeByAny(val) ? ESP_OK : ESP_FAIL;
    else if (var == "quality")    err = s->set_quality(s,    constrain(vi, 10, 63));
    else if (var == "brightness") err = s->set_brightness(s, constrain(vi, -2,  2));
    else if (var == "contrast")   err = s->set_contrast(s,   constrain(vi, -2,  2));
    else if (var == "saturation") err = s->set_saturation(s, constrain(vi, -2,  2));
    else if (var == "hmirror")    err = s->set_hmirror(s,    vi ? 1 : 0);
    else if (var == "vflip")      err = s->set_vflip(s,      vi ? 1 : 0);
    else {
      xSemaphoreGive(s_cam_mutex);
      req->send(404, "text/plain", "unknown var");
      return;
    }
    xSemaphoreGive(s_cam_mutex);

    if (err == ESP_OK) req->send(200, "text/plain", "OK");
    else               req->send(500, "text/plain", "ERR");
  });

  server.onNotFound([](AsyncWebServerRequest* r){
    r->send(404, "text/plain", "Not found");
  });

  server.begin();
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n🚀 ESP32-CAM (AsyncWebServer, /stream + /cam + /status + /control)");

  if (!initCam()) return;

  s_cam_mutex = xSemaphoreCreateMutex();
  if (!s_cam_mutex) {
    Serial.println("❌ create cam mutex failed");
    return;
  }

  connectToWiFi();
  if (WiFi.status() == WL_CONNECTED) {
    startWeb();
  }
}

void loop() {
  // 一切由 AsyncWebServer 處理，這裡不用做事
}
