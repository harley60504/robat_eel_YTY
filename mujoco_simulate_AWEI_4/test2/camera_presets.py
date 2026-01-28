# camera_presets.py
import numpy as np


class FollowCamPreset:
"""
專門管理 MuJoCo viewer 的鏡頭行為：
- start_follow_close(): 立刻 Follow ON + 貼近
- update_follow(): 每個 step 呼叫，讓 lookat 跟著 base_link
- toggle_follow(): 手動切換 Follow
- restore_origin(): 回到一開始鏡頭
"""


def __init__(self, viewer, env, base_body_id,
focus_dist=1.9, focus_elev=-75.0, focus_azim=0.0):
self.viewer = viewer
self.env = env
self.base_body_id = int(base_body_id)


self.focus_dist = float(focus_dist)
self.focus_elev = float(focus_elev)
self.focus_azim = float(focus_azim)


self.follow_on = False


# 記住原始鏡頭
self.origin = {
"lookat": np.array(viewer.cam.lookat, dtype=float),
"distance": float(viewer.cam.distance),
"elevation": float(viewer.cam.elevation),
"azimuth": float(viewer.cam.azimuth),
}


def restore_origin(self):
with self.viewer.lock():
self.viewer.cam.lookat[:] = self.origin["lookat"]
self.viewer.cam.distance = self.origin["distance"]
self.viewer.cam.elevation = self.origin["elevation"]
self.viewer.cam.azimuth = self.origin["azimuth"]


def toggle_follow(self):
self.follow_on = not self.follow_on
print("[Camera] Follow mode:", "ON" if self.follow_on else "OFF")


def start_follow_close(self):
"""Sweep 一開始：自動 Follow + 立刻貼近"""
self.follow_on = True
pos = self.env.data.xpos[self.base_body_id].copy()
with self.viewer.lock():
self.viewer.cam.lookat[:] = pos
self.viewer.cam.distance = self.focus_dist
self.viewer.cam.elevation = self.focus_elev
self.viewer.cam.azimuth = self.focus_azim
print("[Camera] Sweep start -> Follow ON + Close view")


def update_follow(self):
"""每個 step 後呼叫：跟著 eel"""
if not self.follow_on:
return
pos = self.env.data.xpos[self.base_body_id]
with self.viewer.lock():
self.viewer.cam.lookat[:] = pos
self.viewer.cam.distance = self.focus_dist
self.viewer.cam.elevation = self.focus_elev
self.viewer.cam.azimuth = self.focus_azim