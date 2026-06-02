#pragma once
#include <FS.h>
#include <SPIFFS.h>
#include "config.h"
#include "index_html.h"
#include "CamProxy.h"
#include <HTTPClient.h>
#include <Preferences.h>

// -------------------------------------------
// WiFi item struct
// -------------------------------------------
struct WiFiItem {
  String ssid;
  String pass;
};

// -------------------------------------------
// Load WiFi list
// -------------------------------------------
std::vector<WiFiItem> loadSavedWiFi() {
  prefs.begin("wifi", true);
  String raw = prefs.getString("list", "[]");
  prefs.end();

  std::vector<WiFiItem> list;

  DynamicJsonDocument doc(1024);
  deserializeJson(doc, raw);

  for (JsonObject obj : doc.as<JsonArray>()) {
    WiFiItem item;
    item.ssid = obj["ssid"].as<String>();
    item.pass = obj["pass"].as<String>();
    list.push_back(item);
  }
  return list;
}

// -------------------------------------------
// Save WiFi list
// -------------------------------------------
void saveWiFiList(std::vector<WiFiItem>& list) {
  DynamicJsonDocument doc(1024);
  JsonArray arr = doc.to<JsonArray>();

  for (auto& w : list) {
    JsonObject o = arr.createNestedObject();
    o["ssid"] = w.ssid;
    o["pass"] = w.pass;
  }

  String out;
  serializeJson(arr, out);

  prefs.begin("wifi", false);
  prefs.putString("list", out);
  prefs.end();
}

// Camera IP
String cameraIP = "192.168.4.201";

