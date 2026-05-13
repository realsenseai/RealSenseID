// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

using rsid;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.Drawing.Drawing2D;
using System.Drawing.Imaging;
using System.IO;
using System.Linq;
using System.Management;
using System.Runtime.InteropServices;
using System.Text;
using System.Web.Script.Serialization;
using System.Windows.Media.Imaging;

namespace rsid_wrapper_csharp
{
    class ImageHelper
    {
        // return (result 1d array, width, height, result bitmap)
        // if resize is true  rescale and return the rescaled byte array
        public static Tuple<byte[], int, int, Bitmap> ToBgr(string filename)
        {
            var bmp = new Bitmap(filename);
            FixOrientation(bmp);            
            var arr = ToBgr(bmp);
            return Tuple.Create(arr, bmp.Width, bmp.Height, bmp);
        }
  
        public static byte[] ToBgr(Bitmap image)
        {
            var bpp = 3;
            var outStride = image.Width * bpp;
            var rgbArray = new byte[image.Height * outStride];
            var data = image.LockBits(new Rectangle(0, 0, image.Width, image.Height),
                ImageLockMode.ReadOnly, PixelFormat.Format24bppRgb);
            try
            {
                for (var lineNumber = 0; lineNumber < data.Height; lineNumber++)
                {
                    var scanOffset = lineNumber * data.Stride;
                    var outOffset = lineNumber * outStride;
                    Marshal.Copy(data.Scan0 + scanOffset, rgbArray, outOffset, outStride);
                }
            }
            finally
            {
                image.UnlockBits(data);
            }
            return rgbArray;
        }

        public static BitmapImage BitmapToImageSource(Bitmap bitmap)
        {
            using (MemoryStream memoryStream = new MemoryStream())
            {
                bitmap.Save(memoryStream, ImageFormat.Bmp);
                memoryStream.Position = 0;
                var bi = new BitmapImage();
                bi.BeginInit();
                bi.StreamSource = memoryStream;
                bi.CacheOption = BitmapCacheOption.OnLoad;
                bi.EndInit();
                return bi;
            }
        }

        public static System.Windows.Controls.Image CreateImageControl(string fileName)
        {
            var bitmap = new BitmapImage();
            using (var stream = File.OpenRead(fileName))
            {
                bitmap.BeginInit();
                bitmap.CacheOption = BitmapCacheOption.OnLoad;
                bitmap.StreamSource = stream;
                bitmap.EndInit();
            }

            return new System.Windows.Controls.Image
            {
                Source = bitmap,
                Width = bitmap.Width * 2,
                Height = bitmap.Height * 2
            };
        }

        // rotate the image to be top-left orientation if the exif is in different orientation
        public static void FixOrientation(Bitmap bmp)
        {
            const int exifOrientationId = 0x112;
            var props = bmp.PropertyIdList.ToArray();
            if (!props.Contains(exifOrientationId)) return;
            var prop = bmp.GetPropertyItem(exifOrientationId);
            var orientation = BitConverter.ToInt16(prop.Value, 0);
            if (orientation == 1) return; //top left

            // Set Orientation top left
            prop.Value = BitConverter.GetBytes((short)1);
            bmp.SetPropertyItem(prop);

            switch (orientation)
            {
                case 2:
                    bmp.RotateFlip(RotateFlipType.RotateNoneFlipX);
                    break;
                case 3:
                    bmp.RotateFlip(RotateFlipType.Rotate180FlipNone);
                    break;
                case 4:
                    bmp.RotateFlip(RotateFlipType.Rotate180FlipX);
                    break;
                case 5:
                    bmp.RotateFlip(RotateFlipType.Rotate90FlipX);
                    break;
                case 6:
                    bmp.RotateFlip(RotateFlipType.Rotate90FlipNone);
                    break;
                case 7:
                    bmp.RotateFlip(RotateFlipType.Rotate270FlipX);
                    break;
                case 8:
                    bmp.RotateFlip(RotateFlipType.Rotate270FlipNone);
                    break;
            }
        }
    }

    public struct EnrollImageRecord
    {
        public string UserId { get; set; }
        public string Filename { get; set; }
    }

    class JsonHelper
    {
        public static List<EnrollImageRecord> LoadImagesToEnroll(string filename)
        {
            using (var reader = new StreamReader(filename))
            {
                var js = new JavaScriptSerializer();
                return js.Deserialize<List<EnrollImageRecord>>(reader.ReadToEnd());
            }
        }

        public static string PrettyPrintJson(string json)
        {
            int indent = 0;
            bool quoted = false;
            var sb = new StringBuilder();
            for (int i = 0; i < json.Length; i++)
            {
                char ch = json[i];
                switch (ch)
                {
                    case '{':
                    case '[':
                        sb.Append(ch);
                        if (!quoted)
                        {
                            sb.AppendLine();
                            sb.Append(new string(' ', ++indent * 2));
                        }
                        break;
                    case '}':
                    case ']':
                        if (!quoted)
                        {
                            sb.AppendLine();
                            sb.Append(new string(' ', --indent * 2));
                        }
                        sb.Append(ch);
                        break;
                    case '"':
                        sb.Append(ch);
                        bool escaped = false;
                        int index = i;
                        while (index > 0 && json[--index] == '\\')
                            escaped = !escaped;
                        if (!escaped)
                            quoted = !quoted;
                        break;
                    case ',':
                        sb.Append(ch);
                        if (!quoted)
                        {
                            sb.AppendLine();
                            sb.Append(new string(' ', indent * 2));
                        }
                        break;
                    case ':':
                        sb.Append(ch);
                        if (!quoted)
                            sb.Append(" ");
                        break;
                    default:
                        sb.Append(ch);
                        break;
                }
            }
            return sb.ToString();
        }

        public static void SerializeStructToJsonFile(MainWindow.DeviceState data, string filePath)
        {
            try
            {
                var serializer = new JavaScriptSerializer();
                string json = serializer.Serialize(data.ToSerialized());
                string prettyJson = PrettyPrintJson(json);
                File.WriteAllText(filePath, prettyJson); // Automatically creates the file if it doesn't exist.
            }
            catch (Exception ex)
            {
                Console.WriteLine($"Error saving struct to file: {ex.Message}");
            }
        }
    }


    class Logger
    {
        private static readonly object ConsoleLock = new object();
        // Log to console with color around the "Info" text to match spdlog console format 
        public static void Log(string message)
        {
            string timestamp = DateTime.Now.ToString("yyyy-MM-dd HH:mm:ss.fff");
            lock (ConsoleLock)
            {
                Console.Write($"[{timestamp}] [");
                Console.ForegroundColor = ConsoleColor.Green;
                Console.Write("Info");
                Console.ResetColor();
                Console.WriteLine($"] [Viewer] {message}");
            }
        }
    }
}
