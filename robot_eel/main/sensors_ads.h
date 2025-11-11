#pragma once
#include "config.h"

inline bool safeReadADS(Adafruit_ADS1115 &ads, int channel, float &voltage) {
  int16_t raw = ads.readADC_SingleEnded(channel);
  float v = ads.computeVolts(raw);
  if (v < adsMinValidVoltage) v = 0.0f;
  voltage = v;
  return true;
}

inline void readADS() {
  for (int i = 0; i < 4; i++) {
    safeReadADS(ads1, i, adsVoltage1[i]);
    safeReadADS(ads2, i, adsVoltage2[i]);
  }
  ads1Diff[0] = ads1.computeVolts(ads1.readADC_Differential_0_1());
  ads1Diff[1] = ads1.computeVolts(ads1.readADC_Differential_2_3());
  ads1Diff[2] = ads1.computeVolts(ads1.readADC_Differential_0_3());
  for (int k = 0; k < 3; ++k) if (ads1Diff[k] < adsMinValidVoltage) ads1Diff[k] = 0.0f;
}

inline void i2cTask(void *pv) {
  unsigned long lastADS = 0;
  for (;;) {
    if (millis() - lastADS > 200) {
      lastADS = millis();
      readADS();
      logADSDataEveryMinute();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}
