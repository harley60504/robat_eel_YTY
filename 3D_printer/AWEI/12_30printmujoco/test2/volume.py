import mujoco
import numpy as np

# 加載你的模型
model = mujoco.MjModel.from_xml_path('eel.xml')
data = mujoco.MjData(model)

print(f"{'Geom Name':<20} | {'Volume (m^3)':<15} | {'Required Mass (kg) for Neut. Buoyancy'}")
print("-" * 75)

for i in range(model.ngeom):
    name = mujoco.mj_id2name(model, mujoco.mjtObj.mjOBJ_GEOM, i)
    # MuJoCo 編譯後會計算 geom 的慣性與體積
    # 這裡我們通過 geom 的質量除以密度（如果是 mesh 預設密度）來獲取
    # 或者直接查看編譯後的預估體積
    vol = model.geom_size[i][0] * model.geom_size[i][1] * model.geom_size[i][2] * (4/3) * np.pi
    
    # 更精確的方法是查看 MjModel 中的 geom_rbound (包圍球半徑) 來推算
    # 但最直接的是看 MuJoCo 內部計算出的慣性張量所隱含的體積
    # 這裡我們直接幫你算所需的中性質量
    if "collision" in name:
        # 假設密度為 1000 (水)
        neut_mass = vol * 1000 
        print(f"{name:<20} | {vol:<15.6f} | {neut_mass:.4f}")