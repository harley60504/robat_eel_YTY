#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

namespace CamProxy {

  static WebServer* s_server = nullptr;
  static IPAddress  s_fixedIP(192,168,4,201);
  static uint16_t   s_camPort = 80;

  static WiFiClient s_upstream;
  static bool       s_streaming = false;

  struct DownClient {
    WiFiClient client;
    bool active = true;
  };
  static std::vector<DownClient*> s_clients;
  static SemaphoreHandle_t s_clientLock = nullptr;

  inline void init() {
    if (!s_clientLock) s_clientLock = xSemaphoreCreateMutex();
  }

  inline void setIP(const IPAddress& ip) { s_fixedIP = ip; }
  inline IPAddress getIP() { return s_fixedIP; }

  // ====== Connect upstream camera ======
  static bool connectUpstream() {
    if (s_upstream.connected()) return true;
    Serial.printf("🔌 Connecting camera %s:%u\n", s_fixedIP.toString().c_str(), s_camPort);

    if (!s_upstream.connect(s_fixedIP, s_camPort, 3000)) {
      Serial.println("❌ Camera connect failed.");
      return false;
    }

    s_upstream.print("GET /stream HTTP/1.1\r\nHost: ");
    s_upstream.print(s_fixedIP.toString());
    s_upstream.print("\r\nConnection: keep-alive\r\n\r\n");

    int state = 0; unsigned long t0 = millis();
    while (state < 4 && millis() - t0 < 2000) {
      if (s_upstream.available()) {
        char c = s_upstream.read();
        state = (state==0 && c=='\r')?1:(state==1 && c=='\n')?2:(state==2 && c=='\r')?3:(state==3 && c=='\n')?4:0;
      }
      delay(1);
    }
    s_streaming = true;
    Serial.println("📺 Camera stream OK.");
    return true;
  }

  // ====== Camera stream task ======
  static void streamTask(void* pv) {
    uint8_t buf[1460];
    while (true) {
      if (!connectUpstream()) { delay(2000); continue; }
      int n = s_upstream.read(buf, sizeof(buf));

      if (n > 0) {
        xSemaphoreTake(s_clientLock, portMAX_DELAY);
        for (auto it = s_clients.begin(); it != s_clients.end();) {
          auto c = *it;
          if (!c->client.connected()) {
            it = s_clients.erase(it);
            delete c;
            continue;
          }
          if (c->active && c->client.write(buf, n) == 0)
            c->active = false;
          ++it;
        }
        xSemaphoreGive(s_clientLock);
      }
      else if (n < 0) {
        s_upstream.stop();
        Serial.println("⚠️ Stream lost.");
      }
      delay(1);
    }
  }

  // ====== /cam handler ======
  inline void handleStream() {
    if (!connectUpstream()) {
      s_server->send(503, "text/plain", "Camera offline");
      return;
    }
    WiFiClient client = s_server->client();
    client.print(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n\r\n"
    );
    auto d = new DownClient{client,true};
    xSemaphoreTake(s_clientLock, portMAX_DELAY);
    s_clients.push_back(d);
    xSemaphoreGive(s_clientLock);
  }

  // ====== 🚀 CONTROL API PROXY ======
  inline void handleControl() {
    if (!s_server->hasArg("var") || !s_server->hasArg("val")) {
      s_server->send(400,"text/plain","缺少 var / val");
      return;
    }

    String var = s_server->arg("var");
    String val = s_server->arg("val");

    WiFiClient cam;
    if (!cam.connect(s_fixedIP, s_camPort)) {
      s_server->send(500,"text/plain","Camera unreachable");
      return;
    }

    String req = "GET /control?var=" + var + "&val=" + val +
                 " HTTP/1.1\r\nHost:" + s_fixedIP.toString() + "\r\n\r\n";

    cam.print(req);

    s_server->setContentLength(CONTENT_LENGTH_UNKNOWN);
    s_server->send(200,"text/plain","");

    while (cam.connected()) {
      while (cam.available())
        s_server->client().write(cam.read());
    }
  }

  // ====== MOUNT ROUTES ======
  inline void attach(WebServer& server) {
    s_server = &server; init();

    server.on("/cam", handleStream);
    server.on("/cam_control", handleControl);

    xTaskCreatePinnedToCore(streamTask,"camStream",6144,nullptr,1,nullptr,1);
    Serial.println("🎥 CamProxy Ready.");
  }

}
