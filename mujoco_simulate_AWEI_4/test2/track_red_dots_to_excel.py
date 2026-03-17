# track_red_dots_to_excel.py
import os
import cv2
import numpy as np
import pandas as pd
from dataclasses import dataclass
from typing import List, Tuple, Optional


# =========================
# 你只需要改這裡（若換影片）
# =========================
VIDEO_PATH = r"C:\Users\AWEI\Documents\GitHub\robot_eel_YTY\mujoco_simulate_AWEI_4\test2\videos_sweep\Legacy\Legacy_idx0000_A0.35_F0.80_K0.40_B+0.00.mp4"
OUT_DIR = r"C:\Users\AWEI\Documents\GitHub\robot_eel_YTY\mujoco_simulate_AWEI_4\test2\outputs"
BASE_NAME = "Legacy_idx0000"   # 輸出檔名前綴
# =========================


# ----------------------------
# 偵測參數（紅點小 -> RGB 門檻 + 膨脹會比較穩）
# ----------------------------
@dataclass
class DetectConfig:
    # RGB 門檻
    r_min: int = 140   # 抓不到紅點 -> 降到 120；雜訊多 -> 提高到 160
    g_max: int = 170
    b_max: int = 170

    # 膨脹讓小點變得可找 contour
    dilate_iter: int = 2
    dilate_ksize: int = 3

    # 面積過濾（依影片可微調）
    min_area: float = 8.0       # 雜點多 -> 提高到 15~30
    max_area: float = 2000.0

    # 需要的點數
    n_points: int = 6

    # 配對最大允許距離（像素）超過就視為追丟
    max_match_dist: float = 120.0


def find_red_points(frame_bgr: np.ndarray, cfg: DetectConfig):
    """
    回傳：[(cx, cy, bbox(x,y,w,h), area), ...]
    """
    b, g, r = cv2.split(frame_bgr)
    mask = ((r > cfg.r_min) & (g < cfg.g_max) & (b < cfg.b_max)).astype(np.uint8) * 255

    k = np.ones((cfg.dilate_ksize, cfg.dilate_ksize), np.uint8)
    if cfg.dilate_iter > 0:
        mask = cv2.dilate(mask, k, iterations=cfg.dilate_iter)

    contours, _ = cv2.findContours(mask, cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)

    pts = []
    for c in contours:
        area = cv2.contourArea(c)
        if area < cfg.min_area or area > cfg.max_area:
            continue
        M = cv2.moments(c)
        if M["m00"] == 0:
            continue
        cx = float(M["m10"] / M["m00"])
        cy = float(M["m01"] / M["m00"])
        x, y, w, h = cv2.boundingRect(c)
        pts.append((cx, cy, (x, y, w, h), float(area)))

    return pts


def pca_order(points_xy: np.ndarray) -> List[int]:
    """
    用 PCA 找身體主軸，沿主軸投影排序（初始化 head->tail 用）
    points_xy: (N,2)
    """
    mu = points_xy.mean(axis=0, keepdims=True)
    X = points_xy - mu
    C = (X.T @ X) / max(1, (len(points_xy) - 1))
    eigvals, eigvecs = np.linalg.eigh(C)
    axis = eigvecs[:, np.argmax(eigvals)]  # 主軸
    proj = (X @ axis.reshape(2, 1)).reshape(-1)
    order = np.argsort(proj)
    return order.tolist()


def best_assignment(prev_xy: np.ndarray, cur_xy: np.ndarray) -> Optional[List[int]]:
    """
    6 點用 DFS 找最小總距離配對（不需要 scipy）
    回傳 mapping: prev_i -> cur_j
    """
    n = prev_xy.shape[0]
    m = cur_xy.shape[0]
    if m < n:
        return None

    cost = np.linalg.norm(prev_xy[:, None, :] - cur_xy[None, :, :], axis=2)  # (n,m)

    best = {"sum": 1e18, "map": None}
    used = np.zeros(m, dtype=bool)
    mapping = [-1] * n

    # 預先把每列候選按距離排序，加速
    cand_order = [np.argsort(cost[i]).tolist() for i in range(n)]

    def dfs(i: int, acc: float):
        if acc >= best["sum"]:
            return
        if i == n:
            best["sum"] = acc
            best["map"] = mapping.copy()
            return
        for j in cand_order[i]:
            if used[j]:
                continue
            used[j] = True
            mapping[i] = j
            dfs(i + 1, acc + float(cost[i, j]))
            used[j] = False
            mapping[i] = -1

    dfs(0, 0.0)
    return best["map"]


