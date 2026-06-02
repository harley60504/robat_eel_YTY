#include <Arduino.h>
#include "ControltoCamera.h"

/* ===============================
 *  UART1 腳位設定
 * =============================== */
#define UART1_RX_PIN 8
#define UART1_TX_PIN 9   // 接收端可不接，但仍需宣告

/* ===============================
 *  接收狀態機
 * =============================== */
ControlPacket rxPacket;
uint8_t* rxBuf = (uint8_t*)&rxPacket;
size_t rxIndex = 0;
bool receiving = false;

void setup() {
  Serial.begin(115200);
  delay(300);

  Serial1.begin(
    115200,
    SERIAL_8N1,
    UART1_RX_PIN,
    UART1_TX_PIN
  );

  Serial.println("=== ESP32 Camera UART RX Test ===");
}

void loop() {
  while (Serial1.available()) {
    uint8_t byteIn = Serial1.read();

    /* 等待封包 Header */
    if (!receiving) {
      if (byteIn == CONTROL_PACKET_HEADER) {
        receiving = true;
        rxIndex = 0;
        rxBuf[rxIndex++] = byteIn;
      }
    }
    /* 接收剩餘封包內容 */
    else {
      rxBuf[rxIndex++] = byteIn;

      if (rxIndex >= sizeof(ControlPacket)) {
        receiving = false;
        handlePacket();
      }
    }
  }
}

/* ===============================
 *  封包處理
 * =============================== */
void handlePacket() {
  uint8_t cs = calcControlChecksum(
    (uint8_t*)&rxPacket,
    sizeof(ControlPacket) - 1
  );

  if (cs != rxPacket.checksum) {
    Serial.println("[RX] Checksum ERROR");
    return;
  }

  Serial.println("---- RX Control Packet ----");
  Serial.printf("Ajoint        = %.2f\n", rxPacket.Ajoint);
  Serial.printf("frequency     = %.2f\n", rxPacket.frequency);
  Serial.printf("lambda        = %.2f\n", rxPacket.lambda);
  Serial.printf("L             = %.2f\n", rxPacket.L);
  Serial.printf("isPaused      = %d\n", rxPacket.isPaused);
  Serial.printf("controlMode   = %d\n", rxPacket.controlMode);
  Serial.printf("useFeedback   = %d\n", rxPacket.useFeedback);
  Serial.printf("feedbackGain  = %.2f\n", rxPacket.feedbackGain);
}
