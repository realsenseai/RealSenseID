#pragma once

// Matcher impl definitions.
// note that we work with integer-valued feature vectors (value range [-1023,+1023]).
//

#define RSID_MIN_POSSIBLE_SCORE (0)
#define RSID_MAX_POSSIBLE_SCORE (4096)

// Integer valued matching thresholds. Used for adaptive-learning with/without mask faceprints.
// Naming convention:
//		M - for vectors with mask
//		NM - for vectors with no-mask
//		G - galerry vector
//      P - probe vector
//      * we match between a new faceprint vector (probe user) VS. an existing gallery vector.
//
// so for example - RSID_STRONG_THRESHOLD_PNM_GNM means strong threshold for no-mask probe vector vs. no-mask gallery
// vector.
//

//======================================================================================
// we allow 3 confidence levels. This is used in our Matcher during authentication :
// each level means a different set of thresholds is used.
// This allow the user the flexibility to choose between 3 different FPR rates (Low, Medium, High).
// Currently all sets are the "High" confidence level thresholds.
// High :  1:1M
// Medium: 1:250K
// Low :   1:100K
//======================================================================================
#define RSID_IDENTICAL_THRESHOLD_GNM_GNM_HIGH_CONFIDENCE_LEVEL (2754)
#define RSID_IDENTICAL_THRESHOLD_GM_GNM_HIGH_CONFIDENCE_LEVEL  (2754)
//---
#define RSID_STRONG_THRESHOLD_PNM_GNM_HIGH_CONFIDENCE_LEVEL                (768)
#define RSID_STRONG_THRESHOLD_PM_GNM_HIGH_CONFIDENCE_LEVEL                 (1199)
#define RSID_STRONG_THRESHOLD_PM_GM_HIGH_CONFIDENCE_LEVEL                  (1679)
#define RSID_STRONG_THRESHOLD_PNM_GNM_RGB_IMG_ENROLL_HIGH_CONFIDENCE_LEVEL (768) // for enroll from image W10 vs. RGB features.
//---
#define RSID_UPDATE_THRESHOLD_PNM_GNM_HIGH_CONFIDENCE_LEVEL      (1745)
#define RSID_UPDATE_THRESHOLD_PM_GM_HIGH_CONFIDENCE_LEVEL        (1745)
#define RSID_UPDATE_THRESHOLD_PM_GNM_FIRST_HIGH_CONFIDENCE_LEVEL (1450)
//======================================================================================
#define RSID_IDENTICAL_THRESHOLD_GNM_GNM_MEDIUM_CONFIDENCE_LEVEL (2754)
#define RSID_IDENTICAL_THRESHOLD_GM_GNM_MEDIUM_CONFIDENCE_LEVEL  (2754)
//---
#define RSID_STRONG_THRESHOLD_PNM_GNM_MEDIUM_CONFIDENCE_LEVEL                (662)
#define RSID_STRONG_THRESHOLD_PM_GNM_MEDIUM_CONFIDENCE_LEVEL                 (1071)
#define RSID_STRONG_THRESHOLD_PM_GM_MEDIUM_CONFIDENCE_LEVEL                  (1485)
#define RSID_STRONG_THRESHOLD_PNM_GNM_RGB_IMG_ENROLL_MEDIUM_CONFIDENCE_LEVEL (662) // for enroll from image W10 vs. RGB features.
//---
#define RSID_UPDATE_THRESHOLD_PNM_GNM_MEDIUM_CONFIDENCE_LEVEL      (1745)
#define RSID_UPDATE_THRESHOLD_PM_GM_MEDIUM_CONFIDENCE_LEVEL        (1745)
#define RSID_UPDATE_THRESHOLD_PM_GNM_FIRST_MEDIUM_CONFIDENCE_LEVEL (1450)
//======================================================================================
#define RSID_IDENTICAL_THRESHOLD_GNM_GNM_LOW_CONFIDENCE_LEVEL (2754)
#define RSID_IDENTICAL_THRESHOLD_GM_GNM_LOW_CONFIDENCE_LEVEL  (2754)
//---
#define RSID_STRONG_THRESHOLD_PNM_GNM_LOW_CONFIDENCE_LEVEL                (593)
#define RSID_STRONG_THRESHOLD_PM_GNM_LOW_CONFIDENCE_LEVEL                 (300)
#define RSID_STRONG_THRESHOLD_PM_GM_LOW_CONFIDENCE_LEVEL                  (1316)
#define RSID_STRONG_THRESHOLD_PNM_GNM_RGB_IMG_ENROLL_LOW_CONFIDENCE_LEVEL (593) // for enroll from image W10 vs. RGB features.
//---
#define RSID_UPDATE_THRESHOLD_PNM_GNM_LOW_CONFIDENCE_LEVEL      (1745)
#define RSID_UPDATE_THRESHOLD_PM_GM_LOW_CONFIDENCE_LEVEL        (1745)
#define RSID_UPDATE_THRESHOLD_PM_GNM_FIRST_LOW_CONFIDENCE_LEVEL (1450)
//======================================================================================

// limit the number of iterations in the while() loop during the update process.
#define RSID_LIMIT_NUM_ITERS_NM (100)
#define RSID_LIMIT_NUM_ITERS_M  (400)

// Update gallery defines
#define RSID_UPDATE_GALLERY_HISTORY_WEIGHT (30)

// we work with integer-valued feature vectors (value range [-1023,+1023]).
#define RSID_MAX_FEATURE_VALUE (1023)
#define RSID_MIN_FEATURE_VALUE (-1023)

// disable matcher logs
#define RSID_MATCHER_DEBUG_LOGS (0)
//=============================================================================
