#include "CtrlWsServer.h"
#include <ArduinoJson.h>
#include "wifi_manager.h"
#include "config.h"

namespace {

WebSocketsServer* g_ws = nullptr;
ControlPacket     g_pkt;
bool              debugMode = false;

} // anonymous

// ==================================================
// 廣播 ctrl_params
// ==================================================
void CtrlWsServer::broadcastCtrlParams(const ControlPacket &p)
{
    if (!g_ws) return;

    StaticJsonDocument<256> doc;

    doc["type"]        = "ctrl_params";
    doc["Ajoint"]      = p.Ajoint;
    doc["frequency"]   = p.frequency;
    doc["lambda"]      = p.lambda;
    doc["L"]           = p.L;
    doc["paused"]      = p.isPaused;
    doc["mode"]        = p.controlMode;
    doc["useFeedback"] = p.useFeedback;
    doc["feedbackGain"]= p.feedbackGain;

    String out;
    serializeJson(doc, out);
    g_ws->broadcastTXT(out);
}

// ==================================================
// 廣播 servo_status
// ==================================================
void CtrlWsServer::broadcastServoStatus(
    uint8_t  count,
    uint32_t seq,
    const float *target,
    const float *actual,
    const float *error)
{
    if (!g_ws) return;

    StaticJsonDocument<512> doc;

    doc["type"]  = "servo_status";
    doc["seq"]   = seq;
    doc["count"] = count;

    auto t = doc.createNestedArray("target");
    auto a = doc.createNestedArray("actual");
    auto e = doc.createNestedArray("error");

    for (int i = 0; i < count; ++i)
    {
        t.add(target[i]);
        a.add(actual[i]);
        e.add(error[i]);
    }

    String out;
    serializeJson(doc, out);
    g_ws->broadcastTXT(out);
}

// ==================================================
// WiFi Status / Scan
// ==================================================
void CtrlWsServer::sendWifiStatus(uint8_t clientNum, bool broadcast)
{
    if (!g_ws) return;

    StaticJsonDocument<512> doc;
    buildWifiStatusJson(doc);

    String out;
    serializeJson(doc, out);

    if (broadcast)
        g_ws->broadcastTXT(out);
    else
        g_ws->sendTXT(clientNum, out);
}

void CtrlWsServer::sendWifiScanResult(uint8_t clientNum)
{
    if (!g_ws) return;

    StaticJsonDocument<1024> doc;
    buildWifiScanJson(doc);

    String out;
    serializeJson(doc, out);
    g_ws->sendTXT(clientNum, out);
}

// ==================================================
// INIT
// ==================================================
void CtrlWsServer::begin(WebSocketsServer &ws)
{
    g_ws = &ws;

    // UART callback → ctrl_params
    CtrlUartBridge::onCtrlParams =
        [](const ControlPacket &p)
        {
            g_pkt = p;
            CtrlWsServer::broadcastCtrlParams(p);
        };

    // UART callback → servo_status
    CtrlUartBridge::onServoStatus =
        [](const ServoStatus &s)
        {
            if (s.header != SERVO_STATUS_HEADER) return;

            CtrlWsServer::broadcastServoStatus(
                s.count,
                s.seq,
                s.target,
                s.actual,
                s.error
            );
        };

    // WebSocket handler
    ws.onEvent([](uint8_t num,
                  WStype_t type,
                  uint8_t *payload,
                  size_t len)
    {
        if (type != WStype_TEXT) return;

        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, payload, len)) return;

        const char *cmd = doc["cmd"] | "";

        // ---- Debug toggle ----
        if (strcmp(cmd, "debug_on") == 0) {
            debugMode = true;
            g_ws->sendTXT(num, "{\"debug\":true}");
            return;
        }
        if (strcmp(cmd, "debug_off") == 0) {
            debugMode = false;
            g_ws->sendTXT(num, "{\"debug\":false}");
            return;
        }

        // ---- Control Params ----
        if (strcmp(cmd, "set_param") == 0)
        {
            if (doc.containsKey("Ajoint"))       g_pkt.Ajoint      = doc["Ajoint"];
            if (doc.containsKey("frequency"))    g_pkt.frequency   = doc["frequency"];
            if (doc.containsKey("lambda"))       g_pkt.lambda      = doc["lambda"];
            if (doc.containsKey("L"))            g_pkt.L           = doc["L"];
            if (doc.containsKey("paused"))       g_pkt.isPaused    = doc["paused"];
            if (doc.containsKey("mode"))         g_pkt.controlMode = doc["mode"];
            if (doc.containsKey("feedbackGain")) g_pkt.feedbackGain= doc["feedbackGain"];

            CtrlUartBridge::sendCtrlParams(g_pkt);
            g_ws->sendTXT(num, "{\"ok\":true}");
            return;
        }

        // ---- Camera Param (你原本就有的邏輯，可放回來)
        if (strcmp(cmd, "camera_param") == 0) {
            // 這段照你原本使用 esp_camera_sensor_get() 那段放回去即可
            // ...
            return;
        }

        // ---- WiFi Status ----
        if (strcmp(cmd, "wifi_status") == 0) {
            CtrlWsServer::sendWifiStatus(num, false);
            return;
        }

        // ---- WiFi Scan ----
        if (strcmp(cmd, "wifi_scan") == 0) {
            CtrlWsServer::sendWifiScanResult(num);
            return;
        }

        // ---- WiFi Save ----
        if (strcmp(cmd, "wifi_save") == 0) {
            const char* ssid = doc["ssid"] | "";
            const char* pass = doc["pass"] | "";
            if (strlen(ssid) > 0) {
                addOrUpdateWifi(String(ssid), String(pass));
                g_ws->sendTXT(num, "{\"ok\":true}");
            } else {
                g_ws->sendTXT(num, "{\"ok\":false,\"error\":\"ssid empty\"}");
            }
            return;
        }

        // ---- Unknown ----
        g_ws->sendTXT(num, "{\"error\":\"unknown cmd\"}");
    });
}
