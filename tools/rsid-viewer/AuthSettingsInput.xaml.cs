// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

using Microsoft.Win32;
using rsid;
using System;
using System.Collections.Generic;
using System.IO;
using System.Net.Http;
using System.Reflection;
using System.Text.RegularExpressions;
using System.Threading;
using System.Threading.Tasks;
using System.Web.Script.Serialization;
using System.Windows;
using System.Windows.Input;

namespace rsid_wrapper_csharp
{
    public enum Mode
    {
        Face,
        Person,
        Other
    }

    /// <summary>
    /// Interaction logic for EnrollInput.xaml
    /// </summary>
    public partial class AuthSettingsInput : Window
    {
        private MainWindow MyMainWindow { get => (MainWindow)this.Owner; }
        private SerialConfig serialConfig;
        private CancellationTokenSource cts = new CancellationTokenSource();
        public DeviceConfig Config { get; private set; }
        public MainWindow.FlowMode FlowMode { get; private set; }
        public MainWindow.OperationMode OperationMode { get; private set; }

        // Static fields for session persistence
        private static bool _sessionIsBasicExpanded = true;
        private static bool _sessionIsAdvancedExpanded = false;
        private static bool _sessionIsFwUpdateExpanded = false;

        public bool IsBasicExpanded
        {
            get { return (bool)GetValue(IsBasicExpandedProperty); }
            set { SetValue(IsBasicExpandedProperty, value); }
        }

        public static readonly DependencyProperty IsBasicExpandedProperty =
            DependencyProperty.Register("IsBasicExpanded", typeof(bool), typeof(AuthSettingsInput),
            new PropertyMetadata(true, (d, e) => _sessionIsBasicExpanded = (bool)e.NewValue));

        public bool IsAdvancedExpanded
        {
            get { return (bool)GetValue(IsAdvancedExpandedProperty); }
            set { SetValue(IsAdvancedExpandedProperty, value); }
        }

        public static readonly DependencyProperty IsAdvancedExpandedProperty =
            DependencyProperty.Register("IsAdvancedExpanded", typeof(bool), typeof(AuthSettingsInput),
            new PropertyMetadata(false, (d, e) => _sessionIsAdvancedExpanded = (bool)e.NewValue));

        public bool IsFwUpdateExpanded
        {
            get { return (bool)GetValue(IsFwUpdateExpandedProperty); }
            set { SetValue(IsFwUpdateExpandedProperty, value); }
        }

        public static readonly DependencyProperty IsFwUpdateExpandedProperty =
            DependencyProperty.Register("IsFwUpdateExpanded", typeof(bool), typeof(AuthSettingsInput),
            new PropertyMetadata(false, (d, e) => _sessionIsFwUpdateExpanded = (bool)e.NewValue));

        public Visibility DistanceSettingsVisibility
        {
            get { return (Visibility)GetValue(DistanceSettingsVisibilityProperty); }
            set { SetValue(DistanceSettingsVisibilityProperty, value); }
        }

        public static readonly DependencyProperty DistanceSettingsVisibilityProperty =
            DependencyProperty.Register("DistanceSettingsVisibility", typeof(Visibility), typeof(AuthSettingsInput), new PropertyMetadata(Visibility.Visible));

        private void UpdateDistanceSettingsVisibility()
        {
            // Default logic: visible only if in FA team (similar to existing converter logic for FA-only settings)
            bool isVisible = (Mode == Mode.Face);

            // Special logic: Hide on F50x devices if running Face Detection Only or Recognition Only
            if (isVisible && MyMainWindow.GetDeviceType() == DeviceType.F50x)
            {
                if (Operation_DetectionOnly.IsChecked == true || Operation_RecognitionOnly.IsChecked == true)
                {
                    isVisible = false;
                }
            }

            DistanceSettingsVisibility = isVisible ? Visibility.Visible : Visibility.Collapsed;
        }

        public static readonly DependencyProperty ModeProperty =
            DependencyProperty.Register("Mode", typeof(Mode), typeof(AuthSettingsInput),
                new PropertyMetadata(Mode.Face, OnModeChanged));

        private static void OnModeChanged(DependencyObject d, DependencyPropertyChangedEventArgs e)
        {
            if (d is AuthSettingsInput window)
            {
                window.UpdateDistanceSettingsVisibility();
            }
        }

        public Mode Mode
        {
            get { return (Mode)GetValue(ModeProperty); }
            set { SetValue(ModeProperty, value); }
        }

        public PreviewConfig PreviewConfig { get; private set; }
        public string FirmwareFileName { get; private set; } = string.Empty;
        public bool ForceFirmwareUpdate { get; private set; } = false;

