"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2026 RealSense, Inc. All Rights Reserved.

Enroll and authenticate one-to-one using images.

Usage:
  python enroll_image_one_to_one.py <user-id> <enroll-image> <auth-image>
"""
import sys
import cv2
import rsid_py


if __name__ == '__main__':
    if len(sys.argv) != 4:
        print("Usage: python enroll_image_one_to_one.py <user-id> <enroll-image> <auth-image>")
        sys.exit(1)

    user_id, enroll_file, auth_file = sys.argv[1], sys.argv[2], sys.argv[3]

    devices = rsid_py.discover_devices()
    if not devices:
        print("Error: No RealSenseID device detected.")
        sys.exit(1)
    if len(devices) > 1:
        print("Error: Multiple devices detected. Please connect only one.")
        sys.exit(1)
    device = devices[0]
    print(f"Using device on port {device.serial_port}")

    # Enroll
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

        # Authenticate
        auth_im = cv2.imread(auth_file)
        if auth_im is None:
            print(f"Error: Cannot read '{auth_file}'")
            sys.exit(1)
        ah, aw, _ = auth_im.shape
        print(f"Authenticating with {aw}x{ah} image...")

        auth_status, matched_id, score = f.authenticate_image_one_to_one(auth_im.flatten(), aw, ah)
        print(f"Auth result: {auth_status}, user={matched_id}, score={score}")
