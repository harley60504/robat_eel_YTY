# main.py
import threading
import os
import math

from ui_panel import ControlPanel
from sim_runner import run_mujoco


if __name__ == "__main__":
    print("RUNNING:", os.path.abspath(__file__))

    # Match Release/python_backend/angle_generator.py defaults:
    # AJOINT_DEG = 15.0, FREQUENCY_HZ = 1.0, phase_lag = 0.614439 rad.
    p = ControlPanel(
        amp_init=math.radians(15.0),
        freq_init=1.00,
        step_init=0.614439,
        turn_bias_init=0.00,
        bias_step_init=0.05,
        bias_max_init=0.80,
        bias_preset_init=0.30,
        sweep_enable_default=False,
    )

    threading.Thread(target=run_mujoco, args=(p, "eel.xml"), daemon=True).start()
    p.root.mainloop()