        public AuthSettingsInput(
            string fwVersion,
            DeviceConfig? config,
            PreviewConfig? previewConfig,
            MainWindow.FlowMode flowMode,
            MainWindow.OperationMode operationMode,
            bool previewEnabled,
            SerialConfig serialConfig,
            bool one2oneEnabled)
        {
            this.Owner = Application.Current.MainWindow;
            this.DataContext = this;
            this.serialConfig = serialConfig;

            InitializeComponent();

            IsBasicExpanded = _sessionIsBasicExpanded;
            IsAdvancedExpanded = _sessionIsAdvancedExpanded;
            IsFwUpdateExpanded = _sessionIsFwUpdateExpanded;

            // Init dialog values according to current config
            FirmwareVersionNumber.Text = fwVersion;
            OperationMode = operationMode;
            var hasConfig = config.HasValue;
            if (hasConfig)
            {
                Config = config.Value;
                FlowMode = flowMode;
                PreviewConfig = previewConfig.Value;
                UpdateUiSettingsValues(config.Value, previewConfig.Value, flowMode, operationMode);
            }

            FaceSelectionPolicySingle.IsEnabled = hasConfig;
            FaceSelectionPolicyAll.IsEnabled = hasConfig;

            Operation_All.IsEnabled = hasConfig;
            Operation_PersonOnly.IsEnabled = hasConfig && !one2oneEnabled;
            Operation_PoseOnly.IsEnabled = hasConfig && !one2oneEnabled;
            Operation_BarcodeOnly.IsEnabled = hasConfig && !one2oneEnabled;
            Operation_BodyPartOnly.IsEnabled = hasConfig && !one2oneEnabled;
            Operation_DetectionOnly.IsEnabled = hasConfig && !one2oneEnabled;
            Operation_RecognitionOnly.IsEnabled = hasConfig;
            Operation_SpoofOnly.IsEnabled = hasConfig && !one2oneEnabled;

            Feature_Rectangle.IsEnabled = hasConfig && config.Value.landmarksEnable != (byte)1;
            Feature_Landmarks.IsEnabled = hasConfig;
            Feature_Distance.IsEnabled = hasConfig;

            CameraRotation0.IsEnabled = hasConfig;
            CameraRotation180.IsEnabled = hasConfig;
            CameraRotation90.IsEnabled = hasConfig;
            CameraRotation270.IsEnabled = hasConfig;

            ServerModeYes.IsEnabled = hasConfig;
            ServerModeNo.IsEnabled = hasConfig;

            ConfidenceHigh.IsEnabled = hasConfig;
            ConfidenceEnhanced.IsEnabled = hasConfig;
            ConfidenceStandard.IsEnabled = hasConfig;

            FrontalStrict.IsEnabled = hasConfig;
            FrontalModerate.IsEnabled = hasConfig;
            FrontalNone.IsEnabled = hasConfig;

            StaticMode.IsEnabled = hasConfig;
            WalkthroughMode.IsEnabled = hasConfig;

            DistanceLimitSlider.IsEnabled = hasConfig;

            bool previewEnabledAuth = previewEnabled && hasConfig;

            PreviewModeMJPEG_1080P.IsEnabled = previewEnabledAuth;
            PreviewModeMJPEG_720P.IsEnabled = previewEnabledAuth;

            DumpModeNone.IsEnabled = previewEnabledAuth;
            DumpModeFace.IsEnabled = previewEnabledAuth;
            DumpModeFull.IsEnabled = previewEnabledAuth;
            DumpModeDebug.IsEnabled = previewEnabledAuth;

            if (MyMainWindow.GetDeviceType() == DeviceType.F45x)
            {
                ModeGrid.Visibility = Visibility.Collapsed;
            }

            UpdateDistanceSettingsVisibility();
        }

        private Mode DetermineAllowedMode(MainWindow.OperationMode mode)
        {
            switch (mode)
            {
                case MainWindow.OperationMode.All:
                case MainWindow.OperationMode.FaceDetectionOnly:
                case MainWindow.OperationMode.SpoofOnly:
                case MainWindow.OperationMode.RecognitionOnly:
                    return Mode.Face;

                case MainWindow.OperationMode.PersonDetectionOnly:
                case MainWindow.OperationMode.PoseEstimationOnly:
                case MainWindow.OperationMode.BodyPartDetectionOnly:
                    return Mode.Person;

                case MainWindow.OperationMode.BarcodeDecodingOnly:
                    return Mode.Other;

                default:
                    return Mode.Face;
            }
        }

        private void UpdateModeUi(Mode mode)
        {
            Mode = mode;

            ValidateDependentSettings(mode);
            UpdateSensorSettingsState();
        }

        private void ValidateDependentSettings(Mode mode)
        {
            // 1. Reset INVISIBLE settings to GLOBAL DEFAULTS when not in FA mode
            // (Only reset settings that become completely irrelevant/hidden for the other modes)
            if (mode != Mode.Face)
            {
                // Face Selection Strategy -> Single
                FaceSelectionPolicySingle.IsChecked = true;

                // Anti-Spoofing -> Standard
                ConfidenceStandard.IsChecked = true;

                // Frontal Face Policy -> None
                FrontalNone.IsChecked = true;

                // Distance Limit -> No Limit (0)
                DistanceLimitSlider.Value = 0;

                // Database Strategy -> On Device
                ServerModeNo.IsChecked = true;

                // Features -> Only Rectangle
                Feature_Rectangle.IsChecked = true;
                Feature_Landmarks.IsChecked = false;
                Feature_Distance.IsChecked = false;

                // Matching Threshold -> 0 (String "0" for TextBlocks/Boxes)
                MatchThresh.Text = "0";

                // Max Spoofs -> 0
                MaxSpoofs.Text = "0";

                // ROI -> 1 full-frame ROI
                if (NumRoisCombo != null) NumRoisCombo.SelectedIndex = 0;
                UpdateRoiPanelsVisibility(1);
                ResetRoiButton_Click(null, null);
            }
        }



