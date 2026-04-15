// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

using System;
using System.Runtime.InteropServices;

// Log callback support. Provides a way to get log callbacks from the rsid library
namespace rsid
{
    public class Logging
    {
        public enum LogLevel
        {
            Trace,
            Debug,
            Info,
            Warning,
            Error,
            Critical,
            Off
        }


        public delegate void LogCallback(LogLevel level, string msg);
        public static void SetLogCallback(LogCallback clbk, LogLevel minLevel, bool doFormatting)
        {
            _clbk = clbk;
            rsid_set_log_clbk(clbk, (int)minLevel, doFormatting ? 1 : 0);
        }

        private static LogCallback _clbk;
    
        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_set_log_clbk(LogCallback clbk, int minLevel, int do_formatting);
    }
}
