#pragma once
#include <Arduino.h>
#include <WebServer.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// =====================================================
// LX-224 Servo
// =====================================================
#define SERVO_TX_PIN 43
#define SERVO_RX_PIN 44
#define HEADER 0x55

#define CMD_MOVE_TIME_WRITE 0x01
#define CMD_READ_POS        0x1C
#define CMD_ID_WRITE        0x13

#define BROADCAST_ID 0xFE

#define bodyNum 6

// =====================================================
// CPG oscillator
// =====================================================
struct HopfOscillator {
  float r;
  float theta;
  float alpha;
  float mu;
};

// =====================================================
// Web
// =====================================================
extern WebServer server;

// =====================================================
// WiFi
// =====================================================
extern const char* AP_SSID;
extern const char* AP_PASS;
extern const char* HOSTNAME;

// =====================================================
// Servo / Control parameters
// =====================================================
extern float servoDefaultAngles[bodyNum];
extern float angleDeg[bodyNum];

extern float Ajoint;
extern float frequency;
extern float lambda;
extern float L;

extern bool  isPaused;
extern int   controlMode;
extern bool  useFeedback;
extern float feedbackGain;

// =====================================================
// CPG state
// =====================================================
extern HopfOscillator cpg[bodyNum];
