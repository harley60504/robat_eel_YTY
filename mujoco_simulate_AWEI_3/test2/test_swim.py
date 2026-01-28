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
import queue
from eel_env import EelEnv

# ============================================================
# ✅ CONFIG：你要調的「初始參數」全部集中在這裡（只改這裡就好）
# ============================================================

# --- 初始波形參數（UI 預設值）---
AMP_INIT  = 0.50     # 初始 amp
FREQ_INIT = 1.00     # 初始 freq
STEP_INIT = 0.50     # 初始 phase_step / wavenumber

# --- 轉向 UI 參數（你按左/右、滑桿的初始）---
TURN_BIAS_INIT     = 0.00   # turn_bias 初始值
BIAS_STEP_INIT     = 0.05   # Q/W 每次加減
BIAS_MAX_INIT      = 0.80   # slider 上限
BIAS_PRESET_INIT   = 0.30   # A/D 一鍵左/右力度（想更大就加）

# --- ✅「只轉前面幾節」控制（轉向越大：GAIN↑ 或 FRONT_N↑）---
STEER_FRONT_N_INIT = 4      # 只轉前 N 節（2~5 建議）
STEER_GAIN_INIT    = 0.70   # 轉向增益（0.35~0.90 建議）

# --- 自動實驗掃描（保留你原本範圍）---
RANGE_STD_START, RANGE_STD_END, RANGE_STD_STEP   = 0.1, 1.0, 0.1
RANGE_FREQ_START, RANGE_FREQ_END, RANGE_FREQ_STEP = 0.5, 2.0, 0.1

BASE_FREQ = 1.0
BASE_AMP  = 0.5
BASE_STEP = 0.5

# --- ctrl clip ---
CTRL_CLIP_MIN = -1.2
CTRL_CLIP_MAX =  1.2

# --- 相機 Focus / Follow（你要一鍵聚焦）---
FOCUS_DIST = 4.5
FOCUS_ELEV = -60.0
FOCUS_AZIM = 0.0

# --- 視訊錄影參數 ---
RENDER_W, RENDER_H, RENDER_FPS = 640, 480, 30

# ============================================================


