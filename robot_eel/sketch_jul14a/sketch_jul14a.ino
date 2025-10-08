  #include <WiFi.h>
  #include <WebServer.h>
  #include <ESP32Servo.h>
  #include <Wire.h>
  #include "MPU6050_6Axis_MotionApps20.h"
  #include <Update.h>
  #include <math.h>
  #include <Adafruit_ADS1X15.h>  // ✅ ADS1115

  // ✅ 建立 ADS1115 物件
  Adafruit_ADS1115 ads;
  float adsVoltage = 0.0;
  float adsAngle = 0.0;
  // WiFi 設定
  const char *ssid1 = "YTY_2.4g";
  const char *password1 = "weareytylab";
  // const char *ssid2 = "YTY_2.4g";
  // const char *password2 = "weareytylab";
  String connectedSSID = "未連接";

  // 伺服參數
  #define bodyNum 6
  Servo servos[bodyNum];
  int servoPins[bodyNum] = {2, 13, 12, 11, 10, 9};
  float servoDefaultAngles[bodyNum] = {100, 102, 95, 90, 90, 90};
  float angle[bodyNum];
  float Ajoint = 30, frequency = 0.7, LAMBDAinput = 0.4;
  bool isPaused = false;

  // WebServer
  WebServer server(80);

  // ✅ 垂直伺服馬達控制
  Servo verticalServo;
  int verticalServoPin = 18;
  float verticalAngle = 90.0;

  // I2C
  const int SDA_PIN = 3;
  const int SCL_PIN = 8;

  // 電池
  float batteryVoltage = 0.0;
  float batteryPercent = 0.0;

  // MPU6050 物件與相關變數
  MPU6050 mpu;
  uint8_t fifoBuffer[64];
  Quaternion q;
  VectorFloat gravity;
  float ypr[3];
  bool dmpReady = false;
  float ax = 0, ay = 0, yaw = 0;

  float ax_offset = 0, ay_offset = 0;
  unsigned long lastUpdate = 0;
  float moveThreshold = 0.4;
  bool isMoving = false;

  // IMU CSV 紀錄
  String imuCSVLog = "time,ax,ay,yaw\n";
  const int maxCSVLines = 1000;
  int currentCSVLines = 0;
  unsigned long lastCSVTime = 0;

  // 電池 CSV 紀錄
  String batteryCSVLog = "time,voltage,percent\n";
  const int maxBatteryCSVLines = 1000;
  int currentBatteryCSVLines = 0;
  unsigned long lastBatteryLogTime = 0;

  // HTML 主頁面
  const char INDEX_HTML[] PROGMEM = R"rawliteral(
  <!DOCTYPE html>
  <html>
  <head>
  <meta charset="UTF-8">
  <title>ESP32 robot eel</title>
  <style>
    body { font-family:sans-serif; text-align:center; background:#eee; padding:30px; }
    .section { background:#fff; margin:20px auto; padding:20px; border-radius:12px; width:300px; box-shadow:0 2px 10px rgba(0,0,0,0.1); }
    h2 { margin-bottom:10px; }
    button { padding:10px 20px; margin:5px; background:#4CAF50; color:#fff; border:none; border-radius:6px; cursor:pointer; }
    button:hover { background:#45a049; }
  </style>
  </head>
  <body>
    <h1>ESP32 robot eel控制面板</h1>

    <div class="section">
      <h2>頻率</h2>
      <div>數值: <span id="frequency-value">0.7</span> Hz</div>
      <button onclick="sendCommand('/increase_freq')">增加 +</button>
      <button onclick="sendCommand('/decrease_freq')">減少 -</button>
    </div>

    <div class="section">
      <h2>關節角度</h2>
      <div>數值: <span id="ajoint-value">30</span>°</div>
      <button onclick="sendCommand('/increase_ajoint')">增加 +</button>
      <button onclick="sendCommand('/decrease_ajoint')">減少 -</button>
    </div>

    <div class="section">
      <h2>Lambda</h2>
      <div>數值: <span id="lambda-value">0.40</span></div>
      <button onclick="sendCommand('/increase_lambda')">增加 +</button>
      <button onclick="sendCommand('/decrease_lambda')">減少 -</button>
    </div>

    <div class="section">
      <h2>垂直伺服馬達</h2>
      <div>角度: <span id="vertical-angle">90</span>°</div>
      <button onclick="sendCommand('/increase_vertical')">增加 +</button>
      <button onclick="sendCommand('/decrease_vertical')">減少 -</button>
    </div>


    <div class="section">
      <h2>電池狀態</h2>
      <div>電壓: <span id="battery-value">0.0</span> V</div>
      <div>電量: <span id="percent-value">0</span> %</div>
    </div>

    <div class="section">
      <h2>移動狀態</h2>
      <div>Yaw: <span id="yaw-value">0.0</span> °</div>
      <div>加速度 X: <span id="ax-value">0.0</span> m/s²</div>
      <div>加速度 Y: <span id="ay-value">0.0</span> m/s²</div>
      <div>移動中: <span id="moving-value">否</span></div>
      <div>門檻值: <span id="threshold-value">0.4</span> m/s²</div>
      <button onclick="sendCommand('/increase_threshold')">增加門檻 +</button>
      <button onclick="sendCommand('/decrease_threshold')">減少門檻 -</button>
    </div>

    <div class="section">
    <h2>ADS1115 狀態</h2>
    <div>電壓: <span id="ads-voltage">0.000</span> V</div>
    <div>角度: <span id="ads-angle">0.0</span>°</div>
    </div>


    <div class="section">
      <h2>系統控制</h2>
      <div>狀態: <span id="status-value">運行中</span></div>
      <div>已連接: <span id="ssid-value">未連接</span></div>
      <button onclick="sendCommand('/toggle_pause')">暫停/繼續</button>
      <button onclick="sendCommand('/reset_all')">重設</button>
    </div>

    <div class="section">
      <h2>即時狀態</h2>
      <div>時間: <span id="time-value">0.0</span> 秒</div>
      <div>馬達: <span id="motor-status">等待中...</span></div>
      <button onclick="downloadCSV()">下載 IMU CSV</button>
      <button onclick="downloadBatteryCSV()">下載 電池 CSV</button>
    </div>

  <script>
  function sendCommand(url) {
    fetch(url)
      .then(response => response.text())
      .then(data => {
        console.log('Command response:', data);
        updateStatus();
      })
      .catch(err => console.error('Error sending command:', err));
  }

  function updateStatus() {
    fetch('/status')
      .then(response => response.text())
      .then(text => {
        let parts = text.split(';');
        let map = {};
        parts.forEach(p => {
          let kv = p.split(':');
          if (kv.length == 2) map[kv[0].trim()] = kv[1].trim();
        });

        if(map['frequency']) document.getElementById('frequency-value').textContent = parseFloat(map['frequency']).toFixed(2);
        if(map['ajoint']) document.getElementById('ajoint-value').textContent = parseFloat(map['ajoint']).toFixed(1);
        if(map['lambda']) document.getElementById('lambda-value').textContent = parseFloat(map['lambda']).toFixed(2);
        if(map['vertical']) document.getElementById('vertical-angle').textContent = parseInt(map['vertical']);
        if(map['battery']) document.getElementById('battery-value').textContent = parseFloat(map['battery']).toFixed(2);
        if(map['percent']) document.getElementById('percent-value').textContent = parseFloat(map['percent']).toFixed(1);
        if(map['ax']) document.getElementById('ax-value').textContent = parseFloat(map['ax']).toFixed(2);
        if(map['ay']) document.getElementById('ay-value').textContent = parseFloat(map['ay']).toFixed(2);
        if(map['yaw']) document.getElementById('yaw-value').textContent = parseFloat(map['yaw']).toFixed(1);
        if(map['ads_voltage']) document.getElementById('ads-voltage').textContent = parseFloat(map['ads_voltage']).toFixed(3);
        if(map['ads_angle']) document.getElementById('ads-angle').textContent = parseFloat(map['ads_angle']).toFixed(1);
        if(map['threshold']) document.getElementById('threshold-value').textContent = parseFloat(map['threshold']).toFixed(2);
        if(map['moving']) document.getElementById('moving-value').textContent = (map['moving'] === '是' ? '是' : '否');
        if(map['status']) document.getElementById('status-value').textContent = map['status'];
        if(map['ssid']) document.getElementById('ssid-value').textContent = map['ssid'];
        if(map['time']) document.getElementById('time-value').textContent = parseFloat(map['time']).toFixed(1);
      })
      .catch(err => console.error('Error fetching status:', err));
  }

  function downloadCSV() {
    window.location.href = '/imu_log';
  }

  function downloadBatteryCSV() {
    window.location.href = '/battery_log';
  }

  const uploadForm = document.getElementById('uploadForm');
  if(uploadForm) {
    uploadForm.addEventListener('submit', function(e) {
      e.preventDefault();
      const formData = new FormData(uploadForm);
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/update', true);
      xhr.upload.onprogress = function(e) {
        if (e.lengthComputable) {
          const percent = Math.round((e.loaded / e.total) * 100);
          document.getElementById('progressText').innerText = '進度: ' + percent + '%';
        }
      };
      xhr.onload = function() {
        alert(xhr.responseText);
      };
      xhr.send(formData);
    });
  }

  setInterval(updateStatus, 1000);
  updateStatus();
  </script>

  </body>
  </html>
  )rawliteral";


  // OTA 韌體更新表單，會附加到主頁
  const char OTA_HTML_FORM[] PROGMEM = R"rawliteral(
  <div class="section">
    <h2>韌體更新 (OTA)</h2>
    <form id="uploadForm" method="POST" action="/update" enctype="multipart/form-data">
      <input type="file" name="update" required>
      <input type="submit" value="上傳韌體">
      <div id="progressText">進度: 0%</div>
    </form>
  </div>
  <script>
    const form = document.getElementById('uploadForm');
    form.addEventListener('submit', function (e) {
      e.preventDefault();
      const formData = new FormData(form);
      const xhr = new XMLHttpRequest();
      xhr.open('POST', '/update', true);
      xhr.upload.onprogress = function (e) {
        if (e.lengthComputable) {
          const percent = Math.round((e.loaded / e.total) * 100);
          document.getElementById('progressText').innerText = '進度: ' + percent + '%';
        }
      };
      xhr.onload = function () {
        alert(xhr.responseText);
      };
      xhr.send(formData);
    });
  </script>
  </body></html>
  )rawliteral";

  String getStatusString() {
    char buf[600];
    snprintf(buf, sizeof(buf),
      "frequency: %.1f;ajoint: %.1f;lambda: %.2f;status: %s;ssid: %s;time: %.1f;"
      "battery: %.2f;percent: %.1f;ax: %.2f;ay: %.2f;yaw: %.1f;"
      "threshold: %.2f;moving: %s;vertical: %.0f;ads_voltage: %.3f;ads_angle: %.1f",
      frequency, Ajoint, LAMBDAinput,
      isPaused ? "暫停" : "運行中",
      connectedSSID.c_str(),
      millis() / 1000.0,
      batteryVoltage, batteryPercent,
      ax, ay, yaw * 180 / PI,
      moveThreshold,
      isMoving ? "是" : "否",
      verticalAngle,
      adsVoltage, adsAngle  // ✅ 新增 ADS1115 資料
    );
    return String(buf);
  }

  // OTA Upload 處理函式
  void handleOTAUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      Serial.println("OTA Upload Start");
      Update.begin();
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      Update.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
      Serial.println("OTA Upload End");
      if (Update.end(true)) {
        Serial.println("OTA Success, restarting...");
      } else {
        Serial.println("OTA Failed");
      }
    }
  }

  void initializingServo() {
    for (int i = 0; i < bodyNum; i++) {
      servos[i].attach(servoPins[i]);
      servos[i].write(servoDefaultAngles[i]);
    }
    verticalServo.attach(verticalServoPin);
    verticalServo.write(verticalAngle);
    delay(1000);
  }

  void setupWebServer() {
    server.on("/", []() {
      String fullHTML = String(INDEX_HTML) + OTA_HTML_FORM;
      server.send(200, "text/html", fullHTML);
    });

    server.on("/increase_freq", []() { frequency = min(frequency + 0.1f, 3.0f); server.send(200, "text/plain", "ok"); });
    server.on("/decrease_freq", []() { frequency = max(frequency - 0.1f, 0.1f); server.send(200, "text/plain", "ok"); });
    server.on("/increase_ajoint", []() { Ajoint = min(Ajoint + 5.0f, 90.0f); server.send(200, "text/plain", "ok"); });
    server.on("/decrease_ajoint", []() { Ajoint = max(Ajoint - 5.0f, 0.0f); server.send(200, "text/plain", "ok"); });
    server.on("/increase_lambda", []() { LAMBDAinput = min(LAMBDAinput + 0.05f, 2.0f); server.send(200, "text/plain", "ok"); });
    server.on("/decrease_lambda", []() { LAMBDAinput = max(LAMBDAinput - 0.05f, 0.1f); server.send(200, "text/plain", "ok"); });
    server.on("/increase_vertical", []() { verticalAngle = min(verticalAngle + 5.0f, 180.0f); verticalServo.write(verticalAngle); server.send(200, "text/plain", "ok"); });

    server.on("/decrease_vertical", []() { verticalAngle = max(verticalAngle - 5.0f, 0.0f); verticalServo.write(verticalAngle); server.send(200, "text/plain", "ok"); });

    server.on("/increase_threshold", []() { moveThreshold = min(moveThreshold + 0.05f, 2.0f); server.send(200, "text/plain", "ok"); });
    server.on("/decrease_threshold", []() { moveThreshold = max(moveThreshold - 0.05f, 0.05f); server.send(200, "text/plain", "ok"); });
    server.on("/toggle_pause", []() { isPaused = !isPaused; server.send(200, "text/plain", "ok"); });
    server.on("/reset_all", []() {
      frequency = 0.7; Ajoint = 30; LAMBDAinput = 0.4; isPaused = false;
      moveThreshold = 0.4;
      server.send(200, "text/plain", "ok");
    });
    server.on("/status", []() {
      server.send(200, "text/plain", getStatusString());
    });
    server.on("/imu_log", []() {
      server.send(200, "text/csv", imuCSVLog);
    });
    server.on("/battery_log", []() {
      server.send(200, "text/csv", batteryCSVLog);
    });

    server.on("/update", HTTP_POST, []() {
      server.sendHeader("Connection", "close");
      server.send(200, "text/plain", Update.hasError() ? "更新失敗" : "更新成功，將自動重啟");
      delay(1000);
      ESP.restart();
    }, handleOTAUpload);

    server.onNotFound([]() { server.send(404, "text/plain", "404 Not Found"); });

    server.begin();
  }

  void connectToWiFi() {
    WiFi.mode(WIFI_STA);
    // WiFi.begin(ssid2, password2);
    // delay(5000);
    // if (WiFi.status() != WL_CONNECTED) {
      WiFi.begin(ssid1, password1);
      delay(5000);
    // }
    if (WiFi.status() == WL_CONNECTED) {
      connectedSSID = WiFi.SSID();
      setupWebServer();
    }
  }

  // ✅ 從 ADS1115 CH1 讀取電池電壓
  void readBattery() {
      int16_t adc1 = ads.readADC_SingleEnded(1);  // 讀取 CH1
      float v_in = ads.computeVolts(adc1);        // 換成實際電壓（單端）

      // ⚠️ 如果有分壓電阻，請修改這個比例
      batteryVoltage = v_in * 4.4;  

      // 計算百分比（請依你的電池規格調整範圍）
      batteryPercent = (batteryVoltage - 9.0) / (12.4 - 10.25) * 100.0;
      batteryPercent = constrain(batteryPercent, 0.0, 100.0);
  }


  // ✅ 讀取 ADS1115 並轉換角度
  void readADS1115() {
    int16_t adc0 = ads.readADC_SingleEnded(0);
    adsVoltage = ads.computeVolts(adc0);
    adsAngle = (3.1 - adsVoltage) / 0.1 * 10; // 3.1V=0°, 每10°降0.1V
    adsAngle = constrain(adsAngle, 0, 180);
  }

  void setup() {
    Serial.begin(115200);

    // ✅ 只初始化一次 I2C，MPU6050 和 ADS1115 共用
    Wire.begin(SDA_PIN, SCL_PIN);

    // 初始化 MPU6050
    mpu.initialize();
    if (mpu.dmpInitialize() == 0) {
      mpu.setDMPEnabled(true);
      dmpReady = true;
      Serial.println("MPU6050 初始化完成");
    } else {
      Serial.println("MPU6050 初始化失敗");
    }

    // 初始化 ADS1115
    if (!ads.begin(0x48, &Wire)) {
      Serial.println("無法找到 ADS1115，請檢查接線！");
    } else {
      ads.setGain(GAIN_ONE);
      Serial.println("ADS1115 初始化完成");
    }

    initializingServo();
    connectToWiFi();
    lastUpdate = millis();

    Serial.println("Connected to SSID: " + connectedSSID);
    Serial.println("IP address: " + WiFi.localIP().toString());

    // 校正 MPU6050 加速度偏移
    int32_t sum_ax = 0, sum_ay = 0;
    for (int i = 0; i < 100; i++) {
      int16_t ax_temp, ay_temp, az_temp;
      mpu.getAcceleration(&ax_temp, &ay_temp, &az_temp);
      sum_ax += ax_temp;
      sum_ay += ay_temp;
      delay(5);
    }
    ax_offset = (sum_ax / 100.0) / 16384.0 * 9.81;
    ay_offset = (sum_ay / 100.0) / 16384.0 * 9.81;
  }


