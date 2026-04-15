// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using System.Text;

namespace rsid
{

    public struct DeviceInfo
    {
        public string SerialPort { get; set; }
        public DeviceType DeviceType { get; set; }
        public string SerialNumber { get; set; }
        public int CameraNumber { get; set; }
    }

    public class Discover
    {
        static public DeviceType DiscoverDeviceType(string serialPort)
        {
            return (DeviceType)rsid_discover_device_type(serialPort);
        }

        static public DeviceInfo[] DiscoverDevices(int maxDevices = 10)
        {
            var nativeDevices = new rsid_device_info[maxDevices];
            int result = rsid_discover_devices(nativeDevices, maxDevices);
            if (result == -1)
                throw new InvalidOperationException("Buffer too small for discovered devices.");

            var devices = new DeviceInfo[result];
            for (int i = 0; i < result; i++)
            {
                devices[i] = new DeviceInfo
                {
                    SerialPort = nativeDevices[i].serial_port,
                    DeviceType = (DeviceType)nativeDevices[i].device_type,
                    SerialNumber = nativeDevices[i].serial_number,
                    CameraNumber = nativeDevices[i].camera_number
                };
            }
            return devices;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        public struct rsid_device_info
        {
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
            public string serial_port;
            public DeviceType device_type;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
            public string serial_number;
            public int camera_number;
        }

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        public static extern int rsid_discover_device_type(string port);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl)]
        public static extern int rsid_discover_devices([In, Out] rsid_device_info[] devices, int array_size);
    }
}
