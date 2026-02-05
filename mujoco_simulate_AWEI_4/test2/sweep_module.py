# sweep_module.py
import os
import time
import numpy as np
import mujoco
import cv2
import gc
from swimmers.base import SwimParams

# ✅ 加速參數（不降畫質/不降FPS）
ACCEL_FACTOR = 128             # 建議 64/128/256
UI_SYNC_EVERY_N_FRAMES =  1    # 每 N 個錄影幀 sync 一次

def execute_sweep_logic(env, swimmers, panel, initial_qpos, build_cases_func,
                        render_cfg, sweep_cfg, phase_offset_func, check_contact_func, viewer):
    """
    Sweep 加速版（不降解析度/不降 FPS）：
    - physics 暴衝（ACCEL_FACTOR）
    - 只在錄影幀才渲染
    - viewer.sync 降頻
    - sweep 不 sleep
    - ✅ Tkinter 更新用 root.after 丟回主執行緒（避免卡死）
    - ✅ SwimParams 補 wave_type（修正你遇到的 TypeError）
    """
    from recorder import AsyncVideoRecorder
    from metrics_logger import MetricsLogger

    # ✅ Tk 安全呼叫：回主執行緒做 .set()
    def ui_call(fn):
        try:
            panel.root.after(0, fn)
        except:
            pass

    with panel.lock:
        algo_name = panel.swim_mode
        panel.sweep_on = True

    cases = build_cases_func(algo_name)
    metrics_dir = os.path.join(sweep_cfg["OUT_DIR"], algo_name)
    os.makedirs(metrics_dir, exist_ok=True)

    metrics = MetricsLogger(out_dir=metrics_dir, filename="metrics.csv")
    num_j = len(env.data.ctrl)

    print(f"[SWEEP] 啟動成功: {algo_name}, 總計 {len(cases)} 組案例...")

    # 錄影幀率對應「每幾步取一幀」
    recording_interval = max(1, int((1.0 / render_cfg["FPS"]) / env.model.opt.timestep))

    for idx, case in enumerate(cases):
        # ---- (1) 檢查是否中止 + 更新 panel 純資料 ----
        with panel.lock:
            if (not panel.sweep_on) or panel.sweep_abort_req:
                print("[SWEEP] 使用者中止。")
                break

            panel.sweep_status = f"SWEEP: {idx+1}/{len(cases)}"
            panel.amp, panel.freq, panel.phase_step = case["amp"], case["freq"], case["step"]
            panel.paused = False

        # ✅ Tkinter 的 .set() 必須回主執行緒
        ui_call(lambda a=case["amp"]: panel.vars["amp"].set(f"{a:.3f}"))
        ui_call(lambda f=case["freq"]: panel.vars["freq"].set(f"{f:.3f}"))
        ui_call(lambda s=case["step"]: panel.vars["phase_step"].set(f"{s:.3f}"))
        ui_call(lambda name=case.get("algo", algo_name): panel.mode_var.set(name))

        # ---- (2) 開檔/renderer/重置 ----
        formatted_fname = (
            f"{algo_name}_idx{case['idx']:04d}_"
            f"A{case['amp']:.2f}_F{case['freq']:.2f}_"
            f"K{case['step']:.2f}_B+0.00.mp4"
        )

        gc.collect()
        time.sleep(0.2)  # Windows 檔案鎖保險

        recorder = AsyncVideoRecorder(out_dir=metrics_dir, fps=render_cfg["FPS"])
        recorder.start(width=render_cfg["W"], height=render_cfg["H"], filename=formatted_fname)
        time.sleep(0.2)  # 給 writer 啟動時間

        tmp_renderer = mujoco.Renderer(env.model, height=render_cfg["H"], width=render_cfg["W"])

        mujoco.mj_resetData(env.model, env.data)
        env.data.qpos[:] = initial_qpos
        mujoco.mj_forward(env.model, env.data)

        swimmer = swimmers.get(algo_name)
        swimmer.reset(num_j)
        if hasattr(swimmer, "set_dt"):
            swimmer.set_dt(env.model.opt.timestep)

        collided, timeout = False, False
        start_wall = time.time()
        sync_ctr = 0

        # ---- (3) 模擬迴圈（加速核心）----
        while viewer.is_running() and not (collided or timeout):

            # 物理暴衝
            for _ in range(ACCEL_FACTOR):
                t_eff = env.data.time + phase_offset_func(case.get("phase_offset", 0.0), case["freq"])

                # ✅ 修正：SwimParams 必填 wave_type
                sp = SwimParams(
                    amp=case["amp"],
                    freq=case["freq"],
                    step=case["step"],
                    turn=0.0,
                    wave_type=case.get("wave", "Traveling"),
                    auto_mode=True
                )

                ctrl = swimmer.compute_ctrl(t=t_eff, num_joints=num_j, p=sp)

                with viewer.lock():
                    env.data.ctrl[:] = np.clip(ctrl, -1.2, 1.2)
                    mujoco.mj_step(env.model, env.data)

                if check_contact_func():
                    collided = True
                    break

            # timeout
            if (time.time() - start_wall) > sweep_cfg["TIMEOUT"]:
                timeout = True

            # 只在需要錄影幀才渲染
            sim_steps = int(round(env.data.time / env.model.opt.timestep))
            if sim_steps % recording_interval < ACCEL_FACTOR:
                if recorder.is_recording():
                    with viewer.lock():
                        tmp_renderer.update_scene(env.data, camera=viewer.cam)
                    frame = tmp_renderer.render()
                    frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
                    cv2.putText(frame_bgr, f"Time: {env.data.time:.2f}s", (50, 50),
                                cv2.FONT_HERSHEY_SIMPLEX, 1.2, (0, 255, 0), 2)
                    recorder.push(frame_bgr)

                    # viewer.sync 降頻
                    sync_ctr += 1
                    if sync_ctr % UI_SYNC_EVERY_N_FRAMES == 0:
                        viewer.sync()
            else:
                # 非錄影幀更低頻 sync
                if sync_ctr % (UI_SYNC_EVERY_N_FRAMES * 3) == 0:
                    viewer.sync()

            with panel.lock:
                if panel.sweep_abort_req:
                    break

        # ---- (4) 收尾 ----
        recorder.stop()
        tmp_renderer.close()

        try:
            metrics.log_case(
                algo=algo_name, idx=case["idx"],
                amp=case["amp"], freq=case["freq"], k_step=case["step"],
                phase_offset=case.get("phase_offset", 0.0),
                turn=0.0,
                reason="COL" if collided else "TO",
                sim_time_end=env.data.time,
                wall_time_sec=(time.time() - start_wall),
                x_end=float(env.data.qpos[0]) if env.data.qpos.size > 0 else 0.0,
                y_end=float(env.data.qpos[1]) if env.data.qpos.size > 1 else 0.0,
                z_end=float(env.data.qpos[2]) if env.data.qpos.size > 2 else 0.0,
            )
        except:
            pass

        del tmp_renderer
        del recorder
        gc.collect()

        print(f"[SWEEP] Case {idx+1} 完成，冷卻中...")
        time.sleep(1.0)

    metrics.close()
    with panel.lock:
        panel.sweep_on = False
        panel.sweep_status = "FINISHED"
