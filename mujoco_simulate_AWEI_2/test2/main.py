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
from swimmers.base import SwimParams
from swimmers import LegacySwimmer, CPGSwimmer, KuramotoSwimmer


# ============================================================
# ✅ CONFIG：主程式只放 UI/錄影/相機/初始參數
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

# camera
FOCUS_DIST = 4.5
FOCUS_ELEV = -60.0
FOCUS_AZIM = 0.0

# video
RENDER_W, RENDER_H, RENDER_FPS = 640, 480, 30


class ControlPanel:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Eel Control - Swimmer Plugins")
        self.lock = threading.Lock()
        self.vars = {}

        self.paused = True
        self.auto_mode = False
        self.wave_type = "Traveling"  # Traveling / Standing

        # ✅ 現在游法不在主程式：主程式只切 swimmer
        self.swim_mode = "Legacy"      # Legacy / CPG

        self.reset_request = False
        self.current_speed = 0.0
        self.current_z = 0.0
        self.current_passive_z = 0.0
        self.is_alive = True

        self.session_dir = ""
        self.video_dir = ""
        self.results_file = "eel_single_factor_results.csv"

        self.amp, self.freq, self.turn_bias, self.phase_step = AMP_INIT, FREQ_INIT, TURN_BIAS_INIT, STEP_INIT

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
        ttk.Button(btn_frm, text="Wave Type (T)", command=self.toggle_wave_type).pack(side="left", fill="x", expand=True)
        ttk.Button(btn_frm, text="Swim Mode (M)", command=self.toggle_swim_mode).pack(side="left", fill="x", expand=True)
        ttk.Button(btn_frm, text="Reset Physics (R)", command=self.trigger_reset).pack(side="left", fill="x", expand=True)
        ttk.Button(btn_frm, text="Apply Params (Enter)", command=self._manual_update).pack(side="left", fill="x", expand=True)

        # --- 轉向控制 ---
        turn_frm = ttk.LabelFrame(frm, text="Steering (Manual)", padding=10)
        turn_frm.grid(row=3, column=0, columnspan=2, sticky="ew", pady=6)

        ttk.Button(turn_frm, text="⟵ Left (A/←)", command=self.turn_left).grid(row=0, column=0, sticky="ew", padx=4)
        ttk.Button(turn_frm, text="Straight (S)", command=self.turn_straight).grid(row=0, column=1, sticky="ew", padx=4)
        ttk.Button(turn_frm, text="Right (D/→) ⟶", command=self.turn_right).grid(row=0, column=2, sticky="ew", padx=4)

        ttk.Button(turn_frm, text="Bias - (Q)", command=lambda: self.nudge_bias(-self.bias_step)).grid(row=1, column=0, sticky="ew", padx=4, pady=4)
        ttk.Button(turn_frm, text="Bias = 0 (E)", command=self.turn_straight).grid(row=1, column=1, sticky="ew", padx=4, pady=4)
        ttk.Button(turn_frm, text="Bias + (W)", command=lambda: self.nudge_bias(+self.bias_step)).grid(row=1, column=2, sticky="ew", padx=4, pady=4)

        self.bias_slider = tk.DoubleVar(value=self.turn_bias)
        bias_scale = ttk.Scale(
            turn_frm, from_=-self.bias_max, to=self.bias_max,
            variable=self.bias_slider, command=self._on_bias_slider
        )
        bias_scale.grid(row=2, column=0, columnspan=3, sticky="ew", pady=(6, 2))
        self.bias_label = tk.StringVar(value=f"turn_bias = {self.turn_bias:+.3f}")
        ttk.Label(turn_frm, textvariable=self.bias_label, font=("Consolas", 10)).grid(row=3, column=0, columnspan=3, sticky="w")
        for c in range(3):
            turn_frm.grid_columnconfigure(c, weight=1)

        # --- 參數輸入 ---
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
            "Q/W=Nudge bias -, +, E=Zero, T=Wave Type, M=Swim Mode(Legacy/CPG), R=Reset, "
            "F=Focus, G=Follow ON/OFF"
        )
        ttk.Label(frm, text=help_txt, foreground="gray").grid(row=start_row + len(attrs), column=0, columnspan=2, pady=6, sticky="w")

        self._update_ui_loop()

    def _bind_keys(self):
        self.root.focus_force()
        self.root.bind("<space>", lambda e: self.toggle_pause())
        self.root.bind("t", lambda e: self.toggle_wave_type())
        self.root.bind("m", lambda e: self.toggle_swim_mode())
        self.root.bind("r", lambda e: self.trigger_reset())
        self.root.bind("a", lambda e: self.turn_left())
        self.root.bind("<Left>", lambda e: self.turn_left())
        self.root.bind("d", lambda e: self.turn_right())
        self.root.bind("<Right>", lambda e: self.turn_right())
        self.root.bind("s", lambda e: self.turn_straight())
        self.root.bind("q", lambda e: self.nudge_bias(-self.bias_step))
        self.root.bind("w", lambda e: self.nudge_bias(+self.bias_step))
        self.root.bind("e", lambda e: self.turn_straight())

    def toggle_swim_mode(self):
        with self.lock:
            modes = ["Legacy", "Kuramoto", "CPG"]
            i = modes.index(self.swim_mode) if self.swim_mode in modes else 0
            self.swim_mode = modes[(i + 1) % len(modes)]
            print(f"[SwimMode] -> {self.swim_mode}")


    def toggle_wave_type(self):
        with self.lock:
            self.wave_type = "Standing" if self.wave_type == "Traveling" else "Traveling"
            print(f"[WaveType] -> {self.wave_type}")

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
                cur_bias = self.turn_bias
                cur_swim = self.swim_mode
                self.bias_label.set(f"turn_bias = {cur_bias:+.3f}")
            self.speed_var.set(f"Speed: {cur_s:.3f} m/s")
            self.z_var.set(f"Z-Pos: {cur_z:.3f} m")
            self.pass_z_var.set(f"Passive-Z: {cur_pz:.3f} N")
            self.status_var.set(f"MODE: {'RUNNING' if not self.paused else 'PAUSED'} ({cur_type}) [{cur_swim}]")
            self.root.after(100, self._update_ui_loop)
        except:
            pass

    def toggle_pause(self):
        with self.lock:
            self.paused = not self.paused

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
    #     calculated_vol = actual_mass / env.model.opt.density if env.model.opt.density > 0 else 0
    #     print(f"推算排水體積 (Volume): {calculated_vol*1000000:.2f} cm³")
    #     print("-" * 30)
    # except Exception as e:
    #     print(f"檢查重量時出錯: {e}")
    # --------------------------

    env.reset()
    initial_qpos = np.copy(env.data.qpos)

    width, height, fps = RENDER_W, RENDER_H, RENDER_FPS
    renderer = mujoco.Renderer(env.model, height=height, width=width)
    video_queue = None
    video_thread = None
    recording_interval = int((1.0 / fps) / env.model.opt.timestep)

    # ✅ Swimmers：不同游法在不同檔案
    legacy = LegacySwimmer(steer_front_n=STEER_FRONT_N_INIT, steer_gain=STEER_GAIN_INIT)
    kuramoto = KuramotoSwimmer(
        coupling=10.0,
        substeps=5,
        taper_head=0.35,
        taper_tail=1.0,
        steer_gain=0.70,
        steer_front_n=STEER_FRONT_N_INIT,
        steer_sign=1.0,   # ← 如果左右反了就改 -1.0
    )
    cpg = CPGSwimmer(steer_front_n=STEER_FRONT_N_INIT)
    
    swimmers = {"Legacy": legacy, "Kuramoto": kuramoto, "CPG": cpg}


    try:
        with mujoco.viewer.launch_passive(env.model, env.data) as viewer:
            viewer.cam.azimuth, viewer.cam.elevation, viewer.cam.distance = 0, -90.0, 13.0
            viewer.cam.lookat = [0, 0, 0]

            # ===== camera focus / follow =====
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
            # ===== end camera =====

            num_j = len(env.data.ctrl)
            for sw in swimmers.values():
                sw.reset(num_j)
                # CPG 需要 dt（Legacy 不用）
                if hasattr(sw, "set_dt"):
                    sw.set_dt(env.model.opt.timestep)

            step_counter = 0

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

                if p_reset:
                    with viewer.lock():
                        mujoco.mj_resetData(env.model, env.data)
                        env.data.qpos[:] = initial_qpos
                        mujoco.mj_forward(env.model, env.data)
                    with panel.lock:
                        panel.reset_request = False
                    for sw in swimmers.values():
                        sw.reset(num_j)
                    step_counter = 0
                    print("\n[RESET] Physics state restored.")

                if not p_paused:
                    t = env.data.time

                    swimmer = swimmers.get(p_swim_mode, legacy)
                    if hasattr(swimmer, "set_dt"):
                        swimmer.set_dt(env.model.opt.timestep)

                    sp = SwimParams(
                        amp=p_amp, freq=p_freq, step=p_step, turn=p_turn,
                        wave_type=p_wave_type, auto_mode=False
                    )
                    ctrl = swimmer.compute_ctrl(t=t, num_joints=num_j, p=sp)

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

                    step_counter += 1
                    if step_counter % 100 == 0:
                        print(
                            f"\r[Monitor] Swim:{p_swim_mode} | Wave:{p_wave_type} | Bias:{p_turn:+.3f} | "
                            f"Z:{z_pos:+.4f}m | PassiveZ:{passive_z_force:+.4f}N",
                            end=""
                        )

                    # 錄影（如果你要保留原本 auto exp 的話，我再幫你把那套完整搬回來）
                    # 這裡先維持最乾淨的主程式版本

                if follow_mode["on"]:
                    pos = env.data.xpos[base_id]
                    with viewer.lock():
                        viewer.cam.lookat[:] = pos

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
