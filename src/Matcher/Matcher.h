// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#pragma once
#include "MatcherImplDefines.h"
#include "RealSenseID/Faceprints.h"
#include "RealSenseID/MatcherDefines.h"
#include <vector>
#include <stdint.h>

namespace RealSenseID
{

// using feature_t = short;
using match_calc_t = short;

class Matcher
{
public:
    // match single vs. single faceprints. Returns updated faceprints if update conditions fulfilled (indicated in
    // result.should_update).
    static MatchResultInternal MatchFaceprints(const MatchElement& probe_faceprints, const Faceprints& existing_faceprints,
                                               Faceprints& updated_faceprints);

    // match single vs. an array of faceprints. Used e.g. when matching user against a set of users in the database.
    // returns updated faceprints if update conditions fulfilled (indicated in result.should_update).
    // internal thresholds will be used.
    static ExtendedMatchResult MatchFaceprintsToArray(const MatchElement& probe_faceprints,
                                                      const std::vector<UserFaceprints_t>& existing_faceprints_array,
                                                      Faceprints& updated_faceprints);

    // match single vs. an array of faceprints. Used e.g. when matching user against a set of users in the database.
    // returns updated faceprints if update conditions fulfilled (indicated in result.should_update).
    // thresholds provided by caller.
    static ExtendedMatchResult MatchFaceprintsToArray(const MatchElement& probe_faceprints,
                                                      const std::vector<UserFaceprints_t>& existing_faceprints_array,
                                                      Faceprints& updated_faceprints, const Thresholds& thresholds);


    // checks the faceprints vector coordinates are in valid range [-1023,+1023].
    // if check_enrollment_vector=false it validates the adaptive faceprints, otherwise it validates the enrollment
    // faceprints.
    static bool ValidateFaceprints(const Faceprints& faceprints, bool check_enrollment_vector = false);

    static bool ValidateFaceprints(const MatchElement& faceprints);

    static void MatchTwoVectors(const feature_t* T1, const feature_t* T2, match_calc_t* match_score,
                                const uint32_t vec_length = RSID_NUM_OF_RECOGNITION_FEATURES);

private:
    static void BlendAverageVector(feature_t* user_adaptive_faceprints, const feature_t* user_probe_faceprints,
                                   const uint32_t vec_length = RSID_NUM_OF_RECOGNITION_FEATURES);

    static bool LimitAdaptiveVector(feature_t* adaptive_faceprints_vec, const feature_t* anchor_faceprints_vec,
                                    const AdaptiveThresholds& adaptiveThresholds,
                                    const uint32_t vec_length = RSID_NUM_OF_RECOGNITION_FEATURES);


    static void HandleThresholdsConfiguration(const bool& probe_has_mask, const Faceprints& existing_faceprints,
                                              AdaptiveThresholds& adaptiveThresholds);

    static short GetMsb(const uint32_t ux);

    static void FaceMatch(const MatchElement& probe_faceprints, const std::vector<UserFaceprints_t>& existing_faceprints_array,
                          ExtendedMatchResult& result, const bool& probe_has_mask);

    static bool GetScores(const MatchElement& probe_faceprints, const std::vector<UserFaceprints_t>& existing_faceprints_array,
                          TagResult& result, const bool& probe_has_mask);

    static bool ValidateVector(const feature_t* T1, const uint32_t vec_length = RSID_NUM_OF_RECOGNITION_FEATURES);

    static bool IsSameVersion(const Faceprints& newFaceprints, const Faceprints& existingFaceprints);

    static bool IsSameVersion(const MatchElement& newFaceprints, const Faceprints& existingFaceprints);

    static void SetToDefaultThresholds(Thresholds& thresholds);

    static void InitAdaptiveThresholds(const Thresholds& thresholds, AdaptiveThresholds& adaptiveThresholds);
};

} // namespace RealSenseID
