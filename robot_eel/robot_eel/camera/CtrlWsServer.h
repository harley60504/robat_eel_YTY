#pragma once

#include <WebSocketsServer.h>
#include "CtrlUartBridge.h"   
#include "ControltoCamera.h"
// ===== CtrlWsServer =====
namespace CtrlWsServer {

    // 初始化（setup 時呼叫一次）
    void begin(WebSocketsServer &ws);

    // 每個 loop 呼叫（低頻 Wi-Fi 廣播）
    void tick();

    // UART → WS
    void broadcastCtrlParams(const ControlPacket &p);
    void broadcastServoStatus(
        uint8_t count,
        uint32_t seq,
        const float *target,
        const float *actual,
        const float *error
    );
}
