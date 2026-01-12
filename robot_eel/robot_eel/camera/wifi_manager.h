#pragma once
#include <WiFi.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <vector>
#include <algorithm>
#include "esp_wifi.h"
#include "config.h"

static Preferences wifiPrefs;
static bool mdnsStarted = false;

/* =====================================================
 * ⚠️【重要原則】
 * - WiFi.mode() 只能在 startWifiApSta() 呼叫一次
 * - 之後任何地方都「不得」再改 mode
 * - STA 連線只用 WiFi.begin()
 * ===================================================== */


/* =============================
 *  STA 立即連線（不寫 NVS）
 *  ❗ 不改 WiFi.mode / 不動 AP
 * ============================= */
inline bool wifiConnectNow(const String& ssid, const String& pass)
{
  // ❌ 絕對不能再呼叫 WiFi.mode()
  WiFi.begin(ssid.c_str(), pass.c_str());

  unsigned long t0 = millis();
  while (millis() - t0 < 8000) {
    if (WiFi.status() == WL_CONNECTED) {
      return true;
    }
    delay(200);
  }

  // ❌ 失敗也不能動 AP / mode
  WiFi.disconnect(false); // 只斷 STA
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

  for (JsonObject o : doc.as<JsonArray>()) {
    list.push_back({
      o["ssid"].as<String>(),
      o["pass"].as<String>()
    });
  }
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

  if (!found)
    list.push_back({ssid, pass});

  saveWiFiList(list);
}


/* =============================
 *  刪除
 * ============================= */
inline void deleteWifi(const String& ssid)
{
  auto list = loadWiFiList();

  list.erase(
    std::remove_if(
      list.begin(),
      list.end(),
      [&](auto &w){ return w.first == ssid; }
    ),
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

  // AP 永遠存在
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
 *  ✔ 不改任何狀態
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
 *  Wi-Fi 啟動（只呼叫一次）
 * ============================= */
inline void startWifiApSta()
{
  // ⭐ 只在這裡設定 mode，一次而已
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  // AP 永遠開
  WiFi.softAP(AP_SSID, AP_PASS);

  WiFi.setHostname(HOSTNAME);

  // mDNS 只啟一次
  if (!mdnsStarted && MDNS.begin(HOSTNAME)) {
    MDNS.addService("_ws", "_tcp", 80);
    mdnsStarted = true;
  }

  // 嘗試已儲存 STA（不論成功或失敗）
  auto list = loadWiFiList();
  for (auto &w : list) {
    if (wifiConnectNow(w.first, w.second)) {
      break;
    }
  }
}
