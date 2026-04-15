// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "rsid_c/rsid_client.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*******************************************************************************
 Authentication example.

 User defined callbacks are called during the authentication process
 Upon success (RSID_Auth_Success) the user_id param points to the user id.
 *******************************************************************************/
void my_auth_status_clbk(rsid_auth_status status, const char* user_id, short score, void* ctx)
{
    printf("Authentication status: %d (%s)\n", status, rsid_auth_status_str(status));

    if (status == RSID_Auth_Success)
    {
        printf("Authentication Success. User id: %s\n", user_id);
    }
}

void my_auth_hint_clbk(rsid_auth_status hint, float frameScore, void* ctx)
{
    printf("Authentication hint: %d (%s)\n", hint, rsid_auth_status_str(hint));
}

void my_face_detected_calbk(const rsid_face_rect faces[], size_t n_faces, unsigned int ts, void* ctx)
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

    printf("\nAuthenticating user..\n");
    rsid_auth_args auth_args;
    auth_args.result_clbk = my_auth_status_clbk;
    auth_args.hint_clbk = my_auth_hint_clbk;
    auth_args.face_detected_clbk = my_face_detected_calbk;
    auth_args.ctx = NULL; /* user defined context. set to null if not needed. */

    status = rsid_authenticate(authenticator, &auth_args);
    if (status != RSID_Ok)
    {
        printf("Error status: %d (%s)\n", status, rsid_status_str(status));
    }

    rsid_destroy_authenticator(authenticator);
    return 0;
}
