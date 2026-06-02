#pragma once
#include <FS.h>
#include <SPIFFS.h>
#include <Arduino.h>
#include <math.h>
#include "config.h"

// =====================================================
// 外部時間戳
// =====================================================
extern unsigned long g_lastLogTime;

// =====================================================
// Servo 誤差累積結構
// =====================================================
struct ServoErrorAccum {
  float sumErr;
  float sumAbsErr;
  uint32_t count;
};

// =====================================================
// 全域累積器（在本 header 內 static）
// =====================================================
static ServoErrorAccum servoErrAcc[bodyNum];

// =====================================================
// 初始化 CSV（Servo 平均誤差）
// =====================================================
inline void initLogFile() {
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ SPIFFS 初始化失敗");
    return;
  }

  if (!SPIFFS.exists("/data.csv")) {
    File f = SPIFFS.open("/data.csv", FILE_WRITE);
    if (!f) return;

    f.print("time_min");
    for (int i = 0; i < bodyNum; i++) {
      f.printf(",servo%d_avg_err", i);
      f.printf(",servo%d_avg_abs_err", i);
    }
    f.println();
    f.close();

    Serial.println("📄 CSV 初始化完成（Servo 每分鐘平均誤差）");
  }
}

// =====================================================
// 高頻呼叫：累積 Servo 誤差
// 👉 放在 servoTask 裡
// =====================================================
inline void accumulateServoError() {
  for (int i = 0; i < bodyNum; i++) {
    servoErrAcc[i].sumErr     += servoState[i].errorDeg;
    servoErrAcc[i].sumAbsErr += fabs(servoState[i].errorDeg);
    servoErrAcc[i].count++;
  }
}

// =====================================================
// 低頻呼叫：每分鐘寫一次平均值
// 👉 放在 loop()
// =====================================================
inline void logServoErrorAvgPerMinute() {
  unsigned long now = millis();
  if (now - g_lastLogTime < 60000) return;
  g_lastLogTime = now;

  unsigned long t_min = now / 60000;
  File f = SPIFFS.open("/data.csv", FILE_APPEND);
  if (!f) return;

  f.printf("%lu", t_min);

  for (int i = 0; i < bodyNum; i++) {
    if (servoErrAcc[i].count > 0) {
      float avgErr = servoErrAcc[i].sumErr / servoErrAcc[i].count;
      float avgAbs = servoErrAcc[i].sumAbsErr / servoErrAcc[i].count;
      f.printf(",%.4f,%.4f", avgErr, avgAbs);
    } else {
      f.print(",0,0");
    }

    // 重置
    servoErrAcc[i] = {0,0,0};
  }

  f.println();
  f.close();

  Serial.printf("📄 CSV 寫入（Servo 平均誤差）：第 %lu 分鐘\n", t_min);
}
