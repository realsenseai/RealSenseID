"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
"""
import rsid_py


def on_result(result):
    print('on_result', result)    

def on_progress(p):    
    print(f'on_progress {p}')

def on_hint(h, frame_score):
    print(f'on_hint {h} (frame_score={frame_score:.2f})')

def on_faces(faces, timestamp):    
    print(f'detected {len(faces)} face(s)')
    for f in faces:
        print(f'\tface {f.x},{f.y} {f.w}x{f.h}')    


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
        user_id = input("User id to enroll: ")
        f.enroll(user_id=user_id, on_hint=on_hint, on_progress=on_progress, on_faces=on_faces, on_result=on_result)

        #display list of enrolled users
        users = f.query_user_ids()
        print('Users: ', users)
    
    