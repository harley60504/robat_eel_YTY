#pragma once
#include <WiFi.h>
#include <ESPmDNS.h>
#include "esp_wifi.h"
#include "config.h"

// 嘗試連線一次，如果失敗不重掃
inline bool tryConnectOnce(const char* ssid, const char* pass, uint16_t dots = 40) {
  WiFi.begin(ssid, pass);
  Serial.printf("📶 嘗試連線至 %s", ssid);
  for (int i = 0; i < dots && WiFi.status() != WL_CONNECTED; i++) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

inline void connectToWiFi() {

  Serial.println("\n========== WiFi 啟動 ==========");

  // 先啟動 AP（不影響 STA）
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);

  Serial.printf("📡 AP 啟動：SSID=%s  PASS=%s  IP=%s\n",
                AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());

  // 啟動 mDNS
  MDNS.end();
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("🌐 mDNS：http://%s.local\n", HOSTNAME);
  }

  // 確保 STA 是乾淨的狀態
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(HOSTNAME);

  // ==== 嘗試連線到家裡 WiFi ====
  bool connected = false;

  if (tryConnectOnce(ssid1, password1)) {
    connected = true;
  } else if (tryConnectOnce(ssid2, password2)) {
    connected = true;
  }

  // ==== 狀態分析 ====
  if (connected) {
    // 🎉 STA 已連線 → 保持 AP+STA 雙模式
    Serial.printf("✅ 已連線至 %s\nIP 位址: %s\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

    Serial.printf("🌐 可用：AP http://%s  |  STA http://%s.local\n",
                  WiFi.softAPIP().toString().c_str(), HOSTNAME);
  }
  else {
    // ❌ 無法連上網路 → 切為 AP-only（不掃描、不跳頻、不會卡）
    Serial.println("⚠️ 無法連線任何 STA WiFi → 切換為 AP-only 模式，避免卡頓！");

    WiFi.disconnect(true, true);   // 停止 STA
    WiFi.mode(WIFI_AP);           // 🟢 只保留 AP（串流最穩定）
    delay(200);

    Serial.printf("📡 AP-only 模式：http://%s\n", 
                  WiFi.softAPIP().toString().c_str());
  }

  Serial.println("================================\n");
}
