import numpy as np
from .base import SwimmerBase, SwimParams

class LegacySwimmer(SwimmerBase):
    name = "Legacy"

    def __init__(self, steer_front_n: int = 4, steer_gain: float = 0.7):
        self.steer_front_n = int(steer_front_n)
        self.steer_gain = float(steer_gain)

    def compute_ctrl(self, t: float, num_joints: int, p: SwimParams) -> np.ndarray:
        # auto_mode 時 turn 不用
        steer = 0.0 if p.auto_mode else p.turn

        def front_weight(i: int) -> float:
            n = max(1, self.steer_front_n)
            if i >= n:
                return 0.0
            if n == 1:
                return 1.0
            return 1.0 - 0.7 * (i / (n - 1))

        ctrl = np.zeros(num_joints, dtype=np.float64)
        for i in range(num_joints):
            amp_i = p.amp * (0.4 + 0.6 * (i / (num_joints - 1)))
            bias_i = self.steer_gain * steer * front_weight(i)

            if p.wave_type == "Standing":
                u = bias_i + amp_i * np.sin(2 * np.pi * p.freq * t) * np.sin(i * p.step)
            else:
                u = bias_i + amp_i * np.sin(2 * np.pi * p.freq * t - i * p.step)

            ctrl[i] = u

        return ctrl
