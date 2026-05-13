"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2026 RealSense, Inc. All Rights Reserved.

Enroll a user from an image, then authenticate one-to-one using the device camera.

Usage:
  python authenticate_one_to_one.py <user-id> <enroll-image>
"""
import sys
import cv2
import rsid_py


def on_result(result, user_id):
    print("Received result:", result)
    if result == rsid_py.AuthenticateStatus.Success and user_id != "":
        print('Authenticated user:', user_id)


def on_hint(hint, _score):
    print("Hint:", hint)


def on_faces(faces, _timestamp):
    print(f'detected {len(faces)} face(s)')
    for f in faces:
        print(f'\tface {f.x},{f.y} {f.w}x{f.h}')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python authenticate_one_to_one.py <user-id> <enroll-image>")
        sys.exit(1)

    user_id, enroll_file = sys.argv[1], sys.argv[2]

    devices = rsid_py.discover_devices()
    if not devices:
        print("Error: No RealSenseID device detected.")
        sys.exit(1)
    if len(devices) > 1:
        print("Error: Multiple devices detected. Please connect only one.")
        sys.exit(1)
    device = devices[0]
    print(f"Using device on port {device.serial_port}")

    enroll_im = cv2.imread(enroll_file)
    if enroll_im is None:
        print(f"Error: Cannot read '{enroll_file}'")
        sys.exit(1)
    eh, ew, _ = enroll_im.shape
    print(f"Enrolling '{user_id}' with {ew}x{eh} image...")

    with rsid_py.FaceAuthenticator(device.serial_port) as f:
        enroll_status = f.enroll_image_one_to_one(user_id, enroll_im.flatten(), ew, eh)
        print(f"Enroll result: {enroll_status}")

        if enroll_status != rsid_py.EnrollStatus.Success:
            print("Enroll failed, skipping authentication.")
            sys.exit(1)

        print("Authenticating with device camera...")
        f.authenticate_one_to_one(on_result=on_result, on_hint=on_hint, on_faces=on_faces)
