import os
import numpy as np
import pandas as pd
import matplotlib.pyplot as plt

# =========================
# 你只需要改這裡
# =========================
XLSX_PATH = r"C:\Users\AWEI\Documents\GitHub\robot_eel_YTY\mujoco_simulate_AWEI_4\test2\outputs\Legacy_idx0000_points.xlsx"
OUT_DIR   = r"C:\Users\AWEI\Documents\GitHub\robot_eel_YTY\mujoco_simulate_AWEI_4\test2\outputs"
N_POINTS  = 6

# 你知道的驅動頻率（Hz），用來把時間延遲轉成「相位差(度)」
# 如果你想自動估頻率也可以，但先用你 sweep 的 freq 最準
DRIVE_FREQ_HZ = 0.80

# 可選：每節中心距（公尺）。不知道就先填 None，只輸出「秒/度」
# 例如你的 link 節距 0.05m 就填 0.05
LINK_SPACING_M = None
# =========================


def naninterp(t, y):
    """把 NaN 用線性插值補起來（避免互相關爆掉）"""
    y = y.astype(float)
    mask = np.isfinite(y)
    if mask.sum() < 2:
        return y
    return np.interp(t, t[mask], y[mask])


def detrend(y):
    """去 DC + 緩慢漂移（用簡單均值即可；需要更強可改高通）"""
    y = y - np.nanmean(y)
    return y


def rms(y):
    y = y[np.isfinite(y)]
    if len(y) == 0:
        return np.nan
    return float(np.sqrt(np.mean(y**2)))


def peak_to_peak(y):
    y = y[np.isfinite(y)]
    if len(y) == 0:
        return np.nan
    return float(np.max(y) - np.min(y))


def xcorr_lag_seconds(t, a, b, max_lag_sec=2.0):
    """
    互相關估計 b 相對 a 的延遲（正值表示 b 落後 a）
    - a,b: 已插值/去均值的一維訊號
    """
    dt = float(np.median(np.diff(t)))
    if dt <= 0:
        return np.nan

    a = a.astype(float)
    b = b.astype(float)

    # 限制 lag 範圍
    max_lag = int(round(max_lag_sec / dt))
    max_lag = max(1, max_lag)

    # 互相關（full）
    c = np.correlate(b, a, mode="full")  # b vs a
    lags = np.arange(-len(a) + 1, len(a))  # lag index (samples)

    # 只取小範圍
    center = len(c) // 2
    lo = max(0, center - max_lag)
    hi = min(len(c), center + max_lag + 1)

    c2 = c[lo:hi]
    l2 = lags[lo:hi]

    best_idx = int(np.argmax(c2))
    best_lag_samples = int(l2[best_idx])

    lag_sec = best_lag_samples * dt
    return float(lag_sec)


