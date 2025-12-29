#ifndef SERVO_STATUS_UART_H
#define SERVO_STATUS_UART_H

#include <Arduino.h>
#include "ControltoCamera.h"
#include "config.h"
#include "utils.h"     // ← 這裡已經有 ServoState + servoState[]

#define SERVO_STATUS_HEADER 0xBB
#define SERVO_MAX 8


#pragma pack(push,1)
typedef struct {
  uint8_t header;
  uint8_t count;
  float   targetDeg[SERVO_MAX];
  float   actualDeg[SERVO_MAX];
  float   errorDeg[SERVO_MAX];
  uint8_t checksum;
} ServoStatusPacket;
#pragma pack(pop)


// 直接用 utils.h 裡的
extern ServoState servoState[];
extern float angleDeg[];


static inline void sendServoStatusUART(HardwareSerial& serial)
{
  ServoStatusPacket pkt;

  pkt.header = SERVO_STATUS_HEADER;
  pkt.count  = bodyNum;

  for(int i=0;i<bodyNum;i++)
  {
    pkt.targetDeg[i] = servoState[i].targetDeg;
    pkt.actualDeg[i] = servoState[i].actualDeg;
    pkt.errorDeg[i]  = servoState[i].errorDeg;
  }

  pkt.checksum = calcControlChecksum(
    (uint8_t*)&pkt,
    sizeof(ServoStatusPacket)-1
  );

  serial.write((uint8_t*)&pkt, sizeof(ServoStatusPacket));
}


// Task
static void servoStatusTxTask(void *pv)
{
  TickType_t lastWake = xTaskGetTickCount();

  while(true)
  {
    sendServoStatusUART(Serial2);
    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(200));
  }
}

#endif