        private void ModeSelector_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
        {
            if (ModeSelector.SelectedItem is System.Windows.Controls.ComboBoxItem item && item.Tag is string tag)
            {
                if (tag == "FA") UpdateModeUi(Mode.Face);
                else if (tag == "Person") UpdateModeUi(Mode.Person);
                else if (tag == "Other") UpdateModeUi(Mode.Other);
            }
        }

        private void RadioButton_Click(object sender, RoutedEventArgs e)
        {
            UpdateDistanceSettingsVisibility();
        }

        private string GetValidationError()
        {
            bool operationSelected = false;
            if (Mode == Mode.Face)
            {
                operationSelected = (Operation_All.IsChecked == true) ||
                                    (Operation_DetectionOnly.IsChecked == true) ||
                                    (Operation_SpoofOnly.IsChecked == true) ||
                                    (Operation_RecognitionOnly.IsChecked == true);
            }
            else if (Mode == Mode.Person)
            {
                operationSelected = (Operation_PersonOnly.IsChecked == true) ||
                                    (Operation_PoseOnly.IsChecked == true) ||
                                    (Operation_BodyPartOnly.IsChecked == true);
            }
            else if (Mode == Mode.Other)
            {
                operationSelected = Operation_BarcodeOnly.IsChecked == true;
            }

            if (!operationSelected)
            {
                return "Please select an Operation for the chosen Mode before applying.";
            }

            // Frame Dumps validation
            if (Mode == Mode.Face)
            {
                // Valid: None, Face, Full
                bool dumpValid = DumpModeDebug.IsChecked != true &&
                                 (DumpModeNone.IsChecked == true || DumpModeFace.IsChecked == true || DumpModeFull.IsChecked == true);
                if (!dumpValid) return "Please select a valid Frame Dump mode for Face: None, Face Only, or Full Frame.";
            }
            else if (Mode == Mode.Person)
            {
                // Valid: None, Full, Debug
                bool dumpValid = DumpModeFace.IsChecked != true &&
                                 (DumpModeNone.IsChecked == true || DumpModeFull.IsChecked == true || DumpModeDebug.IsChecked == true);
                if (!dumpValid) return "Please select a valid Frame Dump mode for Person: None, Full Frame, or Debug.";
            }
            else // Other
            {
                // Valid: None, Debug
                bool dumpValid = DumpModeFace.IsChecked != true && DumpModeFull.IsChecked != true &&
                                 (DumpModeNone.IsChecked == true || DumpModeDebug.IsChecked == true);
                if (!dumpValid) return "Please select a valid Frame Dump mode for Other: None or Debug.";
            }

            return null;
        }

        // Kept as a no-op so existing XAML Click="CheckCanApply" handlers stay valid;
        // form validation now happens at click time via GetValidationError().
        private void CheckCanApply(object sender, RoutedEventArgs e)
        {
        }

