import numpy as np
from stl import mesh

# 讀取 STL 檔案
your_mesh = mesh.Mesh.from_file('meshes/stl/base_link_visual.stl')

# 計算體積
volume, cog, inertia = your_mesh.get_mass_properties()
print(f"體積: {volume} m³")
print(f"體積: {volume * 1e6} cm³")

# 對所有部件重複
stl_files = ['base_link_visual.stl', 'link1_visual.stl', 
             'link2_visual.stl', 'link3_visual.stl',
             'link4_visual.stl', 'link5_visual.stl', 
             'link6_visual.stl']

total_volume = 0
for filename in stl_files:
    m = mesh.Mesh.from_file(f'meshes/stl/{filename}')
    vol, _, _ = m.get_mass_properties()
    total_volume += vol
    print(f"{filename}: {vol*1e6:.2f} cm³")

print(f"\n總體積: {total_volume*1e6:.2f} cm³")
print(f"總質量: 2.21 kg")
print(f"平均密度: {2.21/total_volume:.2f} kg/m³")