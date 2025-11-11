#pragma once
#include <Arduino.h>

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
    <!-- 模式切換 -->
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
      <img src="/cam" style="width:100%;border-radius:10px;box-shadow:0 0 10px rgba(0,0,0,0.4);">
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