        private void UpdateUiSettingsValues(DeviceConfig deviceConfig, PreviewConfig previewConfig, MainWindow.FlowMode flowMode, MainWindow.OperationMode viewerAlgoFlow)
        {
            // Determine Mode from the current operation mode
            var mode = DetermineAllowedMode(viewerAlgoFlow);

            // Set ComboBox selection
            switch (mode)
            {
                case Mode.Face:
                    ModeSelector.SelectedIndex = 0;
                    break;
                case Mode.Person:
                    ModeSelector.SelectedIndex = 1;
                    break;
                case Mode.Other:
                    ModeSelector.SelectedIndex = 2;
                    break;
            }

            // Update UI visibility based on the mode
            UpdateModeUi(mode);

            // Select the correct sub-option
            Operation_All.IsChecked = viewerAlgoFlow == MainWindow.OperationMode.All;
            Operation_PersonOnly.IsChecked = viewerAlgoFlow == MainWindow.OperationMode.PersonDetectionOnly;
            Operation_PoseOnly.IsChecked = viewerAlgoFlow == MainWindow.OperationMode.PoseEstimationOnly;
            Operation_BarcodeOnly.IsChecked = viewerAlgoFlow == MainWindow.OperationMode.BarcodeDecodingOnly;
            Operation_BodyPartOnly.IsChecked = viewerAlgoFlow == MainWindow.OperationMode.BodyPartDetectionOnly;
            Operation_DetectionOnly.IsChecked = viewerAlgoFlow == MainWindow.OperationMode.FaceDetectionOnly;
            Operation_RecognitionOnly.IsChecked = viewerAlgoFlow == MainWindow.OperationMode.RecognitionOnly;
            Operation_SpoofOnly.IsChecked = viewerAlgoFlow == MainWindow.OperationMode.SpoofOnly;

            FaceSelectionPolicySingle.IsChecked = deviceConfig.faceSelectionPolicy == DeviceConfig.FaceSelectionPolicy.Single;
            FaceSelectionPolicyAll.IsChecked = deviceConfig.faceSelectionPolicy == DeviceConfig.FaceSelectionPolicy.All;

            ServerModeNo.IsChecked = flowMode == MainWindow.FlowMode.Device;
            ServerModeYes.IsChecked = flowMode == MainWindow.FlowMode.Server;

            ConfidenceHigh.IsChecked = deviceConfig.securityLevel == DeviceConfig.SecurityLevel.High;
            ConfidenceEnhanced.IsChecked = deviceConfig.securityLevel == DeviceConfig.SecurityLevel.Medium;
            ConfidenceStandard.IsChecked = deviceConfig.securityLevel == DeviceConfig.SecurityLevel.Low;

            FrontalStrict.IsChecked = deviceConfig.frontalFacePolicy == FrontalFacePolicy.Strict;
            FrontalModerate.IsChecked = deviceConfig.frontalFacePolicy == FrontalFacePolicy.Moderate;
            FrontalNone.IsChecked = deviceConfig.frontalFacePolicy == FrontalFacePolicy.None;

            StaticMode.IsChecked = deviceConfig.personMotionMode == DeviceConfig.PersonMotionMode.Static;
            WalkthroughMode.IsChecked = deviceConfig.personMotionMode == DeviceConfig.PersonMotionMode.Walkthrough;

            DistanceLimitSlider.Value = deviceConfig.distanceLimitCm;

            CameraRotation0.IsChecked = deviceConfig.cameraRotation == DeviceConfig.CameraRotation.Rotation_0_Deg;
            CameraRotation180.IsChecked = deviceConfig.cameraRotation == DeviceConfig.CameraRotation.Rotation_180_Deg;
            CameraRotation90.IsChecked = deviceConfig.cameraRotation == DeviceConfig.CameraRotation.Rotation_90_Deg;
            CameraRotation270.IsChecked = deviceConfig.cameraRotation == DeviceConfig.CameraRotation.Rotation_270_Deg;

            PreviewModeMJPEG_1080P.IsChecked = previewConfig.previewMode == PreviewMode.MJPEG_1080P;
            PreviewModeMJPEG_720P.IsChecked = previewConfig.previewMode == PreviewMode.MJPEG_720P;

            DumpModeNone.IsChecked = deviceConfig.dumpMode == DeviceConfig.DumpMode.None;
            DumpModeFace.IsChecked = deviceConfig.dumpMode == DeviceConfig.DumpMode.CroppedFace;
            DumpModeFull.IsChecked = deviceConfig.dumpMode == DeviceConfig.DumpMode.FullFrame;
            DumpModeDebug.IsChecked = deviceConfig.dumpMode == DeviceConfig.DumpMode.DebugDump;

            MaxSpoofs.Text = deviceConfig.maxSpoofs.ToString();
            MatchThresh.Text = deviceConfig.matchThresh.ToString();
            SensorExpTime.Text = deviceConfig.sensorExpTime.ToString();
            SensorGain.Text = deviceConfig.sensorGain.ToString();

            AuthGpioChk.IsChecked = deviceConfig.GpioAuthToggling == 1;

            // Populate all ROI rows
            int numRois = Math.Max(1, Math.Min(5, (int)deviceConfig.num_rois));
            NumRoisCombo.SelectedIndex = numRois - 1;
            UpdateRoiPanelsVisibility(numRois);

            SetRoiTextBoxes(0, deviceConfig.detection_rois[0]);
            SetRoiTextBoxes(1, deviceConfig.detection_rois[1]);
            SetRoiTextBoxes(2, deviceConfig.detection_rois[2]);
            SetRoiTextBoxes(3, deviceConfig.detection_rois[3]);
            SetRoiTextBoxes(4, deviceConfig.detection_rois[4]);

            Feature_Rectangle.IsChecked = deviceConfig.rectEnable == (byte)1;
            if (deviceConfig.landmarksEnable == (byte)1)
            {
                Feature_Landmarks.IsChecked = true;
                Feature_Rectangle.IsChecked = true;
                Feature_Rectangle.IsEnabled = false;
            }
            else
            {
                Feature_Landmarks.IsChecked = false;
                Feature_Rectangle.IsEnabled = true;
            }
            Feature_Distance.IsChecked = deviceConfig.distanceEnabled == 1;

            UpdateSensorSettingsState();
        }

