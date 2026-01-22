#ifndef SERVO_STATUS_UART_H
#define SERVO_STATUS_UART_H

#include <Arduino.h>
#include "config.h"
#include "utils.h"

// ✅ Servo Status Packet
#define SERVO_STATUS_HEADER 0xBB

// ✅ 用 bodyNum 當上限，永遠跟你的機器人段數一致
#define SERVO_MAX bodyNum

#pragma pack(push, 1)
typedef struct {
  uint8_t header;
  uint8_t count;
  uint32_t seq;
  float targetDeg[SERVO_MAX];
  float actualDeg[SERVO_MAX];
  float errorDeg[SERVO_MAX];
  uint8_t checksum;
} ServoStatusPacket;
#pragma pack(pop)

// 你在其他地方定義的 servo state
extern ServoState servoState[];
extern float angleDeg[];

// ✅ Snapshot buffer（只能在 .cpp 定義一次）
extern ServoStatusPacket g_status;

// ✅ Mutex（只能在 .cpp 定義一次）
extern SemaphoreHandle_t statusMutex;

static inline void sendServoStatusUART(HardwareSerial& serial)
{
  if (!statusMutex) return;

  if (!xSemaphoreTake(statusMutex, 0))
    return;

  serial.write((uint8_t*)&g_status, sizeof(ServoStatusPacket));

  xSemaphoreGive(statusMutex);
}

// ✅ UART TX Task（建議固定用 Serial2，看你架構）
static inline void servoStatusTxTask(void *pv)
{
  TickType_t lastWake = xTaskGetTickCount();

  while(true)
  {
    sendServoStatusUART(Serial2);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(80));
  }
}

#endif
