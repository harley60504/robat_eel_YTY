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

h2 {
  margin: 0;
  padding: 18px;
  color: #fff;
  font-size: 26px;
  text-align: center;
  background: linear-gradient(90deg,#007bff,#0056b3);
  box-shadow: 0 2px 6px rgba(0,0,0,0.2);
}

.container {
  display: grid;
  grid-template-columns: repeat(auto-fill, minmax(320px, 1fr));
  gap: 16px;

  padding: 16px 24px 16px;  /* ⬅ 上 16、左右 24、下 16 */
  max-width: 1100px;
  margin: 0 auto;

  justify-content: center;
}


.flex-wrap {
  display: flex;
  flex-wrap: wrap;
  justify-content: center;   /* 每一排從中間開始排 */
  gap: 16px;
}

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

.card h3 {
  margin-top: 0;
  margin-bottom: 10px;
  font-size: 22px;
  border-left: 6px solid #007bff;
  padding-left: 10px;
}

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

.sensor-table td { padding: 3px 6px; }

.cam-control {
  display: grid;
  grid-template-columns: 1fr;
  gap: 8px;
  margin-top: 6px;
}
.cam-control label { display: block; }

@media (max-width: 480px){
  .card { width: 90%; }
}

/* ---------------- WiFi UI 美化 ---------------- */
.wifi-item {
  padding: 8px 0;
  border-bottom: 1px solid #ddd;
  display: flex;
  align-items: center;
  justify-content: space-between;
}

.wifi-left {
  display: flex;
  align-items: center;
  gap: 8px;
}

.wifi-btn {
  padding: 4px 10px;
  margin-left: 6px;
  border-radius: 6px;
  font-size: 14px;
}

.wifi-btn-edit { background:#28a745; }
.wifi-btn-edit:hover { background:#1d7d33; }

.wifi-btn-del { background:#dc3545; }
.wifi-btn-del:hover { background:#b02a37; }

.wifi-btn-go { background:#007bff; }
.wifi-btn-go:hover { background:#0059c4; }

/* 彈窗 */
#popupBg {
  display:none;
  position:fixed; top:0; left:0; width:100%; height:100%;
  background:rgba(0,0,0,0.5); 
  backdrop-filter: blur(3px);
}

#wifiPopup {
  position:absolute; 
  top:40%; 
  left:50%; 
  transform:translate(-50%,-50%);
  background:white; 
  padding:20px 25px;
  border-radius:15px; 
  width:260px;
  box-shadow:0 0 18px rgba(0,0,0,0.25);
}

.param-row {
  display: flex;
  align-items: center;
  margin-bottom: 8px;
  gap: 8px;
}

.param-row label {
  width: 90px;
  font-weight: 600;
}

.param-row input {
  flex: 1;
}

.param-row button {
  white-space: nowrap;
}

/* ---------------- 相機主卡片（自動寬度）---------------- */
.cam-big {
  grid-column: 1 / -1;
  width: 100%;
  max-width: calc(320px * 3 + 32px);
  margin-left: auto;
  margin-right: auto;

  padding-left: 12px;  /* ⬅ Camera 內側邊界 */
  padding-right: 12px;
  box-sizing: border-box;
}

/* ---------------- 相機內容（固定三張卡片寬）--------------- */
.cam-inner {
  display: flex;
  gap: 16px;
  max-width: calc(320px * 3 + 32px);   /* 三卡片 + gap */
  margin: 0 auto;                      /* 中置 */
}

.cam-left {
  flex: 3;
}

.cam-right {
  flex: 1;
}

.cam-left,
.cam-right {
  flex-shrink: 0;   /* 避免被壓扁 */
}

.cam-left img {
  width: 100%;
  border-radius: 10px;
}
/* ---------------- Servo Error Table ---------------- */
.servo-table {
  width: 100%;
  border-collapse: collapse;
  font-size: 14px;
}

.servo-table th {
  background: #f1f4f8;
  padding: 6px;
  font-weight: 600;
  border-bottom: 2px solid #007bff;
  text-align: center;
}

.servo-table td {
  padding: 5px 6px;
  text-align: center;
  border-bottom: 1px solid #ddd;
}

.servo-ok   { color: #28a745; font-weight: 600; }
.servo-warn { color: #ffc107; font-weight: 600; }
.servo-bad  { color: #dc3545; font-weight: 700; }

</style>
</head>

<body>

<h2>🐍 ESP32 LX-224 控制面板</h2>

<div class="container">

  <!-- 📷 相機畫面 + 控制 -->
  <div class="card cam-big">
    <div class="cam-inner">
      <div class="cam-left">
        <h3>📷 相機畫面</h3>
        <img src="/cam">
      </div>

      <div class="cam-right">
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
        </div>
      </div>
    </div>
  </div>
</div>

<div class="flex-wrap">
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

  <!-- ⚙️ 參數設定 -->
  <div class="card">
    <h3>⚙️ 參數設定</h3>

    <div class="param-row">
      <label>頻率 (Hz):</label>
      <input type="number" step="0.1" id="freqInput">
      <button onclick="setFrequency()">設定</button>
    </div>

    <div class="param-row">
      <label>振幅 (°):</label>
      <input type="number" step="1" id="ampInput">
      <button onclick="setAmplitude()">設定</button>
    </div>

    <div class="param-row">
      <label>λ:</label>
      <input type="number" step="0.05" id="lambdaInput">
      <button onclick="setLambda()">設定</button>
    </div>

    <div class="param-row">
      <label>L:</label>
      <input type="number" step="0.05" id="Linput">
      <button onclick="setL()">設定</button>
    </div>

    <div class="param-row">
      <label>回授權重:</label>
      <input type="range" id="fbGain" min="0" max="1" step="0.1"
            oninput="document.getElementById('fbVal').innerText=this.value">
      <span id="fbVal" style="margin-left:8px;">1.0</span>
      <button onclick="setFeedbackGain()">設定</button>
    </div>
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
  <!-- 🦾 Servo 角度誤差 -->
  <div class="card">
    <h3>🦾 Servo 角度誤差</h3>

    <table class="servo-table">
      <thead>
        <tr>
          <th>ID</th>
          <th>Target (°)</th>
          <th>Actual (°)</th>
          <th>Error (°)</th>
        </tr>
      </thead>
      <tbody id="servoTable">
        <tr><td colspan="4">讀取中…</td></tr>
      </tbody>
    </table>
  </div>


  <!-- 🌐 WiFi 設定 -->
  <div class="card">
    <h3>🌐 WiFi 設定</h3>

    <button onclick="scanWiFi()" id="scanBtn">🔍 掃描附近 WiFi</button>
    <h4>📡 目前連線：</h4>
    <div id="wifiCurrent">讀取中...</div>
    <br>
    <h4 style="cursor:pointer;" onclick="toggleScanMenu()">📶 附近 WiFi（展開/收合）</h4>
      <div id="scanMenu" style="display:none;">
        <ul id="wifiScanList" style="list-style:none; padding-left:0;"></ul>
      </div>

    <h4 style="cursor:pointer;" onclick="toggleSavedMenu()">📁 已儲存 WiFi（展開/收合）</h4>
      <div id="savedMenu" style="display:none;">
        <ul id="wifiSavedList" style="list-style:none; padding-left:0;"></ul>
      </div>

    <div id="popupBg">
      <div id="wifiPopup">
        <h3 id="popupTitle">WiFi 操作</h3>
        <p>密碼：<input type="password" id="popupPass" style="width:100%;"></p>
        <button onclick="confirmConnectWiFi()" style="width:100%; margin-top:6px;">▶️ 連線</button>
        <button onclick="saveNewPassword()" id="editBtn" style="width:100%; margin-top:6px; background:#28a745; display:none;">✏️ 修改密碼</button>
        <button onclick="closePopup()" style="width:100%; margin-top:6px;">❌ 取消</button>
      </div>
    </div>

    <p id="wifiStatus">狀態：-</p>
  </div>

  <!-- 🕒 控制 -->
  <div class="card">
    <h3>🕒 系統控制</h3>
    <p>運作時間：<span id="uptime">-</span></p>
    <button onclick="togglePause()">⏸ 暫停 / ▶️ 繼續</button>
    <button onclick="downloadCSV()">📥 下載 CSV</button>
  </div>

</div> 

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
    uptime.innerText = `${j.uptime_min}:${j.uptime_sec.toString().padStart(2,'0')}`;
  });
}

let selectedSSID="";
let editMode=false;

function wifiBars(rssi){
  if (rssi >= -50) return "📶📶📶📶";
  if (rssi >= -60) return "📶📶📶";
  if (rssi >= -70) return "📶📶";
  return "📶";
}

function scanWiFi(){
  document.getElementById("scanMenu").style.display = "block";

  wifiScanList.innerHTML = "<li>⏳ 掃描中...</li>";
  scanBtn.innerText = "⏳ 掃描中…";

  Promise.all([
    fetch('/wifi_current').then(r=>r.json()),
    fetch('/wifi_scan').then(r=>r.json())
  ])
  .then(([cur,scan])=>{
    let html="";
    scan.wifi.sort((a,b)=>b.rssi-a.rssi);

    scan.wifi.forEach(w=>{
      if(cur.connected && w.ssid===cur.ssid) return;

      html += `
        <li class="wifi-item">
          <div class="wifi-left">${wifiBars(w.rssi)} ${w.ssid || "(隱藏網路)"}</div>
          <button class="wifi-btn wifi-btn-go" onclick="showPopup('${w.ssid}',false)">➡️</button>
        </li>
      `;
    });

    wifiScanList.innerHTML = html;
    scanBtn.innerText = "🔍 重新掃描";
  });
}

function loadSavedWiFi(){
  fetch('/wifi_saved')
    .then(r=>r.json())
    .then(j=>{
      let html = "";

      j.saved.forEach(w=>{
        html += `
        <li class="wifi-item">
          <div class="wifi-left">⭐ ${w.ssid}</div>
          <div>
            <button class="wifi-btn wifi-btn-go" onclick="quickReconnect('${w.ssid}')">重連</button>
            <button class="wifi-btn wifi-btn-edit" onclick="showPopup('${w.ssid}',true)">✏️</button>
            <button class="wifi-btn wifi-btn-del" onclick="forgetWiFi('${w.ssid}')">🗑</button>
          </div>
        </li>`;
      });

      wifiSavedList.innerHTML = html;
    });
}

function quickReconnect(ssid){
  wifiStatus.innerText="⏳ 嘗試快速重連…";

  fetch(`/wifi_reconnect?ssid=${encodeURIComponent(ssid)}`)
    .then(r=>r.text())
    .then(t=>{ wifiStatus.innerText=t; });
}

function showPopup(ssid,isEdit){
  selectedSSID=ssid;
  editMode=isEdit;

  popupTitle.innerText = isEdit ? `修改密碼：${ssid}` : `連線到：${ssid}`;
  popupPass.value="";
  document.getElementById("editBtn").style.display = isEdit ? "block" : "none";

  popupBg.style.display="block";
}

function closePopup(){
  popupBg.style.display="none";
}

function confirmConnectWiFi(){
  const pass = popupPass.value.trim();
  wifiStatus.innerText = "⏳ 嘗試連線中…";

  fetch(`/wifi_connect?ssid=${encodeURIComponent(selectedSSID)}&pass=${encodeURIComponent(pass)}`)
    .then(r=>r.text())
    .then(t=>{
      wifiStatus.innerText=t;
      loadSavedWiFi();
    });

  closePopup();
}

function saveNewPassword(){
  const pass = popupPass.value.trim();
  wifiStatus.innerText="⏳ 儲存密碼…";

  fetch(`/wifi_edit_pass?ssid=${encodeURIComponent(selectedSSID)}&pass=${encodeURIComponent(pass)}`)
    .then(r=>r.text())
    .then(t=>{
      wifiStatus.innerText=t;
      loadSavedWiFi();
    });

  closePopup();
}

function forgetWiFi(ssid){
  fetch(`/wifi_forget?ssid=${encodeURIComponent(ssid)}`)
    .then(r=>r.text())
    .then(t=>{
      wifiStatus.innerText=t;
      loadSavedWiFi();
    });
}

function loadCurrentWiFi(){
  fetch('/wifi_current')
    .then(r=>r.json())
    .then(j=>{
      if (!j.connected){
        wifiCurrent.innerText = "尚未連線任何 WiFi";
        return;
      }

      wifiCurrent.innerHTML = `
        ⭐ SSID：${j.ssid}<br>
        📶 訊號：${wifiBars(j.rssi)}<br>
        🌐 IP：${j.ip}
      `;
    });
}

function toggleSavedMenu(){
  const menu = document.getElementById("savedMenu");
  menu.style.display = (menu.style.display === "none") ? "block" : "none";
}

function toggleScanMenu(){
  const menu = document.getElementById("scanMenu");
  menu.style.display = (menu.style.display === "none") ? "block" : "none";
}

async function updateServoTable() {
  try {
    const r = await fetch('/servo_status');
    const j = await r.json();
    const tb = document.getElementById('servoTable');
    tb.innerHTML = '';

    j.servos.forEach(s => {
      const err = Math.abs(s.errorDeg);
      let cls = 'servo-ok';
      if (err > 5) cls = 'servo-bad';
      else if (err > 2) cls = 'servo-warn';

      const tr = document.createElement('tr');
      tr.innerHTML = `
        <td>${s.id}</td>
        <td>${s.targetDeg.toFixed(1)}</td>
        <td>${s.actualDeg.toFixed(1)}</td>
        <td class="${cls}">${s.errorDeg.toFixed(2)}</td>
      `;
      tb.appendChild(tr);
    });
  } catch (e) {
    console.error(e);
  }
}

setTimeout(()=>{
  loadCurrentWiFi();
  loadSavedWiFi();
  scanWiFi();
},200);


setInterval(updateServoTable, 500);
setInterval(refreshStatus,1000);
</script>

</body>
</html>
)rawliteral";

