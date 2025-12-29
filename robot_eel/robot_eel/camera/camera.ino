

#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <esp_camera.h>

#include "config.h"
#include "wifi_init.h"
#include "camera_pins.h"
#include "camera_init.h"    // ★ 要加
#include "cam_stream.h"     
#include "cam_control.h"

// ==== 改成兩個 WS ====
WebSocketsServer wsCam(81);   // 影像串流
WebSocketsServer wsCtrl(82);  // 控制 WebSocket

void setup() 
{
    Serial.begin(115200);
    Serial.println("\nBooting ESP32 CAM");

    startControlRx();   // UART RX → WS broadcast
    initWiFi();
    initCamera();

    initStreamWS(wsCam);     // 影像
    initControlWS(wsCtrl);   // 控制

    wsCam.begin();
    wsCtrl.begin();
}

void loop() 
{
    wsCtrl.loop();        // 控制 JSON client
    wsCam.loop();         // 影像 client
    sendCameraFrame(wsCam);   // ★ 只送影像到 port 81
}

