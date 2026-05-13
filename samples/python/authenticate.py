"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

Sample demonstrating face authentication.
Auto-discovers the device and runs a single authentication.
"""

import rsid_py

FACE_LANDMARK_NAMES = ["left_eye", "right_eye",
                       "nose", "left_mouth", "right_mouth"]


def on_result(result, user_id):
    if result == rsid_py.AuthenticateStatus.Success:
        print(f'\tAuthenticated user: {user_id}')
    else:
        print(f'\tAuthentication failed with status: {result}')


def on_hint(hint, score):
    print(f'\tReceived hint {hint} with score {score:.2f}')


def on_landmarks(landmarks, _timestamp):
    for i, lm in enumerate(landmarks):
        parts = [f'{FACE_LANDMARK_NAMES[j]}=({lm.lm_x[j]}, {lm.lm_y[j]})' for j in range(
            len(FACE_LANDMARK_NAMES))]
        print(f'\tFace {i}: {" ".join(parts)}')


def on_face_distances(distances, _timestamp):
    for i, d in enumerate(distances):
        print(f'\tFace {i} distance: {d:.2f} cm')


def on_faces(faces, _timestamp):
    for i, f in enumerate(faces):
        print(f'\tFace {i} detected at ({f.x}, {f.y}) with size {f.w}x{f.h}')


if __name__ == '__main__':
    devices = rsid_py.discover_devices()
    if not devices:
        print("Error: No RealSenseID device detected.")
        exit(1)
    if len(devices) > 1:
        print("Error: Multiple devices detected. Please connect only one.")
        exit(1)
    device = devices[0]
    print(f"\tUsing device on port {device.serial_port}")

    with rsid_py.FaceAuthenticator(device.device_type, device.serial_port) as f:
        f.authenticate(
            on_faces=on_faces,
            on_hint=on_hint,
            on_landmarks=on_landmarks,
            on_face_distances=on_face_distances,
            on_result=on_result,
        )
