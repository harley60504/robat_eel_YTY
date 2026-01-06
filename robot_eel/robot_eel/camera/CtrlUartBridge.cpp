#include "CtrlUartBridge.h"
#include <cstring>      // memcpy

// ==== UART & 解包狀態 ====
static ControlRxState   g_rx;
static HardwareSerial*  g_ser = nullptr;

// callback
std::function<void(const ControlPacket&)> CtrlUartBridge::onCtrlParams = nullptr;
std::function<void(const ServoStatus&)>   CtrlUartBridge::onServoStatus = nullptr;

// servo_status 專用 buffer
static uint8_t buf[128];
static size_t  idx        = 0;
static bool    receiving  = false;

// 封包固定長度（103 bytes）
static const size_t SERVO_PKT_SIZE = sizeof(ServoStatus); // 應該 = 103


// ==================================================
// UART RX Task
// ==================================================
static void uartRxTask(void *pv)
{
  while(true)
  {
    while(g_ser && g_ser->available())
    {
      uint8_t b = g_ser->read();

      // ------------- ctrl_params 解包 -------------
      if (feedControlRx(g_rx, b))
      {
        if (CtrlUartBridge::onCtrlParams)
          CtrlUartBridge::onCtrlParams(g_rx.pkt);
        continue;
      }

      // ------------- servo_status 解包 -------------
      if (!receiving)
      {
        // 等待封包開頭 header = 0xBB
        if (b == SERVO_STATUS_HEADER)
        {
          receiving = true;
          idx       = 0;
          buf[idx++] = b;
        }
        continue;
      }

      // 已進入接收狀態 → 把 byte 填進 buffer
      buf[idx++] = b;

      // 收滿一整包就解析
      if (idx >= SERVO_PKT_SIZE)
      {
        receiving = false;

        // 基本保護：確認第一個 byte 仍然是 header
        if (buf[0] != SERVO_STATUS_HEADER)
        {
          // 丟掉這包
          continue;
        }

        ServoStatus ss;
        memcpy(&ss, buf, SERVO_PKT_SIZE);

        // 計算 checksum
        uint8_t cs =
          calcControlChecksum(
            reinterpret_cast<uint8_t*>(&ss),
            SERVO_PKT_SIZE - 1       // 不含 checksum 自己
          );

        if (cs == ss.checksum)
        {
          if (CtrlUartBridge::onServoStatus)
            CtrlUartBridge::onServoStatus(ss);
        }
        else {
          // 你要的話可以打 debug
          // Serial.printf("[UART] servo_status checksum fail: got=0x%02X, expect=0x%02X\n",
          //              ss.checksum, cs);
        }
      }
    }

    vTaskDelay(1);
  }
}


// ==================================================
// TX：把控制參數從 camera 送回控制板
// ==================================================
void CtrlUartBridge::sendCtrlParams(const ControlPacket &pkt)
{
  if (!g_ser) return;

  sendControlParamsUART(
    *g_ser,
    pkt.Ajoint,
    pkt.frequency,
    pkt.lambda,
    pkt.L,
    pkt.isPaused,
    pkt.controlMode,
    pkt.useFeedback,
    pkt.feedbackGain
  );
}


// ==================================================
// INIT
// ==================================================
void CtrlUartBridge::begin(HardwareSerial& ser,
                           uint32_t baud,
                           int rxPin,
                           int txPin)
{
  g_ser = &ser;

  ser.begin(
    baud,
    SERIAL_8N1,
    rxPin,
    txPin
  );

  xTaskCreatePinnedToCore(
    uartRxTask,
    "uartRxTask",
    4096,
    nullptr,
    1,
    nullptr,
    1
  );
}
