#pragma once
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"
#include "config.h"

Preferences prefs;

// =============================
//  工具：嘗試連線一次
// =============================
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

// =============================
//  從 NVS 讀取 WiFi 清單
// =============================
inline std::vector<std::pair<String,String>> loadWiFiList() {
  prefs.begin("wifi", true);
  String raw = prefs.getString("list", "[]");
  prefs.end();

  std::vector<std::pair<String,String>> list;
  DynamicJsonDocument doc(2048);
  deserializeJson(doc, raw);

  for (JsonObject o : doc.as<JsonArray>()) {
    list.push_back({ o["ssid"].as<String>(), o["pass"].as<String>() });
  }
  return list;
}

// =============================
//  儲存 WiFi 清單
// =============================
inline void saveWiFiList(const std::vector<std::pair<String,String>>& list) {
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.to<JsonArray>();

  for (auto& w : list) {
    JsonObject o = arr.createNestedObject();
    o["ssid"] = w.first;
    o["pass"] = w.second;
  }

  String out;
  serializeJson(arr, out);

  prefs.begin("wifi", false);
  prefs.putString("list", out);
  prefs.end();
}

// =============================
//        WiFi 啟動流程
// =============================
inline void connectToWiFi() {
  Serial.println("\n========== WiFi 啟動 ==========");

  // 啟動 AP+STA
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);

  Serial.printf("📡 AP 啟動：SSID=%s  PASS=%s  IP=%s\n",
                AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());

  // mDNS
  MDNS.end();
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    Serial.printf("🌐 mDNS：http://%s.local\n", HOSTNAME);
  }

  WiFi.disconnect(true, true);
  delay(200);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(HOSTNAME);

  // 嘗試連線儲存 WiFi
  auto saved = loadWiFiList();
  bool connected = false;

  if (saved.size() > 0) {
    Serial.println("📘 已儲存 WiFi 清單，開始嘗試連線…");

    for (auto& w : saved) {
      Serial.printf("➡️ 嘗試：%s\n", w.first.c_str());
      if (tryConnectOnce(w.first.c_str(), w.second.c_str())) {
        connected = true;
        break;
      }
    }
  } else {
    Serial.println("⚠️ 沒有儲存的 WiFi 設定");
  }

  // 結果
  if (connected) {
    Serial.printf("✅ 已連線：%s\n", WiFi.SSID().c_str());
    Serial.printf("🌐 STA：http://%s.local (IP=%s)\n",
                  HOSTNAME,
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println("⚠️ 無法連線 → 啟動 AP-only 模式");
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_AP);
    delay(200);
    Serial.printf("📡 AP-only：http://%s\n", WiFi.softAPIP().toString().c_str());
  }

  Serial.println("================================\n");
}
