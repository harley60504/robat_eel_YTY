# metrics_logger.py
import os
import csv
import time


class MetricsLogger:
    def __init__(self, out_dir="videos_sweep", filename="metrics.csv", overwrite=True):
        os.makedirs(out_dir, exist_ok=True)
        self.path = os.path.join(out_dir, filename)

        # ✅ 關鍵：overwrite=True → 用 "w"，否則用 "a"
        mode = "w" if overwrite else "a"

        self._fp = open(self.path, mode, newline="", encoding="utf-8")
        self._w = csv.writer(self._fp)

        # 只在新檔或覆蓋時寫 header
        if mode == "w" or self._fp.tell() == 0:
            self._w.writerow([
                "timestamp",
                "algo", "idx",
                "amp", "freq", "k_step", "phase_offset", "turn",
                "reason",
                "sim_time_end", "wall_time_sec",
                "x_end", "y_end", "z_end",
            ])
            self._fp.flush()

    def log_case(self, *, algo, idx, amp, freq, k_step, phase_offset, turn,
                 reason, sim_time_end, wall_time_sec, x_end, y_end, z_end):
        self._w.writerow([
            time.strftime("%Y-%m-%d %H:%M:%S"),
            algo, idx,
            amp, freq, k_step, phase_offset, turn,
            reason,
            f"{sim_time_end:.6f}", f"{wall_time_sec:.6f}",
            f"{x_end:.6f}", f"{y_end:.6f}", f"{z_end:.6f}",
        ])
        self._fp.flush()

    def close(self):
        try:
            self._fp.close()
        except:
            pass
