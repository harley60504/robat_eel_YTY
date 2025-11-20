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
  background: #eef1f5;
  margin: 0;
}

/* ===== 🔹 全域標題 ===== */
h2 {
  margin: 0;
  padding: 18px;
  color: #fff;
  font-size: 26px;
  text-align: center;
  background: linear-gradient(90deg,#007bff,#0056b3);
  box-shadow: 0 2px 6px rgba(0,0,0,0.2);
}

/* ===== 🧱 卡片群組 ===== */
.container {
  display: flex;
  flex-wrap: wrap;
  justify-content: center;
  padding: 12px;
  gap: 16px;
}

/* ===== 📦 卡片 ===== */
.card {
  width: 320px;
  background: white;
  border-radius: 14px;
  padding: 14px 18px;
  box-shadow: 0 3px 10px rgba(0,0,0,0.12);
  border-top: 5px solid #007bff;
  transition: 0.25s;
}
.card:hover { transform: translateY(-4px); }

/* ===== 🏷 卡片內部標題 ===== */
.card h3 {
  margin-top: 0;
  margin-bottom: 10px;
  font-size: 22px;
  border-left: 6px solid #007bff;
  padding-left: 10px;
}

/* ===== 🎛 按鈕與輸入 ===== */
button, input, select {
  font-size: 16px;
  padding: 6px 10px;
  border-radius: 8px;
  border: 1px solid #ccc;
  margin: 3px 0;
}

button {
  border: none;
  background: #007bff;
  color: white;
  cursor: pointer;
  font-weight: 600;
  box-shadow: 0 2px 6px rgba(0,0,0,0.18);
}
button:hover { background: #0059c4; }

/* ===== 🟦 感測 & 相機 ===== */
.sensor-table td { padding: 3px 6px; }

/* ===== 🎥 相機控制區 ===== */
.cam-control {
  display: grid;
  grid-template-columns: 1fr;
  gap: 8px;
  margin-top: 6px;
}
.cam-control label { display: block; }

/* ===== 📱 手機優化 ===== */
@media (max-width: 480px){
  .card { width: 90%; }
}
</style>
</head>


<body>
<h2>🐍 ESP32 LX-224 控制面板</h2>

<div class="container">

<!-- 🧭 模式切換 -->
<div class="card">
  <h3>🧭 模式切換</h3>
  <button onclick="setMode(0)">Sin 模式</button>
  <button onclick="setMode(1)">CPG 模式</button>
  <button onclick="setMode(2)">Offset 模式</button>
  <p>目前模式：<span id="mode">-</span></p>

  <button onclick="toggleFeedback()">切換回授</button>
  <p>回授狀態：<span id="feedback">-</span></p>
</div>

<!-- ⚙️ 控制參數 -->
<div class="card">
  <h3>⚙️ 參數設定</h3>

  <label>頻率 (Hz):
    <input type="number" step="0.1" id="freqInput">
  </label><button onclick="setFrequency()">設定</button>

  <label>振幅 (°):
    <input type="number" step="1" id="ampInput">
  </label><button onclick="setAmplitude()">設定</button>

  <label>λ:
    <input type="number" step="0.05" id="lambdaInput">
  </label><button onclick="setLambda()">設定</button>

  <label>L:
    <input type="number" step="0.05" id="Linput">
  </label><button onclick="setL()">設定</button>

  <label>回授權重:
    <input type="range" id="fbGain" min="0" max="1" step="0.1"
    oninput="document.getElementById('fbVal').innerText=this.value">
  </label>
  <span id="fbVal">1.0</span>
  <button onclick="setFeedbackGain()">設定</button>
</div>

<!-- 📡 狀態監控 -->
<div class="card" id="status">
  <h3>📡 系統狀態</h3>
  <p>頻率：<span id="freq">-</span> Hz</p>
  <p>振幅：<span id="amp">-</span>°</p>
  <p>λ：<span id="lambda">-</span></p>
  <p>L：<span id="L">-</span></p>
  <p>回授權重：<span id="fbGainStatus">-</span></p>
</div>

<!-- 📈 ADXL355 -->
<div class="card">
  <h3>📈 ADXL355 加速度</h3>
  <table class="sensor-table">
    <tr><td>X:</td><td><span id="ax">-</span> g</td></tr>
    <tr><td>Y:</td><td><span id="ay">-</span> g</td></tr>
    <tr><td>Z:</td><td><span id="az">-</span> g</td></tr>
    <tr><td>Pitch:</td><td><span id="pitch">-</span>°</td></tr>
    <tr><td>Roll:</td><td><span id="roll">-</span>°</td></tr>
  </table>
</div>

<!-- 🔌 ADS1115 -->
<div class="card">
  <h3>🔌 ADS1115 電壓</h3>
  <table class="sensor-table">
    <tr><td>A1-0:</td><td><span id="ads1_0">-</span></td></tr>
    <tr><td>A1-1:</td><td><span id="ads1_1">-</span></td></tr>
    <tr><td>A1-2:</td><td><span id="ads1_2">-</span></td></tr>
    <tr><td>A1-3:</td><td><span id="ads1_3">-</span></td></tr>
    <tr><td>A2-0:</td><td><span id="ads2_0">-</span></td></tr>
    <tr><td>A2-1:</td><td><span id="ads2_1">-</span></td></tr>
    <tr><td>A2-2:</td><td><span id="ads2_2">-</span></td></tr>
    <tr><td>A2-3:</td><td><span id="ads2_3">-</span></td></tr>
  </table>
</div>

<!-- 📷 相機畫面 -->
<div class="card">
  <h3>📷 XIAO ESP32S3 相機</h3>
  <img src="/cam" style="width:100%;border-radius:10px;">
</div>

<!-- 🎛 相機參數調整 -->
<div class="card">
  <h3>🎛 相機控制</h3>

  <div class="cam-control">
    <label>解析度：
      <select onchange="sendCam('framesize',this.value)">
        <option value="10">UXGA</option>
        <option value="9">SXGA</option>
        <option value="8" selected>SVGA</option>
        <option value="6">VGA</option>
        <option value="5">CIF</option>
        <option value="3">QVGA</option>
      </select>
    </label>

    <label>畫質：
      <input type="range" min="4" max="63" value="10"
      oninput="sendCam('quality', this.value)">
    </label>

    <label>亮度：
      <input type="range" min="-2" max="2" value="0"
      oninput="sendCam('brightness', this.value)">
    </label>

    <label>對比：
      <input type="range" min="-2" max="2" value="0"
      oninput="sendCam('contrast', this.value)">
    </label>

    <label>飽和：
      <input type="range" min="-2" max="2" value="0"
      oninput="sendCam('saturation', this.value)">
    </label>

    <button onclick="sendCam('aec',1)">🌞 自動曝光</button>
    <button onclick="sendCam('aec',0)">🌑 關閉 AE</button>
    <button onclick="sendCam('awb',1)">🎨 自動白平衡</button>
    <button onclick="sendCam('awb',0)">❌ 關閉 AWB</button>
  </div>
</div>

<!-- 🕒 系統控制 -->
<div class="card">
  <h3>🕒 系統控制</h3>
  <p>運作時間：<span id="uptime">00:00</span></p>
  <button onclick="togglePause()">⏸ 暫停 / ▶️ 繼續</button>
  <button onclick="downloadCSV()">📥 下載 CSV</button>
</div>

</div> <!-- container END -->

<script>
function sendCam(v,val){ fetch(`/cam_control?var=${v}&val=${val}`); }
function setMode(m){ fetch('/setMode?m='+m).then(r=>r.text()).then(t=>mode.innerText=t); }
function toggleFeedback(){ fetch('/toggleFeedback').then(r=>r.text()).then(t=>feedback.innerText=t); }
function setFrequency(){ fetch('/setFrequency?f='+freqInput.value); }
function setAmplitude(){ fetch('/setAmplitude?a='+ampInput.value); }
function setLambda(){ fetch('/setLambda?lambda='+lambdaInput.value); }
function setL(){ fetch('/setL?L='+Linput.value); }
function setFeedbackGain(){ fetch('/setFeedbackGain?g='+fbGain.value); }
function togglePause(){ fetch('/toggle_pause'); }
function downloadCSV(){ location.href='/download'; }

function refreshStatus(){
  fetch('/status').then(r=>r.json()).then(j=>{
    freq.innerText=j.frequency.toFixed(2);
    amp.innerText=j.amplitude.toFixed(1);
    lambda.innerText=j.lambda.toFixed(2);
    L.innerText=j.L.toFixed(2);
    fbGainStatus.innerText=j.fbGain.toFixed(2);
    ax.innerText=j.adxl_x_g.toFixed(3);
    ay.innerText=j.adxl_y_g.toFixed(3);
    az.innerText=j.adxl_z_g.toFixed(3);
    pitch.innerText=j.pitch_deg.toFixed(2);
    roll.innerText=j.roll_deg.toFixed(2);
    for(let i=0;i<4;i++) eval(`ads1_${i}.innerText=j.ads1_ch${i}.toFixed(3)`);
    for(let i=0;i<4;i++) eval(`ads2_${i}.innerText=j.ads2_ch${i}.toFixed(3)`);
    uptime.innerText = j.uptime_min.toFixed(1)+" min";
  });
}
setInterval(refreshStatus,1000);
</script>

</body>
</html>
)rawliteral";
