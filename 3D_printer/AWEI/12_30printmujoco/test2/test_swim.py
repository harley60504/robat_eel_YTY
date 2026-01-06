import time
import threading
import tkinter as tk
from tkinter import ttk
import numpy as np
import mujoco
import mujoco.viewer
import csv
from eel_env import EelEnv

class ControlPanel:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Eel Sweep - Final Stable Fix")
        self.lock = threading.Lock()

        # --- 基準值與範圍設定 ---
        self.BASE_FREQ = 1.0  
        self.BASE_AMP = 0.5
        self.BASE_STEP = 0.5
        
        self.range_std = np.round(np.arange(0.1, 1.1, 0.1), 1).tolist()
        self.range_freq = np.round(np.arange(0.5, 2.1, 0.1), 1).tolist()
        
        self.experiment_queue = []
        for f in self.range_freq: self.experiment_queue.append({"amp": self.BASE_AMP, "freq": f, "step": self.BASE_STEP, "tag": "Sweep_Freq"})
        for a in self.range_std: self.experiment_queue.append({"amp": a, "freq": self.BASE_FREQ, "step": self.BASE_STEP, "tag": "Sweep_Amp"})
            
        self.queue_idx = 0
        self.paused = True
        self.auto_mode = False
        self.reset_request = False # ✅ 新增：用來通知物理執行緒重置
        self.current_speed = 0.0
        self.is_alive = True
        self.results_file = "eel_single_factor_results.csv"
        self.vars = {}

        self.amp, self.freq, self.turn_bias, self.phase_step = self.BASE_AMP, self.BASE_FREQ, 0.0, self.BASE_STEP
        
        self._setup_ui()
        self.root.protocol("WM_DELETE_WINDOW", self.quit)

    def _setup_ui(self):
        frm = ttk.Frame(self.root, padding=15)
        frm.grid(row=0, column=0, sticky="nsew")

        self.status_var = tk.StringVar(value="MODE: MANUAL")
        self.speed_var = tk.StringVar(value="Speed: 0.00 m/s")
        ttk.Label(frm, textvariable=self.status_var, font=("Segoe UI", 10, "bold")).grid(row=0, column=0, pady=(0, 5), sticky="w")
        ttk.Label(frm, textvariable=self.speed_var, font=("Consolas", 11), foreground="blue").grid(row=1, column=0, pady=(0, 10), sticky="w")

        btn_frm = ttk.Frame(frm)
        btn_frm.grid(row=2, column=0, columnspan=2, sticky="ew", pady=5)
        ttk.Button(btn_frm, text="Run/Pause", command=self.toggle_pause).pack(side="left", fill="x", expand=True)
        ttk.Button(btn_frm, text="Start Exp", command=self.toggle_auto_mode).pack(side="left", fill="x", expand=True)
        
        # ✅ 補回 Reset 按鈕
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
        """ ✅ 點擊按鈕時設定重置請求 """
        with self.lock:
            self.reset_request = True
        print("物理重置請求已送出")

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