def main():
    os.makedirs(OUT_DIR, exist_ok=True)

    df = pd.read_excel(XLSX_PATH)
    if "time_sec" not in df.columns:
        raise RuntimeError("Excel 欄位缺少 time_sec")

    # 只取 ok==1 的幀（但為了時間軸連續，我們仍會插值）
    t = df["time_sec"].to_numpy(dtype=float)

    # 讀出 6 個點的 x,y
    xs = []
    ys = []
    for i in range(1, N_POINTS + 1):
        xs.append(df.get(f"x{i}", pd.Series([np.nan]*len(df))).to_numpy(dtype=float))
        ys.append(df.get(f"y{i}", pd.Series([np.nan]*len(df))).to_numpy(dtype=float))

    xs = np.vstack(xs)  # (6, T)
    ys = np.vstack(ys)  # (6, T)

    # ====== 1) 繪圖：y(t) ======
    plt.figure()
    for i in range(N_POINTS):
        plt.plot(t, ys[i], label=f"P{i+1}")
    plt.xlabel("Time (s)")
    plt.ylabel("Pixel Y")
    plt.title("Joint marker Y vs Time")
    plt.legend()
    plt.tight_layout()
    y_plot_path = os.path.join(OUT_DIR, "plots_y_vs_time.png")
    plt.savefig(y_plot_path, dpi=200)
    plt.close()

    # ====== 2) 繪圖：x(t) ======
    plt.figure()
    for i in range(N_POINTS):
        plt.plot(t, xs[i], label=f"P{i+1}")
    plt.xlabel("Time (s)")
    plt.ylabel("Pixel X")
    plt.title("Joint marker X vs Time")
    plt.legend()
    plt.tight_layout()
    x_plot_path = os.path.join(OUT_DIR, "plots_x_vs_time.png")
    plt.savefig(x_plot_path, dpi=200)
    plt.close()

    # ====== 3) 振幅統計 ======
    amp_rows = []
    for i in range(N_POINTS):
        y = ys[i]
        amp_rows.append({
            "point": f"P{i+1}",
            "y_rms": rms(detrend(y)),
            "y_peak_to_peak": peak_to_peak(detrend(y)),
            "x_rms": rms(detrend(xs[i])),
            "x_peak_to_peak": peak_to_peak(detrend(xs[i])),
        })
    amp_df = pd.DataFrame(amp_rows)
    amp_xlsx = os.path.join(OUT_DIR, "amplitude_summary.xlsx")
    amp_df.to_excel(amp_xlsx, index=False)

    # ====== 4) 相位差：相鄰關節互相關估 lag ======
    # 先插值/去均值，避免 NaN & 漂移
    y_proc = []
    for i in range(N_POINTS):
        y_i = naninterp(t, ys[i])
        y_i = detrend(y_i)
        y_proc.append(y_i)
    y_proc = np.vstack(y_proc)

    phase_rows = []
    for i in range(N_POINTS - 1):
        lag_sec = xcorr_lag_seconds(t, y_proc[i], y_proc[i+1], max_lag_sec=2.0)

        # 轉度數：phase = lag * f * 360
        phase_deg = lag_sec * float(DRIVE_FREQ_HZ) * 360.0 if np.isfinite(lag_sec) else np.nan

        phase_rows.append({
            "pair": f"P{i+1}->P{i+2}",
            "lag_sec (P{i+2} relative to P{i+1})": lag_sec,
            f"phase_deg @ {DRIVE_FREQ_HZ:.2f}Hz": phase_deg,
        })

    phase_df = pd.DataFrame(phase_rows)
    phase_xlsx = os.path.join(OUT_DIR, "phase_lag_summary.xlsx")
    phase_df.to_excel(phase_xlsx, index=False)

    # ====== 5) 估波速（可選，需要節距） ======
    wave_rows = []
    if LINK_SPACING_M is not None:
        # 以相鄰節距/時間延遲估「相速度」
        for r in phase_rows:
            lag_sec = r[f"lag_sec (P{int(r['pair'][1]) + 1} relative to P{int(r['pair'][1])})"] if False else r["lag_sec (P{i+2} relative to P{i+1})"]  # 不用這行
        # 寫正確版本
        wave_rows = []
        for i in range(N_POINTS - 1):
            lag_sec = phase_rows[i][f"lag_sec (P{i+2} relative to P{i+1})"]
            if np.isfinite(lag_sec) and abs(lag_sec) > 1e-6:
                v = LINK_SPACING_M / lag_sec  # m/s (符號表示方向)
            else:
                v = np.nan
            wave_rows.append({
                "pair": f"P{i+1}->P{i+2}",
                "link_spacing_m": LINK_SPACING_M,
                "lag_sec": lag_sec,
                "wave_speed_m_per_s": v,
            })

    wave_df = pd.DataFrame(wave_rows)
    wave_xlsx = os.path.join(OUT_DIR, "wave_speed_estimate.xlsx")
    wave_df.to_excel(wave_xlsx, index=False)

    print("[DONE] plots:", y_plot_path, x_plot_path)
    print("[DONE] excel:", amp_xlsx, phase_xlsx, wave_xlsx)
    print("[NOTE] phase sign: positive lag means downstream joint lags upstream joint (traveling wave).")


if __name__ == "__main__":
    main()
