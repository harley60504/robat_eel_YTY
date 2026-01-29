# main.py
import threading
import os

from ui_panel import ControlPanel
from sim_runner import run_mujoco


if __name__ == "__main__":
    # ✅ 確認你跑到的檔案就是這份
    print("RUNNING:", os.path.abspath(__file__))

    p = ControlPanel(
        amp_init=0.50,
        freq_init=1.00,
        step_init=0.50,
        turn_bias_init=0.00,
        bias_step_init=0.05,
        bias_max_init=0.80,
        bias_preset_init=0.30,
        sweep_enable_default=False,
    )

    threading.Thread(target=run_mujoco, args=(p, "eel.xml"), daemon=True).start()
    p.root.mainloop()