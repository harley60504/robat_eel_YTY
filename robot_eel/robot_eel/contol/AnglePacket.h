#pragma once
#include <Arduino.h>

/* ===============================
 *  UART Angle Packet Definition
 * =============================== */

#define ANGLE_PACKET_HEADER 0xAB
#define MAX_SERVO_COUNT  16   // 你可以改成你的上限（例如 12 / 18 / 24）

#pragma pack(push, 1)
typedef struct {
  uint8_t  header;                       // 0xAB
  uint8_t  count;                        // servo 數量（bodyNum）
  uint32_t seq;                          // sequence number
  float    targetDeg[MAX_SERVO_COUNT];   // 每顆 servo 的目標角度
  uint8_t  checksum;                     // XOR checksum
} AnglePacket;
#pragma pack(pop)

/* ===============================
 *  Checksum Utility
 * =============================== */
static inline uint8_t calcXorChecksum(const uint8_t* data, size_t len) {
  uint8_t cs = 0;
  for (size_t i = 0; i < len; i++) cs ^= data[i];
  return cs;
}

/* ===============================
 *  UART Send Interface (TX)
 * =============================== */
static inline void sendAnglePacketUART(
  HardwareSerial& serial,
  const float* targetDeg,
  uint8_t count,
  uint32_t seq
) {
  AnglePacket pkt;
  pkt.header = ANGLE_PACKET_HEADER;
  pkt.count  = count;
  pkt.seq    = seq;

  for (int i = 0; i < MAX_SERVO_COUNT; i++) {
    pkt.targetDeg[i] = (i < count) ? targetDeg[i] : 0.0f;
  }

  pkt.checksum = calcXorChecksum(
    (uint8_t*)&pkt,
    sizeof(AnglePacket) - 1
  );

  serial.write((uint8_t*)&pkt, sizeof(AnglePacket));
}

/* ===============================
 *  UART Receive State (RX)
 * =============================== */
typedef struct {
  AnglePacket pkt;
  size_t index = 0;
  bool receiving = false;
} AngleRxState;

/* ===============================
 *  Feed byte (return true = ok)
 * =============================== */
static inline bool feedAngleRx(AngleRxState &st, uint8_t b) {
  uint8_t* buf = (uint8_t*)&st.pkt;

  // wait for header
  if (!st.receiving) {
    if (b == ANGLE_PACKET_HEADER) {
      st.receiving = true;
      st.index = 0;
      buf[st.index++] = b;
    }
    return false;
  }

  // receiving...
  buf[st.index++] = b;

  // packet complete
  if (st.index >= sizeof(AnglePacket)) {
    st.receiving = false;

    uint8_t cs = calcXorChecksum(
      (uint8_t*)&st.pkt,
      sizeof(AnglePacket) - 1
    );

    return (cs == st.pkt.checksum);
  }

  return false;
}

/* ===============================
 *  RX Helper: polling serial input
 *
 *  功能：
 *  - 讀取 serial buffer
 *  - 嘗試 parse AnglePacket
 *  - 若成功回傳 true + outPkt 填入
 * =============================== */
static inline bool pollAnglePacketUART(
  HardwareSerial& serial,
  AngleRxState& st,
  AnglePacket& outPkt
) {
  while (serial.available() > 0) {
    uint8_t b = serial.read();

    if (feedAngleRx(st, b)) {
      // ✅ checksum OK
      outPkt = st.pkt;
      return true;
    }
  }
  return false;
}

/* ===============================
 *  RX Cache (optional)
 *
 *  這組變數用於:
 *  - servoTask 直接讀取「最後一次收到的角度」
 *  - 不用每次都傳 AnglePacket 指標出去
 *
 *  注意：
 *  - 這是 header-only 版本，因此用 static
 *  - 只要你 include 這個檔案，每個 .cpp 會各有一份
 *  - 建議你「只在一個 .cpp include」它，或改成 extern 版本
 * =============================== */

static inline bool updateAngleCacheFromUART(
  HardwareSerial& serial,
  AngleRxState& st,
  float angleOut[MAX_SERVO_COUNT],
  uint8_t& countOut,
  uint32_t& lastSeqOut
) {
  AnglePacket pkt;

  if (!pollAnglePacketUART(serial, st, pkt)) {
    return false;
  }

  // ✅ 基本防呆
  if (pkt.count > MAX_SERVO_COUNT) return false;

  // ✅ 防重複封包（可選）
  if (pkt.seq == lastSeqOut) {
    return false;
  }

  countOut = pkt.count;
  lastSeqOut = pkt.seq;

  for (int i = 0; i < pkt.count; i++) {
    angleOut[i] = pkt.targetDeg[i];
  }

  return true;
}
