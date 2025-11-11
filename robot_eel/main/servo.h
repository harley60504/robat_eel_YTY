#pragma once
#include <math.h>
#include "config.h"
#include "utils.h"
#include "cpg.h"

inline void servoTask(void *pv) {
  const float dt = 0.05f;
  for (;;) {
    if (!isPaused) {
      float t = millis() / 1000.0f;
      for (int j = 0; j < bodyNum; j++) {
        float outDeg = 0.0f;
        if (controlMode == 0) {
          outDeg = Ajoint * sinf(j / (fmaxf(lambda * L, 1e-6f)) + 2.0f * PI * frequency * t);
          angleDeg[j] = servoDefaultAngles[j] + outDeg;
        } else if (controlMode == 1) {
          float fb_phase = 0.0f, fb_amp = 0.0f;
          if (useFeedback && feedbackGain > 0.0f) {
            float desired_angle = servoDefaultAngles[j];
            float actual_angle  = getSensorAngle(j);
            fb_phase = feedbackGain * (desired_angle - actual_angle) / fmaxf(Ajoint, 1e-3f);
            fb_amp   = fb_phase;
          }
          updateCPG(t, dt, j, fb_phase, fb_amp);
          outDeg = getCPGOutput(j);
          angleDeg[j] = servoDefaultAngles[j] + outDeg;
        } else {
          angleDeg[j] = servoDefaultAngles[j];
        }
        int target_pos = degreeToLX224(angleDeg[j]);
        moveLX224(j + 1, target_pos, 50);
      }
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}