        void QueryUiSettingsValues(out DeviceConfig deviceConfig, out PreviewConfig previewConfig, out MainWindow.FlowMode flowMode, out MainWindow.OperationMode viewerAlgoFlow)
        {
            deviceConfig = new DeviceConfig();
            deviceConfig.detection_rois = new Roi[5];
            deviceConfig.num_rois = 1;
            previewConfig = new PreviewConfig();
            viewerAlgoFlow = MainWindow.OperationMode.All;

            // algo flow
            if (Operation_All.IsChecked.GetValueOrDefault())
            {
                viewerAlgoFlow = MainWindow.OperationMode.All;
                deviceConfig.algoFlow = DeviceConfig.AlgoFlow.All;
            }
            else if (Operation_DetectionOnly.IsChecked.GetValueOrDefault())
            {
                viewerAlgoFlow = MainWindow.OperationMode.FaceDetectionOnly;
                deviceConfig.algoFlow = DeviceConfig.AlgoFlow.FaceDetectionOnly;
            }
            else if (Operation_RecognitionOnly.IsChecked.GetValueOrDefault())
            {
                viewerAlgoFlow = MainWindow.OperationMode.RecognitionOnly;
                deviceConfig.algoFlow = DeviceConfig.AlgoFlow.RecognitionOnly;
            }
            else if (Operation_SpoofOnly.IsChecked.GetValueOrDefault())
            {
                viewerAlgoFlow = MainWindow.OperationMode.SpoofOnly;
                deviceConfig.algoFlow = DeviceConfig.AlgoFlow.SpoofOnly;
            }
            // Person, Pose, BodyPart & Barcode detectors (f500 only).
            // These options are not part of the algo flow setting sent to the device since
            // they are only for viewer display the right button option
            else if (Operation_PersonOnly.IsChecked.GetValueOrDefault())
            {
                viewerAlgoFlow = MainWindow.OperationMode.PersonDetectionOnly;
                deviceConfig.algoFlow = Config.algoFlow;
            }
            else if (Operation_PoseOnly.IsChecked.GetValueOrDefault())
            {
                viewerAlgoFlow = MainWindow.OperationMode.PoseEstimationOnly;
                deviceConfig.algoFlow = Config.algoFlow;
            }
            else if (Operation_BodyPartOnly.IsChecked.GetValueOrDefault())
            {
                viewerAlgoFlow = MainWindow.OperationMode.BodyPartDetectionOnly;
                deviceConfig.algoFlow = Config.algoFlow;
            }
            else if (Operation_BarcodeOnly.IsChecked.GetValueOrDefault())
            {
                viewerAlgoFlow = MainWindow.OperationMode.BarcodeDecodingOnly;
                deviceConfig.algoFlow = Config.algoFlow;
            }

            // face selection policy (all/single)
            if (FaceSelectionPolicyAll.IsChecked.GetValueOrDefault())
                deviceConfig.faceSelectionPolicy = DeviceConfig.FaceSelectionPolicy.All;
            else
                deviceConfig.faceSelectionPolicy = DeviceConfig.FaceSelectionPolicy.Single;

            // camera rotation
            if (CameraRotation0.IsChecked.GetValueOrDefault())
                deviceConfig.cameraRotation = DeviceConfig.CameraRotation.Rotation_0_Deg;
            else if (CameraRotation90.IsChecked.GetValueOrDefault())
                deviceConfig.cameraRotation = DeviceConfig.CameraRotation.Rotation_90_Deg;
            else if (CameraRotation270.IsChecked.GetValueOrDefault())
                deviceConfig.cameraRotation = DeviceConfig.CameraRotation.Rotation_270_Deg;
            else if (CameraRotation180.IsChecked.GetValueOrDefault())
                deviceConfig.cameraRotation = DeviceConfig.CameraRotation.Rotation_180_Deg;

            // flow mode
            flowMode = ServerModeNo.IsChecked.GetValueOrDefault() ? MainWindow.FlowMode.Device : MainWindow.FlowMode.Server;

            // security level from confidence radio buttons
            if (ConfidenceHigh.IsChecked.GetValueOrDefault())
            {
                deviceConfig.securityLevel = DeviceConfig.SecurityLevel.High;
            }
            else if (ConfidenceEnhanced.IsChecked.GetValueOrDefault())
            {
                deviceConfig.securityLevel = DeviceConfig.SecurityLevel.Medium;
            }
            else if (ConfidenceStandard.IsChecked.GetValueOrDefault())
            {
                deviceConfig.securityLevel = DeviceConfig.SecurityLevel.Low;
            }

            // frontal face policy
            if (FrontalStrict.IsChecked.GetValueOrDefault())
                deviceConfig.frontalFacePolicy = FrontalFacePolicy.Strict;
            else if (FrontalModerate.IsChecked.GetValueOrDefault())
                deviceConfig.frontalFacePolicy = FrontalFacePolicy.Moderate;
            else if (FrontalNone.IsChecked.GetValueOrDefault())
                deviceConfig.frontalFacePolicy = FrontalFacePolicy.None;

            // person motion mode
            bool isPersonTeam = (viewerAlgoFlow == MainWindow.OperationMode.PersonDetectionOnly ||
                                 viewerAlgoFlow == MainWindow.OperationMode.PoseEstimationOnly ||
                                 viewerAlgoFlow == MainWindow.OperationMode.BodyPartDetectionOnly) ||
                                 (Operation_PersonOnly.IsChecked.GetValueOrDefault() ||
                                  Operation_PoseOnly.IsChecked.GetValueOrDefault() ||
                                  Operation_BodyPartOnly.IsChecked.GetValueOrDefault());

            if (WalkthroughMode.IsChecked.GetValueOrDefault() || isPersonTeam)
                deviceConfig.personMotionMode = DeviceConfig.PersonMotionMode.Walkthrough;
            else // default is static
                deviceConfig.personMotionMode = DeviceConfig.PersonMotionMode.Static;

            // distance limit (cm)
            if (DistanceSettingsVisibility != Visibility.Visible)
            {
                deviceConfig.distanceLimitCm = 0;
            }
            else
            {
                deviceConfig.distanceLimitCm = (byte)Math.Round(DistanceLimitSlider.Value);
            }

            previewConfig.portraitMode = deviceConfig.cameraRotation == DeviceConfig.CameraRotation.Rotation_0_Deg || deviceConfig.cameraRotation == DeviceConfig.CameraRotation.Rotation_180_Deg;

            if (PreviewModeMJPEG_1080P.IsChecked.GetValueOrDefault())
                previewConfig.previewMode = PreviewMode.MJPEG_1080P;
            else if (PreviewModeMJPEG_720P.IsChecked.GetValueOrDefault())
                previewConfig.previewMode = PreviewMode.MJPEG_720P;
            else // default mode
                previewConfig.previewMode = PreviewMode.RAW10_1080P;

            previewConfig.cameraNumber = this.PreviewConfig.cameraNumber;
            // dump mode
            if (DumpModeNone.IsChecked.GetValueOrDefault())
                deviceConfig.dumpMode = DeviceConfig.DumpMode.None;
            else if (DumpModeFace.IsChecked.GetValueOrDefault())
                deviceConfig.dumpMode = DeviceConfig.DumpMode.CroppedFace;
            else if (DumpModeFull.IsChecked.GetValueOrDefault())
                deviceConfig.dumpMode = DeviceConfig.DumpMode.FullFrame;
            else if (DumpModeDebug.IsChecked.GetValueOrDefault())
                deviceConfig.dumpMode = DeviceConfig.DumpMode.DebugDump;
            else // default is no dump
                deviceConfig.dumpMode = DeviceConfig.DumpMode.None;

            // max spoofs
            if (Byte.TryParse(MaxSpoofs.Text.Trim(), out byte maxSpoofs))
            {
                deviceConfig.maxSpoofs = maxSpoofs;
            }
            else
            {
                throw new Exception("Max Spoofs is invalid. Must be in range of 0-255.");
            }

            // match thresh
            if (short.TryParse(MatchThresh.Text.Trim(), out short matchThresh))
            {
                deviceConfig.matchThresh = matchThresh;
            }
            else
            {
                throw new Exception("Matching Threshold is invalid.");
            }

            // Sensor exposure settings
            if (short.TryParse(SensorExpTime.Text.Trim(), out short sensorExpTime))
            {
                deviceConfig.sensorExpTime = sensorExpTime;
            }
            else
            {
                throw new Exception("Sensor exposure time is invalid.");
            }

            if (short.TryParse(SensorGain.Text.Trim(), out short sensorGain))
            {
                deviceConfig.sensorGain = sensorGain;
            }
            else
            {
                throw new Exception("Sensor gain is invalid.");
            }

            // GPIO Auth Toggling
            deviceConfig.GpioAuthToggling = AuthGpioChk.IsChecked.GetValueOrDefault() ? 1 : 0;

            // ROI
            deviceConfig.num_rois = (byte)Math.Max(1, Math.Min(5, NumRoisCombo.SelectedIndex + 1));
            for (int i = 0; i < deviceConfig.num_rois; i++)
            {
                deviceConfig.detection_rois[i] = GetRoiFromTextBoxes(i);
            }

            // Features
            deviceConfig.rectEnable = Feature_Rectangle.IsChecked.GetValueOrDefault() ? (byte)1 : (byte)0;
            deviceConfig.landmarksEnable = Feature_Landmarks.IsChecked.GetValueOrDefault() ? (byte)1 : (byte)0;

            if (DistanceSettingsVisibility != Visibility.Visible)
            {
                // Force disable if hidden
                deviceConfig.distanceEnabled = 0;
            }
            else
            {
                deviceConfig.distanceEnabled = Feature_Distance.IsChecked.GetValueOrDefault() ? (byte)1 : (byte)0;
            }
        }

