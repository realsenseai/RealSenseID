"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

Sample demonstrating body part detection using the detect_body_parts API.
Auto-discovers the device and runs detection for 10 seconds.
"""

import time
import rsid_py

DURATION_SECONDS = 10

_start_time = None

def on_body_parts(body_parts, timestamp, status):
    global _start_time
    if _start_time is None:
        _start_time = time.time()
    print(f"detected {len(body_parts)} body part(s) (ts={timestamp}, status={status})")
    for i, bp in enumerate(body_parts):
        print(f"  [{i}] {bp.body_part.name}: x={bp.x} y={bp.y} {bp.w}x{bp.h} id={bp.id} distance={bp.distance}")
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
        f.detect_body_parts(callback=on_body_parts, loop=True)
