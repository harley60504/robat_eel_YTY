#include <Arduino.h>
#include "driver/uart.h"

#include "config.h"
#include "utils.h"
#include "logging.h"
#include "cpg.h"
#include "servo.h"
#include "ControltoCamera.h"

// ==============================
// Camera UART 定義
//（請依你實際接線調整 GPIO）
// ==============================
#define CAMERA_RX_PIN   8     // D9
#define CAMERA_TX_PIN   9     // D10


// ==============================
// Servo / params（原封不動）
// ==============================
float servoDefaultAngles[bodyNum] = {120,120,120,120,120,120};
float angleDeg[bodyNum];

float Ajoint       = 20.0f;
float frequency    = 0.7f;
float lambda       = 0.7f;
float L            = 0.85f;

bool  isPaused     = false;
int   controlMode  = 0;
bool  useFeedback  = false;
float feedbackGain = 1.0f;


// ==============================
// CPG
// ==============================
HopfOscillator cpg[bodyNum];


// ==============================
// Logging
// ==============================
unsigned long g_lastLogTime = 0;


// ==============================
// Camera UART TX Task
// ==============================
// 每 100ms 傳送一次參數（10Hz）

void cameraTxTask(void* pv) {

  TickType_t lastWake = xTaskGetTickCount();

  while (true) {

    sendControlParamsUART(
      Serial2,
      Ajoint,
      frequency,
      lambda,
      L,
      isPaused,
      controlMode,
      useFeedback,
      feedbackGain
    );

    vTaskDelayUntil(&lastWake, pdMS_TO_TICKS(100));   // 10Hz
  }
}



// ==============================
// SETUP
// ==============================

void setup() {
  Serial.begin(115200);
  delay(300);


  // ===== Servo UART（保持原樣）=====
  Serial1.begin(115200, SERIAL_8N1, SERVO_RX_PIN, SERVO_TX_PIN);
  uart_set_mode(UART_NUM_1, UART_MODE_RS485_HALF_DUPLEX);


  // ===== Camera UART =====
  Serial2.begin(
    115200,
    SERIAL_8N1,
    CAMERA_RX_PIN,
    CAMERA_TX_PIN
  );

  Serial.println("Control Board Ready");


  initCPG();
  initLogFile();


  // ===== Servo Task（原樣）=====
  xTaskCreatePinnedToCore(
    servoTask,
    "servoTask",
    4096,
    nullptr,
    2,         // 高優先權
    nullptr,
    1          // CPU Core 1
  );


  // ===== Camera UART TX Task =====
  xTaskCreatePinnedToCore(
    cameraTxTask,
    "cameraTxTask",
    4096,
    nullptr,
    1,         // 比 servoTask 低
    nullptr,
    0          // CPU Core 0
  );
}



// ==============================
// LOOP（維持 logging）
// ==============================

void loop() {
  logServoErrorAvgPerMinute();
}