        private string GetFirmwareDirectory()
        {
            var executablePath = Path.GetDirectoryName(Assembly.GetEntryAssembly()?.Location);
            var firmwarePath = Path.Combine(Directory.GetParent(executablePath)?.FullName, "firmware");
            return Directory.Exists(firmwarePath) ? firmwarePath : executablePath;
        }

        private void LandmarksCheckbox_Click(object sender, RoutedEventArgs e)
        {
            if (Feature_Landmarks.IsChecked.GetValueOrDefault())
            {
                Feature_Rectangle.IsChecked = true;
                Feature_Rectangle.IsEnabled = false;
            }
            else
            {
                Feature_Rectangle.IsEnabled = true;
            }
        }

        private Roi GetDefaultRoi()
        {
            var roi = new Roi();
            roi.x = 0;
            roi.y = 0;
            if (CameraRotation90.IsChecked == true || CameraRotation270.IsChecked == true)
            {
                roi.width = 1920;
                roi.height = 1080;
            }
            else
            {
                roi.width = 1080;
                roi.height = 1920;
            }
            return roi;
        }

        private void SetRoiTextBoxes(int index, Roi roi)
        {
            switch (index)
            {
                case 0: RoiX.Text = roi.x.ToString(); RoiY.Text = roi.y.ToString(); RoiWidth.Text = roi.width.ToString(); RoiHeight.Text = roi.height.ToString(); break;
                case 1: Roi1X.Text = roi.x.ToString(); Roi1Y.Text = roi.y.ToString(); Roi1Width.Text = roi.width.ToString(); Roi1Height.Text = roi.height.ToString(); break;
                case 2: Roi2X.Text = roi.x.ToString(); Roi2Y.Text = roi.y.ToString(); Roi2Width.Text = roi.width.ToString(); Roi2Height.Text = roi.height.ToString(); break;
                case 3: Roi3X.Text = roi.x.ToString(); Roi3Y.Text = roi.y.ToString(); Roi3Width.Text = roi.width.ToString(); Roi3Height.Text = roi.height.ToString(); break;
                case 4: Roi4X.Text = roi.x.ToString(); Roi4Y.Text = roi.y.ToString(); Roi4Width.Text = roi.width.ToString(); Roi4Height.Text = roi.height.ToString(); break;
            }
        }

