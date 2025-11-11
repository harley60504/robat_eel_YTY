// Your custom frontend (uses /status and /set), points <img> to :81/stream with fallback.
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>XIAO ESP32S3 高速相機</title>
  <meta http-equiv="Cache-Control" content="no-store, no-cache, must-revalidate, max-age=0">
  <meta http-equiv="Pragma" content="no-cache">
  <style>
    :root{--bg:#0a0a0a;--fg:#eaeaea;--accent:#00e5ff}
    *{box-sizing:border-box} body{background:var(--bg);color:var(--fg);font-family:"Segoe UI",sans-serif;margin:0}
    header{padding:16px 12px;text-align:center;border-bottom:1px solid #1a1a1a}
    h1{color:var(--accent);margin:6px 0 2px}
    .wrap{display:grid;grid-template-columns:320px 1fr;gap:14px;max-width:1080px;margin:14px auto;padding:0 12px}
    .card{background:#111;border:1px solid #1d1d1d;border-radius:12px;padding:12px;box-shadow:0 6px 24px rgba(0,0,0,.25)}
    .card h2{font-size:16px;margin:0 0 10px;color:#bfefff}
    label{display:block;margin:10px 0 4px;opacity:.9}
    select,input[type=range]{width:100%}
    .row{display:grid;grid-template-columns:1fr auto;gap:8px;align-items:center}
    .pill{display:inline-block;padding:2px 8px;border:1px solid #2a2a2a;border-radius:999px;opacity:.85}
    #stream{width:100%;max-height:75vh;object-fit:contain;border-radius:10px;box-shadow:0 0 25px rgba(0,255,255,.25)}
    footer{opacity:.7;text-align:center;padding:8px 0 14px}
    button{padding:8px 10px;border-radius:10px;border:1px solid #2a2a2a;background:#161616;color:#fff;cursor:pointer}
    button:hover{background:#1c1c1c}
    .switch{display:flex;align-items:center;gap:8px;margin:8px 0}
    .switch input{transform:scale(1.2)}
  </style>
</head>
<body>
  <header>
    <div class="pill">http stream</div>
    <h1>⚡ XIAO ESP32S3 MJPEG 串流伺服器</h1>
    <div style="opacity:.8">(可即時調整解析度 / 壓縮率 / 翻轉 / 亮度 / 對比)</div>
  </header>
  <div class="wrap">
    <div class="card">
      <h2>📷 即時參數</h2>

      <label>解析度 (framesize)</label>
      <select id="framesize">
        <option value="QQVGA">QQVGA (160×120)</option>
        <option value="QVGA">QVGA (320×240)</option>
        <option value="HVGA">HVGA (480×320)</option>
        <option value="VGA">VGA (640×480)</option>
        <option value="SVGA">SVGA (800×600)</option>
      </select>

      <label style="margin-top:12px">JPEG 壓縮率 (quality) <small style="opacity:.8">（數值越大壓得越狠、延遲通常更低）</small></label>
      <div class="row">
        <input id="quality" type="range" min="10" max="40" step="1">
        <div class="pill"><span id="qv">16</span></div>
      </div>

      <div class="switch"><input type="checkbox" id="vflip"><label for="vflip">垂直翻轉 (vflip)</label></div>
      <div class="switch"><input type="checkbox" id="hmirror"><label for="hmirror">水平鏡像 (hmirror)</label></div>

      <label>亮度 (brightness) [-2..2]</label>
      <div class="row">
        <input id="brightness" type="range" min="-2" max="2" step="1">
        <div class="pill"><span id="bv">0</span></div>
      </div>

      <label>對比 (contrast) [-2..2]</label>
      <div class="row">
        <input id="contrast" type="range" min="-2" max="2" step="1">
        <div class="pill"><span id="cv">0</span></div>
      </div>

      <div style="display:flex;gap:8px;margin-top:12px">
        <button id="apply">套用</button>
        <button id="refresh">重新載入設定</button>
      </div>
      <div id="msg" style="margin-top:8px;opacity:.8"></div>
    </div>

    <div class="card">
      <h2>🖼️ 即時影像</h2>
      <img id="stream" src=""/>
    </div>
  </div>
  <footer>小提示：若網路擁擠想要穩定低延遲，建議先降解析度，再把 quality 提高到 18–22。</footer>
<script>
// 將 <img> 指到官方 :81/stream；若 :81 被擋，回退到同埠 /stream（若已註冊回退端點）
document.addEventListener('DOMContentLoaded', ()=>{
  const img  = document.getElementById('stream');
  if (!img) return;
  const host = location.hostname || (window.location.host.split(':')[0]);
  const url81 = location.protocol + '//' + host + ':81/stream';
  let triedFallback = false;
  img.onerror = () => { if (!triedFallback) { triedFallback = true; img.src = '/stream'; } };
  img.src = url81;
});

async function getStatus(){
  const r = await fetch('/status');
  const j = await r.json();
  document.getElementById('framesize').value = j.framesize || 'QVGA';
  document.getElementById('quality').value   = j.quality  || 16;
  document.getElementById('qv').innerText    = j.quality  || 16;
  document.getElementById('vflip').checked   = !!j.vflip;
  document.getElementById('hmirror').checked = !!j.hmirror;
  document.getElementById('brightness').value = (j.brightness ?? 0);
  document.getElementById('bv').innerText     = (j.brightness ?? 0);
  document.getElementById('contrast').value   = (j.contrast ?? 0);
  document.getElementById('cv').innerText     = (j.contrast ?? 0);
}
function setMsg(t){ const m=document.getElementById('msg'); m.textContent=t; setTimeout(()=>m.textContent='',2000); }

getStatus();

document.getElementById('quality').addEventListener('input', e=>{ document.getElementById('qv').innerText = e.target.value; });
document.getElementById('brightness').addEventListener('input', e=>{ document.getElementById('bv').innerText = e.target.value; });
document.getElementById('contrast').addEventListener('input', e=>{ document.getElementById('cv').innerText = e.target.value; });

document.getElementById('apply').addEventListener('click', async ()=>{
  const p = new URLSearchParams();
  p.set('framesize', document.getElementById('framesize').value);
  p.set('quality',   document.getElementById('quality').value);
  p.set('vflip',     document.getElementById('vflip').checked ? '1' : '0');
  p.set('hmirror',   document.getElementById('hmirror').checked ? '1' : '0');
  p.set('brightness',document.getElementById('brightness').value);
  p.set('contrast',  document.getElementById('contrast').value);
  try{
    const r = await fetch(`/set?${p.toString()}`);
    const j = await r.json();
    setMsg(j.ok ? '已套用' : '套用失敗');
  }catch(e){ setMsg('連線錯誤'); }
});

document.getElementById('refresh').addEventListener('click', getStatus);
</script>
</body>
</html>
)rawliteral";
