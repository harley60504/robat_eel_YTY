#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <math.h>
#include <Wire.h>
#include <Adafruit_ADS1X15.h>
#include "FS.h"
#include "SPIFFS.h"
#include <SPI.h>
#include <PL_ADXL355.h>

// ================== 安全常數 ==================
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ================== LX-224 串列伺服控制 ==================
#define SERVO_TX_PIN 43
#define CMD_MOVE_TIME_WRITE 0x01
#define HEADER 0x55
#define BROADCAST_ID 0xFE
#define CMD_ID_WRITE 0x13
#define bodyNum 6

float servoDefaultAngles[bodyNum] = {120, 120, 120, 120, 120, 120};
float angleDeg[bodyNum]; // 當前輸出的角度（度）

// ================== 全域控制參數 ==================
float Ajoint = 20.0f;     // 關節振幅（度）
float frequency = 0.7f;   // 赫茲
float lambda = 0.7f;      // 波長控制
float L = 0.85f;          // 幾何長度係數
float adsMinValidVoltage = 0.6f; // ADS 懸空修正閾值（小於則視為 0）

bool isPaused = false;    // 模式: 0 = sin, 1 = cpg, 2 = offset
int  controlMode = 0;
bool useFeedback = true;  // 預設啟用回授
float feedbackGain = 1.0f;// 回授權重 (0 = 關閉, 1 = 全部啟用)

// ================== WiFi ==================
const char *ssid1 = "YTY_2.4g";
const char *password1 = "weareytylab";
const char *ssid2 = "TP-Link_9BD8_2.4g";
const char *password2 = "qwer4321";
String connectedSSID = "未連接";
WebServer server(80);

// ================== 兩顆 ADS1115 ==================
#define SCL_PIN 2
#define SDA_PIN 3
Adafruit_ADS1115 ads1; // 0x48
Adafruit_ADS1115 ads2; // 0x49
float adsVoltage1[4] = {0,0,0,0}; // 單端
float adsVoltage2[4] = {0,0,0,0};
float ads1Diff[3]    = {0,0,0};   // 差分: [0]=A0-A1, [1]=A2-A3, [2]=A0-A3

// ================== CSV 紀錄 ==================
unsigned long g_lastLogTime = 0;
File g_logFile;

