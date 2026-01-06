#ifndef SERVO_STATUS_UART_H
#define SERVO_STATUS_UART_H

#include <Arduino.h>
#include "ControltoCamera.h"
#include "config.h"
#include "utils.h"

#define SERVO_STATUS_HEADER 0xBB
#define SERVO_MAX 8

#pragma pack(push,1)
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

extern ServoState servoState[];
extern float angleDeg[];

/* ========= Snapshot buffer ========= */
ServoStatusPacket g_status;

/* ========= Mutex ========= */
SemaphoreHandle_t statusMutex = xSemaphoreCreateMutex();


static inline void sendServoStatusUART(HardwareSerial& serial)
{
  if (!xSemaphoreTake(statusMutex, 0))
    return;   // 取不到就等下次

  serial.write((uint8_t*)&g_status, sizeof(ServoStatusPacket));

  xSemaphoreGive(statusMutex);
}


/* ========= UART TX Task ========= */
static void servoStatusTxTask(void *pv)
{
  TickType_t lastWake = xTaskGetTickCount();

  while(true)
  {
    sendServoStatusUART(Serial2);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(80));
  }
}

#endif
