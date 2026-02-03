import time
import threading
import tkinter as tk
from tkinter import ttk
import numpy as np
import mujoco
import mujoco.viewer
import csv
import cv2
import os
import queue  # 用於非同步錄影，不卡主執行緒
from eel_env import EelEnv

class ControlPanel:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Eel Sweep - High Performance Async Video")
        self.lock = threading.Lock()
        self.vars = {}

        # --- 1. 定義基礎參數與實驗排程 (完全保留你的設定) ---
        self.BASE_FREQ, self.BASE_AMP, self.BASE_STEP = 1.0, 0.5, 0.5
        
        self.range_std = np.round(np.arange(0.1, 1.1, 0.1), 1).tolist()
        self.range_freq = np.round(np.arange(0.5, 2.1, 0.1), 1).tolist()
        
        self.experiment_queue = []
        # (A) 掃描 Frequency (原本的代碼)
        for f in self.range_freq: 
            self.experiment_queue.append({"amp": self.BASE_AMP, "freq": f, "step": self.BASE_STEP, "tag": "Sweep_Freq"})
        # (B) 掃描 Amplitude (原本的代碼)
        for a in self.range_std: 
            self.experiment_queue.append({"amp": a, "freq": self.BASE_FREQ, "step": self.BASE_STEP, "tag": "Sweep_Amp"})
        # (C) ✅ 補回缺失的 Phase Step 掃描 (新增)
        for s in self.range_std:
            self.experiment_queue.append({"amp": self.BASE_AMP, "freq": self.BASE_FREQ, "step": s, "tag": "Sweep_Step"})
            
        self.queue_idx = 0
        self.paused = True
        self.auto_mode = False
        self.reset_request = False 
        self.current_speed = 0.0
        self.is_alive = True
        
        self.session_dir = "" 
        self.video_dir = ""
        self.results_file = "eel_single_factor_results.csv"

        self.amp, self.freq, self.turn_bias, self.phase_step = self.BASE_AMP, self.BASE_FREQ, 0.0, self.BASE_STEP
        
        self._setup_ui()
        self.root.protocol("WM_DELETE_WINDOW", self.quit)

    def _setup_ui(self):
        frm = ttk.Frame(self.root, padding=15)
        frm.grid(row=0, column=0, sticky="nsew")

        self.status_var = tk.StringVar(value="MODE: MANUAL")
        self.speed_var = tk.StringVar(value="Speed: 0.00 m/s")
        ttk.Label(frm, textvariable=self.status_var, font=("Segoe UI", 10, "bold")).grid(row=0, column=0, pady=5, sticky="w")
        ttk.Label(frm, textvariable=self.speed_var, font=("Consolas", 11), foreground="blue").grid(row=1, column=0, pady=5, sticky="w")

        btn_frm = ttk.Frame(frm)
        btn_frm.grid(row=2, column=0, columnspan=2, pady=5, sticky="ew")
        ttk.Button(btn_frm, text="Run/Pause", command=self.toggle_pause).pack(side="left", fill="x", expand=True)
        ttk.Button(btn_frm, text="Start Exp", command=self.toggle_auto_mode).pack(side="left", fill="x", expand=True)
        ttk.Button(btn_frm, text="Reset Physics", command=self.trigger_reset).pack(side="left", fill="x", expand=True)
        ttk.Button(btn_frm, text="Apply Params", command=self._manual_update).pack(side="left", fill="x", expand=True)

        attrs = [("Amp", "amp"), ("Freq", "freq"), ("Bias", "turn_bias"), ("Step", "phase_step")]
        for i, (label, attr) in enumerate(attrs):
            ttk.Label(frm, text=label).grid(row=3+i, column=0, sticky="w")
            var = tk.StringVar(value=str(getattr(self, attr)))
            self.vars[attr] = var
            ent = ttk.Entry(frm, textvariable=var, width=10)
            ent.grid(row=3+i, column=1, sticky="w", pady=2)
            ent.bind("<Return>", lambda e: self._manual_update())

        self._update_ui_loop()

    def trigger_reset(self):
        with self.lock: self.reset_request = True

    def _manual_update(self):
        with self.lock:
            try:
                self.amp = float(self.vars['amp'].get())
                self.freq = float(self.vars['freq'].get())
                self.turn_bias = float(self.vars['turn_bias'].get())
                self.phase_step = float(self.vars['phase_step'].get())
            except ValueError: pass

    def _update_ui_loop(self):
        if not self.is_alive: return
        try:
            with self.lock:
                cur_s = self.current_speed
                if self.auto_mode:
                    self.vars['amp'].set(f"{self.amp:.2f}")
                    self.vars['freq'].set(f"{self.freq:.2f}")
                    self.vars['phase_step'].set(f"{self.phase_step:.2f}")
            self.speed_var.set(f"Speed: {cur_s:.3f} m/s")
            self._update_status_ui_text()
            self.root.after(100, self._update_ui_loop)
        except: pass

    def _update_status_ui_text(self):
        m = "EXP" if self.auto_mode else "MANUAL"
        s = "RUNNING" if not self.paused else "PAUSED"
        prog = f"({self.queue_idx+1}/{len(self.experiment_queue)})" if self.auto_mode else ""
        self.status_var.set(f"MODE: {m} / {s} {prog}")

    def toggle_pause(self):
        with self.lock: self.paused = not self.paused
        self._update_status_ui_text()

    def toggle_auto_mode(self):
        with self.lock:
            self.auto_mode = not self.auto_mode
            if self.auto_mode:
                ts = time.strftime("%Y%m%d_%H%M%S")
                self.session_dir = f"Exp_{ts}"
                self.video_dir = os.path.join(self.session_dir, "videos")
                os.makedirs(self.video_dir, exist_ok=True)
                self.results_file = os.path.join(self.session_dir, "results.csv")
                self.paused, self.queue_idx = False, 0
                with open(self.results_file, 'w', newline='') as f:
                    csv.writer(f).writerow(["Time", "Tag", "Amp", "Freq", "Step", "AvgSpeed", "Y_Offset", "Valid"])
        self._apply_current_queue()
        self._update_status_ui_text()

    def _apply_current_queue(self):
        if not self.is_alive: return
        with self.lock:
            if self.queue_idx < len(self.experiment_queue):
                p = self.experiment_queue[self.queue_idx]
                self.amp, self.freq, self.phase_step = p["amp"], p["freq"], p["step"]

    def quit(self):
        with self.lock: self.is_alive = False
        self.root.quit()
        self.root.destroy()

