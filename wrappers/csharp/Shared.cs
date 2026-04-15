// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

using System;
using System.Runtime.InteropServices;


namespace rsid
{
    internal class Shared
    {
#if DEBUG
        public const string DllName = "rsid_c_debug";
#else
        public const  string DllName = "rsid_c";
#endif //DEBUG
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct SerialConfig
    {
        public string port;
    }

    //
    // Serial status
    //
    public enum Status
    {
        Ok = 100,
        Error,
        SerialError,
        SecurityError,
        VersionMismatch,
        CrcError,
        TooManySpoofs,
        NotSupported,
        DatabaseFull,
        DuplicateUserId,
        DuplicateFaceprints,
        InvalidSettings
    }

    // Signature callbacks
    public delegate int Sign(IntPtr buffer, int bufferLen, IntPtr outSig, IntPtr ctx);
    public delegate int Verify(IntPtr buffer, int bufferLen, IntPtr sig, int sigLen, IntPtr ctx);

    [StructLayout(LayoutKind.Sequential)]
    public struct SignatureCallback
    {
        public Sign signCallback;
        public Verify verifyCallback;
        public IntPtr ctx;
    }
}