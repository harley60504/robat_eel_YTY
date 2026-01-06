#include "CtrlUartBridge.h"

#define SERVO_STATUS_HEADER 0xBB
#define SERVO_MAX 8

static ControlRxState g_rx;
static HardwareSerial* g_ser = nullptr;

std::function<void(const ControlPacket&)> CtrlUartBridge::onCtrlParams = nullptr;
std::function<void(const ServoStatus&)>   CtrlUartBridge::onServoStatus = nullptr;

static uint8_t buf[128];
static size_t idx = 0;
static bool receiving = false;

static const size_t SERVO_PKT_SIZE =
    sizeof(ServoStatus);   // = 1 +1 + 8*3*4 +1 = 102 bytes


// ==================================================
// UART RX Task
// ==================================================
static void uartRxTask(void *pv)
{
  while(true)
  {
    while(g_ser->available())
    {
      uint8_t b = g_ser->read();

      // ---------- ctrl_params ----------
      if(feedControlRx(g_rx,b))
      {
        if(CtrlUartBridge::onCtrlParams)
          CtrlUartBridge::onCtrlParams(g_rx.pkt);

        continue;
      }

      // ---------- servo_status ----------
      if(!receiving)
      {
        if(b == SERVO_STATUS_HEADER)
        {
          receiving = true;
          idx = 0;
          buf[idx++] = b;
        }
        continue;
      }

      buf[idx++] = b;

      if(idx >= SERVO_PKT_SIZE)
      {
        receiving = false;

        ServoStatus ss;
        memcpy(&ss, buf, SERVO_PKT_SIZE);

        // checksum 驗證
        uint8_t cs =
          calcControlChecksum((uint8_t*)&ss, SERVO_PKT_SIZE-1);

        if(cs == ss.checksum)
        {
          if(CtrlUartBridge::onServoStatus)
            CtrlUartBridge::onServoStatus(ss);
        }
      }
    }

    vTaskDelay(1);
  }
}


// ==================================================
// TX
// ==================================================
void CtrlUartBridge::sendCtrlParams(const ControlPacket &pkt)
{
  sendControlParamsUART(
    *g_ser,
    pkt.Ajoint,
    pkt.frequency,
    pkt.lambda,
    pkt.L,
    pkt.isPaused,
    pkt.controlMode,
    pkt.useFeedback,
    pkt.feedbackGain
  );
}


// ==================================================
// INIT
// ==================================================
void CtrlUartBridge::begin(HardwareSerial& ser,
                           uint32_t baud,
                           int rxPin,
                           int txPin)
{
  g_ser = &ser;

  ser.begin(
    baud,
    SERIAL_8N1,
    rxPin,
    txPin
  );

  xTaskCreatePinnedToCore(
    uartRxTask,
    "uartRxTask",
    4096,
    nullptr,
    1,
    nullptr,
    1
  );
}
