#ifndef __RSID_MATCHER_DEFINES_H__
#define __RSID_MATCHER_DEFINES_H__

// This file contains matcher related defines.
// This file should be compiled in C and C++ compilers - so we can use it in both C and C++ clients.
// For this reason - only structs used here (no classes).
//
#ifdef __cplusplus
namespace RealSenseID
{
#endif // __cplusplus

typedef short match_calc_t;

/**
 * Result used by the Matcher module.
 */
struct MatchResultHost
{
    bool success = false;
    bool should_update = false;
    match_calc_t score = 0;
};

struct ExtendedMatchResult
{
    bool isSame = false;
    int userId = -1;
    match_calc_t maxScore = 0;
    bool should_update = false;
};

struct MatchResultInternal
{
    bool success = false;
    bool should_update = false;
    match_calc_t score = 0;
};

// this struct must abe aligned with the onw in AlgorithmsWrapper.py.
struct TagResult
{
    int idx = -1;
    match_calc_t score = 0;
    int should_update = 0;
};

//=============================================================================
// Thresholds related defines.
//=============================================================================
// for adaptive-learning w/wo mask we adjust the thresholds to some configuration
// w.r.t. the 2 vectors been matched. Naming convention here:
//      p - probe vector, g - gallery vector.
//      M - with mask, NM - no-mask.
// so for example _pM_gNM means we're going to match
// a probe vector with mask VS. gallery vector without mask.
typedef enum AdjustableThresholdsConfig
{
    ThresholdConfig_pNM_gNM = 0,
    ThresholdConfig_pM_gNM = 1,
    ThresholdConfig_pM_gM = 2,
    NumThresholdConfigs
} ThresholdsConfigEnum;

// Single set of matcher thresholds.
struct Thresholds
{
    // naming convention here :
    //
    // M = with mask, NM = no mask (e.g. without mask)
    // p - prob, g - gallery (we match prob vector against gallery vector)
    //
    // So for example:
    //
    // strongThreshold_pMgNM - refers to a prob vector with-mask
    //                         against gallery vector without-mask.
    //
    short identicalThreshold_gNMgNM; // gallery no-mask, gallery no-mask.
    short identicalThreshold_gMgNM;  // gallery mask, gallery no-mask.

    short strongThreshold_pNMgNM;              // prob no-mask, gallery no-mask.
    short strongThreshold_pMgM;                // prob with mask, gallery with mask.
    short strongThreshold_pMgNM;               // prob with mask, gallery no-mask.
    short strongThreshold_pNMgNM_rgbImgEnroll; // this one is for Enroll-from-Image feature.

    // update thresholds.
    short updateThreshold_pNMgNM;
    short updateThreshold_pMgM;
    short updateThreshold_pMgNM_First; // for opening first adaptive with-mask vector.
};

// Adaptive thresholds : set during runtime.
struct AdaptiveThresholds
{
    // static thresholds.
    Thresholds thresholds;

    // active thresholds.
    // these are the active thresholds i.e. they will be set during runtime
    // given the matched faceprints pair (w/wo mask etc).
    ThresholdsConfigEnum activeConfig;
    short activeIdenticalThreshold;
    short activeStrongThreshold;
    short activeUpdateThreshold;
};
//=============================================================================
//=============================================================================

#ifdef __cplusplus
} // close namespace RealSenseID
#endif // __cplusplus

#endif // __RSID_MATCHER_DEFINES_H__