// ================== 小工具 ==================
static inline float clampf(float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
// Arduino 內建 map 是 long→long，這裡做 float 版
static inline float linmap(float x, float in_min, float in_max, float out_min, float out_max) {
  if (fabs(in_max - in_min) < 1e-6f) return out_min;
  float r = (x - in_min) / (in_max - in_min);
  return out_min + r * (out_max - out_min);
}
int degreeToLX224(float deg) {
  deg = clampf(deg, 0.0f, 240.0f);
  return (int)(deg / 240.0f * 1000.0f); // 0..1000
}
uint8_t checksum(const uint8_t *buf) {
  uint16_t sum = 0;
  uint8_t length = buf[3];
  for (int i = 2; i < length + 2; ++i) sum += buf[i];
  return (uint8_t)(~sum);
}
void writePacket(uint8_t id, uint8_t cmd, const uint8_t *payload, uint8_t plen) {
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
void moveLX224(uint8_t id, int position, uint16_t time_ms) {
  position = position < 0 ? 0 : (position > 1000 ? 1000 : position);
  uint8_t p[4];
  p[0] = position & 0xFF;
  p[1] = (position >> 8) & 0xFF;
  p[2] = time_ms & 0xFF;
  p[3] = (time_ms >> 8) & 0xFF;
  writePacket(id, CMD_MOVE_TIME_WRITE, p, 4);
}
void setServoID(uint8_t targetId, uint8_t newId) {
  uint8_t p[1] = { newId };
  writePacket(targetId, CMD_ID_WRITE, p, 1);
  delay(50);
}

// ================== CPG 模組（改良版 Hopf：極座標 + 耦合 + 錨定 + 回授） ==================
struct HopfOscillator {
  float r;     // 振幅半徑
  float theta; // 相位
  float alpha; // 參數（成長率）
  float mu;    // 參數（極限環半徑平方）
};
HopfOscillator cpg[bodyNum];
inline float wrap_pi(float x) {
  while (x >  M_PI) x -= 2*M_PI;
  while (x < -M_PI) x += 2*M_PI;
  return x;
}
void initCPG() {
  for (int j = 0; j < bodyNum; j++) {
    cpg[j].r = 0.25f;
    cpg[j].theta = j / (lambda * L); // 初始相位微分散
    cpg[j].alpha = 12.0f;
    cpg[j].mu = 1.0f;
  }
}
// 取 CPG 輸出（對應 Python: y = Ajoint * r * cos(theta)）
float getCPGOutput(int j) {
  return Ajoint * cpg[j].r * cosf(cpg[j].theta);
}
// Flex Sensor 轉角度（度）：由 ADS 電壓線性映射
float getSensorAngle(int j) {
  float v = adsVoltage1[j % 4];
  if (v < adsMinValidVoltage) v = 0.0f;
  // ★ 校正建議：用兩個姿態點量測電壓（例如 0°、90°），把下方 in_min/in_max 改成實測值。
  const float in_min = 3.16f; // 電壓對應 0 度
  const float in_max = 2.26f; // 電壓對應 90 度（示意）
  float angle = clampf(linmap(v, in_min, in_max, 0.0f, 90.0f), 0.0f, 180.0f);
  return angle;
}
float getLambdaInput() { return lambda * L; }
float getTargetDelta() { return 1.0f / getLambdaInput(); }
// 更新單一節點的 CPG 狀態（含耦合、錨定、回授）
void updateCPG(float t, float dt, int j, float fb_phase, float fb_amp) {
  HopfOscillator &o = cpg[j];
  float omega = 2.0f * M_PI * frequency; // 角頻率
  float dr = o.alpha * (o.mu - o.r * o.r) * o.r;
  float dtheta = omega;

  const float K_couple   = 1.0f;
  const float K_anchor   = 0.3f;
  const float k_fb_phase = 0.8f;
  const float k_fb_amp   = 0.25f;
  const float target_delta = getTargetDelta();

  // 與左鄰/右鄰的相位耦合（希望相鄰相位差接近 ±target_delta）
  if (j - 1 >= 0) {
    float errL = wrap_pi((cpg[j-1].theta - o.theta) - (-target_delta));
    dtheta += K_couple * sinf(errL);
  }
  if (j + 1 < bodyNum) {
    float errR = wrap_pi((cpg[j+1].theta - o.theta) - (+target_delta));
    dtheta += K_couple * sinf(errR);
  }
  // 錨定：朝向「理想行進波相位」
  float th_ref = omega * t + j / getLambdaInput();
  float e_ref = wrap_pi(th_ref - o.theta);
  dtheta += K_anchor * sinf(e_ref);

  // Flex Sensor 回授修正（相位、振幅）
  dtheta += k_fb_phase * fb_phase;
  dr     += k_fb_amp   * fb_amp;

  // 整合
  o.r     += dr * dt;
  o.theta  = wrap_pi(o.theta + dtheta * dt);
}

// ================== ADXL355（SPI） ==================
// ★ 依實際佈線修改下列腳位（這組是 ESP32 常見 VSPI 腳位）
//   ESP32-S3 請改成你板子可用的 SPI 腳位
#define ADXL_SCLK 14
#define ADXL_MISO 12
#define ADXL_MOSI 13
#define ADXL_CS   15

PL::ADXL355 adxl355;
volatile float adxlX = 0.0f, adxlY = 0.0f, adxlZ = 0.0f;
volatile float pitchDeg = 0.0f;   // 俯仰（deg）
volatile float rollDeg  = 0.0f;   // 橫滾（deg）

static inline void accelToEuler(float ax, float ay, float az,
                                float &pitch_deg, float &roll_deg) {
  // 常見定義（右手座標）：roll = atan2(ay, az)
  //                        pitch = atan2(-ax, sqrt(ay^2 + az^2))
  // 若裝法不同可針對 ax/ay/az 加負號或互換
  float roll  = atan2f(ay, az);
  float pitch = atan2f(-ax, sqrtf(ay*ay + az*az));
  pitch_deg = pitch * 180.0f / M_PI;
  roll_deg  = roll  * 180.0f / M_PI;
}

void initADXL() {
  // 初始化 SPI 總線與 ADXL355
  SPI.begin(ADXL_SCLK, ADXL_MISO, ADXL_MOSI, ADXL_CS);
  delay(10);
  adxl355.beginSPI(ADXL_CS); // 使用 PL_ADXL355 的 SPI 介面
  adxl355.setRange(PL::ADXL355_Range::range2g); // +/- 2g
  adxl355.enableMeasurement();
  Serial.println("✅ ADXL355 SPI 初始化完成");
}

// ================== Web UI ==================
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-TW">
<head>
<meta charset="UTF-8">
<title>ESP32 LX-224 控制面板</title>
<style>
  body {
    font-family: "Noto Sans TC", Arial, sans-serif;
    background-color: #f5f5f5;
    margin: 0;
    padding: 0;
  }
  h2 {
    background-color: #007bff;
    color: white;
    padding: 12px;
    margin: 0;
  }
  .container {
    display: flex;
    flex-wrap: wrap;
    justify-content: center;
    padding: 10px;
  }
  .card {
    background: white;
    box-shadow: 0 2px 6px rgba(0,0,0,0.2);
    border-radius: 12px;
    padding: 15px;
    margin: 10px;
    width: 300px;
    transition: 0.3s;
  }
  .card:hover { transform: translateY(-3px); }
  button, input, label {
    margin: 5px;
    padding: 6px;
    font-size: 15px;
  }
  button {
    background-color: #007bff;
    color: white;
    border: none;
    border-radius: 6px;
    cursor: pointer;
  }
  button:hover { background-color: #0056b3; }
  #status span {
    font-weight: bold;
    color: #007bff;
  }
  .sensor-table {
    text-align: left;
    width: 100%;
  }
  .sensor-table td {
    padding: 2px 6px;
  }
</style>
</head>

<body>
  <h2>🐍 ESP32 LX-224 控制面板</h2>

  <div class="container">
    <!-- 模式控制 -->
    <div class="card">
      <h3>🧭 模式切換</h3>
      <button onclick="setMode(0)">Sin 模式</button>
      <button onclick="setMode(1)">CPG 模式</button>
      <button onclick="setMode(2)">Offset 模式</button><br>
      <p>目前模式：<span id="mode">-</span></p>
      <button onclick="toggleFeedback()">切換回授</button>
      <p>回授狀態：<span id="feedback">-</span></p>
    </div>

    <!-- 參數控制 -->
    <div class="card">
      <h3>⚙️ 參數設定</h3>
      <label>頻率 (Hz):</label>
      <input type="number" id="freqInput" step="0.1" value="0.7">
      <button onclick="setFrequency()">設定</button><br>

      <label>振幅 (°):</label>
      <input type="number" id="ampInput" step="1" value="20">
      <button onclick="setAmplitude()">設定</button><br>

      <label>λ (lambda):</label>
      <input type="number" id="lambdaInput" step="0.05" value="0.7">
      <button onclick="setLambda()">設定</button><br>

      <label>L:</label>
      <input type="number" id="Linput" step="0.05" value="0.85">
      <button onclick="setL()">設定</button><br>

      <label>回授權重:</label>
      <input type="range" id="fbGain" min="0" max="1" step="0.1" value="1" oninput="document.getElementById('fbVal').innerText=this.value">
      <span id="fbVal">1.0</span>
      <button onclick="setFeedbackGain()">設定</button>
    </div>

    <!-- 狀態監控 -->
    <div class="card" id="status">
      <h3>📡 系統狀態</h3>
      <p>頻率：<span id="freq">-</span> Hz</p>
      <p>振幅：<span id="amp">-</span> °</p>
      <p>λ：<span id="lambda">-</span></p>
      <p>L：<span id="L">-</span></p>
      <p>回授權重：<span id="fbGainStatus">-</span></p>
    </div>

    <!-- ADXL355 -->
    <div class="card">
      <h3>📈 ADXL355 加速度計</h3>
      <table class="sensor-table">
        <tr><td>X (g):</td><td><span id="ax">-</span></td></tr>
        <tr><td>Y (g):</td><td><span id="ay">-</span></td></tr>
        <tr><td>Z (g):</td><td><span id="az">-</span></td></tr>
        <tr><td>Pitch (°):</td><td><span id="pitch">-</span></td></tr>
        <tr><td>Roll (°):</td><td><span id="roll">-</span></td></tr>
      </table>
    </div>

    <!-- ADS1115 -->
    <div class="card">
      <h3>🔌 ADS1115 8通道電壓</h3>
      <table class="sensor-table">
        <tr><td>ADS1 A0:</td><td><span id="ads1_0">-</span> V</td></tr>
        <tr><td>ADS1 A1:</td><td><span id="ads1_1">-</span> V</td></tr>
        <tr><td>ADS1 A2:</td><td><span id="ads1_2">-</span> V</td></tr>
        <tr><td>ADS1 A3:</td><td><span id="ads1_3">-</span> V</td></tr>
        <tr><td>ADS2 A0:</td><td><span id="ads2_0">-</span> V</td></tr>
        <tr><td>ADS2 A1:</td><td><span id="ads2_1">-</span> V</td></tr>
        <tr><td>ADS2 A2:</td><td><span id="ads2_2">-</span> V</td></tr>
        <tr><td>ADS2 A3:</td><td><span id="ads2_3">-</span> V</td></tr>
      </table>
    </div>

    <!-- 相機串流 -->
    <div class="card">
      <h3>📷 XIAO ESP32S3 相機畫面</h3>
      <img src="http://192.168.0.199/stream"
          style="width:100%;border-radius:10px;box-shadow:0 0 10px rgba(0,0,0,0.4);">
    </div>


    <!-- 🕒 系統控制 -->
    <div class="card">
      <h3>🕒 系統控制</h3>
      <p>運作時間：<span id="uptime">0:00</span></p>
      <button onclick="togglePause()">⏸ 暫停 / ▶️ 繼續</button>
      <button onclick="downloadCSV()">📥 下載 CSV</button>
    </div>
  </div>




  <script>
    function setMode(m){ fetch('/setMode?m='+m).then(r=>r.text()).then(t=>{document.getElementById("mode").innerText=t;}); }
    function toggleFeedback(){ fetch('/toggleFeedback').then(r=>r.text()).then(t=>{document.getElementById("feedback").innerText=t;}); }
    function setFrequency(){ fetch('/setFrequency?f='+document.getElementById("freqInput").value); }
    function setAmplitude(){ fetch('/setAmplitude?a='+document.getElementById("ampInput").value); }
    function setLambda(){ fetch('/setLambda?lambda='+document.getElementById("lambdaInput").value); }
    function setL(){ fetch('/setL?L='+document.getElementById("Linput").value); }
    function setFeedbackGain(){ fetch('/setFeedbackGain?g='+document.getElementById("fbGain").value); }

    function togglePause(){fetch('/toggle_pause').then(r=>r.text()).then(()=>{alert("已切換暫停/繼續狀態");}); }

    function downloadCSV(){window.location.href = '/download'; }

    // 將分鐘值轉換為「分:秒」格式
    function formatTime(minuteTotal){
      const totalSec = Math.floor(minuteTotal * 60);
      const min = Math.floor(totalSec / 60);
      const sec = totalSec % 60;
      return `${min}:${sec.toString().padStart(2, '0')}`;
    } 


    function refreshStatus(){
      fetch('/status').then(r=>r.json()).then(j=>{
        document.getElementById("freq").innerText = j.frequency.toFixed(2);
        document.getElementById("amp").innerText = j.amplitude.toFixed(1);
        document.getElementById("lambda").innerText = j.lambda.toFixed(2);
        document.getElementById("L").innerText = j.L.toFixed(2);
        document.getElementById("mode").innerText = j.mode;
        document.getElementById("feedback").innerText = j.feedback;
        document.getElementById("fbGainStatus").innerText = j.fbGain.toFixed(2);
        document.getElementById("ax").innerText = j.adxl_x_g.toFixed(3);
        document.getElementById("ay").innerText = j.adxl_y_g.toFixed(3);
        document.getElementById("az").innerText = j.adxl_z_g.toFixed(3);
        document.getElementById("pitch").innerText = j.pitch_deg.toFixed(2);
        document.getElementById("roll").innerText = j.roll_deg.toFixed(2);

        // ADS
        for(let i=0;i<4;i++) document.getElementById("ads1_"+i).innerText = j["ads1_ch"+i].toFixed(3);
        for(let i=0;i<4;i++) document.getElementById("ads2_"+i).innerText = j["ads2_ch"+i].toFixed(3);

        document.getElementById("uptime").innerText = formatTime(j.uptime_min);
      });
    }
    setInterval(refreshStatus, 1000);
  </script>
</body>
</html>
)rawliteral";


void setupWebServer() {
  // 首頁 (Web UI)
  server.on("/", []() {
    server.send(200, "text/html", INDEX_HTML);
  });

  // ---- 模式切換 ----
  server.on("/setMode", []() {
    if (server.hasArg("m")) {
      controlMode = server.arg("m").toInt();
      if (controlMode == 1) initCPG(); // 只有 CPG 需要初始化
    }
    String modeName;
    if (controlMode == 0) modeName = "Sin";
    else if (controlMode == 1) modeName = "CPG";
    else if (controlMode == 2) modeName = "Offset";
    else modeName = "Unknown";
    server.send(200, "text/plain", modeName);
  });

  // ---- 回授開關 ----
  server.on("/toggleFeedback", []() {
    useFeedback = !useFeedback;
    server.send(200, "text/plain", String(useFeedback ? "開啟" : "關閉"));
  });

  // ---- 參數設定 ----
  server.on("/setFrequency", []() { if (server.hasArg("f")) frequency = server.arg("f").toFloat(); server.send(200, "text/plain", String(frequency)); });
  server.on("/setAmplitude", []() { if (server.hasArg("a")) Ajoint = server.arg("a").toFloat(); server.send(200, "text/plain", String(Ajoint)); });
  server.on("/setLambda",   []() { if (server.hasArg("lambda")) lambda = server.arg("lambda").toFloat(); server.send(200, "text/plain", String(lambda)); });
  server.on("/setL",        []() { if (server.hasArg("L")) L = server.arg("L").toFloat(); server.send(200, "text/plain", String(L)); });
  server.on("/setFeedbackGain", []() { if (server.hasArg("g")) feedbackGain = server.arg("g").toFloat(); server.send(200, "text/plain", String(feedbackGain)); });

  // ---- 狀態查詢 (回傳 JSON) ----
  server.on("/status", []() {
    String json = "{";
    json += "\"frequency\":" + String(frequency, 2) + ",";
    json += "\"amplitude\":" + String(Ajoint, 2) + ",";
    json += "\"lambda_input\":" + String(lambda * L, 2) + ",";
    json += "\"lambda\":" + String(lambda, 2) + ",";
    json += "\"L\":" + String(L, 2) + ",";
    String modeName;
    if (controlMode == 0) modeName = "Sin";
    else if (controlMode == 1) modeName = "CPG";
    else if (controlMode == 2) modeName = "Offset";
    else modeName = "Unknown";
    json += "\"mode\":\"" + modeName + "\",";
    json += "\"feedback\":\"" + String(useFeedback ? "開啟" : "關閉") + "\",";
    json += "\"fbGain\":" + String(feedbackGain, 2) + ",";
    // 既有 ADXL 三軸
    json += "\"adxl_x_g\":" + String(adxlX, 4) + ",";
    json += "\"adxl_y_g\":" + String(adxlY, 4) + ",";
    json += "\"adxl_z_g\":" + String(adxlZ, 4) + ",";

    // ★ 新增 Pitch / Roll（度）
    json += "\"pitch_deg\":" + String(pitchDeg, 2) + ",";
    json += "\"roll_deg\":"  + String(rollDeg, 2);

    json += ",";
    for (int i = 0; i < 4; i++) {
      json += "\"ads1_ch" + String(i) + "\":" + String(adsVoltage1[i], 4) + ",";
    }
    for (int i = 0; i < 4; i++) {
      json += "\"ads2_ch" + String(i) + "\":" + String(adsVoltage2[i], 4);
      if (i < 3) json += ",";
    }
    // ★ 新增運作時間（分鐘）
    json += ",\"uptime_min\":" + String(millis() / 1000.0 / 60.0, 3);

    json += "}";
    server.send(200, "application/json", json);

  });

  // ---- 簡易調整 API ----
  server.on("/increase_freq",   []() { frequency = fminf(frequency + 0.1f, 3.0f); server.send(200, "ok"); });
  server.on("/decrease_freq",   []() { frequency = fmaxf(frequency - 0.1f, 0.1f); server.send(200, "ok"); });
  server.on("/increase_ajoint", []() { Ajoint    = fminf(Ajoint + 5.0f, 90.0f);  server.send(200, "ok"); });
  server.on("/decrease_ajoint", []() { Ajoint    = fmaxf(Ajoint - 5.0f, 0.0f);   server.send(200, "ok"); });
  server.on("/increase_lambda", []() { lambda    = fminf(lambda + 0.05f, 2.0f);  server.send(200, "ok"); });
  server.on("/decrease_lambda", []() { lambda    = fmaxf(lambda - 0.05f, 0.1f);  server.send(200, "ok"); });
  server.on("/increase_L",      []() { L         = fminf(L + 0.05f, 2.0f);       server.send(200, "ok"); });
  server.on("/decrease_L",      []() { L         = fmaxf(L - 0.05f, 0.1f);       server.send(200, "ok"); });

  // ---- 暫停/重設 ----
  server.on("/toggle_pause", []() { isPaused = !isPaused; server.send(200, "ok"); });
  server.on("/reset_all", []() {
    frequency = 0.7f;
    Ajoint = 30.0f;
    adsMinValidVoltage = 0.6f;
    isPaused = false;
    if (controlMode == 1) initCPG();
    server.send(200, "ok");
  });

  // ---- CSV 下載 ----
  server.on("/download", []() {
    if (!SPIFFS.exists("/data.csv")) {
      server.send(404, "text/plain", "data.csv 不存在");
      return;
    }
    File f = SPIFFS.open("/data.csv", "r");
    server.streamFile(f, "text/csv");
    f.close();
  });

  // 啟動 WebServer
  server.begin();
}

// ================== SPIFFS / CSV ==================
void initLogFile() {
  if (!SPIFFS.begin(true)) {
    Serial.println("❌ SPIFFS 初始化失敗");
    return;
  }
  if (!SPIFFS.exists("/data.csv")) {
    File f = SPIFFS.open("/data.csv", FILE_WRITE);
    if (f) {
      f.println("time_min,ads1_A0,ads1_A1,ads1_A2,ads1_A3,ads2_A0,ads2_A1,ads2_A2,ads2_A3,ads1_diff01,ads1_diff23,ads1_diff03,adxl_x_g,adxl_y_g,adxl_z_g");
      f.close();
    }
  }
}
void logADSDataEveryMinute() {
  unsigned long now = millis();
  if (now - g_lastLogTime < 60000) return; // 每 1 分鐘
  g_lastLogTime = now;
  unsigned long t_min = now / 60000;
  File f = SPIFFS.open("/data.csv", FILE_APPEND);
  if (!f) return;
  f.printf("%lu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
           t_min,
           adsVoltage1[0], adsVoltage1[1], adsVoltage1[2], adsVoltage1[3],
           adsVoltage2[0], adsVoltage2[1], adsVoltage2[2], adsVoltage2[3],
           ads1Diff[0], ads1Diff[1], ads1Diff[2],
           adxlX, adxlY, adxlZ);
  f.close();
  Serial.printf("📄 CSV 已寫入: 第 %lu 分鐘\n", t_min);
}

// ================== ADS 讀取 ==================
bool safeReadADS(Adafruit_ADS1115 &ads, int channel, float &voltage) {
  int16_t raw = ads.readADC_SingleEnded(channel);
  float v = ads.computeVolts(raw);
  if (v < adsMinValidVoltage) v = 0.0f;
  voltage = v;
  return true;
}
void readADS() {
  for (int i = 0; i < 4; i++) {
    safeReadADS(ads1, i, adsVoltage1[i]);
    safeReadADS(ads2, i, adsVoltage2[i]);
  }
  // 差分（ADS1）
  ads1Diff[0] = ads1.computeVolts(ads1.readADC_Differential_0_1());
  ads1Diff[1] = ads1.computeVolts(ads1.readADC_Differential_2_3());
  ads1Diff[2] = ads1.computeVolts(ads1.readADC_Differential_0_3());
  for (int k = 0; k < 3; ++k) if (ads1Diff[k] < adsMinValidVoltage) ads1Diff[k] = 0.0f;
}

// ================== Servo Task ==================
void servoTask(void *pvParameters) {
  const float dt = 0.05f; // 50 ms
  for (;;) {
    if (!isPaused) {
      float t = millis() / 1000.0f;
      for (int j = 0; j < bodyNum; j++) {
        float outDeg = 0.0f;
        if (controlMode == 0) {
          // ---- Sin 模式 ----
          outDeg = Ajoint * sinf(j / (fmaxf(lambda * L, 1e-6f)) + 2.0f * PI * frequency * t);
          angleDeg[j] = servoDefaultAngles[j] + outDeg;
        } else if (controlMode == 1) {
          // ---- CPG 模式 ----
          float fb_phase = 0.0f;
          float fb_amp   = 0.0f;
          if (useFeedback && feedbackGain > 0.0f) {
            float desired_angle = servoDefaultAngles[j];
            float actual_angle  = getSensorAngle(j);
            fb_phase = feedbackGain * (desired_angle - actual_angle) / fmaxf(Ajoint, 1e-3f);
            fb_amp   = fb_phase;
          }
          updateCPG(t, dt, j, fb_phase, fb_amp);
          outDeg = getCPGOutput(j);
          angleDeg[j] = servoDefaultAngles[j] + outDeg;
        } else if (controlMode == 2) {
          // ---- Offset 模式：固定在預設角度 (120°) ----
          outDeg = 0.0f;
          angleDeg[j] = servoDefaultAngles[j];
        }
        // 輸出給伺服
        int target_pos = degreeToLX224(angleDeg[j]);
        moveLX224(j + 1, target_pos, 50); // 動作時間 50ms
      }
    }
    vTaskDelay(50 / portTICK_PERIOD_MS);
  }
}

// ================== I2C Task（ADS） ==================
void i2cTask(void *pvParameters) {
  unsigned long lastADS = 0;
  for (;;) {
    if (millis() - lastADS > 200) {
      lastADS = millis();
      readADS();
      logADSDataEveryMinute();
    }
    vTaskDelay(10 / portTICK_PERIOD_MS);
  }
}

// ================== ADXL Task（SPI） ==================
void adxlTask(void *pvParameters) {
  const float alpha = 0.2f; // 0~1：越大越貼新值（平滑係數）
  for (;;) {
    auto a = adxl355.getAccelerations(); // g
    adxlX = a.x; adxlY = a.y; adxlZ = a.z;

    float p_now, r_now;
    accelToEuler(adxlX, adxlY, adxlZ, p_now, r_now);

    // 簡單低通濾波，讓顯示更穩
    pitchDeg = alpha * p_now + (1.0f - alpha) * pitchDeg;
    rollDeg  = alpha * r_now + (1.0f - alpha) * rollDeg;

    vTaskDelay(20 / portTICK_PERIOD_MS); // 50 Hz
  }
}


// ================== WiFi ==================
void connectToWiFi() {
  WiFi.mode(WIFI_STA);

  // 固定 IP 設定
  IPAddress local_IP(192, 168, 0, 198);
  IPAddress gateway(192, 168, 0, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.config(local_IP, gateway, subnet);
  
  WiFi.begin(ssid1, password1);
  Serial.print("WiFi 連線中");
  for (int i = 0; i < 10 && WiFi.status() != WL_CONNECTED; ++i) {
    delay(300);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    connectedSSID = WiFi.SSID();
    setupWebServer();
    Serial.print("✅ WiFi 已連上，IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("❌ WiFi 連線失敗，嘗試連接第二組 WiFi...");
    WiFi.begin(ssid2, password2);
    for (int i = 0; i < 10 && WiFi.status() != WL_CONNECTED; ++i) {
      delay(300);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      connectedSSID = WiFi.SSID();
      setupWebServer();
      Serial.print("✅ 第二組 WiFi 已連上，IP: ");
      Serial.println(WiFi.localIP());
    } else {
      Serial.println("❌ 第二組 WiFi 也連線失敗（將不開啟 Web 介面）");
    }
  }
}


// ================== setup / loop ==================
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("✅ 原生 USB 已啟動");

  // 伺服 UART
  Serial1.begin(115200, SERIAL_8N1, -1, SERVO_TX_PIN); // TX: SERVO_TX_PIN

  // I2C
  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setTimeout(50);
  if (!ads1.begin(0x48, &Wire)) Serial.println("❌ 找不到 ADS1115 #1 (0x48)");
  else { ads1.setGain(GAIN_TWOTHIRDS); Serial.println("✅ ADS1115 #1 初始化完成"); }
  if (!ads2.begin(0x49, &Wire)) Serial.println("❌ 找不到 ADS1115 #2 (0x49)");
  else { ads2.setGain(GAIN_TWOTHIRDS); Serial.println("✅ ADS1115 #2 初始化完成"); }

  // ADXL355（SPI）
  initADXL();

  // 檔案系統 / WiFi / CPG 初始化
  initLogFile();
  connectToWiFi();
  initCPG();

  // 建立 Tasks
  xTaskCreatePinnedToCore(servoTask, "ServoTask", 4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(i2cTask,  "I2CTask",   4096, NULL, 1, NULL, 1);
  xTaskCreatePinnedToCore(adxlTask, "ADXLTask",  4096, NULL, 1, NULL, 1);
}

void loop() {
  server.handleClient();
}