class ControlPanel:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Eel Sweep - Standing Wave Support")
        self.lock = threading.Lock()
        self.vars = {}

        # --- 1. 定義基礎參數與實驗排程 ---
        self.BASE_FREQ, self.BASE_AMP, self.BASE_STEP = BASE_FREQ, BASE_AMP, BASE_STEP

        self.range_std = np.round(np.arange(RANGE_STD_START, RANGE_STD_END + 1e-9, RANGE_STD_STEP), 1).tolist()
        self.range_freq = np.round(np.arange(RANGE_FREQ_START, RANGE_FREQ_END + 1e-9, RANGE_FREQ_STEP), 1).tolist()

        self.experiment_queue = []
        for f in self.range_freq:
            self.experiment_queue.append({"amp": self.BASE_AMP, "freq": f, "step": self.BASE_STEP, "tag": "Sweep_Freq"})
        for a in self.range_std:
            self.experiment_queue.append({"amp": a, "freq": self.BASE_FREQ, "step": self.BASE_STEP, "tag": "Sweep_Amp"})
        for s in self.range_std:
            self.experiment_queue.append({"amp": self.BASE_AMP, "freq": self.BASE_FREQ, "step": s, "tag": "Sweep_Step"})

        self.queue_idx = 0
        self.paused = True
        self.auto_mode = False
        self.wave_type = "Traveling"  # ✅ 新增：用於切換行進波與駐波 (Traveling / Standing)
        self.reset_request = False
        self.current_speed = 0.0
        self.current_z = 0.0
        self.current_passive_z = 0.0
        self.is_alive = True
        self.safety_break = False
        self.session_dir = ""
        self.video_dir = ""
        self.results_file = "eel_single_factor_results.csv"

        # ✅ 初始波形參數（都從 CONFIG 來）
        self.amp, self.freq, self.turn_bias, self.phase_step = AMP_INIT, FREQ_INIT, TURN_BIAS_INIT, STEP_INIT

        # ✅ 轉向控制設定（手動左右轉）— 也從 CONFIG 來
        self.bias_step = BIAS_STEP_INIT
        self.bias_max = BIAS_MAX_INIT
        self.bias_preset = BIAS_PRESET_INIT

        self._setup_ui()
        self._bind_keys()
        self.root.protocol("WM_DELETE_WINDOW", self.quit)

    def _setup_ui(self):
        frm = ttk.Frame(self.root, padding=15)
        frm.grid(row=0, column=0, sticky="nsew")

        self.status_var = tk.StringVar(value="MODE: MANUAL")
        self.speed_var = tk.StringVar(value="Speed: 0.00 m/s")
        self.z_var = tk.StringVar(value="Z-Pos: 0.000 m")
        self.pass_z_var = tk.StringVar(value="Passive-Z: 0.00 N")

        ttk.Label(frm, textvariable=self.status_var, font=("Segoe UI", 10, "bold")).grid(row=0, column=0, pady=5, sticky="w")
        ttk.Label(frm, textvariable=self.speed_var, font=("Consolas", 11), foreground="blue").grid(row=1, column=0, pady=5, sticky="w")
        ttk.Label(frm, textvariable=self.z_var, font=("Consolas", 11), foreground="green").grid(row=1, column=1, pady=5, sticky="w")
        ttk.Label(frm, textvariable=self.pass_z_var, font=("Consolas", 11), foreground="purple").grid(row=0, column=1, pady=5, sticky="w")

        btn_frm = ttk.Frame(frm)
        btn_frm.grid(row=2, column=0, columnspan=2, pady=5, sticky="ew")
        ttk.Button(btn_frm, text="Run/Pause (Space)", command=self.toggle_pause).pack(side="left", fill="x", expand=True)
        ttk.Button(btn_frm, text="Start Exp", command=self.toggle_auto_mode).pack(side="left", fill="x", expand=True)
        ttk.Button(btn_frm, text="Wave Type (T)", command=self.toggle_wave_type).pack(side="left", fill="x", expand=True)
        ttk.Button(btn_frm, text="Reset Physics (R)", command=self.trigger_reset).pack(side="left", fill="x", expand=True)
        ttk.Button(btn_frm, text="Apply Params (Enter)", command=self._manual_update).pack(side="left", fill="x", expand=True)

        # --- ✅ 轉向控制區（左右轉）---
        turn_frm = ttk.LabelFrame(frm, text="Steering (Manual)", padding=10)
        turn_frm.grid(row=3, column=0, columnspan=2, sticky="ew", pady=6)

        ttk.Button(turn_frm, text="⟵ Left (A/←)", command=self.turn_left).grid(row=0, column=0, sticky="ew", padx=4)
        ttk.Button(turn_frm, text="Straight (S)", command=self.turn_straight).grid(row=0, column=1, sticky="ew", padx=4)
        ttk.Button(turn_frm, text="Right (D/→) ⟶", command=self.turn_right).grid(row=0, column=2, sticky="ew", padx=4)

        ttk.Button(turn_frm, text="Bias - (Q)", command=lambda: self.nudge_bias(-self.bias_step)).grid(row=1, column=0, sticky="ew", padx=4, pady=4)
        ttk.Button(turn_frm, text="Bias = 0 (E)", command=self.turn_straight).grid(row=1, column=1, sticky="ew", padx=4, pady=4)
        ttk.Button(turn_frm, text="Bias + (W)", command=lambda: self.nudge_bias(+self.bias_step)).grid(row=1, column=2, sticky="ew", padx=4, pady=4)

        # Slider 直接拉 turn_bias
        self.bias_slider = tk.DoubleVar(value=self.turn_bias)
        bias_scale = ttk.Scale(
            turn_frm,
            from_=-self.bias_max, to=self.bias_max,
            variable=self.bias_slider,
            command=self._on_bias_slider
        )
        bias_scale.grid(row=2, column=0, columnspan=3, sticky="ew", pady=(6, 2))
        self.bias_label = tk.StringVar(value=f"turn_bias = {self.turn_bias:+.3f}")
        ttk.Label(turn_frm, textvariable=self.bias_label, font=("Consolas", 10)).grid(row=3, column=0, columnspan=3, sticky="w")

        for c in range(3):
            turn_frm.grid_columnconfigure(c, weight=1)

        attrs = [("Amp", "amp"), ("Freq", "freq"), ("Bias (turn_bias)", "turn_bias"), ("Step/Wavenumber", "phase_step")]
        start_row = 4
        for i, (label, attr) in enumerate(attrs):
            ttk.Label(frm, text=label).grid(row=start_row + i, column=0, sticky="w")
            var = tk.StringVar(value=str(getattr(self, attr)))
            self.vars[attr] = var
            ent = ttk.Entry(frm, textvariable=var, width=10)
            ent.grid(row=start_row + i, column=1, sticky="w", pady=2)
            ent.bind("<Return>", lambda e: self._manual_update())

        help_txt = (
            "Keys: Space=Run/Pause, A/←=Left, D/→=Right, S=Straight, "
            "Q/W=Nudge bias -, +, E=Zero, T=Wave Type, R=Reset, "
            "F=Focus, G=Follow ON/OFF"
        )
        ttk.Label(frm, text=help_txt, foreground="gray").grid(row=start_row + len(attrs), column=0, columnspan=2, pady=6, sticky="w")

        self._update_ui_loop()

    def _bind_keys(self):
        # 讓按鍵一定打到這個視窗
        self.root.focus_force()

        self.root.bind("<space>", lambda e: self.toggle_pause())
        self.root.bind("t", lambda e: self.toggle_wave_type())
        self.root.bind("r", lambda e: self.trigger_reset())

        # 方向控制
        self.root.bind("a", lambda e: self.turn_left())
        self.root.bind("<Left>", lambda e: self.turn_left())
        self.root.bind("d", lambda e: self.turn_right())
        self.root.bind("<Right>", lambda e: self.turn_right())
        self.root.bind("s", lambda e: self.turn_straight())

        # 小步調整 turn_bias
        self.root.bind("q", lambda e: self.nudge_bias(-self.bias_step))
        self.root.bind("w", lambda e: self.nudge_bias(+self.bias_step))
        self.root.bind("e", lambda e: self.turn_straight())

    def toggle_wave_type(self):  # ✅ 新增：切換駐波邏輯
        with self.lock:
            self.wave_type = "Standing" if self.wave_type == "Traveling" else "Traveling"
            print(f"[Mode Switch] Current Wave Type: {self.wave_type}")

    def trigger_reset(self):
        with self.lock:
            self.reset_request = True

    def _manual_update(self):
        with self.lock:
            try:
                self.amp = float(self.vars["amp"].get())
                self.freq = float(self.vars["freq"].get())
                self.turn_bias = float(self.vars["turn_bias"].get())
                self.phase_step = float(self.vars["phase_step"].get())
            except ValueError:
                pass

            # 同步 slider/label
            self.turn_bias = float(np.clip(self.turn_bias, -self.bias_max, self.bias_max))
            self.bias_slider.set(self.turn_bias)
            self.bias_label.set(f"turn_bias = {self.turn_bias:+.3f}")

    # ---------------- Steering API ----------------
    def _set_bias(self, v: float):
        v = float(np.clip(v, -self.bias_max, self.bias_max))
        with self.lock:
            self.turn_bias = v
            self.vars["turn_bias"].set(f"{v:.4f}")
            self.bias_slider.set(v)
            self.bias_label.set(f"turn_bias = {v:+.3f}")

    def _on_bias_slider(self, _=None):
        self._set_bias(self.bias_slider.get())

    def nudge_bias(self, dv: float):
        with self.lock:
            v = self.turn_bias + dv
        self._set_bias(v)

    # ✅ 左右方向（你說方向反了 → 已交換）
    def turn_left(self):
        self._set_bias(-self.bias_preset)

    def turn_right(self):
        self._set_bias(+self.bias_preset)

    def turn_straight(self):
        self._set_bias(0.0)

    def _update_ui_loop(self):
        if not self.is_alive:
            return
        try:
            with self.lock:
                cur_s = self.current_speed
                cur_z = self.current_z
                cur_pz = self.current_passive_z
                cur_type = self.wave_type
                cur_bias = self.turn_bias

                if abs(cur_z) > 0.4:
                    self.z_var.set(f"Z-Pos: {cur_z:.3f} m (HIGH!)")
                else:
                    self.z_var.set(f"Z-Pos: {cur_z:.3f} m")

                if self.auto_mode:
                    self.vars["amp"].set(f"{self.amp:.2f}")
                    self.vars["freq"].set(f"{self.freq:.2f}")
                    self.vars["phase_step"].set(f"{self.phase_step:.2f}")

                self.bias_label.set(f"turn_bias = {cur_bias:+.3f}")

            self.speed_var.set(f"Speed: {cur_s:.3f} m/s")
            self.z_var.set(f"Z-Pos: {cur_z:.3f} m")
            self.pass_z_var.set(f"Passive-Z: {cur_pz:.3f} N")
            self._update_status_ui_text(cur_type)
            self.root.after(100, self._update_ui_loop)
        except:
            pass

    def _update_status_ui_text(self, w_type=""):
        m = "EXP" if self.auto_mode else "MANUAL"
        s = "RUNNING" if not self.paused else "PAUSED"
        prog = f"({self.queue_idx+1}/{len(self.experiment_queue)})" if self.auto_mode else ""
        self.status_var.set(f"MODE: {m} / {s} ({w_type}) {prog}")

    def toggle_pause(self):
        with self.lock:
            self.paused = not self.paused

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
                with open(self.results_file, "w", newline="") as f:
                    csv.writer(f).writerow(["Time", "Tag", "Amp", "Freq", "Step", "AvgSpeed", "Y_Offset", "Z_Final", "Valid"])
                self.turn_bias = 0.0
                self.bias_slider.set(0.0)
                self.vars["turn_bias"].set("0.0")
        self._apply_current_queue()

    def _apply_current_queue(self):
        if not self.is_alive:
            return
        with self.lock:
            if self.queue_idx < len(self.experiment_queue):
                p = self.experiment_queue[self.queue_idx]
                self.amp, self.freq, self.phase_step = p["amp"], p["freq"], p["step"]

    def quit(self):
        with self.lock:
            self.is_alive = False
        self.root.quit()
        self.root.destroy()


