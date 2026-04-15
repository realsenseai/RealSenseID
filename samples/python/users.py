"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
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

    with rsid_py.FaceAuthenticator(device.serial_port) as f:
        #display list of enrolled users
        users = f.query_user_ids()
        print('Users: ', users)

        #delete all users
        if input("Delete all users [y/n]?") == 'y':
            f.remove_all_users()
        
    
    