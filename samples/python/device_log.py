"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2020-2024 RealSense, Inc. All Rights Reserved.
"""

import rsid_py

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

    with rsid_py.DeviceController(device.serial_port) as d:
        log = d.fetch_log()
    with open("device.log", 'w') as f:
        f.write(log)
    print(f'Saved {len(log)} bytes to device.log')
