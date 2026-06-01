from __future__ import annotations

from dataclasses import dataclass

import numpy as np


def wrap_pi(x):
    return (x + np.pi) % (2.0 * np.pi) - np.pi


@dataclass
class HopfCPGParams:
    frequency: float = 1.0
    wavelength: float = 1.5
    body_length: float = 1.0
    ajoint: float = 0.45
    alpha: float = 12.0
    mu: float = 1.0
    k_couple: float = 1.0
    k_anchor: float = 0.3
    k_fb_phase: float = 0.8
    k_fb_amp: float = 0.25
    fb_phase: float = 0.0
    fb_amp: float = 0.0
    amp_scales: tuple[float, ...] | None = None
    phase_lags: tuple[float, ...] | None = None
    joint_bias: tuple[float, ...] | None = None


class HopfCPG:
    def __init__(self, num_joints: int, params: HopfCPGParams | None = None):
        self.num_joints = int(num_joints)
        self.params = params or HopfCPGParams()
        self.r = np.zeros(self.num_joints, dtype=np.float64)
        self.theta = np.zeros(self.num_joints, dtype=np.float64)
        self.reset()

    def reset(self):
        self.r[:] = 0.25
        self.theta[:] = self._phase_offsets(self.params, self.num_joints)

    def step(self, t: float, dt: float, params: HopfCPGParams | None = None) -> np.ndarray:
        if params is not None:
            self.params = params

        p = self.params
        omega = 2.0 * np.pi * p.frequency
        phase_offsets = self._phase_offsets(p, self.num_joints)

        old_r = self.r.copy()
        old_theta = self.theta.copy()
        dr = p.alpha * (p.mu - old_r * old_r) * old_r
        dtheta = np.full(self.num_joints, omega, dtype=np.float64)

        for j in range(self.num_joints):
            if j - 1 >= 0:
                desired_l = phase_offsets[j - 1] - phase_offsets[j]
                err_l = wrap_pi((old_theta[j - 1] - old_theta[j]) - desired_l)
                dtheta[j] += p.k_couple * np.sin(err_l)
            if j + 1 < self.num_joints:
                desired_r = phase_offsets[j + 1] - phase_offsets[j]
                err_r = wrap_pi((old_theta[j + 1] - old_theta[j]) - desired_r)
                dtheta[j] += p.k_couple * np.sin(err_r)

            th_ref = omega * t + phase_offsets[j]
            e_ref = wrap_pi(th_ref - old_theta[j])
            dtheta[j] += p.k_anchor * np.sin(e_ref)

        dtheta += p.k_fb_phase * p.fb_phase
        dr += p.k_fb_amp * p.fb_amp

        self.r = np.maximum(0.0, old_r + dr * dt)
        self.theta = wrap_pi(old_theta + dtheta * dt)
        return self.output()

    def output(self) -> np.ndarray:
        amp_scales = self._amp_scales(self.params, self.num_joints)
        joint_bias = self._joint_bias(self.params, self.num_joints)
        return self.params.ajoint * amp_scales * self.r * np.cos(self.theta) + joint_bias

    @staticmethod
    def _target_delta(params: HopfCPGParams) -> float:
        lambda_input = max(1e-6, params.wavelength * params.body_length)
        return 1.0 / lambda_input

    @classmethod
    def _phase_offsets(cls, params: HopfCPGParams, num_joints: int) -> np.ndarray:
        if params.phase_lags is None:
            target_delta = cls._target_delta(params)
            return -np.arange(num_joints, dtype=np.float64) * target_delta

        lags = np.asarray(params.phase_lags, dtype=np.float64)
        if lags.size != num_joints - 1:
            raise ValueError(f"phase_lags must have {num_joints - 1} values, got {lags.size}")

        offsets = np.zeros(num_joints, dtype=np.float64)
        offsets[1:] = -np.cumsum(lags)
        return offsets

    @staticmethod
    def _amp_scales(params: HopfCPGParams, num_joints: int) -> np.ndarray:
        if params.amp_scales is None:
            return np.ones(num_joints, dtype=np.float64)

        amp_scales = np.asarray(params.amp_scales, dtype=np.float64)
        if amp_scales.size != num_joints:
            raise ValueError(f"amp_scales must have {num_joints} values, got {amp_scales.size}")
        return amp_scales

    @staticmethod
    def _joint_bias(params: HopfCPGParams, num_joints: int) -> np.ndarray:
        if params.joint_bias is None:
            return np.zeros(num_joints, dtype=np.float64)

        joint_bias = np.asarray(params.joint_bias, dtype=np.float64)
        if joint_bias.size != num_joints:
            raise ValueError(f"joint_bias must have {num_joints} values, got {joint_bias.size}")
        return joint_bias
