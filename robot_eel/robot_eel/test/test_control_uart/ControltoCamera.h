#ifndef CONTROL_TO_CAMERA_H
#define CONTROL_TO_CAMERA_H

#include <Arduino.h>

/* ===============================
 *  UART Control Packet Definition
 * =============================== */

#define CONTROL_PACKET_HEADER 0xAA

#pragma pack(push, 1)
typedef struct {
  uint8_t  header;        // 0xAA
  float    Ajoint;
  float    frequency;
  float    lambda;
  float    L;
  bool     isPaused;
  uint8_t  controlMode;
  bool     useFeedback;
  float    feedbackGain;
  uint8_t  checksum;      // XOR checksum
} ControlPacket;
#pragma pack(pop)

/* ===============================
 *  Checksum Utility
 * =============================== */
static inline uint8_t calcControlChecksum(const uint8_t* data, size_t len) {
  uint8_t cs = 0;
  for (size_t i = 0; i < len; i++) {
    cs ^= data[i];
  }
  return cs;
}

/* ===============================
 *  UART Send Interface
 * =============================== */
static inline void sendControlParamsUART(
  HardwareSerial& serial,
  float  Ajoint,
  float  frequency,
  float  lambda,
  float  L,
  bool   isPaused,
  uint8_t controlMode,
  bool   useFeedback,
  float  feedbackGain
) {
  ControlPacket pkt;

  pkt.header        = CONTROL_PACKET_HEADER;
  pkt.Ajoint        = Ajoint;
  pkt.frequency     = frequency;
  pkt.lambda        = lambda;
  pkt.L             = L;
  pkt.isPaused      = isPaused;
  pkt.controlMode   = controlMode;
  pkt.useFeedback   = useFeedback;
  pkt.feedbackGain  = feedbackGain;

  pkt.checksum = calcControlChecksum(
    (uint8_t*)&pkt,
    sizeof(ControlPacket) - 1
  );

  serial.write((uint8_t*)&pkt, sizeof(ControlPacket));
}

#endif  // CONTROL_TO_CAMERA_H
