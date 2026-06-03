import math
from dataclasses import dataclass, field

# =============================
# Servo Config
# =============================
SERVO_COUNT = 6
MIN_DEG = 0
MAX_DEG = 240
servoDefaultAngles = [120] * SERVO_COUNT

# =============================
# Mode Select
# 改這裡就好："SIN" 或 "CPG"
# =============================
ANGLE_MODE = "CPG"

# =============================
# SIN Params
# =============================
SIN_BASE = 0.0
SIN_AMP = 20.0
SIN_FREQ = 0.6
SIN_PHASE_STEP = 0.7

# =============================
# CPG Params
# =============================
L = 0.65
lambda_ = 0.6
frequency = 0.5
Ajoint = 30.0

# =============================
# On-board CPG Params
# 給 controller.py 的 cpg output mode 使用：
# Python / PPO 只送參數，控制板自己跑 CPG。
# =============================
ONBOARD_AJOINT = 30.0
ONBOARD_FREQ = 0.5
ONBOARD_LAMBDA = 0.6
ONBOARD_L = 0.65
ONBOARD_FEEDBACK_GAIN = 1.0


@dataclass
class OnboardCPGCommand:
    Ajoint: float = ONBOARD_AJOINT
    frequency: float = ONBOARD_FREQ
    lambda_: float = ONBOARD_LAMBDA
    L: float = ONBOARD_L
    paused: bool = False
    feedback: float = ONBOARD_FEEDBACK_GAIN
    extra: dict = field(default_factory=dict)

# =============================
# Hopf Oscillator
# =============================
class HopfOscillator:
    def __init__(self):
        self.r = 0.25
        self.theta = 0.0
        self.alpha = 12.0
        self.mu = 1.0

cpg = [HopfOscillator() for _ in range(SERVO_COUNT)]

# =============================
# Utils
# =============================
def clamp(x, lo, hi):
    return max(lo, min(hi, x))

def wrap_pi(x):
    while x > math.pi:
        x -= 2 * math.pi
    while x < -math.pi:
        x += 2 * math.pi
    return x

# =============================
# CPG Init
# =============================
def init_cpg():
    for j in range(SERVO_COUNT):
        cpg[j].r = 0.25
        cpg[j].theta = j / (lambda_ * L)
        cpg[j].alpha = 12.0
        cpg[j].mu = 1.0

# =============================
# SIN Generator
# =============================
def generate_angles_sin(t):
    angles = []
    for i in range(SERVO_COUNT):
        out_deg = SIN_BASE + SIN_AMP * math.sin(2 * math.pi * SIN_FREQ * t + i * SIN_PHASE_STEP)
        target_deg = servoDefaultAngles[i] + out_deg
        angles.append(clamp(round(target_deg, 1), MIN_DEG, MAX_DEG))
    return angles

# =============================
# CPG Core
# =============================
def get_cpg_output(j):
    return Ajoint * cpg[j].r * math.cos(cpg[j].theta)

def get_lambda_input():
    return lambda_ * L

def get_target_delta():
    return 1.0 / get_lambda_input()

def update_cpg(t, dt, j, fb_phase=0.0, fb_amp=0.0):
    o = cpg[j]

    omega = 2.0 * math.pi * frequency
    dr = o.alpha * (o.mu - o.r * o.r) * o.r
    dtheta = omega

    K_couple = 1.0
    K_anchor = 0.3
    k_fb_phase = 0.8
    k_fb_amp = 0.25
    target_delta = get_target_delta()

    if j - 1 >= 0:
        errL = wrap_pi((cpg[j - 1].theta - o.theta) - (-target_delta))
        dtheta += K_couple * math.sin(errL)

    if j + 1 < SERVO_COUNT:
        errR = wrap_pi((cpg[j + 1].theta - o.theta) - (+target_delta))
        dtheta += K_couple * math.sin(errR)

    th_ref = omega * t + j / get_lambda_input()
    e_ref = wrap_pi(th_ref - o.theta)
    dtheta += K_anchor * math.sin(e_ref)

    dtheta += k_fb_phase * fb_phase
    dr += k_fb_amp * fb_amp

    o.r += dr * dt
    o.theta = wrap_pi(o.theta + dtheta * dt)

def generate_angles_cpg(t, dt, fb_phase=0.0, fb_amp=0.0):
    angles = []
    for j in range(SERVO_COUNT):
        update_cpg(t, dt, j, fb_phase, fb_amp)
        out_deg = get_cpg_output(j)
        target_deg = servoDefaultAngles[j] + out_deg
        angles.append(clamp(round(target_deg, 1), MIN_DEG, MAX_DEG))
    return angles

# =============================
# Unified API
# 外部只呼叫這個
# =============================
def init_generator():
    if ANGLE_MODE.upper() == "CPG":
        init_cpg()

def generate_angles(t, dt):
    mode = ANGLE_MODE.upper()

    if mode == "SIN":
        return generate_angles_sin(t)

    if mode == "CPG":
        return generate_angles_cpg(t, dt)

    raise ValueError(f"Unknown ANGLE_MODE: {ANGLE_MODE}")


def generate_cpg_params(t, dt):
    """Return Flutter-compatible set_param fields for on-board CPG mode.

    This is the hook for PPO/path-following code. Keep the returned keys the
    same as Flutter's WsControlApi.setParam payload so the ESP interface stays
    unchanged.
    """
    cmd = OnboardCPGCommand()
    payload = {
        "Ajoint": round(cmd.Ajoint, 4),
        "frequency": round(cmd.frequency, 4),
        "lambda": round(cmd.lambda_, 4),
        "L": round(cmd.L, 4),
        "paused": cmd.paused,
        "feedback": round(cmd.feedback, 4),
    }
    payload.update(cmd.extra)
    return payload
