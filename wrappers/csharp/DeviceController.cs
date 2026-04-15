// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

using System;
using System.Runtime.InteropServices;
using System.Text;

namespace rsid
{
    public class DeviceController : IDisposable
    {
        public DeviceController(DeviceType deviceType = DeviceType.F45x)
        {
            switch (deviceType)
            {
                case DeviceType.F45x:
                    _handle = rsid_create_device_controller_F45x();
                    break;
                case DeviceType.F50x:
                    _handle = rsid_create_device_controller_F50x();
                    break;
                default:
                    throw new ArgumentException("Invalid device type");
            }

            if (_handle == IntPtr.Zero)
            {
                throw new Exception("Error creating device controller");
            }
        }

        ~DeviceController()
        {
            Dispose(false);
        }

        public Status Connect(SerialConfig config)
        {
            return rsid_connect_controller(_handle, ref config);
        }

        public string QueryFirmwareVersion()
        {
            var output = new byte[250];
            if (rsid_query_firmware_version(_handle, output, output.Length) != Status.Ok)
                return "";

            return Encoding.ASCII.GetString(output).TrimEnd('\0');
        }

        public string QuerySerialNumber()
        {
            var output = new byte[30];
            if (rsid_query_serial_number(_handle, output, output.Length) != Status.Ok)
                return "";

            return Encoding.ASCII.GetString(output).TrimEnd('\0');
        }

        public Status Ping()
        {
            return rsid_ping(_handle);
        }

        public string FetchLog()
        {
            var output = new byte[128 * 1024];
            var status = rsid_fetch_log(_handle, output, output.Length);
            if (status != Status.Ok)
                throw new Exception("FetchLog failed with status " + status);

            return Encoding.ASCII.GetString(output).TrimEnd('\0');
        }

        public Status GetColorGains(out int red, out int blue)
        {
            IntPtr outRedPtr = Marshal.AllocHGlobal(sizeof(int));
            IntPtr outBluePtr = Marshal.AllocHGlobal(sizeof(int));

            var status = rsid_get_color_gains(_handle, outRedPtr, outBluePtr);
            if (status == Status.Ok)
            {
                red = Marshal.ReadInt32(outRedPtr);
                blue = Marshal.ReadInt32(outBluePtr);
            }
            else
            {
                red = blue = 0;

            }
            return status;
        }

        public Status SetColorGains(int red, int blue)
        {
            return rsid_set_color_gains(_handle, red, blue);
        }

        public void Disconnect()
        {
            rsid_disconnect_controller(_handle);
        }


        public Status GetTemperature(out float soc, out float board)
        {
            IntPtr socPtr = Marshal.AllocHGlobal(sizeof(float));
            IntPtr boardPtr = Marshal.AllocHGlobal(sizeof(float));
            try
            {
                var status = rsid_get_temperature(_handle, socPtr, boardPtr);
                if (status == Status.Ok)
                {
                    soc = Marshal.PtrToStructure<float>(socPtr);
                    board = Marshal.PtrToStructure<float>(boardPtr);
                }
                else
                {
                    soc = board = 0;

                }
                return status;
            }
            finally
            {
                Marshal.FreeHGlobal(socPtr);
                Marshal.FreeHGlobal(boardPtr);
            }

        }

        public string QueryBspVer()
        {
            var output = new byte[1024];
            if (rsid_query_bspver(_handle, output, output.Length) != Status.Ok)
                return "";
            return Encoding.ASCII.GetString(output).TrimEnd('\0');
        }

        public Status Reboot()
        {
            return rsid_reboot(_handle);
        }

        public void Dispose()
        {
            Dispose(true);
            // prevent finalization code for this object
            // from executing a second time.
            GC.SuppressFinalize(this);
        }

        protected virtual void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                rsid_destroy_device_controller(_handle);
                _handle = IntPtr.Zero;
                _disposed = true;
            }
        }
        private IntPtr _handle;
        private bool _disposed = false;

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_create_device_controller();

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_create_device_controller_F45x();

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_create_device_controller_F50x();

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern void rsid_destroy_device_controller(IntPtr rsid_device_controller);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_connect_controller(IntPtr rsid_device_controller, ref SerialConfig serialConfig);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern void rsid_disconnect_controller(IntPtr rsid_device_controller);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_query_firmware_version(IntPtr rsid_device_controller, [Out, MarshalAs(UnmanagedType.LPArray)] byte[] output, int len);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_query_serial_number(IntPtr rsid_device_controller, [Out, MarshalAs(UnmanagedType.LPArray)] byte[] output, int len);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_ping(IntPtr rsid_device_controller);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_fetch_log(IntPtr rsid_device_controller, [Out, MarshalAs(UnmanagedType.LPArray)] byte[] output, int len);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_get_temperature(IntPtr deviceController, IntPtr soc, IntPtr board);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_set_color_gains(IntPtr rsid_device_controller, int red, int blue);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_get_color_gains(IntPtr rsid_device_controller, IntPtr red, IntPtr blue);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_reboot(IntPtr rsid_device_controller);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_query_bspver(IntPtr rsid_device_controller, [Out, MarshalAs(UnmanagedType.LPArray)] byte[] output, int len);
    }
}
