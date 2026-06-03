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
# Current MuJoCo / Arduino gait defaults
# =============================
ANGLE_MODE = "CPG"

AJOINT_DEG = 25.7831
FREQUENCY_HZ = 1.0
LAMBDA = 1.6275
BODY_LENGTH = 1.0
AMP_SCALES = [1.24, 1.08, 1.0, 1.05, 1.1, 1.2]
PHASE_LAGS = [0.614439, 0.614439, 0.614439, 0.614439, 0.614439]
JOINT_BIAS_DEG = [0.0, 0.0, 0.0, 0.0, 0.0, 0.0]

# =============================
# SIN Params
# =============================
SIN_BASE = 0.0
SIN_AMP = AJOINT_DEG
SIN_FREQ = FREQUENCY_HZ

# =============================
# CPG Params
# =============================
L = BODY_LENGTH
lambda_ = LAMBDA
frequency = FREQUENCY_HZ
Ajoint = AJOINT_DEG

# =============================
# On-board CPG Params
# Python sends these when output_mode == "cpg".
# Per-joint amp/phase/bias live in the control-board firmware defaults.
# =============================
ONBOARD_AJOINT = AJOINT_DEG
ONBOARD_FREQ = FREQUENCY_HZ
ONBOARD_LAMBDA = LAMBDA
ONBOARD_L = BODY_LENGTH
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


def clamp(x, lo, hi):
    return max(lo, min(hi, x))


def phase_offset(j):
    return -sum(PHASE_LAGS[:j])


def target_angle(j, theta):
    out_deg = (
        Ajoint
        * AMP_SCALES[j]
        * math.cos(theta + phase_offset(j))
        + JOINT_BIAS_DEG[j]
    )
    return clamp(round(servoDefaultAngles[j] + out_deg, 1), MIN_DEG, MAX_DEG)


def init_generator():
    # Kept for controller.py compatibility.
    pass


def generate_angles_sin(t):
    theta = 2.0 * math.pi * SIN_FREQ * t
    angles = []
    for j in range(SERVO_COUNT):
        out_deg = (
            SIN_BASE
            + SIN_AMP
            * AMP_SCALES[j]
            * math.sin(theta + phase_offset(j))
            + JOINT_BIAS_DEG[j]
        )
        angles.append(clamp(round(servoDefaultAngles[j] + out_deg, 1), MIN_DEG, MAX_DEG))
    return angles


def generate_angles_cpg(t, dt):
    theta = 2.0 * math.pi * frequency * t
    return [target_angle(j, theta) for j in range(SERVO_COUNT)]


def generate_angles(t, dt):
    mode = ANGLE_MODE.upper()

    if mode == "SIN":
        return generate_angles_sin(t)

    if mode == "CPG":
        return generate_angles_cpg(t, dt)

    raise ValueError(f"Unknown ANGLE_MODE: {ANGLE_MODE}")


def generate_cpg_params(t, dt):
    """Return Flutter-compatible set_param fields for on-board CPG mode."""
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
