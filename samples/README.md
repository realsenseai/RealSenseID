# RealSense ID Samples
This folder contains basic code samples for the the supported languages.


To build use the -DRSID_SAMPLES cmake flag when building the library:

```console
cmake -DRSID_SAMPLES=1 ..
```
|Name | Language | Description | OS | Extra Cmake flag needed |
|---- | ----- | ---- | ----- | --- |
|Authenticate  | [C++](cpp/authenticate.cc) , [C](c/authenticate.c) , [C#](csharp/Program.cs) , [python](python/authenticate.py)| Connect to device and preform one authentication.| Windows, Linux | None |
|Enroll  | [C++](cpp/enroll.cc) , [C](c/enroll.c) , [python](python/enroll.py)| Connect to device and preform one enrollment for one new user.| Windows, Linux | None |
|Preview  | [C++](cpp/preview.cc) , [C](c/preview.c), , [python](python/preview.py)| Run preview for few seconds. Preview callback prints information on each frame.images are rgb24. **gui not included**| Windows, Linux | DRSID_PREVIW=1 |
|Preview-Snapshot  | [C++](cpp/preview-snapshot.cc) | Run preview and get snapshots cropped around the user's face. Snapshots won't be sent unless a face is detected by  the device. | Windows, Linux | DRSID_PREVIEW=1
|Multi Faces  | [C++](cpp/multi-faces.cc) | Demonstrate how to authenticate or detect spoof on multiple faces. After authentication (spoof detection) is done, trying to match timestamps from face detection rectangles to timestamps from image. | Windows, Linux **Preview timestamps - only on windows**  | DRSID_PREVIW=1 |
|Host Mode  | [C++](cpp/host-mode.cc) , [python](python/host_mode.py)| Example of using device in host mode. Preform one face extraction for enrollment and than preform one face extraction for authentication.| Windows, Linux | None |
|Pair Device  | [C++](cpp/pair-device.cc) | Example on how to pair the device with the host. Pairing is needed to enable secure communication with the device. | Windows, Linux | DRSID_SECURE=1 |
|Secure Mode Helper | [C++](cpp/secure_mode_helper.cc) , [C++ header](cpp/secure_mode_helper.h) | Example how to sign and verify keys exchanged with the device when using RSID_SECURE mode (ECDH protocol). In this sample we store the keys in memory and use the mbedtls library to sign/verify keys.| Windows, Linux | DRSID_SECURE=1 |
