# sim_runner.py
import os
import time
import numpy as np
import mujoco
import mujoco.viewer
import cv2

from eel_env import EelEnv
from swimmers.base import SwimParams
from swimmers import LegacySwimmer, CPGSwimmer, KuramotoSwimmer
from recorder import AsyncVideoRecorder
from metrics_logger import MetricsLogger


# =========================
# CONFIG（你要改 sweep 就改這裡）
# =========================
STEER_FRONT_N_INIT = 4
STEER_GAIN_INIT    = 0.70

CTRL_CLIP_MIN = -1.2
CTRL_CLIP_MAX =  1.2

# ===== 初始全景（看整個泳池）=====
POOL_DIST = 13      # 距離越大越看得到全場
POOL_ELEV = -90    # 稍微俯視
POOL_AZIM = 0.0      # 從側面看（可調 0/90/180）
POOL_LOOKAT = np.array([0.0, 0.0, 0.0], dtype=float)  # 看場中央

FOCUS_DIST = 1.9
FOCUS_ELEV = -90
FOCUS_AZIM = 0.0

RENDER_W, RENDER_H, RENDER_FPS = 640, 480, 30

# ----- 撞牆偵測 geom 名稱（不對就改成你 XML 的 geom name）-----
BASE_GEOM_NAME = "base_link_collision"
WALL_GEOM_NAME = "wall_front"

# ----- Sweep 的 parameter list（先用少量測試）-----
SWEEP_AMPS  = [0.35, 0.50]          # amp
SWEEP_FREQS = [0.8, 1.0, 1.2]       # frequency
SWEEP_STEPS = [0.40, 0.50, 0.60]    # wavenumber / phase_step
SWEEP_PHASE_OFFSETS = [0.0]         # phase offset (rad) 先固定 0，減少次數

SWEEP_TURN_BIAS = 0.0               # steering bias 固定

# ✅ 你要「只 sweep 哪些維度」
SWEEP_DIM_AMP  = True
SWEEP_DIM_FREQ = True
SWEEP_DIM_STEP = True
SWEEP_DIM_PHASE = False  # 先不要 sweep phase offset，太多 case

# output dir
SWEEP_VIDEO_DIR_BASE = "videos_sweep"

# timeout（秒），None = 不用 timeout（只撞牆才換）
TRIAL_MAX_SEC = 100

# Sweep 一開始是否自動 Follow Cam
SWEEP_AUTO_FOLLOW_ON_START = True

# 三種演算法 sweep 順序
ALL_SWEEP_ORDER = ["Legacy", "Kuramoto"]

# ✅【方案A】加速：每個 UI loop 內跑幾個 mj_step
# - 越大越快（但畫面更新較不平滑）
SIM_SUBSTEPS_PER_UI = 2
# =========================


