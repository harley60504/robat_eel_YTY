#include "CtrlWsServer.h"

#include <ArduinoJson.h>
#include <esp_camera.h>

#include "wifi_manager.h"
#include "config.h"

namespace {

// WebSocket instance
WebSocketsServer* g_ws = nullptr;

// 最新控制參數快取
ControlPacket g_pkt;

// Debug flag
bool debugMode = false;

// servo_status 廣播頻率限制（40 Hz）
unsigned long lastServoBroadcast = 0;
const unsigned long SERVO_INTERVAL_MS = 25;

} // anonymous namespace


// ==================================================
// 廣播 ctrl_params
// ==================================================
void CtrlWsServer::broadcastCtrlParams(const ControlPacket &p)
{
    if (!g_ws) return;

    StaticJsonDocument<256> doc;

    doc["type"]         = "ctrl_params";
    doc["Ajoint"]       = p.Ajoint;
    doc["frequency"]    = p.frequency;
    doc["lambda"]       = p.lambda;
    doc["L"]            = p.L;
    doc["paused"]       = p.isPaused;
    doc["mode"]         = p.controlMode;
    doc["useFeedback"]  = p.useFeedback;
    doc["feedbackGain"] = p.feedbackGain;

    String out;
    serializeJson(doc, out);
    g_ws->broadcastTXT(out);
}


// ==================================================
// 廣播 servo_status（含頻率限制）
// ==================================================
void CtrlWsServer::broadcastServoStatus(
    uint8_t  count,
    uint32_t seq,
    const float *target,
    const float *actual,
    const float *error)
{
    if (!g_ws) return;

    unsigned long now = millis();
    if (now - lastServoBroadcast < SERVO_INTERVAL_MS)
        return;

    lastServoBroadcast = now;

    StaticJsonDocument<512> doc;
    doc["type"]  = "servo_status";
    doc["seq"]   = seq;
    doc["count"] = count;

    JsonArray t = doc.createNestedArray("target");
    JsonArray a = doc.createNestedArray("actual");
    JsonArray e = doc.createNestedArray("error");

    for (int i = 0; i < count; ++i) {
        t.add(target[i]);
        a.add(actual[i]);
        e.add(error[i]);
    }

    String out;
    serializeJson(doc, out);
    g_ws->broadcastTXT(out);
}


// ==================================================
// Wi-Fi Status
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


