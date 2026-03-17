import time
import json
import threading
from dataclasses import dataclass
from websocket import create_connection
from fastapi import FastAPI
from pydantic import BaseModel

from angle_generator import generate_angles

# =========================
# RTT / CSV
# =========================
seq_counter = 0
measure_enabled = False
csv_lines = ["seq,rtt_ms"]

def save_csv():
    with open("latency.csv", "w") as f:
        f.write("\n".join(csv_lines))

# =========================
# State
# =========================
@dataclass
class ControlState:
    running: bool = False
    esp_host: str = "192.168.4.1"
    esp_ws_port: int = 82
    interval_ms: int = 50   # ← 固定在 Python

state = ControlState()
worker_thread = None
state_lock = threading.Lock()

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
        except:
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

    t0 = time.time()

    while True:
        with state_lock:
            if not state.running:
                break
            interval = state.interval_ms

        t = time.time() - t0
        angles = generate_angles(t)

        try:
            if measure_enabled:
                send_angle_rtt(ws, angles)
            else:
                send_angle(ws, angles)
        except:
            break

        time.sleep(interval / 1000.0)

    ws.close()
    print("[PY] control loop stop")

# =========================
# FastAPI
# =========================
app = FastAPI()

class HostReq(BaseModel):
    esp_host: str
    esp_ws_port: int = 82

@app.post("/set_esp_host")
def set_host(req: HostReq):
    state.esp_host = req.esp_host
    state.esp_ws_port = req.esp_ws_port
    return {"ok": True}

@app.post("/start")
def start():
    global worker_thread

    state.running = True

    if worker_thread is None or not worker_thread.is_alive():
        worker_thread = threading.Thread(target=control_loop, daemon=True)
        worker_thread.start()

    return {"ok": True}

@app.post("/stop")
def stop():
    state.running = False
    if measure_enabled:
        save_csv()
    return {"ok": True}

@app.post("/measure_on")
def m_on():
    global measure_enabled
    measure_enabled = True
    return {"ok": True}

@app.post("/measure_off")
def m_off():
    global measure_enabled
    measure_enabled = False
    return {"ok": True}
