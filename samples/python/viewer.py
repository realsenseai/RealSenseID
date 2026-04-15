#!/usr/bin/env python3

"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2020-2024 RealSense, Inc. All Rights Reserved.
"""

import argparse
import copy
import ctypes
import logging
import os
import pathlib
import queue
import signal
import sys
import threading
import time
import traceback
from dataclasses import dataclass
from typing import Optional

import PIL

try:
    import numpy as np
except ImportError:
    print('Failed importing numpy. Please install it (pip install numpy).')
    print('  On Ubuntu, you may install the system wide package instead: sudo apt install python3-numpy')
    exit(1)

try:
    import tkinter as tk
    import tkinter.ttk as ttk
    from tkinter import messagebox, simpledialog
except ImportError as ex:
    print(f'Failed importing tkinter ({ex}).')
    print('  On Ubuntu, you also need to: sudo apt install python3-tk')
    print('  On Fedora, you also need to: sudo dnf install python3-tkinter')
    exit(1)

try:
    from PIL import Image, ImageDraw, ImageOps, ImageTk
except ImportError as ex:
    print(f'Failed importing PIL ({ex}). Please install Pillow *version 9.1.0 or newer* (pip install Pillow).')
    print(
        '  On Ubuntu, you may install the system wide package instead: sudo apt install python3-pil python3-pil.imagetk')
    exit(1)

import rsid_py

# Configure logging
logging.basicConfig(
    level=logging.INFO,
    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
    handlers=[
        logging.StreamHandler(sys.stdout)
    ]
)
logger = logging.getLogger(__name__)

logger.info(f'Version: {rsid_py.__version__}')

# globals
WINDOW_NAME = 'RealSenseID'


# Configuration
@dataclass
class ViewerConfig:
    """Configuration for the RealSenseID viewer application."""
    port: Optional[str] = None
    camera_index: int = -1
    dump_mode: rsid_py.DumpMode = rsid_py.DumpMode.Disable
    device_type: Optional[rsid_py.DeviceType] = None
    max_queue_size: int = 10
    video_update_interval_ms: int = 15
    reset_delay_ms: int = 3000
    snapshot_display_duration_ms: int = 5000
    canvas_default_width: int = int(720 / 1.5)
    canvas_default_height: int = int(1280 / 1.5) + 80

    def __post_init__(self):
        if self.dump_mode in [rsid_py.DumpMode.CroppedFace, rsid_py.DumpMode.FullFrame]:
            self.dumps_dir = pathlib.Path('.') / 'dumps'
            self.dumps_dir.mkdir(parents=True, exist_ok=True)
            if not self.dumps_dir.exists():
                raise RuntimeError('Unable to create dumps directory.')


# Custom exceptions
class PreviewError(Exception):
    """Exception raised for preview-related errors."""
    pass


class DeviceError(Exception):
    """Exception raised for device-related errors."""
    pass


class Controller(threading.Thread):
    """Controller thread for managing device preview and authentication."""

    status_msg: str
    detected_faces: list[dict]  # array of (faces, success, user_name)
    running: bool = True
    window_init: bool = False

    def __init__(self, config: ViewerConfig):
        super().__init__()
        self.config = config
        self.preview: Optional[rsid_py.Preview] = None
        self.status_msg = ''
        self.detected_faces = []
        self.port = config.port
        self.camera_index = config.camera_index
        self.device_type = config.device_type
        self.dump_mode = config.dump_mode
        self.image_q: queue.Queue = queue.Queue(maxsize=config.max_queue_size)
        self.snapshot_q: queue.Queue = queue.Queue(maxsize=config.max_queue_size)

        if self.dump_mode in [rsid_py.DumpMode.CroppedFace, rsid_py.DumpMode.FullFrame]:
            self.status_msg = '-- Dump Mode --' \
                if rsid_py.DumpMode.FullFrame == self.dump_mode else '-- Cropped Face --'
            logger.info(f'Running in {self.status_msg} mode')

    def reset(self):
        self.status_msg = ''
        self.detected_faces = []

    def on_result(self, result, user_id=None):
        success = result == rsid_py.AuthenticateStatus.Success
        self.status_msg = f'Success "{user_id}"' if success else str(result)

        # find next face without a status
        for f in self.detected_faces:
            if 'success' not in f:
                f['success'] = success
                f['user_id'] = user_id
                break

    def on_progress(self, p: rsid_py.FacePose):
        self.status_msg = f'on_progress {p}'

    def on_hint(self, hint: rsid_py.AuthenticateStatus | rsid_py.EnrollStatus | None, frame_score: float):
        self.status_msg = f'{hint}'

    def on_faces(self, faces: list[rsid_py.FaceRect], timestamp: int):
        self.status_msg = f'detected {len(faces)} face(s)'
        self.detected_faces = [{'face': f} for f in faces]

    def on_face_cropped_image(self, buffer: memoryview, width: int, height: int, timestamp: int):
        """Callback for cropped face images during authentication/enrollment."""
        logger.debug(f"on_face_cropped_image called - width: {width}, height: {height}, ts: {timestamp}")

        try:
            if self.dump_mode == rsid_py.DumpMode.CroppedFace:
                # Convert memoryview to numpy array
                arr = np.asarray(buffer, dtype=np.uint8)
                # Reshape to RGB image (buffer is RGB format)
                array2d = arr.reshape((height, width, 3))

                # Convert to PIL Image
                pil_image = Image.fromarray(array2d, 'RGB')

                # Save to dumps directory
                dump_path = pathlib.Path('.') / 'dumps'
                dump_path.mkdir(parents=True, exist_ok=True)

                # Create filename with timestamp
                file_name = f'cropped_face_{timestamp}.png'
                file_path = dump_path / file_name

                try:
                    pil_image.save(file_path, 'PNG')
                    logger.info(f'Cropped face saved to: {file_path.absolute()}')
                except Exception as e:
                    logger.error(f"Failed to save cropped face: {e}")

                # Also put in queue for GUI display
                try:
                    self.snapshot_q.put_nowait(pil_image)
                except queue.Full:
                    logger.debug("Snapshot queue full, dropping snapshot")
        except Exception as e:
            logger.exception(f"Error processing cropped face image: {e}")

    def auth_example(self):
        with rsid_py.FaceAuthenticator(self.device_type, self.port) as f:
            self.status_msg = "Authenticating.."
            f.authenticate(on_hint=self.on_hint, on_result=self.on_result, on_faces=self.on_faces,
                           on_face_cropped_image=self.on_face_cropped_image)

    def enroll_example(self, user_id=f'user_{int(time.time() / 1000)}'):
        with rsid_py.FaceAuthenticator(self.port) as f:
            self.status_msg = "Enroll.."
            f.enroll(on_hint=self.on_hint, on_progress=self.on_progress,
                     on_result=self.on_result, on_faces=self.on_faces, user_id=user_id,
                     on_face_cropped_image=self.on_face_cropped_image)

    def remove_all_users(self):
        with rsid_py.FaceAuthenticator(self.port) as f:
            self.status_msg = "Remove.."
            f.remove_all_users()
            self.status_msg = 'Remove Success'

    def query_users(self):
        with rsid_py.FaceAuthenticator(self.port) as f:
            return f.query_user_ids()

    #########################
    # Preview
    #########################
    def on_image(self, image: rsid_py.Image):
        if not self.running:
            return
        try:
            buffer = memoryview(image.get_buffer())
            arr = np.asarray(buffer, dtype=np.uint8)
            array2d = arr.reshape((image.height, image.width, -1))
            # Use put_nowait to avoid blocking if queue is full
            try:
                self.image_q.put_nowait(array2d.copy())
            except queue.Full:
                # Drop frame if queue is full
                logger.debug("Image queue full, dropping frame")
        except ValueError as e:
            logger.error(f"Error reshaping image buffer: {e}")
        except Exception as e:
            logger.exception(f"Unexpected error processing image: {e}")

    def on_snapshot(self, image: rsid_py.Image):
        logger.debug(f"on_snapshot called - dump_mode: {self.dump_mode}")
        try:
            if self.dump_mode == rsid_py.DumpMode.FullFrame:
                buffer = copy.copy(bytearray(image.get_buffer()))
                dump_path = (pathlib.Path('.') / 'dumps' / f'timestamp-{image.metadata.timestamp}')
                dump_path.mkdir(parents=True, exist_ok=True)
                logger.info(f"Frame metadata: ts={image.metadata.timestamp}, "
                            f"status={image.metadata.status}, "
                            f"sensor_id={image.metadata.sensor_id}, "
                            f"exposure={image.metadata.exposure}, "
                            f"gain={image.metadata.gain}, "
                            f"led={image.metadata.led}")
                file_name = (f'{image.metadata.timestamp}-{image.metadata.status}-{image.metadata.sensor_id}-'
                             f'{image.metadata.exposure}-{image.metadata.gain}.w10')
                file_path = dump_path / file_name
                with open(file_path, 'wb') as fd:
                    fd.write(buffer)
                logger.info(f'RAW file saved to: {file_path.absolute()}')
            elif self.dump_mode == rsid_py.DumpMode.CroppedFace:
                buffer = image.get_buffer()
                # https://pillow.readthedocs.io/en/stable/reference/Image.html#PIL.Image.frombytes
                # https://pillow.readthedocs.io/en/stable/handbook/writing-your-own-image-plugin.html#the-raw-decoder
                pil_image = Image.frombytes('RGB', (image.width, image.height), buffer, 'raw',
                                            'RGB', 0, 1)

                # Save to dumps directory
                dump_path = pathlib.Path('.') / 'dumps'
                dump_path.mkdir(parents=True, exist_ok=True)

                # Create filename with timestamp
                timestamp = int(time.time() * 1000)  # milliseconds
                file_name = f'cropped_face_{timestamp}.png'
                file_path = dump_path / file_name

                try:
                    pil_image.save(file_path, 'PNG')
                    logger.info(f'Cropped face saved to: {file_path.absolute()}')
                except Exception as e:
                    logger.error(f"Failed to save cropped face: {e}")

                # Also put in queue for GUI display
                try:
                    self.snapshot_q.put_nowait(pil_image)
                except queue.Full:
                    logger.debug("Snapshot queue full, dropping snapshot")
        except Exception as e:
            logger.exception(f"Unexpected error processing snapshot: {e}")

    def start_preview(self):
        try:
            preview_cfg = rsid_py.PreviewConfig()
            preview_cfg.device_type = self.device_type
            preview_cfg.camera_number = self.camera_index
            if self.dump_mode == rsid_py.DumpMode.FullFrame:
                preview_cfg.preview_mode = rsid_py.PreviewMode.RAW10_1080P  # In dump mode, we can use RAW10
            elif self.dump_mode in [rsid_py.DumpMode.CroppedFace, rsid_py.DumpMode.Disable]:
                preview_cfg.preview_mode = rsid_py.PreviewMode.MJPEG_1080P

            self.preview = rsid_py.Preview(preview_cfg)
            self.preview.start(preview_callback=self.on_image, snapshot_callback=self.on_snapshot)
            logger.info(f"Preview started for camera {self.camera_index}")
        except Exception as e:
            logger.exception(f"Failed to start preview: {e}")
            raise PreviewError(f"Failed to start preview: {e}") from e

    def change_camera(self, camera_index: int, device_type: Optional[rsid_py.DeviceType] = None):
        self.camera_index = camera_index
        if device_type is not None:
            self.device_type = device_type
        try:
            if self.preview is not None:
                self.preview.stop()
                logger.info("Previous preview stopped")
        except Exception as e:
            logger.exception(f"Error stopping preview: {e}")
        self.start_preview()
        self.status_msg = f"Camera {self.camera_index}"

    def run(self):
        try:
            self.start_preview()
            while self.running:
                time.sleep(0.1)
        except Exception as e:
            logger.exception(f"Error in controller thread: {e}")
        finally:
            logger.info("Stopping preview...")
            if self.preview is not None:
                try:
                    self.preview.stop()
                    self.preview = None
                except Exception as e:
                    logger.exception(f"Error stopping preview: {e}")
            logger.info("Controller thread exited")

    def exit_thread(self):
        self.status_msg = 'Bye.. :)'
        self.running = False
        time.sleep(1)


class GUI(tk.Tk):
    """GUI application for RealSenseID viewer."""

    def __init__(self, controller: Controller):
        super().__init__(className=WINDOW_NAME)
        self.scaled_image: Optional[ImageTk.PhotoImage] = None
        self.image: Optional[Image.Image] = None
        self.snapshot_image: Optional[ImageTk.PhotoImage] = None
        self.controller = controller
        self.reset_handle: Optional[str] = None
        self.video_update_handle: Optional[str] = None
        self.resize_handle: Optional[str] = None
        self.snapshot_handle: Optional[str] = None

        self.devices: list = []
        self.device_combo: Optional[ttk.Combobox] = None

        self.title(f'{WINDOW_NAME} ({str(controller.device_type).split('.')[-1]})')
        max_w = controller.config.canvas_default_width
        max_h = controller.config.canvas_default_height
        self.geometry(f"{max_w}x{max_h}")
        self.minsize(int(max_w / 1.5), int(max_h / 2.5))
        self.maxsize(max_w, max_h)

        # Window bindings
        self.protocol("WM_DELETE_WINDOW", self.exit_app)
        self.bind('<Escape>', lambda e: self.exit_app())
        self.bind("<Configure>", self.resize)

        self.grid_columnconfigure((1, 0), weight=1)
        self.grid_rowconfigure((1, 0), weight=1)

        # Video frame
        self.video_frame = ttk.Frame(self)
        self.video_frame.grid(row=0, column=0, padx=(0, 0), pady=(0, 20), sticky="nsew", columnspan=2)
        self.video_frame.grid_rowconfigure((0, 1), weight=1)
        self.video_frame.grid_columnconfigure((0, 1), weight=1)
        self.canvas = tk.Canvas(self.video_frame, bg='black')
        self.canvas.grid(row=0, column=0, padx=0, pady=0, sticky="nsew", columnspan=1)
        self.canvas.configure(width=max_w, height=max_h)

        # Canvas
        self.reset_canvas = True
        self.canvas_image_id = None
        self.canvas_text_id = None
        self.canvas_text_bg_id = None
        self.canvas_snapshot_image_id = None

        # Button frame
        self.button_frame = ttk.Frame(self)
        self.button_frame.grid(row=1, column=0, padx=(5, 5), pady=(0, 5), sticky="nsew", columnspan=2)

        self.auth_button = ttk.Button(self.button_frame, text="Authenticate",
                                      command=self.authenticate)
        self.auth_button.grid(row=0, column=0, padx=(5, 5), pady=(5, 5), ipady=5, sticky="nsew")

        self.enroll_button = ttk.Button(self.button_frame, text="Enroll",
                                        command=self.enroll)
        self.enroll_button.grid(row=0, column=1, padx=(5, 5), pady=(5, 5), ipady=5, sticky="nsew")

        self.delete_button = ttk.Button(self.button_frame, text="Delete All",
                                        command=self.remove_all_users)
        self.delete_button.grid(row=0, column=2, padx=(5, 5), pady=(5, 5), ipady=5, sticky="nsew")

        # Camera selection combo box
        self.init_camera_combo()

        self.button_frame.grid_columnconfigure(0, weight=1)
        self.button_frame.grid_columnconfigure(1, weight=1)
        self.button_frame.grid_columnconfigure(2, weight=1)
        self.button_frame.grid_columnconfigure(3, weight=1)

        style = ttk.Style(self)
        if sys.platform.startswith('win'):
            style.theme_use('vista')
        else:
            style.theme_use('clam')

        self.bind("<Key>", self.key_event)
        self.after(50, self.update_video)
        self.after(200, self.update_app_icon)

    def init_camera_combo(self):
        self.devices = rsid_py.discover_devices()

        # If no devices – show a disabled combo
        if not self.devices:
            self.device_combo = ttk.Combobox(
                self.button_frame,
                state="disabled",
                values=["No devices"]
            )
            self.device_combo.current(0)
            self.device_combo.grid(row=0, column=3, padx=(5, 5), pady=(5, 5), sticky="nsew")
            return

        items = []
        selected_index = 0
        for i, d in enumerate(self.devices):
            items.append(f"Cam {d.camera_number}")
            if d.serial_port == self.controller.port and d.camera_number == self.controller.camera_index:
                selected_index = i

        self.device_combo = ttk.Combobox(
            self.button_frame,
            width = 6,
            state="readonly",
            values=items
        )
        self.device_combo.grid(row=0, column=3, padx=(5, 5), pady=(5, 5), sticky="nsew")
        self.device_combo.current(selected_index)
        self.device_combo.bind("<<ComboboxSelected>>", self.on_camera_selected)

    def on_camera_selected(self, event=None):
        if not self.devices or self.device_combo is None:
            return

        idx = self.device_combo.current()
        if idx < 0 or idx >= len(self.devices):
            return

        chosen = self.devices[idx]
        self.controller.port = chosen.serial_port
        self.controller.change_camera(chosen.camera_number, chosen.device_type)
        self.title(f'{WINDOW_NAME} ({str(self.controller.device_type).split(".")[-1]})')

    def update_app_icon(self):
        # Window Icon
        icon = Image.new("RGB", (50, 50))
        op = ImageDraw.Draw(icon)
        op.text((10, 0), "R", font_size=40, fill="green")
        self.icon = ImageTk.PhotoImage(icon)
        self.wm_iconphoto(False, self.icon)

    def key_event(self, event):
        cmd_exec = {'a': self.authenticate,
                    'e': self.enroll,
                    'd': self.remove_all_users,
                    'q': self.exit_app}
        cmd_exec.get(event.char, lambda: None)()

    def resize(self, event):
        if event.widget == self.canvas:
            self.canvas.configure(width=event.width, height=event.height)
            self.reset_canvas = True
            if self.resize_handle is not None:
                self.after_cancel(self.resize_handle)
            if self.resize_handle is None:
                self.resize_handle = self.after(100, self.canvas.update_idletasks)

    def reset_later(self):
        if self.reset_handle is not None:
            self.after_cancel(self.reset_handle)
        self.reset_handle = self.after(self.controller.config.reset_delay_ms, self.controller_reset)

    def controller_reset(self):
        self.controller.reset()
        self.reset_canvas = True

    def authenticate(self):
        self.controller.reset()
        self.controller.auth_example()
        self.reset_later()

    def remove_all_users(self):
        self.controller.reset()
        result = messagebox.askyesno("Remove all users",
                                     "Are you sure you want to remove all users?",
                                     parent=self)
        if result:
            self.controller.remove_all_users()
            self.reset_later()

    def enroll(self):
        self.controller.reset()
        user_input = simpledialog.askstring(prompt="User ID:", title="Enter user id", parent=self)
        if user_input is not None:
            self.controller.enroll_example(user_input)
            self.reset_later()

    def clear_snapshot(self):
        self.canvas.itemconfig(self.canvas_snapshot_image_id, image=None)
        self.canvas.itemconfig(self.canvas_snapshot_image_id, state='hidden')
        self.snapshot_handle = None

    def update_video(self):
        self.update_idletasks()
        if not self.controller.image_q.empty() and self.controller.running:
            array2d = None
            while not self.controller.image_q.empty():
                array2d = self.controller.image_q.get()
            try:
                self.image = Image.fromarray(array2d)
            except PIL.UnidentifiedImageError as e:
                logger.error(f"Preview error: UnidentifiedImageError - {e}")

        self.canvas.update_idletasks()
        canvas_h = self.canvas.winfo_reqheight()
        canvas_w = self.canvas.winfo_reqwidth()

        if self.image is not None:
            image = self.image.copy()
            # Render faces
            for f in self.controller.detected_faces:
                self.render_face_rect(f, image)

            scaled_image = ImageOps.contain(image, size=(canvas_w, canvas_h)).transpose(Image.Transpose.FLIP_LEFT_RIGHT)
            self.scaled_image = ImageTk.PhotoImage(image=scaled_image)

            if self.reset_canvas:
                self.canvas.delete("all")
                self.canvas_image_id = self.canvas.create_image(int(self.scaled_image.width() / 2),
                                                                int(self.scaled_image.height() / 2),
                                                                anchor=tk.CENTER, image=None)
                self.canvas_text_bg_id = self.canvas.create_rectangle(0, canvas_h - 50, canvas_w, canvas_h,
                                                                      fill='black', stipple='gray50')
                self.canvas_text_id = self.canvas.create_text(canvas_w / 2, canvas_h - 30, text='',
                                                              font='Helvetica 18 bold')
                self.canvas_snapshot_image_id = self.canvas.create_image(0, 0, anchor=tk.NW, image=None)
                self.canvas.itemconfig(self.canvas_snapshot_image_id, state='hidden')
                self.reset_canvas = False

            self.canvas.itemconfig(self.canvas_image_id, image=self.scaled_image)
            self.canvas.moveto(self.canvas_image_id, int((canvas_w - self.scaled_image.width()) / 2),
                               int((canvas_h - self.scaled_image.height()) / 2))

            # Render message
            msg = self.controller.status_msg.replace('Status.', ' ')
            if msg != '':
                color = self.color_from_msg(self.controller.status_msg)
                self.canvas.itemconfig(self.canvas_text_bg_id, state='normal')
                self.canvas.itemconfig(self.canvas_text_id, state='normal', text=msg, fill=color)
            else:
                self.canvas.itemconfig(self.canvas_text_bg_id, state='hidden')
                self.canvas.itemconfig(self.canvas_text_id, state='hidden')

            # Render snapshot
            new_snapshot = None
            while not self.controller.snapshot_q.empty():
                new_snapshot = self.controller.snapshot_q.get()

            if new_snapshot is not None:
                scaled_image = ImageOps.contain(new_snapshot, size=(int(canvas_w / 4), int(canvas_h / 4))).transpose(
                    Image.Transpose.FLIP_LEFT_RIGHT)

                self.snapshot_image = ImageTk.PhotoImage(image=scaled_image)

                if self.snapshot_handle is not None:
                    self.after_cancel(self.snapshot_handle)
                self.snapshot_handle = self.after(self.controller.config.snapshot_display_duration_ms,
                                                  self.clear_snapshot)
                self.canvas.itemconfig(self.canvas_snapshot_image_id, image=self.snapshot_image)
                self.canvas.itemconfig(self.canvas_snapshot_image_id, state='normal')

        elif self.image is None and self.controller.dump_mode:
            self.canvas.delete("all")
            self.canvas.create_text(canvas_w / 2, canvas_h / 2,
                                    text='Dump Mode', fill='white', font='Helvetica 20 bold')
            self.canvas.create_text(canvas_w / 2, (canvas_h / 2) + 30,
                                    text='Auth or Enroll to proceed', fill='white', font='Helvetica 14')

        self.update_idletasks()

        if self.video_update_handle is not None:
            self.after_cancel(self.video_update_handle)
        if self.controller.running:
            self.video_update_handle = self.after(self.controller.config.video_update_interval_ms,
                                                  self.update_video)

    @staticmethod
    def render_face_rect(face, image):
        img1 = ImageDraw.Draw(image)
        f = face['face']

        success = face.get('success')
        if success is None:
            color = 'yellow'
        else:
            color = 'green' if success else 'blue'

        shape = [(f.x, f.y), (f.x + f.w, f.y + f.h)]
        img1.rectangle(shape, width=8, outline=color)

    @staticmethod
    def color_from_msg(msg):
        if 'Success' in msg:
            return 'lime green'  # 0x3c, 0xff, 0x3c
        if 'Forbidden' in msg or 'Fail' in msg or 'NoFace' in msg:
            return 'RoyalBlue1'  # 0x3c, 0x3c, 255
        return 'gray80'  # 0xcc, 0xcc, 0xcc

    def exit_app(self):
        self.controller.exit_thread()
        self.quit()


def parse_arguments() -> argparse.Namespace:
    """Parse command line arguments."""
    arg_parser = argparse.ArgumentParser(prog='viewer', add_help=False)
    options = arg_parser.add_argument_group('Options')
    options.add_argument('-h', '--help', action='help', default=argparse.SUPPRESS,
                         help='Show this help message and exit.')
    options.add_argument('-p', '--port', help='Device port. Will detect first device '
                                              'port if not specified.', type=str)
    options.add_argument('-c', '--camera', help='Camera number. -1 for autodetect.', type=int, default=-1)
    options.add_argument('-v', '--verbose', help='Enable verbose logging.', action='store_true')

    group = arg_parser.add_mutually_exclusive_group(required=False)
    group.add_argument('-d', '--dump', help='Dump mode.', action='store_true')
    group.add_argument('-r', '--crop', help='Cropped Face mode.', action='store_true')

    return arg_parser.parse_args()


def discover_device(port: Optional[str]) -> tuple[str, rsid_py.DeviceType, int]:
    """Discover device or use provided port."""
    if port is None:
        devices = rsid_py.discover_devices()
        if len(devices) == 0:
            raise DeviceError('No rsid devices were found and no port was specified.')

        chosen = devices[0]
        device_type = chosen.device_type
        camera_index = chosen.camera_number
        port = chosen.serial_port
    else:
        device_type = rsid_py.discover_device_type(port)
        camera_index = -1

    logger.info(f'Using port: {port} ({device_type})')
    logger.info(f'Using camera index: {camera_index}')
    return port, device_type, camera_index


def configure_device(device_type: rsid_py.DeviceType, port: str, args: argparse.Namespace) -> rsid_py.DumpMode:
    """Configure device with specified dump mode."""
    dump_mode = rsid_py.DumpMode.Disable

    if args.dump:
        logger.warning("-" * 60)
        logger.warning('NOTE: Running in DUMP mode.')
        logger.warning('      While in dump mode, you need to use a separate rsid-client to initiate authentication')
        logger.warning('      for the RAW image to appear on this viewer.')
        logger.warning("-" * 60)
        dump_mode = rsid_py.DumpMode.FullFrame
    elif args.crop:
        dump_mode = rsid_py.DumpMode.CroppedFace

    try:
        with rsid_py.FaceAuthenticator(device_type, str(port)) as f:
            config = copy.copy(f.query_device_config())
            config.dump_mode = dump_mode
            f.set_device_config(config)
            f.disconnect()
        return dump_mode
    except Exception as e:
        logger.exception(f"Failed to configure device: {e}")
        raise DeviceError(f"Failed to configure device: {e}") from e


def main():
    args = parse_arguments()

    # Set logging level based on verbose flag
    if args.verbose:
        logging.getLogger().setLevel(logging.DEBUG)

    try:
        port, device_type, camera_index = discover_device(args.port)
        dump_mode = configure_device(device_type, port, args)

        # Create configuration
        viewer_config = ViewerConfig(
            port=port,
            camera_index=camera_index if args.camera == -1 else args.camera,
            device_type=device_type,
            dump_mode=dump_mode
        )

        def signal_handler(sig, frame):
            logger.info("Received interrupt signal")
            gui.exit_app()

        signal.signal(signal.SIGINT, signal_handler)

        controller = Controller(viewer_config)
        controller.daemon = True
        controller.start()
        gui = GUI(controller)
        gui.mainloop()

    except DeviceError as e:
        logger.error(f"Device error: {e}")
        sys.exit(1)
    except Exception as e:
        logger.exception(f"Unexpected error: {e}")
        sys.exit(1)


if __name__ == '__main__':
    if sys.platform.startswith('win'):
        app_id = 'realsenseai.realsenseid.viewer.1.0'
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(1)
            ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID(app_id)
        except:
            ctypes.windll.user32.SetProcessDPIAware()

    main()
