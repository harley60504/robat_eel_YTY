import numpy as np
from eel_env import EelEnv

env = EelEnv("eel.xml")
env.reset()

t = 0
dt = 0.002

print("Launching viewer...")

freq = 0

amp = 0.5  

while True:
    ctrl = np.zeros(6)

    for i in range(6):
        phase = i * 0.7
        ctrl[i] = amp * np.sin(2*np.pi*freq*t + phase)

    env.step(ctrl)
    env.render()

    t += dt