def build_sweep_cases(algo: str):
    amps  = SWEEP_AMPS  if SWEEP_DIM_AMP  else [SWEEP_AMPS[0]]
    freqs = SWEEP_FREQS if SWEEP_DIM_FREQ else [SWEEP_FREQS[0]]
    steps = SWEEP_STEPS if SWEEP_DIM_STEP else [SWEEP_STEPS[0]]
    phs   = SWEEP_PHASE_OFFSETS if SWEEP_DIM_PHASE else [0.0]

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

    renderer = mujoco.Renderer(env.model, height=RENDER_H, width=RENDER_W)

    # 手動錄影（獨立）
    manual_recorder = AsyncVideoRecorder(out_dir="videos", fps=RENDER_FPS)
    recording_interval = max(1, int((1.0 / RENDER_FPS) / env.model.opt.timestep))

    # sweep runtime
    sweep_case_recorder = None
    sweep_cases = []
    sweep_idx = 0
    sweep_trial_active = False
    sweep_trial_start_walltime = 0.0

    # 「現在 sweep 哪個 algo」 + 「是否在 sweep all」
    sweep_algo_locked = None
    sweep_all_queue = []  # e.g. ["Legacy","Kuramoto","CPG"]

    # ✅ metrics logger（每個 algo 各一份 metrics.csv）
    metrics = None

    legacy = LegacySwimmer(steer_front_n=STEER_FRONT_N_INIT, steer_gain=STEER_GAIN_INIT)
    kuramoto = KuramotoSwimmer(
        coupling=10.0, substeps=5, taper_head=0.35, taper_tail=1.0,
        steer_gain=0.70, steer_front_n=STEER_FRONT_N_INIT, steer_sign=1.0,
    )
    #cpg = CPGSwimmer(steer_front_n=STEER_FRONT_N_INIT)
    swimmers = {"Legacy": legacy, "Kuramoto": kuramoto}

    try:
        with mujoco.viewer.launch_passive(env.model, env.data) as viewer:
            # ✅ 相機：不要硬改 azimuth/elevation/distance（保留 XML 的 global）
            base_id = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_BODY, "base_link")
            mujoco.mj_forward(env.model, env.data)
            base_pos0 = env.data.xpos[base_id].copy()
            with viewer.lock():
                viewer.cam.lookat[:]   = POOL_LOOKAT
                viewer.cam.distance    = POOL_DIST
                viewer.cam.elevation   = POOL_ELEV
                viewer.cam.azimuth     = POOL_AZIM

            cam_origin = {
                "lookat": np.array(viewer.cam.lookat, dtype=float),
                "distance": float(viewer.cam.distance),
                "elevation": float(viewer.cam.elevation),
                "azimuth": float(viewer.cam.azimuth),
            }

            follow_mode = {"on": False}

            # collision geom ids
            geom_base = None
            geom_wall = None
            try:
                geom_base = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_GEOM, BASE_GEOM_NAME)
                geom_wall = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_GEOM, WALL_GEOM_NAME)
            except:
                print(f"[WARN] 找不到 geom：{BASE_GEOM_NAME} / {WALL_GEOM_NAME}，撞牆偵測失效（case 會不換）")

            def restore_camera_origin():
                with viewer.lock():
                    viewer.cam.lookat[:] = cam_origin["lookat"]
                    viewer.cam.distance  = cam_origin["distance"]
                    viewer.cam.elevation = cam_origin["elevation"]
                    viewer.cam.azimuth   = cam_origin["azimuth"]

            def toggle_follow():
                follow_mode["on"] = not follow_mode["on"]
                print("[Camera] Follow mode:", "ON" if follow_mode["on"] else "OFF")

            def key_cb(key):
                if key == ord("F"):
                    follow_mode["on"] = False
                    restore_camera_origin()
                    print("[Camera] Restore original view (follow OFF)")
                elif key == ord("G"):
                    toggle_follow()

            viewer.user_key_callback = key_cb

            num_j = len(env.data.ctrl)
            for sw in swimmers.values():
                sw.reset(num_j)
                if hasattr(sw, "set_dt"):
                    sw.set_dt(env.model.opt.timestep)

            kp_default = np.copy(env.model.actuator_gainprm[:, 0])

            def hard_reset_and_forward():
                with viewer.lock():
                    mujoco.mj_resetData(env.model, env.data)
                    env.data.qpos[:] = initial_qpos
                    mujoco.mj_forward(env.model, env.data)

            def check_goal_contact():
                if geom_base is None or geom_wall is None:
                    return False
                if env.data.ncon <= 0:
                    return False
                for i in range(env.data.ncon):
                    con = env.data.contact[i]
                    g1 = con.geom1
                    g2 = con.geom2
                    if (g1 == geom_base and g2 == geom_wall) or (g1 == geom_wall and g2 == geom_base):
                        return True
                return False

            # ---------- sweep control ----------
            def _start_sweep_algo(algo_name: str):
                nonlocal sweep_case_recorder, sweep_cases, sweep_idx, sweep_trial_active, sweep_trial_start_walltime
                nonlocal sweep_algo_locked, metrics

                sweep_algo_locked = algo_name
                sweep_cases = build_sweep_cases(algo_name)
                sweep_idx = 0
                sweep_trial_active = False
                sweep_trial_start_walltime = 0.0

                if sweep_case_recorder is not None:
                    sweep_case_recorder.stop()
                    sweep_case_recorder = None

                # metrics per algo
                try:
                    if metrics is not None:
                        metrics.close()
                except:
                    pass
                metrics = MetricsLogger(out_dir=os.path.join(SWEEP_VIDEO_DIR_BASE, algo_name), filename="metrics.csv")

                with panel.lock:
                    panel.sweep_on = True
                    panel.paused = False
                    panel.sweep_status = f"SWEEP: ON [{algo_name}] (0/{len(sweep_cases)})"

                print(f"[SWEEP] START algo={algo_name}, total cases={len(sweep_cases)}")

                if SWEEP_AUTO_FOLLOW_ON_START:
                    follow_mode["on"] = True
                    pos = env.data.xpos[base_id].copy()
                    with viewer.lock():
                        viewer.cam.lookat[:] = pos
                        viewer.cam.distance  = FOCUS_DIST
                        viewer.cam.elevation = FOCUS_ELEV
                        viewer.cam.azimuth   = FOCUS_AZIM
                    print("[Camera] Sweep start -> Follow ON + Close view")

            def stop_sweep(reason="USER_STOP"):
                nonlocal sweep_case_recorder, sweep_trial_active, sweep_algo_locked, sweep_all_queue, metrics
                if sweep_case_recorder is not None:
                    sweep_case_recorder.stop()
                    sweep_case_recorder = None
                sweep_trial_active = False
                sweep_algo_locked = None
                sweep_all_queue = []
                with panel.lock:
                    panel.sweep_on = False
                    panel.sweep_status = f"SWEEP: OFF ({reason})"
                print("[SWEEP] STOP", reason)

                try:
                    if metrics is not None:
                        metrics.close()
                        metrics = None
                except:
                    pass

            def start_sweep_current_algo(algo_name: str):
                nonlocal sweep_all_queue
                sweep_all_queue = []
                stop_sweep(reason="RESTART")
                _start_sweep_algo(algo_name)

            def start_sweep_all_algos():
                nonlocal sweep_all_queue
                stop_sweep(reason="RESTART_ALL")
                sweep_all_queue = list(ALL_SWEEP_ORDER)
                _start_sweep_algo(sweep_all_queue.pop(0))

            # ---------- main loop ----------
            while viewer.is_running():
                loop_wall_start = time.time()

                with panel.lock:
                    if not panel.is_alive:
                        break

                    p_paused = panel.paused
                    p_amp, p_freq, p_step = panel.amp, panel.freq, panel.phase_step
                    p_turn = panel.turn_bias
                    p_wave_type = panel.wave_type
                    p_swim_mode = panel.swim_mode
                    p_reset = panel.reset_request
                    p_motor_on = list(panel.motor_on)

                    p_cam_focus = panel.cam_focus_req
                    p_cam_follow_toggle = panel.cam_follow_toggle_req

                    p_rec_toggle = panel.rec_toggle_req

                    p_sweep_toggle = panel.sweep_toggle_req
                    p_sweep_on = panel.sweep_on
                    p_sweep_start_current = panel.sweep_start_current_req
                    p_sweep_start_all = panel.sweep_start_all_algos_req
                    p_sweep_abort = panel.sweep_abort_req

                # camera controls
                if p_cam_focus:
                    follow_mode["on"] = False
                    restore_camera_origin()
                    with panel.lock:
                        panel.cam_focus_req = False

                if p_cam_follow_toggle:
                    toggle_follow()
                    with panel.lock:
                        panel.cam_follow_toggle_req = False

                # abort sweep (X)
                if p_sweep_abort:
                    with panel.lock:
                        panel.sweep_abort_req = False
                    stop_sweep(reason="ABORT")

                # Y：sweep all
                if p_sweep_start_all:
                    with panel.lock:
                        panel.sweep_start_all_algos_req = False
                    start_sweep_all_algos()

                # T：sweep current algo
                if p_sweep_start_current:
                    with panel.lock:
                        panel.sweep_start_current_req = False
                    start_sweep_current_algo(p_swim_mode)

                # P：toggle sweep（可中止）
                if p_sweep_toggle:
                    with panel.lock:
                        panel.sweep_toggle_req = False
                        panel.sweep_on = not panel.sweep_on
                        p_sweep_on = panel.sweep_on

                    if p_sweep_on:
                        if sweep_algo_locked is None:
                            start_sweep_current_algo(p_swim_mode)
                    else:
                        stop_sweep(reason="TOGGLE_OFF")

                # 手動錄影 toggle
                if p_rec_toggle:
                    with panel.lock:
                        panel.rec_toggle_req = False
                        panel.rec_on = not panel.rec_on
                        rec_on = panel.rec_on

                    if rec_on:
                        fname = (
                            f"{time.strftime('%Y%m%d_%H%M%S')}_"
                            f"{p_swim_mode}_{p_wave_type}_"
                            f"A{p_amp:.2f}_F{p_freq:.2f}_K{p_step:.2f}_B{p_turn:+.2f}.mp4"
                        )
                        manual_recorder.start(width=RENDER_W, height=RENDER_H, filename=fname)
                    else:
                        manual_recorder.stop()

                if p_reset:
                    hard_reset_and_forward()
                    with panel.lock:
                        panel.reset_request = False
                    for sw in swimmers.values():
                        sw.reset(num_j)
                    print("\n[RESET] Physics state restored.")

                # motor failure (kp=0)
                n = min(6, env.model.nu, kp_default.shape[0])
                for i in range(n):
                    env.model.actuator_gainprm[i, 0] = kp_default[i] if p_motor_on[i] else 0.0

                # ===========================
                # SWEEP state machine（加速版：每 loop 內跑多步）
                # ===========================
                if p_sweep_on and sweep_algo_locked is not None:
                    if sweep_idx >= len(sweep_cases):
                        if sweep_case_recorder is not None:
                            sweep_case_recorder.stop()
                            sweep_case_recorder = None

                        if len(sweep_all_queue) > 0:
                            next_algo = sweep_all_queue.pop(0)
                            print(f"[SWEEP] NEXT ALGO -> {next_algo}")
                            _start_sweep_algo(next_algo)
                        else:
                            with panel.lock:
                                panel.sweep_on = False
                                panel.sweep_status = f"SWEEP: DONE [{sweep_algo_locked}]"
                                panel.paused = True
                            print("[SWEEP] DONE")
                            sweep_algo_locked = None
                            try:
                                if metrics is not None:
                                    metrics.close()
                                    metrics = None
                            except:
                                pass

                    else:
                        if not sweep_trial_active:
                            case = sweep_cases[sweep_idx]
                            sweep_trial_active = True
                            sweep_trial_start_walltime = time.time()

                            with panel.lock:
                                panel.amp = case["amp"]
                                panel.freq = case["freq"]
                                panel.phase_step = case["step"]
                                panel.turn_bias = case["turn"]
                                panel.swim_mode = case["algo"]
                                panel.wave_type = case["wave"]

                                panel.vars["amp"].set(f"{case['amp']:.3f}")
                                panel.vars["freq"].set(f"{case['freq']:.3f}")
                                panel.vars["phase_step"].set(f"{case['step']:.3f}")
                                panel.vars["turn_bias"].set(f"{case['turn']:.3f}")
                                panel.mode_var.set(case["algo"])
                                panel.wave_var.set("行進波")
                                panel.sweep_status = (
                                    f"SWEEP: [{case['algo']}] ({sweep_idx+1}/{len(sweep_cases)}) "
                                    f"A{case['amp']:.2f} F{case['freq']:.2f} K{case['step']:.2f}"
                                )
                                panel.paused = False

                            hard_reset_and_forward()
                            for sw in swimmers.values():
                                sw.reset(num_j)

                            out_dir = os.path.join(SWEEP_VIDEO_DIR_BASE, case["algo"])
                            sweep_case_recorder = AsyncVideoRecorder(out_dir=out_dir, fps=RENDER_FPS)

                            fname = (
                                f"{case['algo']}_idx{case['idx']:04d}_"
                                f"A{case['amp']:.2f}_F{case['freq']:.2f}_"
                                f"K{case['step']:.2f}_B{case['turn']:+.2f}.mp4"
                            )
                            sweep_case_recorder.start(width=RENDER_W, height=RENDER_H, filename=fname)
                            print("[SWEEP] START CASE:", case)

                        else:
                            # ✅ 讀目前參數
                            with panel.lock:
                                p_amp = panel.amp
                                p_freq = panel.freq
                                p_step = panel.phase_step
                                p_turn = panel.turn_bias
                                p_swim_mode = panel.swim_mode
                                p_wave_type = panel.wave_type

                            case = sweep_cases[sweep_idx]
                            phase_offset = case["phase_offset"]

                            swimmer = swimmers.get(p_swim_mode, legacy)
                            if hasattr(swimmer, "set_dt"):
                                swimmer.set_dt(env.model.opt.timestep)

                            # ✅【加速核心】一次 UI loop 跑多個 mj_step
                            collided = False
                            timeout = False
                            for _ in range(int(max(1, SIM_SUBSTEPS_PER_UI))):
                                t_eff = env.data.time + phase_offset_to_time_offset(phase_offset, p_freq)

                                sp = SwimParams(
                                    amp=p_amp, freq=p_freq, step=p_step, turn=p_turn,
                                    wave_type=p_wave_type, auto_mode=True
                                )
                                ctrl = swimmer.compute_ctrl(t=t_eff, num_joints=num_j, p=sp)

                                with viewer.lock():
                                    env.data.ctrl[:] = np.clip(ctrl, CTRL_CLIP_MIN, CTRL_CLIP_MAX)
                                    mujoco.mj_step(env.model, env.data)

                                # end condition：只看撞牆（可選 timeout）
                                collided = check_goal_contact()
                                if TRIAL_MAX_SEC is not None:
                                    timeout = (time.time() - sweep_trial_start_walltime) > float(TRIAL_MAX_SEC)

                                if collided or timeout:
                                    break

                                # ✅ record frames（sweep）- 在影片中加上時間文字
                                if sweep_case_recorder is not None and sweep_case_recorder.is_recording():
                                    sim_steps = int(env.data.time / env.model.opt.timestep)
                                    if sim_steps % recording_interval == 0:
                                        renderer.update_scene(env.data, camera=viewer.cam)
                                        frame = renderer.render()
                                        frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
                                        
                                        # 加入時間浮水印
                                        cv2.putText(frame_bgr, f"Time: {env.data.time:.2f}s", (10, 30), 
                                                    cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
                                        sweep_case_recorder.push(frame_bgr)

                            # telemetry（用最後狀態）
                            speed = np.linalg.norm(env.data.qvel[0:2])
                            z_pos = env.data.qpos[2]
                            passive_z_force = env.data.qfrc_passive[2]
                            with panel.lock:
                                panel.current_speed = speed
                                panel.current_z = z_pos
                                panel.current_passive_z = passive_z_force

                            if collided or timeout:
                                reason = "COLLISION" if collided else "TIMEOUT"
                                x_end = float(env.data.qpos[0])
                                y_end = float(env.data.qpos[1])
                                z_end = float(env.data.qpos[2])
                                sim_t_end = float(env.data.time)
                                wall_dt = float(time.time() - sweep_trial_start_walltime)

                                print(f"[SWEEP] END CASE idx={sweep_idx} reason={reason} x={x_end:.3f}")

                                # ✅ log metrics
                                try:
                                    if metrics is not None:
                                        metrics.log_case(
                                            algo=case["algo"], idx=case["idx"],
                                            amp=case["amp"], freq=case["freq"], k_step=case["step"],
                                            phase_offset=case["phase_offset"], turn=case["turn"],
                                            reason=reason, sim_time_end=sim_t_end, wall_time_sec=wall_dt,
                                            x_end=x_end, y_end=y_end, z_end=z_end,
                                        )
                                except Exception as e:
                                    print("[WARN] metrics log failed:", e)

                                if sweep_case_recorder is not None:
                                    sweep_case_recorder.stop()
                                    sweep_case_recorder = None

                                sweep_idx += 1
                                sweep_trial_active = False

                # ===========================
                # normal manual (non-sweep)（加速版）
                # ===========================
                if (not p_sweep_on) and (not p_paused):
                    swimmer = swimmers.get(p_swim_mode, legacy)
                    if hasattr(swimmer, "set_dt"):
                        swimmer.set_dt(env.model.opt.timestep)

                    for _ in range(int(max(1, SIM_SUBSTEPS_PER_UI))):
                        sp = SwimParams(
                            amp=p_amp, freq=p_freq, step=p_step, turn=p_turn,
                            wave_type=p_wave_type, auto_mode=False
                        )
                        ctrl = swimmer.compute_ctrl(t=env.data.time, num_joints=num_j, p=sp)

                        with viewer.lock():
                            env.data.ctrl[:] = np.clip(ctrl, CTRL_CLIP_MIN, CTRL_CLIP_MAX)
                            mujoco.mj_step(env.model, env.data)

                        if manual_recorder.is_recording():
                            sim_steps = int(env.data.time / env.model.opt.timestep)
                            if sim_steps % recording_interval == 0:
                                renderer.update_scene(env.data, camera=viewer.cam)
                                frame = renderer.render()
                                frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
                                
                                # 加入時間浮水印
                                cv2.putText(frame_bgr, f"Time: {env.data.time:.2f}s", (10, 30), 
                                            cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2)
                                manual_recorder.push(frame_bgr)

                    speed = np.linalg.norm(env.data.qvel[0:2])
                    z_pos = env.data.qpos[2]
                    passive_z_force = env.data.qfrc_passive[2]
                    with panel.lock:
                        panel.current_speed = speed
                        panel.current_z = z_pos
                        panel.current_passive_z = passive_z_force

                # camera follow
                if follow_mode["on"]:
                    pos = env.data.xpos[base_id]
                    with viewer.lock():
                        viewer.cam.lookat[:] = pos
                        viewer.cam.distance  = FOCUS_DIST
                        viewer.cam.elevation = FOCUS_ELEV
                        viewer.cam.azimuth   = FOCUS_AZIM

                viewer.sync()

    finally:
        manual_recorder.stop()
        if sweep_case_recorder is not None:
            sweep_case_recorder.stop()
        try:
            if metrics is not None:
                metrics.close()
        except:
            pass
        renderer.close()