// ==================================================
// Wi-Fi Scan
// ==================================================
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
// Wi-Fi List（⭐新增，不影響原本任何功能）
// ==================================================
static void sendWifiList(uint8_t clientNum)
{
    if (!g_ws) return;

    StaticJsonDocument<512> doc;
    doc["type"] = "wifi_list";

    JsonArray arr = doc.createNestedArray("list");
    for (auto &w : loadWiFiList()) {
        JsonObject o = arr.createNestedObject();
        o["ssid"] = w.first;
    }

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

    // UART → ctrl_params
    CtrlUartBridge::onCtrlParams =
        [](const ControlPacket &p)
        {
            g_pkt = p;
            CtrlWsServer::broadcastCtrlParams(p);
        };

    // UART → servo_status
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


    // ============================
    // WebSocket handler
    // ============================
    ws.onEvent([](uint8_t num,
                  WStype_t type,
                  uint8_t *payload,
                  size_t len)
    {
        if (type != WStype_TEXT) return;

        StaticJsonDocument<256> doc;
        if (deserializeJson(doc, payload, len)) return;

        const char* cmd = doc["cmd"] | "";

        // ---------- Debug ----------
        if (!strcmp(cmd, "debug_on")) {
            debugMode = true;
            g_ws->sendTXT(num, "{\"type\":\"debug\",\"debug\":true}");
            return;
        }

        if (!strcmp(cmd, "debug_off")) {
            debugMode = false;
            g_ws->sendTXT(num, "{\"type\":\"debug\",\"debug\":false}");
            return;
        }

        // ---------- Get Params ----------
        if (!strcmp(cmd, "get_params")) {
            StaticJsonDocument<256> out;
            out["type"]         = "ctrl_params";
            out["Ajoint"]       = g_pkt.Ajoint;
            out["frequency"]    = g_pkt.frequency;
            out["lambda"]       = g_pkt.lambda;
            out["L"]            = g_pkt.L;
            out["paused"]       = g_pkt.isPaused;
            out["mode"]         = g_pkt.controlMode;
            out["feedbackGain"] = g_pkt.feedbackGain;

            String txt;
            serializeJson(out, txt);
            g_ws->sendTXT(num, txt);
            return;
        }

        // ---------- Set Param ----------
        if (!strcmp(cmd, "set_param")) {
            if (doc.containsKey("Ajoint"))       g_pkt.Ajoint       = doc["Ajoint"];
            if (doc.containsKey("frequency"))    g_pkt.frequency    = doc["frequency"];
            if (doc.containsKey("lambda"))       g_pkt.lambda       = doc["lambda"];
            if (doc.containsKey("L"))            g_pkt.L            = doc["L"];
            if (doc.containsKey("paused"))       g_pkt.isPaused     = doc["paused"];
            if (doc.containsKey("mode"))         g_pkt.controlMode  = doc["mode"];
            if (doc.containsKey("feedbackGain")) g_pkt.feedbackGain = doc["feedbackGain"];

            CtrlUartBridge::sendCtrlParams(g_pkt);
            g_ws->sendTXT(num, "{\"type\":\"ack\",\"ok\":true}");
            return;
        }

        // ---------- Camera Param ----------
        if (!strcmp(cmd, "camera_param")) {
            sensor_t *s = esp_camera_sensor_get();

            if (doc.containsKey("quality"))
                s->set_quality(s, doc["quality"]);

            if (doc.containsKey("framesize"))
                s->set_framesize(s, (framesize_t)doc["framesize"]);

            StaticJsonDocument<128> out;
            out["type"] = "camera_param";
            if (doc.containsKey("quality"))   out["quality"]   = doc["quality"];
            if (doc.containsKey("framesize")) out["framesize"] = doc["framesize"];

            String txt;
            serializeJson(out, txt);
            g_ws->broadcastTXT(txt);
            return;
        }

        // ---------- Wi-Fi Status ----------
        if (!strcmp(cmd, "wifi_status")) {
            CtrlWsServer::sendWifiStatus(num, false);
            return;
        }

        // ---------- Wi-Fi Scan ----------
        if (!strcmp(cmd, "wifi_scan")) {
            CtrlWsServer::sendWifiScanResult(num);
            return;
        }

        // ---------- Wi-Fi List ----------
        if (!strcmp(cmd, "wifi_list")) {
            sendWifiList(num);
            return;
        }

        // ---------- Wi-Fi Connect（不存） ----------
        if (!strcmp(cmd, "wifi_connect")) {
            StaticJsonDocument<128> out;
            out["type"] = "wifi_connect_result";

            bool ok = wifiConnectNow(
                String((const char*)doc["ssid"]),
                String((const char*)doc["pass"])
            );

            out["ok"] = ok;

            String txt;
            serializeJson(out, txt);
            g_ws->sendTXT(num, txt);

            CtrlWsServer::sendWifiStatus(num, true);
            return;
        }

        // ---------- Wi-Fi Save ----------
        if (!strcmp(cmd, "wifi_save")) {
            addOrUpdateWifi(
                String((const char*)doc["ssid"]),
                String((const char*)doc["pass"])
            );

            CtrlWsServer::sendWifiStatus(num, true);
            return;
        }

        // ---------- Wi-Fi Delete ----------
        if (!strcmp(cmd, "wifi_delete")) {
            deleteWifi(String((const char*)doc["ssid"]));
            CtrlWsServer::sendWifiStatus(num, true);
            return;
        }

        // ---------- Unknown ----------
        g_ws->sendTXT(num,
            "{\"type\":\"error\",\"msg\":\"unknown cmd\"}");
    });
}
