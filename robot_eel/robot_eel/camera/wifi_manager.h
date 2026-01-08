#pragma once
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include "esp_wifi.h"
#include "config.h"

static Preferences wifiPrefs;
static bool mdnsStarted = false;

/* =============================
 *  STA 立即連線（不寫 NVS）
 * ============================= */
inline bool wifiConnectNow(const String& ssid, const String& pass)
{
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long t0 = millis();
  while (millis() - t0 < 8000) {
    if (WiFi.status() == WL_CONNECTED) return true;
    delay(200);
  }
  return false;
}

/* =============================
 *  NVS 讀取
 * ============================= */
inline std::vector<std::pair<String,String>> loadWiFiList()
{
  wifiPrefs.begin("wifi", true);
  String raw = wifiPrefs.getString("list", "[]");
  wifiPrefs.end();

  std::vector<std::pair<String,String>> list;
  DynamicJsonDocument doc(2048);
  if (deserializeJson(doc, raw)) return list;

  for (JsonObject o : doc.as<JsonArray>())
    list.push_back({ o["ssid"].as<String>(), o["pass"].as<String>() });

  return list;
}

/* =============================
 *  NVS 儲存
 * ============================= */
inline void saveWiFiList(const std::vector<std::pair<String,String>>& list)
{
  DynamicJsonDocument doc(2048);
  JsonArray arr = doc.to<JsonArray>();

  for (auto &w : list) {
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

/* =============================
 *  新增 / 更新
 * ============================= */
inline void addOrUpdateWifi(const String& ssid, const String& pass)
{
  auto list = loadWiFiList();
  bool found = false;

  for (auto &w : list) {
    if (w.first == ssid) {
      w.second = pass;
      found = true;
      break;
    }
  }
  if (!found) list.push_back({ssid, pass});
  saveWiFiList(list);
}

/* =============================
 *  刪除
 * ============================= */
inline void deleteWifi(const String& ssid)
{
  auto list = loadWiFiList();
  list.erase(
    std::remove_if(list.begin(), list.end(),
      [&](auto &w){ return w.first == ssid; }),
    list.end()
  );
  saveWiFiList(list);
}

/* =============================
 *  JSON：Wi-Fi 狀態
 * ============================= */
inline void buildWifiStatusJson(JsonDocument &doc)
{
  doc["type"] = "wifi_status";

  doc["ap_ssid"] = AP_SSID;
  doc["ap_ip"]   = WiFi.softAPIP().toString();

  bool connected = (WiFi.status() == WL_CONNECTED);
  doc["sta_connected"] = connected;

  if (connected) {
    doc["sta_ssid"] = WiFi.SSID();
    doc["sta_ip"]   = WiFi.localIP().toString();
    doc["rssi"]     = WiFi.RSSI();
  } else {
    doc["sta_ssid"] = "";
    doc["sta_ip"]   = "";
    doc["rssi"]     = 0;
  }
}

/* =============================
 *  JSON：Wi-Fi Scan
 * ============================= */
inline void buildWifiScanJson(JsonDocument &doc)
{
  doc["type"] = "wifi_scan";
  int n = WiFi.scanNetworks();
  JsonArray arr = doc.createNestedArray("list");

  for (int i = 0; i < n; i++) {
    JsonObject o = arr.createNestedObject();
    o["ssid"] = WiFi.SSID(i);
    o["rssi"] = WiFi.RSSI(i);
  }
}

/* =============================
 *  AP + 嘗試 STA
 * ============================= */
inline void startWifiApSta()
{
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  WiFi.setHostname(HOSTNAME);

  if (!mdnsStarted && MDNS.begin(HOSTNAME)) {
    MDNS.addService("_ws","_tcp",80);
    mdnsStarted = true;
  }

  auto list = loadWiFiList();
  for (auto &w : list) {
    if (wifiConnectNow(w.first, w.second)) break;
  }
}
