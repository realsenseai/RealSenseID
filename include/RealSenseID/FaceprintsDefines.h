#ifndef __RSID_FACEPRINTS_COMMON_DEFINES_H__
#define __RSID_FACEPRINTS_COMMON_DEFINES_H__

// This file contains common matcher and vector related defines, that are used on host and device sides.
// This file should be compiled in C and C++ compilers - so we can use it in both C and C++ clients.
// For this reason - only structs used here (no classes).
//

#ifdef __cplusplus
#include <string.h>
namespace RealSenseID
{
#endif // __cplusplus

#define RSID_FACEPRINTS_VERSION (9)

#define RSID_NUM_OF_RECOGNITION_FEATURES (512)
// 3 extra elements (1 for hasMask , 1 for norm, 1 spare).
#define RSID_FEATURES_VECTOR_ALLOC_SIZE        (515) // for DB element vector alloc size.
#define RSID_INDEX_IN_FEATURES_VECTOR_TO_FLAGS (512)

#define RSID_EXTRACTED_FEATURES_VECTOR_ALLOC_SIZE (515) // for Extracted element vector alloc size.

typedef short feature_t;

typedef enum FaceprintsType
{
    W10 = 0,
    RGB,
    NUMBER_OF_FACEPRINTS_TYPES
} FaceprintsTypeEnum;

typedef enum FaOperationFlags
{
    OpFlagError1 = 0,
    OpFlagAuthWithoutMask = 1,
    OpFlagAuthWithMask = 2,
    OpFlagEnrollWithoutMask = 3,
    OpFlagBenchmarksMode = 4,
    OpFlagError2 = 5,
    NumOpFlags
} FaOperationFlagsEnum;

typedef enum FaVectorFlags
{
    VecFlagNotSet = 0,
    VecFlagValidWithoutMask = 1,
    VecFlagInvalid = 2,
    VecFlagError1 = 3,
    VecFlagError2 = 4,
    NumVecFlags
} FaVectorFlagsEnum;

// extracted faceprints element
// a reduced structure that is used to represent the extracted faceprints been transferred from the device to the host
// through the packet layer.
#pragma pack(push, 1)
struct ExtractedFaceprintsElement
{
    int version;

    int featuresType;

    int flags;

    feature_t featuresVector[RSID_EXTRACTED_FEATURES_VECTOR_ALLOC_SIZE];

#ifdef __cplusplus
    ExtractedFaceprintsElement()
    {
        version = RSID_FACEPRINTS_VERSION;
        featuresType = 0;
        flags = 0;
        ::memset(featuresVector, 0, sizeof(featuresVector));
    }

    ExtractedFaceprintsElement(const ExtractedFaceprintsElement& other)
    {
        version = other.version;
        flags = other.flags;
        featuresType = other.featuresType;
        ::memcpy(featuresVector, other.featuresVector, sizeof(featuresVector));
    }

    ExtractedFaceprintsElement& operator=(const ExtractedFaceprintsElement& other)
    {
        if (this != &other)
        {
            version = other.version;
            flags = other.flags;
            featuresType = other.featuresType;
            ::memcpy(featuresVector, other.featuresVector, sizeof(featuresVector));
        }
        return *this;
    }
#endif
};
#pragma pack(pop)

// db layer faceprints element.
// a structure that is used in the DB layer, to save user faceprints and metadata to the DB.
// the struct includes several vectors and metadata to support all our internal matching mechanism (e.g.
// adaptive-learning).
struct DBFaceprintsElement
{
    int reserved[5]; // reserved placeholders (to minimize chance to re-create DB).

    int version;

    int featuresType;

    int flags; // flags - generic flags to indicate whatever we need.

    // enrollmentDescriptor - is the enrollment faceprints per user.
    // adaptiveDescriptorWithoutMask - is the ongoing faceprints per user with mask (we update it over time).
    feature_t adaptiveDescriptorWithoutMask[RSID_FEATURES_VECTOR_ALLOC_SIZE];
    feature_t enrollmentDescriptor[RSID_FEATURES_VECTOR_ALLOC_SIZE];

#ifdef __cplusplus
    DBFaceprintsElement()
    {
        version = (int)RSID_FACEPRINTS_VERSION;
        featuresType = (int)(FaceprintsTypeEnum::W10);
        flags = 0;
        ::memset(reserved, 0, sizeof(reserved));
        ::memset(adaptiveDescriptorWithoutMask, 0, sizeof(adaptiveDescriptorWithoutMask));
        ::memset(enrollmentDescriptor, 0, sizeof(enrollmentDescriptor));
    }
#endif
};

#ifdef __cplusplus
} // close namespace RealSenseID
#endif // __cplusplus

#endif // __RSID_FACEPRINTS_COMMON_DEFINES_H__
