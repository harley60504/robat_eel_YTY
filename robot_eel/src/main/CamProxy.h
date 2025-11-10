#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include "esp_task_wdt.h"

namespace CamProxy {

  // ======= 相機固定 IP/Port（控制端） =======
  static WebServer* s_server = nullptr;
  static IPAddress  s_fixedIP(192, 168, 4, 201);
  static uint16_t   s_camPort = 80;

  inline void setIP(const IPAddress& ip) { s_fixedIP = ip; }
  inline IPAddress getIP()               { return s_fixedIP; }
  inline void setPort(uint16_t port)     { s_camPort = port; }
  inline uint16_t getPort()              { return s_camPort; }

  // ======= 任務上下文 =======
  struct CamTaskCtx {
    WiFiClient down;
    IPAddress  ip;
    uint16_t   port;
    String     path;
  };

  // ======= 上游 Header 解析 =======
  struct RespHeader {
    int status = 0;
    size_t content_len = 0;
    String content_type;
  };

  static bool readHeaders(WiFiClient& up, RespHeader& out, uint32_t timeout_ms = 2000) {
    out = RespHeader{};
    String line, head;
    unsigned long t0 = millis();
    while (millis() - t0 < timeout_ms) {
      while (up.available()) {
        char c = (char)up.read();
        head += c;
        if (head.endsWith("\r\n")) {
          line = head; head.clear();
          if (out.status == 0) {
            int sp1 = line.indexOf(' ');
            int sp2 = line.indexOf(' ', sp1 + 1);
            if (sp1 >= 0 && sp2 > sp1) out.status = line.substring(sp1 + 1, sp2).toInt();
          } else if (line.indexOf(':') > 0) {
            int colon = line.indexOf(':');
            String key = line.substring(0, colon); key.trim(); key.toLowerCase();
            String val = line.substring(colon + 1); val.trim();
            if (key == "content-length") out.content_len = val.toInt();
            else if (key == "content-type") out.content_type = val;
          }
          if (line == "\r\n") return true;
        }
      }
      delay(1);
    }
    return false;
  }

  // ======= 串流代理任務（/cam） =======
  static void camStreamTask(void* pv) {
    CamTaskCtx* ctx = static_cast<CamTaskCtx*>(pv);
    WiFiClient up;

    // 🟢 關閉看門狗，避免高畫質 WDT
    disableCore0WDT();
    disableCore1WDT();


    if (!up.connect(ctx->ip, ctx->port)) {
      ctx->down.print("HTTP/1.1 502 Bad Gateway\r\n\r\nCannot connect to camera.\r\n");
      ctx->down.stop(); delete ctx; vTaskDelete(nullptr); return;
    }

    up.print("GET " + ctx->path + " HTTP/1.1\r\nHost: ");
    up.print(ctx->ip.toString());
    up.print("\r\nConnection: keep-alive\r\n\r\n");

    ctx->down.print(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
      "Cache-Control: no-cache\r\n\r\n");

    RespHeader rh;
    readHeaders(up, rh, 3000);

    uint8_t buf[1460];
    uint32_t lastYield = millis();
    while (ctx->down.connected()) {
      int n = up.read(buf, sizeof(buf));
      if (n > 0) {
        if (ctx->down.write(buf, n) == 0) break;
      } else if (n < 0) break;

      // 🟢 每幀節流，防止 CPU 過載
      if (millis() - lastYield > 3) {
        vTaskDelay(2);
        lastYield = millis();
      }
    }

    up.stop(); ctx->down.stop(); delete ctx;
    vTaskDelete(nullptr);
  }

  // ======= 狀態代理（/cam_status） =======
  inline void handleStatus() {
    if (!s_server) return;
    WiFiClient up; up.setTimeout(3);
    if (!up.connect(s_fixedIP, s_camPort)) {
      s_server->send(502, "text/plain", "Cannot connect to camera.");
      return;
    }

    up.print("GET /status HTTP/1.1\r\nHost: ");
    up.print(s_fixedIP.toString());
    up.print("\r\nConnection: close\r\n\r\n");

    RespHeader rh;
    if (!readHeaders(up, rh, 2000) || rh.status != 200) {
      s_server->send(502, "text/plain", "Bad upstream response");
      up.stop();
      return;
    }

    String body;
    while (up.available()) body += (char)up.read();
    up.stop();
    s_server->send(200, (rh.content_type.length()? rh.content_type : "application/json"), body);
  }

