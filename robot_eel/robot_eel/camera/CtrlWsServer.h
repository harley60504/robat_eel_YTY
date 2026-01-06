#pragma once
#include <WebSocketsServer.h>
#include "ControltoCamera.h"
#include "CtrlUartBridge.h"

namespace CtrlWsServer
{
    void begin(WebSocketsServer &ws);

    void broadcastCtrlParams(const ControlPacket &pkt);

    void broadcastServoStatus(uint8_t count,
                              const float *target,
                              const float *actual,
                              const float *error);
}
