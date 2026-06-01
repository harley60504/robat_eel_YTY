#include <Arduino.h>
#include <WebSocketsServer.h>
#include <WebServer.h>
#include "camera_init.h"
#include "cam_stream.h"
#include "wifi_http.h"
#include "CtrlUartBridge.h"
#include "CtrlWsServer.h"
#include "wifi_manager.h"
#include <Preferences.h>
// ========= WebSocket Ports =========
WebSocketsServer wsCam(81);    // 影像
WebSocketsServer wsCtrl(82);   // 控制
WebServer server(80);

void setup()
{
    // Preferences prefs;
    // prefs.begin("wifi", false);
    // prefs.remove("list");   // ← 清掉！
    // prefs.end();

    // Serial.println("WiFi list cleared!");

    // while(true);   // 防止繼續
    Serial.begin(115200);

    // ============ Wi-Fi =================
    startWifiApSta();
    setupWifiHttpApi();     // ⭐Wi-Fi HTTP
    // ============ Camera ===============
    initCamera();


    // ============ UART Bridge ============
    //   UART2 → 控制板
    CtrlUartBridge::begin(
        Serial2,
        115200,
        UART_RX ,
        UART_TX 
    );


    // ============ WebSocket Server ============
    initStreamWS(wsCam);      // 影像 WS
    CtrlWsServer::begin(wsCtrl);  // 控制 WS

    wsCam.begin();
    wsCtrl.begin();
    server.begin();
    Serial.println("System Ready.");
}



void loop()
{
    wsCam.loop();     // 影像 WS Client
    wsCtrl.loop();    // 控制 WS Client
    server.handleClient(); 
    CtrlWsServer::tick();   // ⭐ Wi-Fi 低頻率主動廣播
    sendCameraFrame(wsCam);   // 傳 MJPEG
}
