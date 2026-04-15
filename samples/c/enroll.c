// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "rsid_c/rsid_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*******************************************************************************
 Enrollment example
 User defined callbacks are called during the enrollment process
 *******************************************************************************/
void my_enroll_status_clbk(rsid_enroll_status status, void* ctx)
{
    printf("Enroll status: %d (%s)\n", status, rsid_enroll_status_str(status));
    if (status == RSID_Enroll_Success)
    {
        printf("Enroll success\n");
    }
}

void my_enroll_hint_clbk(rsid_enroll_status hint, float frameScore, void* ctx)
{
    printf("Enroll hint: %d (%s)\n", hint, rsid_enroll_status_str(hint));
}

void my_enroll_progress_clbk(rsid_face_pose pose, void* ctx)
{
    printf("Enroll face pose callback: %d (%s)\n", pose, rsid_face_pose_str(pose));
}

void my_face_detected_clbk(const rsid_face_rect faces[], size_t n_faces, unsigned int ts, void* ctx)
{
    for (size_t i = 0; i < n_faces; i++)
    {
        rsid_face_rect face = faces[i];
        printf("Detected face #%zu: %u,%u %ux%ux (ts=%u)\n", i + 1, face.x, face.y, face.w, face.h, ts);
    }
}


int main()
{
    rsid_device_info devices[1];
    int n_devices = rsid_discover_devices(devices, 1);
    if (n_devices <= 0)
    {
        printf("No device detected\n");
        exit(1);
    }
    printf("Using device on port %s\n", devices[0].serial_port);

    rsid_serial_config serial_config = {devices[0].serial_port};

    rsid_authenticator* authenticator = rsid_create_authenticator();
    if (!authenticator)
    {
        printf("Failed creating authenticator\n");
        exit(1);
    }

    rsid_status status = rsid_connect(authenticator, &serial_config);
    if (status != RSID_Ok)
    {
        printf("Failed connecting: %s\n", rsid_status_str(status));
        exit(1);
    }

    const char* user_id = "some_user_id";
    printf("Enrolling user \"%s\"\n", user_id);
    rsid_enroll_args enroll_args;
    enroll_args.user_id = user_id; /* user id. null terminated string of ascii chars (max 15 chars + null) */
    enroll_args.status_clbk = my_enroll_status_clbk;
    enroll_args.hint_clbk = my_enroll_hint_clbk;
    enroll_args.progress_clbk = my_enroll_progress_clbk;
    enroll_args.face_detected_clbk = my_face_detected_clbk;
    enroll_args.ctx = NULL; /* user defined context struct. set to null if not needed. */

    status = rsid_enroll(authenticator, &enroll_args);
    if (status != RSID_Ok)
    {
        printf("Error status: %d (%s)\n", status, rsid_status_str(status));
    }
    rsid_destroy_authenticator(authenticator);
    return 0;
}
