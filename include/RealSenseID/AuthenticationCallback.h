// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once

#include "AuthenticateStatus.h"
#include "FaceRect.h"
#include <vector>
#include <string>

namespace RealSenseID
{
/**
 * User defined callback for authentication.
 * Callback will be used to provide feedback to the client.
 */
class RSID_API AuthenticationCallback
{
public:
    virtual ~AuthenticationCallback() = default;

    /**
     * Called to inform the client that authentication operation ended.
     *
     * @param[in] status Final authentication status.
     * @param[in] userId Unique id (31 bytes) of the authenticated user if succeeded. Should be ignored in case of a
     * @param[in] score Matching score result
     * failure.
     */
    virtual void OnResult(AuthenticateStatus status, const char* userId, short score) = 0;

    /**
     * Called to inform the client of problems encountered during the authentication operation.
     *
     * @param[in] hint Hint for the problem encountered.
     * @param[in] frameScore score of processed frame [0-1]
     */
    virtual void OnHint(AuthenticateStatus hint, float frameScore) = 0;


    /**
     * Called to inform the client about detected faces during the authentication operation.
     *
     * @param[in] faces Detected faces. First item is the selected one for the authentication operation.
     * @param[in] ts timestamp
     */
    virtual void OnFaceDetected(const std::vector<FaceRect>& faces, const unsigned int ts)
    {
        // default empty impl for backward compatibility
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
     * Called to inform the client about calculated face distances during the authentication operation.
     *
     * @param[in] distances Calculated face distances in centimeters. First item corresponds to the first detected face.
     * @param[in] ts timestamp
     */
    virtual void OnFaceDistances(const std::vector<double>& distances, const unsigned int ts)
    {
        // default empty impl for backward compatibility
        (void)distances;
        (void)ts;
    }

    /**
     * Called to inform the client about detected persons during the person detection.
     *
     * @param[in] persons Detected persons.
     * @param[in] ts timestamp
     */
    virtual void OnPersonDetected(const std::vector<PersonRect>& persons, const unsigned int ts)
    {
        // default empty impl for backward compatibility
        (void)persons;
        (void)ts;
    }

    /**
     * Called to inform the client about detected poses.
     *
     * @param[in] poses Detected poses.
     * @param[in] ts timestamp
     */
    virtual void OnPoseDetected(const std::vector<PersonPose>& poses, const unsigned int ts)
    {
        // default empty impl for backward compatibility
        (void)poses;
        (void)ts;
    }

    /**
     * Called to inform the client about decoded barcodes during the barcode decoding operation.
     *
     * @param[in] barcodes Decoded barcodes.
     * @param[in] ts timestamp
     */
    virtual void OnBarcodeDecoded(const std::vector<std::string>& barcodes, unsigned int ts)
    {
        // default empty impl for backward compatibility
        (void)barcodes;
        (void)ts;
    }

    /**
     * Called to inform the client that face cropped is ready.
     *
     * @param[in] buffer bgr24 image buffer of the authenticated user face.
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
