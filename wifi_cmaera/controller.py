import time
import json
import threading
from dataclasses import dataclass

from websocket import create_connection
from fastapi import FastAPI
from pydantic import BaseModel

from angle_generator import generate_angles, init_generator

# =========================
# RTT / CSV
# =========================
seq_counter = 0
measure_enabled = False
csv_lines = ["seq,rtt_ms"]

def save_csv():
    with open("latency.csv", "w", encoding="utf-8") as f:
        f.write("\n".join(csv_lines))

# =========================
# State
# =========================
@dataclass
class ControlState:
    running: bool = False
    esp_host: str = "192.168.4.1"
    esp_ws_port: int = 82
    interval_ms: int = 50

state = ControlState()
worker_thread = None
state_lock = threading.Lock()

# =========================
# Request Models
# =========================
class HostReq(BaseModel):
    esp_host: str
    esp_ws_port: int = 82

class IntervalReq(BaseModel):
    interval_ms: int

# =========================
# Utils
# =========================
def ws_url():
    return f"ws://{state.esp_host}:{state.esp_ws_port}"

# =========================
# Send
# =========================
def send_angle(ws, angles):
    global seq_counter

    payload = {
        "cmd": "set_angle",
        "seq": seq_counter,
        "angles": angles
    }

    ws.send(json.dumps(payload))
    seq_counter += 1

def send_angle_rtt(ws, angles):
    global seq_counter

    seq = seq_counter
    seq_counter += 1

    payload = {
        "cmd": "set_angle",
        "seq": seq,
        "angles": angles
    }

    t1 = time.perf_counter_ns()
    ws.send(json.dumps(payload))

    while True:
        msg = ws.recv()
        t2 = time.perf_counter_ns()

        try:
            data = json.loads(msg)
        except Exception:
            continue

        if data.get("type") == "angle_ack" and data.get("seq") == seq:
            rtt = (t2 - t1) / 1e6
            csv_lines.append(f"{seq},{rtt:.2f}")
            print(f"[RTT] {rtt:.2f} ms")
            return

# =========================
# Control Loop
# =========================
def control_loop():
    print("[PY] control loop start")

    try:
        ws = create_connection(ws_url(), timeout=3)
    except Exception as e:
        print("[PY] connect fail:", e)
        return

    init_generator()

    t0 = time.time()
    last_time = t0

    while True:
        with state_lock:
            if not state.running:
                break
            interval = state.interval_ms

        now = time.time()
        t = now - t0
        dt = now - last_time
        last_time = now

        if dt <= 0:
            dt = interval / 1000.0

        try:
            angles = generate_angles(t, dt)
        except Exception as e:
            print("[PY] generate angle fail:", e)
            break

        try:
            if measure_enabled:
                send_angle_rtt(ws, angles)
            else:
                send_angle(ws, angles)
        except Exception as e:
            print("[PY] send fail:", e)
            break

        time.sleep(interval / 1000.0)

    try:
        ws.close()
    except Exception:
        pass

    print("[PY] control loop stop")

# =========================
# FastAPI
# =========================
app = FastAPI()

@app.get("/")
def root():
    with state_lock:
        return {
            "running": state.running,
            "esp_host": state.esp_host,
            "esp_ws_port": state.esp_ws_port,
            "interval_ms": state.interval_ms,
            "measure_enabled": measure_enabled
        }

@app.post("/set_esp_host")
def set_host(req: HostReq):
    with state_lock:
        state.esp_host = req.esp_host
        state.esp_ws_port = req.esp_ws_port
    return {"ok": True}

@app.post("/set_interval")
def set_interval(req: IntervalReq):
    if req.interval_ms <= 0:
        return {"ok": False, "error": "interval_ms must be > 0"}

    with state_lock:
        state.interval_ms = req.interval_ms

    return {"ok": True, "interval_ms": req.interval_ms}

@app.post("/start")
def start():
    global worker_thread

    with state_lock:
        state.running = True

    if worker_thread is None or not worker_thread.is_alive():
        worker_thread = threading.Thread(target=control_loop, daemon=True)
        worker_thread.start()

    return {"ok": True}

@app.post("/stop")
def stop():
    with state_lock:
        state.running = False

    if measure_enabled:
        save_csv()

    return {"ok": True}

@app.post("/measure_on")
def measure_on():
    global measure_enabled
    measure_enabled = True
    return {"ok": True}

@app.post("/measure_off")
def measure_off():
    global measure_enabled
    measure_enabled = False
    return {"ok": True}