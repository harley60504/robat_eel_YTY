#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

namespace CamProxy {

  // ===== 固定相機 IP（預設 192.168.4.2）=====
  static WebServer* s_server = nullptr;
  static IPAddress  s_fixedIP(192, 168, 4, 201);
  static uint16_t   s_camPort = 80;

  // 對外 API（可在 setup 內覆寫 IP）
  inline void setIP(const IPAddress& ip) { s_fixedIP = ip; }
  inline IPAddress getIP()               { return s_fixedIP; }
  inline void setHost(const String&) {}  // 兼容舊介面，固定 IP 模式下不使用
  inline String getHost() { return String(); }

  // ===== 任務上下文 =====
  struct CamTaskCtx {
    WiFiClient down;
    IPAddress  ip;
    uint16_t   port;
  };

  // ===== 代理轉送任務（非阻塞）=====
  static void camProxyTask(void* pv) {
    CamTaskCtx* ctx = static_cast<CamTaskCtx*>(pv);

    WiFiClient up;
    up.setTimeout(3);
    if (!up.connect(ctx->ip, ctx->port)) {
      ctx->down.print("HTTP/1.1 502 Bad Gateway\r\n"
                      "Content-Type: text/plain\r\n\r\n"
                      "Cannot connect to camera.\r\n");
      ctx->down.stop();
      delete ctx;
      vTaskDelete(nullptr);
      return;
    }

    // 上游請求 /stream
    up.print("GET /stream HTTP/1.1\r\nHost: ");
    up.print(ctx->ip.toString());
    up.print("\r\nConnection: keep-alive\r\n\r\n");

    // 回覆前端（下游）
    ctx->down.print(
      "HTTP/1.1 200 OK\r\n"
      "Content-Type: multipart/x-mixed-replace; boundary=frame\r\n"
      "Cache-Control: no-store, no-cache, must-revalidate, max-age=0\r\n"
      "Pragma: no-cache\r\n\r\n"
    );

    // 略過上游 header
    int state = 0;
    unsigned long t0 = millis();
    while (state < 4 && (millis() - t0) < 2000) {
      while (up.available() && state < 4) {
        char c = up.read();
        state = (state==0 && c=='\r') ? 1 :
                (state==1 && c=='\n') ? 2 :
                (state==2 && c=='\r') ? 3 :
                (state==3 && c=='\n') ? 4 : 0;
      }
      vTaskDelay(1);
    }

    // MJPEG 轉送
    uint8_t buf[1460];
    for (;;) {
      if (!ctx->down.connected()) break;
      int n = up.read(buf, sizeof(buf));
      if (n > 0) {
        if (ctx->down.write(buf, n) == 0) break;
      } else if (n < 0) {
        break;
      } else {
        if (!up.connected()) break;
        vTaskDelay(1);
      }
      taskYIELD();
    }

    up.stop();
    ctx->down.stop();
    delete ctx;
    vTaskDelete(nullptr);
  }

  // ===== /cam handler：啟動任務即可 =====
  inline void handleProxy() {
    if (!s_server) return;

    if (WiFi.status() != WL_CONNECTED) {
      s_server->send(503, "text/plain", "Controller STA not connected.");
      return;
    }

    CamTaskCtx* ctx = new CamTaskCtx{ s_server->client(), s_fixedIP, s_camPort };
    // Core 0/1 擇一；堆疊可視情況調整
    xTaskCreatePinnedToCore(camProxyTask, "camProxyTask", 6144, ctx, 1, nullptr, 0);
    // 注意：此路徑直接對 client 輸出，不要再 server.send()
  }

  // ===== 診斷/設定路由 =====
  inline void handleGetIP() {
    if (!s_server) return;
    String out = "ip=" + s_fixedIP.toString();
    s_server->send(200, "text/plain", out);
  }
  inline void handleSetHost() {
    if (!s_server) return;
    s_server->send(200, "text/plain", "fixed-ip mode"); // 兼容舊路由
  }
  inline void handleSetIP() {
    if (!s_server) return;
    if (s_server->hasArg("ip")) {
      IPAddress ip;
      if (ip.fromString(s_server->arg("ip"))) {
        setIP(ip);
        s_server->send(200, "text/plain", "ok");
      } else {
        s_server->send(400, "text/plain", "bad ip");
      }
    } else {
      s_server->send(400, "text/plain", "missing 'ip'");
    }
  }

  // ===== 掛載到 WebServer =====
  inline void attach(WebServer& server,
                     const char* routeStream = "/cam",
                     const char* routeIP     = "/cam_ip",
                     const char* routeSetHost= "/set_cam_host",
                     const char* routeSetIP  = "/set_cam_ip") {
    s_server = &server;
    server.on(routeStream, handleProxy);
    server.on(routeIP,     handleGetIP);
    server.on(routeSetHost,handleSetHost);
    server.on(routeSetIP,  handleSetIP);
    Serial.printf("🎯 CamProxy fixed IP = %s:%u\n", s_fixedIP.toString().c_str(), s_camPort);
  }

} // namespace CamProxy
