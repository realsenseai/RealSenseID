
# 			RealSense ID Tools

[Instructions for Windows](#windows----compilation-and-usage)

[Instructions for Linux](#linux----compilation-and-usage)

## **Windows** -  Compilation and usage 

##  **Dependencies**:

 -  **[CMake](https://cmake.org/)**
-   **[Visual Studio](https://visualstudio.microsoft.com/downloads/)**


##  **How to build:**
Use CMake version 3.13.0 or above
1. Download or Clone the repository
2. In cloned folder, open command line

	**Default build:**
	```console
	> mkdir build
	> cd build
	> cmake ..
	```
	**Build with secure communication enabled:**
	```console
	> mkdir build
	> cd build
	> cmake -DRSID_SECURE=1 ..
	```
	**Build with preview enabled:**
	```console
	> mkdir build
	> cd build
	> cmake -DRSID_PREVIEW=1 ..
	```

3. In build folder you should have full visual studio solution. Run RealSenseID.sln
4. Build solution
5. After building solution you will find in \build\bin\<Debug/Release> three executables:
	- rsid-viewer.exe: GUI to view and use RealsenseID
	- rsid-cli.exe: Command line tool to use RealSenseID.
	- fw-updater-cli.exe: Firmware update tool
    

**Done!**
## **How to run example apps**

***Make sure you have connected the RealSense ID F450 / F455 device to your PC***.

Run Windows "Device Manager" and check in which COM port the device was recognized (It should appear under **Ports (COM & LPT)** (For example: COM3)

###  **RealSenseID Viewer:**
1. Simply Run: **rsid-viewer.exe**
2. Further configuration can be found in  **"rsid-viewer.exe.config"** file located next to the .exe file.

#### Dumping  
For saving images from sensors stream you can enable "Dump Frames" under app's setting screen. notice that:
- Dumping is running only in authentication,enroll or faceprints session.  
- Images will be saved under the directory specified in "DumpDir" attribute in  **"rsid-viewer.exe.config"** in PNG/RAW format.  
- If metadata enabled (see "sensor timestamps" in repository readme):  
 images will be named by sensor timestamp and the main image used by algorithm will be named with the suffix "selected".  
- Otherwise, images will be named by a frame number only.  


###  **RealSenseID Command Line Tool:**
At bin folder (where .exe files are located) run command line and run the app as following:
```console
rsid-cli.exe <port>
```
For Example:
```console
rsid-cli.exe COM3 
```


## **Linux** -  Compilation and usage 

***  instructions for Ubuntu 18.4 ***

##  **Dependencies**:

 -  **[CMake](https://cmake.org/)** - 3.13.0 or higher.

##  **How to compile**:

 1.  Download or Clone the repository.
 2. In main folder, Open Terminal and run the following:
 ```console
 > mkdir build
 > cd build
 > cmake ..
 > make
 ```
3. After building solution you will find in \build\bin\ two executables:
	1. rsid-cli: Command line interface to RealSenseID.
    2. fw-updater-cli: Firmware update tool.
    

**Done!**

## **How to run toos**

***First make sure you have connected the RealSense ID F450 / F455 Camera to your PC***.

1. Identify your device by running:
	```console 
	sudo dmesg | grep 'F450' -A 5 | grep ttyACM
	```
	The output should be something like: "... ttyACM#: USB ACM device ..." - where # is the port number.
    
2. Grant the necessary permissions for your user, to be able to communicate with the device:
	```console
	sudo usermod -a -G dialout $USER
	```
	Log out of your current session for this to take effect.

   Alternatively, you can run the following command, replacing # with the port number found in step 1:
	```console
	sudo chmod 666 /dev/ttyACM#
	```
	This will only affect the current session.

###  **RealSenseID Command Line Interface:**
At bin folder (where .exe files are located) run command line and run the app as following:
```console
./rsid-cli <port> 
```
For Example:
```console
./rsid-cli /dev/ttyACM0 usb
```