void loop() {
    server.handleClient();
    if (!dmpReady) return;

    // 只有在 DMP 有新資料時才更新 IMU + ADS
    if (mpu.dmpGetCurrentFIFOPacket(fifoBuffer)) {
        unsigned long now = millis();
        float dt = (now - lastUpdate) / 1000.0;

        // ✅ 控制 IMU 與 ADS 更新頻率
        if (dt >= 0.001) {  
            lastUpdate = now;

            // ✅ 讀取 ADS1115 與 IMU 同步
            readADS1115();

            // 讀取 IMU 姿態
            mpu.dmpGetQuaternion(&q, fifoBuffer);
            mpu.dmpGetGravity(&gravity, &q);
            mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
            yaw = ypr[0];

            // 讀取加速度並扣除偏移
            int16_t ax_raw, ay_raw, az_raw;
            mpu.getAcceleration(&ax_raw, &ay_raw, &az_raw);
            ax = ax_raw / 16384.0 * 9.81 - ax_offset;
            ay = ay_raw / 16384.0 * 9.81 - ay_offset;

            // 判斷移動狀態
            isMoving = fabs(ax) > moveThreshold || fabs(ay) > moveThreshold;

            // 控制伺服馬達運動
            if (!isPaused) {
                float t = millis() / 1000.0;
                for (int j = 0; j < bodyNum; j++) {
                    angle[j] = Ajoint * sin(j / LAMBDAinput + 2 * PI * frequency * t);
                    float target = servoDefaultAngles[j] + angle[j];
                    target = constrain(target, 0, 180);
                    servos[j].write(target);
                }
            }
            verticalServo.write(verticalAngle);

            // IMU CSV 紀錄（每 10ms）
            if (currentCSVLines < maxCSVLines && now - lastCSVTime >= 10) {
                lastCSVTime = now;
                imuCSVLog += String(now / 1000.0, 2) + "," +
                            String(ax, 3) + "," +
                            String(ay, 3) + "," +
                            String(yaw * 180 / PI, 2) + "\n";
                currentCSVLines++;
            }

            // 電池紀錄（每 60 秒）
            if (now - lastBatteryLogTime >= 60000) {
                lastBatteryLogTime = now;
                if (currentBatteryCSVLines >= maxBatteryCSVLines) {
                    batteryCSVLog = "time,voltage,percent\n";
                    currentBatteryCSVLines = 0;
                }
                batteryCSVLog += String(now / 1000.0, 2) + "," +
                                String(batteryVoltage, 2) + "," +
                                String(batteryPercent, 1) + "\n";
                currentBatteryCSVLines++;
            }

            // 電池即時讀取（每 5 秒）
            static unsigned long lastBatteryRead = 0;
            if (millis() - lastBatteryRead > 5000) {
                readBattery();
                lastBatteryRead = millis();
            }
        }
    }

    delay(10);
}

