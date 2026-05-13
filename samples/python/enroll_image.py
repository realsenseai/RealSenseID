"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
"""
import sys
import cv2
import rsid_py


def enroll_with_image(user_id, filename, port):
    im_cv = cv2.imread(filename)    
    height, width, channels = im_cv.shape
    with rsid_py.FaceAuthenticator(port) as f:
        result = f.enroll_image(user_id, im_cv.flatten(), width, height)
        print(result)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python enroll_image.py <user-id> <image-filename>")
        sys.exit(1)

    devices = rsid_py.discover_devices()
    if not devices:
        print("Error: No RealSenseID device detected.")
        sys.exit(1)
    if len(devices) > 1:
        print("Error: Multiple devices detected. Please connect only one.")
        sys.exit(1)
    device = devices[0]
    print(f"Using device on port {device.serial_port}")

    enroll_with_image(sys.argv[1], sys.argv[2], device.serial_port)