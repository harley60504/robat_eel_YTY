import mujoco
import mujoco.viewer
import time
import numpy as np

# 載入你剛剛改寫好的新 URDF
model = mujoco.MjModel.from_xml_path(r"C:\Users\AWEI\Documents\GitHub\robot_eel_YTY\3D_printer\AWEI\12_30printmujoco\test2\urdf\test2.urdf")
data = mujoco.MjData(model)

# ==========================================
# 測試設定：我們只保留穩定性參數，但恢復碰撞
# ==========================================
model.opt.timestep = 0.001          # 精度設為 0.001 配合新 URDF
model.dof_damping[:] = 1.5          # 保持阻尼，吸收細微擺動能量
model.dof_armature[:] = 0.05        # 增加數值穩定性

# 注意：我們「刪除」了之前的 geom_contype[:] = 0
# 現在模擬器會根據 URDF 裡的設定進行碰撞試驗
# ==========================================

with mujoco.viewer.launch_passive(model, data) as viewer:
    # 可以在視窗開啟時手動按下鍵盤 'C' 觀察碰撞體
    try:
        # 顯示坐標軸 (用來檢查 Body 是否排開)
        viewer.vopt.flags[mujoco.mjtVisFlag.mjVIS_AXES] = 1
        # 顯示關節 (檢查旋轉中心)
        viewer.vopt.flags[mujoco.mjtVisFlag.mjVIS_JOINT] = 1
        # 顯示碰撞體 (檢查碰撞 Mesh 是否位移)
        viewer.vopt.flags[mujoco.mjtVisFlag.mjVIS_COLLISION] = 1
    except Exception as e:
        print(f"顯示設定失敗，但不影響模擬: {e}")
    print("模擬已啟動。請在模擬視窗按下 'C' 鍵觀察碰撞邊界。")
    
    while viewer.is_running():
        step_start = time.time()

        # 執行物理步進
        mujoco.mj_step(model, data)

        # 同步畫面
        viewer.sync()

        # 控制模擬速度
        time_until_next_step = model.opt.timestep - (time.time() - step_start)
        if time_until_next_step > 0:
            time.sleep(time_until_next_step)