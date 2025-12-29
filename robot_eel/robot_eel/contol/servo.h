#pragma once
#include <math.h>
#include "config.h"
#include "utils.h"
#include "logging.h"
void servoTask(void *pv)
{
  const float dt = 0.05f;
  uint32_t lastWake = xTaskGetTickCount();

  static uint32_t loopCount = 0;
  static int readIndex = 0;   // 輪流讀哪一顆 servo

  for (;;)
  {
    if (!isPaused)
    {
      float t = millis() / 1000.0f;

      /* ========= 1. 先寫入所有 servo（控制主流程） ========= */
      for (int j = 0; j < bodyNum; j++)
      {
        float outDeg = 0.0f;

        if (controlMode == 0)
        {
          outDeg = Ajoint * sinf(
            j / fmaxf(lambda * L, 1e-6f)
            + 2.0f * PI * frequency * t
          );
        }
        else if (controlMode == 1)
        {
          float fb_phase = 0.0f, fb_amp = 0.0f;
          updateCPG(t, dt, j, fb_phase, fb_amp);
          outDeg = getCPGOutput(j);
        }

        float targetDeg = servoDefaultAngles[j] + outDeg;
        angleDeg[j] = targetDeg;
        servoState[j].targetDeg = targetDeg;

        int pos = degreeToLX224(targetDeg);
        moveLX224(j + 1, pos, 80);   // 速度你已經調快，OK
      }

      /* ========= 2. 低頻、輪流 read（安全關鍵） ========= */
      loopCount++;

      // 每 4 圈才讀一次 → 80ms * 4 ≈ 320ms（~3 Hz）
      if (loopCount % 4 == 0)
      {
        int j = readIndex;

        int actualPos = readPositionLX224(j + 1);
        if (actualPos >= 0)
        {
          servoState[j].actualPos = actualPos;

          float actualDeg = lx224ToDegree(actualPos);
          servoState[j].actualDeg = actualDeg;

          servoState[j].errorDeg =
            servoState[j].targetDeg - actualDeg;
        }

        // 下一圈換下一顆
        readIndex++;
        if (readIndex >= bodyNum)
          readIndex = 0;
      }

      /* ========= 3. 誤差累積（只用已更新的資料） ========= */
      accumulateServoError();
    }

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(80));
  }
}
