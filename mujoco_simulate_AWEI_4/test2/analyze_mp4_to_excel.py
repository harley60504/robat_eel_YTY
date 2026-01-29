import os
import re
import glob
import math
import cv2
import numpy as np
import pandas as pd


# =========================
# CONFIG（你只要改這裡）
# =========================
VIDEO_DIR = "videos_sweep"         # 你 sweep 輸出的根目錄（底下可能有 Legacy/CPG/Kuramoto 子資料夾）
OUTPUT_XLSX = "sweep_video_metrics.xlsx"

# 取樣：每隔幾幀做一次偵測（越大越快，但時間解析度越低）
FRAME_STRIDE = 2

# 紅色偵測（HSV），通常不用改；如果你的紅點顏色怪怪再調
RED_MIN_AREA = 25                 # 過小雜訊過濾（像素面積）
MORPH_KERNEL = 5                  # 形態學去雜訊 kernel

# 牆判斷（可選）
# wall_x_px = None 表示不算撞牆
# 你可以在第一支影片用 imshow 看一下牆大概在畫面 x=多少
WALL_X_PX = None  # 例如 540

# 像素 -> 公尺（可選）
# 如果你知道畫面中某段距離對應真實公尺，可以填：METER_PER_PIXEL = 真實距離(m) / 量到的像素(px)
METER_PER_PIXEL = None
# =========================


def parse_params_from_filename(fname: str) -> dict:
    """
    解析你 sweep 命名格式：
    e.g. Legacy_idx0003_A0.35_F1.00_OFF1.57rad_S0.50_B+0.00.mp4
    """
    base = os.path.basename(fname)
    d = {"file": base, "algo": None, "idx": None, "A": None, "F": None, "OFFrad": None, "S": None, "B": None}

    m = re.search(r"^(Legacy|CPG|Kuramoto)", base)
    if m: d["algo"] = m.group(1)

    m = re.search(r"_idx(\d+)_", base)
    if m: d["idx"] = int(m.group(1))

    m = re.search(r"_A([0-9.]+)_", base)
    if m: d["A"] = float(m.group(1))

    m = re.search(r"_F([0-9.]+)_", base)
    if m: d["F"] = float(m.group(1))

    m = re.search(r"_OFF([0-9.]+)rad_", base)
    if m: d["OFFrad"] = float(m.group(1))

    m = re.search(r"_S([0-9.]+)_", base)
    if m: d["S"] = float(m.group(1))

    m = re.search(r"_B([+-]?[0-9.]+)\.mp4$", base)
    if m: d["B"] = float(m.group(1))

    return d


def detect_red_centroid_bgr(frame_bgr: np.ndarray):
    """
    回傳：
      (cx, cy), area_sum
    找不到回 None
    """
    hsv = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2HSV)

    # 紅色在 HSV 會跨 0 度，所以用兩段 mask
    lower1 = np.array([0, 120, 80])
    upper1 = np.array([10, 255, 255])
    lower2 = np.array([170, 120, 80])
    upper2 = np.array([180, 255, 255])

    mask1 = cv2.inRange(hsv, lower1, upper1)
    mask2 = cv2.inRange(hsv, lower2, upper2)
    mask = cv2.bitwise_or(mask1, mask2)

    # 去雜訊
    k = MORPH_KERNEL
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (k, k))
    mask = cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel, iterations=1)
    mask = cv2.morphologyEx(mask, cv2.MORPH_CLOSE, kernel, iterations=1)

    # 找所有紅色連通區
    cnts, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    total_area = 0.0
    sum_x = 0.0
    sum_y = 0.0

    for c in cnts:
        area = cv2.contourArea(c)
        if area < RED_MIN_AREA:
            continue
        M = cv2.moments(c)
        if M["m00"] <= 1e-9:
            continue
        cx = M["m10"] / M["m00"]
        cy = M["m01"] / M["m00"]
        total_area += area
        sum_x += cx * area
        sum_y += cy * area

    if total_area <= 1e-9:
        return None, 0.0

    cx = sum_x / total_area
    cy = sum_y / total_area
    return (float(cx), float(cy)), float(total_area)