  // ======= 控制代理（/cam_control） =======
  inline void handleControl() {
    if (!s_server) return;
    if (!s_server->hasArg("var") || !s_server->hasArg("val")) {
      s_server->send(400, "text/plain", "Missing var or val"); return;
    }

    String var = s_server->arg("var");
    String val = s_server->arg("val");

    WiFiClient up; up.setTimeout(3);
    if (!up.connect(s_fixedIP, s_camPort)) {
      s_server->send(502, "text/plain", "Cannot connect to camera.");
      return;
    }

    String path = "/control?var=" + var + "&val=" + val;
    up.print("GET " + path + " HTTP/1.1\r\nHost: ");
    up.print(s_fixedIP.toString());
    up.print("\r\nConnection: close\r\n\r\n");

    RespHeader rh;
    readHeaders(up, rh, 2000);
    up.stop();
    s_server->send(200, "text/plain", "OK");
  }

  // ======= 快照代理（/cam_snapshot） =======
  static void camSnapshotTask(void* pv) {
    CamTaskCtx* ctx = static_cast<CamTaskCtx*>(pv);
    WiFiClient up;

    // 🟢 關閉 watchdog（防止 snapshot 高畫質卡死）
    esp_task_wdt_delete(NULL);

    if (!up.connect(ctx->ip, ctx->port)) {
      ctx->down.print("HTTP/1.1 502 Bad Gateway\r\n\r\nCannot connect to camera.\r\n");
      ctx->down.stop(); delete ctx; vTaskDelete(nullptr); return;
    }

    up.print("GET " + ctx->path + " HTTP/1.1\r\nHost: ");
    up.print(ctx->ip.toString());
    up.print("\r\nConnection: close\r\n\r\n");

    RespHeader rh;
    if (!readHeaders(up, rh, 2000) || rh.status != 200) {
      ctx->down.print("HTTP/1.1 502 Bad Gateway\r\n\r\nBad response\r\n");
      up.stop(); ctx->down.stop(); delete ctx; vTaskDelete(nullptr); return;
    }

    ctx->down.print("HTTP/1.1 200 OK\r\nContent-Type: image/jpeg\r\n\r\n");

    uint8_t buf[1460];
    while (up.available() && ctx->down.connected()) {
      int n = up.read(buf, sizeof(buf));
      if (n <= 0) break;
      ctx->down.write(buf, n);
      vTaskDelay(1); // 🟢 Snapshot 節流避免阻塞
    }

    up.stop(); ctx->down.stop(); delete ctx; vTaskDelete(nullptr);
  }

  // ======= 掛載 HTTP Handler =======
  inline void attach(WebServer& server,
                     const char* routeStream   = "/cam",
                     const char* routeStatus   = "/cam_status",
                     const char* routeControl  = "/cam_control",
                     const char* routeSnapshot = "/cam_snapshot") {
    s_server = &server;
    server.on(routeStream, []() {
      CamTaskCtx* ctx = new CamTaskCtx{ s_server->client(), s_fixedIP, s_camPort, String("/stream") };
      xTaskCreatePinnedToCore(camStreamTask, "camStreamTask", 6144, ctx, 1, nullptr, 0);
    });
    server.on(routeStatus, handleStatus);
    server.on(routeControl, handleControl);
    server.on(routeSnapshot, []() {
      CamTaskCtx* ctx = new CamTaskCtx{ s_server->client(), s_fixedIP, s_camPort, String("/snapshot") };
      xTaskCreatePinnedToCore(camSnapshotTask, "camSnapTask", 6144, ctx, 1, nullptr, 0);
    });
    Serial.printf("📸 CamProxy ready → %s:%u\n", s_fixedIP.toString().c_str(), s_camPort);
  }

} // namespace CamProxy
