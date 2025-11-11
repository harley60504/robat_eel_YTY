#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <Adafruit_ADS1X15.h>
#include <PL_ADXL355.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// LX-224
#define SERVO_TX_PIN 43
#define CMD_MOVE_TIME_WRITE 0x01
#define HEADER 0x55
#define BROADCAST_ID 0xFE
#define CMD_ID_WRITE 0x13
#define bodyNum 6

// ADS1115 I2C pins
#define SCL_PIN 2
#define SDA_PIN 3

// ADXL355 SPI pins
#define ADXL_SCLK 14
#define ADXL_MISO 12
#define ADXL_MOSI 13
#define ADXL_CS   15

struct HopfOscillator {
  float r;
  float theta;
  float alpha;
  float mu;
};

extern WebServer server;

// WiFi
extern const char* ssid1;
extern const char* password1;
extern const char* ssid2;
extern const char* password2;
extern const char* AP_SSID;
extern const char* AP_PASS;
extern const char* HOSTNAME;
extern String connectedSSID;

// Servo / params
extern float servoDefaultAngles[bodyNum];
extern float angleDeg[bodyNum];
extern float Ajoint;
extern float frequency;
extern float lambda;
extern float L;
extern float adsMinValidVoltage;
extern bool  isPaused;
extern int   controlMode;
extern bool  useFeedback;
extern float feedbackGain;

// CPG
extern HopfOscillator cpg[bodyNum];

// ADS1115
extern Adafruit_ADS1115 ads1;
extern Adafruit_ADS1115 ads2;
extern float adsVoltage1[4];
extern float adsVoltage2[4];
extern float ads1Diff[3];

// Logging
extern unsigned long g_lastLogTime;

// ADXL355
extern PL::ADXL355 adxl355;
extern volatile float adxlX, adxlY, adxlZ;
extern volatile float pitchDeg, rollDeg;

// forward decl
void logADSDataEveryMinute();
