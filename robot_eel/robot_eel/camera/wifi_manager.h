#pragma once
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"
#include "config.h"

static Preferences wifiPrefs;

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
//   格式: [{"ssid":"xxx","pass":"yyy"},...]
// =============================
inline std::vector<std::pair<String,String>> loadWiFiList() {
  wifiPrefs.begin("wifi", true);
  String raw = wifiPrefs.getString("list", "[]");
  wifiPrefs.end();

  std::vector<std::pair<String,String>> list;
  DynamicJsonDocument doc(2048);
  auto err = deserializeJson(doc, raw);
  if (err) return list;

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

  wifiPrefs.begin("wifi", false);
  wifiPrefs.putString("list", out);
  wifiPrefs.end();
}

// =============================
//  加入 / 替換 WiFi (同 SSID 視為更新)
// =============================
inline void addOrUpdateWifi(const String& ssid, const String& pass) {
  auto list = loadWiFiList();
  bool found = false;
  for (auto& w : list) {
    if (w.first == ssid) {
      w.second = pass;
      found = true;
      break;
    }
  }
  if (!found) {
    list.push_back({ssid, pass});
  }
  saveWiFiList(list);
}

// =============================
//  啟動 AP + 嘗試 STA
// =============================
inline void startWifiApSta() {
  Serial.println("\n========== WiFi 啟動 ==========");

  // AP+STA mode
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  // 啟動 AP
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(200);

  Serial.printf("📡 AP 啟動：SSID=%s  PASS=%s  IP=%s\n",
                AP_SSID, AP_PASS, WiFi.softAPIP().toString().c_str());

  // mDNS
  MDNS.end();
  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("ws", "tcp", 80);
    Serial.printf("🌐 mDNS：http://%s.local\n", HOSTNAME);
  }

  // 清 STA 狀態
  WiFi.disconnect(true, true);
  delay(200);
  WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
  WiFi.setHostname(HOSTNAME);

  // 嘗試從 NVS 連線
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

  if (connected) {
    Serial.printf("✅ STA 已連線：%s\n", WiFi.SSID().c_str());
    Serial.printf("🌐 STA IP：%s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("⚠️ STA 無法連線，目前只啟動 AP");
    WiFi.disconnect(true, true);
    WiFi.mode(WIFI_AP);
  }

  Serial.println("================================\n");
}

// =============================
//  產生 wifi_status JSON 給 WS 用
// =============================
inline void buildWifiStatusJson(JsonDocument& doc) {
  doc["type"] = "wifi_status";
  doc["ap_ssid"] = AP_SSID;
  doc["ap_ip"]   = WiFi.softAPIP().toString();

  wl_status_t st = WiFi.status();
  bool sta_ok = (st == WL_CONNECTED);

  doc["mode"] = (WiFi.getMode() == WIFI_AP_STA) ? "AP_STA" :
                (WiFi.getMode() == WIFI_AP)     ? "AP" :
                (WiFi.getMode() == WIFI_STA)    ? "STA" : "UNKNOWN";

  doc["sta_connected"] = sta_ok;
  if (sta_ok) {
    doc["sta_ssid"] = WiFi.SSID();
    doc["sta_ip"]   = WiFi.localIP().toString();
    doc["rssi"]     = WiFi.RSSI();
  } else {
    doc["sta_ssid"] = "";
    doc["sta_ip"]   = "";
    doc["rssi"]     = 0;
  }
}

// =============================
//  WiFi 掃描 → JSON 陣列
// =============================
inline void buildWifiScanJson(JsonDocument& doc) {
  doc["type"] = "wifi_scan";

  int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);
  JsonArray arr = doc.createNestedArray("list");

  for (int i = 0; i < n; ++i) {
    JsonObject o = arr.createNestedObject();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
    o["open"] = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
  }
  WiFi.scanDelete();
}
