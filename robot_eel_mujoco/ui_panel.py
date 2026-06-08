# ui_panel.py
import threading
import tkinter as tk
from tkinter import ttk
import numpy as np


class ControlPanel:
    def __init__(
        self,
        amp_init=0.50,
        freq_init=1.00,
        step_init=0.50,
        turn_bias_init=0.00,
        bias_step_init=0.05,
        bias_max_init=0.80,
        bias_preset_init=0.30,
        sweep_enable_default=False,
    ):
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
        self.sweep_on = sweep_enable_default
        self.sweep_status = "SWEEP: OFF"

        self.sweep_start_current_req = False   # T
        self.sweep_start_all_algos_req = False # Y
        self.sweep_abort_req = False           # X
        # ==================

        # telemetry
        self.current_speed = 0.0
        self.current_z = 0.0
        self.current_passive_z = 0.0
        self.is_alive = True

        # params
        self.amp, self.freq, self.turn_bias, self.phase_step = amp_init, freq_init, turn_bias_init, step_init
        self.bias_step = bias_step_init
        self.bias_max = bias_max_init
        self.bias_preset = bias_preset_init

        # Motor Failure toggles
        self.motor_on = [True] * 6
        self.motor_vars = [tk.BooleanVar(value=True) for _ in range(6)]

        # UI radio vars
        self.wave_var = tk.StringVar(value="行進波")   # 行進波 / 駐波
        self.mode_var = tk.StringVar(value="Legacy")  # Legacy / Kuramoto / CPG

        self._setup_ui()
        self._bind_keys()
        self.root.protocol("WM_DELETE_WINDOW", self.quit)

    # ---------- UI callbacks ----------
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

    def request_start_sweep_all_algos(self):
        with self.lock:
            self.sweep_start_all_algos_req = True

    def request_abort_sweep(self):
        with self.lock:
            self.sweep_abort_req = True

    def toggle_pause(self):
        with self.lock:
            self.paused = not self.paused

    # ---------- UI layout ----------
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
        ttk.Button(top, text="Sweep Current (T)", command=self.request_start_sweep_current_algo).grid(row=0, column=6, padx=4, sticky="e")
        ttk.Button(top, text="Sweep ALL (Y)", command=self.request_start_sweep_all_algos).grid(row=0, column=7, padx=4, sticky="e")
        ttk.Button(top, text="ABORT Sweep (X)", command=self.request_abort_sweep).grid(row=0, column=8, padx=4, sticky="e")

        ttk.Label(top, textvariable=self.sweep_var, foreground="darkgreen").grid(row=1, column=0, sticky="w", pady=(4, 0))

        tel = ttk.Frame(frm)
        tel.grid(row=1, column=0, columnspan=2, sticky="ew", pady=(0, 10))
        ttk.Label(tel, textvariable=self.speed_var, font=("Consolas", 11), foreground="blue").grid(row=0, column=0, padx=(0, 14), sticky="w")
        ttk.Label(tel, textvariable=self.z_var, font=("Consolas", 11), foreground="green").grid(row=0, column=1, padx=(0, 14), sticky="w")
        ttk.Label(tel, textvariable=self.pass_z_var, font=("Consolas", 11), foreground="purple").grid(row=0, column=2, sticky="w")

        # wave radio
        wave_frm = ttk.LabelFrame(frm, text="Wave Type", padding=10)
        wave_frm.grid(row=2, column=0, columnspan=2, sticky="ew", pady=6)
        ttk.Radiobutton(wave_frm, text="駐波 (Standing)", value="駐波",
                        variable=self.wave_var, command=self._on_wave_radio).grid(row=0, column=0, padx=6, sticky="w")
        ttk.Radiobutton(wave_frm, text="行進波 (Traveling)", value="行進波",
                        variable=self.wave_var, command=self._on_wave_radio).grid(row=0, column=1, padx=6, sticky="w")

        # algo radio
        algo_frm = ttk.LabelFrame(frm, text="Algorithm", padding=10)
        algo_frm.grid(row=3, column=0, columnspan=2, sticky="ew", pady=6)
        ttk.Radiobutton(algo_frm, text="Legacy", value="Legacy",
                        variable=self.mode_var, command=self._on_mode_radio).grid(row=0, column=0, padx=6, sticky="w")
        ttk.Radiobutton(algo_frm, text="Kuramoto", value="Kuramoto",
                        variable=self.mode_var, command=self._on_mode_radio).grid(row=0, column=1, padx=6, sticky="w")
        ttk.Radiobutton(algo_frm, text="CPG", value="CPG",
                        variable=self.mode_var, command=self._on_mode_radio).grid(row=0, column=2, padx=6, sticky="w")

        # steering
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

        # motor failure
        motor_frm = ttk.LabelFrame(frm, text="Motor Failure (Disable joints)", padding=10)
        motor_frm.grid(row=5, column=0, columnspan=2, sticky="ew", pady=6)
        for i in range(6):
            cb = ttk.Checkbutton(motor_frm, text=f"link{i+1}",
                                 variable=self.motor_vars[i], command=self._on_motor_toggle)
            cb.grid(row=0, column=i, padx=4, sticky="w")
        for c in range(6):
            motor_frm.grid_columnconfigure(c, weight=1)

        # params
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
            "R=Reset, F=ResetCam, G=FollowCam, V=Record, "
            "P=AutoSweep(toggle), T=SweepCurrent, Y=SweepALL, X=AbortSweep"
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
        self.root.bind("y", lambda e: self.request_start_sweep_all_algos())
        self.root.bind("x", lambda e: self.request_abort_sweep())

    # ---------- param/steer helpers ----------
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
        self._set_bias(+self.bias_preset)

    def turn_right(self):
        self._set_bias(-self.bias_preset)

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
