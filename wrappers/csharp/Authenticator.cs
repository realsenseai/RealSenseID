// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
namespace rsid
{
    public static class FaceprintsConsts
    {
        public const int RSID_NUMBER_OF_RECOGNITION_FACEPRINTS = 512;

        // here we should use the same vector lengths as in RSID_FEATURES_VECTOR_ALLOC_SIZE.
        // 3 extra elements (1 for mask-detector hasMask , 1 for norm, 1 spare).
        public const int RSID_FEATURES_VECTOR_ALLOC_SIZE = 515; // DB element vector alloc size.
        public const int RSID_INDEX_IN_FEATURES_VECTOR_TO_FLAGS = 512;
        public const int RSID_EXTRACTED_FEATURES_VECTOR_ALLOC_SIZE = 515; // Extracted element vector alloc size.
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PairingArgs
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)]
        public byte[] HostPubkey;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)]
        public byte[] hostPubkeySignature;

        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 64)]
        public byte[] DevicePubkey;
    }

    public enum DeviceType
    {
        Unknown = 0,
        F45x = 1,
        F50x = 2,
    }


    // Enroll API struct
    public enum EnrollStatus
    {
        Success,
        NoFaceDetected,
        FaceDetected,
        PersonNotFound,
        PersonFound,
        BarcodeNotFound,
        BarcodeFound,
        LedFlowSuccess,
        FaceIsTooFarToTheTop,
        FaceIsTooFarToTheBottom,
        FaceIsTooFarToTheRight,
        FaceIsTooFarToTheLeft,
        FaceTiltIsTooUp,
        FaceTiltIsTooDown,
        FaceTiltIsTooRight,
        FaceTiltIsTooLeft,
        FaceIsNotFrontal,
        CameraStarted,
        CameraStopped,
        MultipleFacesDetected,
        Failure,
        DeviceError,
        Spoof,
        InvalidFeatures,
        AmbiguousFace,
        Sunglasses = 50,
        MedicalMask,
        FaceTooClose = 63,
        Serial_Ok = 100,
        Serial_Error,
        Serial_SerialError,
        Serial_SecurityError,
        Serial_VersionMismatch,
        Serial_CrcError,
        TooManySpoofs,
        NotSupported,
        DatabaseFull,
        DuplicateUserId,
        DuplicateFaceprints,
        Spoof_2D = 120,
        Spoof_3D,
        Spoof_LR,
        Spoof_Disparity,
        Spoof_Vision,
        Spoof_Surface,
        Spoof_Plane_Disparity,
        Spoof_2D_Right,
    }

    public enum FacePose
    {
        Center,
        Up,
        Down,
        Left,
        Right
    }

    // we allow 3 confidence levels. This is used in our Matcher during authentication :
    // each level means a different set of thresholds is used.
    // This allow the user the flexibility to choose between 3 different FPR rates (Low, Medium, High).
    // Currently all sets are the "High" confidence level thresholds.
    public enum MatcherConfidenceLevel
    {
        High = 0,   // high confidence level (default).
        Medium = 1, // medium
        Low = 2 // low.
    };


    // Frontal face policy
    public enum FrontalFacePolicy
    {
        None = 0, // No frontal face policy (default)
        Moderate = 1, // Allow some non-frontal orientations
        Strict = 2 // Strictly frontal face
    }
    // Face detection ROI
    public struct Roi
    {
        public short x;
        public short y;
        public short width;
        public short height;
    };

    //
    // Enroll callbacks
    //
    public delegate void EnrollResultCallback(EnrollStatus status, IntPtr ctx);
    public delegate void EnrollHintCallback(EnrollStatus status, float frameScore, IntPtr ctx);
    public delegate void EnrollProgressCallback(FacePose status, IntPtr ctx);
    public delegate void EnrollExtractionResultCallback(EnrollStatus status, IntPtr faceprintsHandle, IntPtr ctx);
    public delegate void EnrollFaceCroppedImageCallback(byte[] buffer, int width, int height, IntPtr ctx);

    [Serializable]
    [StructLayout(LayoutKind.Sequential)]


    // db layer faceprints element.
    // a structure that is used in the DB layer, to save user faceprints plus additional metadata to the DB.
    // the struct includes several vectors and metadata to support all our internal matching mechanism (e.g. adaptive-learning etc..).
    // (1) this structure will be used to represent faceprints in the DB (and therefore contains
    //     more vectors and info).
    // (2) this structure must be aligned with struct DBSecureVersionDescriptor_t (FaceprintsDefines.h) and Faceprints (Faceprints.h)!
    //     order and types matters (due to marshaling etc..).
    //
    public struct Faceprints
    {
        // reserved[5] placeholders (to minimize chance to re-create DB).
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 5)]
        public int[] reserved;

        // version (int)
        [MarshalAs(UnmanagedType.I4, SizeConst = 1)]
        public int version;

        // featureType (int)
        [MarshalAs(UnmanagedType.I4, SizeConst = 1)]
        public int featuresType;

        // flags - generic flags to indicate whatever we need.
        [MarshalAs(UnmanagedType.I4, SizeConst = 1)]
        public int flags;

        // here we should use the same vector lengths as in RSID_FEATURES_VECTOR_ALLOC_SIZE (512 for now, may increase to 513 in the future).
        // we have 3 vectors :
        //
        // adaptiveDescriptorWithoutMask - adaptive vector for user (without mask).
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = FaceprintsConsts.RSID_FEATURES_VECTOR_ALLOC_SIZE)]
        public short[] adaptiveDescriptorWithoutMask;

        // enrollmentDescriptor - for the enrollment vector (saved once).
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = FaceprintsConsts.RSID_FEATURES_VECTOR_ALLOC_SIZE)]
        public short[] enrollmentDescriptor;
    }

    // extracted faceprints element
    // a reduced structure that is used to represent the extracted faceprints been transferred from the device to the host
    // through the packet layer.
    // (1) this structure must be aligned with struct ExtractedFaceprintsElement (FaceprintsDefines.h) and ExtractedFaceprints (Faceprints.h)!
    //     order and types matters (due to marshaling etc..).
    //
    public struct ExtractedFaceprints
    {
        // version (int)
        [MarshalAs(UnmanagedType.I4, SizeConst = 1)]
        public int version;

        // featureType (int)
        [MarshalAs(UnmanagedType.I4, SizeConst = 1)]
        public int featuresType;

        // flags (int)
        [MarshalAs(UnmanagedType.I4, SizeConst = 1)]
        public int flags;

        // featuresVector - for the matched features vector.
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = FaceprintsConsts.RSID_EXTRACTED_FEATURES_VECTOR_ALLOC_SIZE)]
        public short[] featuresVector;
    }

    // match element used during authentication flow, where we match between faceprints object received from the device
    // to user objects read from the DB.
    // (1) this structure must be aligned with struct MatchElement in (Faceprints.h)!
    public struct MatchElement
    {
        // version (int)
        [MarshalAs(UnmanagedType.I4, SizeConst = 1)]
        public int version;

        // featureType (int)
        [MarshalAs(UnmanagedType.I4, SizeConst = 1)]
        public int featuresType;

        // flags (int)
        [MarshalAs(UnmanagedType.I4, SizeConst = 1)]
        public int flags;

        // featuresVector - for the matched features vector.
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = FaceprintsConsts.RSID_EXTRACTED_FEATURES_VECTOR_ALLOC_SIZE)]
        public short[] featuresVector;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct EnrollExtractArgs
    {
        public EnrollExtractionResultCallback resultClbk;
        public EnrollProgressCallback progressClbk;
        public EnrollHintCallback hintClbk;
        public FaceDetectedCallback faceDetectedClbk;
        public LandmarksDetectedCallback landmarksDetectedClbk;
        public FaceCroppedImageCallback faceCroppedImageClbk;
        public IntPtr ctx;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct EnrollArgs
    {
        public string userId;
        public EnrollResultCallback resultClbk;
        public EnrollProgressCallback progressClbk;
        public EnrollHintCallback hintClbk;
        public FaceDetectedCallback faceDetectedClbk;
        public LandmarksDetectedCallback landmarksDetectedClbk;
        public FaceCroppedImageCallback faceCroppedImageClbk;
        public IntPtr ctx;
    }

    [Serializable]
    [StructLayout(LayoutKind.Sequential)]
    public struct MatchArgs
    {
        public rsid.MatchElement newFaceprints;
        public rsid.Faceprints existingFaceprints;
        public rsid.Faceprints updatedFaceprints;
        public rsid.MatcherConfidenceLevel matcherConfidenceLevel;
    }

    //[Serializable]
    [StructLayout(LayoutKind.Sequential)]
    public struct UserFaceprints
    {
        public string userID;
        public rsid.Faceprints faceprints;
    }

    //
    // Auth config
    //

    [StructLayout(LayoutKind.Sequential)]
    public struct DeviceConfig
    {
        public const int MaxRois = 5; // must match RSID_MAX_ROIS / DeviceConfig::MAX_ROIS

        public enum CameraRotation
        {
            Rotation_0_Deg = 0, // default
            Rotation_180_Deg = 1,
            Rotation_90_Deg = 2,
            Rotation_270_Deg = 3
        };

        public enum SecurityLevel
        {
            High = 0,   // high security, no mask support, all AS algo(s) will be activated.
            Medium = 1, // default mode, supports masks. Projector AS wont be activated.
            Low = 2     // low security level, only main AS algo will be activated.
        };

        public enum AlgoFlow
        {
            All = 0,                  // default
            FaceDetectionOnly = 1,    // face detection only
            SpoofOnly = 2,            // spoof only
            RecognitionOnly = 3      // recognition only            
        };

        public enum FaceSelectionPolicy
        {
            Single = 0, // default, run authentication on closest face
            All = 1     // run authentication on all (up to 5) detected faces
        };

        public enum DumpMode
        {
            None,
            CroppedFace,
            FullFrame,
            DebugDump,
        };

        public enum PersonMotionMode
        {
            Static = 0, // default
            Walkthrough = 1
        };

        public enum DistanceLimit
        {
            NoLimit = 0, // default, no distance limit
            Short = 1, // 70cm
            Mid = 2, // 100cm
            Long = 3 // 130cm
        };

        public CameraRotation cameraRotation;
        public SecurityLevel securityLevel;
        public AlgoFlow algoFlow;
        public FaceSelectionPolicy faceSelectionPolicy;
        public DumpMode dumpMode;
        public MatcherConfidenceLevel matcherConfidenceLevel;
        public byte maxSpoofs;
        public int GpioAuthToggling;
        public FrontalFacePolicy frontalFacePolicy;
        public PersonMotionMode personMotionMode;
        public short matchThresh;
        public short sensorExpTime;
        public short sensorGain;
        public byte rectEnable;
        public byte landmarksEnable;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = MaxRois)]
        public Roi[] detection_rois;
        public byte num_rois;
        public DistanceLimit distanceLimit;
        public byte distanceEnabled;

        public SerializableDeviceConfig ToSerialized()
        {
            return new SerializableDeviceConfig
            {
                CameraRotation = cameraRotation.ToString(),
                SecurityLevel = securityLevel.ToString(),
                AlgoFlow = algoFlow.ToString(),
                FaceSelectionPolicy = faceSelectionPolicy.ToString(),
                DumpMode = dumpMode.ToString(),
                MatcherConfidenceLevel = matcherConfidenceLevel.ToString(),
                maxSpoofs = maxSpoofs,
                GpioAuthToggling = GpioAuthToggling,
                FrontalFacePolicy = frontalFacePolicy.ToString(),
                PersonMotionMode = personMotionMode.ToString(),
                DistanceLimit = distanceLimit.ToString(),
                MatchThresh = matchThresh,
                SensorExpTime = sensorExpTime,
                SensorGain = sensorGain,
                RectEnable = rectEnable,
                LandmarksEnable = landmarksEnable,
                Detection_Rois = detection_rois,
                Num_Rois = num_rois,
                DistanceEnabled = distanceEnabled
            };
        }
    }

    /// Serializable version of DeviceConfig for JSON export
    /// NOTE:
    /// Enum values are converted to strings because raw integer values are not
    /// human-readable in JSON and can be unclear during debugging, logging, or manual inspection.
    public struct SerializableDeviceConfig
    {
        public string CameraRotation;
        public string SecurityLevel;
        public string AlgoFlow;
        public string FaceSelectionPolicy;
        public string DumpMode;
        public string MatcherConfidenceLevel;
        public byte maxSpoofs;
        public int GpioAuthToggling;
        public string FrontalFacePolicy;
        public string PersonMotionMode;
        public string DistanceLimit;
        public short MatchThresh;
        public short SensorExpTime;
        public short SensorGain;
        public byte RectEnable;
        public byte LandmarksEnable;
        public Roi[] Detection_Rois;
        public byte Num_Rois;
        public byte DistanceEnabled;
    }

    //
    // Authenticate API struct
    //
    public enum AuthStatus
    {
        Success,
        NoFaceDetected,
        FaceDetected,
        PersonNotFound,
        PersonFound,
        BarcodeNotFound,
        BarcodeFound,
        LedFlowSuccess,
        FaceIsTooFarToTheTop,
        FaceIsTooFarToTheBottom,
        FaceIsTooFarToTheRight,
        FaceIsTooFarToTheLeft,
        FaceTiltIsTooUp,
        FaceTiltIsTooDown,
        FaceTiltIsTooRight,
        FaceTiltIsTooLeft,
        FaceIsNotFrontal,
        CameraStarted,
        CameraStopped,
        Spoof,
        Forbidden,
        DeviceError,
        Failure,
        TooManySpoofs,
        InvalidFeatures,
        AmbiguousFace,
        Sunglasses = 50,
        MedicalMask,
        FaceTooFar = 61,
        CalcDistanceFailure = 62,
        FaceTooClose = 63,
        Serial_Ok = 100,
        Serial_Error,
        Serial_SerialError,
        Serial_SecurityError,
        Serial_VersionMismatch,
        Serial_CrcError,
        Spoof_2D = 120,
        Spoof_3D,
        Spoof_LR,
        Spoof_Disparity,
        Spoof_Vision,
        Spoof_Surface,
        Spoof_Plane_Disparity,
        Spoof_2D_Right,
    }

    [StructLayout(LayoutKind.Sequential)]

    public struct MatchResult
    {
        [MarshalAs(UnmanagedType.I4, SizeConst = 1)]
        public int success;
        [MarshalAs(UnmanagedType.I4, SizeConst = 1)]
        public int shouldUpdate;
        [MarshalAs(UnmanagedType.I4, SizeConst = 1)]
        public int score;
    }

    public delegate void AuthResultCallback(AuthStatus status, string userId, short score, IntPtr ctx);
    public delegate void AuthHintCallback(AuthStatus status, float frameScore, IntPtr ctx);
    public delegate void FaceDetectedCallback(IntPtr faces, int count, uint ts, IntPtr ctx);
    public delegate void LandmarksDetectedCallback(IntPtr landmarks, int count, uint ts, IntPtr ctx);
    public delegate void FaceDistancesCallback(IntPtr distances, int count, uint ts, IntPtr ctx);
    public delegate void PersonDetectedCallback(IntPtr persons, int count, uint ts, IntPtr ctx);
    public delegate void PoseDetectedCallback(IntPtr poses, int count, uint ts, IntPtr ctx);
    public delegate void BarcodeDecodedCallback(IntPtr barcodes, int count, uint ts, IntPtr ctx);
    public delegate void FaceCroppedImageCallback(IntPtr buffer, int width, int height, uint ts, IntPtr ctx);
    public delegate void AuthExtractionResultCallback(AuthStatus status, IntPtr faceprints, IntPtr ctx);

    // Detection loop callbacks - return false to stop the loop, true to continue
    public delegate bool PersonDetectionCallback(IntPtr persons, int count, uint ts, AuthStatus status, IntPtr ctx);
    public delegate bool PoseDetectionCallback(IntPtr poses, int count, uint ts, AuthStatus status, IntPtr ctx);
    public delegate bool BarcodeDetectionCallback(IntPtr barcodes, int count, uint ts, AuthStatus status, IntPtr ctx);
    public delegate bool BodyPartDetectionCallback(IntPtr bodyParts, int count, uint ts, AuthStatus status, IntPtr ctx);

    [StructLayout(LayoutKind.Sequential)]
    public struct AuthArgs
    {
        public AuthResultCallback resultClbk;
        public AuthHintCallback hintClbk;
        public FaceDetectedCallback faceDetectedClbk;
        public LandmarksDetectedCallback landmarksDetectedClbk;        
        public FaceDistancesCallback faceDistancesClbk;
        public FaceCroppedImageCallback faceCroppedImageClbk;
        public IntPtr ctx;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct AuthExtractArgs
    {
        public AuthExtractionResultCallback resultClbk;
        public AuthHintCallback hintClbk;
        public FaceDetectedCallback faceDetectedClbk;
        public LandmarksDetectedCallback landmarksDetectedClbk;
        public FaceDistancesCallback faceDistancesClbk;
        public FaceCroppedImageCallback faceCroppedImageClbk;
        public IntPtr faceprints;
        public IntPtr ctx;
    }

    public class Authenticator : IDisposable
    {
        public const int MaxUserIdSize = 30;
#if RSID_SECURE
        public Authenticator(SignatureCallback signatureCallback, DeviceType deviceType = DeviceType.F45x)
        {
            switch (deviceType)
            {
                case DeviceType.F45x:
                    _handle = rsid_create_authenticator_F45x(ref signatureCallback);
                    break;
                case DeviceType.F50x:
                    throw new ArgumentException("Secure mode not supported in F50x");
                default:
                    throw new ArgumentException("Invalid device type");
            }

            if (_handle == IntPtr.Zero)
            {
                throw new Exception("Failed creating face authenticator");
            }
        }
#else
        public Authenticator(DeviceType deviceType = DeviceType.F45x)
        {
            switch (deviceType)
            {
                case DeviceType.F45x:
                    _handle = rsid_create_authenticator_F45x();
                    break;
                case DeviceType.F50x:
                    _handle = rsid_create_authenticator_F50x();
                    break;
                default:
                    throw new ArgumentException("Invalid device type");
            }

            if (_handle == IntPtr.Zero)
            {
                throw new Exception("Failed creating face authenticator");
            }
        }
#endif


        ~Authenticator()
        {
            Dispose(false);
        }

        public Status Connect(SerialConfig config)
        {
            return rsid_connect(_handle, ref config);
        }

        public void Disconnect()
        {
            rsid_disconnect(_handle);
        }

        public Status Pair(ref PairingArgs args)
        {
            return rsid_pair(_handle, ref args);
        }

        public Status Unpair()
        {
            return rsid_unpair(_handle);
        }

        public Status SetDeviceConfig(DeviceConfig args)
        {
            return rsid_set_device_config(_handle, ref args);
        }

        public Status QueryDeviceConfig(out DeviceConfig result)
        {
            result = new DeviceConfig();
            result.detection_rois = new Roi[DeviceConfig.MaxRois];
            Status status = rsid_query_device_config(_handle, ref result);
            return status;
        }

        public void Dispose()
        {
            Dispose(true);
            // prevent finalization code for this object
            // from executing a second time.
            GC.SuppressFinalize(this);
        }

        public Status Enroll(EnrollArgs args)
        {
            _enrollArgs = args; // store to prevent the delegates to be garbage collected
            return rsid_enroll(_handle, ref args);
        }

        public EnrollStatus EnrollImage(string userId, byte[] buffer, int width, int height)
        {
            var pinnedArray = GCHandle.Alloc(buffer, GCHandleType.Pinned);
            try
            {
                var pointer = pinnedArray.AddrOfPinnedObject();
                return rsid_enroll_image(_handle, userId, pointer, width, height);
            }
            finally { pinnedArray.Free(); }
        }

        public EnrollStatus EnrollImageFeatureExtraction(string userId, byte[] buffer, int width, int height, ref Faceprints userFaceprints)
        {
            var pinnedArray = GCHandle.Alloc(buffer, GCHandleType.Pinned);
            try
            {
                var pointer = pinnedArray.AddrOfPinnedObject();
                return rsid_extract_faceprints_from_image(_handle, userId, pointer, width, height, ref userFaceprints);

            }
            finally { pinnedArray.Free(); }
        }

        public Status Authenticate(AuthArgs args)
        {
            _authArgs = args;
            return rsid_authenticate(_handle, ref args);
        }

        public Status AuthenticateLoop(AuthArgs args)
        {
            _authArgs = args;
            return rsid_authenticate_loop(_handle, ref args);
        }

        public Status DetectPersons(PersonDetectionCallback callback, bool loop = false)
        {
            return rsid_detect_persons(_handle, callback, loop ? 1 : 0, IntPtr.Zero);
        }

        public Status DetectPoses(PoseDetectionCallback callback, bool loop = false)
        {
            return rsid_detect_poses(_handle, callback, loop ? 1 : 0, IntPtr.Zero);
        }

        public Status DecodeBarcodes(BarcodeDetectionCallback callback, bool loop = false)
        {
            return rsid_decode_barcodes(_handle, callback, loop ? 1 : 0, IntPtr.Zero);
        }

        public Status DetectBodyParts(BodyPartDetectionCallback callback, bool loop = false)
        {
            return rsid_detect_body_parts(_handle, callback, loop ? 1 : 0, IntPtr.Zero);
        }

        public static string Version()
        {
            return Marshal.PtrToStringAnsi(rsid_version());
        }

        public static string CompatibleFirmwareVersion(DeviceType deviceType)
        {
            return Marshal.PtrToStringAnsi(rsid_compatible_firmware_version((int)deviceType));
        }

        public static bool IsFwCompatibleWithHost(DeviceType deviceType, string fw_version)
        {

            return rsid_is_fw_compatible_with_host((int)deviceType, fw_version) != 0;
        }

        public Status Cancel()
        {
            return rsid_cancel(_handle);
        }

        public Status RemoveAllUsers()
        {
            return rsid_remove_all_users(_handle);
        }

        public Status RemoveUser(string userId)
        {
            return rsid_remove_user(_handle, userId);
        }


        public Status QueryNumberOfUsers(out int numberOfUsers)
        {
            return rsid_query_number_of_users(_handle, out numberOfUsers);
        }

        public Status QueryUserIds(out string[] userIds)
        {

            int userCount;
            userIds = new string[0];

            // Query user count first
            var status = QueryNumberOfUsers(out userCount);
            if (status != rsid.Status.Ok || userCount == 0)
            {
                return status;
            }

            // Allocate buffer to hold the results (31 bytes for each user)
            var chunkSize = MaxUserIdSize + 1;
            var buf = new byte[chunkSize * userCount];
            status = rsid_query_user_ids_to_buf(_handle, buf, ref userCount);

            if (status != rsid.Status.Ok || userCount <= 0)
            {
                return status;
            }

            // translate to string array.
            userIds = new string[userCount];
            for (var i = 0; i < userCount; i++)
            {
                userIds[i] = Encoding.ASCII.GetString(buf, i * chunkSize, chunkSize).TrimEnd('\0'); ;
            }
            return status;
        }

        // Send device to standby
        public Status Standby()
        {
            return rsid_standby(_handle);
        }

        public Status Hibernate()
        {
            return rsid_hibernate(_handle);
        }

        // Send de/* Unlock previously locked device due to too many spoof attempts*/
        public Status Unlock()
        {
            return rsid_unlock(_handle);
        }

#if RSID_ONE2ONE
        public EnrollStatus EnrollImageOneToOne(string userId, byte[] buffer, int width, int height)
        {
            var pinnedArray = GCHandle.Alloc(buffer, GCHandleType.Pinned);
            try
            {
                var pointer = pinnedArray.AddrOfPinnedObject();
                return rsid_enroll_image_one_to_one(_handle, userId, pointer, width, height);
            }
            finally { pinnedArray.Free(); }
        }

        public Status AuthenticateOneToOne(AuthArgs args)
        {
            _authArgs = args;
            return rsid_authenticate_one_to_one(_handle, ref args);
        }

        public AuthStatus AuthenticateImageOneToOne(byte[] buffer, int width, int height, ref string userId, ref short score)
        {
            var pinnedArray = GCHandle.Alloc(buffer, GCHandleType.Pinned);
            try
            {
                var pointer = pinnedArray.AddrOfPinnedObject();
                var outputUserId = new byte[MaxUserIdSize];
                var status = rsid_authenticate_image_one_to_one(_handle, pointer, width, height, outputUserId, ref score);
                userId = Encoding.ASCII.GetString(outputUserId).TrimEnd('\0');
                return status;
            }
            finally { pinnedArray.Free(); }
        }

        // extract_image_faceprints using host pipeline (no device involved)
        // buffer must be bgr 24 bits image
        public Status ExtractFaceprintsOnHost(byte[] buffer, int width, int height, ref ExtractedFaceprints faceprints)
        {
            var pinnedArray = GCHandle.Alloc(buffer, GCHandleType.Pinned);
            try
            {
                var pointer = pinnedArray.AddrOfPinnedObject();
                return rsid_extract_faceprints_on_host(_handle, pointer, width, height, ref faceprints);
            }
            finally { pinnedArray.Free(); }
        }

        // Find face in given image using host pipeline (no device involved)
        public Status DetectFace(byte[] buffer, int width, int height, ref FaceRect faceRect)
        {
            var pinnedArray = GCHandle.Alloc(buffer, GCHandleType.Pinned);
            try
            {
                var pointer = pinnedArray.AddrOfPinnedObject();
                return rsid_detect_face(_handle, pointer, width, height, ref faceRect);
            }
            finally { pinnedArray.Free(); }
        }

        // Find face in given image using host pipeline (no device involved).        
        // Use inside the OnPreview callback.
        public Status DetectFace(PreviewImage image, ref FaceRect faceRect)
        {
            return rsid_detect_face(_handle, image.buffer, image.width, image.height, ref faceRect);
        }
#endif // ONE2ONE

        protected virtual void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                rsid_destroy_authenticator(_handle);
                _handle = IntPtr.Zero;
                _disposed = true;
            }
        }

        public Status EnrollExtractFaceprints(EnrollExtractArgs args)
        {
            _enrollExtractArgs = args; // store to prevent the delegates to be garbage collected
            return rsid_extract_faceprints_for_enroll(_handle, ref args);
        }

        public Status AuthenticateExtractFaceprints(AuthExtractArgs args)
        {
            _authExtractArgs = args;
            return rsid_extract_faceprints_for_auth(_handle, ref args);
        }

        public Status AuthenticateLoopExtractFaceprints(AuthExtractArgs args)
        {
            _authExtractArgs = args;
            return rsid_extract_faceprints_for_auth_loop(_handle, ref args);
        }

        public MatchResult MatchFaceprintsToFaceprints(ref MatchArgs args)
        {
            _matchArgs = args;

            MatchResult result = rsid_match_faceprints(_handle, ref args);

            return result;
        }

        // Helper to get FaceRect from IntPtr to faces array passed in the callbacks
        public static FaceRect[] MarshalFaces(IntPtr facesArr, int faceCount)
        {
            // Marshal the IntPtr to array of FaceRect
            var faces = new rsid.FaceRect[faceCount];
            for (int i = 0; i < faces.Length; i++)
            {
                faces[i] = (rsid.FaceRect)Marshal.PtrToStructure(facesArr, typeof(rsid.FaceRect));
                facesArr += Marshal.SizeOf(typeof(rsid.FaceRect));
            }
            return faces;
        }

        // Helper to get FaceRect from IntPtr to faces array passed in the callbacks
        public static FaceLandmarks[] MarshalLandamrks(IntPtr landmarksArr, int faceCount)
        {
            // Marshal the IntPtr to array of FaceLandmarks
            var landmarks = new rsid.FaceLandmarks[faceCount];
            for (int i = 0; i < landmarks.Length; i++)
            {
                landmarks[i] = (rsid.FaceLandmarks)Marshal.PtrToStructure(landmarksArr, typeof(rsid.FaceLandmarks));
                landmarksArr += Marshal.SizeOf(typeof(rsid.FaceLandmarks));
            }
            return landmarks;
        }

        // Helper to get PersonRect from IntPtr to persons array passed in the callbacks
        public static PersonRect[] MarshalPersons(IntPtr personsArr, int personCount)
        {
            // Marshal the IntPtr to array of PersonRect
            var persons = new rsid.PersonRect[personCount];
            for (int i = 0; i < persons.Length; i++)
            {
                persons[i] = (rsid.PersonRect)Marshal.PtrToStructure(personsArr, typeof(rsid.PersonRect));
                personsArr += Marshal.SizeOf(typeof(rsid.PersonRect));
            }
            return persons;
        }

        // Helper to get PersonPose structs from IntPtr to poses array passed in the callbacks
        public static PersonPose[] MarshalPoses(IntPtr posesArr, int count)
        {
            // Marshal the IntPtr to array of Pose
            var poses = new rsid.PersonPose[count];
            for (int i = 0; i < poses.Length; i++)
            {
                poses[i] = (rsid.PersonPose)Marshal.PtrToStructure(posesArr, typeof(rsid.PersonPose));
                posesArr += Marshal.SizeOf(typeof(rsid.PersonPose));
            }
            return poses;
        }


        public static string[] MarshalBarcodes(IntPtr barcodesArr, int barcodeCount)
        {
            // Marshal the IntPtr to array of strings (barcodes)
            var barcodes = new string[barcodeCount];
            for (int i = 0; i < barcodes.Length; i++)
            {
                barcodes[i] = Marshal.PtrToStringAnsi(Marshal.ReadIntPtr(barcodesArr, i * IntPtr.Size));
            }
            return barcodes;
        }

        public List<UserFaceprints> GetUsersFaceprints()
        {
            int number_of_users = 0;
            var status = QueryNumberOfUsers(out number_of_users);
            if (status != Status.Ok)
                return null;
            var exported_db = new rsid.Faceprints[number_of_users];
            String[] user_ids = new String[number_of_users];
            status = QueryUserIds(out user_ids);
            if (status != Status.Ok)
                return null;
            for (int i = 0; i < number_of_users; i++)
                exported_db[i] = new Faceprints();
            status = rsid_get_users_faceprints(_handle, exported_db);
            var user_features = new List<UserFaceprints>();

            for (uint i = 0; i < number_of_users; i++)
            {
                user_features.Add(new UserFaceprints
                {
                    faceprints = exported_db[i],
                    userID = user_ids[i]
                });
                if (status != Status.Ok)
                    return null;
            }
            return user_features;
        }

        public Status DumpAndMount()
        {
            return rsid_dump_and_mount(_handle);
        }

        public Status SetUsersFaceprints(List<rsid.UserFaceprints> user_features)
        {
            return rsid_set_users_faceprints(_handle, user_features.ToArray(), user_features.Count);
        }

        private IntPtr _handle;
        private bool _disposed = false;
        private EnrollArgs _enrollArgs;
        private AuthArgs _authArgs;
        private EnrollExtractArgs _enrollExtractArgs;
        private AuthExtractArgs _authExtractArgs;
        private MatchArgs _matchArgs;


