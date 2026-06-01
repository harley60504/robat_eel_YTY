from __future__ import annotations

import argparse
import csv
from pathlib import Path

import mujoco
import numpy as np

from hopf_cpg import HopfCPG, HopfCPGParams, wrap_pi
from view_rectangle_course import parse_float_list, parse_waypoints, steering_profile


def parse_args():
    parser = argparse.ArgumentParser(description="Headless test for the 3 m x 1.5 m rectangle course.")
    parser.add_argument("--xml", default="eel_rectangle.xml")
    parser.add_argument("--seconds", type=float, default=80.0)
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
        default=parse_waypoints("0.55,-0.40;0.55,0.40;-0.55,0.40;-0.55,-0.40"),
    )
    parser.add_argument("--reach-radius", type=float, default=0.22)
    parser.add_argument("--steer-gain", type=float, default=0.50)
    parser.add_argument("--max-bias", type=float, default=0.34)
    parser.add_argument("--steer-smoothing", type=float, default=0.08)
    parser.add_argument("--reset-x", type=float, default=1.15)
    parser.add_argument("--reset-y", type=float, default=0.90)
    parser.add_argument("--csv", type=Path, default=None)
    return parser.parse_args()


def main():
    args = parse_args()
    model = mujoco.MjModel.from_xml_path(args.xml)
    data = mujoco.MjData(model)
    model.opt.gravity[:] = (0, 0, 0)
    base_body_id = mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_BODY, "base_link")
    cpg = HopfCPG(num_joints=6)

    waypoint_index = 0
    waypoint_hits = 0
    laps = 0
    out_of_bounds = False
    wall_contact_steps = 0
    wall_contact_names = set()
    min_distances = np.full(len(args.waypoints), np.inf, dtype=np.float64)
    records = []
    steer_state = 0.0
    wall_geom_ids = {
        mujoco.mj_name2id(model, mujoco.mjtObj.mjOBJ_GEOM, name)
        for name in ("wall_bottom", "wall_top", "wall_left", "wall_right")
    }

    for _ in range(int(round(args.seconds / model.opt.timestep))):
        base_pos = data.xpos[base_body_id].copy()
        waypoint = args.waypoints[waypoint_index]
        delta = waypoint - base_pos[:2]
        distance = float(np.linalg.norm(delta))
        min_distances[waypoint_index] = min(min_distances[waypoint_index], distance)

        if distance < args.reach_radius:
            waypoint_hits += 1
            waypoint_index = (waypoint_index + 1) % len(args.waypoints)
            if waypoint_index == 0:
                laps += 1
            waypoint = args.waypoints[waypoint_index]
            delta = waypoint - base_pos[:2]
            distance = float(np.linalg.norm(delta))

        desired_yaw = float(np.arctan2(delta[1], delta[0]))
        heading_error = float(wrap_pi(desired_yaw - data.qpos[2]))
        target_steer = float(np.clip(-args.steer_gain * heading_error, -args.max_bias, args.max_bias))
        alpha = float(np.clip(args.steer_smoothing, 0.0, 1.0))
        steer_state += alpha * (target_steer - steer_state)
        steer = steer_state
        params = HopfCPGParams(
            frequency=args.freq,
            wavelength=args.wavelength,
            ajoint=args.ajoint,
            amp_scales=args.amp_scales,
            phase_lags=args.phase_lags,
            joint_bias=steering_profile(steer),
        )
        data.ctrl[0:6] = np.clip(cpg.step(data.time, model.opt.timestep, params), -1.2, 1.2)
        mujoco.mj_step(model, data)

        base_pos = data.xpos[base_body_id].copy()
        had_wall_contact = False
        for i in range(data.ncon):
            contact = data.contact[i]
            if contact.geom1 in wall_geom_ids or contact.geom2 in wall_geom_ids:
                had_wall_contact = True
                wall_contact_names.add(
                    (
                        mujoco.mj_id2name(model, mujoco.mjtObj.mjOBJ_GEOM, contact.geom1),
                        mujoco.mj_id2name(model, mujoco.mjtObj.mjOBJ_GEOM, contact.geom2),
                    )
                )
        if had_wall_contact:
            wall_contact_steps += 1

        if abs(base_pos[0]) > args.reset_x or abs(base_pos[1]) > args.reset_y:
            out_of_bounds = True
            break

        if args.csv and int(data.time / model.opt.timestep) % 20 == 0:
            records.append(
                (
                    data.time,
                    base_pos[0],
                    base_pos[1],
                    data.qpos[2],
                    waypoint_index,
                    distance,
                    steer,
                    laps,
                )
            )

    base_pos = data.xpos[base_body_id]
    print("Rectangle course measurement")
    print(f"  seconds={data.time:.2f}, laps={laps}, waypoint_hits={waypoint_hits}, next_wp={waypoint_index + 1}")
    print(f"  out_of_bounds={out_of_bounds}")
    print(f"  final x={base_pos[0]:.3f}, y={base_pos[1]:.3f}, yaw={data.qpos[2]:.3f}")
    print(f"  wall_contact_steps={wall_contact_steps}")
    if wall_contact_names:
        examples = sorted(wall_contact_names)[:8]
        print("  wall contact examples:", examples)
    print("  min distances:", ", ".join(f"{value:.3f}" for value in min_distances))
    print(f"  score={laps * 100 + waypoint_hits - float(out_of_bounds) * 25:.1f}")

    if args.csv:
        args.csv.parent.mkdir(parents=True, exist_ok=True)
        with args.csv.open("w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(["time", "x", "y", "yaw", "waypoint_index", "distance", "steer", "laps"])
            writer.writerows(records)
        print(f"  wrote {args.csv}")


if __name__ == "__main__":
    main()

