---
name: rsid-sdk
description: >
  RealSenseID face authentication SDK reference. Use when writing code for face
  enrollment, authentication, host-mode faceprint extraction/matching, device
  configuration, user management, device discovery, camera preview, logging,
  or serial connection. Covers C++ (RealSenseID namespace), Python (rsid_py),
  and C# (rsid).
---

# RealSenseID SDK Reference

## SDK Overview

- **Two operating modes:**
  - **Device mode** — enroll/authenticate with on-device database and matcher
  - **Host mode** — device extracts faceprints only; host owns the database and calls `MatchFaceprints()`
- **Device families:** F45x (F450/F455) and F50x (F500). Selected at construction or auto-detected at connect time.
- **Entry-point classes:**
  - `FaceAuthenticator` — enroll, authenticate, host-mode faceprint extraction, matching
  - `DeviceController` — connect, reboot, query firmware version, serial number, ping, diagnostics
- **Connection model:** Serial port. Always `Connect()` before any operation; destructor auto-disconnects (RAII).
- **Max user ID:** 30 characters (31 bytes with null terminator).
- **Namespace/module:** C++ `RealSenseID`, Python `rsid_py`, C# `rsid`.
- **Samples:** See `samples/` for complete working examples (C, C++, Python, C#).

## Common AI Pitfalls

Read these before generating any RealSenseID code — these are patterns AI models frequently get wrong.

1. **Don't mix device mode and host mode.**
   Device mode uses `Enroll`, `Authenticate`, `RemoveUser` (device DB + device matcher).
   Host mode uses `ExtractFaceprints*` + `MatchFaceprints` (host DB + host matcher).
   Never combine them in the same flow.

2. **Lifecycle: Connect → operate → destroy.**
   Always `Connect()` first. Cancel any running loop (`AuthenticateLoop`, detection loops) before shutdown. C++ auto-disconnects via RAII; Python uses context managers; C# uses `IDisposable`.

3. **Always use `DiscoverDevices()` for connection.**
   Never hardcode serial ports like `COM9` or `/dev/ttyACM0`. Use the discovery API to find connected devices and their types.

4. **Adaptive updates in host mode are mandatory.**
   If `MatchFaceprints` returns `should_update == true`, persist the `updated` faceprints. Do not overwrite DB entries unconditionally.

5. **F50x-only APIs.**
   `DetectPersons`, `DetectPoses`, `DetectBodyParts`, `DecodeBarcodes` are F50x-only. On F45x expect `Status::NotSupported`.

6. **Don't memcpy faceprint internals unsafely.**
   Treat faceprint struct layouts as SDK-defined and versioned. Only copy between documented fields.

8. **Serial port name on Windows (COM10+).**
   High COM ports may require `\\.\COM10` prefix. If `Connect()` fails with serial error, try this prefix.

9. **Preview requires `RSID_PREVIEW=ON`.**
   If preview fails, confirm build flag, platform support, and correct camera index.

10. **On `VersionMismatch`, check compatibility — don't retry.**
    Call `IsFwCompatibleWithHost(...)` to diagnose. Don't loop.

11. **Linux serial permissions.**
    Ensure access to `/dev/ttyACM*` via udev rule or dialout group membership.

## Device Discovery

Always enumerate devices before connecting — never hardcode ports.

```cpp
#include "RealSenseID/DiscoverDevices.h"
auto devices = RealSenseID::DiscoverDevices();
for (auto& dev : devices) {
    // dev.serialPort, dev.deviceType, dev.serialNumber, dev.cameraNumber
}
auto type = RealSenseID::DiscoverDeviceType("/dev/ttyACM0");
auto captures = RealSenseID::DiscoverCapture();  // returns vector<int>
```
```python
import rsid_py
devices = rsid_py.discover_devices()
for dev in devices:
    # dev.serial_port, dev.device_type, dev.serial_number, dev.camera_number
```
```csharp
var devices = rsid.Discover.DiscoverDevices();
foreach (var dev in devices) {
    // dev.SerialPort, dev.DeviceType, dev.SerialNumber, dev.CameraNumber
}
```

`DeviceInfo` fields: serial port path, `DeviceType` (F45x/F50x/Unknown), serial number string, camera number (0-based or -1).

## Connection & Lifecycle

```cpp
{
    auto devices = RealSenseID::DiscoverDevices();
    if (devices.empty()) { /* handle no device */ return; }
    auto& dev = devices.front();
    RealSenseID::FaceAuthenticator authenticator(dev.deviceType);
    auto status = authenticator.Connect({dev.serialPort});
    if (status != RealSenseID::Status::Ok) { /* handle error */ }
    // ... use authenticator ...
}   // auto-disconnects on destruction
```
```python
devices = rsid_py.discover_devices()
if not devices:
    raise RuntimeError("No device found")
dev = devices[0]
with rsid_py.FaceAuthenticator(dev.device_type, dev.serial_port) as f:
    f.authenticate(on_result=on_result)
```
```csharp
var devices = rsid.Discover.DiscoverDevices();
if (devices.Length == 0) throw new Exception("No device found");
var dev = devices[0];
using var auth = new rsid.Authenticator();
var status = auth.Connect(new rsid.SerialConfig { port = dev.SerialPort });
```

## Device Mode — Enrollment & Authentication

### Enrollment

```cpp
class MyEnrollClbk : public RealSenseID::EnrollmentCallback {
public:
    void OnResult(const RealSenseID::EnrollStatus status) override { /* handle */ }
    void OnProgress(const RealSenseID::FacePose pose) override { /* handle */ }
    void OnHint(const RealSenseID::EnrollStatus hint, float frameScore) override { /* handle */ }
};
MyEnrollClbk clbk;
auto status = authenticator.Enroll(clbk, "john");
// Optional overrides: OnFaceDetected, OnLandmarksDetected, OnFaceCroppedImage
```
```python
f.enroll(user_id="john", on_result=on_result, on_progress=on_progress, on_hint=on_hint)
```
```csharp
auth.Enroll(new rsid.EnrollArgs {
    userId = "john", resultClbk = (s, ctx) => {}, progressClbk = (p, ctx) => {}, hintClbk = (h, sc, ctx) => {}
});
```

**Image enrollment** (BGR24, exactly one face in image, face width and height each >= 144 px): `EnrollImage("john", buf, w, h)` / `enroll_image(...)` / `auth.EnrollImage(...)`.

### Authentication

```cpp
class MyAuthClbk : public RealSenseID::AuthenticationCallback {
public:
    void OnResult(RealSenseID::AuthenticateStatus status, const char* userId, short score) override {
        if (status == RealSenseID::AuthenticateStatus::Success)
            std::cout << "Auth: " << userId << std::endl;
    }
    void OnHint(RealSenseID::AuthenticateStatus hint, float frameScore) override {}
};
MyAuthClbk clbk;
authenticator.Authenticate(clbk);       // single attempt
authenticator.AuthenticateLoop(clbk);   // continuous until Cancel()
authenticator.Cancel();
// Optional overrides: OnFaceDetected, OnLandmarksDetected, OnFaceDistances,
//   OnPersonDetected, OnPoseDetected, OnBarcodeDecoded, OnFaceCroppedImage
```
```python
def on_result(status, user_id):
    if status == rsid_py.AuthenticateStatus.Success:
        print('Auth:', user_id)
f.authenticate(on_result=on_result, on_hint=on_hint)
f.authenticate_loop(on_result=on_result, on_hint=on_hint)
f.cancel()
```
```csharp
var args = new rsid.AuthArgs {
    resultClbk = (status, userId, score, ctx) => { /* handle */ },
    hintClbk = (hint, score, ctx) => { /* handle */ }
};
auth.Authenticate(args);
auth.AuthenticateLoop(args);
auth.Cancel();
```

C# marshalling helpers: `Authenticator.MarshalFaces()`, `MarshalPersons()`, `MarshalPoses()`, `MarshalBarcodes()`.

## Host Mode

Device extracts faceprints, host owns the database and performs matching.
Pattern: **extract → match → adaptive update**.

### C++ (full pattern)

```cpp
// Step 1: Enroll — extract faceprints and store in DB
class EnrollExtractClbk : public RealSenseID::EnrollFaceprintsExtractionCallback {
public:
    RealSenseID::Faceprints db_entry;
    void OnResult(const RealSenseID::EnrollStatus status,
                  const RealSenseID::ExtractedFaceprints* fp) override {
        if (status != RealSenseID::EnrollStatus::Success) return;
        db_entry.data.version = fp->data.version;
        db_entry.data.flags = fp->data.flags;
        db_entry.data.featuresType = fp->data.featuresType;
        ::memcpy(db_entry.data.adaptiveDescriptorWithoutMask,
                 fp->data.featuresVector, sizeof(fp->data.featuresVector));
        ::memcpy(db_entry.data.enrollmentDescriptor,
                 fp->data.featuresVector, sizeof(fp->data.featuresVector));
    }
    void OnProgress(const RealSenseID::FacePose) override {}
    void OnHint(const RealSenseID::EnrollStatus, float) override {}
};
EnrollExtractClbk enroll_clbk;
authenticator.ExtractFaceprintsForEnroll(enroll_clbk);

// Step 2: Auth — extract faceprints from probe face
class AuthExtractClbk : public RealSenseID::AuthFaceprintsExtractionCallback {
public:
    RealSenseID::ExtractedFaceprints extracted;
    void OnResult(const RealSenseID::AuthenticateStatus status,
                  const RealSenseID::ExtractedFaceprints* fp) override {
        if (status == RealSenseID::AuthenticateStatus::Success) extracted = *fp;
    }
    void OnHint(const RealSenseID::AuthenticateStatus, float) override {}
};
AuthExtractClbk auth_clbk;
authenticator.ExtractFaceprintsForAuth(auth_clbk);
// Or continuous: authenticator.ExtractFaceprintsForAuthLoop(auth_clbk);

// Step 3: Match + adaptive update
RealSenseID::MatchElement scanned;
scanned.data.version = auth_clbk.extracted.data.version;
scanned.data.featuresType = auth_clbk.extracted.data.featuresType;
scanned.data.flags = RealSenseID::FaOperationFlagsEnum::OpFlagAuthWithoutMask;
::memcpy(scanned.data.featuresVector, auth_clbk.extracted.data.featuresVector,
         sizeof(auth_clbk.extracted.data.featuresVector));
RealSenseID::Faceprints updated;
auto result = authenticator.MatchFaceprints(scanned, enroll_clbk.db_entry, updated);
if (result.success && result.should_update)
    enroll_clbk.db_entry = updated;  // persist adaptive update
```

### Python

```python
def on_enroll(status, extracted):
    if status == rsid_py.EnrollStatus.Success:
        db = rsid_py.Faceprints()
        db.version, db.features_type, db.flags = extracted.version, extracted.features_type, extracted.flags
        db.adaptive_descriptor_nomask = extracted.features
        db.enroll_descriptor = extracted.features
        faceprints_db.append(db)
f.extract_faceprints_for_enroll(on_result=on_enroll, on_progress=lambda p: None)

def on_auth(status, new_prints):
    if status != rsid_py.AuthenticateStatus.Success: return
    for db_item in faceprints_db:
        updated = rsid_py.Faceprints()
        result = f.match_faceprints(new_prints, db_item, updated)
        if result.success:
            if result.should_update: db_item = updated  # adaptive update
            break
f.extract_faceprints_for_auth(on_result=on_auth)
```

### C#

```csharp
auth.EnrollExtractFaceprints(new rsid.EnrollExtractArgs {
    resultClbk = (status, fpHandle, ctx) => { /* marshal and store in DB */ },
    progressClbk = (pose, ctx) => { }, hintClbk = (hint, score, ctx) => { }
});
auth.AuthExtractFaceprints(new rsid.AuthExtractArgs {
    resultClbk = (status, fpHandle, ctx) => { /* marshal, match, adaptive update */ },
    hintClbk = (hint, score, ctx) => { }
});
```

**Key types:** `ExtractedFaceprints` (raw from device), `Faceprints` (DB element: adaptive + enrollment descriptors), `MatchElement` (input to match), `MatchResultHost`/`MatchResult` (`success`, `should_update`, `score`).

**Adaptive update:** When `should_update` is true, persist the updated faceprints. This improves recognition over time.

## User Management

```cpp
unsigned int count;
authenticator.QueryNumberOfUsers(count);
char* user_ids[100];
for (auto& ptr : user_ids) ptr = new char[31];
unsigned int num = 100;
authenticator.QueryUserIds(user_ids, num);
authenticator.RemoveUser("john");
authenticator.RemoveAll();
```
```python
users = f.query_user_ids()       # returns list[str]
f.remove_user("john")
f.remove_all_users()
```
```csharp
auth.QueryUserIds(out string[] userIds);
auth.RemoveUser("john");
auth.RemoveAllUsers();
```

## Device Configuration

### DeviceConfig fields

| Field | Type | Default |
|-------|------|---------|
| `camera_rotation` | `CameraRotation` | `Rotation_0_Deg` |
| `security_level` | `SecurityLevel` | `Low` |
| `algo_flow` | `AlgoFlow` | `FaceDetectionOnly` |
| `face_selection_policy` | `FaceSelectionPolicy` | `Single` |
| `dump_mode` | `DumpMode` | `None` |
| `frontal_face_policy` | `FrontalFacePolicy` | `None` |
| `person_motion_mode` | `PersonMotionMode` | `Static` |
| `distance_limit_cm` | `uint8` | `0` (no limit, 1-150 = cm) |
| `max_spoofs` | `uint8` | `0` (disabled) |
| `match_thresh` | `uint16` | `0` (device recommended) |
| `gpio_auth_toggling` | `int` | `0` (disabled) |
| `manual_exposure_time_us` | `uint16` | `0` (auto) |
| `manual_gain` | `uint16` | `0` (auto) |
| `rect_enable` | `uint8` | `1` (enabled) |
| `landmarks_enable` | `uint8` | `0` (disabled) |
| `detection_roi` | `Roi` | `{0, 0, 1080, 1920}` |

### Enum values

- **CameraRotation:** `Rotation_0_Deg`, `Rotation_180_Deg`, `Rotation_90_Deg`, `Rotation_270_Deg`
- **SecurityLevel:** `High`, `Medium`, `Low`
- **AlgoFlow:** `All`, `FaceDetectionOnly`, `SpoofOnly`, `RecognitionOnly`
- **FaceSelectionPolicy:** `Single` (closest), `All` (up to 5)
- **DumpMode:** `None`, `CroppedFace`, `FullFrame`. Python: `DumpMode.Disable` (not `None`)
- **FrontalFacePolicy:** `None`, `Moderate`, `Strict`
- **PersonMotionMode:** `Static`, `Walkthrough`
- **distance_limit_cm:** `0` = no limit, `1`-`150` = max distance in centimeters

**API:** `QueryDeviceConfig(config)` / `SetDeviceConfig(config)`. Python: `query_device_config()` / `set_device_config()`. C#: same names with `out` parameter.

## Status Codes

**`Status`:** `Ok`, `Error`, `SerialError`, `SecurityError`, `VersionMismatch`, `CrcError`, `TooManySpoofs`, `NotSupported`, `DatabaseFull`, `DuplicateUserId`, `DuplicateFaceprints`, `InvalidSettings`

**`EnrollStatus`:** Success = `Success`. **`AuthenticateStatus`:** Success = `Success`.

All status enums have `Description()` (C++) and stream operator overload. Python enums print directly.

## Advanced Detection (F50x only)

F50x-only APIs — returns `Status::NotSupported` on F45x. All accept a `loop` parameter; callback returns `bool` (`true` = continue). Use `Cancel()` to stop.

- `DetectPersons` — bounding boxes with `id`, `distance`, `body_part`
- `DetectPoses` — bounding boxes + 17 body keypoints
- `DetectBodyParts` — body part detection
- `DecodeBarcodes` — barcode reading

## DeviceController

| Method | Description |
|--------|-------------|
| `Connect` / `Disconnect` | Serial connection (RAII auto-disconnects) |
| `QueryFirmwareVersion` | FW version string |
| `QuerySerialNumber` | Device serial number |
| `Ping` | Connectivity check |
| `Reboot` | Restart device |
| `GetTemperature` | SoC + board temps |
| `FetchLog` | Device log (~128KB, ~12-14s) |
| `Get/SetColorGains` | Camera color gains (0-511) |
| `QueryBspVer` | BSP version string |

```cpp
RealSenseID::DeviceController ctrl;
ctrl.Connect(RealSenseID::SerialConfig{dev.serialPort});
std::string fw_ver;
ctrl.QueryFirmwareVersion(fw_ver);
ctrl.Reboot();  // auto-disconnects on destruction
```
```python
with rsid_py.DeviceController(port) as ctrl:
    print(ctrl.query_firmware_version())
    ctrl.reboot()
```

**Power management** (via `FaceAuthenticator`): `Standby()` (low-power, auto-wakes), `Hibernate()` (deep sleep, GPIO 10 or power reset to wake), `Unlock()` (after TooManySpoofs lock).

**Version:** `RealSenseID::Version()`, `CompatibleFirmwareVersion(deviceType)`, `IsFwCompatibleWithHost(deviceType, fw_ver)`.

## Camera Preview (`RSID_PREVIEW=ON`)

```cpp
RealSenseID::PreviewConfig config;
config.deviceType = RealSenseID::DeviceType::F45x;
config.cameraNumber = -1;  // auto-detect
config.previewMode = RealSenseID::PreviewMode::MJPEG_1080P;  // or MJPEG_720P, RAW10_1080P
config.portraitMode = true;

RealSenseID::Preview preview(config);
MyPreviewClbk clbk;  // implement PreviewImageReadyCallback::OnPreviewImageReady(const Image&)
preview.StartPreview(clbk);
// Image fields: buffer (RGB), width, height, stride, size, metadata (timestamp, exposure, gain)
preview.StopPreview();
```

## Logging

```cpp
RealSenseID::SetLogCallback([](RealSenseID::LogLevel level, const char* msg) {
    printf("[%d] %s\n", (int)level, msg);
}, RealSenseID::LogLevel::Debug, /*do_formatting=*/true);
```
```python
rsid_py.set_log_callback(callback, log_level, do_formatting=True)
```

**LogLevel:** `Trace`, `Debug`, `Info`, `Warning`, `Error`, `Critical`, `Off`
