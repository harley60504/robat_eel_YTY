import math

# =============================
# Servo Config
# =============================
SERVO_COUNT = 6
MIN_DEG = 0
MAX_DEG = 240

# =============================
# Wave Params（全部放這）
# =============================
BASE = 120
AMP = 20
FREQ = 0.6
PHASE_STEP = 0.7

def clamp(x, lo, hi):
    return max(lo, min(hi, x))

def generate_angles(t):
    angles = []
    for i in range(SERVO_COUNT):
        a = BASE + AMP * math.sin(2 * math.pi * FREQ * t + i * PHASE_STEP)
        angles.append(clamp(round(a, 1), MIN_DEG, MAX_DEG))
    return angles
