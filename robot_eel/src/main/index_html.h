#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-TW">
<head>
<meta charset="UTF-8">
<title>ESP32 LX-224 控制面板</title>
<style>
  body { font-family:"Noto Sans TC",Arial,sans-serif; background:#f5f5f5; margin:0; padding:0; }
  h2 { background:#007bff; color:#fff; padding:12px; margin:0; }
  .container { display:flex; flex-wrap:wrap; justify-content:center; padding:10px; }
  .card { background:#fff; box-shadow:0 2px 6px rgba(0,0,0,.2); border-radius:12px; padding:15px; margin:10px; width:300px; transition:.3s; }
  .card:hover { transform:translateY(-3px); }
  button,input,label,select { margin:5px; padding:6px; font-size:15px; }
  button { background:#007bff; color:#fff; border:none; border-radius:6px; cursor:pointer; }
  button:hover { background:#0056b3; }
  .sensor-table { text-align:left; width:100%; }
  .sensor-table td { padding:2px 6px; }
  .row { display:flex; align-items:center; gap:6px; flex-wrap:wrap; }
  .row label { min-width:86px; }
  .pill { display:inline-block; padding:3px 8px; border:1px solid #ddd; border-radius:999px; font-size:12px; color:#555; }
  img.stream { width:100%; border-radius:10px; box-shadow:0 0 10px rgba(0,0,0,.4); background:#111; }
  #cam_stream_card { width:720px; max-width:96%; transition:all .3s; }
  #fullscreenSnapBtn {
    position:fixed; bottom:20px; right:20px;
    background:rgba(0,0,0,0.6); color:#fff; padding:12px 18px;
    border-radius:50px; border:none; cursor:pointer;
    font-size:16px; box-shadow:0 3px 8px rgba(0,0,0,.4);
    display:none; transition:opacity .3s;
  }
</style>
</head>

<body>
  <h2>🐍 ESP32 LX-224 控制面板</h2>

  <div class="container">

    <!-- 📺 相機串流 -->
    <div class="card" id="cam_stream_card">
      <h3>📺 相機串流畫面</h3>
      <img id="cam_img" class="stream" alt="MJPEG stream">
      <div class="row" style="margin-top:6px;">
        <span class="pill">解析度:<span id="st_framesize">-</span></span>
        <span class="pill">品質:<span id="st_quality">-</span></span>
        <span class="pill">鏡像:<span id="st_hmirror">-</span></span>
        <span class="pill">翻轉:<span id="st_vflip">-</span></span>
        <a class="pill" href="/cam_status" target="_blank">📜 狀態 JSON</a>
      </div>
    </div>

    <!-- 🎛️ 相機控制 -->
    <div class="card">
      <h3>🎛️ 相機控制設定</h3>
      <div class="row">
        <label>解析度</label>
        <select id="cam_framesize" onchange="updateResLabel()">
          <option>QQVGA</option><option>QVGA</option><option selected>VGA</option>
          <option>SVGA</option><option>XGA</option><option>SXGA</option>
          <option>UXGA</option><option>HD</option><option>FHD</option>
        </select>
      </div>
      <p id="res_label">目前解析度：VGA (640×480)</p>

      <div class="row">
        <label>JPEG 品質</label>
        <input type="range" id="cam_quality" min="10" max="63" value="10" oninput="cam_qv.value=this.value">
        <input type="number" id="cam_qv" min="10" max="63" value="10" style="width:70px" oninput="cam_quality.value=this.value">
      </div>

      <div class="row">
        <label><input type="checkbox" id="cam_hmirror"> 水平鏡像</label>
        <label><input type="checkbox" id="cam_vflip" checked> 垂直翻轉</label>
      </div>

      <div class="row">
        <button onclick="camApply()">套用參數</button>
        <button onclick="camStart()">▶️ 開始串流</button>
        <button onclick="camStop()">⏹ 停止串流</button>
        <button onclick="camSnap()">📷 快照</button>
        <button onclick="toggleFullscreen()">🖥️ 全螢幕</button>
        <span class="pill">狀態：<span id="cam_state">idle</span></span>
      </div>
    </div>

    <!-- 🧭 模式切換 -->
    <div class="card">
      <h3>🧭 模式切換</h3>
      <button onclick="setMode(0)">Sin 模式</button>
      <button onclick="setMode(1)">CPG 模式</button>
      <button onclick="setMode(2)">Offset 模式</button><br>
      <p>目前模式：<span id="mode">-</span></p>
      <button onclick="toggleFeedback()">切換回授</button>
      <p>回授狀態：<span id="feedback">-</span></p>
    </div>

    <!-- ⚙️ 參數設定 -->
    <div class="card">
      <h3>⚙️ 參數設定</h3>
      <label>頻率 (Hz):</label>
      <input type="number" id="freqInput" step="0.1" value="0.7"><button onclick="setFrequency()">設定</button><br>
      <label>振幅 (°):</label>
      <input type="number" id="ampInput" step="1" value="20"><button onclick="setAmplitude()">設定</button><br>
      <label>λ (lambda):</label>
      <input type="number" id="lambdaInput" step="0.05" value="0.7"><button onclick="setLambda()">設定</button><br>
      <label>L:</label>
      <input type="number" id="Linput" step="0.05" value="0.85"><button onclick="setL()">設定</button><br>
      <label>回授權重:</label>
      <input type="range" id="fbGain" min="0" max="1" step="0.1" value="1" oninput="document.getElementById('fbVal').innerText=this.value">
      <span id="fbVal">1.0</span><button onclick="setFeedbackGain()">設定</button>
    </div>

    <!-- 📡 系統狀態 -->
    <div class="card" id="status">
      <h3>📡 系統狀態</h3>
      <p>頻率：<span id="freq">-</span> Hz</p>
      <p>振幅：<span id="amp">-</span> °</p>
      <p>λ：<span id="lambda">-</span></p>
      <p>L：<span id="L">-</span></p>
      <p>回授權重：<span id="fbGainStatus">-</span></p>
    </div>

    <!-- 📈 ADXL355 -->
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

    <!-- 🔌 ADS1115 -->
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

    <!-- 🕒 系統控制 -->
    <div class="card">
      <h3>🕒 系統控制</h3>
      <p>運作時間：<span id="uptime">0:00</span></p>
      <button onclick="togglePause()">⏸ 暫停 / ▶️ 繼續</button>
      <button onclick="downloadCSV()">📥 下載 CSV</button>
    </div>
  </div>

  <button id="fullscreenSnapBtn" onclick="exitFullscreen()">❌ 退出全螢幕</button>

  <script>
    const camImg=document.getElementById('cam_img');
    const stateEl=document.getElementById('cam_state');
    const fsBtn=document.getElementById('fullscreenSnapBtn');
    const resLabel=document.getElementById('res_label');
    const resMap={QQVGA:"160×120",QVGA:"320×240",VGA:"640×480",SVGA:"800×600",XGA:"1024×768",SXGA:"1280×1024",UXGA:"1600×1200",HD:"1280×720",FHD:"1920×1080"};

    function updateResLabel(){
      const v=document.getElementById('cam_framesize').value;
      resLabel.textContent=`目前解析度：${v} (${resMap[v]||"-"})`;
    }

    function camSet(v,val){return fetch('/cam_control?var='+v+'&val='+val).then(r=>r.text());}
    function camApply(){
      const fs=document.getElementById('cam_framesize').value;
      const q=document.getElementById('cam_quality').value;
      const hm=document.getElementById('cam_hmirror').checked?1:0;
      const vf=document.getElementById('cam_vflip').checked?1:0;
      stateEl.textContent='applying...';
      Promise.resolve().then(()=>camSet('framesize',fs))
      .then(()=>camSet('quality',q))
      .then(()=>camSet('hmirror',hm))
      .then(()=>camSet('vflip',vf))
      .then(()=>{camRestart();stateEl.textContent='ok';})
      .catch(e=>{alert('設定失敗:'+e);stateEl.textContent='error';});
    }
    function camStart(){camImg.onerror=()=>{stateEl.textContent='reconnect...';setTimeout(()=>camStart(),800);};camImg.onload=()=>{stateEl.textContent='streaming';};camImg.src='/cam?ts='+Date.now();}
    function camStop(){camImg.src='';stateEl.textContent='stopped';}
    function camRestart(){camStop();setTimeout(camStart,200);}
    function camSnap(){window.open('/cam_snapshot','_blank');}

    function toggleFullscreen(){
      const camCard=document.getElementById('cam_stream_card');
      if(!document.fullscreenElement){
        camCard.requestFullscreen().then(()=>{fsBtn.style.display='block';});
      }else{
        document.exitFullscreen().then(()=>{fsBtn.style.display='none';});
      }
    }
    function exitFullscreen(){
      if(document.fullscreenElement) document.exitFullscreen();
      fsBtn.style.display='none';
    }

    window.addEventListener('load',()=>{updateResLabel();camStart();});
  </script>
</body>
</html>
)rawliteral";
