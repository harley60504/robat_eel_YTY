#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <strings.h>  // for strcasecmp

// === Board selection ===
// Use the official Seeed "camera_pins.h" for XIAO ESP32S3 Sense.
// Make sure your board package provides CAMERA_MODEL_XIAO_ESP32S3.
#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"

// ===========================
// Wi-Fi (STA with static IP)
// ===========================
// Change these to your network (or keep if already correct)
static const char* STA_SSID = "Sunday";
static const char* STA_PASS = "qwer1234";

// Static IP set here (adjust to your LAN)
static const IPAddress STATIC_IP(192, 168, 1, 201);
static const IPAddress GATEWAY  (192, 168, 1, 1);
static const IPAddress SUBNET   (255, 255, 255, 0);
static const IPAddress DNS1     (192, 168, 1, 1);  // or 8.8.8.8

static const char* HOSTNAME = "esp32-cam";

// ===========================
// HTTP Server (esp_http_server)
// ===========================
#define STREAM_BOUNDARY "frame"
static const char *STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace; boundary=" STREAM_BOUNDARY;
static const char *STREAM_PART =
    "--" STREAM_BOUNDARY "\\r\\n"
    "Content-Type: image/jpeg\\r\\n"
    "Content-Length: %u\\r\\n\\r\\n";

static httpd_handle_t s_httpd = nullptr;         // :80 control server
static httpd_handle_t s_stream_httpd = nullptr;  // :81 stream server
static SemaphoreHandle_t s_stream_lock = nullptr;   // single-client lock
static SemaphoreHandle_t s_cam_lock    = nullptr;   // sensor config lock

// === Forward declarations ===

static esp_err_t root_handler(httpd_req_t *req);
static esp_err_t status_handler(httpd_req_t *req);
static esp_err_t set_handler(httpd_req_t *req);
static esp_err_t still_handler(httpd_req_t *req);
static esp_err_t stream_handler(httpd_req_t *req);
static httpd_handle_t startCameraServer();
static httpd_handle_t startStreamServer();
static void wifi_init_sta_static();

// ---- Utils: framesize <-> text ----
static const char* framesize_to_str(framesize_t fs) {
  switch (fs) {
    case FRAMESIZE_QQVGA: return "QQVGA";  // 160x120
    case FRAMESIZE_QVGA:  return "QVGA";   // 320x240
    case FRAMESIZE_HVGA:  return "HVGA";   // 480x320
    case FRAMESIZE_VGA:   return "VGA";    // 640x480
    case FRAMESIZE_SVGA:  return "SVGA";   // 800x600
    default:              return "QVGA";
  }
}

static framesize_t str_to_framesize(const char* s) {
  if (!s) return FRAMESIZE_QVGA;
  if (!strcasecmp(s, "QQVGA")) return FRAMESIZE_QQVGA;
  if (!strcasecmp(s, "QVGA"))  return FRAMESIZE_QVGA;
  if (!strcasecmp(s, "HVGA"))  return FRAMESIZE_HVGA;
  if (!strcasecmp(s, "VGA"))   return FRAMESIZE_VGA;
  if (!strcasecmp(s, "SVGA"))  return FRAMESIZE_SVGA;
  return FRAMESIZE_QVGA;
}

// ---- HTML (your custom frontend) ----
#include "index_myui.h"

// ---- / (root) ----
static esp_err_t root_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

// ---- /capture ----
static esp_err_t still_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Capture failed"); return ESP_FAIL; }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

// ---- /status ----
static esp_err_t status_handler(httpd_req_t *req) {
  sensor_t *s = esp_camera_sensor_get();
  if (!s) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sensor err"); return ESP_FAIL; }
  char resp[256];
  snprintf(resp, sizeof(resp),
           "{\"framesize\":\"%s\",\"quality\":%d,\"vflip\":%d,\"hmirror\":%d,\"brightness\":%d,\"contrast\":%d}",
           framesize_to_str((framesize_t)s->status.framesize), s->status.quality,
           s->status.vflip, s->status.hmirror, s->status.brightness, s->status.contrast);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  return httpd_resp_send(req, resp, HTTPD_RESP_USE_STRLEN);
}

