# Python samples. 

#### Build
```bash
cmake -B build -DRSID_PY=ON -DRSID_PREVIEW=ON
cmake --build build --config Release
```

The build automatically copies `rsid.dll` (Windows) or `rsid.so` (Linux) next to the Python module.

#### Run
```bash
# Windows
cd build/bin/Release
python ../../../samples/python/authenticate.py

# Linux
cd build/bin
python3 ../../../samples/python/authenticate.py
```

#### Samples
* [authenticate.py](authenticate.py): basic sample to authenticate a user. 
* [enroll.py](enroll.py): basic sample to enroll a user.
* [enroll_image.py](enroll_image.py): basic sample to enroll a user using an image file (requires opencv).
* [users.py](users.py): display enrolled users.
* [preview.py](preview.py): getting preview images from the camera.
* [host_mode.py](host_mode.py): host mode where face features are stored in the host instead of the device.
* [detect_persons.py](detect_persons.py): detect persons (F50x only).
* [bodyparts.py](bodyparts.py): detect body parts (F50x only).
* [poses.py](poses.py): detect body poses (F50x only).
* [viewer.py](viewer.py): nice little GUI to authenticate/enroll/delete users (requires opencv and numpy).
