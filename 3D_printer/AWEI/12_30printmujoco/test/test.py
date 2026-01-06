
# import mujoco

# urdf_path = r"C:\Users\AWEI\Documents\GitHub\robot_eel_YTY\3D_printer\AWEI\12_30printmujoco\test\urdf\test.urdf"
# model = mujoco.MjModel.from_xml_path(urdf_path)

# out_xml = r"C:\Users\AWEI\Documents\GitHub\robot_eel_YTY\3D_printer\AWEI\12_30printmujoco\test\urdf\test.xml"
# mujoco.mj_saveLastXML(out_xml, model)

# print("Saved:", out_xml)
import mujoco
import mujoco.viewer
import time

# 載入你的模型（將 test.urdf 放在同一個資料夾）
model = mujoco.MjModel.from_xml_path(r"C:\Users\AWEI\Documents\GitHub\robot_eel_YTY\3D_printer\AWEI\12_30printmujoco\test\urdf\test.urdf")
data = mujoco.MjData(model)

# 開啟模擬器視窗
with mujoco.viewer.launch_passive(model, data) as viewer:
    # 檢查視窗是否還在執行
    while viewer.is_running():
        step_start = time.time()

        # 物理運算步進
        mujoco.mj_step(model, data)

        # 同步更新畫面
        viewer.sync()

        # 維持模擬頻率與真實時間一致
        time_until_next_step = model.opt.timestep - (time.time() - step_start)
        if time_until_next_step > 0:
            time.sleep(time_until_next_step)