// ---- /set ----
static esp_err_t set_handler(httpd_req_t *req) {
  size_t qlen = httpd_req_get_url_query_len(req) + 1;
  char *qstr = nullptr;
  char val[32];
  sensor_t *s = esp_camera_sensor_get();
  if (!s) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "sensor err"); return ESP_FAIL; }

  if (!s_cam_lock) s_cam_lock = xSemaphoreCreateMutex();

  if (qlen > 1) {
    qstr = (char*)malloc(qlen);
    if (qstr && httpd_req_get_url_query_str(req, qstr, qlen) == ESP_OK) {
      // framesize
      if (httpd_query_key_value(qstr, "framesize", val, sizeof(val)) == ESP_OK) {
        framesize_t fs = str_to_framesize(val);
        xSemaphoreTake(s_cam_lock, portMAX_DELAY);
        s->set_framesize(s, fs);
        xSemaphoreGive(s_cam_lock);
      }
      // quality
      if (httpd_query_key_value(qstr, "quality", val, sizeof(val)) == ESP_OK) {
        int q = atoi(val);
        if (q < 10) q = 10; if (q > 63) q = 63;
        xSemaphoreTake(s_cam_lock, portMAX_DELAY);
        s->set_quality(s, q);
        xSemaphoreGive(s_cam_lock);
      }
      // vflip
      if (httpd_query_key_value(qstr, "vflip", val, sizeof(val)) == ESP_OK) {
        int vf = atoi(val); vf = vf ? 1 : 0;
        xSemaphoreTake(s_cam_lock, portMAX_DELAY);
        s->set_vflip(s, vf);
        xSemaphoreGive(s_cam_lock);
      }
      // hmirror
      if (httpd_query_key_value(qstr, "hmirror", val, sizeof(val)) == ESP_OK) {
        int hm = atoi(val); hm = hm ? 1 : 0;
        xSemaphoreTake(s_cam_lock, portMAX_DELAY);
        s->set_hmirror(s, hm);
        xSemaphoreGive(s_cam_lock);
      }
      // brightness [-2..2]
      if (httpd_query_key_value(qstr, "brightness", val, sizeof(val)) == ESP_OK) {
        int b = atoi(val); if (b < -2) b = -2; if (b > 2) b = 2;
        xSemaphoreTake(s_cam_lock, portMAX_DELAY);
        s->set_brightness(s, b);
        xSemaphoreGive(s_cam_lock);
      }
      // contrast [-2..2]
      if (httpd_query_key_value(qstr, "contrast", val, sizeof(val)) == ESP_OK) {
        int c = atoi(val); if (c < -2) c = -2; if (c > 2) c = 2;
        xSemaphoreTake(s_cam_lock, portMAX_DELAY);
        s->set_contrast(s, c);
        xSemaphoreGive(s_cam_lock);
      }
    }
  }
  if (qstr) free(qstr);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  return httpd_resp_sendstr(req, "{\"ok\":true}");
}

// ---- /stream (MJPEG) ----
static esp_err_t stream_handler(httpd_req_t *req) {
  // single-client lock
  if (!s_stream_lock) s_stream_lock = xSemaphoreCreateMutex();
  if (xSemaphoreTake(s_stream_lock, 0) != pdTRUE) {
    httpd_resp_set_status(req, "503 Service Unavailable");
    httpd_resp_set_type(req, "text/plain");
    httpd_resp_sendstr(req, "busy");
    return ESP_OK;
  }

  httpd_resp_set_type(req, STREAM_CONTENT_TYPE);
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char hdrbuf[64];
  esp_err_t res = ESP_OK;

  Serial.println("stream: client connected");

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) { vTaskDelay(1); continue; }

    int hlen = snprintf(hdrbuf, sizeof(hdrbuf), STREAM_PART, fb->len);

    // header
    res = httpd_resp_send_chunk(req, hdrbuf, hlen);
    if (res != ESP_OK) { esp_camera_fb_return(fb); break; }

    // jpeg payload
    res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    if (res != ESP_OK) { esp_camera_fb_return(fb); break; }

    // tail
    res = httpd_resp_send_chunk(req, "\\r\\n", 2);
    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;

    vTaskDelay(1);
  }

  // end response
  httpd_resp_send_chunk(req, nullptr, 0);
  xSemaphoreGive(s_stream_lock);
  return ESP_OK;
}

