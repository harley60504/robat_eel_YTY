#include "CtrlWsServer.h"

#include <ArduinoJson.h>
#include <esp_camera.h>
#include <WiFi.h>
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
 * LOW RATE — ctrl_params snapshot（可留可不留）
 * ========================================================= */
void CtrlWsServer::tick()
{
    if (!g_ws) return;

    unsigned long now = millis();
    if (now - lastSnapshot < SNAPSHOT_INTERVAL_MS) return;
    lastSnapshot = now;

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

/* =========================================================
 * INIT
 * ========================================================= */
void CtrlWsServer::begin(WebSocketsServer &ws)
{
    g_ws = &ws;

    /* ===== UART → ServoStatus (HIGH RATE) ===== */
    CtrlUartBridge::onServoStatus =
        [](const ServoStatus &s)
        {
            if (s.header != SERVO_STATUS_HEADER) return;

            CtrlWsServer::broadcastServoStatus(
                s.count, s.seq, s.target, s.actual, s.error
            );
        };

    /* ===== UART → ctrl_params cache ===== */
    CtrlUartBridge::onCtrlParams =
        [](const ControlPacket &p)
        {
            g_pkt = p;
        };

    /* ===== WS RX ===== */
    ws.onEvent([](uint8_t num,
                  WStype_t type,
                  uint8_t *payload,
                  size_t len)
    {
        if (type != WStype_TEXT) return;

        StaticJsonDocument<512> doc;
        if (deserializeJson(doc, payload, len)) return;

        const char* cmd = doc["cmd"] | "";

        /* ---- Control ---- */
        if (!strcmp(cmd, "set_param")) {
            if (doc.containsKey("Ajoint"))     g_pkt.Ajoint        = doc["Ajoint"];
            if (doc.containsKey("frequency")) g_pkt.frequency     = doc["frequency"];
            if (doc.containsKey("lambda"))    g_pkt.lambda        = doc["lambda"];
            if (doc.containsKey("L"))         g_pkt.L             = doc["L"];
            if (doc.containsKey("paused"))    g_pkt.isPaused      = doc["paused"];
            if (doc.containsKey("mode"))      g_pkt.controlMode   = doc["mode"];
            if (doc.containsKey("feedback"))  g_pkt.feedbackGain  = doc["feedback"];

            CtrlUartBridge::sendCtrlParams(g_pkt);
            return;
        }

        /* ✅ Angle control：Flutter → WS → UART AnglePacket */
        if (!strcmp(cmd, "set_angle")) {
            if (!doc.containsKey("angles")) return;

            JsonArray arr = doc["angles"].as<JsonArray>();
            if (arr.isNull()) return;

            float tmp[bodyNum] = {0};   // ✅ 用 bodyNum
            uint8_t count = 0;

            for (JsonVariant v : arr) {
                if (count >= bodyNum) break;
                tmp[count++] = v.as<float>();
            }

            if (count == 0) return;

            // UART → 控制板：AnglePacket
            CtrlUartBridge::sendAngle(tmp, count);
            return;
        }

        /* ---- Camera ---- */
        if (!strcmp(cmd, "camera_param")) {
            sensor_t *s = esp_camera_sensor_get();
            if (!s) return;

            if (doc.containsKey("quality"))
                s->set_quality(s, doc["quality"]);

            if (doc.containsKey("framesize"))
                s->set_framesize(s, (framesize_t)doc["framesize"]);

            return;
        }
    });
}