inline void setupWebServer() {

  // ==========================================
  // Serve index.html
  // ==========================================
  server.on("/", []() {
    server.send(200, "text/html", INDEX_HTML);
  });

  // camera stream proxy
  CamProxy::attach(server);

  // ==========================================
  // WiFi Scan
  // ==========================================
  server.on("/wifi_scan", []() {
    int n = WiFi.scanNetworks();

    DynamicJsonDocument doc(2048);
    JsonArray arr = doc.createNestedArray("wifi");

    for (int i = 0; i < n; i++) {
      JsonObject w = arr.createNestedObject();
      w["ssid"] = WiFi.SSID(i);
      w["rssi"] = WiFi.RSSI(i);
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  // ==========================================
  // WiFi Saved List
  // ==========================================
  server.on("/wifi_saved", []() {
    auto list = loadSavedWiFi();  // ← 用 WiFiItem 結構

    DynamicJsonDocument doc(2048);
    JsonArray arr = doc.createNestedArray("saved");

    for (auto& w : list) {
      JsonObject o = arr.createNestedObject();
      o["ssid"] = w.ssid;
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });

  // ==========================================
  // WiFi Connect
  // ==========================================
  server.on("/wifi_connect", []() {
    if (!server.hasArg("ssid")) {
      server.send(400, "text/plain", "缺少 ssid");
      return;
    }

    String ssid = server.arg("ssid");
    String pass = server.hasArg("pass") ? server.arg("pass") : "";

    auto list = loadSavedWiFi();
    bool found = false;

    for (auto& w : list) {
      if (w.ssid == ssid) {
        if (pass.length() > 0)
          w.pass = pass;
        pass = w.pass;
        found = true;
      }
    }

    if (!found)
      list.push_back({ssid, pass});

    saveWiFiList(list);

    if (tryConnectOnce(ssid.c_str(), pass.c_str()))
      server.send(200, "text/plain", "✅ 已連線：" + ssid);
    else
      server.send(200, "text/plain", "❌ 連線失敗：" + ssid);
  });

  // ==========================================
  // WiFi Forget
  // ==========================================
  server.on("/wifi_forget", []() {
    if (!server.hasArg("ssid")) {
      server.send(400, "text/plain", "缺少 ssid");
      return;
    }

    String ssid = server.arg("ssid");
    auto list = loadSavedWiFi();

    std::vector<WiFiItem> newList;
    for (auto& w : list) {
      if (w.ssid != ssid)
        newList.push_back(w);
    }

    saveWiFiList(newList);
    server.send(200, "text/plain", "🗑 已刪除：" + ssid);
  });

  server.on("/wifi_reconnect", []() {
    if (!server.hasArg("ssid")) {
      server.send(400, "text/plain", "缺少 ssid");
      return;
    }

    String ssid = server.arg("ssid");
    auto list = loadSavedWiFi();

    String pass = "";
    for (auto& w : list) {
      if (w.ssid == ssid) {
        pass = w.pass;
        break;
      }
    }

    if (pass == "") {
      server.send(200, "text/plain", "❌ 找不到儲存的密碼");
      return;
    }

    if (tryConnectOnce(ssid.c_str(), pass.c_str())) {
      server.send(200, "text/plain", "✅ 已連線：" + ssid);
    } else {
      server.send(200, "text/plain", "❌ 連線失敗：" + ssid);
    }
  });

  server.on("/wifi_edit_pass", []() {
    if (!server.hasArg("ssid") || !server.hasArg("pass")) {
      server.send(400, "text/plain", "缺少 ssid 或 pass");
      return;
    }

    String ssid = server.arg("ssid");
    String pass = server.arg("pass");

    auto list = loadSavedWiFi();

    bool changed = false;
    for (auto& w : list) {
      if (w.ssid == ssid) {
        w.pass = pass;
        changed = true;
        break;
      }
    }

    if (!changed) {
      list.push_back({ssid, pass});
    }

    saveWiFiList(list);

    server.send(200, "text/plain", "✏️ 已更新密碼：" + ssid);
  });

  // 目前連線資訊
server.on("/wifi_current", []() {
  DynamicJsonDocument doc(256);

  if (WiFi.status() == WL_CONNECTED) {
    doc["connected"] = true;
    doc["ssid"] = WiFi.SSID();
    doc["ip"]   = WiFi.localIP().toString();
    doc["rssi"] = WiFi.RSSI();
  } else {
    doc["connected"] = false;
  }

  String out;
  serializeJson(doc, out);
  server.send(200, "application/json", out);
});



  // ==========================================
  // Robot control API （全部保留）
  // ==========================================
  server.on("/setMode", []() {
    if (server.hasArg("m")) {
      controlMode = server.arg("m").toInt();
      if (controlMode == 1) {
        extern void initCPG();
        initCPG();
      }
    }
    String modeName = (controlMode == 0) ? "Sin" :
                      (controlMode == 1) ? "CPG" :
                      (controlMode == 2) ? "Offset" : "Unknown";
    server.send(200, "text/plain", modeName);
  });

  server.on("/toggleFeedback", []() {
    useFeedback = !useFeedback;
    server.send(200, "text/plain", useFeedback ? "開啟" : "關閉");
  });

  server.on("/setFrequency", []() {
    if (server.hasArg("f")) frequency = server.arg("f").toFloat();
    server.send(200, "text/plain", String(frequency));
  });

  server.on("/setAmplitude", []() {
    if (server.hasArg("a")) Ajoint = server.arg("a").toFloat();
    server.send(200, "text/plain", String(Ajoint));
  });

  server.on("/setLambda", []() {
    if (server.hasArg("lambda")) lambda = server.arg("lambda").toFloat();
    server.send(200, "text/plain", String(lambda));
  });

  server.on("/setL", []() {
    if (server.hasArg("L")) L = server.arg("L").toFloat();
    server.send(200, "text/plain", String(L));
  });

  server.on("/setFeedbackGain", []() {
    if (server.hasArg("g")) feedbackGain = server.arg("g").toFloat();
    server.send(200, "text/plain", String(feedbackGain));
  });

  // ==========================================
  // Status JSON
  // ==========================================
  server.on("/status", []() {
    DynamicJsonDocument doc(2048);

    doc["frequency"] = frequency;
    doc["amplitude"] = Ajoint;
    doc["lambda"] = lambda;
    doc["L"] = L;
    doc["fbGain"] = feedbackGain;

    // 🕒 分鐘 + 秒
    unsigned long ms = millis();
    doc["uptime_min"] = ms / 60000;         // 整數分鐘
    doc["uptime_sec"] = (ms / 1000) % 60;   // 秒（0~59）

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
  });
  // ==========================================
  // Servo angle / error status
  // ==========================================
  server.on("/servo_status", []() {
    DynamicJsonDocument doc(2048);
    JsonArray arr = doc.createNestedArray("servos");

    for (int i = 0; i < bodyNum; i++) {
      JsonObject o = arr.createNestedObject();
      o["id"]        = i;
      o["targetDeg"] = servoState[i].targetDeg;
      o["actualDeg"] = servoState[i].actualDeg;
      o["errorDeg"]  = servoState[i].errorDeg;
    }

    String out;
    serializeJson(doc, out);
    server.send(200, "application/json", out);
});
  // ==========================================
  // CSV download
  // ==========================================
  server.on("/download", []() {
    if (!SPIFFS.exists("/data.csv")) {
      server.send(404, "text/plain", "data.csv 不存在");
      return;
    }
    File f = SPIFFS.open("/data.csv", "r");
    server.streamFile(f, "text/csv");
    f.close();
  });

  // ==========================================
  // Camera control proxy
  // ==========================================
  server.on("/cam_control", []() {
    if (!server.hasArg("var") || !server.hasArg("val")) {
      server.send(400, "text/plain", "缺少 var 或 val");
      return;
    }

    HTTPClient http;
    String url = "http://" + cameraIP + "/control?var=" +
                 server.arg("var") + "&val=" + server.arg("val");

    http.begin(url);
    int code = http.GET();
    String res = http.getString();
    http.end();

    if (code > 0)
      server.send(200, "text/plain", "OK:" + res);
    else
      server.send(500, "text/plain", "相機未回應");
  });

  server.begin();
  Serial.println("🌐 Web Server 啟動完成");
}