# ✅ 背景存檔執行緒，防止卡頓
def video_saver_worker(video_queue, video_path, fps, width, height):
    out = cv2.VideoWriter(video_path, cv2.VideoWriter_fourcc(*'mp4v'), fps, (width, height))
    while True:
        frame = video_queue.get()
        if frame is None: break
        out.write(frame)
    out.release()

def run_mujoco(panel: ControlPanel, xml_path="eel.xml"):
    env = EelEnv(xml_path)
    env.reset()
    initial_qpos = np.copy(env.data.qpos)
    trial_speeds, is_waiting, wait_start_time = [], False, 0
    
    # 錄影參數
    width, height, fps = 640, 480, 30
    renderer = mujoco.Renderer(env.model, height=height, width=width)
    video_queue = None
    video_thread = None
    recording_interval = int((1.0 / fps) / env.model.opt.timestep)

    try:
        with mujoco.viewer.launch_passive(env.model, env.data) as viewer:
            viewer.cam.azimuth, viewer.cam.elevation, viewer.cam.distance = 0, -90.0, 13.0
            viewer.cam.lookat = [0, 0, 0]

            while viewer.is_running():
                step_start = time.time()
                with panel.lock:
                    if not panel.is_alive: break
                    p_paused = panel.paused
                    p_amp, p_freq, p_step = panel.amp, panel.freq, panel.phase_step
                    p_auto, p_turn = panel.auto_mode, panel.turn_bias
                    p_reset = panel.reset_request
                    cur_results, cur_v_dir, cur_q_idx = panel.results_file, panel.video_dir, panel.queue_idx

                if p_reset:
                    with viewer.lock():
                        mujoco.mj_resetData(env.model, env.data)
                        env.data.qpos[:] = initial_qpos
                        mujoco.mj_forward(env.model, env.data)
                    with panel.lock: panel.reset_request = False
                    trial_speeds = []

                if not p_paused:
                    if not is_waiting:
                        # ✅ 啟動非同步錄影執行緒
                        if video_thread is None and p_auto:
                            tag = panel.experiment_queue[cur_q_idx]["tag"]
                            # 檔名加入參數標籤以便區分
                            v_name = os.path.join(cur_v_dir, f"{tag}_idx{cur_q_idx}_F{p_freq}_A{p_amp}_S{p_step}.mp4")
                            video_queue = queue.Queue()
                            video_thread = threading.Thread(target=video_saver_worker, args=(video_queue, v_name, fps, width, height))
                            video_thread.start()

                        # --- 核心游泳公式 (保留原本邏輯) ---
                        t = env.data.time
                        num_j = len(env.data.ctrl)
                        current_bias = 0.0 if p_auto else p_turn
                       # ctrl = [current_bias + (p_amp * (0.4 + 0.6 * (i/(num_j-1)))) * np.sin(2 * np.pi * p_freq * t - i * p_step) for i in range(num_j)]
                        ctrl = [current_bias + p_amp * np.sin(2 * np.pi * p_freq * t) for i in range(num_j)]  #駐波
                        with viewer.lock():
                            env.data.ctrl[:] = np.clip(ctrl, -1.2, 1.2)
                            mujoco.mj_step(env.model, env.data)

                        speed = np.linalg.norm(env.data.qvel[0:2])
                        trial_speeds.append(speed)
                        with panel.lock: panel.current_speed = speed

                        # ✅ 效能優化：只在特定步數擷取畫面丟入隊列，不阻塞計算
                        if video_thread is not None:
                            sim_steps = int(t / env.model.opt.timestep)
                            if sim_steps % recording_interval == 0:
                                renderer.update_scene(env.data, camera=viewer.cam)
                                frame = cv2.cvtColor(renderer.render(), cv2.COLOR_RGB2BGR)
                                video_queue.put(frame)

                        # --- 碰撞判定 (保留原本邏輯) ---
                        is_collided = False
                        if env.data.ncon > 0:
                            for i in range(env.data.ncon):
                                con = env.data.contact[i]
                                n1 = mujoco.mj_id2name(env.model, mujoco.mjtObj.mjOBJ_GEOM, con.geom1)
                                n2 = mujoco.mj_id2name(env.model, mujoco.mjtObj.mjOBJ_GEOM, con.geom2)
                                if (n1 == "base_link_collision" and n2 == "wall_front") or (n1 == "wall_front" and n2 == "base_link_collision"):
                                    is_collided = True; break

                        if is_collided:
                            if video_thread is not None:
                                video_queue.put(None) # 通知錄影執行緒結束
                                video_thread = None

                            avg_s = np.mean(trial_speeds) if trial_speeds else 0
                            y_off = abs(env.data.qpos[1])
                            with panel.lock: tag = panel.experiment_queue[panel.queue_idx]["tag"] if p_auto else "Manual"
                            with open(cur_results, 'a', newline='') as f:
                                csv.writer(f).writerow([time.strftime("%H:%M:%S"), tag, p_amp, p_freq, p_step, f"{avg_s:.4f}", f"{y_off:.2f}", y_off < 1.0])
                            is_waiting, wait_start_time = True, time.time()
                    
                    else:
                        if time.time() - wait_start_time > 1.2:
                            with panel.lock:
                                if panel.auto_mode:
                                    panel.queue_idx += 1
                                    if panel.queue_idx >= len(panel.experiment_queue):
                                        panel.auto_mode, panel.paused = False, True
                                else: panel.paused = True 
                                panel._update_status_ui_text()

                            panel._apply_current_queue()
                            with viewer.lock():
                                mujoco.mj_resetData(env.model, env.data)
                                env.data.qpos[:] = initial_qpos
                                mujoco.mj_forward(env.model, env.data)
                            trial_speeds, is_waiting = [], False

                viewer.sync()
                dt = env.model.opt.timestep
                elapsed = time.time() - step_start
                if dt > elapsed: time.sleep(dt - elapsed)
    finally:
        # ✅ 清理錄影執行緒防止程式無法關閉
        if video_thread is not None: video_queue.put(None)
        renderer.close()

if __name__ == "__main__":
    p = ControlPanel()
    threading.Thread(target=run_mujoco, args=(p, "eel.xml"), daemon=True).start()
    p.root.mainloop()