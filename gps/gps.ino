#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include "FS.h"
#include "SD.h"
#include "SPI.h"

// ======================================================
// XIAO ESP32S3 Sense GPS + SD CSV Logger 診斷完整版
// GPS: D5 / D6
// SD : XIAO ESP32S3 Sense 內建 SD 卡槽
// ======================================================

// =======================
// GPS 設定
// =======================
HardwareSerial GPSserial(1);
TinyGPSPlus gps;

#define GPS_RX_PIN D5   // GPS TX -> XIAO D5
#define GPS_TX_PIN D6   // GPS RX -> XIAO D6，可不接

// GPS 鮑率
#define GPS_BAUD 9600

// 是否顯示原始 NMEA
// 如果要看 $GNGGA / $GNRMC，把 false 改成 true
#define SHOW_RAW_NMEA false

// =======================
// SD 卡設定
// XIAO ESP32S3 Sense 內建 SD 卡 CS = GPIO21
// =======================
#define SD_CS_PIN 21

const char *csvPath = "/gps_log.csv";

// =======================
// 狀態變數
// =======================
bool sdOK = false;

unsigned long lastLogTime = 0;
unsigned long lastCheckTime = 0;

const unsigned long logInterval = 1000;      // 每 1 秒嘗試寫入一次
const unsigned long checkInterval = 3000;    // 每 3 秒診斷一次

unsigned long gpsCharCount = 0;
unsigned long lastGpsCharCount = 0;

// ======================================================
// SD 卡診斷
// ======================================================
bool initSDCard() {
  Serial.println();
  Serial.println("========== SD 卡檢測 ==========");

  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("❌ SD 卡初始化失敗");
    Serial.println("可能原因：");
    Serial.println("1. SD 卡沒有插好");
    Serial.println("2. SD 卡不是 FAT32 格式");
    Serial.println("3. SD 卡接觸不良");
    Serial.println("4. 使用的不是 XIAO ESP32S3 Sense 內建 SD 卡槽");
    Serial.println("5. D8/D9/D10 被其他裝置佔用，導致 SD SPI 衝突");
    return false;
  }

  uint8_t cardType = SD.cardType();

  if (cardType == CARD_NONE) {
    Serial.println("❌ 沒有偵測到 SD 卡");
    Serial.println("請確認 SD 卡是否插入，或重新格式化為 FAT32");
    return false;
  }

  Serial.print("✅ 偵測到 SD 卡，類型：");

  if (cardType == CARD_MMC) {
    Serial.println("MMC");
  } else if (cardType == CARD_SD) {
    Serial.println("SDSC");
  } else if (cardType == CARD_SDHC) {
    Serial.println("SDHC");
  } else {
    Serial.println("UNKNOWN");
  }

  uint64_t cardSize = SD.cardSize() / (1024 * 1024);
  Serial.print("SD 卡容量：");
  Serial.print(cardSize);
  Serial.println(" MB");

  File testFile = SD.open("/sd_test.txt", FILE_WRITE);

  if (!testFile) {
    Serial.println("❌ SD 卡可以初始化，但無法建立測試檔案");
    Serial.println("可能原因：SD 卡檔案系統異常、接觸不良或寫入失敗");
    return false;
  }

  testFile.println("SD write test OK");
  testFile.close();

  Serial.println("✅ SD 卡寫入測試成功");

  return true;
}

// ======================================================
// 建立 CSV 標題
// ======================================================
void createCSVHeaderIfNeeded() {
  if (!sdOK) {
    return;
  }

  if (!SD.exists(csvPath)) {
    File file = SD.open(csvPath, FILE_WRITE);

    if (!file) {
      Serial.println("❌ CSV 檔案建立失敗");
      sdOK = false;
      return;
    }

    file.println("millis,utc_date,utc_time,taiwan_date,taiwan_time,latitude,longitude,altitude_m,satellites,hdop");
    file.close();

    Serial.println("✅ 已建立 gps_log.csv 並寫入標題");
  } else {
    Serial.println("✅ gps_log.csv 已存在，會繼續追加資料");
  }
}

// ======================================================
// UTC 日期
// ======================================================
String getUTCDate() {
  if (gps.date.isValid()) {
    char buffer[16];
    sprintf(buffer, "%04d-%02d-%02d",
            gps.date.year(),
            gps.date.month(),
            gps.date.day());
    return String(buffer);
  }

  return "invalid";
}

// ======================================================
// UTC 時間
// ======================================================
String getUTCTime() {
  if (gps.time.isValid()) {
    char buffer[16];
    sprintf(buffer, "%02d:%02d:%02d",
            gps.time.hour(),
            gps.time.minute(),
            gps.time.second());
    return String(buffer);
  }

  return "invalid";
}

