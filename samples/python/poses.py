"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

Sample demonstrating pose detection using the detect_poses API.
Auto-discovers the device and runs detection for 10 seconds.
"""

import time
import rsid_py

DURATION_SECONDS = 10

LANDMARK_NAMES = [
    "nose", "left_eye", "right_eye", "left_ear", "right_ear",
    "left_shoulder", "right_shoulder", "left_elbow", "right_elbow",
    "left_wrist", "right_wrist", "left_hip", "right_hip",
    "left_knee", "right_knee", "left_ankle", "right_ankle",
]

_start_time = None

def on_poses(poses, timestamp, status):
    global _start_time
    if _start_time is None:
        _start_time = time.time()
    print(f"detected {len(poses)} pose(s) (ts={timestamp}, status={status})")
    for i, p in enumerate(poses):
        print(f"  [{i}] x={p.x} y={p.y} {p.w}x{p.h}")
        for idx, lm in enumerate(p.landmarks):
            name = LANDMARK_NAMES[idx] if idx < len(LANDMARK_NAMES) else f"landmark_{idx}"
            print(f"    {name.ljust(14)}: (x, y)=({lm[0]:4d}, {lm[1]:4d}), score={lm[2]:.4f}")
    if time.time() - _start_time >= DURATION_SECONDS:
        print(f"Stopping after {DURATION_SECONDS} seconds.")
        return False
    return True


if __name__ == '__main__':
    devices = rsid_py.discover_devices()
    if not devices:
        print("Error: No RealSenseID device detected.")
        exit(1)
    if len(devices) > 1:
        print("Error: Multiple devices detected. Please connect only one.")
        exit(1)
    device = devices[0]
    print(f"Using device on port {device.serial_port}")
    with rsid_py.FaceAuthenticator(device.device_type, device.serial_port) as f:
        f.detect_poses(callback=on_poses, loop=True)
