#pragma once
#include <FS.h>
#include <SPIFFS.h>
#include "config.h"
#include "index_html.h"
#include "CamProxy.h"
#include <HTTPClient.h>

// 🧠 相機 IP（依實際設定修改）
String cameraIP = "192.168.4.201";  // XIAO ESP32S3-CAM

inline void setupWebServer() {

  // --- 首頁 HTML ---
  server.on("/", []() { server.send(200, "text/html", INDEX_HTML); });

  // --- 串流代理 ---
  CamProxy::attach(server);

  // --- 模式與控制 ---
  server.on("/setMode", []() {
    if (server.hasArg("m")) {
      controlMode = server.arg("m").toInt();
      if (controlMode == 1) { extern void initCPG(); initCPG(); }
    }
    String modeName = (controlMode==0)?"Sin":(controlMode==1)?"CPG":(controlMode==2)?"Offset":"Unknown";
    server.send(200, "text/plain", modeName);
  });

  server.on("/toggleFeedback", []() {
    useFeedback = !useFeedback;
    server.send(200, "text/plain", String(useFeedback ? "開啟" : "關閉"));
  });

  server.on("/setFrequency", []() { if (server.hasArg("f")) frequency = server.arg("f").toFloat(); server.send(200, "text/plain", String(frequency)); });
  server.on("/setAmplitude", []() { if (server.hasArg("a")) Ajoint = server.arg("a").toFloat(); server.send(200, "text/plain", String(Ajoint)); });
  server.on("/setLambda",   []() { if (server.hasArg("lambda")) lambda = server.arg("lambda").toFloat(); server.send(200, "text/plain", String(lambda)); });
  server.on("/setL",        []() { if (server.hasArg("L")) L = server.arg("L").toFloat(); server.send(200, "text/plain", String(L)); });
  server.on("/setFeedbackGain", []() { if (server.hasArg("g")) feedbackGain = server.arg("g").toFloat(); server.send(200, "text/plain", String(feedbackGain)); });

  // --- 狀態 JSON ---
  server.on("/status", []() {
    String json = "{";
    json += "\"frequency\":" + String(frequency, 2) + ",";
    json += "\"amplitude\":" + String(Ajoint, 2) + ",";
    json += "\"lambda_input\":" + String(lambda * L, 2) + ",";
    json += "\"lambda\":" + String(lambda, 2) + ",";
    json += "\"L\":" + String(L, 2) + ",";
    String modeName = (controlMode==0)?"Sin":(controlMode==1)?"CPG":(controlMode==2)?"Offset":"Unknown";
    json += "\"mode\":\"" + modeName + "\",";
    json += "\"feedback\":\"" + String(useFeedback ? "開啟" : "關閉") + "\",";
    json += "\"fbGain\":" + String(feedbackGain, 2) + ",";
    json += "\"adxl_x_g\":" + String(adxlX, 4) + ",";
    json += "\"adxl_y_g\":" + String(adxlY, 4) + ",";
    json += "\"adxl_z_g\":" + String(adxlZ, 4) + ",";
    json += "\"pitch_deg\":" + String(pitchDeg, 2) + ",";
    json += "\"roll_deg\":"  + String(rollDeg, 2) + ",";
    for (int i=0;i<4;i++) json += "\"ads1_ch"+String(i)+"\":"+String(adsVoltage1[i],4)+",";
    for (int i=0;i<4;i++) { json += "\"ads2_ch"+String(i)+"\":"+String(adsVoltage2[i],4); if (i<3) json += ","; }
    json += ",\"uptime_min\":" + String(millis() / 1000.0 / 60.0, 3);
    json += "}";
    server.send(200, "application/json", json);
  });

  // --- 增減快速調整 ---
  server.on("/increase_freq",   []() { frequency = fminf(frequency + 0.1f, 3.0f); server.send(200, "ok"); });
  server.on("/decrease_freq",   []() { frequency = fmaxf(frequency - 0.1f, 0.1f); server.send(200, "ok"); });
  server.on("/increase_ajoint", []() { Ajoint    = fminf(Ajoint + 5.0f, 90.0f);  server.send(200, "ok"); });
  server.on("/decrease_ajoint", []() { Ajoint    = fmaxf(Ajoint - 5.0f, 0.0f);   server.send(200, "ok"); });
  server.on("/increase_lambda", []() { lambda    = fminf(lambda + 0.05f, 2.0f);  server.send(200, "ok"); });
  server.on("/decrease_lambda", []() { lambda    = fmaxf(lambda - 0.05f, 0.1f);  server.send(200, "ok"); });
  server.on("/increase_L",      []() { L         = fminf(L + 0.05f, 2.0f);       server.send(200, "ok"); });
  server.on("/decrease_L",      []() { L         = fmaxf(L - 0.05f, 0.1f);       server.send(200, "ok"); });

  // --- 暫停控制與重置 ---
  server.on("/toggle_pause", []() { isPaused = !isPaused; server.send(200, "ok"); });
  server.on("/reset_all", []() {
    frequency = 0.7f; Ajoint = 30.0f; adsMinValidVoltage = 0.6f; isPaused = false;
    if (controlMode == 1) { extern void initCPG(); initCPG(); }
    server.send(200, "ok");
  });

  // --- CSV 下載 ---
  server.on("/download", []() {
    if (!SPIFFS.exists("/data.csv")) { server.send(404, "text/plain", "data.csv 不存在"); return; }
    File f = SPIFFS.open("/data.csv", "r");
    server.streamFile(f, "text/csv");
    f.close();
  });

  // --- ✅ 新增：相機控制代理 ---
  server.on("/cam_control", []() {
    if (!server.hasArg("var") || !server.hasArg("val")) {
      server.send(400, "text/plain", "缺少參數 var 或 val");
      return;
    }
    String var = server.arg("var");
    String val = server.arg("val");

    HTTPClient http;
    String url = "http://" + cameraIP + "/control?var=" + var + "&val=" + val;
    http.begin(url);
    int httpCode = http.GET();
    String response = http.getString();
    http.end();

    if (httpCode > 0) {
      server.send(200, "text/plain", "OK: " + response);
    } else {
      server.send(500, "text/plain", "無法連線到相機");
    }
  });

  server.begin();
}
