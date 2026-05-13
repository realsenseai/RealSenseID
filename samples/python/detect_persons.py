"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

Sample demonstrating person detection using the detect_persons API.
Auto-discovers the device and runs detection for 10 seconds.
"""

import time
import rsid_py

DURATION_SECONDS = 10

_start_time = None

def on_persons(persons, timestamp, status):
    global _start_time
    if _start_time is None:
        _start_time = time.time()
    print(f"detected {len(persons)} person(s) (ts={timestamp}, status={status})")
    for i, p in enumerate(persons):
        print(f"  [{i}] id={p.id}  x={p.x} y={p.y} {p.w}x{p.h}  distance={p.distance}  score={p.score:.3f}")
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
        f.detect_persons(callback=on_persons, loop=True)
