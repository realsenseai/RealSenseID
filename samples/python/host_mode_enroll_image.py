"""
License: Apache 2.0. See LICENSE file in root directory.

Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
"""

"""
Example of extracting faceprints from an image for enrollment purposes.

Note: the API requires exactly one face in the image, and the face width and
height must each be at least 144 pixels.
"""

import sys
import cv2
import rsid_py


def extract_image_faceprints_for_enroll(filename, port):
    im_cv = cv2.imread(filename)
    height, width, channels = im_cv.shape
    with rsid_py.FaceAuthenticator(port) as authenticator:
        try:
            features = authenticator.extract_image_faceprints_for_enroll(buffer=im_cv.flatten(), width=width,
                                                                         height=height)
            print(features)
        except Exception as ex:
            print('Failed to extract:', ex)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print(f"Usage: python {sys.argv[0]} <image-filename>")
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

    try:
        extract_image_faceprints_for_enroll(filename=sys.argv[1], port=device.serial_port)
    except Exception as ex:
        print("Error", ex)
