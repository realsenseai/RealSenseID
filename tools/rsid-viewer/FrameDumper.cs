using System;
using System.Collections;
using System.Drawing;
using System.Drawing.Imaging;
using System.IO;
using System.Runtime.InteropServices;
using System.Windows.Media.Imaging;

namespace rsid_wrapper_csharp
{
    internal class FrameDumper
    {
        private static readonly TimeSpan MaxFrameAge = TimeSpan.FromMinutes(1);
        private static readonly string Raw10Extension = ".w10";
        private static readonly string JpgExtension = ".jpg";
        private static string _fileNamePrefix = "";
        private static uint _prevTimestamp = 0;
        private static readonly string settingsFileName = "settings.json";
        private readonly string _dumpDir;
        private object _mutex = new object();

        public FrameDumper(string dumpDir, string title, MainWindow.DeviceState deviceState)
        {
            // create sub folder for current session.            
            var unixMilliSeconds = ((DateTimeOffset)DateTime.Now).ToUnixTimeMilliseconds();
            var t = title.Replace(' ', '_');
            var subFolderName = $"{t}_session_{unixMilliSeconds}" + $"_SN_{deviceState.SerialNumber}";
            _dumpDir = Path.Combine(dumpDir, subFolderName);

            Directory.CreateDirectory(_dumpDir);
            string settingsFilePath = Path.Combine(_dumpDir, settingsFileName);

            JsonHelper.SerializeStructToJsonFile(deviceState, settingsFilePath);
            _fileNamePrefix = title.Contains("Enroll") ? "REG" : "AUTH";
        }

        public string DumpPreviewImage(rsid.PreviewImage image, ArrayList accesories)
        {
            lock (_mutex)
            {
                if (image.stride % 4 != 0)
                {
                    Logger.Log($"Warning: Skipping dump of image {image.width}x{image.height} (stride {image.stride} not 4-byte aligned)");
                    return null;
                }

                using (var bmp = new Bitmap(image.width, image.height, image.stride, PixelFormat.Format24bppRgb, image.buffer))
                {
                    var bmpSrc = ToBitmapSource(bmp);
                    var filename = GetJpgFilename(image, accesories);
                    var fileNameWithoutExt = Path.GetFileNameWithoutExtension(filename);
                    var extension = Path.GetExtension(filename);

                    string fullPath;
                    int count = 1;
                    do
                    {
                        fullPath = Path.Combine(_dumpDir, $"{fileNameWithoutExt}_face_{count}{extension}");
                        count++;
                    } while (File.Exists(fullPath));

                    DumpBitmapImage(bmpSrc, fullPath);
                    return fullPath;
                }
            }
        }

        // Save raw debug frame to dump dir        
        public string DumpRawImage(rsid.PreviewImage image, ArrayList accessories)
        {
            lock (_mutex)
            {
                var byteArray = new Byte[image.size];
                Marshal.Copy(image.buffer, byteArray, 0, image.size);
                var filename = GetRaw10Filename(image, accessories);
                // prevent path traversal
                if (_dumpDir == null || _dumpDir.Contains("../") || _dumpDir.Contains(@"..\"))
                {
                    throw new ArgumentException("Invalid dump dir");
                }
                if (filename == null || filename.Contains("../") || filename.Contains(@"..\"))
                {
                    throw new ArgumentException("Invalid dump file path");
                }
                var fullPath = Path.Combine(_dumpDir, filename);
                using (var fs = new FileStream(fullPath, FileMode.Create, FileAccess.Write))
                {
                    fs.Write(byteArray, 0, byteArray.Length);
                }
                return fullPath;
            }
        }


        public void Cancel()
        {
            lock (_mutex)
            {
                var file = Path.Combine(_dumpDir, "cancelled.txt");
                try
                {
                    using (FileStream fs = File.Create(file)) { }
                }
                catch { }
            }
        }

        private static string AccesoriesToString(ArrayList accessories)
        {
            if (accessories.Count == 0)
            {
                return String.Empty;
            }
            return "_" + string.Join("_", accessories.ToArray()).ToLower();
        }

        // return filename e.g. 
        // AUTH_timestamp_2042434_exp_38119_gain_64_led_on_sensor_left_status_0_sunglasses_medicalmask.w10
        private static string GetRaw10Filename(rsid.PreviewImage image, ArrayList accessories)
        {
            var timestampAvailable = image.metadata.timestamp != 0;
            var isYuv = image.metadata.timestamp == 1;
            uint currTimestamp = isYuv ? _prevTimestamp : (uint)image.metadata.timestamp;
            var prefix = timestampAvailable ? $"timestamp_{currTimestamp}" : $"frame_{image.number}";
            if (timestampAvailable)
            {
                _prevTimestamp = currTimestamp;
                uint exposure = image.metadata.exposure;
                uint gain = image.metadata.gain;
                var ledStr = (image.metadata.led != 0) ? "led_on" : "led_off";
                var sensorStr = (image.metadata.sensor_id != 0) ? "right" : "left";
                uint status = image.metadata.status;
                var accessoriesStr = AccesoriesToString(accessories);
                var postfix = isYuv ? "_yuv" : "";
                return $"{_fileNamePrefix}_{prefix}_exp_{exposure}_gain_{gain}_{ledStr}_sensor_{sensorStr}_status_{status}{accessoriesStr}{postfix}{Raw10Extension}";
            }
            else // metadata isn't valid
            {
                return $"{prefix}{Raw10Extension}";
            }
        }

        // get filename for jpg snapshots
        private static string GetJpgFilename(rsid.PreviewImage image, ArrayList accessories)
        {
            var timestampAvailable = image.metadata.timestamp != 0;
            var prefix = timestampAvailable ? $"timestamp_{image.metadata.timestamp}" : $"frame_{image.number}";
            if (timestampAvailable)
            {
                uint exposure = image.metadata.exposure;
                uint gain = image.metadata.gain;
                var accessoriesStr = AccesoriesToString(accessories);
                return $"{_fileNamePrefix}_{prefix}_exp_{exposure}_gain_{gain}{accessoriesStr}{JpgExtension}";

            }
            else // metadata isn't valid
            {
                return $"{prefix}{JpgExtension}";
            }
        }

        // needed since the orig image is bgr
        private BitmapSource ToBitmapSource(Bitmap bitmap)
        {
            var bitmapData = bitmap.LockBits(
                new Rectangle(0, 0, bitmap.Width, bitmap.Height),
                ImageLockMode.ReadOnly, bitmap.PixelFormat);

            var bitmapSource = BitmapSource.Create(
                bitmapData.Width, bitmapData.Height,
                bitmap.HorizontalResolution, bitmap.VerticalResolution,
                System.Windows.Media.PixelFormats.Rgb24, null,
                bitmapData.Scan0, bitmapData.Stride * bitmapData.Height, bitmapData.Stride);

            bitmap.UnlockBits(bitmapData);

            return bitmapSource;
        }


        private void DumpBitmapImage(BitmapSource bitmap, string fullPath)
        {
            var encoder = new JpegBitmapEncoder();
            encoder.QualityLevel = 100;
            encoder.Frames.Add(BitmapFrame.Create(bitmap));
            using (var stream = new FileStream(fullPath, FileMode.Create))
            {
                encoder.Save(stream);
            }
        }


    }
}