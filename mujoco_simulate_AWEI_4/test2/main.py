# main.py
import time
import threading
import tkinter as tk
from tkinter import ttk
import numpy as np
import mujoco
import mujoco.viewer
import cv2
import os

from eel_env import EelEnv
from swimmers.base import SwimParams
from swimmers import LegacySwimmer, CPGSwimmer, KuramotoSwimmer

from recorder import AsyncVideoRecorder


# ============================================================
# CONFIG：主程式只放 UI/錄影/相機/初始參數
# ============================================================
AMP_INIT  = 0.50
FREQ_INIT = 1.00
STEP_INIT = 0.50

TURN_BIAS_INIT     = 0.00
BIAS_STEP_INIT     = 0.05
BIAS_MAX_INIT      = 0.80
BIAS_PRESET_INIT   = 0.30

# Legacy steering
STEER_FRONT_N_INIT = 4
STEER_GAIN_INIT    = 0.70

CTRL_CLIP_MIN = -1.2
CTRL_CLIP_MAX =  1.2

# camera (Follow 模式用這個視角)
FOCUS_DIST = 1.9
FOCUS_ELEV = -75.0
FOCUS_AZIM = 0.0

# video render
RENDER_W, RENDER_H, RENDER_FPS = 640, 480, 30


# ============================================================
# ===== SWEEP CONFIG（你要調 sweep 在這裡改）=====
# ============================================================
SWEEP_ENABLE_DEFAULT = False

TRIAL_MAX_SEC = 12.0
GOAL_X_THRESHOLD = 4.85  # 備用（你也可不用）

# Sweep 參數：Amp / Freq / Offset(phase rad)
SWEEP_AMPS  = [0.35, 0.50]
SWEEP_FREQS = [0.8, 1.0, 1.2]

# offset 定義為「相位偏移（rad）」：0, 90°, 180° ...
SWEEP_PHASE_OFFSETS = [0.0, np.pi/2, np.pi]

# 如果你還想 sweep step，也保留（可選）
SWEEP_STEPS = [0.40, 0.50, 0.60]

SWEEP_TURN_BIAS = 0.0

# output dir（會自動建 subdir：videos_sweep/Legacy/..., Kuramoto/..., CPG/...）
SWEEP_VIDEO_DIR_BASE = "videos_sweep"

# Sweep 一開始是否自動開 Follow Cam
SWEEP_AUTO_FOLLOW_ON_START = True
# ============================================================


def build_sweep_cases(algo: str):
    """只針對單一 algo 建 cases，檔名用 algo 當 prefix。"""
    cases = []
    idx = 0
    for a in SWEEP_AMPS:
        for f in SWEEP_FREQS:
            for ph in SWEEP_PHASE_OFFSETS:
                for s in SWEEP_STEPS:
                    cases.append({
                        "idx": idx,
                        "algo": algo,
                        "wave": "Traveling",
                        "amp": float(a),
                        "freq": float(f),
                        "phase_offset": float(ph),     # rad
                        "step": float(s),
                        "turn": float(SWEEP_TURN_BIAS),
                    })
                    idx += 1
    return cases


def phase_offset_to_time_offset(phase_offset_rad: float, freq_hz: float) -> float:
    """把 phase(rad) 轉成時間偏移(sec)：t += phase/(2πf)。freq=0 時回 0。"""
    if freq_hz <= 1e-9:
        return 0.0
    return float(phase_offset_rad) / (2.0 * np.pi * float(freq_hz))


