using System;
using System.Globalization;
using System.Windows;
using System.Windows.Data;

namespace rsid_wrapper_csharp
{
    public class ModeToVisibilityConverter : IValueConverter
    {
        public object Convert(object value, Type targetType, object parameter, CultureInfo culture)
        {
            if (value is Mode mode && parameter is string settingName)
            {
                // Logic based on the requested setting
                switch (settingName)
                {
                    case "DBManagement":
                    case "DistanceLimit":
                    case "AntiSpoofing":
                    case "FrontalFacePolicy":
                    case "MaxSpoofs":
                    case "MatchingThreshold":
                    case "GpioToggleAuth":
                    case "ROI":
                    case "FeaturesDistance": // If applicable, per request
                        // FA only
                        return (mode == Mode.Face) ? Visibility.Visible : Visibility.Collapsed;
                    case "FeaturesRect":
                    case "FeaturesLM":
                    case "FeaturesGroup":
                    case "FaceSelectionSingle":
                    case "MotionMode":
                        // FA only
                        return (mode == Mode.Face) ? Visibility.Visible : Visibility.Collapsed;

                    case "FrameDumpsFaceCropped":
                        // FA only
                        return (mode == Mode.Face) ? Visibility.Visible : Visibility.Collapsed;

                    case "FrameDumpsFull":
                        // FA and Person (Collapsed in Other)
                        return (mode == Mode.Face || mode == Mode.Person) ? Visibility.Visible : Visibility.Collapsed;

                    case "FrameDumpsDebug":
                        // Person and Other (Hidden for FA)
                        return (mode != Mode.Face) ? Visibility.Visible : Visibility.Collapsed;

                    case "OperationFace":
                        return (mode == Mode.Face) ? Visibility.Visible : Visibility.Collapsed;
                    case "OperationPerson":
                        return (mode == Mode.Person) ? Visibility.Visible : Visibility.Collapsed;
                    case "OperationOther":
                        return (mode == Mode.Other) ? Visibility.Visible : Visibility.Collapsed;

                    case "Preview":
                    case "FrameDumpsGroup":
                    case "CameraRotation":
                    case "SensorSettings":
                        // All 3 modes -> always visible
                        return Visibility.Visible;

                    default:
                        // Default fallback
                        return Visibility.Visible;
                }
            }
            // Logic for null or mismatch: default to Visible (or Collapsed if safer)
            return Visibility.Visible;
        }

        public object ConvertBack(object value, Type targetType, object parameter, CultureInfo culture)
        {
            throw new NotImplementedException();
        }
    }
}
