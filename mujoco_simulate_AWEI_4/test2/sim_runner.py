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
                        "idx": idx, "algo": algo, "wave": "Traveling",
                        "amp": float(a), "freq": float(f), "phase_offset": float(ph),
                        "step": float(s), "turn": float(SWEEP_TURN_BIAS),
                    })
                    idx += 1
    return cases

def phase_offset_to_time_offset(phase_offset_rad: float, freq_hz: float) -> float:
    if freq_hz <= 1e-9: return 0.0
    return float(phase_offset_rad) / (2.0 * np.pi * float(freq_hz))

def run_mujoco(panel, xml_path="eel.xml"):
    env = EelEnv(xml_path)
    env.reset()
    initial_qpos = np.copy(env.data.qpos)

    renderer = None
    manual_recorder = AsyncVideoRecorder(out_dir="videos", fps=RENDER_FPS)
    recording_interval = max(1, int((1.0 / RENDER_FPS) / env.model.opt.timestep))

    sweep_case_recorder = None
    sweep_cases = []
    sweep_idx = 0
    sweep_trial_active = False
    sweep_trial_start_walltime = 0.0
    sweep_algo_locked = None
    sweep_all_queue = []
    metrics = None
    needs_reset = False

    legacy = LegacySwimmer(steer_front_n=STEER_FRONT_N_INIT, steer_gain=STEER_GAIN_INIT)
    kuramoto = KuramotoSwimmer(coupling=10.0, substeps=5, taper_head=0.35, taper_tail=1.0, steer_gain=0.70, steer_front_n=STEER_FRONT_N_INIT, steer_sign=1.0)
    #cpg = CPGSwimmer(steer_front_n=STEER_FRONT_N_INIT)
    swimmers = {"Legacy": legacy, "Kuramoto": kuramoto}

    try:
        with mujoco.viewer.launch_passive(env.model, env.data) as viewer:
            base_id = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_BODY, "base_link")
            mujoco.mj_forward(env.model, env.data)

            with viewer.lock():
                viewer.cam.lookat[:] = POOL_LOOKAT
                viewer.cam.distance, viewer.cam.elevation, viewer.cam.azimuth = POOL_DIST, POOL_ELEV, POOL_AZIM

            cam_origin = {"lookat": np.array(viewer.cam.lookat, dtype=float), "distance": float(viewer.cam.distance), "elevation": float(viewer.cam.elevation), "azimuth": float(viewer.cam.azimuth)}
            follow_mode = {"on": False}

            def restore_camera_origin():
                with viewer.lock():
                    viewer.cam.lookat[:] = cam_origin["lookat"]
                    viewer.cam.distance, viewer.cam.elevation, viewer.cam.azimuth = cam_origin["distance"], cam_origin["elevation"], cam_origin["azimuth"]

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
                if hasattr(sw, "set_dt"): sw.set_dt(env.model.opt.timestep)

            kp_default = np.copy(env.model.actuator_gainprm[:, 0])

            def check_goal_contact():
                if env.data.ncon <= 0: return False
                try:
                    g1_id = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_GEOM, BASE_GEOM_NAME)
                    g2_id = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_GEOM, WALL_GEOM_NAME)
                    for i in range(env.data.ncon):
                        con = env.data.contact[i]
                        if (con.geom1 == g1_id and con.geom2 == g2_id) or (con.geom1 == g2_id and con.geom2 == g1_id): return True
                except: pass
                return False

            def _start_sweep_algo(algo_name: str):
                nonlocal sweep_cases, sweep_idx, sweep_trial_active, sweep_algo_locked, metrics
                sweep_algo_locked, sweep_cases, sweep_idx, sweep_trial_active = algo_name, build_sweep_cases(algo_name), 0, False
                if metrics: metrics.close()
                metrics = MetricsLogger(out_dir=os.path.join(SWEEP_VIDEO_DIR_BASE, algo_name), filename="metrics.csv")
                with panel.lock: 
                    panel.sweep_on = True
                    panel.paused = False
                    panel.sweep_status = f"START: {algo_name}"

            def stop_sweep(reason="STOP"):
                nonlocal sweep_case_recorder, sweep_trial_active, sweep_algo_locked, sweep_all_queue, metrics, renderer
                if sweep_case_recorder: 
                    sweep_case_recorder.stop()
                    sweep_case_recorder = None
                if renderer: 
                    renderer.close()
                    renderer = None
                sweep_trial_active, sweep_algo_locked, sweep_all_queue = False, None, []
                with panel.lock: 
                    panel.sweep_on = False
                    panel.sweep_status = f"OFF ({reason})"
                if metrics: 
                    metrics.close()
                    metrics = None

            # ---------- 主迴圈 ----------
            while viewer.is_running():
                time.sleep(0.001) 

                with panel.lock:
                    if not panel.is_alive: break
                    p_paused, p_amp, p_freq, p_step, p_turn = panel.paused, panel.amp, panel.freq, panel.phase_step, panel.turn_bias
                    p_wave_type, p_swim_mode, p_reset = panel.wave_type, panel.swim_mode, panel.reset_request
                    p_rec_toggle, p_sweep_on = panel.rec_toggle_req, panel.sweep_on
                    p_sweep_start_all, p_sweep_abort = panel.sweep_start_all_algos_req, panel.sweep_abort_req

                if p_sweep_abort: 
                    stop_sweep("ABORT")
                    with panel.lock: panel.sweep_abort_req = False

                if p_sweep_start_all: 
                    stop_sweep("RESTART")
                    sweep_all_queue = list(ALL_SWEEP_ORDER)
                    _start_sweep_algo(sweep_all_queue.pop(0))
                    with panel.lock: panel.sweep_start_all_algos_req = False
                
                if p_rec_toggle:
                    with panel.lock: 
                        panel.rec_toggle_req = False
                        panel.rec_on = not panel.rec_on
                    if panel.rec_on:
                        if renderer is None: renderer = mujoco.Renderer(env.model, height=RENDER_H, width=RENDER_W)
                        fname = f"{time.strftime('%Y%m%d_%H%M%S')}_{p_swim_mode}.mp4"
                        manual_recorder.start(width=RENDER_W, height=RENDER_H, filename=fname)
                    else: manual_recorder.stop()

                if p_reset: 
                    needs_reset = True
                    with panel.lock: panel.reset_request = False

                # KP 
                for i in range(min(6, env.model.nu)):
                    env.model.actuator_gainprm[i, 0] = kp_default[i] if panel.motor_on[i] else 0.0

                # ===========================
                # SWEEP 模式 (核心抗卡死優化)
                # ===========================
                if p_sweep_on and sweep_algo_locked:
                    if sweep_idx >= len(sweep_cases):
                        if sweep_all_queue: _start_sweep_algo(sweep_all_queue.pop(0))
                        else: stop_sweep("DONE")
                    else:
                        if not sweep_trial_active:
                            # 1. 銷毀舊 Renderer 並執行 GC
                            if renderer: 
                                renderer.close()
                                renderer = None
                            gc.collect()
                            
                            # 2. 初始化本組參數
                            case = sweep_cases[sweep_idx]
                            sweep_trial_active, sweep_trial_start_walltime = True, time.time()
                            mujoco.mj_resetData(env.model, env.data)
                            env.data.qpos[:] = initial_qpos
                            for sw in swimmers.values(): sw.reset(num_j)
                            
                            out_dir = os.path.join(SWEEP_VIDEO_DIR_BASE, case["algo"])
                            if not os.path.exists(out_dir): os.makedirs(out_dir)
                            
                            # 檔名格式
                            fname = (f"{case['algo']}_idx{case['idx']:04d}_"
                                     f"A{case['amp']:.2f}_F{case['freq']:.2f}_"
                                     f"K{case['step']:.2f}_B{case['turn']:+0.2f}.mp4")
                            
                            # 3. 分段啟動錄影與渲染，避開 I/O 峰值
                            sweep_case_recorder = AsyncVideoRecorder(out_dir=out_dir, fps=RENDER_FPS)
                            sweep_case_recorder.start(width=RENDER_W, height=RENDER_H, filename=fname)
                            time.sleep(0.5) # ✅ 救命延遲：確保 ffmpeg 正確鎖定檔案
                            
                            renderer = mujoco.Renderer(env.model, height=RENDER_H, width=RENDER_W)
                            
                            with panel.lock: 
                                panel.amp, panel.freq, panel.phase_step = case["amp"], case["freq"], case["step"]
                                panel.sweep_status = f"RUN: {sweep_idx+1}/{len(sweep_cases)}"
                                panel.vars["amp"].set(f"{case['amp']:.3f}")
                                panel.vars["freq"].set(f"{case['freq']:.3f}")
                                panel.vars["phase_step"].set(f"{case['step']:.3f}")
                        else:
                            swimmer = swimmers.get(p_swim_mode, legacy)
                            collided, timeout = False, False
                            for _ in range(SIM_SUBSTEPS_PER_UI):
                                t_eff = env.data.time + phase_offset_to_time_offset(case["phase_offset"], p_freq)
                                sp = SwimParams(amp=p_amp, freq=p_freq, step=p_step, turn=p_turn, wave_type=p_wave_type, auto_mode=True)
                                ctrl = swimmer.compute_ctrl(t=t_eff, num_joints=num_j, p=sp)
                                with viewer.lock():
                                    env.data.ctrl[:] = np.clip(ctrl, CTRL_CLIP_MIN, CTRL_CLIP_MAX)
                                    mujoco.mj_step(env.model, env.data)
                                collided = check_goal_contact()
                                if (time.time() - sweep_trial_start_walltime) > TRIAL_MAX_SEC: timeout = True
                                if collided or timeout: break

                            if sweep_case_recorder and sweep_case_recorder.is_recording():
                                sim_steps = int(round(env.data.time / env.model.opt.timestep))
                                if sim_steps % recording_interval < SIM_SUBSTEPS_PER_UI:
                                    renderer.update_scene(env.data, camera=viewer.cam)
                                    frame_bgr = cv2.cvtColor(renderer.render(), cv2.COLOR_RGB2BGR)
                                    cv2.putText(frame_bgr, f"Time: {env.data.time:.2f}s", (50, 50), cv2.FONT_HERSHEY_SIMPLEX, 1.2, (0, 255, 0), 2)
                                    sweep_case_recorder.push(frame_bgr)

                            if collided or timeout:
                                if sweep_case_recorder: 
                                    sweep_case_recorder.stop()
                                    sweep_case_recorder = None
                                try: 
                                    metrics.log_case(algo=case["algo"], idx=case["idx"], amp=p_amp, freq=p_freq, k_step=p_step, reason="COL" if collided else "TO", sim_time_end=env.data.time, x_end=env.data.qpos[0])
                                except: pass
                                
                                sweep_idx += 1
                                sweep_trial_active = False
                                # ✅ 救命冷卻：給予磁碟 2 秒鐘完成 I/O 隊列清理
                                time.sleep(3.0)

                # --- 一般互動模式 ---
                elif not p_paused:
                    swimmer = swimmers.get(p_swim_mode, legacy)
                    for _ in range(SIM_SUBSTEPS_PER_UI):
                        ctrl = swimmer.compute_ctrl(t=env.data.time, num_joints=num_j, p=SwimParams(amp=p_amp, freq=p_freq, step=p_step, turn=p_turn, wave_type=p_wave_type, auto_mode=False))
                        with viewer.lock():
                            env.data.ctrl[:] = np.clip(ctrl, CTRL_CLIP_MIN, CTRL_CLIP_MAX)
                            mujoco.mj_step(env.model, env.data)
                    
                    if manual_recorder.is_recording():
                        sim_steps = int(round(env.data.time / env.model.opt.timestep))
                        if sim_steps % recording_interval < SIM_SUBSTEPS_PER_UI:
                            if renderer is None: renderer = mujoco.Renderer(env.model, height=RENDER_H, width=RENDER_W)
                            renderer.update_scene(env.data, camera=viewer.cam)
                            frame_bgr = cv2.cvtColor(renderer.render(), cv2.COLOR_RGB2BGR)
                            manual_recorder.push(frame_bgr)

                if needs_reset:
                    mujoco.mj_resetData(env.model, env.data)
                    env.data.qpos[:] = initial_qpos
                    mujoco.mj_forward(env.model, env.data)
                    needs_reset = False

                if follow_mode["on"]:
                    pos = env.data.xpos[base_id]
                    with viewer.lock(): 
                        viewer.cam.lookat[:] = pos

                viewer.sync()

    finally:
        manual_recorder.stop()
        if sweep_case_recorder: sweep_case_recorder.stop()
        if renderer: renderer.close()