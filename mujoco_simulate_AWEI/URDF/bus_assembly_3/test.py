import mujoco

urdf_path = r"C:\Users\AWEI\Documents\GitHub\robot_eel_YTY\mujoco_simulate_AWEI\URDF\bus_assembly_3\urdf\bus_assembly_3.urdf"
model = mujoco.MjModel.from_xml_path(urdf_path)

out_xml = r"C:\Users\AWEI\Documents\GitHub\robot_eel_YTY\mujoco_simulate_AWEI\URDF\bus_assembly_3\bus_assembly_3.xml"
mujoco.mj_saveLastXML(out_xml, model)

print("Saved:", out_xml)
