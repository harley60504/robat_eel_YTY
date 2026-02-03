import os
import re
import glob
import math
import cv2
import numpy as np
import pandas as pd

# =========================
# CONFIG
# =========================
VIDEO_DIR = "videos_sweep" 
OUTPUT_XLSX = "sweep_video_metrics.xlsx"
FRAME_STRIDE = 1 
RED_MIN_AREA = 10 
MORPH_KERNEL = 3 
NUM_RED_POINTS = 6

# =========================

def parse_params_from_filename(fname: str) -> dict:
    base = os.path.basename(fname)
    d = {"file": base, "algo": "Unknown", "idx": 0, "A": 0.0, "F": 0.0, "K": 0.0, "B": 0.0}
    m = re.search(r"^(Legacy|CPG|Kuramoto)", base)
    if m: d["algo"] = m.group(1)
    m = re.search(r"_idx(\d+)", base)
    if m: d["idx"] = int(m.group(1))
    m = re.search(r"_A([0-9.]+)", base)
    if m: d["A"] = float(m.group(1))
    m = re.search(r"_F([0-9.]+)", base)
    if m: d["F"] = float(m.group(1))
    m = re.search(r"_K([0-9.]+)", base)
    if m: d["K"] = float(m.group(1))
    m = re.search(r"_B([+-]?[0-9.]+)", base)
    if m:
        try:
            val_str = m.group(1).rstrip('.')
            d["B"] = float(val_str)
        except ValueError:
            d["B"] = 0.0
    return d

def detect_multi_red_points(frame_bgr: np.ndarray, num_points=6):
    hsv = cv2.cvtColor(frame_bgr, cv2.COLOR_BGR2HSV)
    lower1, upper1 = np.array([0, 100, 50]), np.array([10, 255, 255])
    lower2, upper2 = np.array([160, 100, 50]), np.array([180, 255, 255])
    mask = cv2.bitwise_or(cv2.inRange(hsv, lower1, upper1), cv2.inRange(hsv, lower2, upper2))
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (MORPH_KERNEL, MORPH_KERNEL))
    mask = cv2.morphologyEx(cv2.morphologyEx(mask, cv2.MORPH_OPEN, kernel), cv2.MORPH_CLOSE, kernel)
    cnts, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    
    candidates = []
    for c in cnts:
        area = cv2.contourArea(c)
        if area < RED_MIN_AREA: continue
        M = cv2.moments(c)
        if M["m00"] <= 1e-9: continue
        candidates.append(((M["m10"] / M["m00"], M["m01"] / M["m00"]), area))

    candidates = sorted(candidates, key=lambda x: x[1], reverse=True)[:num_points]
    candidates = sorted(candidates, key=lambda x: x[0][0])
    return [p[0] for p in candidates]

def analyze_one_video(path: str):
    cap = cv2.VideoCapture(path)
    if not cap.isOpened(): return {"found": False}, pd.DataFrame([])
    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    
    multi_tracks = []
    frame_idx = 0
    baseline_y = None 

    while True:
        ok, frame = cap.read()
        if not ok: break
        if frame_idx % FRAME_STRIDE == 0:
            t = frame_idx / fps
            points = detect_multi_red_points(frame, NUM_RED_POINTS)
            if len(points) == NUM_RED_POINTS:
                if baseline_y is None:
                    baseline_y = sum(p[1] for p in points) / NUM_RED_POINTS
                
                row = {"time_sec": round(t, 4), "frame_no": frame_idx}
                deviations = []
                for i, (px, py) in enumerate(points):
                    row[f"pt{i+1}_x"], row[f"pt{i+1}_y"] = round(px, 2), round(py, 2)
                    dev_y = py - baseline_y
                    row[f"pt{i+1}_dev_y"] = round(dev_y, 2)
                    deviations.append(abs(dev_y))
                row["avg_side_deviation"] = round(sum(deviations) / NUM_RED_POINTS, 2)
                multi_tracks.append(row)
        frame_idx += 1
    cap.release()

    if not multi_tracks: return {"found": False}, pd.DataFrame([])

    df_track = pd.DataFrame(multi_tracks)
    
    # === 擺動統計分析核心 ===
    res = {
        "found": True, 
        "fps": fps, 
        "total_time": df_track["time_sec"].iloc[-1] - df_track["time_sec"].iloc[0],
        "overall_max_drift": df_track["avg_side_deviation"].max(), # 整體游歪最大值
    }
    
    for i in range(1, NUM_RED_POINTS + 1):
        col = f"pt{i}_dev_y"
        # 1. 振幅 (Amplitude)：觀察該節擺動的大小
        res[f"pt{i}_swing_amp"] = round(df_track[col].max() - df_track[col].min(), 2)
        
        # 2. 平均偏置 (Offset)：觀察該節是否偏向某一邊 (正值偏下, 負值偏上)
        res[f"pt{i}_avg_offset"] = round(df_track[col].mean(), 2)
        
        # 3. 擺動強度 (STD)：標準差越高，代表該節點擺動越劇烈
        res[f"pt{i}_swing_intensity"] = round(df_track[col].std(), 2)
        
    # 推進速度分析
    if len(df_track) > 1:
        total_time = res["total_time"]
        total_dist_x = df_track["pt1_x"].iloc[-1] - df_track["pt1_x"].iloc[0]
        res["forward_velocity_px_s"] = round(total_dist_x / total_time, 2) if total_time > 0 else 0

    return res, df_track

def main():
    mp4_files = sorted(glob.glob(os.path.join(VIDEO_DIR, "**", "*.mp4"), recursive=True))
    if not mp4_files: 
        print(f"錯誤：在 {VIDEO_DIR} 找不到影片。")
        return
    
    summary_list, all_tracks_list = [], []

    for video_path in mp4_files:
        params = parse_params_from_filename(video_path)
        metrics, df_track = analyze_one_video(video_path)
        summary_list.append({**params, **metrics})
        if not df_track.empty:
            for key, value in params.items(): df_track.insert(0, f"meta_{key}", value)
            all_tracks_list.append(df_track)
        print(f"分析完成: {params['file']} | 推進速度: {metrics.get('forward_velocity_px_s', 0)} px/s")

    with pd.ExcelWriter(OUTPUT_XLSX, engine="openpyxl") as writer:
        pd.DataFrame(summary_list).to_excel(writer, sheet_name="summary", index=False)
        if all_tracks_list:
            pd.concat(all_tracks_list, ignore_index=True).to_excel(writer, sheet_name="tracks", index=False)
    
    print(f"\n[成功] 數據已存至: {OUTPUT_XLSX}")
    print("數據解讀建議：")
    print("1. 查看 pt1_swing_amp 到 pt6_swing_amp：如果數值遞增，代表波形由頭部向尾部放大，是典型的鰻魚推進特徵。")
    print("2. 查看 pt_avg_offset：如果數值很大，代表控制參數 B 或 K 導致鰻魚轉向而非直線行進。")

if __name__ == "__main__":
    main()