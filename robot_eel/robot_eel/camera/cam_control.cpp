#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebSocketsServer.h>
#include <esp_camera.h>

#include "cam_control.h"
#include "ControltoCamera.h"


#define CTRL_RX_PIN 8
#define CTRL_TX_PIN 9

static ControlRxState g_rx;
static ControlPacket  g_lastPkt;

static bool g_debugMode = false;

static WebSocketsServer* g_ctrlWs = nullptr;



// ==================================================
// UART RX Task
// ==================================================
void controlRxTask(void *pv)
{
    Serial2.begin(115200, SERIAL_8N1, CTRL_RX_PIN, CTRL_TX_PIN);

    Serial.println("[CTRL RX] UART ready");

    while(true)
    {
        while(Serial2.available())
        {
            uint8_t b = Serial2.read();

            if(feedControlRx(g_rx, b))
            {
                g_lastPkt = g_rx.pkt;

                if(g_debugMode)
                {
                    Serial.println("---- CONTROL PACKET RX ----");
                    Serial.printf("Ajoint=%.2f\n", g_lastPkt.Ajoint);
                    Serial.printf("freq=%.2f\n",   g_lastPkt.frequency);
                    Serial.printf("lambda=%.2f\n", g_lastPkt.lambda);
                    Serial.printf("L=%.2f\n",      g_lastPkt.L);
                }

                if(g_ctrlWs)
                {
                    StaticJsonDocument<256> doc;

                    doc["type"] = "ctrl_params";
                    doc["Ajoint"] = g_lastPkt.Ajoint;
                    doc["frequency"] = g_lastPkt.frequency;
                    doc["lambda"] = g_lastPkt.lambda;
                    doc["L"] = g_lastPkt.L;
                    doc["paused"] = g_lastPkt.isPaused;
                    doc["mode"] = g_lastPkt.controlMode;
                    doc["useFeedback"] = g_lastPkt.useFeedback;
                    doc["feedbackGain"] = g_lastPkt.feedbackGain;

                    String out;
                    serializeJson(doc,out);
                    g_ctrlWs->broadcastTXT(out);
                }
            }
        }

        vTaskDelay(1);
    }
}



// ==================================================
// Public API
// ==================================================
void startControlRx()
{
    xTaskCreatePinnedToCore(
        controlRxTask,
        "controlRxTask",
        4096,
        nullptr,
        1,
        nullptr,
        1
    );

    Serial.println("[CTRL RX] Task started");
}



// ==================================================
// Helper: UART TX
// ==================================================
static void sendToController()
{
    sendControlParamsUART(
        Serial2,
        g_lastPkt.Ajoint,
        g_lastPkt.frequency,
        g_lastPkt.lambda,
        g_lastPkt.L,
        g_lastPkt.isPaused,
        g_lastPkt.controlMode,
        g_lastPkt.useFeedback,
        g_lastPkt.feedbackGain
    );
}



// ==================================================
// WebSocket API
// ==================================================
void initControlWS(WebSocketsServer &ws)
{
    g_ctrlWs = &ws;

    Serial.println("[WS] Control WebSocket ready");

    ws.onEvent([&ws](uint8_t num, WStype_t type, uint8_t *payload, size_t len){

        if(type != WStype_TEXT) return;

        StaticJsonDocument<256> doc;

        if(deserializeJson(doc,payload))
        {
            ws.sendTXT(num,"{\"error\":\"bad json\"}");
            return;
        }

        const char* cmd = doc["cmd"];


        // ================= Debug =================
        if(strcmp(cmd,"debug_on")==0){
            g_debugMode = true;
            ws.sendTXT(num,"{\"debug\":true}");
            return;
        }

        if(strcmp(cmd,"debug_off")==0){
            g_debugMode = false;
            ws.sendTXT(num,"{\"debug\":false}");
            return;
        }


        // ================= camera param =================
        if(strcmp(cmd,"camera_param")==0)
        {
            sensor_t *s = esp_camera_sensor_get();

            int framesize = -1;
            int quality   = -1;

            if(doc.containsKey("quality")){
                quality = doc["quality"];
                s->set_quality(s, quality);
            }

            if(doc.containsKey("framesize")){
                framesize = doc["framesize"];
                s->set_framesize(s,(framesize_t)framesize);
                delay(100);   // 解析度建議稍微等一下
            }

            // ====== 廣播 camera 狀態 ======
            if(g_ctrlWs){
                StaticJsonDocument<128> out;

                out["type"] = "camera_param";

                if(framesize >= 0)
                    out["framesize"] = framesize;

                if(quality >= 0)
                    out["quality"] = quality;

                String txt;
                serializeJson(out,txt);
                g_ctrlWs->broadcastTXT(txt);
            }

            ws.sendTXT(num,"{\"ok\":true}");
            return;
        }



        // ================= get params =================
        if(strcmp(cmd,"get_params")==0)
        {
            StaticJsonDocument<256> out;

            out["type"]="ctrl_params";
            out["Ajoint"]=g_lastPkt.Ajoint;
            out["frequency"]=g_lastPkt.frequency;
            out["lambda"]=g_lastPkt.lambda;
            out["L"]=g_lastPkt.L;
            out["paused"]=g_lastPkt.isPaused;
            out["mode"]=g_lastPkt.controlMode;
            out["useFeedback"]=g_lastPkt.useFeedback;
            out["feedbackGain"]=g_lastPkt.feedbackGain;

            String s;
            serializeJson(out,s);
            ws.sendTXT(num,s);
            return;
        }


        // ================= set params =================
        if(strcmp(cmd,"set_param")==0)
        {
            if(doc.containsKey("Ajoint"))
                g_lastPkt.Ajoint = doc["Ajoint"];

            if(doc.containsKey("frequency"))
                g_lastPkt.frequency = doc["frequency"];

            if(doc.containsKey("lambda"))
                g_lastPkt.lambda = doc["lambda"];

            if(doc.containsKey("L"))
                g_lastPkt.L = doc["L"];

            if(doc.containsKey("paused"))
                g_lastPkt.isPaused = doc["paused"];

            if(doc.containsKey("mode"))
                g_lastPkt.controlMode = doc["mode"];

            if(doc.containsKey("feedbackGain"))
                g_lastPkt.feedbackGain = doc["feedbackGain"];

            sendToController();

            ws.sendTXT(num,"{\"ok\":true}");
            return;
        }


        // ================= set_mode =================
        if(strcmp(cmd,"set_mode")==0)
        {
            g_lastPkt.controlMode = doc["value"];

            sendToController();

            ws.sendTXT(num,"{\"ok\":true}");
            return;
        }


        // ================= servo test =================
        if(strcmp(cmd,"servo_angle")==0)
        {
            // 依需求擴充
            ws.sendTXT(num,"{\"ok\":true}");
            return;
        }


        ws.sendTXT(num,"{\"error\":\"unknown cmd\"}");
    });
}
