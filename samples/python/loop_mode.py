"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2020-2021 Intel Corporation. All Rights Reserved.

Sample demonstrating continuous detection in loop mode with FPS measurement.
Supports authentication, pose detection, body part detection, person detection, and barcode decoding.
Auto-discovers the device and runs the selected flow for a configurable duration.

Usage:
  python loop_mode.py                      # interactive mode selection
  python loop_mode.py --mode auth          # authentication loop
  python loop_mode.py --mode poses         # pose detection loop
  python loop_mode.py --mode bodyparts     # body part detection loop
  python loop_mode.py --mode barcodes      # barcode decoding loop
  python loop_mode.py --mode persons       # person detection loop
  python loop_mode.py --mode poses -d 60   # run for 60 seconds
"""

import time
import argparse
import threading
import rsid_py

DURATION_SECONDS = 30

MODES = {
    "auth": "Authentication",
    "poses": "Pose Detection",
    "bodyparts": "Body Part Detection",
    "barcodes": "Barcode Decoding",
    "persons": "Person Detection",
}


class FpsTracker:
    def __init__(self, duration):
        self._start_time = None
        self._frame_count = 0
        self._duration = duration

    def tick(self):
        if self._start_time is None:
            self._start_time = time.time()
        self._frame_count += 1

    @property
    def elapsed(self):
        if self._start_time is None:
            return 0.0
        return time.time() - self._start_time

    @property
    def fps(self):
        e = self.elapsed
        return self._frame_count / e if e > 0 else 0.0

    @property
    def frame_count(self):
        return self._frame_count

    @property
    def expired(self):
        return self.elapsed >= self._duration

    def stats_str(self):
        if self._frame_count > 1:
            return f" [frame={self._frame_count}, fps={self.fps:.2f}]"
        return ""

    def summary(self):
        return f"Total frames: {self._frame_count}, duration: {self.elapsed:.1f}s, avg fps: {self.fps:.2f}"


tracker = FpsTracker(DURATION_SECONDS)


def on_auth_result(result, user_id):
    tracker.tick()
    if result == rsid_py.AuthenticateStatus.Success:
        print(f"Authenticated user: {user_id}{tracker.stats_str()}")
    else:
        print(f"Auth status: {result}{tracker.stats_str()}")


def on_poses(poses, timestamp, status):
    tracker.tick()
    print(f"Detected {len(poses)} pose(s) (ts={timestamp}, status={status}){tracker.stats_str()}")
    for i, p in enumerate(poses):
        print(f"  [{i}] x={p.x} y={p.y} {p.w}x{p.h}")
    if tracker.expired:
        return False
    return True


def on_body_parts(body_parts, timestamp, status):
    tracker.tick()
    print(f"Detected {len(body_parts)} body part(s) (ts={timestamp}, status={status}){tracker.stats_str()}")
    for i, bp in enumerate(body_parts):
        print(f"  [{i}] {bp.body_part.name}: x={bp.x} y={bp.y} {bp.w}x{bp.h} score={bp.score:.2f}")
    if tracker.expired:
        return False
    return True


def on_persons(persons, timestamp, status):
    tracker.tick()
    print(f"Detected {len(persons)} person(s) (ts={timestamp}, status={status}){tracker.stats_str()}")
    for i, p in enumerate(persons):
        print(f"  [{i}] x={p.x} y={p.y} {p.w}x{p.h}")
    if tracker.expired:
        return False
    return True


def on_barcodes(barcodes, timestamp, status):
    tracker.tick()
    print(f"Decoded {len(barcodes)} barcode(s) (ts={timestamp}, status={status}){tracker.stats_str()}")
    for i, b in enumerate(barcodes):
        print(f"  [{i}] {b}")
    if tracker.expired:
        return False
    return True


def run_auth_loop(f, duration):
    timer = threading.Timer(duration, lambda: f.cancel())
    timer.start()
    try:
        f.authenticate_loop(on_result=on_auth_result)
    finally:
        timer.cancel()


def run_detection_loop(f, mode):
    if mode == "poses":
        f.detect_poses(callback=on_poses, loop=True)
    elif mode == "bodyparts":
        f.detect_body_parts(callback=on_body_parts, loop=True)
    elif mode == "barcodes":
        f.decode_barcodes(callback=on_barcodes, loop=True)
    elif mode == "persons":
        f.detect_persons(callback=on_persons, loop=True)


def parse_args():
    parser = argparse.ArgumentParser(description="Continuous detection loop with FPS measurement.")
    parser.add_argument("-m", "--mode", choices=MODES.keys(), default=list(MODES.keys())[0],
                        help="Detection mode to run")
    parser.add_argument("-d", "--duration", type=int, default=DURATION_SECONDS,
                        help=f"Duration in seconds (default: {DURATION_SECONDS})")
    return parser.parse_args()


def select_mode():
    print("Select detection mode:")
    for i, (key, name) in enumerate(MODES.items(), 1):
        print(f"  {i}. {name} ({key})")
    choice = input("Enter number or name: ").strip()
    keys = list(MODES.keys())
    if choice.isdigit() and 1 <= int(choice) <= len(keys):
        return keys[int(choice) - 1]
    if choice in MODES:
        return choice
    print(f"ERROR: Invalid selection '{choice}'.")
    raise SystemExit(1)


if __name__ == '__main__':
    args = parse_args()
    mode = args.mode if args.mode else select_mode()
    duration = args.duration

    tracker = FpsTracker(duration)

    devices = rsid_py.discover_devices()
    if not devices:
        print("ERROR: No RealSenseID device detected.")
        exit(1)
    if len(devices) > 1:
        print("ERROR: Multiple devices detected. Please connect only one.")
        exit(1)
    device = devices[0]
    print(f"Using device on port {device.serial_port}")
    print(f"Running {MODES[mode]} loop for {duration} seconds...")

    with rsid_py.FaceAuthenticator(device.device_type, device.serial_port) as f:
        if mode == "auth":
            run_auth_loop(f, duration)
        else:
            run_detection_loop(f, mode)

    print(tracker.summary())
