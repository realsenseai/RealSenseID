"""
License: Apache 2.0. See LICENSE file in root directory.

Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
"""

"""
Example of extracting from image its faceprints for enrollment purposes 
1. Set device config algo to all and high confidence and security levels
2. Sends resized image to device (120 width or height)
3. Prints the extracted features response from the device 
"""

import sys
import cv2
import rsid_py


def resize_to_120(cv_image):
    height, width, channels = cv_image.shape
    if width <= 120 and height <= 120:
        return cv_image
    if width > height:
        new_width = 120
        scale_h = new_width / width
        new_height = int(height * scale_h)
    else:
        new_height = 120
        scale_w = new_height / height
        new_width = int(width * scale_w)

    print(f"Resize to {new_width}x{new_height}")
    return cv2.resize(cv_image, (new_width, new_height), interpolation=cv2.INTER_CUBIC)


def set_device_config(authenticator):
    """
    Set device config algo to all and high confidence and security levels
    After setting it, query the config and print it
    """
    config = rsid_py.DeviceConfig()
    config.algo_flow = rsid_py.AlgoFlow.All
    config.security_level = rsid_py.SecurityLevel.High
    config.matcher_confidence_level = rsid_py.MatcherConfidenceLevel.High
    authenticator.set_device_config(config)
    config = authenticator.query_device_config()
    print('=' * 40)
    print("Device config is:")
    print(config)
    print('=' * 40)


def extract_image_faceprints_for_enroll(filename, port):
    im_cv = cv2.imread(filename)
    im_cv = resize_to_120(im_cv)
    height, width, channels = im_cv.shape
    with rsid_py.FaceAuthenticator(port) as authenticator:
        set_device_config(authenticator)
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
