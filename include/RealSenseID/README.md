# RealSenseID C++ API Reference

This document covers the public C++ API for the RealSenseID SDK. All classes are in the `RealSenseID` namespace.

For build instructions, platform support, and general usage, see the [main README](../../Readme.md).

For code examples, see the [samples](../../samples) directory. For additional tools, see the [tools](../../tools) directory.

## Table of Contents

1. [FaceAuthenticator](#faceauthenticator)
   - [Connect / Disconnect](#connect--disconnect)
   - [Enrollment](#enrollment)
   - [EnrollImage](#enrollimage)
   - [Authenticate](#authenticate)
   - [AuthenticateLoop](#authenticateloop)
   - [Device Configuration](#device-configuration)
   - [QueryNumberOfUsers](#querynumberofusers)
   - [QueryUserIds](#queryuserids)
   - [RemoveUser](#removeuser)
   - [RemoveAll](#removeall)
   - [Cancel](#cancel)
   - [Unlock](#unlock)
   - [Host Mode Methods](#host-mode-methods)
2. [DeviceController](#devicecontroller)
3. [Preview API](#preview-api)
4. [FwUpdater](#fwupdater)

---

## FaceAuthenticator

The main class for face authentication operations. All operations are synchronous; a new operation should not be started until the previous one has completed.

### Connect / Disconnect

Connects the host to the device over USB or UART.

```cpp
RealSenseID::FaceAuthenticator authenticator;
Status connect_status = authenticator.Connect({RealSenseID::SerialType::USB, "COM9"});
authenticator.Disconnect();
```

### Enrollment

Starts the device, runs the neural network algorithm, and stores encrypted faceprints on secured flash on the RealSense™ ID device. Stored faceprints are matched against enrolled users during authentication.

For best performance, enroll under normal lighting conditions and look directly at the device.
During the enrollment process, the device sends status _hints_ to the callback. The full list of hints can be found in [EnrollStatus.h](EnrollStatus.h).

```cpp
class MyEnrollClbk : public RealSenseID::EnrollmentCallback
{
public:
    void OnResult(const RealSenseID::EnrollStatus status) override
    {
        std::cout << "on_result: status: " << status << std::endl;
    }

    void OnProgress(const RealSenseID::FacePose pose) override
    {
        std::cout << "on_progress: pose: " << pose << std::endl;
    }

    void OnHint(const RealSenseID::EnrollStatus hint) override
    {
        std::cout << "on_hint: hint: " << hint << std::endl;
    }
};

const char* user_id = "John";
MyEnrollClbk enroll_clbk;
Status status = authenticator.Enroll(enroll_clbk, user_id);
```

### EnrollImage

Enrolls a user from a BGR24 image buffer instead of using the camera. The face should occupy at least 75% of the image area.

```cpp
const char* user_id = "John";
unsigned char* buffer = ...; // bgr24 image buffer
unsigned int width = 640, height = 480;
EnrollStatus status = authenticator.EnrollImage(user_id, buffer, width, height);
```

### Authenticate

Single authentication attempt: starts the device, runs the neural network algorithm, generates faceprints and compares them to all enrolled faceprints in the database.
Returns whether authentication was allowed or denied, along with the matched user ID. During the process, the device sends status _hints_ to the callback. The full list of hints can be found in [AuthenticationStatus.h](AuthenticateStatus.h).

This operation can be further configured via [Device Configuration](#device-configuration).

```cpp
class MyAuthClbk : public RealSenseID::AuthenticationCallback
{
public:
    // Called when authentication result is available.
    // If there are multiple faces it will be called for each face detected.
    void OnResult(const RealSenseID::AuthenticateStatus status, const char* user_id) override
    {
        if (status == RealSenseID::AuthenticateStatus::Success)
        {
            std::cout << "******* Authenticate success.  user_id: " << user_id << " *******" << std::endl;
        }
        else
        {
            std::cout << "on_result: status: " << status << std::endl;
        }
    }

    void OnHint(const RealSenseID::AuthenticateStatus hint) override
    {
        std::cout << "on_hint: hint: " << hint << std::endl;
    }
};

MyAuthClbk auth_clbk;
Status status = authenticator.Authenticate(auth_clbk);
```

### AuthenticateLoop

Starts the device and runs authentication in a loop until `Cancel()` is called. Each iteration:

- Runs the neural network algorithm pipeline
- Extracts facial features
- Matches them against all previously enrolled users in the database
- Reports the result via callback: whether authentication was allowed or denied, and the matched user ID

```cpp
class MyAuthClbk : public RealSenseID::AuthenticationCallback
{
public:
    // Called when result is available for a detected face.
    // If the status==AuthenticateStatus::Success then the user_id will point to c string of the authenticated user id.
    void OnResult(const RealSenseID::AuthenticateStatus status, const char* user_id) override
    {
        if (status == RealSenseID::AuthenticateStatus::Success)
        {
            std::cout << "******* Authenticate success.  user_id: " << user_id << " *******" << std::endl;
        }
        else
        {
            std::cout << "on_result: status: " << status << std::endl;
        }
    }

    void OnHint(const RealSenseID::AuthenticateStatus hint) override
    {
        std::cout << "on_hint: hint: " << hint << std::endl;
    }

    // Called when faces are detected. Can be single or multiple faces.
    // The faces vector contains coord X(,y,h,w) of faces detected.
    // Coords are in full HD resolution (1920x1080):
    //   x,y: top left face rect
    //   w,h: width/height of the face rect
    //   Note: The `X` coords are flipped — X==0 is most right, and X==1920 is most left.
    // The timestamp argument is the timestamp in millis of the frame that the faces were found in.
    void OnFaceDetected(const std::vector<RealSenseID::FaceRect>& faces, const unsigned int timestamp) override
    {
        _faces = faces;
    }
};

MyAuthClbk auth_clbk;
Status status = authenticator.AuthenticateLoop(auth_clbk);
```

### Device Configuration

The device operation can be configured by passing the [DeviceConfig](DeviceConfig.h) struct to `FaceAuthenticator::SetDeviceConfig(const DeviceConfig&)`.

If `SetDeviceConfig()` is never called, the device will use the default values described below.

```cpp
struct RSID_API DeviceConfig
{
    /**
     * @enum CameraRotation
     * @brief Camera rotation.
     */
    enum class CameraRotation
    {
        Rotation_0_Deg = 0, // default
        Rotation_180_Deg = 1,
        Rotation_90_Deg = 2,
        Rotation_270_Deg = 3
    };

    /**
     * @enum SecurityLevel
     * @brief Security level to allow (default is Low).
     */
    enum class SecurityLevel
    {
        High = 0,   // high security level
        Medium = 1, // medium security level
        Low = 2,    // low security level
    };

    /**
     * @enum AlgoFlow
     * @brief Algorithms used during authentication.
     */
    enum class AlgoFlow
    {
        All = 0,               // recognition, spoof and face detection
        FaceDetectionOnly = 1, // face detection only (default)
        SpoofOnly = 2,         // spoof only
        RecognitionOnly = 3,   // recognition only
    };

    /**
     * @enum FaceSelectionPolicy
     * @brief Controls whether to run authentication on all (up to 5) detected faces or only the single closest face.
     */
    enum class FaceSelectionPolicy
    {
        Single = 0, // default, run authentication on closest face
        All = 1     // run authentication on all (up to 5) detected faces
    };

    enum class DumpMode
    {
        None = 0,        // default
        CroppedFace = 1, // sends snapshot of the detected face (as jpg)
        FullFrame = 2,   // sends left+right raw frames with metadata
    };

    /**
     * @brief Defines three confidence levels used by the Matcher during authentication.
     *
     * Each confidence level corresponds to a different set of thresholds, providing the user with the flexibility to
     * choose between three different False Positive Rates (FPR): Low, Medium, and High. Currently, all sets use the
     * thresholds associated with the "Low" confidence level by default.
     */
    enum class MatcherConfidenceLevel
    {
        High = 0,
        Medium = 1,
        Low = 2 // default
    };

    /**
     * @brief Defines the policy for frontal face orientation.
     *
     * - None: No restriction on face angle (default).
     * - Moderate: Allow some deviation from a forward-facing orientation.
     * - Strict: The face must be directly oriented toward the camera.
     */
    enum class FrontalFacePolicy
    {
        None = 0, // default
        Moderate = 1,
        Strict = 2
    };

    /**
     * @enum PersonMotionMode
     * @brief Describes the expected movement behavior of a person within the camera frame.
     *
     * - Static: The person remains mostly stationary within the frame.
     * - Walkthrough: The person moves through the frame, such as walking past the camera.
     */
    enum class PersonMotionMode
    {
        Static = 0, // default
        Walkthrough = 1
    };

    /**
     * @enum DistanceLimit
     * @brief Controls the maximum distance for face authentication.
     */
    enum class DistanceLimit
    {
        NoLimit = 0, // default, no distance limit
        Short = 1,   // 70cm
        Mid = 2,     // 100cm
        Long = 3     // 130cm
    };

    /**
     * @struct Roi
     * @brief Describes a region of interest (ROI) within the camera frame.
     */
    struct Roi
    {
        unsigned short x = 0;
        unsigned short y = 0;
        unsigned short width = 1080;
        unsigned short height = 1920;
    };

    CameraRotation camera_rotation = CameraRotation::Rotation_0_Deg;
    SecurityLevel security_level = SecurityLevel::Low;
    AlgoFlow algo_flow = AlgoFlow::FaceDetectionOnly;
    FaceSelectionPolicy face_selection_policy = FaceSelectionPolicy::Single;
    DumpMode dump_mode = DumpMode::None;
    MatcherConfidenceLevel matcher_confidence_level = MatcherConfidenceLevel::Low;
    FrontalFacePolicy frontal_face_policy = FrontalFacePolicy::None;
    PersonMotionMode person_motion_mode = PersonMotionMode::Static;
    DistanceLimit distance_limit = DistanceLimit::NoLimit;

    /**
     * @brief Specifies the maximum number of consecutive spoofing attempts allowed before the device rejects further
     * authentication requests.
     *
     * Setting this value to 0 disables the check, which is the default behavior. If the number of consecutive spoofing
     * attempts reaches max_spoofs, the device will reject any subsequent authentication requests. To reset this
     * behavior and allow further authentication attempts, the device must be unlocked using the Unlock() API call.
     */
    unsigned char max_spoofs = 0;

    /**
     * @brief Specifies the feature matching threshold.
     *
     * Setting this value to 0 will use the device-recommended threshold.
     */
    unsigned short match_thresh = 0;

    /**
     * @brief Controls whether GPIO toggling is enabled (1) or disabled (0, default) after successful authentication.
     *
     * Set this value to 1 to enable toggling of GPIO pin #1 after each successful authentication.
     * Set this value to 0 to disable GPIO toggling (default).
     *
     * @note Only GPIO pin #1 can be toggled. Other values are not supported.
     */
    int gpio_auth_toggling = 0;

    unsigned short manual_exposure_time_us = 0; // 0 means auto exposure
    unsigned short manual_gain = 0;             // 0 means auto gain
    unsigned char rect_enable = 0x01;           // enable face rectangle retrieval via OnFaceDetected callback
    unsigned char landmarks_enable = 0;         // enable face landmarks retrieval via OnLandmarksDetected callback

    static constexpr unsigned int MAX_ROIS = 5;
    Roi detection_rois[MAX_ROIS];               // up to MAX_ROIS regions; only detection_rois[0..num_rois-1] are active
    unsigned char num_rois = 1;

    bool distance_enabled = false;
};
```

Notes:

- `CameraRotation` enables the algorithm to work with a rotated device. For preview rotation to match, set `PreviewConfig::portraitMode` accordingly (see [Preview API](#preview-api)).
  - `Rotation_0_Deg` and `Rotation_180_Deg` → `portraitMode == true` (default)
  - `Rotation_90_Deg` and `Rotation_270_Deg` → `portraitMode == false`

Example — configure spoof-only detection:

```cpp
using namespace RealSenseID;
DeviceConfig device_config;
device_config.algo_flow = AlgoFlow::SpoofOnly;
auto status = authenticator->SetDeviceConfig(device_config);
```

### QueryNumberOfUsers

Returns the number of users currently enrolled in the device database.

```cpp
unsigned int num_users = 0;
Status status = authenticator.QueryNumberOfUsers(num_users);
```

### QueryUserIds

Returns the IDs of all enrolled users. Allocate the array using the count from `QueryNumberOfUsers()`.

```cpp
unsigned int num_users = 0;
authenticator.QueryNumberOfUsers(num_users);

char** user_ids = new char*[num_users];
for (unsigned int i = 0; i < num_users; i++)
    user_ids[i] = new char[RealSenseID::MAX_USERID_LENGTH];

Status status = authenticator.QueryUserIds(user_ids, num_users);
```

### RemoveUser

Removes a specific user from the device database.

```cpp
const char* user_id = "John";
Status status = authenticator.RemoveUser(user_id);
```

### RemoveAll

Removes all enrolled users from the device database.

```cpp
Status status = authenticator.RemoveAll();
```

### Cancel

Stops the current operation (Enrollment/Authenticate/AuthenticateLoop).

```cpp
Status status = authenticator.Cancel();
```

### Unlock

Unlocks the device after it has been locked due to too many consecutive spoofing attempts (see `max_spoofs` in [Device Configuration](#device-configuration)).

```cpp
Status status = authenticator.Unlock();
```

### Host Mode Methods

In Host Mode, the device acts as a feature-extraction unit. Faceprints are sent to the host for matching against a host-managed database.

#### ExtractFaceprintsForAuth

Extracts faceprints from a face using the authentication flow (eliminates spoof attempts) and sends them to the host.

```cpp
class FaceprintsAuthClbk : public RealSenseID::AuthFaceprintsExtractionCallback
{
public:
    void OnResult(const RealSenseID::AuthenticateStatus status, const Faceprints* faceprints) override
    {
        std::cout << "result: " << status << std::endl;
        // if status was success, pass faceprints to your database matcher
    }

    void OnHint(const RealSenseID::AuthenticateStatus hint) override
    {
        std::cout << "hint: " << hint << std::endl;
    }
};

FaceprintsAuthClbk clbk;
auto status = authenticator->ExtractFaceprintsForAuth(clbk);
```

#### ExtractFaceprintsForEnroll

Extracts faceprints using the enrollment flow (verifies face pose) and sends them to the host.

```cpp
class MyEnrollServerClbk : public RealSenseID::EnrollmentCallback
{
    const char* _user_id = nullptr;

public:
    explicit MyEnrollServerClbk(const char* user_id) : _user_id {user_id} {}

    void OnResult(const RealSenseID::EnrollStatus status, const Faceprints* faceprints) override
    {
        std::cout << "result: " << status << " for user: " << _user_id << std::endl;
        // if status was success, store faceprints in your database
    }

    void OnProgress(const RealSenseID::FacePose pose) override
    {
        std::cout << "pose: " << pose << std::endl;
    }

    void OnHint(const RealSenseID::EnrollStatus hint) override
    {
        std::cout << "hint: " << hint << std::endl;
    }
};

MyEnrollServerClbk enroll_clbk {user_id.c_str()};
auto status = authenticator->ExtractFaceprintsForEnroll(enroll_clbk);
```

#### ExtractFaceprintsForAuthLoop

Extracts faceprints in a loop using the authentication flow. Each iteration extracts from a single face and sends it to the host.

```cpp
class AuthLoopExtrClbk : public RealSenseID::AuthFaceprintsExtractionCallback
{
public:
    void OnResult(const RealSenseID::AuthenticateStatus status, const Faceprints* faceprints) override
    {
        std::cout << "result: " << status << std::endl;
        // if status was success, pass faceprints to your database matcher
    }

    void OnHint(const RealSenseID::AuthenticateStatus hint) override
    {
        std::cout << "hint: " << hint << std::endl;
    }
};

AuthLoopExtrClbk clbk;
auto status = authenticator->ExtractFaceprintsForAuthLoop(clbk);
```

#### MatchFaceprints

Matches two faceprints and returns a prediction of whether they belong to the same person.

```cpp
for (auto& db_item : db)
{
    auto match_result = authenticator->MatchFaceprints(scanned_faceprints, db_item.faceprints, updated_faceprints, ThresholdsConfidenceLevel_High);
    if (match_result.success)
    {
        std::cout << "Match succeeded with user id: " << db_item.id << std::endl;
        break;
    }
}
```

---

## DeviceController

Provides device management operations independent of face authentication.

### Connect / Disconnect

```cpp
RealSenseID::DeviceController deviceController;
Status connect_status = deviceController.Connect({"COM9"});
deviceController.Disconnect();
```

### Reboot

Resets and reboots the device.

```cpp
Status status = deviceController.Reboot();
```

### QueryFirmwareVersion

Retrieves the firmware version string from the device.

```cpp
std::string version;
Status status = deviceController.QueryFirmwareVersion(version);
```

### Ping

Sends a ping packet to verify the device is responsive.

```cpp
Status status = deviceController.Ping();
```

---

## Preview API

Provides live image frames from the device camera. Available formats: 704x1280 or 1056x1920 RGB.

### Preview Configuration

```cpp
/**
 * Preview modes
 */
enum class PreviewMode
{
    MJPEG_1080P = 0, // default
    MJPEG_720P = 1,
    RAW10_1080P = 2,
};

/**
 * Preview configuration
 */
struct RSID_API PreviewConfig
{
    DeviceType deviceType = DeviceType::F45x;           // device type
    int cameraNumber = -1;                              // attempt to auto detect by default
    PreviewMode previewMode = PreviewMode::MJPEG_1080P; // RAW10 requires custom fw support
    bool portraitMode = true; // portrait or landscape output; algorithm rotation is set separately via DeviceConfig::CameraRotation
    bool rotateRaw = false;   // enables rotation of raw data when portraitMode == true
    bool skip_decode = false; // set to true to skip decoding MJPEG frames to RGB (to get MJPEG frames directly)
};
```

### Sensor Timestamps

Sensor timestamps (in milliseconds) can be accessed via `image.metadata.timestamp` in `OnPreviewImageReady`. Other metadata fields are not valid.

To enable timestamps on Windows, turn on the Metadata option when using the SDK installer. The installer creates a dedicated registry entry for each unique RealSenseID device.
For Linux, metadata is supported on kernel 4.16 and above only.

More information about metadata on Windows can be found in [Microsoft UVC documentation](https://docs.microsoft.com/en-us/windows-hardware/drivers/stream/uvc-extensions-1-5#2211-still-image-capture--method-2).

### StartPreview

The callback is invoked for each newly arrived image.

```cpp
class PreviewRender : public RealSenseID::PreviewImageReadyCallback
{
public:
    void OnPreviewImageReady(const Image& image) override
    {
        // Provides RGB preview image (for RAW10_1080P PreviewMode: raw converted to RGB).
    }

    void OnSnapshotImageReady(const Image& image) override // optional
    {
        // Provides images triggered by DeviceConfig::DumpMode (cropped face or full frame).
    }
};

PreviewConfig previewConfig;
PreviewRender image_clbk;
Preview preview(previewConfig);
bool success = preview.StartPreview(image_clbk);
```

### PausePreview / ResumePreview / StopPreview

```cpp
preview.PausePreview();
preview.ResumePreview();
preview.StopPreview();
```

---

## FwUpdater

Handles firmware update operations for RealSense™ ID devices.

Always call `CheckCompatibility()` before `UpdateModules()` to verify that the firmware binary is compatible with the connected device.

```cpp
RealSenseID::FwUpdater updater(RealSenseID::DeviceType::F45x);
RealSenseID::FwUpdater::Settings settings;
settings.serial_config = {"COM4"};

// Check compatibility before updating
RealSenseID::FwUpdater::FwCompatibilityInfo info;
if (!updater.CheckCompatibility(settings, "path/to/fw.bin", info))
{
    // inspect info fields to determine what is incompatible
    return;
}

// Perform the update
struct MyEventHandler : public RealSenseID::FwUpdater::EventHandler
{
    void OnProgress(float progress) override
    {
        std::cout << "Update progress: " << (int)(progress * 100) << "%" << std::endl;
    }
};

MyEventHandler handler;
Status status = updater.UpdateModules(&handler, settings, "path/to/fw.bin");
```

You can also inspect the firmware binary before updating:

```cpp
std::string fwVersion, recognitionVersion;
std::vector<std::string> moduleNames;
updater.ExtractFwInformation("path/to/fw.bin", fwVersion, recognitionVersion, moduleNames);
```