// ======================================================
// 台灣時間日期
// 簡化版：只把 UTC hour + 8
// 如果跨日，只處理顯示用途，不做完整月曆修正
// GPS CSV 主要仍以 UTC 為準
// ======================================================
String getTaiwanTime() {
  if (gps.time.isValid()) {
    int hourTW = gps.time.hour() + 8;

    if (hourTW >= 24) {
      hourTW -= 24;
    }

    char buffer[16];
    sprintf(buffer, "%02d:%02d:%02d",
            hourTW,
            gps.time.minute(),
            gps.time.second());
    return String(buffer);
  }

  return "invalid";
}

String getTaiwanDateNote() {
  if (!gps.date.isValid()) {
    return "invalid";
  }

  int hourTW = gps.time.hour() + 8;

  if (gps.time.isValid() && hourTW >= 24) {
    return getUTCDate() + String("+1day_note");
  }

  return getUTCDate();
}

// ======================================================
// GPS 狀態診斷
// ======================================================
void checkGPSStatus() {
  Serial.println();
  Serial.println("========== GPS 狀態檢測 ==========");

  unsigned long newChars = gpsCharCount - lastGpsCharCount;
  lastGpsCharCount = gpsCharCount;

  Serial.print("GPS 接收字元總數：");
  Serial.println(gpsCharCount);

  Serial.print("最近 3 秒收到 GPS 字元數：");
  Serial.println(newChars);

  Serial.print("TinyGPS++ 已處理字元數：");
  Serial.println(gps.charsProcessed());

  Serial.print("TinyGPS++ failed checksum：");
  Serial.println(gps.failedChecksum());

  Serial.print("TinyGPS++ passed checksum：");
  Serial.println(gps.passedChecksum());

  Serial.print("TinyGPS++ sentences with fix：");
  Serial.println(gps.sentencesWithFix());

  Serial.println();

  Serial.print("日期有效 date valid：");
  Serial.println(gps.date.isValid() ? "YES" : "NO");

  Serial.print("時間有效 time valid：");
  Serial.println(gps.time.isValid() ? "YES" : "NO");

  Serial.print("位置有效 location valid：");
  Serial.println(gps.location.isValid() ? "YES" : "NO");

  Serial.print("衛星數有效 satellites valid：");
  Serial.println(gps.satellites.isValid() ? "YES" : "NO");

  Serial.print("HDOP 有效：");
  Serial.println(gps.hdop.isValid() ? "YES" : "NO");

  if (gps.date.isValid()) {
    Serial.print("UTC 日期：");
    Serial.println(getUTCDate());
  }

  if (gps.time.isValid()) {
    Serial.print("UTC 時間：");
    Serial.println(getUTCTime());

    Serial.print("台灣時間：約 ");
    Serial.println(getTaiwanTime());
  }

  if (gps.satellites.isValid()) {
    Serial.print("目前衛星數：");
    Serial.println(gps.satellites.value());
  }

  if (gps.hdop.isValid()) {
    Serial.print("HDOP：");
    Serial.println(gps.hdop.hdop());
  }

  if (gps.location.isValid()) {
    Serial.print("緯度：");
    Serial.println(gps.location.lat(), 8);

    Serial.print("經度：");
    Serial.println(gps.location.lng(), 8);
  }

  Serial.println();

  // =======================
  // 診斷判斷
  // =======================
  if (newChars == 0) {
    Serial.println("❌ GPS 完全沒有收到新資料");
    Serial.println("可能原因：");
    Serial.println("1. GPS TX 沒有接到 XIAO D5");
    Serial.println("2. GPS TX/RX 接反");
    Serial.println("3. GPS 沒有供電");
    Serial.println("4. GPS GND 沒有跟 XIAO GND 共地");
    Serial.println("5. GPS 鮑率不是 9600");
    Serial.println();
    Serial.println("請確認接線：");
    Serial.println("GPS TX  -> XIAO D5");
    Serial.println("GPS RX  -> XIAO D6，可不接");
    Serial.println("GPS GND -> XIAO GND");
    Serial.println("GPS VCC -> 3V3 或 5V");
    return;
  }

  if (gps.charsProcessed() < 10) {
    Serial.println("❌ 有收到資料，但 TinyGPS++ 幾乎無法處理");
    Serial.println("可能原因：");
    Serial.println("1. GPS 鮑率錯誤");
    Serial.println("2. 接收到的不是標準 NMEA 資料");
    Serial.println("3. GPS 模組輸出格式不是 NMEA");
    return;
  }

  if (gps.failedChecksum() > gps.passedChecksum() && gps.failedChecksum() > 20) {
    Serial.println("⚠️ GPS 有資料，但 checksum 失敗偏多");
    Serial.println("可能原因：");
    Serial.println("1. 鮑率不對");
    Serial.println("2. 線太長或接觸不良");
    Serial.println("3. GPS 供電不穩");
    Serial.println("4. UART 干擾");
    Serial.println("建議：先把 SHOW_RAW_NMEA 改成 true，看原始句子是否正常");
    return;
  }

  if (!gps.location.isValid()) {
    Serial.println("⚠️ GPS 有資料，但尚未取得有效經緯度");
    Serial.println("這通常不是接線錯，而是 GPS 尚未定位成功");
    Serial.println("建議：");
    Serial.println("1. 拿到戶外空曠處");
    Serial.println("2. GPS 天線朝上");
    Serial.println("3. 靜止等待 5 到 10 分鐘");
    Serial.println("4. 觀察衛星數是否逐漸增加到 4 顆以上");
    Serial.println("5. 觀察 GGA fix quality 是否從 0 變成 1");
    return;
  }

  Serial.println("✅ GPS 已成功定位，可以寫入 CSV");
}

