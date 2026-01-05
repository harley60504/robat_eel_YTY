import time
import threading
import tkinter as tk
from tkinter import ttk

import numpy as np
import mujoco
import mujoco.viewer

from eel_env import EelEnv


class ControlPanel:
    def __init__(self):
        self.root = tk.Tk()
        self.root.title("Eel Control Panel")

        # shared states (thread-safe enough for simple floats with GIL; use lock anyway)
        self.lock = threading.Lock()

        self.paused = True
        self.reset_requested = False

        self.amp = 0.5
        self.freq = 1.0
        self.turn_bias = 0.0
        self.phase_step = 0.8

        # --- UI ---
        frm = ttk.Frame(self.root, padding=10)
        frm.grid(row=0, column=0, sticky="nsew")

        self.status_var = tk.StringVar(value="PAUSED")
        ttk.Label(frm, textvariable=self.status_var, font=("Segoe UI", 12, "bold")).grid(
            row=0, column=0, columnspan=3, sticky="w", pady=(0, 8)
        )

        ttk.Button(frm, text="Run / Pause", command=self.toggle_pause).grid(row=1, column=0, sticky="ew")
        ttk.Button(frm, text="Reset", command=self.request_reset).grid(row=1, column=1, sticky="ew")
        ttk.Button(frm, text="Quit", command=self.quit).grid(row=1, column=2, sticky="ew")

        self._add_slider(frm, "Amplitude (amp)", 0.0, 1.2, 0.01, 2, "amp")
        self._add_slider(frm, "Frequency (freq)", 0.0, 3.0, 0.01, 3, "freq")
        self._add_slider(frm, "Turn bias (rad)", -0.6, 0.6, 0.01, 4, "turn_bias")
        self._add_slider(frm, "Phase step", 0.1, 2.0, 0.01, 5, "phase_step")

        frm.columnconfigure(0, weight=1)
        frm.columnconfigure(1, weight=1)
        frm.columnconfigure(2, weight=1)

        self.root.protocol("WM_DELETE_WINDOW", self.quit)

    def _add_slider(self, parent, label, vmin, vmax, step, row, attr):
        ttk.Label(parent, text=label).grid(row=row, column=0, sticky="w", pady=(10, 0))

        var = tk.DoubleVar(value=getattr(self, attr))

        def on_change(_=None):
            with self.lock:
                setattr(self, attr, float(var.get()))

        scale = ttk.Scale(parent, from_=vmin, to=vmax, orient="horizontal", variable=var, command=lambda _: on_change())
        scale.grid(row=row, column=1, sticky="ew", padx=(10, 10), pady=(10, 0))

        val_label = ttk.Label(parent, text=f"{getattr(self, attr):.2f}")

        def refresh_label():
            val_label.configure(text=f"{var.get():.2f}")
            parent.after(100, refresh_label)

        val_label.grid(row=row, column=2, sticky="e", pady=(10, 0))
        refresh_label()

    def toggle_pause(self):
        with self.lock:
            self.paused = not self.paused
            self.status_var.set("PAUSED" if self.paused else "RUNNING")

    def request_reset(self):
        with self.lock:
            self.reset_requested = True

    def quit(self):
        # end Tk loop
        try:
            self.root.quit()
            self.root.destroy()
        except Exception:
            pass


def run_mujoco(panel: ControlPanel, xml_path="eel.xml"):
    env = EelEnv(xml_path)
    env.reset()
    env.model.body("base_link").pos = [-4.2, 0, 0]

    print("MuJoCo viewer launched. Use the Tk panel to control amp/freq/turn/pause/reset.")

    with mujoco.viewer.launch_passive(env.model, env.data) as viewer:
        while viewer.is_running():
            with viewer.lock():
                # read UI states
                with panel.lock:
                    paused = panel.paused
                    amp = panel.amp
                    freq = panel.freq
                    turn_bias = panel.turn_bias
                    phase_step = panel.phase_step
                    do_reset = panel.reset_requested
                    panel.reset_requested = False

                if do_reset:
                    mujoco.mj_resetData(env.model, env.data)
                    env.model.body("base_link").pos = [-4.2, 0, 0]
                    env.data.ctrl[:] = 0
                    mujoco.mj_forward(env.model, env.data)

                if paused:
                    env.data.ctrl[:] = 0
                    mujoco.mj_forward(env.model, env.data)
                else:
                    t = env.data.time
                    ctrl = np.zeros(6)
                    for i in range(6):
                        phase = -i * phase_step
                        ctrl[i] = turn_bias + amp * np.sin(2 * np.pi * freq * t + phase)

                    env.data.ctrl[:] = np.clip(ctrl, -1.2, 1.2)
                    mujoco.mj_step(env.model, env.data)

            viewer.sync()
            time.sleep(env.model.opt.timestep)


if __name__ == "__main__":
    panel = ControlPanel()

    sim_thread = threading.Thread(target=run_mujoco, args=(panel, "eel.xml"), daemon=True)
    sim_thread.start()

    panel.root.mainloop()
