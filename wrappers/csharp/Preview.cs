// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
namespace rsid
{
    public enum PreviewMode
    {
        MJPEG_1080P = 0, // default
        MJPEG_720P = 1,
        RAW10_1080P = 2
    };

    [StructLayout(LayoutKind.Sequential)]
    public struct PreviewConfig
    {
        public DeviceType deviceType;
        public int cameraNumber;
        public PreviewMode previewMode;
        public bool portraitMode;
        public bool rotateRaw;
        public bool skip_decode;

        public SerializablePreviewConfig ToSerialized()
        {
            return new SerializablePreviewConfig
            {
                DeviceType = deviceType.ToString(),
                cameraNumber = cameraNumber,
                PreviewMode = previewMode.ToString(),
                portraitMode = portraitMode,
                rotateRaw = rotateRaw,
                skip_decode = skip_decode
            };
        }
    }

    /// Serializable version of PreviewConfig for JSON export
    /// NOTE:
    /// Enum values are converted to strings because raw integer values are not
    /// human-readable in JSON and can be unclear during debugging, logging, or manual inspection.
    public struct SerializablePreviewConfig
    {
        public string DeviceType;
        public int cameraNumber;
        public string PreviewMode;
        public bool portraitMode;
        public bool rotateRaw;
        public bool skip_decode;
    }

    [StructLayout(LayoutKind.Sequential, Pack = 0)]    
    public struct FaceRect
    {
        public UInt32 x;
        public UInt32 y;
        public UInt32 width;
        public UInt32 height;
    };

    public enum BodyPart
    {
        Person = 0,
        Foot = 1,
        Arm = 2,
        Leg = 3,
        Hand = 4,
        Torso = 5
    }

    [StructLayout(LayoutKind.Sequential, Pack = 0)]
    public struct FaceLandmarks
    {
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 5)]
        public UInt32[] landmarksX;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 5)]
        public UInt32[] landmarksY;
    };

    [StructLayout(LayoutKind.Sequential, Pack = 0)]
    public struct PersonRect
    {
        public UInt32 x;
        public UInt32 y;
        public UInt32 width;
        public UInt32 height;
        public UInt32 id;
        public UInt32 distance;
        public BodyPart bodyPart;
        public float score;
    };

    [StructLayout(LayoutKind.Sequential, Pack = 0)]
    public struct PersonPose
    {
        public UInt32 x;
        public UInt32 y;
        public UInt32 width;
        public UInt32 height;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)]
        public UInt32[] landmarksX;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)]
        public UInt32[] landmarksY;
        [MarshalAs(UnmanagedType.ByValArray, SizeConst = 17)]
        public float[] landmarksScore;
    };

    [StructLayout(LayoutKind.Sequential, Pack = 0)]
    public struct Barcode
    {
        [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 1024)]
        public string data;
    };

    [StructLayout(LayoutKind.Sequential)]
    public struct PreviewImageMetadata
    {
        public UInt32 timestamp;
        public UInt32 exposure;
        public UInt32 gain;
        public char led;
        public UInt32 sensor_id;
        public UInt32 status;
        public char is_snapshot;
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct PreviewImage
    {
        public IntPtr buffer;
        public int size;
        public int width;
        public int height;
        public int stride;
        public int number;
        public PreviewImageMetadata metadata;
    }

    public delegate void PreviewCallback(PreviewImage image, IntPtr ctx);

    public class Preview : IDisposable
    {
        PreviewCallback _clbkDelegate;
        PreviewCallback _clbkDelegateSnapshot;
        PreviewConfig _config;

        public Preview(PreviewConfig config)
        {
            UpdateConfig(config);
        }

        public void UpdateConfig(PreviewConfig config)
        {
            _config = config;
            try
            {
                if (_handle != IntPtr.Zero)
                    rsid_destroy_preview(_handle);
                _handle = rsid_create_preview(ref _config);
            }
            catch (TypeLoadException)
            {
                // work without preview
                _handle = IntPtr.Zero;
            }
        }

        ~Preview()
        {
            Dispose(false);
        }

        public bool Start(PreviewCallback clbkPreview)
        {
            if (_handle == IntPtr.Zero)
                return false;

            _clbkDelegate = clbkPreview; //save it to prevent from the delegate garbage collected
            var rv = rsid_start_preview(_handle, _clbkDelegate, IntPtr.Zero) != 0;
            return rv;
        }

        public bool Start(PreviewCallback clbkPreview, PreviewCallback clbkSnapshot)
        {
            if (_handle == IntPtr.Zero)
                return false;

            _clbkDelegate = clbkPreview; //save it to prevent from the delegate garbage collected
            _clbkDelegateSnapshot = clbkSnapshot;
            var rv = rsid_start_preview_and_snapshots(_handle, _clbkDelegate, _clbkDelegateSnapshot, IntPtr.Zero) != 0;
            return rv;
        }

        public bool Pause()
        {
            if (_handle == IntPtr.Zero)
                return false;
            return rsid_pause_preview(_handle) != 0;
        }

        public bool Resume()
        {
            if (_handle == IntPtr.Zero)
                return false;
            return rsid_resume_preview(_handle) != 0;
        }

        public bool Stop()
        {
            if (_handle == IntPtr.Zero)
                return false;
            return rsid_stop_preview(_handle) != 0;
        }

        public void Dispose()
        {
            Dispose(true);
            // prevent finalization code for this object
            // from executing a second time.
            GC.SuppressFinalize(this);
        }

        private IntPtr _handle = IntPtr.Zero;
        private bool _disposed = false;

        protected virtual void Dispose(bool disposing)
        {
            if (!_disposed)
            {
                if (_handle != IntPtr.Zero)
                    rsid_destroy_preview(_handle);
                _handle = IntPtr.Zero;
                _disposed = true;
            }
        }

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_create_preview(ref PreviewConfig config);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern void rsid_destroy_preview(IntPtr rsid_preview);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern int rsid_start_preview(IntPtr rsid_preview, PreviewCallback clbkPreview, IntPtr ctx);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern int rsid_start_preview_and_snapshots(IntPtr rsid_preview, PreviewCallback clbkPreview, PreviewCallback clbkSnapshots, IntPtr ctx);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern int rsid_pause_preview(IntPtr rsid_preview);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern int rsid_resume_preview(IntPtr rsid_preview);

        [DllImport(Shared.DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern int rsid_stop_preview(IntPtr rsid_preview);
    }

}
