// XIAO ESP32S3 Sense GPS 測試
// 你的設定：D10 = RX, D9 = TX
// GPS 模組 TX -> XIAO D10
// GPS 模組 RX -> XIAO D9
// GND -> GND
// VCC -> 3V3 或 5V（依你的 GPS breakout 板而定）

HardwareSerial GPS(1);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("GPS raw NMEA test start");

  // 你的接法：RX=D10, TX=D9
  GPS.begin(9600, SERIAL_8N1, D9, D10);
}

void loop() {
  while (GPS.available()) {
    char c = GPS.read();
    Serial.write(c);   // 直接把 GPS 原始 NMEA 印到序列埠監看視窗
  }
}