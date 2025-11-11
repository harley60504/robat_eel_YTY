#pragma once
#include <FS.h>
#include <SPIFFS.h>
#include "config.h"

inline void initLogFile() {
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ SPIFFS 初始化失敗");
    return;
  }
  if (!SPIFFS.exists("/data.csv")) {
    File f = SPIFFS.open("/data.csv", FILE_WRITE);
    if (f) {
      f.println("time_min,ads1_A0,ads1_A1,ads1_A2,ads1_A3,ads2_A0,ads2_A1,ads2_A2,ads2_A3,ads1_diff01,ads1_diff23,ads1_diff03,adxl_x_g,adxl_y_g,adxl_z_g");
      f.close();
    }
  }
}

inline void logADSDataEveryMinute() {
  unsigned long now = millis();
  if (now - g_lastLogTime < 60000) return;
  g_lastLogTime = now;
  unsigned long t_min = now / 60000;
  File f = SPIFFS.open("/data.csv", FILE_APPEND);
  if (!f) return;
  f.printf("%lu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
           t_min,
           adsVoltage1[0], adsVoltage1[1], adsVoltage1[2], adsVoltage1[3],
           adsVoltage2[0], adsVoltage2[1], adsVoltage2[2], adsVoltage2[3],
           ads1Diff[0], ads1Diff[1], ads1Diff[2],
           adxlX, adxlY, adxlZ);
  f.close();
  Serial.printf("📄 CSV 已寫入: 第 %lu 分鐘\n", t_min);
}
