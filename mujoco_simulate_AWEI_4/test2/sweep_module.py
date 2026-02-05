# sweep_module.py
import os
import time
import numpy as np
import mujoco
import cv2
import gc
from swimmers.base import SwimParams

def execute_sweep_logic(env, swimmers, panel, initial_qpos, build_cases_func, 
                        render_cfg, sweep_cfg, phase_offset_func, check_contact_func, viewer):
    """
    專門處理 Sweep 迴圈的獨立模組，強化了 I/O 冷卻機制，徹底解決 15 組後卡死的問題。
    """
    from recorder import AsyncVideoRecorder
    from metrics_logger import MetricsLogger

    with panel.lock:
        algo_name = panel.swim_mode
        panel.sweep_on = True

    cases = build_cases_func(algo_name)
    metrics_dir = os.path.join(sweep_cfg["OUT_DIR"], algo_name)
    if not os.path.exists(metrics_dir):
        os.makedirs(metrics_dir)
    
    metrics = MetricsLogger(out_dir=metrics_dir, filename="metrics.csv")
    num_j = len(env.data.ctrl)

    print(f"[SWEEP] 啟動成功: {algo_name}, 總計 {len(cases)} 組案例...")

    for idx, case in enumerate(cases):
        with panel.lock:
            if not panel.sweep_on or panel.sweep_abort_req:
                print("[SWEEP] 使用者中止。")
                break
            # 更新介面
            panel.sweep_status = f"SWEEP: {idx+1}/{len(cases)}"
            panel.amp, panel.freq, panel.phase_step = case["amp"], case["freq"], case["step"]
            panel.vars["amp"].set(f"{case['amp']:.3f}")
            panel.vars["freq"].set(f"{case['freq']:.3f}")
            panel.vars["phase_step"].set(f"{case['step']:.3f}")
            panel.mode_var.set(case["algo"])
            panel.paused = False

        # --- A. 初始化與檔案系統保護 ---
        formatted_fname = (f"{algo_name}_idx{case['idx']:04d}_"
                           f"A{case['amp']:.2f}_F{case['freq']:.2f}_"
                           f"K{case['step']:.2f}_B+0.00.mp4")

        # ✅ 強化回收：在開新檔案前，先強迫 GC 一次
        gc.collect()
        time.sleep(0.5) 

        # 初始化錄影機 (此步驟最容易卡死，故獨立出來)
        recorder = AsyncVideoRecorder(out_dir=metrics_dir, fps=render_cfg["FPS"])
        recorder.start(width=render_cfg["W"], height=render_cfg["H"], filename=formatted_fname)
        time.sleep(0.5) # 給 ffmpeg 一點啟動時間

        # 初始化渲染器
        tmp_renderer = mujoco.Renderer(env.model, height=render_cfg["H"], width=render_cfg["W"])

        # 重置物理
        mujoco.mj_resetData(env.model, env.data)
        env.data.qpos[:] = initial_qpos
        mujoco.mj_forward(env.model, env.data)
        swimmer = swimmers.get(algo_name)
        swimmer.reset(num_j)
        if hasattr(swimmer, "set_dt"):
            swimmer.set_dt(env.model.opt.timestep)

        collided, timeout, start_wall = False, False, time.time()
        recording_interval = max(1, int((1.0 / render_cfg["FPS"]) / env.model.opt.timestep))

        # --- B. 模擬循環 ---
        while viewer.is_running() and not (collided or timeout):
            # 物理運算 (加速區)
            for _ in range(sweep_cfg["SUBSTEPS"]):
                t_eff = env.data.time + phase_offset_func(case["phase_offset"], case["freq"])
                sp = SwimParams(amp=case["amp"], freq=case["freq"], step=case["step"], turn=0.0, auto_mode=True)
                ctrl = swimmer.compute_ctrl(t=t_eff, num_joints=num_j, p=sp)
                
                with viewer.lock():
                    env.data.ctrl[:] = np.clip(ctrl, -1.2, 1.2)
                    mujoco.mj_step(env.model, env.data)
                
                if check_contact_func():
                    collided = True
                    break
            
            # 渲染判定
            sim_steps = int(round(env.data.time / env.model.opt.timestep))
            if sim_steps % recording_interval < sweep_cfg["SUBSTEPS"]:
                if recorder.is_recording():
                    with viewer.lock():
                        tmp_renderer.update_scene(env.data, camera=viewer.cam)
                    frame = tmp_renderer.render()
                    frame_bgr = cv2.cvtColor(frame, cv2.COLOR_RGB2BGR)
                    cv2.putText(frame_bgr, f"Time: {env.data.time:.2f}s", (50, 50), 
                                cv2.FONT_HERSHEY_SIMPLEX, 1.2, (0, 255, 0), 2)
                    recorder.push(frame_bgr)

            viewer.sync()
            time.sleep(0.001)

            if (time.time() - start_wall) > sweep_cfg["TIMEOUT"]:
                timeout = True
            
            with panel.lock:
                if panel.sweep_abort_req: break

        # --- C. 深度回收與強力冷卻 ---
        # 1. 停止錄影
        recorder.stop()
        # 2. 關閉渲染器
        tmp_renderer.close()
        
        # 3. 紀錄數據
        try:
            metrics.log_case(algo=algo_name, idx=case["idx"], amp=case["amp"], freq=case["freq"], 
                             k_step=case["step"], reason="COL" if collided else "TO", 
                             sim_time_end=env.data.time, x_end=env.data.qpos[0])
        except: pass
        
        # 4. 暴力釋放
        del tmp_renderer
        del recorder
        gc.collect()
        
        # ✅ 救命延遲：對於 1080p 60fps，3 秒鐘是保證硬碟寫入不塞車的安全線
        print(f"[SWEEP] Case {idx+1} 已完成，強制冷卻中...")
        time.sleep(3.0) 

    metrics.close()
    with panel.lock:
        panel.sweep_on = False
        panel.sweep_status = "FINISHED"