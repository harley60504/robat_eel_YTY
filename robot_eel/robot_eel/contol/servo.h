#pragma once
#include <math.h>
#include "config.h"
#include "utils.h"
#include "logging.h"
#include "ServoStatusUART.h"   // ★ 要包含，取得 g_status 與 mutex

void servoTask(void *pv)
{
  const uint16_t MOVE_TIME_MS = 100;
  const float    dt = MOVE_TIME_MS / 1000.0f;
  static uint32_t seq = 0;
  TickType_t lastWake = xTaskGetTickCount();

  for (;;)
  {
    if (!isPaused)
    {
      float t = millis() / 1000.0f;

      /* ========= 1. 計算 target 並輸出 MOVE ========= */
      for (int j = 0; j < bodyNum; j++)
      {
        float outDeg = 0.0f;

        switch (controlMode)
        {
          case 0:
            outDeg =
              Ajoint *
              sinf(j / fmaxf(lambda * L, 1e-6f)
                   + 2 * PI * frequency * t);
            break;

          case 1:
          {
            float fb_phase = 0, fb_amp = 0;
            updateCPG(t, dt, j, fb_phase, fb_amp);
            outDeg = getCPGOutput(j);
          }
            break;

          case 2:
            outDeg = 0.0f;
            break;
        }

        float targetDeg = servoDefaultAngles[j] + outDeg;

        servoState[j].targetDeg = targetDeg;
        angleDeg[j] = targetDeg;

        int pos = degreeToLX224(targetDeg);
        moveLX224(j + 1, pos, MOVE_TIME_MS);
      }

      /* ========= 2. 等待 servo 完成運動 ========= */
      vTaskDelay(pdMS_TO_TICKS(MOVE_TIME_MS));

      /* ========= 3. 同步讀回授 ========= */
      for (int j = 0; j < bodyNum; j++)
      {
        int actualPos = readPositionLX224(j + 1);

        if (actualPos >= 0)
        {
          servoState[j].actualPos = actualPos;

          float actualDeg = lx224ToDegree(actualPos);
          servoState[j].actualDeg = actualDeg;

          servoState[j].errorDeg =
            servoState[j].targetDeg - actualDeg;
        }
      }

      /* ========= 4. 建立封包 SNAPSHOT ========= */
      if (xSemaphoreTake(statusMutex, portMAX_DELAY))
      {
        g_status.header = SERVO_STATUS_HEADER;
        g_status.count  = bodyNum;
        g_status.seq    = seq++;
        for(int i=0;i<bodyNum;i++)
        {
          g_status.targetDeg[i] = servoState[i].targetDeg;
          g_status.actualDeg[i] = servoState[i].actualDeg;
          g_status.errorDeg[i]  = servoState[i].errorDeg;
        }

        g_status.checksum = calcControlChecksum(
          (uint8_t*)&g_status,
          sizeof(ServoStatusPacket)-1
        );

        xSemaphoreGive(statusMutex);
      }
    }
    else
    {
      vTaskDelay(pdMS_TO_TICKS(10));
    }
  }
}
