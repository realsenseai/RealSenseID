# Embedded C SDK — API Coverage Status

Compared against C++ `FaceAuthenticator.h`, `DeviceController.h`, and callback headers.

```
+----+-------------------------------------+------------+------------------------------------------+
| #  | Function (C++ API)                  | Embedded C | Notes                                    |
+----+-------------------------------------+------------+------------------------------------------+
|    | CORE AUTHENTICATION                 |            |                                          |
+----+-------------------------------------+------------+------------------------------------------+
| 1  | Connect / Disconnect                | N/A        | User owns transport via rsid_platform_t |
| 2  | Enroll                              | Yes        |                                          |
| 3  | Authenticate                        | Yes        |                                          |
| 4  | AuthenticateLoop                    | Yes        |                                          |
| 5  | Cancel                              | Yes        | Async-safe (ISR/thread)                  |
+----+-------------------------------------+------------+------------------------------------------+
|    | CALLBACKS                           |            |                                          |
+----+-------------------------------------+------------+------------------------------------------+
| 6  | OnResult (auth)                     | Yes        | user_id + score                          |
| 7  | OnResult (enroll)                   | Yes        |                                          |
| 8  | OnHint (auth/enroll)                | Yes        | Includes frame_score (0.0-1.0)           |
| 9  | OnProgress (enroll)                 | Yes        | Face pose requests                       |
| 10 | OnFaceDetected                      | Yes        | Face rectangles + timestamp              |
| 11 | OnFaceCroppedImage                  | No         | Planned — two-phase callback design      |
| 12 | OnLandmarksDetected                 | Yes        | 5 x/y landmark points per face           |
| 13 | OnFaceDistances                     | Yes        | Per-face distance (double)               |
+----+-------------------------------------+------------+------------------------------------------+
|    | USER MANAGEMENT                     |            |                                          |
+----+-------------------------------------+------------+------------------------------------------+
| 14 | RemoveUser                          | Yes        |                                          |
| 15 | RemoveAll                           | Yes        |                                          |
| 16 | QueryUserIds                        | Yes        | Chunked, 50 per request                  |
| 17 | QueryNumberOfUsers                  | Yes        |                                          |
+----+-------------------------------------+------------+------------------------------------------+
|    | DEVICE CONFIG & CONTROL             |            |                                          |
+----+-------------------------------------+------------+------------------------------------------+
| 18 | SetDeviceConfig                     | Yes        | With echo verification                   |
| 19 | QueryDeviceConfig                   | Yes        |                                          |
| 20 | Standby                             | Yes        |                                          |
| 21 | Hibernate                           | Yes        |                                          |
| 22 | Unlock                              | Yes        |                                          |
+----+-------------------------------------+------------+------------------------------------------+
|    | DEVICE CONTROLLER                   |            |                                          |
+----+-------------------------------------+------------+------------------------------------------+
| 23 | Ping                                | Yes        |                                          |
| 24 | QueryFirmwareVersion                | Yes        | Parsed MODULE:ver|... format             |
| 25 | QueryBspVersion                     | Yes        | Raw bspver text                          |
| 26 | QuerySerialNumber                   | Yes        |                                          |
| 27 | QueryOtpVersion                     | Yes        |                                          |
| 28 | GetTemperature                      | Yes        | F50x only                                |
| 29 | GetColorGains                       | Yes        | F450 only                                |
| 30 | SetColorGains                       | Yes        | F450 only                                |
| 31 | Reboot                              | Yes        |                                          |
| 32 | FetchLog                            | No         | 128KB transfer, ~12-14s                  |
+----+-------------------------------------+------------+------------------------------------------+
|    | ONE-TO-ONE MODE                     |            |                                          |
+----+-------------------------------------+------------+------------------------------------------+
| 33 | AuthenticateOneToOne                | No         | Easy to add (different MsgId only)       |
| 34 | EnrollImageOneToOne                 | No         | Requires sending BGR24 image to device   |
| 35 | AuthenticateImageOneToOne           | No         | Requires sending BGR24 image to device   |
+----+-------------------------------------+------------+------------------------------------------+
|    | HOST-SIDE FACEPRINTS (SERVER MODE)  |            |                                          |
+----+-------------------------------------+------------+------------------------------------------+
| 36 | ExtractFaceprintsForEnroll          | No         | Device extracts, host stores/matches     |
| 37 | ExtractFaceprintsForAuth            | No         | Device extracts, host stores/matches     |
| 38 | ExtractFaceprintsForAuthLoop        | No         | Device extracts, host stores/matches     |
| 39 | MatchFaceprints                     | No         | Host-side matcher (no device involved)   |
| 40 | GetUsersFaceprints                  | No         | DB export from device                    |
| 41 | SetUsersFaceprints                  | No         | DB import to device                      |
+----+-------------------------------------+------------+------------------------------------------+
|    | IMAGE-BASED OPERATIONS              |            |                                          |
+----+-------------------------------------+------------+------------------------------------------+
| 42 | EnrollImage                         | No         | Requires sending BGR24 image (~1.4MB)    |
| 43 | EnrollImageFeatureExtraction        | No         | Requires sending BGR24 image             |
| 44 | DetectFace                          | No         | Host-side only (requires ONNX pipeline)  |
+----+-------------------------------------+------------+------------------------------------------+
|    | F500 DETECTION LOOPS                |            |                                          |
+----+-------------------------------------+------------+------------------------------------------+
| 45 | DetectPersons                       | No         | F500+ only                               |
| 46 | DetectPoses                         | No         | F500+ only                               |
| 47 | DetectBodyParts                     | No         | F500+ only                               |
| 48 | DecodeBarcodes                      | No         | F500+ only                               |
+----+-------------------------------------+------------+------------------------------------------+
|    | DEBUG / MAINTENANCE                 |            |                                          |
+----+-------------------------------------+------------+------------------------------------------+
| 49 | DumpAndMount                        | No         | F500+ debug only                         |
| 50 | MountDebug                          | No         | F500+ debug only                         |
+----+-------------------------------------+------------+------------------------------------------+
|    | PREVIEW (FRAME STREAMING)           |            |                                          |
+----+-------------------------------------+------------+------------------------------------------+
| 51 | StartPreview / StopPreview          | No         | UVC protoco over USB                     |
+----+-------------------------------------+------------+------------------------------------------+
|    | FIRMWARE UPDATE                     |            |                                          |
+----+-------------------------------------+------------+------------------------------------------+
| 52 | FwUpdate                            | No         | Requires large binary transfer (~MB)     |
+----+-------------------------------------+------------+------------------------------------------+
|    | SECURE MODE (ECDSA)                 |            |                                          |
+----+-------------------------------------+------------+------------------------------------------+
| 53 | Pair                                | No         | Out of scope                             |
| 54 | Unpair                              | No         | Out of scope                             |
+----+-------------------------------------+------------+------------------------------------------+

SUMMARY: 29 of 54 functions/callbacks implemented (all core operations + device control).
         Remaining 25 are specialized features (image transfer, host matching, cropped
         image callback, F500 detection, preview, FW update, debug, secure mode).
```
