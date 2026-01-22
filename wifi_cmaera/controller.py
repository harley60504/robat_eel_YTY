import math
import time
import json
import threading
from dataclasses import dataclass
from typing import Optional

from fastapi import FastAPI
from pydantic import BaseModel
from websocket import create_connection


# =========================
# Servo 設定
# =========================
SERVO_COUNT = 6
MIN_DEG = 0
MAX_DEG = 240


def clamp(x, lo, hi):
    return max(lo, min(hi, x))


@dataclass
class ControlState:
    running: bool = False

    # ✅ ESP32 host（Flutter 傳進來）
    esp_host: str = "192.168.4.1"

    # ✅ Flutter wsControlUrl 固定用 82
    esp_ws_port: int = 82

    # angle waveform params
    base: float = 120.0
    amp: float = 30.0
    freq: float = 0.6
    phase_step: float = 0.7
    interval_ms: int = 50


state = ControlState()
state_lock = threading.Lock()
worker_thread: Optional[threading.Thread] = None


def build_esp_ws_url() -> str:
    # ✅ 關鍵：必須跟 Flutter 一樣 → ws://host:82
    return f"ws://{state.esp_host}:{state.esp_ws_port}"


def generate_angles(t: float, base: float, amp: float, freq: float, phase_step: float):
    angles = []
    for i in range(SERVO_COUNT):
        a = base + amp * math.sin(2 * math.pi * freq * t + i * phase_step)
        angles.append(clamp(a, MIN_DEG, MAX_DEG))
    return angles


def send_to_esp32(ws, angles):
    # ✅ 關鍵：必須跟 Flutter 一樣
    payload = {
        "cmd": "set_angle",
        "angles": [float(round(a, 1)) for a in angles],
    }
    ws.send(json.dumps(payload))


def control_loop():
    print("[PY] control loop started")

    with state_lock:
        ws_url = build_esp_ws_url()

    try:
        ws = create_connection(ws_url, timeout=3)
        print("[PY] connected to ESP32:", ws_url)
    except Exception as e:
        print("[PY] connect to ESP32 failed:", e)
        with state_lock:
            state.running = False
        return

    t0 = time.time()

    while True:
        with state_lock:
            if not state.running:
                break

            base = state.base
            amp = state.amp
            freq = state.freq
            phase_step = state.phase_step
            interval_ms = state.interval_ms

        t = time.time() - t0
        angles = generate_angles(t, base, amp, freq, phase_step)

        try:
            send_to_esp32(ws, angles)
        except Exception as e:
            print("[PY] send failed:", e)
            break

        time.sleep(interval_ms / 1000.0)

    try:
        ws.close()
    except:
        pass

    with state_lock:
        state.running = False

    print("[PY] control loop stopped")


# =========================
# FastAPI
# =========================
app = FastAPI()


class EspHostReq(BaseModel):
    esp_host: str
    esp_ws_port: int = 82  # ✅ 預設就是 82


class StartReq(BaseModel):
    base: float = 120
    amp: float = 30
    freq: float = 0.6
    phase_step: float = 0.7
    interval_ms: int = 50


class SetParamsReq(BaseModel):
    base: Optional[float] = None
    amp: Optional[float] = None
    freq: Optional[float] = None
    phase_step: Optional[float] = None
    interval_ms: Optional[int] = None


@app.get("/health")
def health():
    with state_lock:
        return {
            "ok": True,
            "service": "python_controller",
            "running": state.running,
            "esp_host": state.esp_host,
            "esp_ws_port": state.esp_ws_port,
            "esp_ws_url": build_esp_ws_url(),
            "params": {
                "base": state.base,
                "amp": state.amp,
                "freq": state.freq,
                "phase_step": state.phase_step,
                "interval_ms": state.interval_ms,
            },
        }


@app.post("/set_esp_host")
def set_esp_host(req: EspHostReq):
    with state_lock:
        state.esp_host = req.esp_host
        state.esp_ws_port = req.esp_ws_port
        url = build_esp_ws_url()

    print(f"[PY] set esp host -> {url}")
    return {"ok": True, "esp_ws_url": url}


@app.post("/start")
def start(req: StartReq):
    global worker_thread

    with state_lock:
        state.base = req.base
        state.amp = req.amp
        state.freq = req.freq
        state.phase_step = req.phase_step
        state.interval_ms = req.interval_ms
        state.running = True

    if worker_thread is None or not worker_thread.is_alive():
        worker_thread = threading.Thread(target=control_loop, daemon=True)
        worker_thread.start()
        print("[PY] worker thread started")
    else:
        print("[PY] worker thread already running")

    with state_lock:
        return {
            "ok": True,
            "running": state.running,
            "esp_ws_url": build_esp_ws_url(),
        }


@app.post("/stop")
def stop():
    with state_lock:
        state.running = False
    return {"ok": True, "running": False}


@app.post("/set_params")
def set_params(req: SetParamsReq):
    with state_lock:
        if req.base is not None:
            state.base = req.base
        if req.amp is not None:
            state.amp = req.amp
        if req.freq is not None:
            state.freq = req.freq
        if req.phase_step is not None:
            state.phase_step = req.phase_step
        if req.interval_ms is not None:
            state.interval_ms = req.interval_ms

    return {"ok": True}
