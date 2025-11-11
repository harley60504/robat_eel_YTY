#pragma once
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_wifi.h"
#include "config.h"

inline bool tryConnectOnce(const char* ssid, const char* pass, uint16_t dots = 40) {
  WiFi.begin(ssid, pass);
  Serial.printf("WiFi 連線中（%s）", ssid);
  for (int i = 0; i < dots && WiFi.status() != WL_CONNECTED; ++i) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

inline void connectToWiFi() {
  WiFi.onEvent([](WiFiEvent_t e, WiFiEventInfo_t info) {
    if (e == ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      Serial.printf("⚠️ STA 斷線（reason=%d），維持 AP，嘗試重連…\n", info.wifi_sta_disconnected.reason);
      WiFi.reconnect();
    }
  });

  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);
  Serial.printf("📡 AP 啟動：SSID=%s  PASS=%s  IP=%s\n",
                AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());

  MDNS.end();
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("🌐 可用連線：http://%s.local\n", HOSTNAME);
  }

  WiFi.disconnect(true, true);
  delay(200);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(HOSTNAME);

  if (!tryConnectOnce(ssid1, password1)) {
    Serial.println("❌ 第一組失敗，改用第二組...");
    tryConnectOnce(ssid2, password2);
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("✅ 已連線至 %s\nIP 位址: %s\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  } else {
    Serial.println("⚠️ STA 未連線，僅提供 AP 模式操作");
  }

  Serial.printf("🔗 可使用：AP http://%s  |  mDNS http://%s.local\n",
                WiFi.softAPIP().toString().c_str(), HOSTNAME);
}
