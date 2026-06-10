from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

import cv2
import mujoco
import numpy as np

from hopf_cpg import HopfCPG, HopfCPGParams, amp_scales_to_mu_scales, degrees_to_radians
from plot_fixed_gait_trajectories import is_wall_contact


FPS = 15
PANEL_W = 540
PANEL_H = 960
OUT_DIR = Path("outputs/real_sim_wall_videos")


@dataclass(frozen=True)
class Clip:
    key: str
    real_video: Path
    real_wall_s: float
    gait_json: Path


CLIPS = (
    Clip("straight", Path("Release/python_backend/recordings/clean_v_20260607_233739.mp4"), 15.0, Path("gaits/straight.json")),
    Clip("turn_left", Path("Release/python_backend/recordings/clean_v_20260608_141203.mp4"), 8.0, Path("gaits/turn_left.json")),
    Clip("spin_left", Path("Release/python_backend/recordings/clean_v_20260608_141254.mp4"), 8.0, Path("gaits/spin_left.json")),
)


def fit_cover(frame: np.ndarray, size: tuple[int, int]) -> np.ndarray:
    target_w, target_h = size
    h, w = frame.shape[:2]
    scale = max(target_w / w, target_h / h)
    resized = cv2.resize(frame, (int(round(w * scale)), int(round(h * scale))), interpolation=cv2.INTER_AREA)
    h2, w2 = resized.shape[:2]
    x0 = max(0, (w2 - target_w) // 2)
    y0 = max(0, (h2 - target_h) // 2)
    return resized[y0 : y0 + target_h, x0 : x0 + target_w]


def write_label(frame: np.ndarray, title: str, time_s: float, stopped: bool):
    cv2.rectangle(frame, (0, 0), (frame.shape[1], 58), (245, 245, 245), -1)
    cv2.putText(frame, title, (14, 34), cv2.FONT_HERSHEY_SIMPLEX, 0.78, (30, 30, 30), 2, cv2.LINE_AA)
    suffix = " wall" if stopped else ""
    cv2.putText(frame, f"{time_s:4.1f}s{suffix}", (frame.shape[1] - 148, 34), cv2.FONT_HERSHEY_SIMPLEX, 0.68, (0, 0, 220), 2, cv2.LINE_AA)


def make_real_frames(video_path: Path, wall_s: float) -> list[np.ndarray]:
    cap = cv2.VideoCapture(str(video_path))
    src_fps = cap.get(cv2.CAP_PROP_FPS) or FPS
    frames = []
    total = int(round(wall_s * FPS)) + 1
    last = None
    for i in range(total):
        t = i / FPS
        cap.set(cv2.CAP_PROP_POS_FRAMES, int(round(t * src_fps)))
        ok, frame = cap.read()
        if ok:
            last = frame
        elif last is not None:
            frame = last.copy()
        else:
            frame = np.full((PANEL_H, PANEL_W, 3), 220, dtype=np.uint8)
        if "20260607_233739" in str(video_path):
            frame = cv2.rotate(frame, cv2.ROTATE_180)
        panel = fit_cover(frame, (PANEL_W, PANEL_H))
        write_label(panel, "REAL", t, i == total - 1)
        frames.append(panel)
    return frames


def load_gait(path: Path) -> HopfCPGParams:
    gait = json.loads(path.read_text(encoding="utf-8"))
    return HopfCPGParams(
        frequency=float(gait["freq"]),
        wavelength=float(gait["wavelength"]),
        ajoint=degrees_to_radians(float(gait["ajoint"])),
        mu_scales=amp_scales_to_mu_scales(tuple(gait["amp_scales"])),
        phase_lags=tuple(gait["phase_lags"]),
        joint_bias=tuple(gait["joint_bias"]),
    )


def setup_sim(gait_path: Path):
    model = mujoco.MjModel.from_xml_path("eel.xml")
    data = mujoco.MjData(model)
    model.opt.gravity[:] = (0, 0, 0)
    base_id = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_BODY, "base_link")
    base_xml_pos = model.body_pos[base_id]
    data.qpos[0] = -0.90 - base_xml_pos[0]
    data.qpos[1] = 0.0 - base_xml_pos[1]
    mujoco.mj_forward(model, data)
    cpg = HopfCPG(num_joints=6, params=load_gait(gait_path))
    return model, data, base_id, cpg, load_gait(gait_path)


def transform_sim_frame(frame_rgb: np.ndarray) -> np.ndarray:
    bgr = cv2.cvtColor(frame_rgb, cv2.COLOR_RGB2BGR)
    # The renderer's raw top-down camera already matches the real-video pool orientation.
    return fit_cover(bgr, (PANEL_W, PANEL_H))


def render_sim_frames(gait_path: Path) -> list[np.ndarray]:
    model, data, _, cpg, params = setup_sim(gait_path)
    renderer = mujoco.Renderer(model, height=540, width=960)
    camera = mujoco.MjvCamera()
    camera.type = mujoco.mjtCamera.mjCAMERA_FREE
    camera.lookat[:] = np.array([0.0, 0.0, -0.02])
    camera.distance = 2.65
    camera.elevation = -90
    camera.azimuth = 0

    steps_per_frame = max(1, int(round((1.0 / FPS) / model.opt.timestep)))
    frames = []
    hit_wall = False
    sim_t = 0.0
    last_panel = None
    while sim_t <= 35.0:
        for _ in range(steps_per_frame):
            targets = cpg.step(data.time, model.opt.timestep, params)
            data.ctrl[0:6] = np.clip(targets, -1.2, 1.2)
            mujoco.mj_step(model, data)
            if is_wall_contact(model, data):
                hit_wall = True
                break
        renderer.update_scene(data, camera=camera)
        panel = transform_sim_frame(renderer.render())
        write_label(panel, "MUJOCO", float(data.time), hit_wall)
        frames.append(panel)
        last_panel = panel
        sim_t = float(data.time)
        if hit_wall:
            break
    renderer.close()
    return frames if frames else [last_panel]


def combine_frames(real_frames: list[np.ndarray], sim_frames: list[np.ndarray], out_path: Path):
    count = max(len(real_frames), len(sim_frames))
    last_real = real_frames[-1]
    last_sim = sim_frames[-1]
    writer = cv2.VideoWriter(str(out_path), cv2.VideoWriter_fourcc(*"mp4v"), FPS, (PANEL_W * 2, PANEL_H))
    if not writer.isOpened():
        raise RuntimeError(f"Could not open VideoWriter: {out_path}")
    for i in range(count):
        real = real_frames[i] if i < len(real_frames) else last_real
        sim = sim_frames[i] if i < len(sim_frames) else last_sim
        writer.write(np.hstack((real, sim)))
    writer.release()


def main():
    OUT_DIR.mkdir(parents=True, exist_ok=True)
    for clip in CLIPS:
        print(f"processing {clip.key}")
        real_frames = make_real_frames(clip.real_video, clip.real_wall_s)
        sim_frames = render_sim_frames(clip.gait_json)
        out_path = OUT_DIR / f"{clip.key}_real_vs_mujoco_to_wall.mp4"
        combine_frames(real_frames, sim_frames, out_path)
        print(out_path, "real_frames", len(real_frames), "sim_frames", len(sim_frames))


if __name__ == "__main__":
    main()
