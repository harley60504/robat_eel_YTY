#include <Arduino.h>
#include "ControltoCamera.h"

#define UART1_RX_PIN 8
#define UART1_TX_PIN 9
/* ===============================
 *  測試用參數
 * =============================== */
float Ajoint       = 20.0f;
float frequency    = 0.7f;
float lambda       = 0.7f;
float L            = 0.85f;
bool  isPaused     = false;
uint8_t controlMode = 0;
bool  useFeedback  = false;
float feedbackGain = 1.0f;

unsigned long lastSend = 0;

void setup() {
  Serial.begin(115200);
  delay(300);

  // 與你正式程式一致
  Serial1.begin(115200, SERIAL_8N1, UART1_RX_PIN, UART1_TX_PIN);

  Serial.println("=== Control → Camera UART Test Start ===");
}

void loop() {

  // 每 500 ms 更新一次測試參數
  if (millis() - lastSend > 500) {
    lastSend = millis();

    // 讓參數有變化，方便觀察
    Ajoint       = 20.0f + 5.0f * sin(millis() * 0.001f);
    frequency    = 0.5f + 0.2f * sin(millis() * 0.0008f);
    lambda       = 0.7f;
    L            = 0.85f;
    isPaused     = false;
    controlMode  = (controlMode + 1) % 3;
    useFeedback  = true;
    feedbackGain = 1.0f + 0.5f * sin(millis() * 0.0005f);

    // 傳送 UART 封包
    sendControlParamsUART(
      Serial1,
      Ajoint,
      frequency,
      lambda,
      L,
      isPaused,
      controlMode,
      useFeedback,
      feedbackGain
    );

    // Debug 印出目前送的值
    Serial.println("---- TX Control Packet ----");
    Serial.printf("Ajoint        = %.2f\n", Ajoint);
    Serial.printf("frequency     = %.2f\n", frequency);
    Serial.printf("lambda        = %.2f\n", lambda);
    Serial.printf("L             = %.2f\n", L);
    Serial.printf("isPaused      = %d\n", isPaused);
    Serial.printf("controlMode   = %d\n", controlMode);
    Serial.printf("useFeedback   = %d\n", useFeedback);
    Serial.printf("feedbackGain  = %.2f\n", feedbackGain);
  }
}