        private Roi GetRoiFromTextBoxes(int index)
        {
            var roi = new Roi();
            string x, y, w, h;
            switch (index)
            {
                case 0: x = RoiX.Text; y = RoiY.Text; w = RoiWidth.Text; h = RoiHeight.Text; break;
                case 1: x = Roi1X.Text; y = Roi1Y.Text; w = Roi1Width.Text; h = Roi1Height.Text; break;
                case 2: x = Roi2X.Text; y = Roi2Y.Text; w = Roi2Width.Text; h = Roi2Height.Text; break;
                case 3: x = Roi3X.Text; y = Roi3Y.Text; w = Roi3Width.Text; h = Roi3Height.Text; break;
                case 4: x = Roi4X.Text; y = Roi4Y.Text; w = Roi4Width.Text; h = Roi4Height.Text; break;
                default: return roi;
            }
            if (short.TryParse(x.Trim(), out short px)) roi.x = px;
            if (short.TryParse(y.Trim(), out short py)) roi.y = py;
            if (short.TryParse(w.Trim(), out short pw)) roi.width = pw;
            if (short.TryParse(h.Trim(), out short ph)) roi.height = ph;
            return roi;
        }

        private void UpdateRoiPanelsVisibility(int numRois)
        {
            RoiPanel1.Visibility = numRois >= 2 ? Visibility.Visible : Visibility.Collapsed;
            RoiPanel2.Visibility = numRois >= 3 ? Visibility.Visible : Visibility.Collapsed;
            RoiPanel3.Visibility = numRois >= 4 ? Visibility.Visible : Visibility.Collapsed;
            RoiPanel4.Visibility = numRois >= 5 ? Visibility.Visible : Visibility.Collapsed;
        }

        private void NumRoisCombo_SelectionChanged(object sender, System.Windows.Controls.SelectionChangedEventArgs e)
        {
            if (NumRoisCombo == null) return;
            int numRois = Math.Max(1, NumRoisCombo.SelectedIndex + 1);
            UpdateRoiPanelsVisibility(numRois);
            // Reset newly revealed ROI panels to defaults
            int prevNumRois = e.RemovedItems.Count > 0 ? NumRoisCombo.Items.IndexOf(e.RemovedItems[0]) + 1 : numRois;
            for (int i = prevNumRois; i < numRois; i++)
            {
                SetRoiTextBoxes(i, GetDefaultRoi());
            }
        }

        private void ResetRoiButton_Click(object sender, RoutedEventArgs e)
        {
            SetRoiTextBoxes(0, GetDefaultRoi());
        }

        private void ResetRoi1Button_Click(object sender, RoutedEventArgs e) { SetRoiTextBoxes(1, GetDefaultRoi()); }
        private void ResetRoi2Button_Click(object sender, RoutedEventArgs e) { SetRoiTextBoxes(2, GetDefaultRoi()); }
        private void ResetRoi3Button_Click(object sender, RoutedEventArgs e) { SetRoiTextBoxes(3, GetDefaultRoi()); }
        private void ResetRoi4Button_Click(object sender, RoutedEventArgs e) { SetRoiTextBoxes(4, GetDefaultRoi()); }

        private void PersonMotionMode_Click(object sender, RoutedEventArgs e)
        {
            UpdateSensorSettingsState();
        }

        private void UpdateSensorSettingsState()
        {
            // Enable if Mode is Person (regardless of current MotionMode which only updates on Apply)
            // OR if MotionMode is Walkthrough (for legacy/other cases if they exist)
            bool isPersonMode = Mode == Mode.Person;
            bool isWalkthrough = WalkthroughMode.IsChecked.GetValueOrDefault();
            bool enableSensorSettings = isPersonMode || isWalkthrough;

            if (!enableSensorSettings)
            {
                SensorExpTime.Text = "0";
                SensorGain.Text = "0";
            }
            SensorExpTime.IsEnabled = enableSensorSettings;
            SensorGain.IsEnabled = enableSensorSettings;
        }

