#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

namespace CamProxy {

  static WebServer* s_server = nullptr;
  static IPAddress  s_fixedIP(192,168,4,201);
  static uint16_t   s_camPort = 80;

  // upstream (camera)
  static WiFiClient s_upstream;
  static bool       s_streaming = false;

  // downstream (browser) - SINGLE CLIENT
  static WiFiClient s_downstream;
  static bool       s_clientActive = false;

  inline void setIP(const IPAddress& ip) { s_fixedIP = ip; }
  inline IPAddress getIP() { return s_fixedIP; }

  // =====================================================
  // Connect upstream camera
  // =====================================================
  static bool connectUpstream() {

    if (s_upstream.connected()) return true;

    Serial.printf("🔌 Connecting camera %s:%u\n",
                  s_fixedIP.toString().c_str(), s_camPort);

    if (!s_upstream.connect(s_fixedIP, s_camPort, 3000)) {
      Serial.println("❌ Camera connect failed.");
      return false;
    }

    // request MJPEG
    s_upstream.print(
      "GET /stream HTTP/1.1\r\n"
      "Host: " + s_fixedIP.toString() + "\r\n"
      "Connection: keep-alive\r\n\r\n"
    );

    // wait for HTTP header end
    int state = 0;
    unsigned long t0 = millis();
    while (state < 4 && millis() - t0 < 2000) {
      if (s_upstream.available()) {
        char c = s_upstream.read();
        state =
          (state==0 && c=='\r')?1:
          (state==1 && c=='\n')?2:
          (state==2 && c=='\r')?3:
          (state==3 && c=='\n')?4:0;
      }
      delay(1);
    }

    s_streaming = true;
    Serial.println("📺 Camera stream OK.");
    return true;
  }

  // =====================================================
  // Background stream task (single downstream)
  // =====================================================
  static void streamTask(void* pv) {

    uint8_t buf[1460];

    while (true) {

      // 如果有 client，但 client 已經死了 → 釋放
      if (s_clientActive && !s_downstream.connected()) {
        Serial.println("🔌 Downstream disconnected (cleanup)");
        s_downstream.stop();
        s_clientActive = false;
      }

      if (!connectUpstream()) {
        delay(2000);
        continue;
      }

      int n = s_upstream.read(buf, sizeof(buf));

      if (n > 0 && s_clientActive && s_downstream.connected()) {
        if (s_downstream.write(buf, n) == 0) {
          Serial.println("⚠️ Downstream write failed");
          s_downstream.stop();
          s_clientActive = false;
        }
      }
      else if (n < 0) {
        Serial.println("⚠️ Upstream lost");
        s_upstream.stop();
        s_streaming = false;
      }

      delay(1);
    }
}

  // =====================================================
  // /cam handler (ONLY ONE CLIENT)
  // =====================================================
  inline void handleStream() {

    if (s_clientActive) {
      s_server->send(429, "text/plain", "Only one client allowed");
      return;
    }

    if (!connectUpstream()) {
      s_server->send(503, "text/plain", "Camera offline");
      return;
    }

    s_downstream = s_server->client();
    s_clientActive = true;

    s_downstream.print(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
      "Cache-Control: no-cache\r\n\r\n"
    );

    Serial.println("▶️ Stream client connected");
  }

  // =====================================================
  // Control API Proxy (unchanged)
  // =====================================================
  inline void handleControl() {

    if (!s_server->hasArg("var") || !s_server->hasArg("val")) {
      s_server->send(400, "text/plain", "Missing var / val");
      return;
    }

    WiFiClient cam;
    if (!cam.connect(s_fixedIP, s_camPort)) {
      s_server->send(500, "text/plain", "Camera unreachable");
      return;
    }

    String req =
      "GET /control?var=" + s_server->arg("var") +
      "&val=" + s_server->arg("val") +
      " HTTP/1.1\r\nHost:" + s_fixedIP.toString() + "\r\n\r\n";

    cam.print(req);
    s_server->send(200, "text/plain", "OK");

    cam.stop();
  }

  // =====================================================
  // Mount routes
  // =====================================================
  inline void attach(WebServer& server) {

    s_server = &server;

    server.on("/cam", handleStream);
    server.on("/cam_control", handleControl);

    xTaskCreatePinnedToCore(
      streamTask,
      "camStream",
      6144,
      nullptr,
      1,
      nullptr,
      1
    );

    Serial.println("🎥 CamProxy Ready (single client)");
  }

} // namespace CamProxy
