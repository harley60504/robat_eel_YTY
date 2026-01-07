#pragma once
#include <WebSocketsServer.h>
#include "ControltoCamera.h"
#include "CtrlUartBridge.h"

namespace CtrlWsServer
{
    void begin(WebSocketsServer &ws);

    void broadcastCtrlParams(const ControlPacket &pkt);

    void broadcastServoStatus(uint8_t count,
                              uint32_t seq,
                              const float *target,
                              const float *actual,
                              const float *error);

    // WiFi 相關：透過 ws 回傳 JSON
    void sendWifiStatus(uint8_t clientNum = 0, bool broadcast = false);
    void sendWifiScanResult(uint8_t clientNum);
}
