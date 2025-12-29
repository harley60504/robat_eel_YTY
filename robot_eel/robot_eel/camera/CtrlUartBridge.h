#pragma once
#include <Arduino.h>
#include <functional>
#include "ControltoCamera.h"

struct ServoStatus {
  uint8_t count;
  float target[8];
  float actual[8];
  float error[8];
  uint8_t checksum;   // ★ 必須加這個
};

namespace CtrlUartBridge {

  void begin(HardwareSerial& ser,
             uint32_t baud,
             int rxPin,
             int txPin);

  void sendCtrlParams(const ControlPacket &pkt);

  extern std::function<void(const ControlPacket&)> onCtrlParams;
  extern std::function<void(const ServoStatus&)>   onServoStatus;
}
