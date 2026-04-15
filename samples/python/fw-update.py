#!/usr/bin/env python3

"""
License: Apache 2.0. See LICENSE file in root directory.
Copyright(c) 2020-2024 RealSense, Inc. All Rights Reserved.
"""

import argparse
import pathlib
import threading
from argparse import SUPPRESS, ArgumentParser

import rsid_py

try:
    from tqdm import tqdm
    tqdm_installed = False
except ImportError:
    print('tqdm not installed - progress bar will not be available. pip install tqdm to enable.')
    tqdm_installed = False


def build_arg_parser():
    class MultiFormatter(argparse.RawTextHelpFormatter,
                         argparse.ArgumentDefaultsHelpFormatter):
        pass

    arg_parser = ArgumentParser(prog='fw-update-py', add_help=False,
                                formatter_class=MultiFormatter)
    options = arg_parser.add_argument_group('Options')
    options.add_argument('-h', '--help', action='help', default=SUPPRESS,
                         help='Show this help message and exit.')
    options.add_argument("-p", "--port", help="Device port (auto-detected if not specified)", required=False, type=str, default=None)
    options.add_argument("-f", "--file", help="Firmware binary file path", required=True, type=pathlib.Path)
    options.add_argument("--dry-run", help="Show summary report and exit", required=False, default=False,
                         action='store_true')
    options.add_argument("--force-version", help="Force update even if host version mismatch",
                         action='store_true',
                         default=False)

    options.add_argument("--force-full", help="Force update of modules even if they already exist \n"
                                              "in the current device \nfirmware. This will update all modules. \n",
                         action='store_true')

    return arg_parser


report_template = """
Summary Report

* Device
────────
    * Type: {device_type}
    * Serial number: {serial_number}
    * Serial port: {serial_port}
    * Firmware version: {firmware_version}
    * Recognition module version: {recognition_module_version}

* Firmware File
───────────────
    * Firmware file path: {bin_file_path}
    * Firmware version: {bin_firmware_version}
    * Recognition module version: {bin_recognition_module_version}

* Compatibility Matrix
──────────────────────
              * Security: {security_compat}
                          {security_msg}
              * Host:   {host_compat}
                        {host_msg}
"""

COMPATIBLE = "✓ Compatible"
NOT_COMPATIBLE = "𐄂 Not Compatible"

if __name__ == '__main__':
    args = build_arg_parser().parse_args()

    if args.port is None:
        devices = rsid_py.discover_devices()
        if not devices:
            print("Error: No rsid devices found.")
            exit(1)
        if len(devices) > 1:
            print("Error: Multiple devices found. Use --port to specify one.")
            for d in devices:
                print(f"  {d.serial_port}")
            exit(1)
        args.port = devices[0].serial_port
        print(f"Using device on port {args.port}")

    update_progress: float = 0
    pbar = None

    def progress_callback(progress: float) -> None:
        global update_progress
        update_progress = progress
        percent = int(100 * progress)
        if pbar is not None:
            pbar.update(percent)
        else:
            print(f"Progress: {percent}%")


    with rsid_py.FWUpdater(str(args.file), args.port) as updater:
        fw_file_info = updater.get_firmware_bin_info()
        device_fw_info = updater.get_device_firmware_info()        
        security_compat, security_msg = updater.is_security_compatible()
        host_compat, host_msg = updater.is_host_compatible(device_fw_info.device_type)

        print(report_template.format(
            device_type = device_fw_info.device_type,
            serial_number=device_fw_info.serial_number,
            serial_port=args.port,
            firmware_version=device_fw_info.fw_version,
            bin_file_path=args.file,
            recognition_module_version=device_fw_info.recognition_version,
            bin_firmware_version=fw_file_info.fw_version,
            bin_recognition_module_version=fw_file_info.recognition_version,
            security_compat=COMPATIBLE if security_compat else NOT_COMPATIBLE, security_msg=security_msg,
            host_compat=COMPATIBLE if host_compat else NOT_COMPATIBLE, host_msg=host_msg))

        if args.dry_run:
            exit(0)

        if not security_compat:
            print(f"Aborting: {security_msg}")
            exit(1)

        print("Started update process...")
        if tqdm_installed:
            pbar = tqdm(total=100, desc="Updating firmware")

        status = updater.update(force_version=args.force_version,
                                force_full=args.force_full,
                                progress_callback=progress_callback)

        if status == rsid_py.Status.Ok:
            condition = threading.Condition()
            with condition:
                condition.wait_for(lambda: update_progress == 1)
            print("Done!")
        else:
            print(f"Failed to start update with status {status}")
            exit(status.value)
