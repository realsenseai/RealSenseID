"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.
"""

"""
Example of host mode where face features are stored in the host instead of the device
"""
import os
import rsid_py

faceprints_db = []

def on_result(r, user_id=None):
    print(f'Success "{user_id}"' if user_id else str(r))    


def on_progress(p):    
    print(f'on_progress {p}')


def on_fp_enroll_result(status, extracted_prints):
    print('on_fp_enroll_result', status, type(extracted_prints))
    if status ==  rsid_py.EnrollStatus.Success:
        db_item = rsid_py.Faceprints()
        db_item.version=extracted_prints.version
        db_item.features_type = extracted_prints.features_type
        db_item.flags = extracted_prints.flags
        db_item.adaptive_descriptor_nomask = extracted_prints.features
        db_item.enroll_descriptor = extracted_prints.features
        faceprints_db.append(db_item)
    

def on_fp_auth_result(status, new_prints, authenticator):
    print("ON FACEPRINTS AUTH RESULT", status, type(new_prints))
    if  status != rsid_py.AuthenticateStatus.Success:
        return    

    #iterate over our db of faceprints and choose the one with highest match score
    max_score = -100    
    selected_user_idx = -1
    for i, db_item in enumerate(faceprints_db):
        existing_prints = faceprints_db[0]                        
        updated_faceprints = rsid_py.Faceprints()                
        match_result = authenticator.match_faceprints(new_prints, db_item, updated_faceprints)               
        print(f'match_result for user {i}: {match_result}')
        if match_result.success:
            print('Authentication success for user', i)
            if(match_result.score > max_score):
                max_score = match_result.score
                selected_user_idx = i
    if(selected_user_idx >=0):
        print("Matched user", selected_user_idx)
    else:
        print("No match found")
                          

def host_mode_example(port):
    with rsid_py.FaceAuthenticator(port) as authenticator:
        print("Enrolling...")
        authenticator.extract_faceprints_for_enroll(on_progress=on_progress, on_result=on_fp_enroll_result)
        if not faceprints_db:
            print("Enroll failed")
            return
        input("Press anykey to authenticate..")
        print("Authenticating...")                       
        authenticator.extract_faceprints_for_auth(
            on_result=lambda status, new_prints: on_fp_auth_result(status, new_prints, authenticator))

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

    host_mode_example(device.serial_port)