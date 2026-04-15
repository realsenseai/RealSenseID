"""
License: Apache 2.0. See LICENSE file in root directory.
"""
import time
import os
import rsid_py

SAVE_FRAMES = False


def on_image(frame):
    print(f'got_frame #{frame.number} {frame.width}x{frame.height} {frame.size/1024:0.2f} KB')    
    # Set SAVE_FRAMES = True to save the frames as JPEGs to the current directory
    if SAVE_FRAMES:
        with open(f'frame_{frame.number}.jpg', "wb") as file:
            file.write(frame.get_buffer())
    if frame.number > 50:
        os._exit(0)


def select_camera():
    """
    Discovers devices and returns the selected device info.
    If multiple devices are found, asks the user to choose one.
    Exits the program if no devices are found.
    """
    devices = rsid_py.discover_devices()

    if len(devices) == 0:
        print('Error: No rsid devices were found and no port was specified.')
        exit(1)

    if len(devices) == 1:
        selected_device = devices[0]
        print(f"Found 1 device. Auto-selecting Camera #{selected_device.camera_number}")
        return selected_device

    # Multiple devices case
    print(f"Found {len(devices)} devices:")
    for d in devices:
        print(f" - Camera Number: {d.camera_number} ({d.device_type}, S/N: \"{d.serial_number}\")")

    while True:
        try:
            user_input = int(input("\nEnter the Camera Number you wish to use: "))
            # Find the device object that matches the input number
            for d in devices:
                if d.camera_number == user_input:
                    return d

            print(f"Error: Device with camera number {user_input} not found in the list.")
        except ValueError:
            print("Invalid input. Please enter a numeric camera number.")


if __name__ == '__main__':
    selected_device = select_camera()
    preview_cfg = rsid_py.PreviewConfig()
    preview_cfg.camera_number = selected_device.camera_number
    preview_cfg.device_type = selected_device.device_type
    preview_cfg.preview_mode = rsid_py.PreviewMode.MJPEG_1080P # or PreviewMode.MJPEG_720P 
    p = rsid_py.Preview(preview_cfg)
    p.start(on_image, snapshot_callback=None)

    try:
        print(f"Preview started on Camera #{selected_device.camera_number}. Press Ctrl+C to stop.")
        while True:
            time.sleep(1)
    except KeyboardInterrupt:
        print("\nCtrl+C detected. Stopping preview...")
        p.stop()
