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


# =========================
# CONFIG
# =========================
STEER_FRONT_N_INIT = 4
STEER_GAIN_INIT    = 0.70

CTRL_CLIP_MIN = -1.2
CTRL_CLIP_MAX =  1.2

FOCUS_DIST = 1.9
FOCUS_ELEV = -75.0
FOCUS_AZIM = 0.0

RENDER_W, RENDER_H, RENDER_FPS = 640, 480, 30

# ===== SWEEP CONFIG =====
TRIAL_MAX_SEC = None  # ✅ None = 不用 timeout（只撞牆才換）
SWEEP_AMPS  = [0.35, 0.50]
SWEEP_FREQS = [0.8, 1.0, 1.2]
SWEEP_PHASE_OFFSETS = [0.0, np.pi/2, np.pi]
SWEEP_STEPS = [0.40, 0.50, 0.60]
SWEEP_TURN_BIAS = 0.0
SWEEP_VIDEO_DIR_BASE = "videos_sweep"
SWEEP_AUTO_FOLLOW_ON_START = True

# ✅ 你要偵測撞牆的 geom 名稱（不對就改這裡）
BASE_GEOM_NAME = "base_link_collision"
WALL_GEOM_NAME = "wall_front"
# =========================


def build_sweep_cases(algo: str):
    cases = []
    idx = 0
    for a in SWEEP_AMPS:
        for f in SWEEP_FREQS:
            for ph in SWEEP_PHASE_OFFSETS:
                for s in SWEEP_STEPS:
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
    sweep_algo_locked = None

    legacy = LegacySwimmer(steer_front_n=STEER_FRONT_N_INIT, steer_gain=STEER_GAIN_INIT)
    kuramoto = KuramotoSwimmer(
        coupling=10.0, substeps=5, taper_head=0.35, taper_tail=1.0,
        steer_gain=0.70, steer_front_n=STEER_FRONT_N_INIT, steer_sign=1.0,
    )
    cpg = CPGSwimmer(steer_front_n=STEER_FRONT_N_INIT)
    swimmers = {"Legacy": legacy, "Kuramoto": kuramoto, "CPG": cpg}

    try:
        with mujoco.viewer.launch_passive(env.model, env.data) as viewer:
            viewer.cam.azimuth, viewer.cam.elevation, viewer.cam.distance = 0, -90.0, 13.0
            viewer.cam.lookat = [0, 0, 0]

            cam_origin = {
                "lookat": np.array(viewer.cam.lookat, dtype=float),
                "distance": float(viewer.cam.distance),
                "elevation": float(viewer.cam.elevation),
                "azimuth": float(viewer.cam.azimuth),
            }

            base_id = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_BODY, "base_link")
            follow_mode = {"on": False}

            # ===== collision geom ids（找不到就 disable collision check）=====
            geom_base = None
            geom_wall = None
            try:
                geom_base = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_GEOM, BASE_GEOM_NAME)
                geom_wall = mujoco.mj_name2id(env.model, mujoco.mjtObj.mjOBJ_GEOM, WALL_GEOM_NAME)
            except:
                print(f"[WARN] 找不到 geom：{BASE_GEOM_NAME} / {WALL_GEOM_NAME}，撞牆偵測將失效（case 會永遠不換）")
            # ================================================================

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

            # ---------------- SWEEP control ----------------
            def start_sweep_for_algo(algo_name: str):
                nonlocal sweep_case_recorder, sweep_cases, sweep_idx, sweep_trial_active, sweep_trial_start_walltime, sweep_algo_locked
                sweep_algo_locked = algo_name
                sweep_cases = build_sweep_cases(algo_name)
                sweep_idx = 0
                sweep_trial_active = False
                sweep_trial_start_walltime = 0.0

                if sweep_case_recorder is not None:
                    sweep_case_recorder.stop()
                    sweep_case_recorder = None

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

            def stop_sweep():
                nonlocal sweep_case_recorder, sweep_trial_active, sweep_algo_locked
                if sweep_case_recorder is not None:
                    sweep_case_recorder.stop()
                    sweep_case_recorder = None
                sweep_trial_active = False
                sweep_algo_locked = None
                with panel.lock:
                    panel.sweep_on = False
                    panel.sweep_status = "SWEEP: OFF"
                print("[SWEEP] STOP")

            # ---------------- main loop ----------------
            while viewer.is_running():
                step_start = time.time()

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

                # T：一鍵開始 sweep（依當下演算法）
                if p_sweep_start_current:
                    with panel.lock:
                        panel.sweep_start_current_req = False
                    stop_sweep()
                    start_sweep_for_algo(p_swim_mode)

                # P：toggle sweep
                if p_sweep_toggle:
                    with panel.lock:
                        panel.sweep_toggle_req = False
                        panel.sweep_on = not panel.sweep_on
                        p_sweep_on = panel.sweep_on
                    if p_sweep_on:
                        start_sweep_for_algo(p_swim_mode)
                    else:
                        stop_sweep()

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
                            f"A{p_amp:.2f}_F{p_freq:.2f}_S{p_step:.2f}_B{p_turn:+.2f}.mp4"
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
                # SWEEP state machine
                # ✅ 只撞牆才換（完全不看 X_REACH）
                # ===========================
                if p_sweep_on and sweep_algo_locked is not None:
                    if sweep_idx >= len(sweep_cases):
                        if sweep_case_recorder is not None:
                            sweep_case_recorder.stop()
                            sweep_case_recorder = None
                        with panel.lock:
                            panel.sweep_on = False
                            panel.sweep_status = f"SWEEP: DONE [{sweep_algo_locked}]"
                            panel.paused = True
                        print("[SWEEP] DONE")
                        sweep_algo_locked = None
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
                                    f"SWEEP: ON [{case['algo']}] ({sweep_idx+1}/{len(sweep_cases)}) "
                                    f"A{case['amp']:.2f} F{case['freq']:.2f} off{case['phase_offset']:.2f}rad"
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
                                f"OFF{case['phase_offset']:.2f}rad_S{case['step']:.2f}_B{case['turn']:+.2f}.mp4"
                            )
                            sweep_case_recorder.start(width=RENDER_W, height=RENDER_H, filename=fname)
                            print("[SWEEP] START CASE:", case)

                        else:
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

                            t_eff = env.data.time + phase_offset_to_time_offset(phase_offset, p_freq)

                            sp = SwimParams(
                                amp=p_amp, freq=p_freq, step=p_step, turn=p_turn,
                                wave_type=p_wave_type, auto_mode=True
                            )
                            ctrl = swimmer.compute_ctrl(t=t_eff, num_joints=num_j, p=sp)

                            with viewer.lock():
                                env.data.ctrl[:] = np.clip(ctrl, CTRL_CLIP_MIN, CTRL_CLIP_MAX)
                                mujoco.mj_step(env.model, env.data)

                            speed = np.linalg.norm(env.data.qvel[0:2])
                            z_pos = env.data.qpos[2]
                            passive_z_force = env.data.qfrc_passive[2]
                            with panel.lock:
                                panel.current_speed = speed
                                panel.current_z = z_pos
                                panel.current_passive_z = passive_z_force

                            # record frames（sweep）
                            if sweep_case_recorder is not None and sweep_case_recorder.is_recording():
                                sim_steps = int(env.data.time / env.model.opt.timestep)
                                if sim_steps % recording_interval == 0:
                                    renderer.update_scene(env.data, camera=viewer.cam)
                                    frame = renderer.render()
                                    frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
                                    sweep_case_recorder.push(frame_bgr)

                            # ✅ end condition：只看撞牆（可選 timeout）
                            collided = check_goal_contact()
                            timeout = False
                            if TRIAL_MAX_SEC is not None:
                                timeout = (time.time() - sweep_trial_start_walltime) > float(TRIAL_MAX_SEC)

                            if collided or timeout:
                                reason = "COLLISION" if collided else "TIMEOUT"
                                x_pos = float(env.data.qpos[0])
                                print(f"[SWEEP] END CASE idx={sweep_idx} reason={reason} x={x_pos:.3f}")

                                if sweep_case_recorder is not None:
                                    sweep_case_recorder.stop()
                                    sweep_case_recorder = None

                                sweep_idx += 1
                                sweep_trial_active = False

                                hard_reset_and_forward()
                                for sw in swimmers.values():
                                    sw.reset(num_j)

                # ===========================
                # normal manual (non-sweep)
                # ===========================
                if (not p_sweep_on) and (not p_paused):
                    swimmer = swimmers.get(p_swim_mode, legacy)
                    if hasattr(swimmer, "set_dt"):
                        swimmer.set_dt(env.model.opt.timestep)

                    sp = SwimParams(
                        amp=p_amp, freq=p_freq, step=p_step, turn=p_turn,
                        wave_type=p_wave_type, auto_mode=False
                    )
                    ctrl = swimmer.compute_ctrl(t=env.data.time, num_joints=num_j, p=sp)

                    with viewer.lock():
                        env.data.ctrl[:] = np.clip(ctrl, CTRL_CLIP_MIN, CTRL_CLIP_MAX)
                        mujoco.mj_step(env.model, env.data)

                    speed = np.linalg.norm(env.data.qvel[0:2])
                    z_pos = env.data.qpos[2]
                    passive_z_force = env.data.qfrc_passive[2]
                    with panel.lock:
                        panel.current_speed = speed
                        panel.current_z = z_pos
                        panel.current_passive_z = passive_z_force

                    if manual_recorder.is_recording():
                        sim_steps = int(env.data.time / env.model.opt.timestep)
                        if sim_steps % recording_interval == 0:
                            renderer.update_scene(env.data, camera=viewer.cam)
                            frame = renderer.render()
                            frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
                            manual_recorder.push(frame_bgr)

                # camera follow
                if follow_mode["on"]:
                    pos = env.data.xpos[base_id]
                    with viewer.lock():
                        viewer.cam.lookat[:] = pos
                        viewer.cam.distance  = FOCUS_DIST
                        viewer.cam.elevation = FOCUS_ELEV
                        viewer.cam.azimuth   = FOCUS_AZIM

                viewer.sync()
                dt = env.model.opt.timestep
                elapsed = time.time() - step_start
                if dt > elapsed:
                    time.sleep(dt - elapsed)

    finally:
        manual_recorder.stop()
        if sweep_case_recorder is not None:
            sweep_case_recorder.stop()
        renderer.close()