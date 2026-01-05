import mujoco

class EelEnv:
    def __init__(self, xml_path="eel.xml"):
        self.model = mujoco.MjModel.from_xml_path(xml_path)
        self.data = mujoco.MjData(self.model)
        # 確保重力關閉，讓鰻魚水平游動
        self.model.opt.gravity[:] = (0, 0, 0)

    def reset(self):
        mujoco.mj_resetData(self.model, self.data)
        return self.data
