#pragma once
#include <SPI.h>
#include <math.h>
#include "config.h"

static inline void accelToEuler(float ax, float ay, float az,
                                float &pitch_deg, float &roll_deg) {
  float roll  = atan2f(ay, az);
  float pitch = atan2f(-ax, sqrtf(ay*ay + az*az));
  pitch_deg = pitch * 180.0f / M_PI;
  roll_deg  = roll  * 180.0f / M_PI;
}

inline void initADXL() {
  SPI.begin(ADXL_SCLK, ADXL_MISO, ADXL_MOSI, ADXL_CS);
  delay(10);
  adxl355.beginSPI(ADXL_CS);
  adxl355.setRange(PL::ADXL355_Range::range2g);
  adxl355.enableMeasurement();
  Serial.println("✅ ADXL355 SPI 初始化完成");
}

inline void adxlTask(void *pv) {
  const float alpha = 0.2f;
  for (;;) {
    auto a = adxl355.getAccelerations();
    adxlX = a.x; adxlY = a.y; adxlZ = a.z;

    float p_now, r_now;
    accelToEuler(adxlX, adxlY, adxlZ, p_now, r_now);

    pitchDeg = alpha * p_now + (1.0f - alpha) * pitchDeg;
    rollDeg  = alpha * r_now + (1.0f - alpha) * rollDeg;

    vTaskDelay(20 / portTICK_PERIOD_MS);
  }
}
