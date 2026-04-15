// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

using System;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;

namespace rsid
{
    public class FwUpdater : IDisposable
    {
        public delegate void ProgressCallback(float progress);

        [StructLayout(LayoutKind.Sequential)]
        public struct EventHandler
        {
            public ProgressCallback progressClbk;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        public struct FwUpdateSettings
        {
            public string port;
            public int force_full;
        }

        public FwUpdater(DeviceType deviceType)
        {
            _handle = rsid_create_fw_updater((int)deviceType);
        }

        ~FwUpdater()
        {
            Dispose();
        }

        public struct FwVersion
        {
            public DeviceType DeviceType { get; set; }
            public string OpfwVersion { get; set; }
            public string RecognitionVersion { get; set; }
        }

        public FwVersion? ExtractFwVersion(string binPath)
        {
            // Decide device type according to filename 
            string filename = Path.GetFileName(binPath);
            string ext = Path.GetExtension(binPath).ToLower();
            DeviceType deviceType = DeviceType.Unknown;
            if (filename.StartsWith("F45") && ext == ".bin")
            {
                deviceType = DeviceType.F45x;
            }
            else if ((filename.StartsWith("F46") || filename.StartsWith("F50")) && ext == ".bin")
            {
                deviceType = DeviceType.F50x;
            }
            else
            {
                return null;
            }

            var newFwVersion = new byte[100];
            var newRecognitionVersion = new byte[100];
            var result = rsid_extract_firmware_version(_handle, binPath, newFwVersion, newFwVersion.Length, newRecognitionVersion, newRecognitionVersion.Length);

            if (result == Status.Ok)
            {
                return new FwVersion
                {
                    DeviceType = deviceType,
                    OpfwVersion = Encoding.ASCII.GetString(newFwVersion).TrimEnd('\0'),
                    RecognitionVersion = Encoding.ASCII.GetString(newRecognitionVersion).TrimEnd('\0')
                };
            }

            return null;
        }

        public Status Update(string binPath, EventHandler eventHandler, FwUpdateSettings settings)
        {
            _eventHandler = eventHandler;
            return rsid_update_firmware(_handle, ref eventHandler, ref settings, binPath);
        }

        public bool IsOtpSkuCompatible(FwUpdateSettings settings, string bin_path, out int expected_otp_sku, out int device_otp_sku)
        {
            return rsid_is_otp_sku_compatible(_handle, settings, bin_path, out expected_otp_sku, out device_otp_sku) != 0;
        }

        public bool IsSecureBootCompatible(FwUpdateSettings settings, string bin_path, out int expected_secure_boot,
                                           out int device_secure_boot)
        {
            return rsid_is_secure_boot_compatible(_handle, settings, bin_path, out expected_secure_boot,
                                                  out device_secure_boot) != 0;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct FwCompatibilityInfo
        {
            // F45x only: OTP-encryption SKU number (1=SKU1, 2=SKU2); -1 = not applicable.
            public int ExpectedOtpSku;
            public int DeviceOtpSku;
            // F460/F500 only: CSS-signed firmware flag (0=unsigned, 1=CSS-signed); -1 = not applicable.
            public int ExpectedSecureBoot;
            public int DeviceSecureBoot;
            public int ExpectedDbVer;
            public int DeviceDbVer;
            public int ExpectedDeviceType;
            public int ConnectedDeviceType;

            public bool IsOtpSkuCompatible => ExpectedOtpSku < 0 || DeviceOtpSku < 0 || ExpectedOtpSku == DeviceOtpSku;
            public bool IsSecureBootCompatible => ExpectedSecureBoot < 0 || DeviceSecureBoot < 0 || ExpectedSecureBoot == DeviceSecureBoot;
            public bool IsDbCompatible => ExpectedDbVer < 0 || DeviceDbVer < 0 || ExpectedDbVer == DeviceDbVer;
            public bool IsDeviceTypeCompatible => ConnectedDeviceType < 0 || (ExpectedDeviceType >= 0 && ExpectedDeviceType == ConnectedDeviceType);
            public bool IsAllCompatible => IsOtpSkuCompatible && IsSecureBootCompatible && IsDbCompatible && IsDeviceTypeCompatible;
        }

        public bool CheckCompatibility(FwUpdateSettings settings, string bin_path, out FwCompatibilityInfo info)
        {
            info = new FwCompatibilityInfo();
            return rsid_check_compatibility(_handle, settings, bin_path, out info) != 0;
        }


        public void Dispose()
        {
            if (!_disposed)
            {
                rsid_destroy_fw_updater(_handle);
                _handle = IntPtr.Zero;
                _disposed = true;
            }
        }

        private IntPtr _handle;
        private EventHandler _eventHandler;
        private bool _disposed = false;

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_create_fw_updater(int deviceType);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern void rsid_destroy_fw_updater(IntPtr handle);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_extract_firmware_version(IntPtr handle, string binPath,
            [Out, MarshalAs(UnmanagedType.LPArray)] byte[] fw_output, int fw_output_len,
            [Out, MarshalAs(UnmanagedType.LPArray)] byte[] recognition_output, int recognition_output_len);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern Status rsid_update_firmware(IntPtr handle, ref EventHandler eventHandler, ref FwUpdateSettings settings, string binPath);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern int rsid_is_otp_sku_compatible(IntPtr handle, FwUpdateSettings settings, string bin_path,
                                                     out int expected_otp_sku, out int device_otp_sku);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern int rsid_is_secure_boot_compatible(IntPtr handle, FwUpdateSettings settings, string bin_path,
                                                         out int expected_secure_boot, out int device_secure_boot);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern int rsid_check_compatibility(IntPtr handle, FwUpdateSettings settings, string bin_path, out FwCompatibilityInfo info);
    }
}
