// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "AuthenticateStatus.h"
#include "FaceRect.h"
#include "Faceprints.h"
#include <vector>

namespace RealSenseID
{
/**
 * User defined callback for faceprints extraction.
 * Callback will be used to provide feedback to the client.
 */
class AuthFaceprintsExtractionCallback
{
public:
    virtual ~AuthFaceprintsExtractionCallback() = default;

    /**
     * Called to inform the client on the result of faceprints extraction, and pass the faceprints in case of success
     *
     * @param[in] status Final authentication status.
     * @param[in] faceprints Pointer to the requested faceprints which were just extracted from the device.
     */
    virtual void OnResult(const AuthenticateStatus status, const ExtractedFaceprints* faceprints) = 0;

    /**
     * Called to inform the client of problems encountered during the authentication operation.
     *
     * @param[in] hint Hint for the problem encountered.
     */
    virtual void OnHint(const AuthenticateStatus hint, float frameScore) = 0;


    /**
     * Called to inform the client about detected faces during the authentication operation.
     *
     * @param[in] faces Detected faces. First item is the selected one for the authentication operation.
     * @param[in] ts Timestamp
     */
    virtual void OnFaceDetected(const std::vector<FaceRect>& faces, const unsigned int ts)
    {
        // default empty impl for backward compatibilty
        (void)faces;
        (void)ts;
    }

    /**
     * Called to inform the client about detected face landmarks during the authentication operation.
     *
     * @param[in] landmarks Detected face landmarks. First item is the selected one for the authentication operation.
     * @param[in] ts timestamp
     */
    virtual void OnLandmarksDetected(const std::vector<FaceLandmarks>& landmarks, const unsigned int ts)
    {
        // default empty impl for backward compatibility
        (void)landmarks;
        (void)ts;
    }

    /**
     * Called to inform the client about detected face distances during the authentication operation.
     *
     * @param[in] distances Detected face distances. First item is the selected one for the authentication operation.
     * @param[in] ts timestamp
     */
    virtual void OnFaceDistances(const std::vector<double>& distances, const unsigned int ts)
    {
        // default empty impl for backward compatibility
        (void)distances;
        (void)ts;
    }

    /**
     * Called to inform the client that face cropped is ready.
     *
     * @param[in] buffer bgr24 image buffer of the authenticated user face.
     * @param[in] width image width.
     * @param[in] height image height.
     * @param[in] ts Timestamp
     */
    virtual void OnFaceCroppedImage(const unsigned char* buffer, const unsigned int width, const unsigned int height, const unsigned int ts)
    {
        // default empty impl for backward compatibility
        (void)buffer;
        (void)width;
        (void)height;
        (void)ts;
    }
};

} // namespace RealSenseID
