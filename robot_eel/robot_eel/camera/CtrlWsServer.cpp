#include "CtrlWsServer.h"

#include <ArduinoJson.h>
#include <esp_camera.h>
#include <WiFi.h>   // ⭐一定要有
#include "wifi_manager.h"
#include "CtrlUartBridge.h"
#include "config.h"

namespace {

// ================= WS =================
WebSocketsServer* g_ws = nullptr;

// ================= Control cache =================
ControlPacket g_pkt;

// ================= Servo (HIGH RATE) =================
unsigned long lastServoBroadcast = 0;
constexpr unsigned long SERVO_INTERVAL_MS = 25;

// ================= Low-rate snapshot =================
unsigned long lastSnapshot = 0;
constexpr unsigned long SNAPSHOT_INTERVAL_MS = 2000; // 2 秒

} // namespace

/* =========================================================
 * HIGH RATE — Servo Status
 * ========================================================= */
void CtrlWsServer::broadcastServoStatus(
    uint8_t count,
    uint32_t seq,
    const float *target,
    const float *actual,
    const float *error)
{
    if (!g_ws) return;

    unsigned long now = millis();
    if (now - lastServoBroadcast < SERVO_INTERVAL_MS) return;
    lastServoBroadcast = now;

    StaticJsonDocument<512> doc;
    doc["type"] = "servo_status";
    doc["seq"]  = seq;

    auto t = doc.createNestedArray("target");
    auto a = doc.createNestedArray("actual");
    auto e = doc.createNestedArray("error");

    for (int i = 0; i < count; i++) {
        t.add(target[i]);
        a.add(actual[i]);
        e.add(error[i]);
    }

    String out;
    serializeJson(doc, out);
    g_ws->broadcastTXT(out);
}

/* =========================================================
 * LOW RATE — Snapshot Tick ⭐核心
 * ========================================================= */
void CtrlWsServer::tick()
{
    if (!g_ws) return;

    unsigned long now = millis();
    if (now - lastSnapshot < SNAPSHOT_INTERVAL_MS) return;
    lastSnapshot = now;

    /* ---------- ctrl_params ---------- */
    {
        StaticJsonDocument<256> doc;
        doc["type"]      = "ctrl_params";
        doc["Ajoint"]    = g_pkt.Ajoint;
        doc["frequency"] = g_pkt.frequency;
        doc["lambda"]    = g_pkt.lambda;
        doc["L"]         = g_pkt.L;
        doc["paused"]    = g_pkt.isPaused;
        doc["mode"]      = g_pkt.controlMode;
        doc["feedback"]  = g_pkt.feedbackGain;

        String out;
        serializeJson(doc, out);
        g_ws->broadcastTXT(out);
    }

    /* ---------- wifi_status ---------- */
    {
        StaticJsonDocument<256> doc;
        buildWifiStatusJson(doc);

        String out;
        serializeJson(doc, out);
        g_ws->broadcastTXT(out);
    }

    /* ---------- wifi_list ---------- */
    {
        StaticJsonDocument<512> doc;
        doc["type"] = "wifi_list";

        JsonArray arr = doc.createNestedArray("list");
        for (auto &w : loadWiFiList()) {
            JsonObject o = arr.createNestedObject();
            o["ssid"] = w.first;
        }

        String out;
        serializeJson(doc, out);
        g_ws->broadcastTXT(out);
    }
}

/* =========================================================
 * INIT
 * ========================================================= */
void CtrlWsServer::begin(WebSocketsServer &ws)
{
    g_ws = &ws;

    /* ===== UART → HIGH RATE ===== */
    CtrlUartBridge::onServoStatus =
        [](const ServoStatus &s)
        {
            if (s.header != SERVO_STATUS_HEADER) return;
            CtrlWsServer::broadcastServoStatus(
                s.count, s.seq, s.target, s.actual, s.error
            );
        };

    /* ===== UART → cache only ===== */
    CtrlUartBridge::onCtrlParams =
        [](const ControlPacket &p)
        {
            g_pkt = p; // ⭐只更新快取
        };

    /* ===== WS RX (commands only) ===== */
    ws.onEvent([](uint8_t num,
                  WStype_t type,
                  uint8_t *payload,
                  size_t len)
    {
        if (type != WStype_TEXT) return;

        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, payload, len)) return;

        const char* cmd = doc["cmd"] | "";

        /* ---- Control ---- */
        if (!strcmp(cmd, "set_param")) {
            if (doc.containsKey("Ajoint"))     g_pkt.Ajoint        = doc["Ajoint"];
            if (doc.containsKey("frequency")) g_pkt.frequency    = doc["frequency"];
            if (doc.containsKey("lambda"))    g_pkt.lambda       = doc["lambda"];
            if (doc.containsKey("L"))         g_pkt.L            = doc["L"];
            if (doc.containsKey("paused"))    g_pkt.isPaused     = doc["paused"];
            if (doc.containsKey("mode"))      g_pkt.controlMode  = doc["mode"];
            if (doc.containsKey("feedback"))  g_pkt.feedbackGain = doc["feedback"];

            CtrlUartBridge::sendCtrlParams(g_pkt);
            return;
        }

        /* ---- Camera ---- */
        if (!strcmp(cmd, "camera_param")) {
            sensor_t *s = esp_camera_sensor_get();
            if (doc.containsKey("quality"))
                s->set_quality(s, doc["quality"]);
            if (doc.containsKey("framesize"))
                s->set_framesize(s, (framesize_t)doc["framesize"]);
            return;
        }

        /* ---- Wi-Fi ---- */
        
        if (!strcmp(cmd, "wifi_connect")) {
            wifiConnectNow(doc["ssid"], doc["pass"]);
            return;
        }

        if (!strcmp(cmd, "wifi_save")) {
            addOrUpdateWifi(doc["ssid"], doc["pass"]);
            return;
        }

        if (!strcmp(cmd, "wifi_delete")) {
            deleteWifi(doc["ssid"]);
            return;
        }
        if (!strcmp(cmd, "wifi_scan")) {
            int n = WiFi.scanNetworks(/*async=*/false, /*hidden=*/true);

            StaticJsonDocument<768> doc;
            doc["type"] = "wifi_scan";

            JsonArray arr = doc.createNestedArray("list");
            for (int i = 0; i < n; i++) {
                JsonObject o = arr.createNestedObject();
                o["ssid"] = WiFi.SSID(i);
                o["rssi"] = WiFi.RSSI(i);
            }

            String out;
            serializeJson(doc, out);
            g_ws->sendTXT(num, out);   // ⭐單播回給請求者

            WiFi.scanDelete();
            return;
        }
    });
}