def run_mujoco(panel: ControlPanel, xml_path="eel.xml"):
    env = EelEnv(xml_path)
    env.reset()
    initial_qpos = np.copy(env.data.qpos)
    trial_speeds = []
    is_waiting = False
    wait_start_time = 0
    
    # 用於效能控制：每 5 步才同步一次畫面 (500Hz / 5 = 100 FPS，足夠流暢)
    frame_skip = 5
    step_counter = 0

    with mujoco.viewer.launch_passive(env.model, env.data) as viewer:
        viewer.cam.azimuth, viewer.cam.elevation, viewer.cam.distance = 0, -90.0, 13.0
        viewer.cam.lookat = [0, 0, 0]

        while viewer.is_running():
            # --- 【核心】記錄這一步開始的時間 ---
            step_start = time.time()
            
            with panel.lock:
                if not panel.is_alive: break
                p_paused = panel.paused
                p_amp, p_freq, p_step = panel.amp, panel.freq, panel.phase_step
                p_auto, p_turn = panel.auto_mode, panel.turn_bias
                p_reset = panel.reset_request

            # 處理手動 Reset
            if p_reset:
                with viewer.lock():
                    mujoco.mj_resetData(env.model, env.data)
                    env.data.qpos[:] = initial_qpos
                    mujoco.mj_forward(env.model, env.data)
                with panel.lock: panel.reset_request = False
                trial_speeds = []

            if not p_paused:
                if not is_waiting:
                    # 使用模擬器內部時間 env.data.time，這保證了 1Hz 的數學準確性
                    t = env.data.time
                    current_bias = 0.0 if p_auto else p_turn
                    num_j = len(env.data.ctrl)

                    ###############################################
                     # --- 自動修正邏輯 ---
                    # y_current = env.data.qpos[1]  # 取得當前 Y 座標（紅線是 Y=0）

                    # # 修正增益 (Kp)，如果修正太慢就調大，如果魚左右晃動太厲害就調小
                    # Kp = 0.2 
                    # auto_correction = -y_current * Kp

                    # # 最終偏壓 = 手動調整值 + 自動修正值
                    # final_bias = p_turn + auto_correction
                    #############################################
                    # 計算控制量 (S型波)
                    ctrl = [current_bias + (p_amp * (0.4 + 0.6 * (i/(num_j-1)))) * np.sin(2 * np.pi * p_freq * t - i * p_step) for i in range(num_j)]
                    
                    with viewer.lock():
                        env.data.ctrl[:] = np.clip(ctrl, -1.2, 1.2)
                        mujoco.mj_step(env.model, env.data)

                    speed = np.linalg.norm(env.data.qvel[0:2])
                    trial_speeds.append(speed)
                    with panel.lock: panel.current_speed = speed

                    # 碰撞檢測
                    is_collided = False
                    if env.data.ncon > 0:
                        for i in range(env.data.ncon):
                            con = env.data.contact[i]
                            n1 = mujoco.mj_id2name(env.model, mujoco.mjtObj.mjOBJ_GEOM, con.geom1)
                            n2 = mujoco.mj_id2name(env.model, mujoco.mjtObj.mjOBJ_GEOM, con.geom2)
                            if (n1 == "base_link_collision" and n2 == "wall_front") or (n1 == "wall_front" and n2 == "base_link_collision"):
                                is_collided = True
                                break

                    if is_collided:
                        avg_s = np.mean(trial_speeds) if trial_speeds else 0
                        y_off = abs(env.data.qpos[1])
                        with panel.lock: tag = panel.experiment_queue[panel.queue_idx]["tag"] if p_auto else "Manual"
                        with open(panel.results_file, 'a', newline='') as f:
                            csv.writer(f).writerow([time.strftime("%H:%M:%S"), tag, p_amp, p_freq, p_step, f"{avg_s:.4f}", f"{y_off:.2f}", y_off < 1.0])
                        is_waiting, wait_start_time = True, time.time()
                
                else:
                    if time.time() - wait_start_time > 1.2:
                        with panel.lock:
                            if panel.auto_mode:
                                panel.queue_idx += 1
                                if panel.queue_idx >= len(panel.experiment_queue):
                                    panel.auto_mode, panel.paused = False, True
                            else:
                                panel.paused = True 
                            panel._update_status_ui_text()

                        panel._apply_current_queue()
                        with viewer.lock():
                            mujoco.mj_resetData(env.model, env.data)
                            env.data.qpos[:] = initial_qpos
                            mujoco.mj_forward(env.model, env.data)
                        trial_speeds, is_waiting = [], False

            # --- 【優化】控制渲染頻率 ---
            step_counter += 1
            if step_counter % frame_skip == 0:
                viewer.sync()

            # --- 【關鍵】強制時間同步 ---
            # 物理步長是 0.002 秒
            dt = env.model.opt.timestep
            elapsed = time.time() - step_start
            if dt > elapsed:
                time.sleep(dt - elapsed) # 如果運算太快，就讓 CPU 休息到滿 0.002 秒

if __name__ == "__main__":
    p = ControlPanel()
    threading.Thread(target=run_mujoco, args=(p, "eel.xml"), daemon=True).start()
    p.root.mainloop()