// ======================================================
// 寫入 GPS 資料到 CSV
// ======================================================
void logGPSData() {
  if (!sdOK) {
    Serial.println("❌ SD 卡不可用，無法寫入 GPS 資料");
    return;
  }

  if (!gps.location.isValid()) {
    Serial.println("⚠️ GPS 尚未定位，不寫入 CSV");
    return;
  }

  File file = SD.open(csvPath, FILE_APPEND);

  if (!file) {
    Serial.println("❌ CSV 檔案開啟失敗");
    Serial.println("可能是 SD 卡中途鬆脫或檔案系統異常");
    sdOK = false;
    return;
  }

  String line = "";

  line += String(millis());
  line += ",";
  line += getUTCDate();
  line += ",";
  line += getUTCTime();
  line += ",";
  line += getTaiwanDateNote();
  line += ",";
  line += getTaiwanTime();
  line += ",";
  line += String(gps.location.lat(), 8);
  line += ",";
  line += String(gps.location.lng(), 8);
  line += ",";

  if (gps.altitude.isValid()) {
    line += String(gps.altitude.meters(), 2);
  } else {
    line += "invalid";
  }

  line += ",";

  if (gps.satellites.isValid()) {
    line += String(gps.satellites.value());
  } else {
    line += "invalid";
  }

  line += ",";

  if (gps.hdop.isValid()) {
    line += String(gps.hdop.hdop(), 2);
  } else {
    line += "invalid";
  }

  file.println(line);
  file.close();

  Serial.print("✅ 已寫入 CSV：");
  Serial.println(line);
}

// ======================================================
// setup
// ======================================================
void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("==============================================");
  Serial.println("XIAO ESP32S3 Sense GPS + SD CSV Logger");
  Serial.println("GPS 腳位：D5 = RX, D6 = TX");
  Serial.println("GPS 接線：GPS TX -> XIAO D5");
  Serial.println("SD 卡：XIAO ESP32S3 Sense 內建 SD 卡槽");
  Serial.println("==============================================");

  // =======================
  // 啟動 GPS UART
  // ESP32 HardwareSerial.begin 格式：
  // begin(baud, config, RX, TX)
  // =======================
  GPSserial.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);

  Serial.println("✅ GPS UART 已啟動");
  Serial.print("GPS baud：");
  Serial.println(GPS_BAUD);
  Serial.println("請確認接線：GPS TX -> XIAO D5，GPS RX -> XIAO D6 可不接");

  // =======================
  // 啟動 SD 卡
  // =======================
  sdOK = initSDCard();

  if (sdOK) {
    createCSVHeaderIfNeeded();
  } else {
    Serial.println();
    Serial.println("⚠️ SD 卡不可用，程式仍會繼續檢查 GPS，但不會寫入 CSV");
  }

  Serial.println();
  Serial.println("系統啟動完成");
  Serial.println("如果 GPS 字元數持續增加，但 location valid = NO，請拿到戶外等待定位");
}

// ======================================================
// loop
// ======================================================
void loop() {
  // =======================
  // 持續讀取 GPS 原始資料
  // =======================
  while (GPSserial.available()) {
    char c = GPSserial.read();

    gpsCharCount++;
    gps.encode(c);

    if (SHOW_RAW_NMEA) {
      Serial.write(c);
    }
  }

  // =======================
  // 每 3 秒做一次 GPS 診斷
  // =======================
  if (millis() - lastCheckTime >= checkInterval) {
    lastCheckTime = millis();
    checkGPSStatus();
  }

  // =======================
  // 每 1 秒嘗試寫入 CSV
  // 只有 GPS 定位成功才會寫入
  // =======================
  if (millis() - lastLogTime >= logInterval) {
    lastLogTime = millis();
    logGPSData();
  }
}