// ====== :80 control server ======
static httpd_handle_t startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port        = 80;
  config.lru_purge_enable   = true;
  config.stack_size         = 4096;
  config.recv_wait_timeout  = 3;
  config.send_wait_timeout  = 3;

  if (httpd_start(&s_httpd, &config) == ESP_OK) {
    // /
    httpd_uri_t root_uri = {};
    root_uri.uri      = "/";
    root_uri.method   = HTTP_GET;
    root_uri.handler  = root_handler;
    root_uri.user_ctx = nullptr;
    httpd_register_uri_handler(s_httpd, &root_uri);

    // /capture
    httpd_uri_t still_uri = {};
    still_uri.uri      = "/capture";
    still_uri.method   = HTTP_GET;
    still_uri.handler  = still_handler;
    still_uri.user_ctx = nullptr;
    httpd_register_uri_handler(s_httpd, &still_uri);

    // /status
    httpd_uri_t status_uri = {};
    status_uri.uri      = "/status";
    status_uri.method   = HTTP_GET;
    status_uri.handler  = status_handler;
    status_uri.user_ctx = nullptr;
    httpd_register_uri_handler(s_httpd, &status_uri);

    // /set
    httpd_uri_t set_uri = {};
    set_uri.uri      = "/set";
    set_uri.method   = HTTP_GET;
    set_uri.handler  = set_handler;
    set_uri.user_ctx = nullptr;
    httpd_register_uri_handler(s_httpd, &set_uri);

    // optional: :80 fallback stream (if you want to allow /stream on 80 too)
    // httpd_uri_t stream80_uri = {};
    // stream80_uri.uri      = "/stream";
    // stream80_uri.method   = HTTP_GET;
    // stream80_uri.handler  = stream_handler;
    // stream80_uri.user_ctx = nullptr;
    // httpd_register_uri_handler(s_httpd, &stream80_uri);
  }
  return s_httpd;
}

// ====== :81 stream server ======
static httpd_handle_t startStreamServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port        = 81;     // dedicated stream port
  config.lru_purge_enable   = true;
  config.stack_size         = 4096;
  config.recv_wait_timeout  = 3;
  config.send_wait_timeout  = 3;

  esp_err_t e = httpd_start(&s_stream_httpd, &config);
  if (e == ESP_OK) {
    httpd_uri_t stream_uri = {};
    stream_uri.uri      = "/stream";
    stream_uri.method   = HTTP_GET;
    stream_uri.handler  = stream_handler;
    stream_uri.user_ctx = nullptr;
    httpd_register_uri_handler(s_stream_httpd, &stream_uri);
    Serial.println("stream server started on :81");
  } else {
    Serial.printf("stream httpd_start failed: %d\\n", (int)e);
  }
  return s_stream_httpd;
}

// ===========================
// Wi-Fi (STA with static IP)
// ===========================
static void wifi_init_sta_static() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);

  if (!WiFi.config(STATIC_IP, GATEWAY, SUBNET, DNS1)) {
    Serial.println("WiFi.config failed (still connecting)");
  } else {
    Serial.printf("Static IP: %s  GW=%s  MASK=%s\\n",
      STATIC_IP.toString().c_str(), GATEWAY.toString().c_str(), SUBNET.toString().c_str());
  }

  WiFi.setHostname(HOSTNAME);
  WiFi.begin(STA_SSID, STA_PASS);
  Serial.printf("Connecting to AP: %s", STA_SSID);
  for (int i = 0; i < 60 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(300); Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("STA connect failed");
    return;
  }

  Serial.printf("WiFi OK, IP: %s\\n", WiFi.localIP().toString().c_str());

  // Wi-Fi optimizations (optional)
  WiFi.setSleep(false);
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT40);
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  // mDNS
  MDNS.end();
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("mDNS: http://%s.local\\n", HOSTNAME);
  } else {
    Serial.println("mDNS start failed");
  }
}

// ===========================
// Camera init & start servers
// ===========================
void setup() {
  Serial.begin(115200);
  Serial.println("\\nBooting XIAO ESP32S3 camera...");

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

  config.xclk_freq_hz = 24000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size   = FRAMESIZE_QVGA;       // low latency
  config.jpeg_quality = 16;                   // 16~22 saves bandwidth
  config.fb_count     = 1;                    // low latency
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST;   // key for low latency

  if (!psramFound()) {
    Serial.println("No PSRAM detected, switching to DRAM");
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed (0x%x)\\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_vflip(s, 1);
    s->set_hmirror(s, 0);
    s->set_brightness(s, 1);
    s->set_saturation(s, 1);
  }
  Serial.println("Camera init OK");

  wifi_init_sta_static();
  if (WiFi.status() == WL_CONNECTED) {
    startCameraServer();     // :80 control
    startStreamServer();     // :81 stream
    Serial.printf("Control page: http://%s/ or http://%s.local/\\n",
                  WiFi.localIP().toString().c_str(), HOSTNAME);
    Serial.printf("Stream URL : http://%s:81/stream\\n",
                  WiFi.localIP().toString().c_str());
  }
}

void loop() {
  delay(10);
}
