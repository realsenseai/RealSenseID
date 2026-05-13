// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "EnrollStatus.h"
#include "FacePose.h"
#include "FaceRect.h"
#include "Faceprints.h"
#include <vector>

namespace RealSenseID
{
/**
 * User defined callback for faceprints extraction.
 * Callback will be used to provide feedback to the client.
 */
class EnrollFaceprintsExtractionCallback
{
public:
    virtual ~EnrollFaceprintsExtractionCallback() = default;

    /**
     * Called to inform the client that enroll operation ended.
     *
     * @param[in] status Final enroll status.
     * @param[in] faceprints Pointer to the requested faceprints which were just extracted from the device
     */
    virtual void OnResult(const EnrollStatus status, const ExtractedFaceprints* faceprints) = 0;

    /**
     * Called to inform the client whenever progress to enrollment has been made.
     *
     * @param[in] pose Current pose detected.
     */
    virtual void OnProgress(const FacePose pose) = 0;

    /**
     * Called to inform the client of problems encountered during the enrollment operation.
     *
     * @param[in] hint Hint for the problem encountered.
     * @param[in] frameScore Score of current frame [0-1].
     */
    virtual void OnHint(const EnrollStatus hint, float frameScore) = 0;

    /**
     * Called to inform the client about detected faces during the authentication operation.
     *
     * @param[in] face Detected faces. First item is the selected one for the authentication operation.
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
     * Called to inform the client that face cropped is ready.
     *
     * @param[in] buffer rgb24 image buffer of the authenticated user face.
     * @param[in] width image width.
     * @param[in] height image height.
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