def track_video(
    video_path: str,
    out_xlsx: str,
    out_annotated_mp4: str,
    cfg: DetectConfig,
):
    cap = cv2.VideoCapture(video_path)
    if not cap.isOpened():
        raise RuntimeError(f"Cannot open video: {video_path}")

    fps = cap.get(cv2.CAP_PROP_FPS) or 30.0
    W = int(cap.get(cv2.CAP_PROP_FRAME_WIDTH))
    H = int(cap.get(cv2.CAP_PROP_FRAME_HEIGHT))
    n_frames = int(cap.get(cv2.CAP_PROP_FRAME_COUNT))

    os.makedirs(os.path.dirname(out_annotated_mp4) or ".", exist_ok=True)
    fourcc = cv2.VideoWriter_fourcc(*"mp4v")
    writer = cv2.VideoWriter(out_annotated_mp4, fourcc, float(fps), (W, H))
    if not writer.isOpened():
        raise RuntimeError(f"Cannot open VideoWriter: {out_annotated_mp4}")

    rows = []
    prev_xy = None  # (6,2)

    frame_idx = 0
    while True:
        ret, frame = cap.read()
        if not ret:
            break

        det = find_red_points(frame, cfg)  # list of (cx,cy,bbox,area)

        ok = False
        ordered_det = None

        if len(det) >= cfg.n_points:
            if prev_xy is None:
                # 初始化：挑面積最大的 6 個（最穩），再用 PCA 沿身體主軸排序
                det_sorted = sorted(det, key=lambda x: x[3], reverse=True)[: cfg.n_points]
                cur_xy6 = np.array([(d[0], d[1]) for d in det_sorted], dtype=np.float32)
                order = pca_order(cur_xy6)
                ordered_det = [det_sorted[i] for i in order]
                prev_xy = np.array([(d[0], d[1]) for d in ordered_det], dtype=np.float32)
                ok = True
            else:
                # 追蹤：為了抗雜訊，如果 det > 6，只取面積最大的前 K 再配對
                det_use = det
                if len(det_use) > cfg.n_points:
                    det_use = sorted(det_use, key=lambda x: x[3], reverse=True)[: max(cfg.n_points, 10)]
                cur_xy = np.array([(d[0], d[1]) for d in det_use], dtype=np.float32)

                mapping = best_assignment(prev_xy, cur_xy)
                if mapping is not None:
                    cand = [det_use[j] for j in mapping]
                    cand_xy = np.array([(d[0], d[1]) for d in cand], dtype=np.float32)
                    dists = np.linalg.norm(prev_xy - cand_xy, axis=1)
                    if float(np.max(dists)) <= cfg.max_match_dist:
                        ordered_det = cand
                        prev_xy = cand_xy
                        ok = True

        # ---- 寫 Excel 資料 ----
        t = frame_idx / float(fps)
        row = {"frame": frame_idx, "time_sec": t, "ok": int(ok)}

        if ok and ordered_det is not None:
            for i, (cx, cy, bbox, area) in enumerate(ordered_det):
                row[f"x{i+1}"] = float(cx)
                row[f"y{i+1}"] = float(cy)
        else:
            for i in range(cfg.n_points):
                row[f"x{i+1}"] = np.nan
                row[f"y{i+1}"] = np.nan

        rows.append(row)

        # ---- 畫綠框 + 標號 ----
        vis = frame.copy()
        if ok and ordered_det is not None:
            for i, (cx, cy, (x, y, w, h), area) in enumerate(ordered_det):
                cv2.rectangle(vis, (x, y), (x + w, y + h), (0, 255, 0), 2)
                cv2.putText(vis, f"P{i+1}", (x, max(0, y - 8)),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
                cv2.circle(vis, (int(round(cx)), int(round(cy))), 3, (0, 255, 0), -1)

        cv2.putText(vis, f"frame {frame_idx}/{max(0, n_frames-1)}", (20, 40),
                    cv2.FONT_HERSHEY_SIMPLEX, 1.0, (255, 255, 255), 2)

        writer.write(vis)

        frame_idx += 1
        if frame_idx % 120 == 0:
            print(f"[PROGRESS] {frame_idx}/{n_frames}")

    cap.release()
    writer.release()

    df = pd.DataFrame(rows)
    os.makedirs(os.path.dirname(out_xlsx) or ".", exist_ok=True)
    df.to_excel(out_xlsx, index=False)

    print(f"[DONE] Excel saved: {out_xlsx}")
    print(f"[DONE] Annotated video saved: {out_annotated_mp4}")


if __name__ == "__main__":
    # 輸出位置
    os.makedirs(OUT_DIR, exist_ok=True)

    out_xlsx = os.path.join(OUT_DIR, f"{BASE_NAME}_points.xlsx")
    out_mp4  = os.path.join(OUT_DIR, f"{BASE_NAME}_annotated.mp4")

    cfg = DetectConfig()

    print("[INFO] Input video:", VIDEO_PATH)
    print("[INFO] Output xlsx:", out_xlsx)
    print("[INFO] Output mp4 :", out_mp4)

    track_video(
        video_path=VIDEO_PATH,
        out_xlsx=out_xlsx,
        out_annotated_mp4=out_mp4,
        cfg=cfg,
    )
