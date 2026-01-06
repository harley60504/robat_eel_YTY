#pragma once
#include <Arduino.h>
#include <functional>
#include "ControltoCamera.h"

// ==== Servo 回報封包定義，需要跟控制板那邊的完全一致 ====

// UART 封包起始
#define SERVO_STATUS_HEADER 0xBB
#define SERVO_MAX 8

#pragma pack(push,1)
struct ServoStatus {
  uint8_t  header;          // 固定 = 0xBB
  uint8_t  count;           // 有幾顆 servo（例：6）
  uint32_t seq;             // 序號：0,1,2,3...
  float    target[SERVO_MAX];
  float    actual[SERVO_MAX];
  float    error[SERVO_MAX];
  uint8_t  checksum;        // calcControlChecksum(前面所有 bytes)
};
#pragma pack(pop)


namespace CtrlUartBridge {

  void begin(HardwareSerial& ser,
             uint32_t baud,
             int rxPin,
             int txPin);

  void sendCtrlParams(const ControlPacket &pkt);

  extern std::function<void(const ControlPacket&)> onCtrlParams;
  extern std::function<void(const ServoStatus&)>   onServoStatus;
}
