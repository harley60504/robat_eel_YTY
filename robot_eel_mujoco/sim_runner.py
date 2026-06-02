# sim_runner.py
import os
import time
import numpy as np
import mujoco
import mujoco.viewer
import cv2
import gc
import traceback

from eel_env import EelEnv
from swimmers.base import SwimParams
from swimmers import LegacySwimmer, CPGSwimmer, KuramotoSwimmer
from recorder import AsyncVideoRecorder
from metrics_logger import MetricsLogger

# ✅ 新增：加速 sweep 模組
from sweep_module import execute_sweep_logic

# =========================
# CONFIG
# =========================
STEER_FRONT_N_INIT = 4
STEER_GAIN_INIT = 0.70
CTRL_CLIP_MIN = -1.2
CTRL_CLIP_MAX = 1.2

POOL_DIST = 5.5
POOL_ELEV = -90
POOL_AZIM = 0.0
POOL_LOOKAT = np.array([0.0, 0.0, 0.0], dtype=float)

FOCUS_DIST = 1.9
FOCUS_ELEV = -90
FOCUS_AZIM = 0.0

RENDER_W, RENDER_H, RENDER_FPS = 1920, 1080, 60

BASE_GEOM_NAME = "base_link_collision"
WALL_GEOM_NAME = "wall_front"

SWEEP_AMPS = [0.35, 0.50]
SWEEP_FREQS = [0.8, 1.0, 1.2]
SWEEP_STEPS = [0.40, 0.50, 0.60]
SWEEP_PHASE_OFFSETS = [0.0]
SWEEP_TURN_BIAS = 0.0

SWEEP_DIM_AMP = True
SWEEP_DIM_FREQ = True
SWEEP_DIM_STEP = True
SWEEP_DIM_PHASE = False

SWEEP_VIDEO_DIR_BASE = "videos_sweep"
TRIAL_MAX_SEC = 100
SWEEP_AUTO_FOLLOW_ON_START = False
ALL_SWEEP_ORDER = ["Legacy", "Kuramoto"]

SIM_SUBSTEPS_PER_UI = 16
DEBUG_MODE = True


def build_sweep_cases(algo: str):
    amps = SWEEP_AMPS if SWEEP_DIM_AMP else [SWEEP_AMPS[0]]
    freqs = SWEEP_FREQS if SWEEP_DIM_FREQ else [SWEEP_FREQS[0]]
    steps = SWEEP_STEPS if SWEEP_DIM_STEP else [SWEEP_STEPS[0]]
    phs = SWEEP_PHASE_OFFSETS if SWEEP_DIM_PHASE else [0.0]
    cases = []
    idx = 0
    for a in amps:
        for f in freqs:
            for s in steps:
                for ph in phs:
                    cases.append({
                        "idx": idx,
                        "algo": algo,
                        "wave": "Traveling",
                        "amp": float(a),
                        "freq": float(f),
                        "phase_offset": float(ph),
                        "step": float(s),
                        "turn": float(SWEEP_TURN_BIAS),
                    })
                    idx += 1
    return cases


def phase_offset_to_time_offset(phase_offset_rad: float, freq_hz: float) -> float:
    if freq_hz <= 1e-9:
        return 0.0
    return float(phase_offset_rad) / (2.0 * np.pi * float(freq_hz))