        private void UpdateFirmwareButton_Click(object sender, RoutedEventArgs e)
        {
            var openFileDialog = new OpenFileDialog
            {
                CheckFileExists = true,
                Multiselect = false,
                InitialDirectory = GetFirmwareDirectory(),
                Title = "Select Firmware Image",
                Filter = "bin files (*.bin)|*.bin|All files (*.*)|*.*",
                FilterIndex = 1
            };
            if (openFileDialog.ShowDialog() == false)
                return;

            FirmwareFileName = openFileDialog.FileName;
            ForceFirmwareUpdate = ForceUpdateChk.IsChecked.Value;
            DialogResult = true;
        }

        private void SettingsApplyButton_Click(object sender, RoutedEventArgs e)
        {
            string validationError = GetValidationError();
            if (validationError != null)
            {
                ErrorDialog.Show("Invalid Settings", validationError);
                return;
            }

            QueryUiSettingsValues(out DeviceConfig config, out PreviewConfig previewConfig, out MainWindow.FlowMode flowMode, out MainWindow.OperationMode operationMode);
            Config = config;
            FlowMode = flowMode;
            OperationMode = operationMode;
            PreviewConfig = previewConfig;
            DialogResult = true;
        }

        private void SettingsCancelButton_Click(object sender, RoutedEventArgs e)
        {
            this.cts.Cancel();
            DialogResult = false;
        }

        private void Window_MouseDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton == MouseButton.Left)
                this.DragMove();
        }

        private static readonly string ReleaseInfoUrl = "https://github.com/realsenseai/RealSenseID/raw/refs/heads/master/release_info.json";
        private static readonly HttpClient httpClient = new HttpClient() { Timeout = TimeSpan.FromSeconds(5) };

        private void OnUpdateCheckStart()
        {
            UpdateFirmwareButton.IsEnabled = false;
            SettingsApplyButton.IsEnabled = false;
            CheckForUpdatesLink.Visibility = Visibility.Collapsed;
            CheckUpdatesBar.Visibility = Visibility.Visible;
        }

        private void OnUpdateCheckEnd()
        {
            UpdateFirmwareButton.IsEnabled = true;
            SettingsApplyButton.IsEnabled = true;
            CheckForUpdatesLink.Visibility = Visibility.Visible;
            CheckUpdatesBar.Visibility = Visibility.Collapsed;
        }

        private static string GetJsonStr(Dictionary<string, object> dict, string key)
        {
            return dict.TryGetValue(key, out var value) ? value?.ToString() ?? "" : "";
        }

        // - Inform user if update available (newer version exist in published release_info.json)
        // - Fetch published release_info.json
        // - Detect fw version of the connected device
        // - Parse version strings via System.Version and compare component-wise
        private async void CheckForUpdates_Click(object sender, RoutedEventArgs e)
        {
            OnUpdateCheckStart();
            try
            {
                var json = await httpClient.GetStringAsync(ReleaseInfoUrl);
                var js = new JavaScriptSerializer();
                var dict = js.Deserialize<Dictionary<string, object>>(json);

                var hostVerStr = GetJsonStr(dict, "host_ver");
                var fw45xStr = GetJsonStr(dict, "fw_45x");
                var fw50xStr = GetJsonStr(dict, "fw_50x");

                var deviceType = MyMainWindow.GetDeviceType();
                string remoteFwStr;
                if (deviceType == DeviceType.F45x)
                    remoteFwStr = fw45xStr;
                else if (deviceType == DeviceType.F50x)
                    remoteFwStr = fw50xStr;
                else
                    remoteFwStr = "";

                var remoteFwKnown = remoteFwStr.Length > 0;

                var remoteInfo = new ReleaseInfo
                {
                    host_ver = hostVerStr,
                    fw_ver = remoteFwStr,
                    release_url = GetJsonStr(dict, "release_url"),
                    release_notes_url = GetJsonStr(dict, "release_notes_url"),
                    fw_known = remoteFwKnown,
                };

                var localHostStr = Authenticator.Version();
                var localFwStr = FirmwareVersionNumber.Text ?? "";
                var localFwKnown = localFwStr.Length > 0;

                var localInfo = new ReleaseInfo
                {
                    host_ver = localHostStr,
                    fw_ver = localFwStr,
                    fw_known = localFwKnown,
                };

                OnUpdateCheckEnd();

                MyMainWindow.ShowLog("Remote release info:");
                MyMainWindow.ShowLog($" * host={hostVerStr}  fw_45x={fw45xStr}  fw_50x={fw50xStr}");
                MyMainWindow.ShowLog("Local release info:");
                MyMainWindow.ShowLog($" * host={localInfo.host_ver}  fw={(localFwKnown ? localInfo.fw_ver : "(no device)")}");

                new UpdateAvailableDialog(localInfo, remoteInfo).ShowDialog();
            }
            catch (Exception ex)
            {
                OnUpdateCheckEnd();
                MyMainWindow.ShowLog("Error checking for updates: " + ex.Message);
                ErrorDialog.Show("Error checking for updates", ex.Message);
            }
        }

        private void CameraRotation_Click(object sender, RoutedEventArgs e)
        {
            int numRois = NumRoisCombo != null ? Math.Max(1, NumRoisCombo.SelectedIndex + 1) : 1;
            for (int i = 0; i < numRois; i++)
                SetRoiTextBoxes(i, GetDefaultRoi());
        }
    }
}