def video_saver_worker(video_queue, video_path, fps, width, height):
    out = cv2.VideoWriter(video_path, cv2.VideoWriter_fourcc(*"mp4v"), fps, (width, height))
    while True:
        frame = video_queue.get()
        if frame is None:
            break
        out.write(frame)
    out.release()


def run_mujoco(panel: ControlPanel, xml_path="eel.xml"):
    env = EelEnv(xml_path)
    # --- ✅ 完整保留註解部分 ---
    # try:
    #     body_id = env.model.body('base_link').id
    #     actual_mass = env.model.body_mass[body_id]
    #     print("-" * 30)
    #     print(f"【物理數據檢查】")
    #     print(f"Base_link ID: {body_id}")
    #     print(f"MuJoCo 認定重量 (Mass): {actual_mass:.4f} kg")
    #     # 如果你有在 XML 設定 density，可以用這行推算體積
    #     calculated_vol = actual_mass / env.model.opt.density if env.model.opt.density > 0 else 0
    #     print(f"推算排水體積 (Volume): {calculated_vol*1000000:.2f} cm³")
    #     print("-" * 30)
    # except Exception as e:
    #     print(f"檢查重量時出錯: {e}")
    # try:
    #     body_id = env.model.body('link1').id
    #     actual_mass = env.model.body_mass[body_id]
    #     print("-" * 30)
    #     print(f"【物理數據檢查】")
    #     print(f"Base_link ID: {body_id}")
    #     print(f"MuJoCo 認定重量 (Mass): {actual_mass:.4f} kg")
    #     # 如果你有在 XML 設定 density，可以用這行推算體積
    #     calculated_vol = actual_mass / env.model.opt.density if env.model.opt.density > 0 else 0
    #     print(f"推算排水體積 (Volume): {calculated_vol*1000000:.2f} cm³")
    #     print("-" * 30)
    # except Exception as e:
    #     print(f"檢查重量時出錯: {e}")
    # try:
    #     body_id = env.model.body('link6').id
    #     actual_mass = env.model.body_mass[body_id]
    #     print("-" * 30)
    #     print(f"【物理數據檢查】")
    #     print(f"Base_link ID: {body_id}")
    #     print(f"MuJoCo 認定重量 (Mass): {actual_mass:.4f} kg")
    #     # 如果你有在 XML 設定 density，可以用這行推算體積
    #     calculated_vol = actual_mass / env.model.opt.density if env.model.opt.density > 0 else 0
    #     print(f"推算排水體積 (Volume): {calculated_vol*1000000:.2f} cm³")
    #     print("-" * 30)
    # except Exception as e:
    #     print(f"檢查重量時出錯: {e}")
    # --------------------------

    env.reset()
    initial_qpos = np.copy(env.data.qpos)
    trial_speeds, is_waiting, wait_start_time = [], False, 0
    step_counter = 0

    width, height, fps = RENDER_W, RENDER_H, RENDER_FPS
    renderer = mujoco.Renderer(env.model, height=height, width=width)
    video_queue = None
    video_thread = None
    recording_interval = int((1.0 / fps) / env.model.opt.timestep)

    try:
        with mujoco.viewer.launch_passive(env.model, env.data) as viewer:
            viewer.cam.azimuth, viewer.cam.elevation, viewer.cam.distance = 0, -90.0, 13.0
            viewer.cam.lookat = [0, 0, 0]

            # ===== ADD: camera focus / follow (保留原本註解，不刪任何東西) =====
            base_id = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_BODY, "base_link")
            follow_mode = {"on": False}

            def focus_on_eel():
                pos = env.data.xpos[base_id].copy()
                with viewer.lock():
                    viewer.cam.lookat[:] = pos
                    viewer.cam.distance = FOCUS_DIST
                    viewer.cam.elevation = FOCUS_ELEV
                    viewer.cam.azimuth = FOCUS_AZIM

            def toggle_follow():
                follow_mode["on"] = not follow_mode["on"]
                print("[Camera] Follow mode:", "ON" if follow_mode["on"] else "OFF")
                if follow_mode["on"]:
                    focus_on_eel()

            def key_cb(key):
                if key == ord("F"):
                    focus_on_eel()
                elif key == ord("G"):
                    toggle_follow()

            viewer.user_key_callback = key_cb
            # ===== END ADD =====

            while viewer.is_running():
                step_start = time.time()
                with panel.lock:
                    if not panel.is_alive:
                        break
                    p_paused = panel.paused
                    p_amp, p_freq, p_step = panel.amp, panel.freq, panel.phase_step
                    p_auto, p_turn = panel.auto_mode, panel.turn_bias
                    p_wave_type = panel.wave_type
                    p_reset = panel.reset_request
                    cur_results, cur_v_dir, cur_q_idx = panel.results_file, panel.video_dir, panel.queue_idx

                if p_reset:
                    with viewer.lock():
                        mujoco.mj_resetData(env.model, env.data)
                        env.data.qpos[:] = initial_qpos
                        mujoco.mj_forward(env.model, env.data)
                    with panel.lock:
                        panel.reset_request = False
                    trial_speeds = []
                    step_counter = 0
                    print("\n[RESET] Physics state restored.")

                if not p_paused:
                    if not is_waiting:
                        if video_thread is None and p_auto:
                            tag = panel.experiment_queue[cur_q_idx]["tag"]
                            v_name = os.path.join(cur_v_dir, f"{tag}_idx{cur_q_idx}_F{p_freq}_A{p_amp}_S{p_step}.mp4")
                            video_queue = queue.Queue()
                            video_thread = threading.Thread(target=video_saver_worker, args=(video_queue, v_name, fps, width, height))
                            video_thread.start()

                        # --- 游泳控制邏輯 ---
                        t = env.data.time
                        num_j = len(env.data.ctrl)

                        # ✅ steer: 你的 turn_bias（手動模式才用，auto 固定 0）
                        steer = 0.0 if p_auto else p_turn

                        # ===== Steering only on FRONT segments =====
                        # ✅ 只轉前面幾節（可在 CONFIG 調 STEER_FRONT_N_INIT）
                        STEER_FRONT_N = STEER_FRONT_N_INIT

                        # ✅ 轉向強度（可在 CONFIG 調 STEER_GAIN_INIT）
                        STEER_GAIN = STEER_GAIN_INIT

                        # ✅ 讓轉向在前段有「漸弱」，避免太硬造成原地扭
                        #    (頭最強 -> 第 N 節最弱)
                        def front_weight(i):
                            if i >= STEER_FRONT_N:
                                return 0.0
                            if STEER_FRONT_N == 1:
                                return 1.0
                            return 1.0 - 0.7 * (i / (STEER_FRONT_N - 1))

                        ctrl = []
                        for i in range(num_j):
                            # 每節振幅漸增（你原本的設計）
                            amp_i = p_amp * (0.4 + 0.6 * (i/(num_j-1)))

                            # ✅ 只在前 N 節加轉向 offset（像舵）
                            bias_i = STEER_GAIN * steer * front_weight(i)

                            # ✅ 根據 p_wave_type 決定控制律
                            if p_wave_type == "Standing":
                                # 駐波公式：時間項 sin(2*pi*f*t) * 空間包絡 sin(k*x)
                                # i * p_step 在這裡代表空間項 (k*x)
                                u = bias_i + amp_i * np.sin(2 * np.pi * p_freq * t) * np.sin(i * p_step)
                            else:
                                # 行進波公式：sin(2*pi*f*t - k*x)
                                u = bias_i + amp_i * np.sin(2 * np.pi * p_freq * t - i * p_step)

                            ctrl.append(u)

                        with viewer.lock():
                            env.data.ctrl[:] = np.clip(ctrl, CTRL_CLIP_MIN, CTRL_CLIP_MAX)
                            mujoco.mj_step(env.model, env.data)

                        # ✅ 數據獲取
                        speed = np.linalg.norm(env.data.qvel[0:2])
                        z_pos = env.data.qpos[2]
                        passive_z_force = env.data.qfrc_passive[2]

                        # ✅ Z 軸安全監控 (你要求關掉，所以保留註解不刪)
                        # SAFETY_THRESHOLD = 0.5
                        # if abs(z_pos) > SAFETY_THRESHOLD:
                        #     print(f"\n[SAFETY ALERT] Z-Pos detected at {z_pos:.4f}m! Triggering Emergency Reset...")
                        #     with panel.lock:
                        #         panel.paused = True
                        #         panel.reset_request = True
                        #         panel.status_var.set("CRITICAL: Z-HEIGHT LIMIT!")

                        trial_speeds.append(speed)
                        with panel.lock:
                            panel.current_speed = speed
                            panel.current_z = z_pos
                            panel.current_passive_z = passive_z_force

                        # ✅ 即時顯示
                        step_counter += 1
                        if step_counter % 100 == 0:
                            alert = " !! WARNING: Z-POS > 10cm !!" if abs(z_pos) > 0.1 else ""
                            print(f"\r[Monitor] Mode: {p_wave_type} | Z: {z_pos:.4f}m | Passive-Z: {passive_z_force:.4f}N{alert}", end="")

                        # 錄影
                        if video_thread is not None:
                            sim_steps = int(t / env.model.opt.timestep)
                            if sim_steps % recording_interval == 0:
                                renderer.update_scene(env.data, camera=viewer.cam)
                                frame = cv2.cvtColor(renderer.render(), cv2.COLOR_RGB2BGR)
                                video_queue.put(frame)

                        # 碰撞判定
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
                            print(f"\n[Goal Reached] Final Z: {z_pos:.4f} m | Final Buoyancy: {passive_z_force:.4f} N")
                            if video_thread is not None:
                                video_queue.put(None)
                                video_thread = None

                            avg_s = np.mean(trial_speeds) if trial_speeds else 0
                            y_off = abs(env.data.qpos[1])
                            with panel.lock:
                                tag = panel.experiment_queue[panel.queue_idx]["tag"] if p_auto else "Manual"

                            with open(cur_results, "a", newline="") as f:
                                csv.writer(f).writerow([
                                    time.strftime("%H:%M:%S"), f"{tag}_{p_wave_type}", p_amp, p_freq, p_step,
                                    f"{avg_s:.4f}", f"{y_off:.2f}", f"{z_pos:.4f}", y_off < 1.0
                                ])
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
                                panel._update_status_ui_text(panel.wave_type)

                            panel._apply_current_queue()
                            with viewer.lock():
                                mujoco.mj_resetData(env.model, env.data)
                                env.data.qpos[:] = initial_qpos
                                mujoco.mj_forward(env.model, env.data)
                            trial_speeds, is_waiting = [], False
                            step_counter = 0

                # ===== ADD: camera follow update =====
                if follow_mode["on"]:
                    pos = env.data.xpos[base_id]
                    with viewer.lock():
                        viewer.cam.lookat[:] = pos
                # ===== END ADD =====

                viewer.sync()
                dt = env.model.opt.timestep
                elapsed = time.time() - step_start
                if dt > elapsed:
                    time.sleep(dt - elapsed)
    finally:
        if video_thread is not None:
            video_queue.put(None)
        renderer.close()


if __name__ == "__main__":
    p = ControlPanel()
    threading.Thread(target=run_mujoco, args=(p, "eel.xml"), daemon=True).start()
    p.root.mainloop()
