# AGENTS.md — RealSenseID Android Sample App

## Project Overview

Android sample app (Java, **not** Kotlin) demonstrating RealSenseID face authentication hardware (F45x/F50x devices). Communicates with the device over USB CDC using a pre-built native SDK shipped as `app/libs/RealSenseID_release.aar`. The AAR exposes JNI bindings (`RealSenseIDSwigJNI`) — source is not in this repo.

## Architecture

- **Single-Activity** (`MainActivity`) with **Jetpack Navigation** (4 fragments: Preview, Users, Firmware, Settings) defined in `res/navigation/mobile_navigation.xml`.
- **`SDKWrapper`** (singleton enum at `util/SDKWrapper.java`) is the central USB/device bridge. All device communication flows through it: `getSerialConfig()` → `getAuthenticator()` / `getDeviceController()`. It manages USB CDC connections with retry logic, permission handling, and connection listeners.
- **`RealSenseIdSharedViewModel`** (`ui/shared/`) is the activity-scoped ViewModel sharing device state (type, USB connection, settings like security level, dump mode, algo flow) across all fragments.
- **Host vs Device mode**: Enrollment and authentication have parallel implementations — `HostEnrollmentHelper` / `DeviceEnrollmentHelper` and `HostAuthenticationHelper` / `DeviceAuthenticationHelper` in `util/enroll/` and `util/auth/`. Host mode stores faceprints in a local **Room** database (`db/AppDatabase.java`, entity `User`). Device mode delegates storage to the hardware.
- **Callback interfaces** in `callbacks/` (`AuthenticationCallback`, `EnrollmentCallback`, `RealSenseIdPreviewCallback`) decouple UI from SDK operations.
- **Preview pipeline**: `BaseRealSenseIdPreviewFragment` manages the native `Preview` object lifecycle via `PreviewHelper` (UVC stream from device). `PreviewFragment` extends it with enroll/auth UI.

## Build & Dependencies

- **Gradle Kotlin DSL** with version catalog at `gradle/libs.versions.toml`. AGP 9.1.0, Java 17, minSdk 26, targetSdk 29, compileSdk 36.
- **Firmware download**: `app/build.gradle.kts` contains a `downloadFirmwareFiles` task that pulls firmware binaries from GitHub Releases into `app/src/main/res/raw/`. Requires `GITHUB_TOKEN` in environment or `local.properties`. Firmware URLs/SHA-256 are declared in `firmware.properties` at project root.
- **Key libraries**: OpenCV (`org.opencv:opencv`), Google ML Kit Face Detection, Room, OkHttp, Timber (logging), PermissionX, Gson.
- **AAR dependency**: `implementation(fileTree("libs", include: ["*.aar"]))` — the SDK AAR must be placed in `app/libs/`.
- Build command: `./gradlew assembleDebug` (firmware files auto-download during preBuild).

## Conventions & Patterns

- **All source is Java** — do not introduce Kotlin source files.
- **Timber for logging** — never use `Log.d/e/i`; always `Timber.d/e/i`. Debug tree planted in `MainApplication`.
- **Threading**: SDK operations run on background `ExecutorService` threads. UI updates posted via `Handler(Looper.getMainLooper())` or `LiveData.postValue()`. The `SDKWrapper.Lock` object guards connection state.
- **Copyright header**: Every source file starts with `// Copyright (C) 2018-2025 RealSense, Inc.` + `// SPDX-License-Identifier: Apache-2.0`.
- **View Binding** is used (not Data Binding or synthetics). Access views via `binding.fieldName`.

## Key Files

| Path                                                              | Purpose                                                               |
| ----------------------------------------------------------------- | --------------------------------------------------------------------- |
| `app/build.gradle.kts`                                            | Build config, firmware download logic, dependency declarations        |
| `firmware.properties`                                             | Firmware binary URLs, filenames, and SHA-256 checksums                |
| `app/libs/RealSenseID_release.aar`                                | Pre-built native SDK (JNI) — treat as opaque binary                   |
| `app/src/main/java/.../util/SDKWrapper.java`                      | Central device connection manager (singleton enum)                    |
| `app/src/main/java/.../ui/shared/RealSenseIdSharedViewModel.java` | Shared state across fragments                                         |
| `app/src/main/java/.../ui/preview/PreviewFragment.java`           | Main face auth/enroll UI (~1000 lines)                                |
| `app/src/main/java/.../db/AppDatabase.java`                       | Room database for host-mode faceprint storage                         |
| `app/src/main/java/.../util/UsbDevicesReceiver.java`              | USB attach/detach broadcast receiver, device type detection (VID/PID) |

## Common Tasks

- **Add a new device config setting**: Add `MutableLiveData` field to `RealSenseIdSharedViewModel`, wire it in `SettingsFragment`/`SettingsViewModel`, read it in `PreviewFragment`.
- **Update firmware version**: Edit `firmware.properties` with new URL, raw_name, and SHA-256.
- **Modify enrollment/auth flow**: Check whether change applies to Host mode, Device mode, or both — they are separate helper classes under `util/enroll/` and `util/auth/`.

