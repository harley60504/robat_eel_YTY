#include "CtrlWsServer.h"
#include <ArduinoJson.h>

namespace {

WebSocketsServer* g_ws = nullptr;
ControlPacket     g_pkt;
bool debugMode = false;

} // anonymous namespace


// ==================================================
// 廣播 ctrl_params
// ==================================================
void CtrlWsServer::broadcastCtrlParams(const ControlPacket &p)
{
    if(!g_ws) return;

    StaticJsonDocument<256> doc;

    doc["type"]       = "ctrl_params";
    doc["Ajoint"]     = p.Ajoint;
    doc["frequency"]  = p.frequency;
    doc["lambda"]     = p.lambda;
    doc["L"]          = p.L;
    doc["paused"]     = p.isPaused;
    doc["mode"]       = p.controlMode;
    doc["useFeedback"]= p.useFeedback;
    doc["feedbackGain"]=p.feedbackGain;

    String out;
    serializeJson(doc,out);
    g_ws->broadcastTXT(out);
}


// ==================================================
// 廣播 servo_status
// ==================================================
void CtrlWsServer::broadcastServoStatus(
    uint8_t count,
    const float *target,
    const float *actual,
    const float *error)
{
    if(!g_ws) return;

    StaticJsonDocument<512> doc;

    doc["type"]  = "servo_status";
    doc["count"] = count;

    auto t = doc.createNestedArray("target");
    auto a = doc.createNestedArray("actual");
    auto e = doc.createNestedArray("error");

    for(int i=0;i<count;i++){
        t.add(target[i]);
        a.add(actual[i]);
        e.add(error[i]);
    }

    String out;
    serializeJson(doc,out);
    g_ws->broadcastTXT(out);
}



// ==================================================
// INIT
// ==================================================
void CtrlWsServer::begin(WebSocketsServer &ws)
{
    g_ws = &ws;

    // ===== 綁定 UART callback =====
    CtrlUartBridge::onCtrlParams =
        [](const ControlPacket &p)
        {
            g_pkt = p;
            CtrlWsServer::broadcastCtrlParams(p);
        };

    CtrlUartBridge::onServoStatus =
        [](const ServoStatus &s)
        {
            CtrlWsServer::broadcastServoStatus(
                s.count,
                s.target,
                s.actual,
                s.error
            );
        };

    // ===== WebSocket handler =====
    ws.onEvent([](uint8_t num,
                  WStype_t type,
                  uint8_t *payload,
                  size_t len)
    {
        if(type != WStype_TEXT) return;

        StaticJsonDocument<256> doc;
        if(deserializeJson(doc,payload)) return;

        const char *cmd = doc["cmd"];

        // ---- Debug toggle ----
        if(strcmp(cmd,"debug_on")==0){
            debugMode = true;
            g_ws->sendTXT(num,"{\"debug\":true}");
            return;
        }

        if(strcmp(cmd,"debug_off")==0){
            debugMode = false;
            g_ws->sendTXT(num,"{\"debug\":false}");
            return;
        }

        // ---- Set Param ----
        if(strcmp(cmd,"set_param")==0)
        {
            if(doc.containsKey("Ajoint"))       g_pkt.Ajoint = doc["Ajoint"];
            if(doc.containsKey("frequency"))    g_pkt.frequency = doc["frequency"];
            if(doc.containsKey("lambda"))       g_pkt.lambda = doc["lambda"];
            if(doc.containsKey("L"))            g_pkt.L = doc["L"];
            if(doc.containsKey("paused"))       g_pkt.isPaused = doc["paused"];
            if(doc.containsKey("mode"))         g_pkt.controlMode = doc["mode"];
            if(doc.containsKey("feedbackGain")) g_pkt.feedbackGain = doc["feedbackGain"];

            CtrlUartBridge::sendCtrlParams(g_pkt);

            g_ws->sendTXT(num,"{\"ok\":true}");
            return;
        }

        // ---- Unknown ----
        g_ws->sendTXT(num,"{\"error\":\"unknown cmd\"}");
    });
}
