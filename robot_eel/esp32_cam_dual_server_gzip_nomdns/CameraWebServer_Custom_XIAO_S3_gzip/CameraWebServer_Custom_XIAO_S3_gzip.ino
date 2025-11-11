#include <Arduino.h>
#include "esp_camera.h"
#include <WiFi.h>
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <strings.h>
#include "esp_heap_caps.h"

#define CAMERA_MODEL_XIAO_ESP32S3
#include "camera_pins.h"

static const char* STA_SSID = "Sunday";
static const char* STA_PASS = "qwer1234";
static const char* HOSTNAME = "esp32-cam";

#define STREAM_BOUNDARY "frame"
static const char *STREAM_CONTENT_TYPE =
    "multipart/x-mixed-replace; boundary=" STREAM_BOUNDARY;
static const char *STREAM_PART =
    "--" STREAM_BOUNDARY "\\r\\n"
    "Content-Type: image/jpeg\\r\\n"
    "Content-Length: %u\\r\\n\\r\\n";

static httpd_handle_t s_httpd = nullptr;
static httpd_handle_t s_stream_httpd = nullptr;
static SemaphoreHandle_t s_stream_lock = nullptr;
static SemaphoreHandle_t s_cam_lock    = nullptr;

static const char* framesize_to_str(framesize_t fs) {
  switch (fs) {
    case FRAMESIZE_QQVGA: return "QQVGA";
    case FRAMESIZE_QVGA:  return "QVGA";
    case FRAMESIZE_HVGA:  return "HVGA";
    case FRAMESIZE_VGA:   return "VGA";
    case FRAMESIZE_SVGA:  return "SVGA";
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

#include "index_myui_gz.h"

static esp_err_t root_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  httpd_resp_set_hdr(req, "Pragma", "no-cache");
  return httpd_resp_send(req, (const char*)INDEX_HTML_GZ, INDEX_HTML_GZ_len);
}

static esp_err_t still_handler(httpd_req_t *req) {
  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) { httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Capture failed"); return ESP_FAIL; }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  esp_err_t res = httpd_resp_send(req, (const char*)fb->buf, fb->len);
  esp_camera_fb_return(fb);
  return res;
}

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
      if (httpd_query_key_value(qstr, "framesize", val, sizeof(val)) == ESP_OK) {
        framesize_t fs = str_to_framesize(val);
        xSemaphoreTake(s_cam_lock, portMAX_DELAY); s->set_framesize(s, fs); xSemaphoreGive(s_cam_lock);
      }
      if (httpd_query_key_value(qstr, "quality", val, sizeof(val)) == ESP_OK) {
        int q = atoi(val); if (q < 10) q = 10; if (q > 63) q = 63;
        xSemaphoreTake(s_cam_lock, portMAX_DELAY); s->set_quality(s, q);     xSemaphoreGive(s_cam_lock);
      }
      if (httpd_query_key_value(qstr, "vflip", val, sizeof(val)) == ESP_OK) {
        int vf = atoi(val); vf = vf ? 1 : 0;
        xSemaphoreTake(s_cam_lock, portMAX_DELAY); s->set_vflip(s, vf);      xSemaphoreGive(s_cam_lock);
      }
      if (httpd_query_key_value(qstr, "hmirror", val, sizeof(val)) == ESP_OK) {
        int hm = atoi(val); hm = hm ? 1 : 0;
        xSemaphoreTake(s_cam_lock, portMAX_DELAY); s->set_hmirror(s, hm);    xSemaphoreGive(s_cam_lock);
      }
      if (httpd_query_key_value(qstr, "brightness", val, sizeof(val)) == ESP_OK) {
        int b = atoi(val); if (b < -2) b = -2; if (b > 2) b = 2;
        xSemaphoreTake(s_cam_lock, portMAX_DELAY); s->set_brightness(s, b);  xSemaphoreGive(s_cam_lock);
      }
      if (httpd_query_key_value(qstr, "contrast", val, sizeof(val)) == ESP_OK) {
        int c = atoi(val); if (c < -2) c = -2; if (c > 2) c = 2;
        xSemaphoreTake(s_cam_lock, portMAX_DELAY); s->set_contrast(s, c);    xSemaphoreGive(s_cam_lock);
      }
    }
  }
  if (qstr) free(qstr);

  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store, no-cache, must-revalidate, max-age=0");
  return httpd_resp_sendstr(req, "{\"ok\":true}");
}

