from __future__ import annotations

import argparse
import time

import mujoco
import mujoco.viewer
import numpy as np

from hopf_cpg import HopfCPG, HopfCPGParams, wrap_pi


def parse_float_list(value: str, expected_len: int, name: str) -> tuple[float, ...]:
    parts = [part.strip() for part in value.split(",") if part.strip()]
    if len(parts) != expected_len:
        raise argparse.ArgumentTypeError(f"{name} needs {expected_len} comma-separated values")
    return tuple(float(part) for part in parts)


def parse_waypoints(value: str) -> np.ndarray:
    points = []
    for item in value.split(";"):
        item = item.strip()
        if not item:
            continue
        xy = [part.strip() for part in item.split(",")]
        if len(xy) != 2:
            raise argparse.ArgumentTypeError("waypoints must look like x,y;x,y;x,y")
        points.append((float(xy[0]), float(xy[1])))
    if len(points) < 2:
        raise argparse.ArgumentTypeError("at least two waypoints are required")
    return np.asarray(points, dtype=np.float64)


def steering_profile(value: float) -> tuple[float, ...]:
    weights = np.array([0.45, 0.55, 0.68, 0.80, 0.92, 1.0], dtype=np.float64)
    return tuple(float(value * weight) for weight in weights)


def parse_args():
    parser = argparse.ArgumentParser(description="View an eel following a 3 m x 1.5 m rectangle course.")
    parser.add_argument("--xml", default="eel_rectangle.xml")
    parser.add_argument("--ajoint", "--amp", dest="ajoint", type=float, default=0.45)
    parser.add_argument("--freq", type=float, default=1.0)
    parser.add_argument("--wavelength", type=float, default=1.6275)
    parser.add_argument(
        "--amp-scales",
        type=lambda value: parse_float_list(value, 6, "amp-scales"),
        default=(1.24, 1.08, 1.0, 1.05, 1.1, 1.2),
    )
    parser.add_argument(
        "--phase-lags",
        type=lambda value: parse_float_list(value, 5, "phase-lags"),
        default=(0.614439, 0.614439, 0.614439, 0.614439, 0.614439),
    )
    parser.add_argument(
        "--waypoints",
        type=parse_waypoints,
        default=parse_waypoints("0.825,-0.40;0.825,0.40;-0.825,0.40;-0.825,-0.40"),
        help="Semicolon-separated waypoint list, for example: x,y;x,y;x,y",
    )
    parser.add_argument("--reach-radius", type=float, default=0.24)
    parser.add_argument("--steer-gain", type=float, default=0.55)
    parser.add_argument("--max-bias", type=float, default=0.34)
    parser.add_argument(
        "--steer-smoothing",
        type=float,
        default=0.08,
        help="Low-pass factor for steering. 1.0 disables smoothing; smaller is smoother.",
    )
    parser.add_argument("--print-hz", type=float, default=2.0)
    parser.add_argument("--reset-x", type=float, default=1.725)
    parser.add_argument("--reset-y", type=float, default=0.90)
    parser.add_argument("--print-contacts", action="store_true")
    return parser.parse_args()


def main():
    args = parse_args()
    model = mujoco.MjModel.from_xml_path(args.xml)
    data = mujoco.MjData(model)
    model.opt.gravity[:] = (0, 0, 0)
    base_body_id = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_BODY, "base_link")

    cpg = HopfCPG(num_joints=6)
    wall_geom_ids = {
        mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_GEOM, name)
        for name in ("wall_bottom", "wall_top", "wall_left", "wall_right")
    }
    waypoint_index = 0
    laps = 0
    last_print = 0.0
    print_period = 1.0 / max(args.print_hz, 1e-6)
    wall_contact_count = 0
    wall_contact_examples: set[str] = set()
    steer_state = 0.0

    def reset_to_start():
        nonlocal waypoint_index, laps, steer_state
        mujoco.mj_resetData(model, data)
        cpg.reset()
        waypoint_index = 0
        laps = 0
        steer_state = 0.0
        mujoco.mj_forward(model, data)

    with mujoco.viewer.launch_passive(model, data) as viewer:
        with viewer.lock():
            viewer.cam.lookat[:] = np.array([0.0, 0.0, -0.02])
            viewer.cam.distance = 2.2
            viewer.cam.elevation = -80
            viewer.cam.azimuth = 0

        while viewer.is_running():
            base_pos = data.xpos[base_body_id].copy()
            waypoint = args.waypoints[waypoint_index]
            delta = waypoint - base_pos[:2]
            distance = float(np.linalg.norm(delta))

            if distance < args.reach_radius:
                waypoint_index = (waypoint_index + 1) % len(args.waypoints)
                if waypoint_index == 0:
                    laps += 1
                waypoint = args.waypoints[waypoint_index]
                delta = waypoint - base_pos[:2]
                distance = float(np.linalg.norm(delta))

            desired_yaw = float(np.arctan2(delta[1], delta[0]))
            yaw = float(data.qpos[2])
            heading_error = float(wrap_pi(desired_yaw - yaw))
            target_steer = float(np.clip(-args.steer_gain * heading_error, -args.max_bias, args.max_bias))
            alpha = float(np.clip(args.steer_smoothing, 0.0, 1.0))
            steer_state += alpha * (target_steer - steer_state)
            steer = steer_state
            joint_bias = steering_profile(steer)

            cpg_params = HopfCPGParams(
                frequency=args.freq,
                wavelength=args.wavelength,
                ajoint=args.ajoint,
                amp_scales=args.amp_scales,
                phase_lags=args.phase_lags,
                joint_bias=joint_bias,
            )
            targets = cpg.step(data.time, model.opt.timestep, cpg_params)
            data.ctrl[0:6] = np.clip(targets, -1.2, 1.2)
            mujoco.mj_step(model, data)

            if args.print_contacts:
                for i in range(data.ncon):
                    contact = data.contact[i]
                    if contact.geom1 in wall_geom_ids or contact.geom2 in wall_geom_ids:
                        g1 = mujoco.mj_id2name(model, mujoco.mjtObj.mjOBJ_GEOM, contact.geom1)
                        g2 = mujoco.mj_id2name(model, mujoco.mjtObj.mjOBJ_GEOM, contact.geom2)
                        wall_contact_count += 1
                        wall_contact_examples.add(f"{g1}<->{g2}")

            base_pos = data.xpos[base_body_id]
            if abs(base_pos[0]) > args.reset_x or abs(base_pos[1]) > args.reset_y:
                print(f"reset: out of course x={base_pos[0]:.3f}, y={base_pos[1]:.3f}", flush=True)
                reset_to_start()
                base_pos = data.xpos[base_body_id]

            now = time.time()
            if now - last_print >= print_period:
                contact_summary = ""
                if args.print_contacts:
                    examples = sorted(wall_contact_examples)[:3]
                    contact_summary = f" | wall contact events={wall_contact_count} {examples}"
                print(
                    f"t={data.time:6.2f}s | lap={laps} wp={waypoint_index + 1}/{len(args.waypoints)} "
                    f"dist={distance:5.2f} steer={steer:6.3f} | "
                    f"x={base_pos[0]:7.3f} y={base_pos[1]:7.3f} yaw={data.qpos[2]:7.3f}"
                    + contact_summary,
                    flush=True,
                )
                wall_contact_count = 0
                wall_contact_examples.clear()
                last_print = now

            with viewer.lock():
                viewer.cam.lookat[0] = base_pos[0]
                viewer.cam.lookat[1] = base_pos[1]
            viewer.sync()
            time.sleep(model.opt.timestep)


if __name__ == "__main__":
    main()

