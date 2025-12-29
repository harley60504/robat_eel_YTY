#include <Arduino.h>
#include "driver/uart.h"

#include "config.h"
#include "utils.h"
#include "logging.h"
#include "cpg.h"
#include "servo.h"
#include "ServoStatusUART.h"
#include "ControltoCamera.h"


// ==========================
//  UART Pins
// ==========================
#define CAMERA_RX_PIN   8
#define CAMERA_TX_PIN   9


// ==========================
//  Servo defaults
// ==========================
float servoDefaultAngles[bodyNum] = {120,120,120,120,120,120};
float angleDeg[bodyNum];


// ==========================
//  Control Parameters
// ==========================
float Ajoint       = 20.0f;
float frequency    = 0.7f;
float lambda       = 0.7f;
float L            = 0.85f;

bool  isPaused     = false;
int   controlMode  = 0;
bool  useFeedback  = false;
float feedbackGain = 1.0f;


// ==========================
HopfOscillator cpg[bodyNum];
unsigned long g_lastLogTime = 0;


// ==========================
// RX state
// ==========================
static ControlRxState camRx;


// ==========================
// UART TX Task  (→ Camera)
// ==========================
void cameraTxTask(void* pv)
{
  TickType_t lastWake = xTaskGetTickCount();

  while(true)
  {
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


// ==========================
// UART RX Task  (← Camera)
// ==========================
void cameraRxTask(void* pv)
{
  Serial.println("Camera RX Task started");

  while(true)
  {
    while(Serial2.available())
    {
      uint8_t b = Serial2.read();

      if(feedControlRx(camRx, b))
      {
        ControlPacket &pkt = camRx.pkt;

        Ajoint       = pkt.Ajoint;
        frequency    = pkt.frequency;
        lambda       = pkt.lambda;
        L            = pkt.L;
        isPaused     = pkt.isPaused;
        controlMode  = pkt.controlMode;
        useFeedback  = pkt.useFeedback;
        feedbackGain = pkt.feedbackGain;

        Serial.println("==== UART ← Camera ====");
        Serial.printf("A=%.2f  f=%.2f  λ=%.2f  L=%.2f\n",
          Ajoint, frequency, lambda, L
        );
      }
    }

    vTaskDelay(1);
  }
}



// ==========================
// SETUP
// ==========================
void setup()
{
  Serial.begin(115200);
  delay(300);

  // Servo UART
  Serial1.begin(115200, SERIAL_8N1, SERVO_RX_PIN, SERVO_TX_PIN);
  uart_set_mode(UART_NUM_1, UART_MODE_RS485_HALF_DUPLEX);

  // Camera UART
  Serial2.begin(
    115200,
    SERIAL_8N1,
    CAMERA_RX_PIN,
    CAMERA_TX_PIN
  );

  Serial.println("Control Board Ready");

  initCPG();
  initLogFile();


  // Servo Task
  xTaskCreatePinnedToCore(
    servoTask,
    "servoTask",
    4096,
    nullptr,
    2,
    nullptr,
    1        // Core 1
  );


  // UART TX Task
  xTaskCreatePinnedToCore(
    cameraTxTask,
    "cameraTxTask",
    4096,
    nullptr,
    1,
    nullptr,
    0        // Core 0
  );

  // UART RX Task
  xTaskCreatePinnedToCore(
    cameraRxTask,
    "cameraRxTask",
    4096,
    nullptr,
    1,
    nullptr,
    0
  );

  xTaskCreatePinnedToCore(
    servoStatusTxTask,
    "servoStatusTxTask",
    4096,
    nullptr,
    1,
    nullptr,
    0
  );
}



// ==========================
// MAIN LOOP
// ==========================
void loop()
{
  logServoErrorAvgPerMinute();
}
