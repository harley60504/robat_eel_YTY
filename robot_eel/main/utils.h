#pragma once
#include "config.h"
#include <math.h>

static inline float clampf(float x, float lo, float hi) {
  return x < lo ? lo : (x > hi ? hi : x);
}
static inline float linmap(float x, float in_min, float in_max, float out_min, float out_max) {
  if (fabs(in_max - in_min) < 1e-6f) return out_min;
  float r = (x - in_min) / (in_max - in_min);
  return out_min + r * (out_max - out_min);
}

inline int degreeToLX224(float deg) {
  deg = clampf(deg, 0.0f, 240.0f);
  return (int)(deg / 240.0f * 1000.0f);
}
inline uint8_t checksum(const uint8_t *buf) {
  uint16_t sum = 0;
  uint8_t length = buf[3];
  for (int i = 2; i < length + 2; ++i) sum += buf[i];
  return (uint8_t)(~sum);
}
inline void writePacket(uint8_t id, uint8_t cmd, const uint8_t *payload, uint8_t plen) {
  uint8_t len = 3 + plen;
  uint8_t buf[32];
  int idx = 0;
  buf[idx++] = HEADER;
  buf[idx++] = HEADER;
  buf[idx++] = id;
  buf[idx++] = len;
  buf[idx++] = cmd;
  for (int i = 0; i < plen; ++i) buf[idx++] = payload[i];
  buf[idx] = checksum(buf);
  Serial1.write(buf, idx + 1);
}
inline void moveLX224(uint8_t id, int position, uint16_t time_ms) {
  position = position < 0 ? 0 : (position > 1000 ? 1000 : position);
  uint8_t p[4];
  p[0] = position & 0xFF;
  p[1] = (position >> 8) & 0xFF;
  p[2] = time_ms & 0xFF;
  p[3] = (time_ms >> 8) & 0xFF;
  writePacket(id, CMD_MOVE_TIME_WRITE, p, 4);
}
inline void setServoID(uint8_t targetId, uint8_t newId) {
  uint8_t p[1] = { newId };
  writePacket(targetId, CMD_ID_WRITE, p, 1);
  delay(50);
}
