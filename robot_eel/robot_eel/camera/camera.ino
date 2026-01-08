#include <Arduino.h>
#include <WebSocketsServer.h>

// #include "wifi_init.h"
#include "camera_init.h"
#include "cam_stream.h"

#include "CtrlUartBridge.h"
#include "CtrlWsServer.h"
#include "wifi_manager.h"
#include <Preferences.h>
// ========= WebSocket Ports =========
WebSocketsServer wsCam(81);    // 影像
WebSocketsServer wsCtrl(82);   // 控制


void setup()
{
    // Preferences prefs;
    // prefs.begin("wifi", false);
    // prefs.remove("list");   // ← 清掉！
    // prefs.end();

    // Serial.println("WiFi list cleared!");

    // while(true);   // 防止繼續執
    Serial.begin(115200);
    Serial.println("\nESP32-CAM Booting...");

    // ============ Wi-Fi =================
    // initWiFi();
    startWifiApSta();
    // ============ Camera ===============
    initCamera();


    // ============ UART Bridge ============
    //   UART2 → 控制板
    //
    //   RX = GPIO 8
    //   TX = GPIO 9
    //
    CtrlUartBridge::begin(
        Serial2,
        115200,
        8,
        9
    );


    // ============ WebSocket Server ============
    initStreamWS(wsCam);      // 影像 WS
    CtrlWsServer::begin(wsCtrl);  // 控制 WS

    wsCam.begin();
    wsCtrl.begin();

    Serial.println("System Ready.");
}



void loop()
{
    wsCam.loop();     // 影像 WS Client
    wsCtrl.loop();    // 控制 WS Client

    sendCameraFrame(wsCam);   // 傳 MJPEG
}