def run_mujoco(panel, xml_path="eel.xml"):
    env = EelEnv(xml_path)
    env.reset()
    initial_qpos = np.copy(env.data.qpos)

    renderer = None
    manual_recorder = AsyncVideoRecorder(out_dir="videos", fps=RENDER_FPS)
    recording_interval = max(1, int((1.0 / RENDER_FPS) / env.model.opt.timestep))

    needs_reset = False

    legacy = LegacySwimmer(steer_front_n=STEER_FRONT_N_INIT, steer_gain=STEER_GAIN_INIT)
    kuramoto = KuramotoSwimmer(
        coupling=10.0, substeps=5, taper_head=0.35, taper_tail=1.0,
        steer_gain=0.70, steer_front_n=STEER_FRONT_N_INIT, steer_sign=1.0
    )
    # cpg = CPGSwimmer(steer_front_n=STEER_FRONT_N_INIT)
    swimmers = {"Legacy": legacy, "Kuramoto": kuramoto}

    try:
        with mujoco.viewer.launch_passive(env.model, env.data) as viewer:
            base_id = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_BODY, "base_link")
            mujoco.mj_forward(env.model, env.data)

            with viewer.lock():
                viewer.cam.lookat[:] = POOL_LOOKAT
                viewer.cam.distance, viewer.cam.elevation, viewer.cam.azimuth = POOL_DIST, POOL_ELEV, POOL_AZIM

            cam_origin = {
                "lookat": np.array(viewer.cam.lookat, dtype=float),
                "distance": float(viewer.cam.distance),
                "elevation": float(viewer.cam.elevation),
                "azimuth": float(viewer.cam.azimuth),
            }
            follow_mode = {"on": False}

            def restore_camera_origin():
                with viewer.lock():
                    viewer.cam.lookat[:] = cam_origin["lookat"]
                    viewer.cam.distance = cam_origin["distance"]
                    viewer.cam.elevation = cam_origin["elevation"]
                    viewer.cam.azimuth = cam_origin["azimuth"]

            def key_cb(key):
                if key == ord("F"):
                    follow_mode["on"] = False
                    restore_camera_origin()
                elif key == ord("G"):
                    follow_mode["on"] = not follow_mode["on"]

            viewer.user_key_callback = key_cb

            num_j = len(env.data.ctrl)
            for sw in swimmers.values():
                sw.reset(num_j)
                if hasattr(sw, "set_dt"):
                    sw.set_dt(env.model.opt.timestep)

            kp_default = np.copy(env.model.actuator_gainprm[:, 0])

            def check_goal_contact():
                if env.data.ncon <= 0:
                    return False
                try:
                    g1_id = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_GEOM, BASE_GEOM_NAME)
                    g2_id = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_GEOM, WALL_GEOM_NAME)
                    for i in range(env.data.ncon):
                        con = env.data.contact[i]
                        if (con.geom1 == g1_id and con.geom2 == g2_id) or (con.geom1 == g2_id and con.geom2 == g1_id):
                            return True
                except:
                    pass
                return False

            # ✅ Sweep 模組配置（不降畫質/不降FPS）
            render_cfg = {"W": RENDER_W, "H": RENDER_H, "FPS": RENDER_FPS}
            sweep_cfg = {
                "OUT_DIR": SWEEP_VIDEO_DIR_BASE,
                "TIMEOUT": TRIAL_MAX_SEC,
                "SUBSTEPS": 128,  # sweep_module 內會用到；實際加速主要看 sweep_module 的 ACCEL_FACTOR
            }

            # ---------- 主迴圈 ----------
            while viewer.is_running():

                # 先讀 panel 狀態
                with panel.lock:
                    if not panel.is_alive:
                        break

                    p_paused = panel.paused
                    p_amp, p_freq, p_step, p_turn = panel.amp, panel.freq, panel.phase_step, panel.turn_bias
                    p_wave_type, p_swim_mode, p_reset = panel.wave_type, panel.swim_mode, panel.reset_request

                    p_rec_toggle = panel.rec_toggle_req

                    # ✅ 只保留 Sweep ALL / Abort（其他 sweep toggle 先不走舊流程）
                    p_sweep_start_all = panel.sweep_start_all_algos_req
                    p_sweep_abort = panel.sweep_abort_req
                    p_sweep_on = panel.sweep_on

                # ✅ 非 sweep 才 sleep（避免 sweep 期間被拖慢）
                if not p_sweep_on:
                    time.sleep(0.001)

                # --- Abort Sweep：讓 sweep_module 自己看到旗標然後中止 ---
                if p_sweep_abort:
                    # 這裡不要做太多事，只保持旗標
                    with panel.lock:
                        panel.sweep_abort_req = True

                # --- Sweep ALL (Y)：用 sweep_module 跑完整 sweep ---
                if p_sweep_start_all:
                    with panel.lock:
                        panel.sweep_start_all_algos_req = False
                        panel.sweep_abort_req = False
                        panel.sweep_on = True
                        panel.paused = False
                        panel.sweep_status = "SWEEP: START ALL"

                    # sweep 時避免跟手動錄影搶資源
                    if manual_recorder.is_recording():
                        manual_recorder.stop()

                    # 如果互動模式 renderer 曾建立，保留也行，但最好關掉避免 GPU 資源緊
                    if renderer is not None:
                        try:
                            renderer.close()
                        except:
                            pass
                        renderer = None
                        gc.collect()

                    # 依序跑每個 algo
                    for algo_name in ALL_SWEEP_ORDER:
                        with panel.lock:
                            if panel.sweep_abort_req:
                                break
                            panel.swim_mode = algo_name  # sweep_module 讀 panel.swim_mode

                        execute_sweep_logic(
                            env=env,
                            swimmers=swimmers,
                            panel=panel,
                            initial_qpos=initial_qpos,
                            build_cases_func=build_sweep_cases,
                            render_cfg=render_cfg,
                            sweep_cfg=sweep_cfg,
                            phase_offset_func=phase_offset_to_time_offset,
                            check_contact_func=check_goal_contact,
                            viewer=viewer,
                        )

                    with panel.lock:
                        panel.sweep_on = False
                        panel.sweep_abort_req = False
                        panel.sweep_status = "SWEEP: FINISHED"

                    viewer.sync()
                    continue  # ✅ sweep 跑完回到主 loop，不要同時做互動步進

                # --- 手動錄影 toggle ---
                if p_rec_toggle:
                    with panel.lock:
                        panel.rec_toggle_req = False
                        panel.rec_on = not panel.rec_on
                        rec_on = panel.rec_on

                    if rec_on:
                        if renderer is None:
                            renderer = mujoco.Renderer(env.model, height=RENDER_H, width=RENDER_W)
                        fname = f"{time.strftime('%Y%m%d_%H%M%S')}_{p_swim_mode}.mp4"
                        manual_recorder.start(width=RENDER_W, height=RENDER_H, filename=fname)
                    else:
                        manual_recorder.stop()

                # --- reset ---
                if p_reset:
                    needs_reset = True
                    with panel.lock:
                        panel.reset_request = False

                # KP 開關（disable joint）
                for i in range(min(6, env.model.nu)):
                    env.model.actuator_gainprm[i, 0] = kp_default[i] if panel.motor_on[i] else 0.0

                # --- 一般互動模式（非 sweep） ---
                if (not p_paused) and (not p_sweep_on):
                    swimmer = swimmers.get(p_swim_mode, legacy)

                    for _ in range(SIM_SUBSTEPS_PER_UI):
                        ctrl = swimmer.compute_ctrl(
                            t=env.data.time,
                            num_joints=num_j,
                            p=SwimParams(
                                amp=p_amp, freq=p_freq, step=p_step, turn=p_turn,
                                wave_type=p_wave_type, auto_mode=False
                            )
                        )
                        with viewer.lock():
                            env.data.ctrl[:] = np.clip(ctrl, CTRL_CLIP_MIN, CTRL_CLIP_MAX)
                            mujoco.mj_step(env.model, env.data)

                    if manual_recorder.is_recording():
                        sim_steps = int(round(env.data.time / env.model.opt.timestep))
                        if sim_steps % recording_interval < SIM_SUBSTEPS_PER_UI:
                            if renderer is None:
                                renderer = mujoco.Renderer(env.model, height=RENDER_H, width=RENDER_W)
                            renderer.update_scene(env.data, camera=viewer.cam)
                            frame_bgr = cv2.cvtColor(renderer.render(), cv2.COLOR_RGB2BGR)
                            manual_recorder.push(frame_bgr)

                # --- reset physics ---
                if needs_reset:
                    mujoco.mj_resetData(env.model, env.data)
                    env.data.qpos[:] = initial_qpos
                    mujoco.mj_forward(env.model, env.data)
                    needs_reset = False

                # --- follow cam ---
                if follow_mode["on"]:
                    pos = env.data.xpos[base_id]
                    with viewer.lock():
                        viewer.cam.lookat[:] = pos

                viewer.sync()

    finally:
        manual_recorder.stop()
        if renderer is not None:
            try:
                renderer.close()
            except:
                pass