#if RSID_SECURE
        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_create_authenticator(ref SignatureCallback signatureCallback);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_create_authenticator_F45x(ref SignatureCallback signatureCallback);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_create_authenticator_F50x(ref SignatureCallback signatureCallback);
#else
        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_create_authenticator();

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_create_authenticator_F45x();

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_create_authenticator_F50x();
#endif


        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern void rsid_destroy_authenticator(IntPtr rsid_authenticator);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_connect(IntPtr rsid_authenticator, ref SerialConfig serialConfig);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_set_device_config(IntPtr rsid_authenticator, ref DeviceConfig deviceConfig);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_query_device_config(IntPtr rsid_authenticator, ref DeviceConfig deviceConfig);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern void rsid_disconnect(IntPtr rsid_authenticator);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_pair(IntPtr rsid_device_controller, ref PairingArgs pairingArgs);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_unpair(IntPtr rsid_device_controller);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_enroll(IntPtr rsid_authenticator, ref EnrollArgs enrollArgs);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern EnrollStatus rsid_enroll_image(IntPtr rsid_authenticator, string userId, IntPtr buffer, int width, int height);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern EnrollStatus rsid_extract_faceprints_from_image(IntPtr rsid_authenticator, string userId, IntPtr buffer, int width, int height, ref Faceprints faceprints);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_authenticate(IntPtr rsid_authenticator, ref AuthArgs authArgs);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_authenticate_loop(IntPtr rsid_authenticator, ref AuthArgs authArgs);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_detect_persons(IntPtr rsid_authenticator, PersonDetectionCallback callback, int loop, IntPtr ctx);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_detect_poses(IntPtr rsid_authenticator, PoseDetectionCallback callback, int loop, IntPtr ctx);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_decode_barcodes(IntPtr rsid_authenticator, BarcodeDetectionCallback callback, int loop, IntPtr ctx);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_detect_body_parts(IntPtr rsid_authenticator, BodyPartDetectionCallback callback, int loop, IntPtr ctx);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_cancel(IntPtr rsid_authenticator);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_remove_all_users(IntPtr rsid_authenticator);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_remove_user(IntPtr rsid_authenticator, string userId);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_version();

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_compatible_firmware_version(int deviceType);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern int rsid_is_fw_compatible_with_host(int deviceType, string fw_version);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_query_number_of_users(IntPtr rsid_authenticator, out int result);


        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_query_user_ids(
            IntPtr rsid_authenticator,
            [In, Out, MarshalAs(UnmanagedType.LPArray, ArraySubType = UnmanagedType.LPStr)] ref StringBuilder[] users,
            [In, Out] ref int result);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_query_user_ids_to_buf(IntPtr rsid_device_controller, [Out, MarshalAs(UnmanagedType.LPArray)] byte[] output, ref int n_users);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_standby(IntPtr rsid_authenticator);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_hibernate(IntPtr rsid_authenticator);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_unlock(IntPtr rsid_authenticator);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_extract_faceprints_for_enroll(IntPtr rsid_authenticator, ref EnrollExtractArgs enrollExtractArgs);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_extract_faceprints_for_auth(IntPtr rsid_authenticator, ref AuthExtractArgs authExtractArgs);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_extract_faceprints_for_auth_loop(IntPtr rsid_authenticator, ref AuthExtractArgs authArgs);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern MatchResult rsid_match_faceprints(IntPtr rsid_authenticator, ref MatchArgs matchArgs);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_get_users_faceprints(IntPtr rsid_authenticator, [Out, MarshalAs(UnmanagedType.LPArray)] rsid.Faceprints[] user_features);


        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_dump_and_mount(IntPtr rsid_authenticator);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_set_users_faceprints(IntPtr rsid_authenticator, rsid.UserFaceprints[] user_features, int n_users);

#if RSID_ONE2ONE
        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern EnrollStatus rsid_enroll_image_one_to_one(IntPtr rsid_authenticator, string userId, IntPtr buffer, int width, int height);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_authenticate_one_to_one(IntPtr rsid_authenticator, ref AuthArgs authArgs);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern AuthStatus rsid_authenticate_image_one_to_one(IntPtr rsid_authenticator, IntPtr buffer, int width, int height, byte[] userId, ref short score);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_extract_faceprints_on_host(IntPtr rsid_authenticator, IntPtr buffer, int width, int height, ref ExtractedFaceprints faceprints);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_detect_face(IntPtr rsid_authenticator, IntPtr buffer, int width, int height, ref FaceRect face);
#endif
    }
}