static esp_err_t stream_handler(httpd_req_t *req) {
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

  while (true) {
    camera_fb_t *fb = esp_camera_fb_get();
    if (!fb) { vTaskDelay(1); continue; }

    int hlen = snprintf(hdrbuf, sizeof(hdrbuf), STREAM_PART, fb->len);
    res = httpd_resp_send_chunk(req, hdrbuf, hlen);
    if (res != ESP_OK) { esp_camera_fb_return(fb); break; }
    res = httpd_resp_send_chunk(req, (const char*)fb->buf, fb->len);
    if (res != ESP_OK) { esp_camera_fb_return(fb); break; }
    res = httpd_resp_send_chunk(req, "\\r\\n", 2);
    esp_camera_fb_return(fb);
    if (res != ESP_OK) break;
    vTaskDelay(1);
  }
  httpd_resp_send_chunk(req, nullptr, 0);
  xSemaphoreGive(s_stream_lock);
  return ESP_OK;
}

static httpd_handle_t startCameraServer() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 80;
  cfg.lru_purge_enable = true;
  cfg.max_open_sockets = 3;
  cfg.recv_wait_timeout = 3;
  cfg.send_wait_timeout = 3;
  if (httpd_start(&s_httpd, &cfg) != ESP_OK) { Serial.println("control httpd_start failed"); return nullptr; }

  httpd_uri_t root_uri = {};
  root_uri.uri="/"; root_uri.method=HTTP_GET; root_uri.handler=root_handler;
  httpd_register_uri_handler(s_httpd, &root_uri);

  httpd_uri_t still_uri = {};
  still_uri.uri="/capture"; still_uri.method=HTTP_GET; still_uri.handler=still_handler;
  httpd_register_uri_handler(s_httpd, &still_uri);

  httpd_uri_t status_uri = {};
  status_uri.uri="/status"; status_uri.method=HTTP_GET; status_uri.handler=status_handler;
  httpd_register_uri_handler(s_httpd, &status_uri);

  httpd_uri_t set_uri = {};
  set_uri.uri="/set"; set_uri.method=HTTP_GET; set_uri.handler=set_handler;
  httpd_register_uri_handler(s_httpd, &set_uri);

  Serial.println("control server started on :80");
  return s_httpd;
}

static httpd_handle_t startStreamServer() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = 81;
  cfg.lru_purge_enable = true;
  cfg.max_open_sockets = 2;
  cfg.recv_wait_timeout = 3;
  cfg.send_wait_timeout = 3;

  Serial.printf("heap free 8bit:%u, min:%u, psram:%u\n",
    heap_caps_get_free_size(MALLOC_CAP_8BIT),
    heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT),
    (uint32_t)ESP.getFreePsram());

  if (httpd_start(&s_stream_httpd, &cfg) != ESP_OK) {
    Serial.println("stream httpd_start failed");
    return nullptr;
  }
  httpd_uri_t stream_uri = {};
  stream_uri.uri="/stream"; stream_uri.method=HTTP_GET; stream_uri.handler=stream_handler;
  httpd_register_uri_handler(s_stream_httpd, &stream_uri);
  Serial.println("stream server started on :81");
  return s_stream_httpd;
}

static void wifi_init_sta_dhcp() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(HOSTNAME);
  WiFi.begin(STA_SSID, STA_PASS);
  Serial.printf("Connecting to AP: %s", STA_SSID);
  for (int i=0;i<60 && WiFi.status()!=WL_CONNECTED;++i){ delay(300); Serial.print("."); }
  Serial.println();
  if (WiFi.status()!=WL_CONNECTED){ Serial.println("STA connect failed"); return; }
  Serial.printf("WiFi OK (DHCP), IP: %s\n", WiFi.localIP().toString().c_str());
}

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
  config.frame_size   = FRAMESIZE_QVGA;
  config.jpeg_quality = 18;
  config.fb_count     = 1;
  config.fb_location  = CAMERA_FB_IN_PSRAM;
  config.grab_mode    = CAMERA_GRAB_LATEST;

  if (!psramFound()) {
    Serial.println("No PSRAM detected, switching to DRAM");
    config.fb_location = CAMERA_FB_IN_DRAM;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) { Serial.printf("Camera init failed (0x%x)\n", err); return; }

  sensor_t *s = esp_camera_sensor_get();
  if (s) { s->set_vflip(s, 1); s->set_hmirror(s, 0); s->set_brightness(s, 1); s->set_saturation(s, 1); }
  Serial.println("Camera init OK");

  wifi_init_sta_dhcp();
  if (WiFi.status() == WL_CONNECTED) {
    startCameraServer();
    if (startStreamServer() == nullptr) {
      httpd_uri_t stream80_uri = {};
      stream80_uri.uri="/stream"; stream80_uri.method=HTTP_GET; stream80_uri.handler=stream_handler;
      httpd_register_uri_handler(s_httpd, &stream80_uri);
      Serial.println("stream fallback attached on :80 (/stream)");
    }
    Serial.printf("Control page: http://%s/\n", WiFi.localIP().toString().c_str());
    Serial.printf("Try stream  : http://%s:81/stream  (fallback: http://%s/stream)\n",
                  WiFi.localIP().toString().c_str(), WiFi.localIP().toString().c_str());
  }
}

void loop() { delay(10); }
