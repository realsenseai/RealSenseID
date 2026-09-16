# RealSense™ ID — Getting Started Guide

## F455

**Audience:** Developers and Integrators  
**Platform:** Windows only

---

## Table of Contents

1. [Connecting the Device](#1-connecting-the-device)
2. [Downloading the Latest Release](#2-downloading-the-latest-release)
3. [Installing the Windows Tools](#3-installing-the-windows-tools)
4. [Updating the Firmware](#4-updating-the-firmware)
5. [rsid-viewer Interface Overview](#5-rsid-viewer-interface-overview)
6. [Settings](#6-settings)
7. [Enrolling a User](#7-enrolling-a-user)
8. [Running Authentication](#8-running-authentication)
9. [1:1 Matching Mode](#9-11-matching-mode)
10. [Host Mode](#10-host-mode)
11. [Next Steps](#11-next-steps)

---

## 1. Connecting the Device

The F455 connects to your host PC using a USB cable. One end is USB Type-C and connects to the rear of the device. The other end is USB Type-A and connects to a USB 2.0 port on your PC.

1. Connect the USB Type-C end of the cable to the rear of the F455.
2. Connect the USB Type-A end to a USB 2.0 port on your PC.
3. Windows will detect the device and install the driver automatically. You may see a taskbar notification confirming the device is ready.

---

## 2. Downloading the Latest Release

All RealSense ID releases are published on GitHub. Navigate to:

**[https://github.com/realsenseai/RealSenseID/releases/latest](https://github.com/realsenseai/RealSenseID/releases/latest)**

This page always shows the most recent release. Download two items:

1. **Firmware binary** — the file ending in `.bin`. Save it to your Desktop.
   > There are two firmware SKUs — SKU1 and SKU2. If unsure what SKU is your device, you can run our PowerShell script [rsid_device_identifier](../scripts/rsid_device_identifier.ps1) to identify your device type before updating. Alternatively, the upgrade tool will detect a mismatch and prompt you to download the correct one.
2. **Windows installer** — the `.exe` file. Save it to the same location.

⚠️ **Important:** always download the firmware binary and the Windows installer from the same release page. Mixing versions may cause a compatibility error.

---

## 3. Installing the Windows Tools

The Windows installer includes the SDK compiled library, rsid-viewer, the firmware upgrade tool, and a command-line tool.

1. Double-click the installer to launch it.
2. If a Windows security prompt appears, click **Yes** — the installer is signed.
3. Accept the license agreement, choose your installation folder, click **Install**, then **Finish**.

---

## 4. Updating the Firmware

Firmware and SDK must always match the same release. Update the firmware before using the device.

1. Launch **rsid-viewer** from the Start menu or your installation folder.
   > When the application opens, it may show a firmware compatibility warning — this is expected. Proceed with the update to resolve it.
2. Click the <img src="img/icons/settings.png" width="16"/> icon to open Settings.
3. At the bottom of the Settings panel, find the **Firmware Update** section — it shows the currently installed version.
4. Click **Browse** and select the FW `.bin` file you downloaded.
5. Click **Open** to begin flashing the device.

   > DB Version Mismatch: A popup may appear warning that the database version on the device is incompatible with the new firmware, and that the database may be erased after the update.
   > If you have enrolled users you want to keep, click No to cancel the update, then:
   >
   > 1. Click the Export DB icon <img src="img/icons/export.png" width="16"/> (next to the <img src="img/icons/settings.png" width="16"/> icon in the top bar) to save your user database to a file on your PC.
   > 2. Return to Settings and apply the firmware update.
   > 3. Once the update is complete, click the Import DB icon <img src="img/icons/import.png" width="16"/> to reload your user database.
   >
   > If you have no enrolled users or don't need to keep them, click Yes to proceed with the update.

   ⚠️ **Do not disconnect the device during the firmware update.** This takes approximately one to two minutes.

6. When complete, the device will reboot. Your device is now running the latest firmware.

---

## 5. rsid-viewer Interface Overview

rsid-viewer connects to the device automatically when launched — no manual port selection is needed. The device serial number appears in the status bar at the bottom once connected.

### Top Bar

The RealSense ID logo and title appear on the left. On the right are five icon buttons:

| Button                                                            | Description                                     |
| ----------------------------------------------------------------- | ----------------------------------------------- |
| **Power** <img src="img/icons/power.png" width="16"/>             | Controls device power modes                     |
| **Settings** <img src="img/icons/settings.png" width="16"/>       | Opens the Settings panel                        |
| **Import DB** <img src="img/icons/import.png" width="16"/>        | Loads a user database from a file               |
| **Export DB** <img src="img/icons/export.png" width="16"/>        | Saves the current database to a file            |
| **Batch Enroll** <img src="img/icons/add_person.png" width="16"/> | Enrolls multiple users at once from a JSON file |

### Left Panel

The left panel has two tabs:

- **Users tab** — shows all enrolled users. The count is shown in the tab header. Use the checkbox to select users and the trash icon to delete them.
- **Log tab** — shows a live log of all operations. At the bottom: a button to clear the log, a button to save the device log to a file, and a toggle to open the debug console.

### Camera Preview

The right panel shows the live camera feed during operations, along with overlays such as face bounding boxes and status messages.

### Action Buttons

At the bottom of the window are five buttons:

| Button                                                                   | Description                                                             |
| ------------------------------------------------------------------------ | ----------------------------------------------------------------------- |
| **Enroll**                                                               | Starts live camera enrollment for a new user                            |
| **Enroll from image** <img src="img/icons/enroll_image.png" width="16"/> | Enrolls a user from a static image file                                 |
| **1:1**                                                                  | Toggles one-to-one matching mode (see [Section 9](#9-11-matching-mode)) |
| **Authenticate**                                                         | Runs facial authentication against the on-device database               |
| **Loop** <img src="img/icons/loop.png" width="16"/>                      | Runs authentication continuously until Stop is pressed                  |

### Status Bar

The status bar at the bottom shows the last operation result on the left, and the device serial number on the right.

---

## 6. Settings

Click <img src="img/icons/settings.png" width="16"/> icon in the top bar to open the Settings panel. Configure the settings below, then click **Apply** to save to the device or **Cancel** to discard.

### Basic

| Setting             | Description                                                                                                                                                                                                                                      |
| ------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **Operation**       | Selects the active algorithm. `Auth` runs the full authentication pipeline and is recommended for standard use. `Face` runs face detection only. `Spoof` runs the anti-spoofing check only. `Recog` runs recognition only without anti-spoofing. |
| **Anti Spoofing**   | Controls liveness detection aggressiveness. `Standard` is recommended for most deployments — solid protection with minimal impact on response time. `Enhanced` and `High` are stricter but add latency.                                          |
| **Frame Dumps**     | Saves captured images for debugging. `Off` disables saving. `Crop` saves the face-cropped region only (jpg). `Full` saves the complete frame (raw).                                                                                              |
| **Camera Rotation** | Rotates the image frame by 0°, 90°, 180°, or 270° to match the physical mounting orientation of the device.                                                                                                                                      |
| **Face Selection**  | Determines behaviour when multiple faces are in frame. `Single` selects the most prominent face. `Multi` processes up to five faces simultaneously.                                                                                              |
| **Motion Mode**     | `Static` for when the user stands still in front of the device. `Walkthrough` for when people walk towards the camera.                                                                                                                           |

### Advanced

| Setting                 | Description                                                                                                                                                                                                                                                  |
| ----------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| **DB Management**       | Controls where the user database is stored and matched. `On Device` stores up to 1,000 templates on the device. `On Host` sends feature data to the host application which manages its own database.                                                         |
| **Preview Format**      | Sets the camera preview resolution — `1080p` or `720p`.                                                                                                                                                                                                      |
| **Distance Limit (cm)** | Maximum distance at which the device processes a face. Set to `0` to disable. Any other value restricts detection to faces within that distance.                                                                                                             |
| **Frontal Face Policy** | `Strict` requires a fully frontal face. `Moderate` allows some angular tolerance. `None` applies no restriction — useful for walk-by or kiosk scenarios.                                                                                                     |
| **Features**            | When enabled, the selected features are returned as callbacks to the host application. `Rectangle` returns the bounding box coordinates of detected faces. `Landmarks` returns facial landmark points. `Distance` returns the face distance from the camera. |
| **Max Spoofs**          | Number of consecutive failed spoof attempts before the device locks. Set to `0` to disable. Unlock via the SDK API.                                                                                                                                          |
| **Matching Threshold**  | Similarity threshold for a successful face match. Set to `0` to use the recommended default.                                                                                                                                                                 |
| **Sensor Settings**     | Manual override for camera exposure and gain. Leave at `0` for automatic control.                                                                                                                                                                            |
| **GPIO Toggling**       | When enabled, toggles GPIO pin 1 on every successful authentication — useful for triggering external hardware such as a door lock.                                                                                                                           |
| **Detection ROIs**      | Defines up to 5 regions of interest for face detection. X, Y, Width, and Height define the active area in pixels. Click Reset to return to the full frame.                                                                                                   |

The **Firmware Update** section at the bottom of the Settings panel shows the current firmware version. Use **Browse** to flash a new file, or **Check For Updates** to see if a newer release is available.

---

## 7. Enrolling a User

1. Click the **Enroll** button at the bottom of the window.
2. Enter a user ID — any identifier for this person in your system, such as an employee ID or name.
3. Position your face in front of the camera, facing it directly. Remove any accessories such as sunglasses or a face mask.
4. The device captures multiple frames and stores the biometric template on-device. On success, the user appears in the **Users** tab.

### Enrolling from an Image

You can also enroll a user from a static image file instead of a live camera capture.

1. Click the **image icon** next to the Enroll button.
2. Select an image file from your computer.

> **Tip:** use a phone selfie, or take a screenshot of your face using Windows Snipping Tool while looking at the camera preview.

---

## 8. Running Authentication

1. Click **Authenticate** and look into the camera.
2. The result appears in the status bar — the matched user ID on success, or a failure status if no match is found.

> **Loop mode:** enable the loop button next to Authenticate to run authentication continuously without pressing the button each time. Click **Stop** to end the loop.

---

## 9. 1:1 Matching Mode

In standard authentication, the device matches the detected face against all enrolled users in the database. 1:1 mode instead authenticates against a single specific reference — without storing anything to the database. This is useful for verifying a person against a reference photo, such as a passport or driver's license.

1. Enable the **1:1** toggle button in the action bar.
2. Enroll the reference — either by clicking the image icon next to Enroll and selecting an image file. Enter any user ID when prompted.
3. Authenticate — either by clicking Authenticate and looking into the camera for a live verification, or by clicking the image icon next to Authenticate to verify against a static image file.
4. The device verifies your face against that single reference only and returns the result in the status bar.

---

## 10. Host Mode

By default, the device operates in On Device mode — storing and matching up to 1,000 biometric templates on-device. For larger deployments, RealSense ID supports On Host mode.

### Switching to On Host Mode

1. Open **Settings** (gear icon).
2. Set **DB Management** to **On Host** and click **Apply**.
3. A file dialog will appear. Either select an existing database file to load, or create a new empty one. This file is where the host will store the biometric templates.

### How It Works

In On Host mode, the device extracts biometric features from the camera input and sends them to your host application. The host is responsible for storing and matching those features against its own database, using the matching functions provided in our SDK.

This removes the 1,000 user limit — your database can scale to as many users as your infrastructure supports, stored in SQL, a vector database, or any custom store.

For a reference implementation showing how to integrate RealSense ID with a vector database, see our REST API repository:

**[https://github.com/realsenseaid/RealSenseID-REST](https://github.com/realsenseaid/RealSenseID-REST)**

---

## 11. Next Steps

You have now completed the full onboarding for the RealSense™ ID F455. From here you are ready to begin integrating RealSense ID into your application using our C, C++, Python, or Android SDK.

Full documentation, source code, and sample applications are available on GitHub:

**[https://github.com/realsenseaid/RealSenseID](https://github.com/realsenseaid/RealSenseID)**
