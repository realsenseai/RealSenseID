// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

#include "Matcher.h"
#include "Logger.h"
#include "RealSenseID/Faceprints.h"
#include <cmath>
#include <assert.h>
#include <cstring>
#include <stdexcept>
#include <vector>
#include <algorithm>
// #include <iostream>

/*
RSID-MATCHER INFO: Functions implementation here in Matcher.cc uses integer arithmetic and tailored to integer-valued
feature-vectors with: (1) length 512 (2) integer valued features in range [-1023,+1023]. Adjustments and checks will be
taken if (1) vectors length becomes more than 512 (2) features value range becomes wider than [-1023,+1023].
RSID-MARCHER INFO: Such adjustments/checks may be required due to accumulators and bit shifts used.
*/

namespace RealSenseID
{
using match_calc_t = short;

#include "MatcherStatics.cc"

static const char* LOG_TAG = "Matcher";

bool Matcher::IsSameVersion(const Faceprints& newFaceprints, const Faceprints& existingFaceprints)
{
    bool versionsMatch = (newFaceprints.data.version == existingFaceprints.data.version);
    if (!versionsMatch)
    {
        LOG_ERROR(LOG_TAG, "Faceprints versions don't match");
    }
    return versionsMatch;
}

bool Matcher::IsSameVersion(const MatchElement& newFaceprints, const Faceprints& existingFaceprints)
{
    bool versionsMatch = (newFaceprints.data.version == existingFaceprints.data.version);
    if (!versionsMatch)
    {
        LOG_ERROR(LOG_TAG, "Faceprints versions don't match");
    }
    return versionsMatch;
}

void Matcher::SetToDefaultThresholds(Thresholds& thresholds)
{
    thresholds.identicalThreshold_gNMgNM = s_identicalThreshold_gNMgNM_LowConfLevel;
    thresholds.identicalThreshold_gMgNM = s_identicalThreshold_gMgNM_LowConfLevel;

    thresholds.strongThreshold_pNMgNM = s_strongThreshold_pNMgNM_LowConfLevel;
    thresholds.strongThreshold_pMgM = s_strongThreshold_pMgM_LowConfLevel;
    thresholds.strongThreshold_pMgNM = s_strongThreshold_pMgNM_LowConfLevel;
    thresholds.strongThreshold_pNMgNM_rgbImgEnroll = s_strongThreshold_pNMgNM_rgbImgEnroll_LowConfLevel;

    thresholds.updateThreshold_pNMgNM = s_updateThreshold_pNMgNM_LowConfLevel;
    thresholds.updateThreshold_pMgM = s_updateThreshold_pMgM_LowConfLevel;
    thresholds.updateThreshold_pMgNM_First = s_updateThreshold_pMgNM_First_LowConfLevel;
}

void Matcher::InitAdaptiveThresholds(const Thresholds& thresholds, AdaptiveThresholds& adaptiveThresholds)
{
    adaptiveThresholds.thresholds = thresholds;
}

void Matcher::HandleThresholdsConfiguration(const bool& probe_has_mask, const Faceprints& existing_faceprints,
                                            AdaptiveThresholds& adaptiveThresholds)
{
    // Fix for Enroll from Image : we need different strong threshold.
    // Does the DB entry of the user is W10type ?
    bool isEnrolledTypeInDbIsRgb = (FaceprintsTypeEnum::RGB == existing_faceprints.data.featuresType);

    // here we handle with/without mask adaptive learning.
    // we adjust the correct thresholds and adaptiveVector for w/wo mask scenarios.
    if (!probe_has_mask)
    {
        adaptiveThresholds.activeConfig = ThresholdsConfigEnum::ThresholdConfig_pNM_gNM;
        adaptiveThresholds.activeIdenticalThreshold = adaptiveThresholds.thresholds.identicalThreshold_gNMgNM;
        adaptiveThresholds.activeStrongThreshold = adaptiveThresholds.thresholds.strongThreshold_pNMgNM;
        adaptiveThresholds.activeUpdateThreshold = adaptiveThresholds.thresholds.updateThreshold_pNMgNM;

        // use different (lower) strong threshold in case the DB enrollment was from rgb image.
        if (isEnrolledTypeInDbIsRgb)
        {
            adaptiveThresholds.activeStrongThreshold = adaptiveThresholds.thresholds.strongThreshold_pNMgNM_rgbImgEnroll;
        }
    }
    else
    {
        // apply adaptation on the WithoutMask[] vector
        adaptiveThresholds.activeConfig = ThresholdsConfigEnum::ThresholdConfig_pM_gNM;
        // if with-mask the anchor vector is the _gNM vector anyway, so identical threshold
        // is _pMgNM.
        adaptiveThresholds.activeIdenticalThreshold = adaptiveThresholds.thresholds.identicalThreshold_gMgNM;
        adaptiveThresholds.activeStrongThreshold = adaptiveThresholds.thresholds.strongThreshold_pMgNM;
        adaptiveThresholds.activeUpdateThreshold = adaptiveThresholds.thresholds.updateThreshold_pMgNM_First;
    }

#if (RSID_MATCHER_DEBUG_LOGS)
    LOG_DEBUG(LOG_TAG, "----> Matcher active setup = %d : hasMask = %d, strongTH = %d, updateTH = %d, identicalTH = %d.",
              adaptiveThresholds.activeConfig, probe_has_mask, adaptiveThresholds.activeStrongThreshold,
              adaptiveThresholds.activeUpdateThreshold, adaptiveThresholds.activeIdenticalThreshold);
#endif

    return;
}

bool Matcher::GetScores(const MatchElement& probe_faceprints, const std::vector<UserFaceprints_t>& existing_faceprints_array,
                        TagResult& result, const bool& probe_has_mask)
{
    if (existing_faceprints_array.size() == 0)
    {
        LOG_ERROR(LOG_TAG, "Can't match with empty array.");
        return false;
    }

    result.score = 0;
    result.idx = -1;

    match_calc_t maxScore = -1; // must init to -1 so that maximum will be saved if matchScore is 0 !!!
    match_calc_t matchScore = -1;
    int numberOfSubjects = (int)existing_faceprints_array.size();
    int maxSubject = -1;
    uint32_t vec_length = RSID_NUM_OF_RECOGNITION_FEATURES;

    const feature_t* probeVector = (feature_t*)(&(probe_faceprints.data.featuresVector[0]));
    feature_t* galeryAdaptiveVector = nullptr;

    for (int subjectIndex = 0; subjectIndex < numberOfSubjects; subjectIndex++)
    {
        matchScore = s_minPossibleScore;
        auto& existing_faceprints = existing_faceprints_array[subjectIndex];

        if (!ValidateFaceprints(existing_faceprints.faceprints))
        {
            LOG_ERROR(LOG_TAG, "Invalid faceprints vector range");
            return false;
        }

        if (!IsSameVersion(probe_faceprints, existing_faceprints.faceprints))
        {
            LOG_ERROR(LOG_TAG, "Mismatch in faceprints versions");
            return false;
        }

        galeryAdaptiveVector = (feature_t*)(&existing_faceprints.faceprints.data.adaptiveDescriptorWithoutMask[0]);
        MatchTwoVectors(probeVector, galeryAdaptiveVector, &matchScore, vec_length);

        // save max found so far
        if (matchScore > maxScore)
        {
            maxScore = matchScore;
            maxSubject = static_cast<int>(subjectIndex);
        }
    }

    result.score = maxScore;
    result.idx = maxSubject;

    return true;
}

void Matcher::FaceMatch(const MatchElement& probe_faceprints, const std::vector<UserFaceprints_t>& existing_faceprints_array,
                        ExtendedMatchResult& result, const bool& probe_has_mask)
{
    result.isSame = false;
    result.maxScore = 0;
    result.userId = -1;
    result.should_update = false;

    TagResult scoresResult;
    // this function returns the index and info of the best score winner in the array.
    bool isScoreSuccess = GetScores(probe_faceprints, existing_faceprints_array, scoresResult, probe_has_mask);

    if (!isScoreSuccess)
    {
        LOG_ERROR(LOG_TAG, "Failed during GetScores() - please check.");
        return;
    }

    result.maxScore = scoresResult.score;
    result.userId = scoresResult.idx;

    // don't set yet - we must call HandleThresholdsConfiguration() first!
    // result.isSame = (scoresResult.score > thresholds.activeStrongThreshold);
}

bool Matcher::ValidateFaceprints(const Faceprints& faceprints, bool check_enrollment_vector)
{
    static_assert((RSID_NUM_OF_RECOGNITION_FEATURES <= 512),
                  "Vector length is higher than 512 - may need careful test and check, due to integer arithmetics and "
                  "overflow risk!");

    uint32_t nfeatures = static_cast<uint32_t>(RSID_NUM_OF_RECOGNITION_FEATURES);

    bool is_valid = true;

    if (check_enrollment_vector)
    {
        is_valid = ValidateVector(&faceprints.data.enrollmentDescriptor[0], nfeatures);
    }
    else
    {
        is_valid = ValidateVector(&faceprints.data.adaptiveDescriptorWithoutMask[0], nfeatures);
    }

    if (!is_valid)
    {
        LOG_ERROR(LOG_TAG, "Vector (Faceprints faceprint) validation failed!");
    }

    return is_valid;
}

bool Matcher::ValidateFaceprints(const MatchElement& faceprints)
{
    static_assert((RSID_NUM_OF_RECOGNITION_FEATURES <= 512),
                  "Vector length is higher than 512 - may need careful test and check, due to integer arithmetics and "
                  "overflow risk!");

    uint32_t nfeatures = static_cast<uint32_t>(RSID_NUM_OF_RECOGNITION_FEATURES);

    bool is_valid = ValidateVector(&faceprints.data.featuresVector[0], nfeatures);

    if (!is_valid)
    {
        LOG_ERROR(LOG_TAG, "Vector (MatchElement faceprint) validation failed!");
    }

    return is_valid;
}

ExtendedMatchResult Matcher::MatchFaceprintsToArray(const MatchElement& probe_faceprints,
                                                    const std::vector<UserFaceprints_t>& existing_faceprints_array,
                                                    Faceprints& updated_faceprints)
{
    Thresholds thresholds;
    SetToDefaultThresholds(thresholds);

    ExtendedMatchResult result = MatchFaceprintsToArray(probe_faceprints, existing_faceprints_array, updated_faceprints, thresholds);

    return result;
}

ExtendedMatchResult Matcher::MatchFaceprintsToArray(const MatchElement& probe_faceprints,
                                                    const std::vector<UserFaceprints_t>& existing_faceprints_array,
                                                    Faceprints& updated_faceprints, const Thresholds& thresholds)
{
    ExtendedMatchResult result;

    result.userId = -1;
    result.maxScore = 0;

    if (!ValidateFaceprints(probe_faceprints))
    {
        LOG_ERROR(LOG_TAG, "Faceprints vector failed range validation.");
        return result;
    }

    if (existing_faceprints_array.size() == 0)
    {
        LOG_ERROR(LOG_TAG, "Faceprints array size is 0.");
        return result;
    }

    if (probe_faceprints.data.version != existing_faceprints_array[0].faceprints.data.version)
    {
        LOG_ERROR(LOG_TAG, "version mismatch between 2 vectors. Skipping this match()!");
        return result;
    }

    // Try to match the 2 faceprints, and raise should_update flag respecively.
    // note that here we also set the active thresholds configurtion
    // which is used for adaptive-learning
    feature_t probeFaceFlags = probe_faceprints.data.featuresVector[RSID_INDEX_IN_FEATURES_VECTOR_TO_FLAGS];

    FaceMatch(probe_faceprints, existing_faceprints_array, result, false);

    size_t user_index = (size_t)result.userId;

    // if no user matched, finish here and return.
    if (user_index >= existing_faceprints_array.size())
    {
        LOG_ERROR(LOG_TAG, "Invalid user_index : Skipping function.");
        return result;
    }

    AdaptiveThresholds adaptiveThresholds;
    InitAdaptiveThresholds(thresholds, adaptiveThresholds);

    // here we handle with/without mask adaptive learning.
    // we choose the correct thresholds Configuration, based on the probe-vector and the (matched) gallery-vector.
    HandleThresholdsConfiguration(false, existing_faceprints_array[user_index].faceprints, adaptiveThresholds);

    // here correct active thresholds set correctly, so we can use them.
    result.isSame = (result.maxScore > adaptiveThresholds.activeStrongThreshold);
    result.should_update = (result.maxScore >= adaptiveThresholds.activeUpdateThreshold) && result.isSame;

    // Does the DB entry of the user is RGB type ?
    // bool isEnrolledTypeInDbIsRgb = (FaceprintsTypeEnum::RGB ==
    // existing_faceprints_array[user_index].faceprints.data.featuresType);

    // if should_update then we create an update vector such that:
    // (1) the current new vector is blended into the latest adaptive vector.
    // (2) then we make sure that the updated vector is not too far from the enrollment vector.
    if (result.should_update)
    {
        // Init updated_faceprints to the faceprints already exists in the DB
        //
        updated_faceprints = existing_faceprints_array[user_index].faceprints;

        const uint32_t vec_length = RSID_NUM_OF_RECOGNITION_FEATURES;
        const feature_t* probeVector = &probe_faceprints.data.featuresVector[0];
        const feature_t* anchorVector = nullptr;
        feature_t* galeryAdaptiveVector = nullptr;

        // handle with/without mask vectors properly.
        // set adaptive vector's flags (based on thresholds configuration).
        anchorVector = &updated_faceprints.data.enrollmentDescriptor[0];
        galeryAdaptiveVector = &updated_faceprints.data.adaptiveDescriptorWithoutMask[0];
        // galeryAdaptiveVector[RSID_INDEX_IN_FEATURES_VECTOR_TO_FLAGS] =
        // FaVectorFlagsEnum::VecFlagValidWithoutMask;
#if (RSID_MATCHER_DEBUG_LOGS)
        LOG_DEBUG(LOG_TAG, "----> Without-mask adaptation.");
#endif

        // blend the current adaptive galery vector with the new vector
        BlendAverageVector(galeryAdaptiveVector, probeVector, vec_length);

        // make sure blended adaptive vector is not too far from enrollment vector
        bool update_was_ok = LimitAdaptiveVector(galeryAdaptiveVector, anchorVector, adaptiveThresholds, vec_length);

        // disable update flag if something went wrong in the update process.
        result.should_update = result.should_update && update_was_ok;
    }

    // information log message here.
    LOG_DEBUG(LOG_TAG,
              "match Score: %d, isSame: %d, shouldUpdate: %d, activeStrongTH: %d, activeUpdateTH: %d, "
              "activeThreshConfig: %d.",
              result.maxScore, result.isSame, result.should_update, adaptiveThresholds.activeStrongThreshold,
              adaptiveThresholds.activeUpdateThreshold, adaptiveThresholds.activeConfig);

    return result;
}

bool Matcher::LimitAdaptiveVector(feature_t* adaptive_faceprints_vec, const feature_t* anchor_faceprints_vec,
                                  const AdaptiveThresholds& adaptiveThresholds, const uint32_t vec_length)
{
    bool success = true;

    if ((nullptr == anchor_faceprints_vec) || (nullptr == adaptive_faceprints_vec))
    {
        LOG_ERROR(LOG_TAG, "Null pointer detected : Skipping function.");

        return false;
    }
    // Explain - as long as the adaptive vector is "too far" from the anchor vector, we
    // want to update the adaptive vector with more samples of the anchor vector.
    // hence refreshing the adaptive to be more similar to the anchor vector.

    match_calc_t match_score = 0;
    MatchTwoVectors(adaptive_faceprints_vec, anchor_faceprints_vec, &match_score, vec_length);

#if (RSID_MATCHER_DEBUG_LOGS)
    LOG_DEBUG(LOG_TAG, "----> match score (adaptive vs. anchor) = %d.", match_score);
#endif

    // adding limit on number of iterations, e.g. if one vector is all zeros we'll get
    // deadlock here.
    uint32_t cnt_iter = 0;

    uint32_t limit_num_iters = static_cast<uint32_t>(RSID_LIMIT_NUM_ITERS_NM);
    if (adaptiveThresholds.activeConfig != ThresholdsConfigEnum::ThresholdConfig_pNM_gNM)
    {
        limit_num_iters = static_cast<uint32_t>(RSID_LIMIT_NUM_ITERS_M);
    }

    while ((match_score < adaptiveThresholds.activeIdenticalThreshold))
    {
#if (RSID_MATCHER_DEBUG_LOGS)
        LOG_DEBUG(LOG_TAG, "----> adaptive vector is far from anchor vector. Doing update while() loop : count = %d. score = %d.", cnt_iter,
                  match_score);
#endif

        BlendAverageVector(adaptive_faceprints_vec, anchor_faceprints_vec, vec_length);

        MatchTwoVectors(adaptive_faceprints_vec, anchor_faceprints_vec, &match_score, vec_length);

        cnt_iter++;
        if (cnt_iter > limit_num_iters)
        {
#if (RSID_MATCHER_DEBUG_LOGS)
            LOG_DEBUG(LOG_TAG,
                      "----> Update while() loop count reached the limit of %d iterations. Breaking the while() loop "
                      "with score = %d.",
                      cnt_iter, match_score);
#endif

            success = false;
            break;
        }
    }

    return success;
}

static void ConvertFaceprintsToUserFaceprints(const Faceprints& faceprints, UserFaceprints_t& extended_faceprints)
{
    extended_faceprints.faceprints.data.version = faceprints.data.version;
    extended_faceprints.faceprints.data.featuresType = faceprints.data.featuresType;
    extended_faceprints.faceprints.data.flags = faceprints.data.flags;

    static_assert(sizeof(extended_faceprints.faceprints.data.adaptiveDescriptorWithoutMask) ==
                      sizeof(faceprints.data.adaptiveDescriptorWithoutMask),
                  "updated faceprints (with mask) sizes don't match");
    ::memcpy(&extended_faceprints.faceprints.data.adaptiveDescriptorWithoutMask[0], &faceprints.data.adaptiveDescriptorWithoutMask[0],
             sizeof(faceprints.data.adaptiveDescriptorWithoutMask));

    static_assert(sizeof(extended_faceprints.faceprints.data.enrollmentDescriptor) == sizeof(faceprints.data.enrollmentDescriptor),
                  "enrollment faceprints sizes don't match");
    ::memcpy(&extended_faceprints.faceprints.data.enrollmentDescriptor[0], &faceprints.data.enrollmentDescriptor[0],
             sizeof(faceprints.data.enrollmentDescriptor));
}

MatchResultInternal Matcher::MatchFaceprints(const MatchElement& probe_faceprints, const Faceprints& existing_faceprints,
                                             Faceprints& updated_faceprints)
{
    // init match result
    MatchResultInternal matchResult;
    matchResult.success = false;
    matchResult.should_update = false;
    matchResult.score = 0;

    /*
    // No need to validate probe_faceprints here because its done during MatchFaceprintsToArray() below.
    */

    if (!ValidateFaceprints(existing_faceprints))
    {
        LOG_ERROR(LOG_TAG, "existing faceprints vector : failed range validation.");
        return matchResult;
    }

    // init existing faceprints array
    UserFaceprints_t existing_extended_faceprints;
    ConvertFaceprintsToUserFaceprints(existing_faceprints, existing_extended_faceprints);
    std::vector<UserFaceprints_t> existing_faceprints_array = {existing_extended_faceprints};

    // match using shared code
    ExtendedMatchResult result = MatchFaceprintsToArray(probe_faceprints, existing_faceprints_array, updated_faceprints);

#if (RSID_MATCHER_DEBUG_LOGS)
    LOG_DEBUG(LOG_TAG, "Match score: %f, isSame: %d, shouldUpdate: %d", float(result.maxScore), result.isSame, result.should_update);
#endif

    // set results into output struct
    matchResult.score = result.maxScore;
    matchResult.success = result.isSame;

    matchResult.should_update = result.should_update;

    return matchResult;
}

// NOTE - Here below we have functions with fixed point calculations.
// These are suitable for fixed-point arithmetic (and feature vectors are integer-valued respectively).
//
bool Matcher::ValidateVector(const feature_t* vec, const uint32_t vec_length)
{
    for (uint32_t i = 0; i < vec_length; i++)
    {
        feature_t curr_feature = (feature_t)vec[i];

        if (curr_feature > s_maxFeatureValue || curr_feature < s_minFeatureValue)
        {
            return false;
        }
    }

    return true;
}

void Matcher::BlendAverageVector(feature_t* user_adaptive_faceprints, const feature_t* user_probe_faceprints, const uint32_t vec_length)
{
    // FUNCTION EXPLAINED :
    //
    // "AV" is the adaptive vector, "NV" is the new vector.
    //
    // we aim to do weighted sum :
    // 		v = round( (30/31)*AV + (1/31)*NV )
    // so we do:
    // 		v = int( (30/31)*AV + (1/31)*NV +/- 1/2)
    // lets multiply all by 2 and then divide by 2 :
    //		v = int ( [((30*2)/31)*AV + (2/31)*NV +/- 1] * 1/2 )
    //      v = int (2*30*AV + 2*NV +/- 31) / (2*31)
    //
    if ((nullptr == user_adaptive_faceprints) || (nullptr == user_probe_faceprints))
    {
        LOG_ERROR(LOG_TAG, "Null pointer detected : Skipping function.");
        return;
    }

    int history_weight = RSID_UPDATE_GALLERY_HISTORY_WEIGHT;
    int round_value = (history_weight + 1);
    for (uint32_t i = 0; i < vec_length; ++i)
    {
        int32_t v = static_cast<int32_t>(user_adaptive_faceprints[i]);
        v *= 2 * history_weight;
        v += 2 * (int)(user_probe_faceprints[i]);
        v = (v >= 0) ? (v + round_value) : (v - round_value);
        v /= (2 * round_value);

        short feature_value = static_cast<short>(v);
        feature_value = (feature_value > s_maxFeatureValue) ? s_maxFeatureValue : feature_value;
        feature_value = (feature_value < s_minFeatureValue) ? s_minFeatureValue : feature_value;

        user_adaptive_faceprints[i] = static_cast<short>(v);
    }
}

short Matcher::GetMsb(const uint32_t ux)
{
    // we find the msb index of a positive integer.
    //
    // index count starts from 1, so for example: msb of 0x10 is 2 , msb of 0x1011 is 4.
    // exception : msb of 0 returns 0.
    // optimization - we avoid while() loop, and make efficient constant time performance.
    // method - we check the range of x in a binary-search manner.
    // note that we basically compute here msb = floor(log2(x))+1.
    //
    uint32_t x = ux;
    uint32_t shift = 0;
    uint32_t msb = 0;

    msb = (x > 0xFFFF) << 4;
    x >>= msb;
    shift = (x > 0xFF) << 3;
    x >>= shift;
    msb |= shift;
    shift = (x > 0xF) << 2;
    x >>= shift;
    msb |= shift;
    shift = (x > 0x3) << 1;
    x >>= shift;
    msb |= shift;
    msb |= (x >> 1);

    // msb of 0 is 0. For any other - add 1 to msb index.
    msb = (ux == 0) ? 0 : (msb + 1);

    return static_cast<short>(msb);
}

void Matcher::MatchTwoVectors(const feature_t* T1, const feature_t* T2, match_calc_t* match_score, const uint32_t vec_length)
{
    // Normalized cross-correlation (ncc) is calculate here with integer arithmetic only.
    // negative cross-correlation will be considered 0 - good for our needs.
    //
    // out = ncc(vec1, vec2)
    //
    // Correct calculation is expected if :
    //
    // (1) all feature vector values are integers in range R = (-1024, +1024).
    // (2) length of vectors is 512.
    //
    // IMPORTANT : If one of the assumptions is violated (longer vectors, or wider range R) - then the calculation may
    // be incorrect due to overflow during bit shifts.
    // We validate these conditions in ValidateFaceprints().
    //
    // The calculated ncc will be an integer in range [0, 4096], with 4096 expected for equal vectors (that satisfy the
    // assumptions above).
    //
    if (nullptr == match_score)
    {
        LOG_ERROR(LOG_TAG, "Null pointer detected : Skipping function.");
        return;
    }

    //  "Matcher may require carefull adjustments and checks for vectors longer than 512."
    if (vec_length > 512)
    {
        LOG_ERROR(LOG_TAG, "Vector length > 512 : Matcher may require carefull adjustments. Skipping this function, please check!");

        *match_score = 0;

        return;
    }

    uint32_t nfeatures = vec_length;

    int32_t corr = 0;
    int32_t min_corr = 0;
    uint32_t ucorr = 0;
    uint32_t norm1 = 0;
    uint32_t norm2 = 0;

    for (uint32_t i = 0; i < nfeatures; ++i)
    {
        int32_t t1 = static_cast<int32_t>(T1[i]);
        int32_t t2 = static_cast<int32_t>(T2[i]);

        corr += t1 * t2;
        norm1 += t1 * t1;
        norm2 += t2 * t2;
    }

    // protect division by 0.
    norm1 = (norm1 == 0) ? 1 : norm1;
    norm2 = (norm2 == 0) ? 1 : norm2;

    // negative correlation will be considered as 0 correlation.
    ucorr = static_cast<uint32_t>(std::max(corr, min_corr));

    short norm1_msb = GetMsb(norm1);
    short norm2_msb = GetMsb(norm2);
    short corr_msb = GetMsb(ucorr);
    int32_t min_shift = 0;

    short max_corr_shift = static_cast<short>(32 - corr_msb);
    short max_shift1 = static_cast<short>(16 - static_cast<short>(std::max(static_cast<int32_t>(corr_msb - norm1_msb), min_shift)));
    short max_shift2 = static_cast<short>(16 - static_cast<short>(std::max(static_cast<int32_t>(corr_msb - norm2_msb), min_shift)));

    short shift1 = static_cast<short>(std::min(static_cast<int32_t>(max_shift1), static_cast<int32_t>(max_corr_shift)));
    short shift2 = static_cast<short>(std::min(static_cast<int32_t>(max_shift2), static_cast<int32_t>(max_corr_shift)));
    short total_shift = static_cast<short>(shift1 + shift2);
    short shift_back = static_cast<short>(total_shift - 12);

    uint32_t norm_corr1 = (ucorr << shift1) / norm1;
    uint32_t norm_corr2 = (ucorr << shift2) / norm2;
    uint32_t similarity = norm_corr1 * norm_corr2;

    uint32_t grade = 0;

    if (shift_back >= 0)
    {
        grade = (similarity >> shift_back);
    }
    else
    {
        grade = (similarity << (-shift_back));
    }

    // LOG_DEBUG(LOG_TAG, "ncc result: -----> grade = %u", grade);

    *match_score = static_cast<match_calc_t>(grade);
}

} // namespace RealSenseID