def analyze_one_video(path: str):
    cap = cv2.VideoCapture(path)
    if not cap.isOpened():
        raise RuntimeError(f"Cannot open video: {path}")

    fps = cap.get(cv2.CAP_PROP_FPS)
    if fps <= 1e-9:
        fps = 30.0

    frame_count = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))

    tracks = []
    last_pos = None
    last_t = None

    # 第一個有效位置用來做相對位移
    start_pos = None
    reached_wall_time = None
    max_dx = 0.0

    frame_idx = 0
    sample_idx = 0

    while True:
        ok, frame = cap.read()
        if not ok:
            break

        if frame_idx % FRAME_STRIDE != 0:
            frame_idx += 1
            continue

        t = frame_idx / fps
        pos, area = detect_red_centroid_bgr(frame)

        if pos is not None:
            if start_pos is None:
                start_pos = pos

            cx, cy = pos
            dx = cx - start_pos[0]
            dy = cy - start_pos[1]
            max_dx = max(max_dx, dx)

            # 撞牆判斷（以 x 超過某個像素）
            if (WALL_X_PX is not None) and (reached_wall_time is None):
                if cx >= float(WALL_X_PX):
                    reached_wall_time = t

            # 瞬時速度（pixel/s）
            v_px_s = None
            if last_pos is not None and last_t is not None:
                dt = (t - last_t)
                if dt > 1e-9:
                    dist = math.hypot(cx - last_pos[0], cy - last_pos[1])
                    v_px_s = dist / dt

            tracks.append({
                "t_sec": t,
                "frame": frame_idx,
                "cx": cx,
                "cy": cy,
                "dx": dx,
                "dy": dy,
                "area": area,
                "v_px_s": v_px_s
            })

            last_pos = pos
            last_t = t

        frame_idx += 1
        sample_idx += 1

    cap.release()

    if len(tracks) == 0:
        # 找不到紅點，回傳空結果
        return {
            "fps": fps,
            "frames": frame_count,
            "duration_sec": frame_count / fps,
            "found": False,
            "avg_v_px_s": None,
            "avg_v_m_s": None,
            "max_dx_px": None,
            "max_dx_m": None,
            "reach_wall": False,
            "reach_wall_t": None,
        }, pd.DataFrame([])

    df = pd.DataFrame(tracks)

    # 平均速度（忽略第一筆 None）
    v = df["v_px_s"].dropna()
    avg_v_px_s = float(v.mean()) if len(v) > 0 else None

    max_dx_px = float(df["dx"].max())

    avg_v_m_s = None
    max_dx_m = None
    if METER_PER_PIXEL is not None:
        avg_v_m_s = None if avg_v_px_s is None else avg_v_px_s * float(METER_PER_PIXEL)
        max_dx_m = max_dx_px * float(METER_PER_PIXEL)

    res = {
        "fps": fps,
        "frames": frame_count,
        "duration_sec": frame_count / fps,
        "found": True,
        "avg_v_px_s": avg_v_px_s,
        "avg_v_m_s": avg_v_m_s,
        "max_dx_px": max_dx_px,
        "max_dx_m": max_dx_m,
        "reach_wall": reached_wall_time is not None,
        "reach_wall_t": reached_wall_time,
    }
    return res, df


def list_all_mp4s(root: str):
    # 支援 videos_sweep/**/**/*.mp4
    return sorted(glob.glob(os.path.join(root, "**", "*.mp4"), recursive=True))


def main():
    mp4s = list_all_mp4s(VIDEO_DIR)
    if len(mp4s) == 0:
        raise RuntimeError(f"No mp4 found under: {VIDEO_DIR}")

    summary_rows = []
    tracks_rows = []

    for p in mp4s:
        info = parse_params_from_filename(p)
        metrics, df_track = analyze_one_video(p)

        row = {}
        row.update(info)
        row.update({
            "path": p,
            "fps": metrics["fps"],
            "duration_sec": metrics["duration_sec"],
            "found": metrics["found"],
            "avg_v_px_s": metrics["avg_v_px_s"],
            "avg_v_m_s": metrics["avg_v_m_s"],
            "max_dx_px": metrics["max_dx_px"],
            "max_dx_m": metrics["max_dx_m"],
            "reach_wall": metrics["reach_wall"],
            "reach_wall_t": metrics["reach_wall_t"],
        })
        summary_rows.append(row)

        if len(df_track) > 0:
            df_track = df_track.copy()
            df_track["file"] = info["file"]
            df_track["algo"] = info["algo"]
            df_track["idx"] = info["idx"]
            df_track["A"] = info["A"]
            df_track["F"] = info["F"]
            df_track["OFFrad"] = info["OFFrad"]
            df_track["S"] = info["S"]
            df_track["B"] = info["B"]
            tracks_rows.append(df_track)

        print(f"[OK] {os.path.basename(p)}  found={metrics['found']}  avg_v(px/s)={metrics['avg_v_px_s']}  max_dx(px)={metrics['max_dx_px']}")

    df_summary = pd.DataFrame(summary_rows)

    if len(tracks_rows) > 0:
        df_tracks = pd.concat(tracks_rows, ignore_index=True)
    else:
        df_tracks = pd.DataFrame([])

    # 排序方便看
    if "algo" in df_summary.columns and "idx" in df_summary.columns:
        df_summary = df_summary.sort_values(["algo", "idx"], na_position="last")

    # 輸出 Excel
    with pd.ExcelWriter(OUTPUT_XLSX, engine="openpyxl") as writer:
        df_summary.to_excel(writer, sheet_name="summary", index=False)
        df_tracks.to_excel(writer, sheet_name="tracks", index=False)

    print(f"\n[DONE] Excel saved: {OUTPUT_XLSX}")
    print("Sheets: summary (per video), tracks (time series)")


if __name__ == "__main__":
    main()