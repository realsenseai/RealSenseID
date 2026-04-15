"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
"""

import rsid_py


def on_result(result, user_id):
    print("Received result:", result)
    if result == rsid_py.AuthenticateStatus.Success and user_id != "":
        print('Authenticated user:', user_id)


def on_faces(faces, _timestamp):
    print(f'detected {len(faces)} face(s)')
    for f in faces:
        print(f'\tface {f.x},{f.y} {f.w}x{f.h}')    


def on_person_detect(persons, _timestamp):
    print(f'detected {len(persons)} person(s)')
    for p in persons:
        print(f"\tbody_part {p.body_part} [{p.x},{p.y} {p.w}x{p.h}]")


def on_person_pose(poses, _timestamp):
    print(f'detected {len(poses)} poses(s)')
    for p in poses:
        print(f"  pose: (x, y, w, h) = ({p.x}, {p.y}, {p.w}, {p.h})")
        print(f"  landmarks:")
        for idx, landmark in enumerate(p.landmarks):
            landmark_names = [
                "nose",
                "left_eye",
                "right_eye",
                "left_ear",
                "right_ear",
                "left_shoulder",
                "right_shoulder",
                "left_elbow",
                "right_elbow",
                "left_wrist",
                "right_wrist",
                "left_hip",
                "right_hip",
                "left_knee",
                "right_knee",
                "left_ankle",
                "right_ankle",
            ]
            landmark_name = (
                landmark_names[idx] if idx < len(landmark_names) else f"landmark_{idx}"
            )
            print(
                f"    {landmark_name.ljust(14)}: (x, y)=({landmark[0]:4d}, {landmark[1]:4d}), score={landmark[2]:.4f}"
            )


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

    with rsid_py.FaceAuthenticator(device.serial_port) as f:
        f.authenticate(on_faces=on_faces, on_person_detect=on_person_detect, on_person_pose=on_person_pose, on_result=on_result)