class ControlPanel:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Eel Control - Swimmer Plugins")
        self.lock = threading.Lock()
        self.vars = {}

        # sim state
        self.paused = True
        self.wave_type = "Traveling"  # Traveling / Standing
        self.swim_mode = "Legacy"     # Legacy / Kuramoto / CPG
        self.reset_request = False

        # camera flags
        self.cam_focus_req = False
        self.cam_follow_toggle_req = False

        # recorder flags
        self.rec_toggle_req = False
        self.rec_on = False

        # ===== SWEEP =====
        self.sweep_toggle_req = False
        self.sweep_on = SWEEP_ENABLE_DEFAULT
        self.sweep_status = "SWEEP: OFF"

        # 一鍵開始 sweep（用當下演算法）
        self.sweep_start_current_req = False
        # ==================

        # telemetry
        self.current_speed = 0.0
        self.current_z = 0.0
        self.current_passive_z = 0.0
        self.is_alive = True

        # params
        self.amp, self.freq, self.turn_bias, self.phase_step = AMP_INIT, FREQ_INIT, TURN_BIAS_INIT, STEP_INIT
        self.bias_step = BIAS_STEP_INIT
        self.bias_max = BIAS_MAX_INIT
        self.bias_preset = BIAS_PRESET_INIT

        # Motor Failure toggles
        self.motor_on = [True] * 6
        self.motor_vars = [tk.BooleanVar(value=True) for _ in range(6)]

        # UI radio vars
        self.wave_var = tk.StringVar(value="行進波")   # 行進波 / 駐波
        self.mode_var = tk.StringVar(value="Legacy")  # Legacy / Kuramoto / CPG

        self._setup_ui()
        self._bind_keys()
        self.root.protocol("WM_DELETE_WINDOW", self.quit)

    def _on_wave_radio(self):
        with self.lock:
            self.wave_type = "Traveling" if self.wave_var.get() == "行進波" else "Standing"

    def _on_mode_radio(self):
        with self.lock:
            v = self.mode_var.get()
            if v in ("Legacy", "Kuramoto", "CPG"):
                self.swim_mode = v

    def _on_motor_toggle(self):
        with self.lock:
            self.motor_on = [v.get() for v in self.motor_vars]

    def trigger_reset(self):
        with self.lock:
            self.reset_request = True

    def request_focus_cam(self):
        with self.lock:
            self.cam_focus_req = True

    def request_toggle_follow_cam(self):
        with self.lock:
            self.cam_follow_toggle_req = True

    def request_toggle_record(self):
        with self.lock:
            self.rec_toggle_req = True

    def request_toggle_sweep(self):
        with self.lock:
            self.sweep_toggle_req = True

    def request_start_sweep_current_algo(self):
        with self.lock:
            self.sweep_start_current_req = True

    def toggle_pause(self):
        with self.lock:
            self.paused = not self.paused

    def _setup_ui(self):
        frm = ttk.Frame(self.root, padding=15)
        frm.grid(row=0, column=0, sticky="nsew")

        self.status_var = tk.StringVar(value="狀態：PAUSED")
        self.speed_var = tk.StringVar(value="Speed: 0.000 m/s")
        self.z_var = tk.StringVar(value="Z-Pos: 0.000 m")
        self.pass_z_var = tk.StringVar(value="Passive-Z: 0.000 N")
        self.sweep_var = tk.StringVar(value=self.sweep_status)

        top = ttk.Frame(frm)
        top.grid(row=0, column=0, columnspan=2, sticky="ew", pady=(0, 8))
        top.grid_columnconfigure(0, weight=1)

        ttk.Label(top, textvariable=self.status_var, font=("Segoe UI", 12, "bold")).grid(row=0, column=0, sticky="w")
        ttk.Button(top, text="Run / Pause (Space)", command=self.toggle_pause).grid(row=0, column=1, padx=4, sticky="e")
        ttk.Button(top, text="Reset Cam (F)", command=self.request_focus_cam).grid(row=0, column=2, padx=4, sticky="e")
        ttk.Button(top, text="Follow Cam (G)", command=self.request_toggle_follow_cam).grid(row=0, column=3, padx=4, sticky="e")
        ttk.Button(top, text="Record (V)", command=self.request_toggle_record).grid(row=0, column=4, padx=4, sticky="e")
        ttk.Button(top, text="Auto Sweep (P)", command=self.request_toggle_sweep).grid(row=0, column=5, padx=4, sticky="e")
        ttk.Button(top, text="Sweep Current Algo (T)", command=self.request_start_sweep_current_algo).grid(row=0, column=6, padx=4, sticky="e")

        ttk.Label(top, textvariable=self.sweep_var, foreground="darkgreen").grid(row=1, column=0, sticky="w", pady=(4, 0))

        tel = ttk.Frame(frm)
        tel.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(0, 10))
        ttk.Label(tel, textvariable=self.speed_var, font=("Consolas", 11), foreground="blue").grid(row=0, column=0, padx=(0, 14), sticky="w")
        ttk.Label(tel, textvariable=self.z_var, font=("Consolas", 11), foreground="green").grid(row=0, column=1, padx=(0, 14), sticky="w")
        ttk.Label(tel, textvariable=self.pass_z_var, font=("Consolas", 11), foreground="purple").grid(row=0, column=2, sticky="w")

        wave_frm = ttk.LabelFrame(frm, text="Wave Type", padding=10)
        wave_frm.grid(row=2, column=0, columnspan=2, sticky="ew", pady=6)
        ttk.Radiobutton(wave_frm, text="駐波 (Standing)", value="駐波",
                        variable=self.wave_var, command=self._on_wave_radio).grid(row=0, column=0, padx=6, sticky="w")
        ttk.Radiobutton(wave_frm, text="行進波 (Traveling)", value="行進波",
                        variable=self.wave_var, command=self._on_wave_radio).grid(row=0, column=1, padx=6, sticky="w")

        algo_frm = ttk.LabelFrame(frm, text="Algorithm", padding=10)
        algo_frm.grid(row=3, column=0, columnspan=2, sticky="ew", pady=6)
        ttk.Radiobutton(algo_frm, text="Legacy", value="Legacy",
                        variable=self.mode_var, command=self._on_mode_radio).grid(row=0, column=0, padx=6, sticky="w")
        ttk.Radiobutton(algo_frm, text="Kuramoto", value="Kuramoto",
                        variable=self.mode_var, command=self._on_mode_radio).grid(row=0, column=1, padx=6, sticky="w")
        ttk.Radiobutton(algo_frm, text="CPG", value="CPG",
                        variable=self.mode_var, command=self._on_mode_radio).grid(row=0, column=2, padx=6, sticky="w")

        turn_frm = ttk.LabelFrame(frm, text="Steering (Manual)", padding=10)
        turn_frm.grid(row=4, column=0, columnspan=2, sticky="ew", pady=6)
        ttk.Button(turn_frm, text="⟵ Left (A/←)", command=self.turn_left).grid(row=0, column=0, sticky="ew", padx=4)
        ttk.Button(turn_frm, text="Straight (S)", command=self.turn_straight).grid(row=0, column=1, sticky="ew", padx=4)
        ttk.Button(turn_frm, text="Right (D/→) ⟶", command=self.turn_right).grid(row=0, column=2, sticky="ew", padx=4)

        ttk.Button(turn_frm, text="Bias - (Q)", command=lambda: self.nudge_bias(-self.bias_step)).grid(row=1, column=0, sticky="ew", padx=4, pady=4)
        ttk.Button(turn_frm, text="Bias = 0 (E)", command=self.turn_straight).grid(row=1, column=1, sticky="ew", padx=4, pady=4)
        ttk.Button(turn_frm, text="Bias + (W)", command=lambda: self.nudge_bias(+self.bias_step)).grid(row=1, column=2, sticky="ew", padx=4, pady=4)

        self.bias_slider = tk.DoubleVar(value=self.turn_bias)
        bias_scale = ttk.Scale(turn_frm, from_=-self.bias_max, to=self.bias_max,
                               variable=self.bias_slider, command=self._on_bias_slider)
        bias_scale.grid(row=2, column=0, columnspan=3, sticky="ew", pady=(6, 2))
        self.bias_label = tk.StringVar(value=f"turn_bias = {self.turn_bias:+.3f}")
        ttk.Label(turn_frm, textvariable=self.bias_label, font=("Consolas", 10)).grid(row=3, column=0, columnspan=3, sticky="w")
        for c in range(3):
            turn_frm.grid_columnconfigure(c, weight=1)

        motor_frm = ttk.LabelFrame(frm, text="Motor Failure (Disable joints)", padding=10)
        motor_frm.grid(row=5, column=0, columnspan=2, sticky="ew", pady=6)
        for i in range(6):
            cb = ttk.Checkbutton(motor_frm, text=f"link{i+1}",
                                 variable=self.motor_vars[i], command=self._on_motor_toggle)
            cb.grid(row=0, column=i, padx=4, sticky="w")
        for c in range(6):
            motor_frm.grid_columnconfigure(c, weight=1)

        param_frm = ttk.LabelFrame(frm, text="Params", padding=10)
        param_frm.grid(row=6, column=0, columnspan=2, sticky="ew", pady=6)

        attrs = [("Amp", "amp"), ("Freq", "freq"), ("Bias (turn_bias)", "turn_bias"), ("Step/Wavenumber", "phase_step")]
        for i, (label, attr) in enumerate(attrs):
            ttk.Label(param_frm, text=label).grid(row=i, column=0, sticky="w")
            var = tk.StringVar(value=str(getattr(self, attr)))
            self.vars[attr] = var
            ent = ttk.Entry(param_frm, textvariable=var, width=12)
            ent.grid(row=i, column=1, sticky="w", pady=2, padx=(6, 0))
            ent.bind("<Return>", lambda e: self._manual_update())

        ttk.Button(param_frm, text="Apply Params (Enter)", command=self._manual_update).grid(row=0, column=2, rowspan=2, padx=8, sticky="ns")
        ttk.Button(param_frm, text="Reset Physics (R)", command=self.trigger_reset).grid(row=2, column=2, rowspan=2, padx=8, sticky="ns")

        help_txt = (
            "Keys: Space=Run/Pause, A/←=Left, D/→=Right, S=Straight, Q/W=bias-,+, E=bias=0, "
            "R=Reset, F=ResetCam, G=FollowCam, V=Record, P=AutoSweep(toggle), T=SweepCurrentAlgo(start)"
        )
        ttk.Label(frm, text=help_txt, foreground="gray").grid(row=7, column=0, columnspan=2, pady=(6, 0), sticky="w")

        self._update_ui_loop()

    def _bind_keys(self):
        self.root.focus_force()
        self.root.bind("<space>", lambda e: self.toggle_pause())
        self.root.bind("r", lambda e: self.trigger_reset())

        self.root.bind("a", lambda e: self.turn_left())
        self.root.bind("<Left>", lambda e: self.turn_left())
        self.root.bind("d", lambda e: self.turn_right())
        self.root.bind("<Right>", lambda e: self.turn_right())
        self.root.bind("s", lambda e: self.turn_straight())
        self.root.bind("q", lambda e: self.nudge_bias(-self.bias_step))
        self.root.bind("w", lambda e: self.nudge_bias(+self.bias_step))
        self.root.bind("e", lambda e: self.turn_straight())

        self.root.bind("f", lambda e: self.request_focus_cam())
        self.root.bind("g", lambda e: self.request_toggle_follow_cam())

        self.root.bind("v", lambda e: self.request_toggle_record())
        self.root.bind("p", lambda e: self.request_toggle_sweep())
        self.root.bind("t", lambda e: self.request_start_sweep_current_algo())

    def _manual_update(self):
        with self.lock:
            try:
                self.amp = float(self.vars["amp"].get())
                self.freq = float(self.vars["freq"].get())
                self.turn_bias = float(self.vars["turn_bias"].get())
                self.phase_step = float(self.vars["phase_step"].get())
            except ValueError:
                pass
            self.turn_bias = float(np.clip(self.turn_bias, -self.bias_max, self.bias_max))
            self.bias_slider.set(self.turn_bias)
            self.bias_label.set(f"turn_bias = {self.turn_bias:+.3f}")

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
                cur_swim = self.swim_mode
                paused = self.paused
                rec = "REC" if self.rec_on else "NO-REC"
                self.bias_label.set(f"turn_bias = {self.turn_bias:+.3f}")
                self.sweep_var.set(self.sweep_status)

            self.wave_var.set("行進波" if cur_type == "Traveling" else "駐波")
            self.mode_var.set(cur_swim)

            self.status_var.set(f"狀態：{'PAUSED' if paused else 'RUNNING'} | Wave:{cur_type} | Algo:{cur_swim} | {rec}")
            self.speed_var.set(f"Speed: {cur_s:.3f} m/s")
            self.z_var.set(f"Z-Pos: {cur_z:.3f} m")
            self.pass_z_var.set(f"Passive-Z: {cur_pz:.3f} N")

            self.root.after(100, self._update_ui_loop)
        except:
            pass

    def quit(self):
        with self.lock:
            self.is_alive = False
        self.root.quit()
        self.root.destroy()


def run_mujoco(panel: ControlPanel, xml_path="eel.xml"):
    env = EelEnv(xml_path)
    env.reset()
    initial_qpos = np.copy(env.data.qpos)

    renderer = mujoco.Renderer(env.model, height=RENDER_H, width=RENDER_W)

    # ===== 手動錄影（獨立一支，不影響 sweep）=====
    manual_recorder = AsyncVideoRecorder(out_dir="videos", fps=RENDER_FPS)
    recording_interval = max(1, int((1.0 / RENDER_FPS) / env.model.opt.timestep))

    # ===== SWEEP runtime states（每個 case 一支影片）=====
    sweep_case_recorder = None  # ✅ 每個 case 會 new 一次
    sweep_cases = []
    sweep_idx = 0
    sweep_trial_active = False
    sweep_trial_start_walltime = 0.0
    sweep_algo_locked = None
    # ================================================

    legacy = LegacySwimmer(steer_front_n=STEER_FRONT_N_INIT, steer_gain=STEER_GAIN_INIT)
    kuramoto = KuramotoSwimmer(
        coupling=10.0,
        substeps=5,
        taper_head=0.35,
        taper_tail=1.0,
        steer_gain=0.70,
        steer_front_n=STEER_FRONT_N_INIT,
        steer_sign=1.0,
    )
    cpg = CPGSwimmer(steer_front_n=STEER_FRONT_N_INIT)
    swimmers = {"Legacy": legacy, "Kuramoto": kuramoto, "CPG": cpg}

    try:
        with mujoco.viewer.launch_passive(env.model, env.data) as viewer:
            viewer.cam.azimuth, viewer.cam.elevation, viewer.cam.distance = 0, -90.0, 13.0
            viewer.cam.lookat = [0, 0, 0]

            cam_origin = {
                "lookat": np.array(viewer.cam.lookat, dtype=float),
                "distance": float(viewer.cam.distance),
                "elevation": float(viewer.cam.elevation),
                "azimuth": float(viewer.cam.azimuth),
            }

            base_id = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_BODY, "base_link")
            follow_mode = {"on": False}

            # 如果你的 XML 沒這些 geom name，會報錯：請自行改成你的 geom 名稱
            geom_base = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_GEOM, "base_link_collision")
            geom_wall = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_GEOM, "wall_front")

            def restore_camera_origin():
                with viewer.lock():
                    viewer.cam.lookat[:] = cam_origin["lookat"]
                    viewer.cam.distance  = cam_origin["distance"]
                    viewer.cam.elevation = cam_origin["elevation"]
                    viewer.cam.azimuth   = cam_origin["azimuth"]

            def toggle_follow():
                follow_mode["on"] = not follow_mode["on"]
                print("[Camera] Follow mode:", "ON" if follow_mode["on"] else "OFF")

            def key_cb(key):
                if key == ord("F"):
                    follow_mode["on"] = False
                    restore_camera_origin()
                    print("[Camera] Restore original view (follow OFF)")
                elif key == ord("G"):
                    toggle_follow()

            viewer.user_key_callback = key_cb

            num_j = len(env.data.ctrl)
            for sw in swimmers.values():
                sw.reset(num_j)
                if hasattr(sw, "set_dt"):
                    sw.set_dt(env.model.opt.timestep)

            kp_default = np.copy(env.model.actuator_gainprm[:, 0])

            def hard_reset_and_forward():
                with viewer.lock():
                    mujoco.mj_resetData(env.model, env.data)
                    env.data.qpos[:] = initial_qpos
                    mujoco.mj_forward(env.model, env.data)

            def check_goal_contact():
                if env.data.ncon <= 0:
                    return False
                for i in range(env.data.ncon):
                    con = env.data.contact[i]
                    g1 = con.geom1
                    g2 = con.geom2
                    if (g1 == geom_base and g2 == geom_wall) or (g1 == geom_wall and g2 == geom_base):
                        return True
                return False

            # ==========================
            # SWEEP control functions
            # ==========================
            def start_sweep_for_algo(algo_name: str):
                nonlocal sweep_case_recorder, sweep_cases, sweep_idx, sweep_trial_active, sweep_trial_start_walltime, sweep_algo_locked

                sweep_algo_locked = algo_name
                sweep_cases = build_sweep_cases(algo_name)
                sweep_idx = 0
                sweep_trial_active = False
                sweep_trial_start_walltime = 0.0

                # 保險：若之前殘留錄影，先收掉
                if sweep_case_recorder is not None:
                    sweep_case_recorder.stop()
                    sweep_case_recorder = None

                with panel.lock:
                    panel.sweep_on = True
                    panel.paused = False
                    panel.sweep_status = f"SWEEP: ON [{algo_name}] (0/{len(sweep_cases)})"

                print(f"[SWEEP] START algo={algo_name}, total cases={len(sweep_cases)}")

                if SWEEP_AUTO_FOLLOW_ON_START:
                    follow_mode["on"] = True
                    pos = env.data.xpos[base_id].copy()
                    with viewer.lock():
                        viewer.cam.lookat[:] = pos
                        viewer.cam.distance  = FOCUS_DIST
                        viewer.cam.elevation = FOCUS_ELEV
                        viewer.cam.azimuth   = FOCUS_AZIM
                    print("[Camera] Sweep start -> Follow ON + Close view")

            def stop_sweep():
                nonlocal sweep_case_recorder, sweep_trial_active, sweep_algo_locked
                if sweep_case_recorder is not None:
                    sweep_case_recorder.stop()
                    sweep_case_recorder = None
                sweep_trial_active = False
                sweep_algo_locked = None
                with panel.lock:
                    panel.sweep_on = False
                    panel.sweep_status = "SWEEP: OFF"
                print("[SWEEP] STOP")

            # ==========================
            # main loop
            # ==========================
            while viewer.is_running():
                step_start = time.time()

                with panel.lock:
                    if not panel.is_alive:
                        break

                    p_paused = panel.paused
                    p_amp, p_freq, p_step = panel.amp, panel.freq, panel.phase_step
                    p_turn = panel.turn_bias
                    p_wave_type = panel.wave_type
                    p_swim_mode = panel.swim_mode
                    p_reset = panel.reset_request
                    p_motor_on = list(panel.motor_on)

                    p_cam_focus = panel.cam_focus_req
                    p_cam_follow_toggle = panel.cam_follow_toggle_req

                    p_rec_toggle = panel.rec_toggle_req

                    p_sweep_toggle = panel.sweep_toggle_req
                    p_sweep_on = panel.sweep_on
                    p_sweep_start_current = panel.sweep_start_current_req

                # camera controls
                if p_cam_focus:
                    follow_mode["on"] = False
                    restore_camera_origin()
                    with panel.lock:
                        panel.cam_focus_req = False

                if p_cam_follow_toggle:
                    toggle_follow()
                    with panel.lock:
                        panel.cam_follow_toggle_req = False

                # T：一鍵開始 sweep（依當下演算法）
                if p_sweep_start_current:
                    with panel.lock:
                        panel.sweep_start_current_req = False
                    stop_sweep()
                    start_sweep_for_algo(p_swim_mode)

                # P：toggle sweep
                if p_sweep_toggle:
                    with panel.lock:
                        panel.sweep_toggle_req = False
                        panel.sweep_on = not panel.sweep_on
                        p_sweep_on = panel.sweep_on

                    if p_sweep_on:
                        start_sweep_for_algo(p_swim_mode)
                    else:
                        stop_sweep()

                # 手動錄影 toggle（跟 sweep 完全獨立）
                if p_rec_toggle:
                    with panel.lock:
                        panel.rec_toggle_req = False
                        panel.rec_on = not panel.rec_on
                        rec_on = panel.rec_on

                    if rec_on:
                        fname = (
                            f"{time.strftime('%Y%m%d_%H%M%S')}_"
                            f"{p_swim_mode}_{p_wave_type}_"
                            f"A{p_amp:.2f}_F{p_freq:.2f}_S{p_step:.2f}_B{p_turn:+.2f}.mp4"
                        )
                        manual_recorder.start(width=RENDER_W, height=RENDER_H, filename=fname)
                    else:
                        manual_recorder.stop()

                if p_reset:
                    hard_reset_and_forward()
                    with panel.lock:
                        panel.reset_request = False
                    for sw in swimmers.values():
                        sw.reset(num_j)
                    print("\n[RESET] Physics state restored.")

                # motor failure (kp=0)
                n = min(6, env.model.nu, kp_default.shape[0])
                for i in range(n):
                    env.model.actuator_gainprm[i, 0] = kp_default[i] if p_motor_on[i] else 0.0

                # ============================================================
                # SWEEP state machine（每個 case 一支影片）
                # ============================================================
                if p_sweep_on and sweep_algo_locked is not None:
                    if sweep_idx >= len(sweep_cases):
                        # sweep 結束
                        if sweep_case_recorder is not None:
                            sweep_case_recorder.stop()
                            sweep_case_recorder = None

                        with panel.lock:
                            panel.sweep_on = False
                            panel.sweep_status = f"SWEEP: DONE [{sweep_algo_locked}]"
                            panel.paused = True
                        print("[SWEEP] DONE")
                        sweep_algo_locked = None
                    else:
                        if not sweep_trial_active:
                            case = sweep_cases[sweep_idx]
                            sweep_trial_active = True
                            sweep_trial_start_walltime = time.time()

                            # UI 顯示當下 case（演算法鎖定）
                            with panel.lock:
                                panel.amp = case["amp"]
                                panel.freq = case["freq"]
                                panel.phase_step = case["step"]
                                panel.turn_bias = case["turn"]
                                panel.swim_mode = case["algo"]
                                panel.wave_type = case["wave"]

                                panel.vars["amp"].set(f"{case['amp']:.3f}")
                                panel.vars["freq"].set(f"{case['freq']:.3f}")
                                panel.vars["phase_step"].set(f"{case['step']:.3f}")
                                panel.vars["turn_bias"].set(f"{case['turn']:.3f}")
                                panel.mode_var.set(case["algo"])
                                panel.wave_var.set("行進波")

                                panel.sweep_status = (
                                    f"SWEEP: ON [{case['algo']}] ({sweep_idx+1}/{len(sweep_cases)}) "
                                    f"A{case['amp']:.2f} F{case['freq']:.2f} off{case['phase_offset']:.2f}rad"
                                )
                                panel.paused = False

                            hard_reset_and_forward()
                            for sw in swimmers.values():
                                sw.reset(num_j)

                            # ✅ 每個 case new 一個 recorder（獨立 thread / writer / 檔案）
                            out_dir = os.path.join(SWEEP_VIDEO_DIR_BASE, case["algo"])
                            sweep_case_recorder = AsyncVideoRecorder(out_dir=out_dir, fps=RENDER_FPS)

                            fname = (
                                f"{case['algo']}_idx{case['idx']:04d}_"
                                f"A{case['amp']:.2f}_F{case['freq']:.2f}_"
                                f"OFF{case['phase_offset']:.2f}rad_S{case['step']:.2f}_B{case['turn']:+.2f}.mp4"
                            )
                            sweep_case_recorder.start(width=RENDER_W, height=RENDER_H, filename=fname)
                            print("[SWEEP] START CASE:", case)

                        else:
                            # 讀出目前 panel 的參數
                            with panel.lock:
                                p_amp = panel.amp
                                p_freq = panel.freq
                                p_step = panel.phase_step
                                p_turn = panel.turn_bias
                                p_swim_mode = panel.swim_mode
                                p_wave_type = panel.wave_type

                            case = sweep_cases[sweep_idx]
                            phase_offset = case["phase_offset"]

                            swimmer = swimmers.get(p_swim_mode, legacy)
                            if hasattr(swimmer, "set_dt"):
                                swimmer.set_dt(env.model.opt.timestep)

                            t = env.data.time
                            t_eff = t + phase_offset_to_time_offset(phase_offset, p_freq)

                            sp = SwimParams(
                                amp=p_amp, freq=p_freq, step=p_step, turn=p_turn,
                                wave_type=p_wave_type, auto_mode=True
                            )
                            ctrl = swimmer.compute_ctrl(t=t_eff, num_joints=num_j, p=sp)

                            with viewer.lock():
                                env.data.ctrl[:] = np.clip(ctrl, CTRL_CLIP_MIN, CTRL_CLIP_MAX)
                                mujoco.mj_step(env.model, env.data)

                            speed = np.linalg.norm(env.data.qvel[0:2])
                            z_pos = env.data.qpos[2]
                            passive_z_force = env.data.qfrc_passive[2]
                            with panel.lock:
                                panel.current_speed = speed
                                panel.current_z = z_pos
                                panel.current_passive_z = passive_z_force

                            # record frames（sweep case）
                            if sweep_case_recorder is not None and sweep_case_recorder.is_recording():
                                sim_steps = int(env.data.time / env.model.opt.timestep)
                                if sim_steps % recording_interval == 0:
                                    renderer.update_scene(env.data, camera=viewer.cam)
                                    frame = renderer.render()
                                    frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
                                    sweep_case_recorder.push(frame_bgr)

                            # end conditions
                            collided = check_goal_contact()
                            x_pos = float(env.data.qpos[0])
                            timeout = (time.time() - sweep_trial_start_walltime) > TRIAL_MAX_SEC
                            reach_x = x_pos > GOAL_X_THRESHOLD

                            if collided or timeout or reach_x:
                                reason = "COLLISION" if collided else ("X_REACH" if reach_x else "TIMEOUT")
                                print(f"[SWEEP] END CASE idx={sweep_idx} reason={reason} x={x_pos:.3f}")

                                if sweep_case_recorder is not None:
                                    sweep_case_recorder.stop()
                                    sweep_case_recorder = None

                                sweep_idx += 1
                                sweep_trial_active = False

                                hard_reset_and_forward()
                                for sw in swimmers.values():
                                    sw.reset(num_j)

                # ============================================================
                # normal manual (non-sweep)
                # ============================================================
                if (not p_sweep_on) and (not p_paused):
                    swimmer = swimmers.get(p_swim_mode, legacy)
                    if hasattr(swimmer, "set_dt"):
                        swimmer.set_dt(env.model.opt.timestep)

                    sp = SwimParams(
                        amp=p_amp, freq=p_freq, step=p_step, turn=p_turn,
                        wave_type=p_wave_type, auto_mode=False
                    )
                    ctrl = swimmer.compute_ctrl(t=env.data.time, num_joints=num_j, p=sp)

                    with viewer.lock():
                        env.data.ctrl[:] = np.clip(ctrl, CTRL_CLIP_MIN, CTRL_CLIP_MAX)
                        mujoco.mj_step(env.model, env.data)

                    speed = np.linalg.norm(env.data.qvel[0:2])
                    z_pos = env.data.qpos[2]
                    passive_z_force = env.data.qfrc_passive[2]
                    with panel.lock:
                        panel.current_speed = speed
                        panel.current_z = z_pos
                        panel.current_passive_z = passive_z_force

                    # record frames（手動錄影）
                    if manual_recorder.is_recording():
                        sim_steps = int(env.data.time / env.model.opt.timestep)
                        if sim_steps % recording_interval == 0:
                            renderer.update_scene(env.data, camera=viewer.cam)
                            frame = renderer.render()
                            frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
                            manual_recorder.push(frame_bgr)

                # camera follow
                if follow_mode["on"]:
                    pos = env.data.xpos[base_id]
                    with viewer.lock():
                        viewer.cam.lookat[:] = pos
                        viewer.cam.distance  = FOCUS_DIST
                        viewer.cam.elevation = FOCUS_ELEV
                        viewer.cam.azimuth   = FOCUS_AZIM

                viewer.sync()
                dt = env.model.opt.timestep
                elapsed = time.time() - step_start
                if dt > elapsed:
                    time.sleep(dt - elapsed)

    finally:
        manual_recorder.stop()
        if sweep_case_recorder is not None:
            sweep_case_recorder.stop()
        renderer.close()


if __name__ == "__main__":
    p = ControlPanel()
    threading.Thread(target=run_mujoco, args=(p, "eel.xml"), daemon=True).start()
    p.root.mainloop()