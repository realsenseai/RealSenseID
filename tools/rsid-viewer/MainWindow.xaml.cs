// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

using Microsoft.Win32;
using Properties;
using rsid;
using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;
using System.Windows.Threading;
using Path = System.IO.Path;

namespace rsid_wrapper_csharp
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        public enum FlowMode
        {
            Device,
            Server
        }

        public enum OperationMode
        {
            All = 0,
            FaceDetectionOnly = 1,
            SpoofOnly = 2,
            RecognitionOnly = 3,
            PersonDetectionOnly = 4,
            PoseEstimationOnly = 5,
            BarcodeDecodingOnly = 6,
            BodyPartDetectionOnly = 7
        }

        private static readonly Brush ProgressBrush = Application.Current.TryFindResource("ProgressBrush") as Brush;
        private static readonly Brush FailBrush = Application.Current.TryFindResource("FailBrush") as Brush;
        private static readonly Brush SuccessBrush = Application.Current.TryFindResource("SuccessBrush") as Brush;
        private static readonly float _updateLoopInterval = 0.05f;
        private static readonly float _userFeedbackDuration = 3.0f;
        private static readonly string _serialNumberFile = "sn.txt";

#if RSID_PREVIEW
        private bool _previewEnabled = true;
#else
        private bool _previewEnabled = false;
#endif

        private DeviceState _deviceState;
        private Authenticator _authenticator;
        private FlowMode _flowMode;
        private OperationMode _operationMode;

        private Preview _preview;
        private WriteableBitmap _previewBitmap;
        private byte[] _previewBuffer = new byte[0]; // store latest frame from the preview callback
        // tuple of (Face,IsAuthenticated,UserId) in current session
        private List<(FaceRect, FaceLandmarks?, AuthStatus?, string userId, short score, float frameScore)> _detectedFaces = new List<(FaceRect, FaceLandmarks?, AuthStatus?, string, short, float)>();
        private List<FaceLandmarks> _detectedLandmarks = new List<FaceLandmarks>();
        private object _previewMutex = new object();

        private string[] _userList = new string[0]; // latest user list that was queried from the device

        private bool _busy;
        private bool _pausePreview;
        private bool _cancelWasCalled;
        private string _lastEnrolledUserId;
        private float _lastFrameScore;
        private ArrayList _sessionHints = new ArrayList();

        private IntPtr _signatureHelpeHandle = IntPtr.Zero;
        private Database _db;// = new Database();
#if RSID_ONE2ONE
        private rsid.Faceprints _lastOne2OneEnrolledFaceprints = new rsid.Faceprints();
#endif

        private string _dumpDir;
        private ProgressBarDialog _progressBar;

        private float _userFeedbackTime;

        private int _frameCounter;
        private Timer _tempertatureTimer;
        private readonly System.Diagnostics.Stopwatch _fpsStopWatch = new System.Diagnostics.Stopwatch();
        private FrameDumper _frameDumper;
        private uint _lastDumpTs = 0;
        private Border _lastDumpBorder = null;
        private StringBuilder _logStringBuilder = new StringBuilder();
        private readonly Stopwatch _wkSuccessTimer = new Stopwatch();

        public MainWindow()
        {
            _cancelWasCalled = false;
            InitializeComponent();

            this.Dispatcher.UnhandledException += OnUnhandledException;
            CreateConsole();
            Title += $" v{Authenticator.Version()}";
#if RSID_SECURE
            Title += " (secure mode)";
#endif
            ContentRendered += MainWindow_ContentRendered;
            Closing += MainWindow_Closing;

            DispatcherTimer timer = new DispatcherTimer();
            timer.Interval = TimeSpan.FromMilliseconds(_updateLoopInterval * 1000);
            timer.Tick += Timer_Tick;
            timer.Start();

            // Log timer
            var logTimer = new DispatcherTimer();
            logTimer.Interval = TimeSpan.FromMilliseconds(250);
            logTimer.Tick += Log_Tick;
            logTimer.Start();

            _fpsStopWatch.Start();
            if (_previewEnabled == false)
                LabelPreview.Visibility = Visibility.Collapsed;
        }

        private void Log_Tick(object sender, EventArgs e)
        {
            var textToAppend = string.Empty;
            lock (_logStringBuilder)
            {
                if (_logStringBuilder.Length == 0) return;
                textToAppend = _logStringBuilder.ToString();
                _logStringBuilder.Clear();
            }
            LogTextBox.AppendText(textToAppend);
            // keep latest ~10kb only
            const int maxLogLength = 10 * 1024;
            const int slack = 5 * 1024;
            if (LogTextBox.Text.Length > maxLogLength + slack)
            {
                Logger.Log("Trimming log..");
                var text = LogTextBox.Text.Substring(LogTextBox.Text.Length - maxLogLength);
                LogTextBox.Clear();
                LogTextBox.AppendText("...\n");
                LogTextBox.AppendText(text);
            }

            // Auto-scroll only if user hasn't scrolled up
            var isAtBottom = LogScroll.ScrollableHeight - LogScroll.VerticalOffset <= 1.0;
            if (isAtBottom)
            {
                LogScroll.ScrollToBottom();
            }
        }

        private void OnUnhandledException(object sender, DispatcherUnhandledExceptionEventArgs e)
        {
            var message = $"Exception occurred:\n{e.Exception.Message}";
            ShowWindowDialog(new ErrorDialog("Error", message));
            e.Handled = true;
        }

        private void MainWindow_ContentRendered(object sender, EventArgs e)
        {
            // load serial port and preview configuration
            LoadConfig();
            ThreadPool.QueueUserWorkItem(InitialSession);
        }

        private void MainWindow_Closing(object sender, EventArgs e)
        {
            _tempertatureTimer?.Dispose();
            _preview?.Stop();
            if (_busy)
            {
                try
                {
                    _cancelWasCalled = true;
                    _authenticator.Cancel();
                    Thread.Sleep(500); // give time to device to cancel before exiting

                }
                catch
                {
                    // ignored
                }
            }
        }

        private void EnrollButton_Click(object sender, RoutedEventArgs e)
        {
            var enrollInput = new EnrollInput();
            if (ShowWindowDialog(enrollInput) == true)
            {
                if (ShowWindowDialog(new EnrollInstructions()) == true)
                {
                    EnrollPanel.Visibility = Visibility.Visible;

                    if (_flowMode == FlowMode.Server)
                        ThreadPool.QueueUserWorkItem(EnrollExtractFaceprintsJob, enrollInput.Username);
                    else
                        ThreadPool.QueueUserWorkItem(EnrollJob, enrollInput.Username);
                }
            }
        }

        private void EnrollImgButton_Click(object sender, RoutedEventArgs e)
        {
            var enrollInput = new EnrollInput();
            if (ShowWindowDialog(enrollInput).GetValueOrDefault() == false)
                return;
            var openFileDialog = new OpenFileDialog
            {
                CheckFileExists = true,
                Multiselect = false,
                Title = "Select Image to Enroll",
                Filter = "Images|*.png;*.jpg;*.jpeg;*.bmp;",
                FilterIndex = 1
            };
            if (openFileDialog.ShowDialog() == false)
                return;
            var enrollData = new EnrollImageRecord
            {
                UserId = enrollInput.Username,
                Filename = openFileDialog.FileName
            };
            if (FlowMode.Server == _flowMode)
            {
#if RSID_ONE2ONE
                if (OneToOneToggle.IsChecked.GetValueOrDefault())
                    Task.Run(() => EnrollImageOne2OneHostJob(enrollData, false));
                else
#endif
                Task.Run(() => EnrollImageHostJob(enrollData, false));
            }
            else
            {
#if RSID_ONE2ONE
                if (OneToOneToggle.IsChecked.GetValueOrDefault())
                    Task.Run(() => EnrollImageOneToOneJob(enrollData, false));
                else
#endif
                Task.Run(() => EnrollImageJob(enrollData, false));
            }
        }

        private async void BatchEnrollImgButton_Click(object sender, RoutedEventArgs e)
        {
            try
            {
                var openFileDialog = new OpenFileDialog
                {
                    Multiselect = false,
                    Title = "Select File to enroll",
                    Filter = "json files (*.json)|*.json",
                    FilterIndex = 1
                };

                if (openFileDialog.ShowDialog() == false)
                    return;

                var enrollList = JsonHelper.LoadImagesToEnroll(openFileDialog.FileName);
                if (enrollList.Count == 0)
                {
                    throw new Exception("Empty enroll list");
                }
                _progressBar = new ProgressBarDialog { DialogTitle = { Text = "Enrolling" } };
                _progressBar.Show();
                var counter = 0;
                var successCounter = 0;
                var sw = System.Diagnostics.Stopwatch.StartNew();
                foreach (var record in enrollList)
                {
                    counter++;
                    var basename = Path.GetFileName(record.Filename);
                    _progressBar.DialogTitle.Text = $"{counter}/{enrollList.Count} {basename}...";
                    var progress = ((float)counter) / enrollList.Count;
                    _progressBar.Update(progress * 100);
                    // perform the enroll
                    var notLast = counter < enrollList.Count;
                    var success = true;
                    if (FlowMode.Server == _flowMode)
                    {
                        success = await Task.Run(() => EnrollImageHostJob(record, notLast));
                    }
                    else
                    {
                        success = await Task.Run(() => EnrollImageJob(record, notLast));
                    }

                    if (success)
                    {
                        successCounter++;
                    }

                    await Task.Delay(1000);
                }

                var summary =
                    $"Total:   {counter}\nSucceed: {successCounter}\nElapsed: {sw.Elapsed.Hours}:{sw.Elapsed.Minutes}:{sw.Elapsed.Seconds}";
                ShowErrorMessage($"Enroll Summary", summary);
            }
            catch (Exception ex)
            {
                ShowErrorMessage("Failed loading json", ex.Message.Substring(0, 300) + "\n...");
            }
            finally
            {
                CloseProgressBar();
            }
        }

        private void PowerJob(Object threadContext)
        {
            var mode = (PowerDialog.PowerMode)threadContext;
            if (!ConnectAuth()) return;

            try
            {
                OnStartSession(mode.ToString(), false);
                Status status = Status.Error;
                if (mode == PowerDialog.PowerMode.Standby)
                {
                    status = _authenticator.Standby();
                }
                else if (mode == PowerDialog.PowerMode.Hibernate)
                {
                    status = _authenticator.Hibernate();
                }
                else if (mode == PowerDialog.PowerMode.MountAndDump)
                {
                    ShowProgressTitle("Please Wait..");
                    status = _authenticator.DumpAndMount();
                }
                if (status == Status.Ok)
                {
                    var title = mode.ToString();
                    if (mode == PowerDialog.PowerMode.MountAndDump)
                    {
                        title = "Mount Mode";
                    }
                    ShowTitle(title, Brushes.DarkGray);
                    NormalDispatch(() =>
                    {
                        // Create a dark overlay over the preview
                        var darkOverlay = new System.Windows.Shapes.Rectangle
                        {
                            Fill = new SolidColorBrush(Color.FromArgb(145, 0, 0, 0)),
                            Width = PreviewCanvas.ActualWidth,
                            Height = PreviewCanvas.ActualHeight
                        };

                        PreviewCanvas.Children.Add(darkOverlay);
                    });
                }
                else
                {
                    ShowFailedTitle(status.ToString());
                    ShowLog(status.ToString());
                }

            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                OnStopSession();
                _authenticator.Disconnect();
            }

        }


        private void CancelEnrollButton_Click(object sender, RoutedEventArgs e)
        {
            ThreadPool.QueueUserWorkItem(CancelJob);
            EnrollPanel.Visibility = Visibility.Collapsed;
        }

        private void AuthenticateButton_Click(object sender, RoutedEventArgs e)
        {
            AuthenticationPanel.Visibility = Visibility.Visible;
            bool isLoop = AuthenticateLoopToggle.IsChecked.GetValueOrDefault() && !OneToOneToggle.IsChecked.GetValueOrDefault();
            if (_flowMode == FlowMode.Server)
            {
                if (isLoop)
                {
                    ThreadPool.QueueUserWorkItem(AuthenticateExtractFaceprintsLoopJob);
                }
                else
                {
#if RSID_ONE2ONE
                    if (OneToOneToggle.IsChecked.GetValueOrDefault())
                        ThreadPool.QueueUserWorkItem(AuthenticateExtractFaceprintsOne2OneJob);
                    else
#endif
                    ThreadPool.QueueUserWorkItem(AuthenticateExtractFaceprintsJob);
                }
            }
            else
            {
                if (isLoop)
                {
                    ThreadPool.QueueUserWorkItem(AuthenticateLoopJob);
                }
                else
                {
#if RSID_ONE2ONE
                    if (OneToOneToggle.IsChecked.GetValueOrDefault())
                        ThreadPool.QueueUserWorkItem(AuthenticateOneToOneJob);
                    else
#endif //RSID_ONE2ONE
                    ThreadPool.QueueUserWorkItem(AuthenticateJob);
                }
            }
        }

        private void UsersListView_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            DeleteButton.IsEnabled = UsersListView.SelectedItems.Count > 0;
        }

        private void SelectAllUsersCheckBox_Checked(object sender, RoutedEventArgs e)
        {
            UsersListView.SelectAll();
        }

        private void SelectAllUsersCheckBox_Unchecked(object sender, RoutedEventArgs e)
        {
            UsersListView.UnselectAll();
        }

        private void CancelAuthenticationButton_Click(object sender, RoutedEventArgs e)
        {
            ThreadPool.QueueUserWorkItem(CancelJob);
            AuthenticationPanel.Visibility = Visibility.Collapsed;
        }

        private void DeleteButton_Click(object sender, RoutedEventArgs e)
        {
            List<string> usersToDelete = UsersListView.SelectedItems.Cast<string>().ToList();
            bool deleteAll = _userList.Length == usersToDelete.Count;

            if (ShowWindowDialog(new DeleteUserInput()) == true)
            {
                if (_flowMode == FlowMode.Server)
                {
                    if (deleteAll)
                    {
                        ThreadPool.QueueUserWorkItem(DeleteUsersServerJob);
                    }
                    else
                    {
                        ThreadPool.QueueUserWorkItem(DeleteSingleUserServerJob, usersToDelete);
                    }
                }
                else
                {
                    if (deleteAll)
                    {
                        ThreadPool.QueueUserWorkItem(DeleteUsersJob);
                    }
                    else
                    {
                        ThreadPool.QueueUserWorkItem(DeleteSingleUserJob, usersToDelete);
                    }
                }

            }
        }

        private void SettingsButton_Click(object sender, RoutedEventArgs e)
        {
            // different behavior when in recovery/operational modes
            DeviceConfig? deviceConfig = null;
            if (_deviceState.IsOperational)
            {
                // device is in operational mode, we continue to query config as usual
                if (!ConnectAuth()) return;
                deviceConfig = QueryDeviceConfig();
                _authenticator.Disconnect();
            }
            else
            {
                // device is in recovery mode, we attempt to detect its settings again
                try
                {
                    _deviceState = DetectDevice();
                }
                catch (Exception ex)
                {
                    OnStopSession();
                    ShowErrorMessage("Connection Error", ex.Message);
                    return;
                }
            }


            var dialog = new AuthSettingsInput(
                _deviceState.FirmwareVersion,
                deviceConfig,
                _deviceState.PreviewConfig,
                _flowMode,
                _operationMode,
                _previewEnabled,
                _deviceState.SerialConfig,
                OneToOneToggle.IsChecked.GetValueOrDefault());

            if (ShowWindowDialog(dialog) == true)
            {
                if (string.IsNullOrEmpty(dialog.FirmwareFileName) == true)
                {
                    ThreadPool.QueueUserWorkItem(SetDeviceConfigJob, (deviceConfig, dialog.Config, _deviceState.PreviewConfig, dialog.PreviewConfig, dialog.FlowMode, dialog.OperationMode));
                }
                else
                {
                    TabsControl.SelectedIndex = 1;  // Switch to logs tab
                    _progressBar = new ProgressBarDialog();
                    ThreadPool.QueueUserWorkItem(FwUpdateJob,
                        new Tuple<string, bool>(dialog.FirmwareFileName, dialog.ForceFirmwareUpdate));
                }
            }
        }

        private void ClearLogButton_Click(object sender, RoutedEventArgs e)
        {
            ClearLog();
        }

        private void OpenConsoleToggle_Click(object sender, RoutedEventArgs e)
        {
            ToggleConsoleAsync(OpenConsoleToggle.IsChecked.GetValueOrDefault());
        }

        private async void FetchDeviceLog_Click(object sender, RoutedEventArgs e)
        {
            var sfd = new SaveFileDialog
            {
                Title = "Save As",
                FileName = $"{_deviceState.deviceType}.log",
                InitialDirectory = Path.GetFullPath(_dumpDir),
                DefaultExt = ".log",
                Filter = "Log files (*.log)|*.log|Text documents (*.txt)|*.txt|All files (*.*)|*.*"
            };
            if (sfd.ShowDialog() == false)
            {
                return;
            }
            OnStartSession("Fetching Device Log..", false);
            ShowProgressTitle("Fetching Log...");
            try
            {
                var log = await Task.Run(() =>
                {
                    using (var deviceController = new DeviceController(_deviceState.deviceType))
                    {
                        var status = deviceController.Connect(_deviceState.SerialConfig);
                        if (status != Status.Ok)
                        {
                            throw new Exception("Connection failed " + status);
                        }
                        return deviceController.FetchLog();
                    }
                });
                ShowSuccessTitle("Done");
                File.WriteAllText(sfd.FileName, log);
                OnStopSession();
                if (ShowWindowDialog(new OKCancelDialog("Open File?", "Open this file with the text editor?", true)) == true)
                {
                    Process.Start(new ProcessStartInfo(sfd.FileName) { UseShellExecute = true });
                }
            }
            catch (Exception ex)
            {
                ShowFailedTitle("Fetch Error");
                ShowErrorMessage("Error", ex.Message);
            }
            finally
            {
                OnStopSession();
            }
        }

        private void ExportDatabaseJob(string dbfilename)
        {

            if (!ConnectAuth()) return;
            _busy = true;
            OnStartSession("Exporting..", false);
            try
            {
                ShowProgressTitle("Exporting..");
                var db = new Database(dbfilename);
                var exportedDb = _authenticator.GetUsersFaceprints();
                if (exportedDb == null)
                {
                    throw new Exception("Error while exporting users");
                }

                foreach (var uf in exportedDb)
                {
                    db.Push(uf.faceprints, uf.userID);
                }
                db.Save();

                if (exportedDb.Count > 0)
                {
                    ShowSuccessTitle($"Exported {exportedDb.Count} users");
                }
                else
                {
                    ShowFailedTitle("No users were exported");
                    ShowErrorMessage("Export DB", "Error while exporting users");
                }
            }
            finally
            {
                OnStopSession();
                _authenticator.Disconnect();
                _busy = false;
            }
        }

        private void ExportButton_Click(object sender, RoutedEventArgs e)
        {
            SaveFileDialog sfd = new SaveFileDialog
            {
                Title = "Select File export DB to",
                Filter = "db files (*.db)|*.db",
                FilterIndex = 1
            };
            if (sfd.ShowDialog() == false)
                return;

            Task.Run(() => ExportDatabaseJob(sfd.FileName));
        }

        private void ImportDatabaseJob(string dbfilename)
        {
            try
            {
                _busy = true;
                OnStartSession("Import DB..", false);
                ShowProgressTitle("Reading DB..");
                var db = new Database(dbfilename);
                if (db.Load() < 0)
                {
                    ShowFailedTitle("Invalid DB");
                    ShowErrorMessage("Import DB", "Error while importing users");
                    return;
                }

                if (!ConnectAuth()) return;
                ShowProgressTitle($"Importing {db.FaceprintsArray.Count} users..");
                var usersFromDb = new List<UserFaceprints>();

                var uf = new UserFaceprints();
                foreach (var (faceprintsDb, userIdDb) in db.FaceprintsArray)
                {
                    uf.userID = userIdDb;
                    uf.faceprints = faceprintsDb;
                    usersFromDb.Add(uf);
                }

                var status = _authenticator.SetUsersFaceprints(usersFromDb);
                if (status == Status.Ok)
                {
                    ShowSuccessTitle("All users imported successfully!");
                }
                else
                {
                    ShowFailedTitle($"{status}");
                    ShowErrorMessage("Import DB", $"Import Error: {status}");
                }
                RefreshUserList();
            }
            catch (Exception ex)
            {
                ShowErrorMessage("Import DB", "Error while importing users!");
                ShowLog("Exception while inserting users: " + ex.Message);
                ShowFailedTitle("Some users were not imported");
            }
            finally
            {
                OnStopSession();
                _authenticator.Disconnect();
                _busy = false;
            }
        }


        private void ImportButton_Click(object sender, RoutedEventArgs e)
        {
            var openFileDialog = new OpenFileDialog
            {
                Multiselect = false,
                Title = "Select File to import from",
                Filter = "db files (*.db)|*.db",
                FilterIndex = 1
            };

            if (openFileDialog.ShowDialog() == false)
                return;

            var dbfilename = openFileDialog.FileName;


            Task.Run(() => ImportDatabaseJob(dbfilename));

        }



        private void TogglePreviewOpacity(bool isActive)
        {
            NormalDispatch(() =>
            {
                PreviewImage.Opacity = isActive ? 1.0 : 0.85;
                LabelPreviewInfo.Opacity = isActive ? 0.66 : 0.3;
            });
        }


        // invoke and wait for visibily change
        private void InvokePreviewVisibility(Visibility visibility)
        {
            Dispatcher.Invoke(() => SetPreviewVisibility(visibility));
        }

        private void SetPreviewVisibility(Visibility visibility)
        {
            var previewMode = _deviceState.PreviewConfig.previewMode;
            LabelPreview.Content = $"Camera Preview\n({previewMode.ToString().ToLower()} preview mode)";
            PreviewImage.Visibility = visibility;
        }

        private void PreviewImage_MouseDown(object sender, System.Windows.Input.MouseButtonEventArgs e)
        {
            if (_deviceState.PreviewConfig.previewMode == PreviewMode.RAW10_1080P || !_deviceState.IsOperational)
                return;

            LabelPlayStop.Visibility = LabelPlayStop.Visibility == Visibility.Visible ? Visibility.Hidden : Visibility.Visible;
            if (LabelPlayStop.Visibility == Visibility.Hidden)
            {
                ResumePreviewAfter(100);
                TogglePreviewOpacity(true);
            }
            else
            {
                PausePreview();
                TogglePreviewOpacity(false);
            }
        }

        private void Timer_Tick(object sender, EventArgs e)
        {
            if (_userFeedbackTime > 0)
            {
                _userFeedbackTime -= _updateLoopInterval;
                if (_userFeedbackTime < 2.0f)
                {
                    UserFeedbackContainer.Opacity -= _updateLoopInterval * 0.5;
                    PreviewCanvas.Opacity -= _updateLoopInterval * 0.5;
                }
                if (_userFeedbackTime <= 0)
                {
                    ClearTitle();
                    ResetOverlay();
                    PreviewCanvas.Visibility = Visibility.Hidden;
                }
            }
        }

        private void BackgroundDispatch(Action action)
        {
            Dispatcher.BeginInvoke(action, DispatcherPriority.Background, null);
        }

        private void BindDispatch(Action action)
        {
            Dispatcher.BeginInvoke(action, DispatcherPriority.DataBind, null);
        }

        // Dispatch with Normal priority
        private void NormalDispatch(Action action)
        {
            try
            {
                Dispatcher.BeginInvoke(action, DispatcherPriority.Normal, null);
            }
            catch (Exception ex)
            {
                Console.WriteLine("NormalDispatch: " + ex.Message);
            }
        }

        private void DumpDispatch(Action action)
        {
            try
            {
                Dispatcher.BeginInvoke(action, DispatcherPriority.Normal, null);
            }
            catch (Exception ex)
            {
                Console.WriteLine("DumpDispatch: " + ex.Message);
            }
        }

        private bool? ShowWindowDialog(Window window)
        {
            SetUiEnabled(false);
            bool? returnOk = window.ShowDialog();
            SetUiEnabled(true);
            return returnOk;
        }

        private void ShowErrorMessage(string title, string message)
        {
            Dispatcher.Invoke(() => (ShowWindowDialog(new ErrorDialog(title, message))));
        }

        private void ClearLog()
        {
            LogTextBox.Clear();
        }

        private void ShowLogTitle(string title)
        {
            var timestamp = DateTime.Now.ToString("HH:mm:ss.fff");
            ShowLog($"\n[{timestamp}] {title}\n===================");
        }

        public Authenticator GetAuthenticator()
        {
            return _authenticator;
        }

        public DeviceType GetDeviceType()
        {
            return _deviceState.deviceType;
        }

        public void ShowLog(string message)
        {
            lock (_logStringBuilder)
            {
                _logStringBuilder.AppendLine($"{message}");
            }
        }

        private void ShowTitle(string message, Brush color, float duration = 0)
        {
            NormalDispatch(() =>
            {
                _userFeedbackTime = duration;
                UserFeedbackText.Text = message;
                UserFeedbackPanel.Background = color;
                UserFeedbackContainer.Visibility = Visibility.Visible;
                UserFeedbackContainer.Opacity = (_detectedFaces.Count <= 1) ? 1.0 : 0.0f; // show title only if single face or person
                OutputText.Text = message;
            });
        }

        private void ClearTitle()
        {
            NormalDispatch(() => UserFeedbackContainer.Visibility = Visibility.Collapsed);
        }

        private void ShowSuccessTitle(string message)
        {
            ShowTitle(message, SuccessBrush, _userFeedbackDuration);
        }

        private void ShowFailedTitle(string message)
        {
            ShowTitle(message, FailBrush, _userFeedbackDuration);
        }

        private string GetFailedSpoofMsg(AuthStatus status)
        {
            //var msg = status.ToString();
            if ((int)status >= (int)AuthStatus.Spoof_2D || (int)status == (int)AuthStatus.Forbidden)
                return "Spoof Attempt";
            else if (status == AuthStatus.NoFaceDetected)
                return "No valid face detected";
            else
                return status.ToString();
        }

        private void ShowProgressTitle(string message, float duration = 0)
        {
            ShowTitle(message, ProgressBrush, duration);
        }

        private void VerifyResult(bool result, string successMessage, string failMessage, Action onSuccess = null)
        {
            if (result)
            {
                ShowSuccessTitle(successMessage);
                ShowLog(successMessage);
                onSuccess?.Invoke();
            }
            else
            {
                ShowFailedTitle(failMessage);
                ShowLog(failMessage);
            }
        }

        private void VerifyResultAuth(AuthStatus status, string successMessage, string failMessage, Action onSuccess = null, string userId = null, short score = 0)
        {
            // provide option to unlock device if locked by too many spoof attempts
            if (status == AuthStatus.TooManySpoofs)
            {
                NormalDispatch(async () =>
                {
                    await Task.Delay(750);
                    var dialog = new OKCancelDialog("Too Many Spoof Attempts!", "The device is locked due to multiple spoof attempts.\nUnlock the device?");
                    var dialogResult = ShowWindowDialog(dialog);
                    if (dialogResult == true)
                    {
                        ThreadPool.QueueUserWorkItem(UnlockJob);
                    }
                });
            }

            // Walkthrough mode: show only specific statuses
            if (IsWalkthroughMode())
            {
                switch (status)
                {
                    case AuthStatus.Success:
                        _wkSuccessTimer.Restart();
                        VerifyResult(true, successMessage, failMessage, onSuccess);
                        break;
                    case AuthStatus.Forbidden:
                        // only show forbidden if more than 1 second passed since last success
                        bool showForbidden = _wkSuccessTimer.IsRunning == false || _wkSuccessTimer.Elapsed.TotalSeconds >= 1;
                        if (showForbidden)
                        {
                            VerifyResult(false, successMessage, failMessage, onSuccess);
                        }
                        else
                        {
                            ShowProgressTitle("Scanning..", _userFeedbackDuration);
                        }
                        break;

                    case AuthStatus.DeviceError:
                    case AuthStatus.Failure:
                    case AuthStatus.TooManySpoofs:
                    case AuthStatus.Serial_Error:
                    case AuthStatus.Serial_SerialError:
                    case AuthStatus.Serial_SecurityError:
                    case AuthStatus.Serial_VersionMismatch:
                    case AuthStatus.Serial_CrcError:
                        VerifyResult(false, successMessage, failMessage, onSuccess);
                        break;
                    default:
                        ShowProgressTitle("Scanning...", _userFeedbackDuration);
                        break;
                }
            }
            else
            {
                // normal mode: show all statuses
                VerifyResult(status == AuthStatus.Success, successMessage, failMessage, onSuccess);
            }
            UpdateFaceResult(status, userId, score);
        }

        private void UpdateFaceResult(AuthStatus status, string userId, short score)
        {
            // updated the detected face success value if exists
            NormalDispatch(() =>
            {
                //find the next face that didn't get a result yet and update it
                for (var i = 0; i < _detectedFaces.Count; i++)
                {
                    var face = _detectedFaces[i];

                    if (!face.Item3.HasValue)
                    {
                        if (_detectedLandmarks.Count > i)
                            _detectedFaces[i] = (face.Item1, _detectedLandmarks[i], status, userId, score, _lastFrameScore);
                        else
                            _detectedFaces[i] = (face.Item1, null, status, userId, score, _lastFrameScore);
                        break;
                    }
                }
                RenderDetectedFaces();
            });
        }


        private void UpdateProgressBar(float progress)
        {
            NormalDispatch(() =>
            {
                _progressBar.Update(progress);
            });
        }

        private void CloseProgressBar()
        {
            NormalDispatch(() =>
            {
                _progressBar?.Close();
                _progressBar = null;
            });
        }

        private void UpdateAuthButtonText()
        {
            string text;
            switch (_operationMode)
            {
                case OperationMode.All:
                    text = "AUTHENTICATE";
                    break;
                case OperationMode.SpoofOnly:
                    text = "DETECT SPOOF";
                    break;
                case OperationMode.FaceDetectionOnly:
                    text = "DETECT FACE";
                    break;
                case OperationMode.RecognitionOnly:
                    text = "RECOGNIZE FACE";
                    break;
                case OperationMode.PersonDetectionOnly:
                    text = "DETECT PERSON";
                    break;
                case OperationMode.PoseEstimationOnly:
                    text = "DETECT POSE";
                    break;
                case OperationMode.BarcodeDecodingOnly:
                    text = "DECODE BARCODE";
                    break;
                case OperationMode.BodyPartDetectionOnly:
                    text = "DETECT TAILGATE";
                    break;
                default:
                    text = "AUTHENTICATE";
                    break;
            }
            AuthenticateButton.Content = text;
        }
        private void SetUiEnabled(bool isEnabled)
        {
            bool isRecogEnabled = IsRecogEnabled();

            SettingsButton.IsEnabled = isEnabled;
            // Prevent any other operatation if device is not compatible
            if (!_deviceState.IsCompatible)
                isEnabled = false;
            DeleteButton.IsEnabled = isEnabled && isRecogEnabled && UsersListView.SelectedItems.Count > 0;
            ImportButton.IsEnabled = isEnabled && isRecogEnabled && (_flowMode != FlowMode.Server);
            ExportButton.IsEnabled = isEnabled && isRecogEnabled && (_flowMode != FlowMode.Server);
            EnrollButton.IsEnabled = isEnabled && isRecogEnabled;
            EnrollImgButton.IsEnabled = isEnabled && isRecogEnabled;
            BatchEnrollButton.IsEnabled = isEnabled && isRecogEnabled;
            PairButton.IsEnabled = isEnabled;
            UnpairButton.IsEnabled = isEnabled;
            ClearLogButton.IsEnabled = isEnabled;
            FetchDeviceLogButton.IsEnabled = isEnabled;
            AuthenticateButton.IsEnabled = isEnabled;
            AuthenticateLoopToggle.IsEnabled = isEnabled;

#if RSID_ONE2ONE
            OneToOneToggle.Visibility = Visibility.Visible;
            if (_operationMode == OperationMode.FaceDetectionOnly || _operationMode == OperationMode.SpoofOnly)
            {
                OneToOneToggle.IsEnabled = false;
            }
            else
            {
                OneToOneToggle.IsEnabled = isEnabled;
            }
            ToggleOneToOne(isEnabled);
#endif
            UsersListView.IsEnabled = isEnabled && isRecogEnabled;
            SelectAllUsersCheckBox.IsEnabled = isEnabled && isRecogEnabled && _userList?.Length > 0;

            UpdateAuthButtonText();
        }

        private bool IsRecogEnabled()
        {
            return _operationMode == OperationMode.All ||
                            _operationMode == OperationMode.RecognitionOnly;
        }

        private FlowMode StringToFlowMode(string flowModeString)
        {
            if (flowModeString == "SERVER")
            {
                ShowLog("Server Mode");
                return FlowMode.Server;
            }
            else if (flowModeString == "DEVICE")
            {
                ShowLog("Device Mode\n");
                return FlowMode.Device;
            }

            ShowFailedTitle("Mode " + flowModeString + " not supported, using Device Mode instead");
            ShowLog("Device Mode\n");
            return FlowMode.Device;
        }

        private void LoadConfig()
        {
            _dumpDir = Settings.Default.DumpDir;

            _flowMode = StringToFlowMode(Settings.Default.FlowMode.ToUpper());

            if (_flowMode == FlowMode.Server)
            {
                ImportButton.IsEnabled = false;
                ExportButton.IsEnabled = false;

                int loadStatus = _db.Load();

                if (loadStatus < 0)
                {
                    HandleDbErrorServer();
                    ShowLog("Error occured during loading the DB. This may be due to faceprints version mismatch or other error. Saved the old DB to backup and started an empty DB.\n");
                    string guimsg = "DB version or load error.";
                    VerifyResult(false, string.Empty, guimsg);
                }
            }
        }

        // Create authenticator
        private Authenticator CreateAuthenticator()
        {
            if (_deviceState.deviceType == DeviceType.Unknown)
            {
                _deviceState.deviceType = Discover.DiscoverDeviceType(_deviceState.SerialConfig.port);
                if (_deviceState.deviceType == DeviceType.Unknown)
                {
                    throw new Exception("Device type is unknown");
                }
            }

#if RSID_SECURE
            _signatureHelpeHandle = rsid_create_example_sig_clbk();
            var sigCallback = (SignatureCallback)Marshal.PtrToStructure(_signatureHelpeHandle, typeof(SignatureCallback));
            return new Authenticator(sigCallback, _deviceState.deviceType);
#else
            return new Authenticator(_deviceState.deviceType);
#endif //RSID_SECURE

        }

        private bool ConnectAuth()
        {
            if (_authenticator == null)
            {
                _authenticator = CreateAuthenticator();
            }
            // Try 3 times to connect before giving up
            for (int i = 0; i < 3; i++)
            {
                ShowLog($"Connecting to port {_deviceState.SerialConfig.port}");
                if (_authenticator.Connect(_deviceState.SerialConfig) == Status.Ok)
                {
                    return true;
                }
                Thread.Sleep(250);
            }
            ShowFailedTitle("Connection Error");
            ShowLog("Connection error");
            ShowErrorMessage($"Connection Failed to Port {_deviceState.SerialConfig.port}",
                $"Connection Error.\n\nPlease check the serial port setting in the config file.");
            return false;
        }

        private void LogDeviceConfig(DeviceConfig deviceConfig)
        {
            ShowLog(" * Camera Rotation: " + deviceConfig.cameraRotation.ToString());
            ShowLog(" * AntiSpoof Level: " + deviceConfig.securityLevel.ToString());
            ShowLog(" * Algo Flow: " + deviceConfig.algoFlow);
            ShowLog(" * Face Selection: " + deviceConfig.faceSelectionPolicy);
            ShowLog(" * Dump Mode: " + deviceConfig.dumpMode.ToString());
            ShowLog(" * Host Mode: " + _flowMode);
            ShowLog(" * Camera Index: " + _deviceState.PreviewConfig.cameraNumber);
            ShowLog(" * Preview Mode: " + _deviceState.PreviewConfig.previewMode);
            ShowLog(" * Person Motion Mode: " + deviceConfig.personMotionMode.ToString());
            ShowLog(" * Matcher Confidence Level: " + deviceConfig.matcherConfidenceLevel.ToString());
            ShowLog(" * Max Spoofs: " + deviceConfig.maxSpoofs.ToString());
            ShowLog(" * Matching Threshold: " + deviceConfig.matchThresh.ToString());
            ShowLog(" * Sensor Exposure Time: " + deviceConfig.sensorExpTime.ToString());
            ShowLog(" * Sensor Gain: " + deviceConfig.sensorGain.ToString());
            ShowLog(" * GPIO Auth-Toggling: " + deviceConfig.GpioAuthToggling.ToString());
            ShowLog(" * Rectangle enabled: " + deviceConfig.rectEnable.ToString());
            ShowLog(" * Landmarks enabled: " + deviceConfig.landmarksEnable.ToString());
            int numRoisLog = Math.Max(1, Math.Min(5, (int)deviceConfig.num_rois));
            ShowLog(" * Num ROIs: " + numRoisLog);
            for (int i = 0; i < numRoisLog; i++)
            {
                ShowLog($" * ROI[{i}] X: {deviceConfig.detection_rois[i].x}  Y: {deviceConfig.detection_rois[i].y}  Width: {deviceConfig.detection_rois[i].width}  Height: {deviceConfig.detection_rois[i].height}");
            }
            ShowLog(" * Distance Limit: " + deviceConfig.distanceLimit.ToString());
            ShowLog(" * Distance Calculation Enable: " + deviceConfig.distanceEnabled.ToString());
            ShowLog("");
        }

        private DeviceConfig? QueryDeviceConfig()
        {

            ShowLogTitle("Device Config");
            DeviceConfig deviceConfig;
            var rv = _authenticator.QueryDeviceConfig(out deviceConfig);
            if (rv != Status.Ok)
            {
                ShowLog("Query error: " + rv.ToString());
                ShowFailedTitle("Query error: " + rv.ToString());
                return null;
            }
            LogDeviceConfig(deviceConfig);
            return deviceConfig;
        }

        private void UpdateAdvancedMode()
        {
            DeviceConfig? deviceConfig = QueryDeviceConfig();
            if (!deviceConfig.HasValue)
            {
                var msg = "Failed to query device config";
                ShowLog(msg);
                ShowErrorMessage("QueryDeviceConfig Error", msg);
                throw new Exception("QueryDeviceConfig Error");
            }
            _deviceState.DeviceConfig = deviceConfig.Value;
            _operationMode = (OperationMode)(int)deviceConfig.Value.algoFlow;
        }

        private bool UpdateUser(int userIndex, string userId, ref Faceprints updatedFaceprints)
        {
            if (_db == null)
                return false;
            bool success = _db.UpdateUser(userIndex, userId, ref updatedFaceprints);

            if (success)
            {
                _db.Save();
            }

            return success;
        }

        private void HandleDbErrorServer()
        {
            // goal of this handler - to handle two possible scenarios :
            //
            // (1) if Faceprints (FP) version changed, e.g. the FP on the db and the current FP changed version (and possibly their internal structure).
            // (2) if db Load() fails on some exception - this may be due to version mismatch (FP structure changed) or other error.
            //
            // in both cases we want to :
            //
            // (a) backup the old db to a separated file.
            // (b) clear the db and start a new db from scratch.
            // (c) refresh the users list on the gui.
            //

            if (_flowMode == FlowMode.Server)
            {
                _db.SaveBackupAndDeleteDb();
                RefreshUserListServer();
            }
        }

        private void Match(rsid.ExtractedFaceprints faceprintsToMatch)
        {
            try
            {
                ShowProgressTitle("Matching faceprints to database");

                // if Faceprints versions don't match - return with error message.
                if (!(_db.VerifyVersionMatchedPLE(ref faceprintsToMatch)))
                {
                    HandleDbErrorServer();
                    string logmsg = $"Faceprints (FP) version mismatch: DB={_db.GetVersion()}, FP={faceprintsToMatch.version}. Saved the old DB to backup file and started a new DB from scratch.";
                    string guimsg = $"Faceprints (FP) version mismatch: DB={_db.GetVersion()}, FP={faceprintsToMatch.version}. DB backuped and cleaned.";
                    ShowLog(logmsg);
                    VerifyResult(false, string.Empty, guimsg);
                    return;
                }

                // handle with/without mask vectors properly (if/as needed).

                rsid.MatchElement faceprintsToMatchObject = new rsid.MatchElement
                {
                    version = faceprintsToMatch.version,
                    flags = faceprintsToMatch.featuresVector[rsid.FaceprintsConsts.RSID_INDEX_IN_FEATURES_VECTOR_TO_FLAGS],
                    featuresVector = faceprintsToMatch.featuresVector
                };

                int saveMaxScore = -1;
                int winningIndex = -1;
                string winningIdStr = "";
                rsid.MatchResult winningMatchResult = new rsid.MatchResult { success = 0, shouldUpdate = 0, score = 0 };
                rsid.Faceprints winningUpdatedFaceprints = new rsid.Faceprints { }; // dummy init, correct data is set below if condition met.

                int usersIndex = 0;

                // take the value from DeviceConfig.matcherConfidenceLevel.
                var matcherConfidenceLevel = _deviceState.DeviceConfig.matcherConfidenceLevel;

                foreach (var (faceprintsDb, userIdDb) in _db.FaceprintsArray)
                {
                    // note we must send initialized vectors to MatchFaceprintsToFaceprints().
                    // so here we init the updated vector to the existing DB vector before calling MatchFaceprintsToFaceprints()
                    MatchArgs matchArgs = new MatchArgs
                    {
                        newFaceprints = faceprintsToMatchObject,
                        existingFaceprints = faceprintsDb,
                        updatedFaceprints = faceprintsDb, // init updated to existing vector.
                        matcherConfidenceLevel = matcherConfidenceLevel
                    };

                    var matchResult = _authenticator.MatchFaceprintsToFaceprints(ref matchArgs);

                    int currentScore = matchResult.score;

                    // save the best winner that matched.
                    if (matchResult.success == 1)
                    {
                        if (currentScore > saveMaxScore)
                        {
                            saveMaxScore = currentScore;
                            winningMatchResult = matchResult;
                            winningIndex = usersIndex;
                            winningIdStr = userIdDb;
                            winningUpdatedFaceprints = matchArgs.updatedFaceprints;
                        }
                    }

                    usersIndex++;

                } // end of for() loop

                if (winningIndex >= 0) // we have a winner so declare success!
                {
                    VerifyResultAuth(AuthStatus.Success, winningIdStr, string.Empty, null, winningIdStr);

                    ShowLog($"Match info : userIdName = \"{winningIdStr}\", index = \"{winningIndex}\", success = {winningMatchResult.success}, score = {winningMatchResult.score}, should_update = {winningMatchResult.shouldUpdate}.");

                    // apply adaptive-update on the db.
                    if (winningMatchResult.shouldUpdate > 0)
                    {
                        // apply adaptive update
                        // take the updated vector from the matchArgs that were sent by reference and updated
                        // during call to MatchFaceprintsToFaceprints() .

                        bool updateSuccess = UpdateUser(winningIndex, winningIdStr, ref winningUpdatedFaceprints);

                        ShowLog($"Adaptive DB update for userIdName = \"{winningIdStr}\" (index=\"{winningIndex}\"): status = {updateSuccess} ");
                    }
                    else
                    {
                        ShowLog($"Macth succeeded for userIdName = \"{winningIdStr}\" (index=\"{winningIndex}\"). However adaptive update condition not passed, so no DB update applied.");
                    }
                }
                else // no winner, declare authentication failed!
                {
                    VerifyResultAuth(AuthStatus.Forbidden, string.Empty, "No match found");
                }

            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
        }

        private static readonly Brush[] _roiColors = { Brushes.Orange, Brushes.Yellow, Brushes.Cyan, Brushes.LimeGreen, Brushes.Magenta };

        private void RenderROI()
        {
            // Don't draw on empty image
            if (PreviewImage.Visibility != Visibility.Visible || _previewBitmap == null)
                return;

            // Convert ROI coords FHD=>VGA if needed
            double scaleX, scaleY;
            const double RawLongDim = 1920.0, RawShortDim = 1080.0;
            if (_previewBitmap.Width > _previewBitmap.Height)
            {
                scaleX = _previewBitmap.Width / RawLongDim;
                scaleY = _previewBitmap.Height / RawShortDim;
            }
            else
            {
                scaleX = _previewBitmap.Width / RawShortDim;
                scaleY = _previewBitmap.Height / RawLongDim;
            }

            int numRois = Math.Max(1, Math.Min(5, (int)_deviceState.DeviceConfig.num_rois));
            for (int i = 0; i < numRois; i++)
            {
                int roiX = _deviceState.DeviceConfig.detection_rois[i].x;
                int roiY = _deviceState.DeviceConfig.detection_rois[i].y;
                int roiWidth = _deviceState.DeviceConfig.detection_rois[i].width;
                int roiHeight = _deviceState.DeviceConfig.detection_rois[i].height;

                // Don't draw if ROI is full frame
                if (roiX == 0 && roiY == 0)
                {
                    if (_previewBitmap.Width > _previewBitmap.Height)
                    {
                        if (roiWidth == 1920 && roiHeight == 1080) continue;
                    }
                    else
                    {
                        if (roiWidth == 1080 && roiHeight == 1920) continue;
                    }
                }

                var x = roiX * scaleX;
                var y = roiY * scaleY;
                var w = roiWidth * scaleX;
                var h = roiHeight * scaleY;

                string tag = "ROI_" + i;
                Rectangle roiRect = PreviewCanvas.Children
                    .OfType<Rectangle>()
                    .FirstOrDefault(r => (string)r.Tag == tag);

                if (roiRect != null)
                {
                    roiRect.Width = w;
                    roiRect.Height = h;
                }
                else
                {
                    roiRect = new Rectangle
                    {
                        Width = w,
                        Height = h,
                        Stroke = _roiColors[i % _roiColors.Length],
                        StrokeThickness = 5,
                        Tag = tag
                    };
                    PreviewCanvas.Children.Add(roiRect);
                }

                Canvas.SetLeft(roiRect, x);
                Canvas.SetTop(roiRect, y);
            }
        }

        private void RenderDetectedFaces()
        {
            // don't draw on empty image
            if (PreviewImage.Visibility != Visibility.Visible || _previewBitmap == null)
                return;
            // show detected faces
            foreach (var (face, landmarks, status, userId, score, frameScore) in _detectedFaces)
            {
                // convert face rect coords FHD=>VGA
                double scaleX, scaleY;
                const double RawLongDim = 1920.0, RawShortDim = 1080.0;
                if (_previewBitmap.Width > _previewBitmap.Height)
                {
                    scaleX = _previewBitmap.Width / RawLongDim;
                    scaleY = _previewBitmap.Height / RawShortDim;
                }
                else
                {
                    scaleX = _previewBitmap.Width / RawShortDim;
                    scaleY = _previewBitmap.Height / RawLongDim;
                }

                var x = face.x * scaleX;
                var y = face.y * scaleY;
                var w = scaleX * face.width;
                var h = scaleY * face.height;

                var stroke = ProgressBrush;
                var strokeThickness = 2;
                // set rect color to green/red if operation succeeed/failed
                if (status.HasValue)
                {
                    stroke = status.Value == AuthStatus.Success ? SuccessBrush : FailBrush;
                    strokeThickness = 3;
                }

                var rect = new Rectangle
                {
                    Width = w,
                    Height = h,
                    Stroke = stroke,
                    StrokeThickness = strokeThickness,
                };

                PreviewCanvas.Children.Add(rect);
                Canvas.SetLeft(rect, x);
                Canvas.SetTop(rect, y);

                if (landmarks.HasValue)
                {
                    // Render landmarks if available as keypoints
                    if (landmarks.Value.landmarksY != null && landmarks.Value.landmarksY != null && landmarks.Value.landmarksX.Length > 0 && landmarks.Value.landmarksY.Length > 0)
                    {
                        // Flag to indicate if at least one landmark point is valid
                        bool hasValidLandmark = false;
                        for (int i = 0; i < landmarks.Value.landmarksX.Length; i++)
                        {
                            if (landmarks.Value.landmarksX[i] != 0 || landmarks.Value.landmarksY[i] != 0)
                            {
                                hasValidLandmark = true;
                                break;
                            }
                        }

                        if (hasValidLandmark)
                        {
                            // Draw keypoints
                            for (int i = 0; i < landmarks.Value.landmarksX.Length; i++)
                            {
                                var lx = landmarks.Value.landmarksX[i] * scaleX;
                                var ly = landmarks.Value.landmarksY[i] * scaleY;
                                var ellipse = new Ellipse
                                {
                                    Width = 12,
                                    Height = 12,
                                    Fill = status.Value == AuthStatus.Success ? SuccessBrush : FailBrush,
                                };
                                PreviewCanvas.Children.Add(ellipse);
                                Canvas.SetLeft(ellipse, lx - 6);
                                Canvas.SetTop(ellipse, ly - 6);
                            }
                        }
                    }
                }

                string rectString = userId != null ? userId : string.Empty;
                var showStatus = status.HasValue && (status.Value != AuthStatus.Success || string.IsNullOrEmpty(userId));
                string statusString = showStatus ? Enum.GetName(typeof(AuthStatus), status) : string.Empty;
                rectString = rectString + " " + statusString;
                // if in walkthrough mode, don't show scores
                if (_deviceState.DeviceConfig.personMotionMode != DeviceConfig.PersonMotionMode.Walkthrough)
                {
                    rectString = score > 0 ? rectString + " (" + score + ")" : rectString;
                    rectString = frameScore > 0 ? rectString + " [" + frameScore.ToString("n4") + "]" : rectString;
                }

                // print username near the rect if available
                if (!string.IsNullOrEmpty(rectString))
                {
                    var userTextBlock = new TextBlock
                    {
                        FontSize = 38,
                        // flip the back the text because the canvas horizontally flips the preview
                        RenderTransformOrigin = new Point(0, 0.5),
                        RenderTransform = new ScaleTransform { ScaleX = -1, ScaleY = 1 },
                        //FontFamily = new FontFamily("Arial"),
                        Text = rectString,
                        Foreground = Brushes.White
                    };
                    // display the text on bottom left
                    PreviewCanvas.Children.Add(userTextBlock);
                    Canvas.SetLeft(userTextBlock, x + w - 4);
                    Canvas.SetTop(userTextBlock, y + h);
                }
            }
        }

        private void RenderDetectedPersons(PersonRect[] persons)
        {
            // don't draw on empty image
            if (PreviewImage.Visibility != Visibility.Visible || _previewBitmap == null)
                return;

            // Render detected persons
            PreviewCanvas.Children.Clear();
            foreach (var person in persons)
            {
                // Convert person rect coords FHD=>VGA
                double scaleX, scaleY;
                const double RawLongDim = 1920.0, RawShortDim = 1080.0;
                if (_previewBitmap.Width > _previewBitmap.Height)
                {
                    scaleX = _previewBitmap.Width / RawLongDim;
                    scaleY = _previewBitmap.Height / RawShortDim;
                }
                else
                {
                    scaleX = _previewBitmap.Width / RawShortDim;
                    scaleY = _previewBitmap.Height / RawLongDim;
                }

                var x = person.x * scaleX;
                var y = person.y * scaleY;
                var w = scaleX * person.width;
                var h = scaleY * person.height;

                // Draw rectangle around person
                // select color
                Brush stroke;
                switch (person.bodyPart)
                {
                    case BodyPart.Person:
                        stroke = SuccessBrush;
                        break;
                    case BodyPart.Foot:
                        stroke = Brushes.Red;
                        break;
                    case BodyPart.Arm:
                        stroke = Brushes.Blue;
                        break;
                    case BodyPart.Leg:
                        stroke = Brushes.Yellow;
                        break;
                    case BodyPart.Hand:
                        stroke = Brushes.Magenta;
                        break;
                    default:
                        stroke = SuccessBrush;
                        break;

                }
                var rect = new Rectangle
                {
                    Width = w,
                    Height = h,
                    Stroke = stroke,
                    StrokeThickness = 3,
                };

                PreviewCanvas.Children.Add(rect);
                Canvas.SetLeft(rect, x);
                Canvas.SetTop(rect, y);
            }
        }

        // 0: Nose, 1: LeftEye, 2: RightEye, 3: LeftEar, 4: RightEar, 5: LeftShoulder, 6: RightShoulder, 7: LeftElbow, 8: RightElbow,
        // 9: LeftWrist, 10: RightWrist, 11: LeftHip, 12: RightHip, 13: LeftKnee, 14: RightKnee, 15: LeftAnkle, 16: RightAnkle
        private static readonly List<(int, int)> _poseConnections = new List<(int, int)> {
            (15, 13), (13, 11), (16, 14), (14, 12), (11, 12), (5, 11), (6, 12),
            (5, 6), (5, 7), (7, 9), (6, 8), (8, 10), (0, 1), (0, 2), (1, 3), (2, 4)
        };
        private void RenderDetectedPoses(PersonPose[] poses)
        {
            // don't draw on empty image
            if (PreviewImage.Visibility != Visibility.Visible || _previewBitmap == null)
                return;

            // Render detected persons
            PreviewCanvas.Children.Clear();
            foreach (var pose in poses)
            {
                // Convert pose rect coords FHD=>VGA
                double scaleX, scaleY;
                const double RawLongDim = 1920.0, RawShortDim = 1080.0;
                if (_previewBitmap.Width > _previewBitmap.Height)
                {
                    scaleX = _previewBitmap.Width / RawLongDim;
                    scaleY = _previewBitmap.Height / RawShortDim;
                }
                else
                {
                    scaleX = _previewBitmap.Width / RawShortDim;
                    scaleY = _previewBitmap.Height / RawLongDim;
                }

                // Draw rectangle around person
                var x = pose.x * scaleX;
                var y = pose.y * scaleY;
                var w = scaleX * pose.width;
                var h = scaleY * pose.height;

                var rect = new Rectangle
                {
                    Width = w,
                    Height = h,
                    Stroke = SuccessBrush,
                    StrokeThickness = 3,
                };

                PreviewCanvas.Children.Add(rect);
                Canvas.SetLeft(rect, x);
                Canvas.SetTop(rect, y);

                // Render landmarks if available as skeleton of keypoints and connections
                if (pose.landmarksX != null && pose.landmarksY != null && pose.landmarksX.Length > 0 && pose.landmarksY.Length > 0)
                {
                    int n = Math.Min(pose.landmarksX.Length, pose.landmarksY.Length);
                    double MinLandmarkScore = Settings.Default.ScoreThreshold;

                    bool ShouldRenderLandmark(int index)
                    {
                        if (index < 0 || index >= n)
                            return false;
                        if (pose.landmarksScore == null || pose.landmarksScore.Length <= index)
                            return true;

                        var normalizedScore = pose.landmarksScore[index];
                        return normalizedScore >= MinLandmarkScore;
                    }

                    // Draw keypoints
                    for (int i = 0; i < n; i++)
                    {
                        if (!ShouldRenderLandmark(i))
                            continue;

                        var lx = pose.landmarksX[i] * scaleX;
                        var ly = pose.landmarksY[i] * scaleY;
                        var ellipse = new Ellipse
                        {
                            Width = 20,
                            Height = 20,
                            Fill = Brushes.Yellow,
                            Stroke = Brushes.Black,
                            StrokeThickness = 2
                        };
                        PreviewCanvas.Children.Add(ellipse);
                        Canvas.SetLeft(ellipse, lx - 10);
                        Canvas.SetTop(ellipse, ly - 10);
                    }

                    foreach (var conn in _poseConnections)
                    {
                        if (conn.Item1 < n && conn.Item2 < n)
                        {
                            if (!ShouldRenderLandmark(conn.Item1) || !ShouldRenderLandmark(conn.Item2))
                                continue;

                            var x0 = pose.landmarksX[conn.Item1] * scaleX;
                            var y0 = pose.landmarksY[conn.Item1] * scaleY;
                            var x1 = pose.landmarksX[conn.Item2] * scaleX;
                            var y1 = pose.landmarksY[conn.Item2] * scaleY;

                            var line = new Line
                            {
                                X1 = x0,
                                Y1 = y0,
                                X2 = x1,
                                Y2 = y1,
                                Stroke = Brushes.LimeGreen,
                                StrokeThickness = 3
                            };
                            PreviewCanvas.Children.Add(line);
                        }
                    }
                }
            }
        }

        private void UiHandlePreview(PreviewImage image)
        {
            var targetWidth = (int)PreviewImage.Width;
            var targetHeight = (int)PreviewImage.Height;

            //create writable bitmap if not exists or if image size changed
            if (_previewBitmap == null || targetWidth != image.width || targetHeight != image.height)
            {
                PreviewImage.Width = image.width;
                PreviewImage.Height = image.height;
                Console.WriteLine($"Creating new WriteableBitmap preview buffer {image.width}x{image.height}");
                _previewBitmap = new WriteableBitmap(image.width, image.height, 96, 96, PixelFormats.Rgb24, null);
                PreviewImage.Source = _previewBitmap;
            }
            Int32Rect sourceRect = new Int32Rect(0, 0, image.width, image.height);
            lock (_previewMutex)
            {
                _previewBitmap.WritePixels(sourceRect, _previewBuffer, image.stride, 0);
            }
        }

        private bool PreviewHandler(ref PreviewImage previewImage)
        {
            System.Drawing.Bitmap bitmap = new System.Drawing.Bitmap(previewImage.width, previewImage.height, previewImage.stride, System.Drawing.Imaging.PixelFormat.Format24bppRgb, previewImage.buffer);

            if (_deviceState.PreviewConfig.previewMode == PreviewMode.RAW10_1080P)
            {
                // in f500 flip for all rotations
                if (_deviceState.IsF500)
                    bitmap.RotateFlip(System.Drawing.RotateFlipType.RotateNoneFlipXY);
                //RAW preview for 180 and 270 degrees needs to be flipped
                if (_deviceState.DeviceConfig.cameraRotation == DeviceConfig.CameraRotation.Rotation_180_Deg ||
                    _deviceState.DeviceConfig.cameraRotation == DeviceConfig.CameraRotation.Rotation_270_Deg)
                    bitmap.RotateFlip(System.Drawing.RotateFlipType.Rotate180FlipNone);
            }

            previewImage.width = bitmap.Width;
            previewImage.height = bitmap.Height;
            previewImage.stride = previewImage.size / previewImage.height;

            var bitmap_data = bitmap.LockBits(new System.Drawing.Rectangle(0, 0, bitmap.Width, bitmap.Height),
                System.Drawing.Imaging.ImageLockMode.ReadOnly, bitmap.PixelFormat);
            Marshal.Copy(bitmap_data.Scan0, _previewBuffer, 0, previewImage.size);
            bitmap.UnlockBits(bitmap_data);
            return true;
        }

        // Handle preview callback.
        private void OnPreview(PreviewImage image, IntPtr ctx)
        {
            if (_pausePreview)
                return;

            if (image.metadata.sensor_id != 0) // preview only left sensor
                return;

            if (image.height == 640) // ignore snapshot type frames
                return;

            // prepare label with fps and resolution
            string previewLabel = null;
            _frameCounter++;
            var elapsedMs = _fpsStopWatch.ElapsedMilliseconds;
            if (elapsedMs >= 1000)
            {
                long fps = _frameCounter * 1000 / elapsedMs;
                var dumpsLabel = _frameDumper != null ? " (dump mode)" : string.Empty;
                // show height x width instead of widthx height since we rotated the image
                previewLabel = $"{image.width}x{image.height}  {fps} FPS {dumpsLabel}";
                _frameCounter = 0;
                _fpsStopWatch.Restart();
            }

            // display preview image
            lock (_previewMutex)
            {
                // preview image is allways RGB24
                if (_previewBuffer.Length < image.size)
                {
                    Console.WriteLine("Creating preview buffer");
                    _previewBuffer = new byte[image.size];
                }

                if (PreviewImage.Visibility != Visibility.Visible)
                    InvokePreviewVisibility(Visibility.Visible);

                // handle preview with optional rotation
                PreviewHandler(ref image);
            }
            NormalDispatch(() =>
            {
                if (previewLabel != null)
                    LabelPreviewInfo.Content = previewLabel;
                UiHandlePreview(image);
                RenderROI();
            });
        }

        private void HandleDumpException(Exception ex)
        {
            _frameDumper = null;
            NormalDispatch(() =>
            {
                ShowErrorMessage("Dump failed", ex.Message + "\nDump stopped..");
            });
        }

        private void OnSnapshot(PreviewImage image, IntPtr ctx)
        {
            try
            {
                DumpImage(image);
            }
            catch (Exception ex)
            {
                HandleDumpException(ex);
            }
        }

        private void DumpImage(PreviewImage image)
        {
            if (_frameDumper != null)
            {
                try
                {
                    var accessories = FilterAccesories(_sessionHints);
                    if (_deviceState.DeviceConfig.dumpMode == DeviceConfig.DumpMode.FullFrame)
                    {
                        _frameDumper.DumpRawImage(image, accessories);
                    }
                    else
                    {
                        var filename = _frameDumper.DumpPreviewImage(image, accessories);
                        if (filename != null)
                            ShowDumpFile(filename, image.metadata.timestamp);
                    }
                }
                catch (Exception ex)
                {
                    HandleDumpException(ex);
                }
            }
        }

        // Display dump file at the top right corner
        private void ShowDumpFile(string filename, uint ts)
        {
            NormalDispatch(() =>
            {
                try
                {
                    if (ts != _lastDumpTs) // New frame
                    {

                        _lastDumpTs = ts;
                        _lastDumpBorder = null;

                        var image = ImageHelper.CreateImageControl(filename);

                        // Create a grid to hold the image and potential overlay
                        var grid = new Grid();
                        grid.Children.Add(image);

                        var border = new Border
                        {
                            BorderThickness = new Thickness(3),
                            BorderBrush = Brushes.White,
                            Child = grid
                        };

                        var panel = new StackPanel { Orientation = Orientation.Horizontal, VerticalAlignment = VerticalAlignment.Top };
                        panel.Children.Add(border);

                        Canvas.SetLeft(panel, 25);
                        Canvas.SetTop(panel, 190);
                        PreviewCanvas.Children.Clear();
                        PreviewCanvas.Children.Add(panel);

                        _lastDumpBorder = border;
                    }
                    else // Same frame, additional face
                    {
                        if (_lastDumpBorder != null && _lastDumpBorder.Parent is StackPanel panel)
                        {
                            var image = ImageHelper.CreateImageControl(filename);
                            // Show small image (half size) next to the main one
                            if (_lastDumpBorder.Child is Grid mainGrid &&
                                mainGrid.Children.Count > 0 &&
                                mainGrid.Children[0] is System.Windows.Controls.Image mainImage)
                            {
                                image.Width = mainImage.Width * 0.5;
                                image.Height = mainImage.Height * 0.5;
                                image.Stretch = Stretch.Uniform;
                            }
                            else
                            {
                                image.LayoutTransform = new ScaleTransform(0.5, 0.5);
                            }

                            var border = new Border
                            {
                                BorderThickness = new Thickness(1),
                                BorderBrush = Brushes.White,
                                Child = image,
                                Margin = new Thickness(5, 0, 0, 0),
                                VerticalAlignment = VerticalAlignment.Top
                            };
                            panel.Children.Add(border);
                        }
                    }
                }
                catch (Exception ex)
                {
                    Logger.Log("ShowDumpFile failed: " + ex.Message);
                }
            });
        }

        private void ResetOverlay()
        {
            NormalDispatch(() =>
            {
                _detectedFaces.Clear();
                _detectedLandmarks.Clear();
                PreviewCanvas.Children.Clear();
                PreviewCanvas.Visibility = Visibility.Visible;
                PreviewCanvas.Opacity = 1.0;
            });
        }

        private void OnStartSession(string title, bool activateDumps)
        {
            //activate full preview dumps only if device's DumpMode is enabled
            activateDumps = activateDumps && _deviceState.DeviceConfig.dumpMode != DeviceConfig.DumpMode.None;
            Dispatcher.Invoke(() =>
            {
                ShowLogTitle(title);
                SetUiEnabled(false);
                RedDot.Visibility = Visibility.Visible;
                _cancelWasCalled = false;
                _sessionHints.Clear();
                AuthenticatingTextBlock.Text = IsWalkthroughMode() ? "Scanning..." : "Authenticating...";
                EnrollingTextBlock.Text = "Enrolling user...";
                CancelAuthenticationButton.IsEnabled = true;
                ResetOverlay();
                _wkSuccessTimer.Reset();
                try
                {
                    _frameDumper = activateDumps ? new FrameDumper(_dumpDir, title, _deviceState) : null;
                }
                catch (Exception)
                {
                    _frameDumper = null;
                    ShowErrorMessage("Failed to create the 'dumps' folder",
                        "Please try changing the 'dumps' variable in the 'rsid-viewer.exe.config' file to a different location and restart.");
                }
            });
        }

        private void OnStopSession()
        {
            Dispatcher.Invoke(() =>
            {
                SetUiEnabled(true);
                RedDot.Visibility = Visibility.Hidden;
            });
        }

        // Enroll callbacks
        private void OnEnrollHint(EnrollStatus hint, float frameScore, IntPtr ctx)
        {
            if (_cancelWasCalled)
            {
                ShowSuccessTitle("Canceled");
                return;
            }
            _sessionHints.Add(hint);
            ShowLog("Hint: " + hint.ToString());
            if (hint != EnrollStatus.Success)
                ShowProgressTitle(hint.ToString());
            if (hint != EnrollStatus.CameraStarted && hint != EnrollStatus.CameraStopped)
                ShowLog("frame score: " + frameScore.ToString("n4"));
        }

        private void OnEnrollProgress(FacePose pose, IntPtr ctx)
        {
            ShowLog("Prgrs: " + pose.ToString());
        }

        private void OnEnrollResult(EnrollStatus status, IntPtr ctx)
        {
            ShowLog($"OnEnrollResult status: {status}");

            if (_cancelWasCalled)
            {
                _lastEnrolledUserId = null;
                ShowSuccessTitle("Canceled");
            }
            else
            {
                if (status != EnrollStatus.Success)
                {
                    _lastEnrolledUserId = null;
                }
                var guimsg = status.ToString() + AcessoriesString(_sessionHints);
                ShowLog(guimsg);
                VerifyResult(status == EnrollStatus.Success, guimsg, guimsg);
            }
            ResetOverlay();
        }

        private void OnEnrollExtractionResult(EnrollStatus status, IntPtr faceprintsHandle, IntPtr ctx)
        {
            ShowLog($"OnEnrollExtractionResult status: {status}");

            if (_cancelWasCalled)
            {
                ShowSuccessTitle("Canceled");
            }
            else
            {
                string logmsg;
                if (status != EnrollStatus.Success)
                {
                    logmsg = status.ToString();

                    ShowFailedTitle(logmsg);
                    ShowLog(logmsg);
                    return;
                }

                logmsg = "Enroll success";
                var faceprints = (Faceprints)Marshal.PtrToStructure(faceprintsHandle, typeof(Faceprints));
                string guimsg = logmsg;

                // handle version mismatch (db version vs. faceprint version).
                if ((status == rsid.EnrollStatus.Success) && !(_db.VerifyVersionMatchedDBLE(ref faceprints)))
                {
                    HandleDbErrorServer();
                    logmsg += $" Faceprints (FP) version mismatch. DB={_db.GetVersion()}, FP={faceprints.version}. Saved the DB to backup file and started a new DB from scratch.";
                    guimsg += $" Faceprints version mismatch : DB backuped and cleaned.";
                }

                guimsg += AcessoriesString(_sessionHints);
                ShowLog(logmsg);

                // handle enroll
                VerifyResult(true, guimsg, guimsg, () =>
                {
                    if (_db.Push(faceprints, _lastEnrolledUserId))
                    {
                        _db.Save();
                    }
                    RefreshUserListServer();
                });
            }
        }

        // return /hints are in the range of 50-60
        private static ArrayList FilterAccesories(ArrayList hints)
        {
            var rv = new ArrayList();
            var startInt = (int)(AuthStatus.Sunglasses);
            var endInt = startInt + 10;
            foreach (var hint in hints)
            {
                if ((int)hint >= startInt && (int)hint <= endInt && !rv.Contains(hint))
                    rv.Add(hint);
            }
            return rv;
        }
        // Return friendly string for found accessories in hints
        // Example: "(sunglasses, medicalmask)
        private static string AcessoriesString(ArrayList hints)
        {
            var accesories = FilterAccesories(hints);
            if (accesories.Count == 0)
                return string.Empty;
            var joined = string.Join(", ", accesories.ToArray());
            return $" ({joined})".ToLower();
        }

        private bool IsWalkthroughMode()
        {
            return _deviceState.DeviceConfig.personMotionMode == DeviceConfig.PersonMotionMode.Walkthrough;
        }

        // Authentication callbacks
        private void OnAuthHint(AuthStatus hint, float frameScore, IntPtr ctx)
        {
            if (_cancelWasCalled)
            {
                ShowSuccessTitle("Canceled");
                return;
            }
            _sessionHints.Add(hint);
            ShowLog(hint.ToString());

            if (!IsWalkthroughMode())
            {
                ShowProgressTitle(hint.ToString()); // don't show hints in walkthrough mode
            }

            if (hint != AuthStatus.CameraStarted && hint != AuthStatus.CameraStopped)
            {
                ShowLog("frame score: " + frameScore.ToString("n4"));
                _lastFrameScore = frameScore;
            }
            if (hint == AuthStatus.NoFaceDetected || hint == AuthStatus.PersonNotFound)
            {
                ResetOverlay();
            }
        }

        private void OnAuthResult(AuthStatus status, string userId, short score, IntPtr ctx)
        {
            ShowLog($"OnAuthResult status: {status} \"{userId}\" {score}");
            if (_cancelWasCalled)
            {
                ShowSuccessTitle("Canceled");
                ResetOverlay();
            }
            // Hide rectangles for invalid features, only show message.
            else if (status == AuthStatus.InvalidFeatures)
            {
                UpdateFaceResult(status, null, 0);
                return;
            }
            // walkthrough - don't show score or accessories
            if (IsWalkthroughMode())
            {
                var sucessMsg = userId.Length > 0 ? userId : status.ToString();
                var failMsg = status.ToString();
                VerifyResultAuth(status, sucessMsg, failMsg, null, userId, score);
            }
            else
            {
                var accesories = AcessoriesString(_sessionHints);
                if (userId.Length > 0)
                    VerifyResultAuth(status, $"{userId}({score}){accesories}", $"{status}({score})", null, userId, score);
                else
                    VerifyResultAuth(status, $"{status}", $"{status}", null, userId, score);
            }
            _sessionHints.Clear();
        }

        private void OnFaceDetected(IntPtr facesArr, int faceCount, uint ts, IntPtr ctx)
        {
            if (_cancelWasCalled)
            {
                ShowSuccessTitle("Canceled");
                return;
            }
            //convert to face rects
            ResetOverlay();
            var faces = Authenticator.MarshalFaces(facesArr, faceCount);

            foreach (var face in faces)
            {
                ShowLog($"OnFaceDetected [{face.x},{face.y} {face.width}x{face.height}]");
                NormalDispatch(() => _detectedFaces.Add((face, null, null, null, 0, 0)));
            }
        }

        private void OnLandmarksDetected(IntPtr landmarksArr, int faceCount, uint ts, IntPtr ctx)
        {
            if (_cancelWasCalled)
            {
                ShowSuccessTitle("Canceled");
                return;
            }
            //convert to face landmarks
            var landmarks = Authenticator.MarshalLandamrks(landmarksArr, faceCount);
            foreach (var lms in landmarks)
            {
                ShowLog($"OnLandmarksDetected [x0={lms.landmarksX[0]},y0={lms.landmarksY[0]}]");
                NormalDispatch(() => _detectedLandmarks.Add(lms));
            }
        }

        private void OnFaceDistances(IntPtr distancesPtr, int count, uint ts, IntPtr ctx)
        {
            if (_cancelWasCalled)
            {
                ShowSuccessTitle("Canceled");
                return;
            }
            // Marshal distance values (doubles)
            double[] distances = new double[count];
            Marshal.Copy(distancesPtr, distances, 0, count);

            for (int i = 0; i < distances.Length; i++)
            {
                ShowLog($"Face[{i}] distance: {distances[i]:F1} cm");
            }
        }

        // Create and return a person detection callback with status handling
        // In loop mode, show searching continuously
        // In single shot mode, show failure
        private PersonDetectionCallback CreatePersonDetectionCallback(bool isLoopMode)
        {
            return (persons, count, ts, authStatus, ctx) =>
            {
                if (_cancelWasCalled)
                {
                    ShowSuccessTitle("Canceled");
                    return false;
                }

                if (authStatus != AuthStatus.PersonFound)
                {
                    ShowLog($"{authStatus}");
                    ResetOverlay();
                    if (isLoopMode)
                        ShowProgressTitle("Scanning..");
                    else
                        ShowFailedTitle(authStatus.ToString());
                }
                else
                {
                    ShowSuccessTitle("Person Detected");
                    ShowLog($"Detected {count} person(s)");
                    OnPersonDetected(persons, count);
                }
                return true; // Continue detection
            };
        }

        private PoseDetectionCallback CreatePoseDetectionCallback(bool isLoopMode)
        {
            return (poses, count, ts, authStatus, ctx) =>
            {
                if (_cancelWasCalled)
                {
                    ShowSuccessTitle("Canceled");
                    return false;
                }
                if (authStatus != AuthStatus.PersonFound)
                {
                    ShowLog($"{authStatus}");
                    ResetOverlay();
                    if (isLoopMode)
                        ShowProgressTitle("Scanning..");
                    else
                        ShowFailedTitle(authStatus.ToString());
                }
                else
                {
                    ShowSuccessTitle("Pose Detected");
                    OnPoseDetected(poses, count, ts, ctx);
                }
                return true; // Continue detection
            };
        }

        private BodyPartDetectionCallback CreateBodyPartDetectionCallback(bool isLoopMode)
        {
            return (bodyParts, count, ts, authStatus, ctx) =>
            {
                if (_cancelWasCalled)
                {
                    ShowSuccessTitle("Canceled");
                    return false;
                }
                if (authStatus != AuthStatus.PersonFound)
                {
                    ShowLog($"{authStatus}");
                    ResetOverlay();
                    if (isLoopMode)
                        ShowProgressTitle("Scanning..");
                    else
                        ShowFailedTitle(authStatus.ToString());
                }
                else
                {
                    ShowSuccessTitle("Person Detected");
                    ShowLog($"Detected {count} body part(s)");
                    OnPersonDetected(bodyParts, count);
                }
                return true; // Continue detection
            };
        }

        private BarcodeDetectionCallback CreateBarcodeDetectionCallback(bool isLoopMode)
        {
            return (barcodes, count, ts, authStatus, ctx) =>
            {
                if (_cancelWasCalled)
                {
                    ShowSuccessTitle("Canceled");
                    return false;
                }
                if (authStatus != AuthStatus.BarcodeFound)
                {
                    ShowLog($"{authStatus}");
                    ResetOverlay();
                    if (isLoopMode)
                        ShowProgressTitle("Scanning..");
                    else
                        ShowFailedTitle(authStatus.ToString());
                }
                else
                {
                    ShowSuccessTitle($"Detected {count} Barcode(s)");
                    OnBarcodeDecoded(barcodes, count, ts, ctx);
                }
                return true; // Continue detection
            };
        }

        private void ExecuteAlgoFlow(OperationMode algoFlow, AuthArgs authArgs, bool isLoopMode)
        {
            switch (algoFlow)
            {
                case OperationMode.PersonDetectionOnly:
                    ShowProgressTitle("Scanning..");
                    _authenticator.DetectPersons(CreatePersonDetectionCallback(isLoopMode), isLoopMode);
                    break;

                case OperationMode.PoseEstimationOnly:
                    ShowProgressTitle("Scanning..");
                    _authenticator.DetectPoses(CreatePoseDetectionCallback(isLoopMode), isLoopMode);
                    break;

                case OperationMode.BodyPartDetectionOnly:
                    ShowProgressTitle("Scanning..");
                    _authenticator.DetectBodyParts(CreateBodyPartDetectionCallback(isLoopMode), isLoopMode);
                    break;
                case OperationMode.BarcodeDecodingOnly:
                    ShowProgressTitle("Scanning..");
                    _authenticator.DecodeBarcodes(CreateBarcodeDetectionCallback(isLoopMode), isLoopMode);
                    break;


                case OperationMode.FaceDetectionOnly:
                case OperationMode.SpoofOnly:
                case OperationMode.RecognitionOnly:
                case OperationMode.All:
                    if (algoFlow == OperationMode.All)
                        ShowProgressTitle("Authenticating...");
                    else
                        ShowProgressTitle("Scanning...");

                    if (isLoopMode)
                        _authenticator.AuthenticateLoop(authArgs);
                    else
                        _authenticator.Authenticate(authArgs);
                    break;
                default:
                    throw new ArgumentException($"Unsupported algorithm flow: {_operationMode}");
            }
        }

        private void OnPersonDetected(IntPtr personsArr, int personCount)
        {
            var persons = Authenticator.MarshalPersons(personsArr, personCount);
            foreach (var person in persons)
            {
                var distanceInfo = person.distance != 0 ? $" {person.distance} cm" : string.Empty;
                ShowLog($" * [{person.bodyPart}] [{person.x},{person.y} {person.width}x{person.height}]{distanceInfo}");
            }

            NormalDispatch(() =>
            {
                RenderDetectedPersons(persons);
            });
        }

        private void OnPoseDetected(IntPtr poseArr, int poseCount, uint ts, IntPtr ctx)
        {
            ShowLog($"OnPoseDetected: {poseCount} poses(s)");
            //convert to person rects            
            var poses = Authenticator.MarshalPoses(poseArr, poseCount);
            foreach (var pose in poses)
            {
                ShowLog($" * [{pose.x},{pose.y} {pose.width}x{pose.height}]");
            }
            NormalDispatch(() =>
            {
                RenderDetectedPoses(poses);
            });
        }

        private void OnBarcodeDecoded(IntPtr barcodesArr, int barcodeCount, uint ts, IntPtr ctx)
        {
            ShowLog($"OnBarcodeDecoded: {barcodeCount} barcode(s)");
            //convert to barcode strings
            var barcodes = Authenticator.MarshalBarcodes(barcodesArr, barcodeCount);
            foreach (var barcode in barcodes)
            {
                ShowLog($"OnBarcodeDecoded: {barcode} (ts {ts})");
            }
        }

        private void OnFaceCroppedImage(IntPtr buf, int w, int h, uint ts, IntPtr ctx)
        {
            ShowLog($"OnFaceCroppedImage [{w}x{h},ts={ts}]");
            var accessories = FilterAccesories(_sessionHints);
            int channels = 3; //rgb
            var image = new PreviewImage
            {
                buffer = buf,
                width = w,
                height = h,
                stride = w * channels,
                metadata = new PreviewImageMetadata
                {
                    timestamp = ts,
                },
            };
            var filename = _frameDumper.DumpPreviewImage(image, accessories);
            if (filename != null)
                ShowDumpFile(filename, ts);
        }

        public void OnAuthLoopExtractionResult(AuthStatus status, IntPtr faceprintsHandle, IntPtr ctx)
        {
            ShowLog($"OnAuthLoopExtractionResult status: {status}");
            if (_cancelWasCalled)
            {
                ShowSuccessTitle("Canceled");
            }
            else if (status == rsid.AuthStatus.Success)
            {
                // handle with/without mask vectors properly (if/as needed).
                var faceprints = (rsid.ExtractedFaceprints)Marshal.PtrToStructure(faceprintsHandle, typeof(rsid.ExtractedFaceprints));
                Match(faceprints);
            }
            else
            {
                VerifyResultAuth(status, string.Empty, status.ToString());
            }
            _sessionHints.Clear();
        }

        private void OnAuthExtractionResult(AuthStatus status, IntPtr faceprintsHandle, IntPtr ctx)
        {
            ShowLog($"OnAuthExtractionResult status: {status}");
            if (_cancelWasCalled)
            {
                ShowSuccessTitle("Canceled");
            }
            else if (status == rsid.AuthStatus.Success)
            {
                // handle with/without mask vectors properly (if/as needed).

                var faceprints = (rsid.ExtractedFaceprints)Marshal.PtrToStructure(faceprintsHandle, typeof(rsid.ExtractedFaceprints));
                Match(faceprints);
            }
            else
            {
                VerifyResultAuth(status, string.Empty, status.ToString());
            }
            _sessionHints.Clear();
        }


        private void HideEnrollingLabelPanel()
        {
            Dispatcher.Invoke(() =>
            {
                EnrollPanel.Visibility = Visibility.Collapsed;
            });
        }

        private void HideAuthenticatingLabelPanel()
        {
            Dispatcher.Invoke(() =>
            {
                AuthenticationPanel.Visibility = Visibility.Collapsed;
            });
        }


        private void SetInstructionsToRefreshUsers(bool isRefresh)
        {
            string text = isRefresh ? "Updating users list" : "Press Enroll to add users";
            Dispatcher.Invoke(() =>
            {
                InstructionsEnrollUsers.Text = text;
            });
        }

        private void UpdateUsersUiList(string[] users)
        {
            UsersListView.ItemsSource = users.ToList();
            UsersListView.UnselectAll();
            DeleteButton.IsEnabled = false;
            SelectAllUsersCheckBox.IsChecked = false;
            var usersCount = users.Length;
            UsersTab.Header = $"Users ({usersCount})";
            var usersExist = usersCount > 0;
            InstructionsEnrollUsers.Visibility = usersExist ? Visibility.Collapsed : Visibility.Visible;
            SelectAllUsersCheckBox.IsEnabled = usersExist &&
                _operationMode == OperationMode.All || _operationMode == OperationMode.RecognitionOnly;
            UpdateAuthButtonText();
        }

        // query user list from the device and update the display
        private void RefreshUserList()
        {
            // Query users and update the user list display
            ShowLog("Fetching users..");
            ShowProgressTitle("Fetching users..");
            SetInstructionsToRefreshUsers(true);
            string[] users;
            var rv = _authenticator.QueryUserIds(out users);
            if (rv != Status.Ok)
            {
                throw new Exception("Query error: " + rv.ToString());
            }
            ClearTitle();

            ShowLog($"{users.Length} users");

            // update the gui and save the list into _userList
            SetInstructionsToRefreshUsers(false);
            NormalDispatch(() =>
            {
                UpdateUsersUiList(users);
            });
            _userList = users;
        }

        private void RefreshUserListServer()
        {
            // Query users and update the user list display
            ShowLog("Query users..");
            SetInstructionsToRefreshUsers(true);
            string[] users;
            _db.GetUserIds(out users);
            ShowLog($"{users.Length} users");

            // update the gui and save the list into _userList
            SetInstructionsToRefreshUsers(false);
            NormalDispatch(() =>
            {
                UpdateUsersUiList(users);
            });
            _userList = users;
        }

        private DeviceState? QueryDeviceMetadata(DeviceInfo deviceInfo)
        {
            var device = new DeviceState();
            device.SerialConfig.port = deviceInfo.SerialPort;
            device.deviceType = deviceInfo.DeviceType;

            using (var controller = new DeviceController(deviceInfo.DeviceType))
            {
                ShowLog($"Connecting to {device.SerialConfig.port}...");
                var status = controller.Connect(device.SerialConfig);
                if (status != Status.Ok)
                {
                    ShowLog("Failed\n");
                    return null;
                }
                ShowLog("Success\n");

                // print FW modules
                var fwVersion = controller.QueryFirmwareVersion();
                ShowLog($"Device: {device.deviceType}");
                ShowLog("Firmware:");
                var versionLines = fwVersion.ToLower().Split('|');
                foreach (var v in versionLines)
                {
                    var splitted = v.Split(':');
                    if (splitted.Length == 2)
                    {
                        ShowLog($" * {splitted[0].ToUpper()} - {splitted[1]}");
                        if (splitted[0] == "opfw")
                            device.FirmwareVersion = splitted[1];
                        else if (splitted[0] == "recog")
                            device.RecognitionVersion = splitted[1];
                    }
                }
                ShowLog("");

                // Detect if it is F500 device
                var bspver = controller.QueryBspVer();
                device.IsF500 = bspver.Contains("RealSense F500");
                ShowLog($"IsF500: {(device.IsF500 ? "Yes" : "No")}\n");

                // print serial number
                var sn = controller.QuerySerialNumber();
                device.SerialNumber = sn;
                ShowLog($"S/N: {device.SerialNumber}\n");
                NormalDispatch(() =>
                {
                    // add device info to the title (replace if already exists)
                    if (!string.IsNullOrEmpty(device.FirmwareVersion))
                    {
                        var idx = Title.IndexOf(" (");
                        if (idx != -1)
                        {
                            Title = Title.Substring(0, idx);
                        }
                        if (device.IsF500)
                        {
                            Title += $" (F500 ver {device.FirmwareVersion})";
                        }
                        else
                        {
                            var deviceName = device.deviceType == DeviceType.F50x ? "F46x" : device.deviceType.ToString();
                            Title += $" ({deviceName} ver {device.FirmwareVersion})";
                        }
                    }
                    SNText.Text = $"S/N: {device.SerialNumber}";
                });

                // write sn.txt file with serial number in it
                try
                {
                    File.WriteAllText(_serialNumberFile, sn);
                }
                catch (Exception ex)
                {
                    // Not critical error. just log it and continue
                    ShowLog("Error writing to sn.txt:\n" + ex.Message);
                }
                ShowLog("Pinging device...");

                status = controller.Ping();
                device.IsOperational = status == Status.Ok;

                ShowLog($"{(device.IsOperational ? "Success" : "Failed")}\n");
            }

            // todo gabi auto detect device type
            var isCompatible = Authenticator.IsFwCompatibleWithHost(device.deviceType, device.FirmwareVersion);
            device.IsCompatible = isCompatible;
            ShowLog($"Is compatible with host? {(device.IsCompatible ? "Yes" : "No")}\n");

            if (_deviceState.IsOperational)
            {
                // device is in operational mode, we continue to query config as usual
                if (!ConnectAuth())
                {
                    ShowLog("Failed\n");
                    return null;
                }
                var deviceConfig = QueryDeviceConfig();
                _authenticator.Disconnect();
                if (deviceConfig.HasValue)
                {
                    device.DeviceConfig = deviceConfig.Value;
                    _operationMode = (OperationMode)(int)deviceConfig.Value.algoFlow;
                    _deviceState.PreviewConfig.portraitMode =
                        device.DeviceConfig.cameraRotation == DeviceConfig.CameraRotation.Rotation_0_Deg ||
                        device.DeviceConfig.cameraRotation == DeviceConfig.CameraRotation.Rotation_180_Deg;
                    device.PreviewConfig = new PreviewConfig
                    {
                        deviceType = device.deviceType,
                        cameraNumber = Settings.Default.CameraNumber,
                        previewMode = _deviceState.PreviewConfig.previewMode,
                        portraitMode = _deviceState.PreviewConfig.portraitMode
                    };
                }
            }

            return device;
        }
#if RSID_SECURE
        private bool PairDevice()
        {
            ShowLog("Pairing..");
            IntPtr pairArgsHandle = IntPtr.Zero;
            pairArgsHandle = rsid_create_pairing_args_example(_signatureHelpeHandle);
            var pairingArgs = (PairingArgs)Marshal.PtrToStructure(pairArgsHandle, typeof(PairingArgs));

            var rv = _authenticator.Pair(ref pairingArgs);
            if (rv != Status.Ok)
            {
                ShowLog($"Pairing Failed {rv}\n");
                if (pairArgsHandle != IntPtr.Zero) rsid_destroy_pairing_args_example(pairArgsHandle);
                return false;
            }

            ShowLog("Pairing Success\n");
            rsid_update_device_pubkey_example(_signatureHelpeHandle, Marshal.UnsafeAddrOfPinnedArrayElement(pairingArgs.DevicePubkey, 0));
            return true;
        }

        private bool UnpairDevice()
        {
            ShowLog("Unpairing..");
            var rv = _authenticator.Unpair();
            if (rv != Status.Ok)
            {
                ShowLog("Unpair error " + rv);
                return false;
            }
            ShowLog("Unpairing success");
            return true;
        }
#else
        private bool PairDevice()
        {
            return true;
        }

        private bool UnpairDevice()
        {
            return true;
        }

#endif

        /// When adding new enum members to DeviceState or its sub-structs,
        /// make sure to update this struct and the corresponding `ToSerialized()` method.
        internal struct DeviceState
        {
            public DeviceType deviceType;
            public string FirmwareVersion;
            public string RecognitionVersion;
            public string SerialNumber;
            public bool IsOperational;
            public bool IsCompatible;
            public SerialConfig SerialConfig;
            public PreviewConfig PreviewConfig;
            public DeviceConfig DeviceConfig;
            public bool IsF500;

            public SerializableDeviceState ToSerialized()
            {
                return new SerializableDeviceState
                {
                    DeviceType = deviceType.ToString(), // convert enum to string
                    FirmwareVersion = FirmwareVersion,
                    RecognitionVersion = RecognitionVersion,
                    SerialNumber = SerialNumber,
                    IsOperational = IsOperational,
                    IsCompatible = IsCompatible,
                    SerialConfig = SerialConfig,
                    IsF500 = IsF500,
                    PreviewConfig = PreviewConfig.ToSerialized(),
                    DeviceConfig = DeviceConfig.ToSerialized()
                };
            }
        }

        /// Serializable version of DeviceState for JSON export
        /// NOTE:
        /// Enum values are converted to strings because raw integer values are not
        /// human-readable in JSON and can be unclear during debugging, logging, or manual inspection.
        internal struct SerializableDeviceState
        {
            public string DeviceType;
            public string FirmwareVersion;
            public string RecognitionVersion;
            public string SerialNumber;
            public bool IsOperational;
            public bool IsCompatible;
            public bool IsF500;
            public SerialConfig SerialConfig; // SerialConfig contains no enums - reused directly

            // Must be serialized to string-based version
            public SerializablePreviewConfig PreviewConfig;
            public SerializableDeviceConfig DeviceConfig;
        }

        private DeviceState DetectDevice()
        {
            DeviceInfo deviceInfo = new DeviceInfo();
            // if command line contains com port use it instead of auto detecting
            string[] args = Environment.GetCommandLineArgs();
            if (args.Length > 1)
            {
                deviceInfo.SerialPort = args[1];
                deviceInfo.DeviceType = rsid.Discover.DiscoverDeviceType(deviceInfo.SerialPort);
            }

            // auto detect com port
            else if (Settings.Default.AutoDetect)
            {
                ShowLog("Detecting Devices..");
                var devices = rsid.Discover.DiscoverDevices();
                foreach (var d in devices)
                {
                    ShowLog($"{d.SerialPort}, {d.DeviceType}, \"{d.SerialNumber}\"");
                }

                if (devices.Length == 0)
                {
                    var msg = "Could not detect device.\nPlease reconnect the device and try again.";
                    ShowErrorMessage("Connection Error", msg);
                    throw new Exception("Connection Error");
                }
                else if (devices.Length > 1)
                {
                    var msg = "More than one device detected.\nPlease make sure only one device is connected and try again.";
                    ShowErrorMessage("Connection Error", msg);
                    throw new Exception("Connection Error");
                }

                deviceInfo = devices[0];
            }
            else
            {
                // Use the device at the given port. Detect it's type using DiscoverDeviceType(port)
                deviceInfo = new DeviceInfo
                {
                    SerialPort = Settings.Default.Port
                };
                deviceInfo.DeviceType = Discover.DiscoverDeviceType(deviceInfo.SerialPort);
                if (deviceInfo.DeviceType == DeviceType.Unknown)
                {
                    throw new Exception("Cannot detect device on port " + deviceInfo.SerialPort);
                }
            }

            var device = QueryDeviceMetadata(deviceInfo);
            if (!device.HasValue)
            {
                var msg = "Could not connect to device.\nPlease reconnect the device and try again.";
                ShowErrorMessage("Connection Error", msg);
                throw new Exception("Connection Error");
            }

            return device.Value;
        }

        // 1. Query some initial info from the device:
        //   * FW Version
        //   * Auth settings
        //   * List of enrolled users
        // 2. Connect and pair to the device
        // 3. Start preview
        private void InitialSession(Object threadContext)
        {
            ShowProgressTitle("Connecting...");

            // show host library version
            var hostVersion = Authenticator.Version();
            ShowLog("Host: v" + hostVersion + "\n");
            try
            {
                _deviceState = DetectDevice();
            }
            catch (Exception ex)
            {
                OnStopSession();
                ShowErrorMessage("Connection Error", ex.Message);
                return;
            }

            // is in loader
            if (!_deviceState.IsOperational)
            {
                OnStopSession();

                var compatibleF450 = Authenticator.CompatibleFirmwareVersion(DeviceType.F45x);
                var compatibleF500 = Authenticator.CompatibleFirmwareVersion(DeviceType.F50x);

                ShowFailedTitle("Device Error");
                var msg = $"Device failed to respond. Please reconnect the device and try again." +
                    $"\nIf the the issue persists, flash firmware version f45x v{compatibleF450} or F50x v{compatibleF500}.\n";
                ShowLog(msg);
                ShowErrorMessage("Device Error", msg);

                return;
            }

            if (!_deviceState.IsCompatible)
            {
                OnStopSession();

                var compatibleVersion = Authenticator.CompatibleFirmwareVersion(_deviceState.deviceType);

                ShowFailedTitle("FW Incompatible");
                var msg = $"Firmware version is incompatible.\nPlease update to version {compatibleVersion} or newer.\n";
                ShowLog(msg);
                ShowErrorMessage("Firmware Version Error", msg);
                return;
            }

            try
            {
                if (!ConnectAuth())
                {
                    throw new Exception("Connection failed");
                }

                bool isPaired = PairDevice();
                if (!isPaired)
                {
                    ShowErrorMessage("Pairing Error", "Device pairing failed.\nPlease make sure the device wasn't previously paired and try again.");
                    throw new Exception("Pairing failed");
                }
                UpdateAdvancedMode();

                // start preview
                _deviceState.PreviewConfig.deviceType = _deviceState.deviceType;
                _deviceState.PreviewConfig.cameraNumber = Settings.Default.CameraNumber;
                _deviceState.PreviewConfig.portraitMode = _deviceState.DeviceConfig.cameraRotation == DeviceConfig.CameraRotation.Rotation_0_Deg ||
                    _deviceState.DeviceConfig.cameraRotation == DeviceConfig.CameraRotation.Rotation_180_Deg;
                _deviceState.PreviewConfig.rotateRaw = Settings.Default.RawRotate;

                // if device in full dump mode, show raw10 preview only
                if (_deviceState.DeviceConfig.dumpMode == DeviceConfig.DumpMode.FullFrame)
                {
                    _deviceState.PreviewConfig.previewMode = PreviewMode.RAW10_1080P;
                    InvokePreviewVisibility(Visibility.Hidden);

                }

                if (_preview == null)
                    _preview = new Preview(_deviceState.PreviewConfig);
                else
                    _preview.UpdateConfig(_deviceState.PreviewConfig);
                _preview.Start(OnPreview, OnSnapshot);
                if (_flowMode == FlowMode.Server)
                    RefreshUserListServer();
                else
                    RefreshUserList();

                // if f500 start device temperature poller
                if (_deviceState.deviceType == DeviceType.F50x)
                {
                    _tempertatureTimer = new Timer(TemperaturePoller, null, TimeSpan.FromSeconds(3), TimeSpan.FromSeconds(30));
                }

                ShowSuccessTitle("Connected");
            }
            catch (Exception ex)
            {
                ShowErrorMessage("Authenticator Error", ex.Message);
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                OnStopSession();
                if (_authenticator != null)
                    _authenticator.Disconnect();
                NormalDispatch(() => UpdatePairingButtons(true));
            }
        }

        // Cancel job
        private void CancelJob(Object threadContext)
        {
            try
            {
                ShowProgressTitle("Cancel..");
                ShowLog("Cancel..");
                _cancelWasCalled = true;
                var status = _authenticator.Cancel();
                ShowLog($"Cancel status: {status}");
                _frameDumper?.Cancel();
                VerifyResult(status == Status.Ok, "Canceled", "Cancel failed");
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
        }

        // Enroll Job
        private void EnrollJob(Object threadContext)
        {
            var userId = threadContext as string;

            if (!ConnectAuth()) return;
            OnStartSession($"Enroll {userId}", true);
            IntPtr userIdCtx = Marshal.StringToHGlobalUni(userId);
            try
            {
                _lastEnrolledUserId = userId;
                ShowProgressTitle("Enroll in progress...");
                _busy = true;
                var enrollArgs = new EnrollArgs
                {
                    userId = userId,
                    hintClbk = OnEnrollHint,
                    resultClbk = OnEnrollResult,
                    progressClbk = OnEnrollProgress,
                    faceDetectedClbk = OnFaceDetected,
                    landmarksDetectedClbk = OnLandmarksDetected,
                    faceCroppedImageClbk = OnFaceCroppedImage,
                    ctx = userIdCtx
                };
                var status = _authenticator.Enroll(enrollArgs);
                if (status == Status.Ok && _lastEnrolledUserId != null)
                {
                    // give some time to show the success message and refresh user list
                    Task.Delay(1600).Wait();
                    HideEnrollingLabelPanel();
                    RefreshUserList();
                }
                else if (status != Status.Ok)
                {
                    ShowFailedTitle(status.ToString());
                }
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                OnStopSession();
                HideEnrollingLabelPanel();
                _busy = false;
                _authenticator.Disconnect();
                Marshal.FreeHGlobal(userIdCtx);
                _lastEnrolledUserId = null;
            }
        }


        // Enroll Job
        private bool EnrollImageJob(EnrollImageRecord enrollRecord, bool isBatch)
        {

            var success = false;
            IntPtr userIdCtx = IntPtr.Zero;
            const int maxImageSize = 10 * 1024 * 1024;
            if (!ConnectAuth()) return false;

            try
            {
                var (buffer, w, h, bitmap) = ImageHelper.ToBgr(enrollRecord.Filename, true);

                OnStartSession($"Enroll {enrollRecord.UserId}", true);
                userIdCtx = Marshal.StringToHGlobalUni(enrollRecord.UserId);

                // validate file not bigger than max allowed
                if (buffer.Length > maxImageSize * 3)
                    throw new Exception("File too big");

                // show uploaded image on preview panel
                NormalDispatch(() =>
                {
                    var bi = ImageHelper.BitmapToImageSource(bitmap);
                    // flip back horizontally since the preview canvas is flipped
                    var transform = new ScaleTransform { ScaleX = -1 };
                    var image = new Image
                    {
                        Source = bi,
                        RenderTransformOrigin = new Point(0.5, 0.5),
                        RenderTransform = transform,
                        Height = 250,
                    };
                    var border = new Border
                    {
                        BorderThickness = new Thickness(2),
                        BorderBrush = Brushes.White,
                        Child = image
                    };

                    PreviewCanvas.Children.Add(border);
                    Canvas.SetRight(border, 16);
                    Canvas.SetTop(border, 205);
                });

                ShowProgressTitle("Uploading To Device..");
                _busy = true;

                var status = _authenticator.EnrollImage(enrollRecord.UserId, buffer, w, h);
                if (status == EnrollStatus.Success && !isBatch)
                {
                    RefreshUserList();
                }

                var logMsg = status == EnrollStatus.Success ? "Enroll success" : status.ToString();
                VerifyResult(status == EnrollStatus.Success, logMsg, logMsg);
                success = status == EnrollStatus.Success;
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
                OnStopSession();
            }
            finally
            {
                if (!isBatch) OnStopSession();
                HideEnrollingLabelPanel();
                _busy = false;
                _authenticator.Disconnect();
                if (userIdCtx != IntPtr.Zero)
                    Marshal.FreeHGlobal(userIdCtx);
            }
            return success;
        }

        private bool EnrollImageHostJob(EnrollImageRecord enrollRecord, bool isBatch)
        {

            var success = false;
            IntPtr userIdCtx = IntPtr.Zero;
            const int maxImageSize = 10 * 1024 * 1024;
            if (!ConnectAuth()) return false;

            try
            {
                var (buffer, w, h, bitmap) = ImageHelper.ToBgr(enrollRecord.Filename, true);

                OnStartSession($"Enroll {enrollRecord.UserId}", true);
                userIdCtx = Marshal.StringToHGlobalUni(enrollRecord.UserId);

                // validate file not bigger than max allowed
                if (buffer.Length > maxImageSize * 3)
                    throw new Exception("File too big");

                // show uploaded image on preview panel
                NormalDispatch(() =>
                {
                    var bi = ImageHelper.BitmapToImageSource(bitmap);
                    // flip back horizontally since the preview canvas is flipped
                    var transform = new ScaleTransform { ScaleX = -1 };
                    var image = new Image
                    {
                        Source = bi,
                        RenderTransformOrigin = new Point(0.5, 0.5),
                        RenderTransform = transform,
                        Height = 250,
                    };
                    var border = new Border
                    {
                        BorderThickness = new Thickness(2),
                        BorderBrush = Brushes.White,
                        Child = image
                    };

                    PreviewCanvas.Children.Add(border);
                    Canvas.SetRight(border, 16);
                    Canvas.SetTop(border, 205);
                });

                ShowProgressTitle("Uploading To Device..");
                _busy = true;

                var faceprints = new rsid.Faceprints();
                var status = _authenticator.EnrollImageFeatureExtraction(enrollRecord.UserId, buffer, w, h, ref faceprints);
                if (status == EnrollStatus.Success && !isBatch)
                {
                    _db.Push(faceprints, enrollRecord.UserId);
                    _db.Save();
                    RefreshUserListServer();
                }

                var logMsg = status == EnrollStatus.Success ? "Enroll success" : status.ToString();
                VerifyResult(status == EnrollStatus.Success, logMsg, logMsg);
                success = status == EnrollStatus.Success;
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
                OnStopSession();
            }
            finally
            {
                if (!isBatch) OnStopSession();
                HideEnrollingLabelPanel();
                _busy = false;
                _authenticator.Disconnect();
                if (userIdCtx != IntPtr.Zero)
                    Marshal.FreeHGlobal(userIdCtx);
            }
            return success;
        }

        // Enroll Job
        private void EnrollExtractFaceprintsJob(Object threadContext)
        {
            var userId = threadContext as string;

            if (!ConnectAuth()) return;
            OnStartSession($"Enroll {userId}", true);
            try
            {
                _lastEnrolledUserId = userId + '\0';
                var enrollExtArgs = new EnrollExtractArgs
                {
                    hintClbk = OnEnrollHint,
                    resultClbk = OnEnrollExtractionResult,
                    progressClbk = OnEnrollProgress,
                    faceDetectedClbk = OnFaceDetected,
                    landmarksDetectedClbk = OnLandmarksDetected,
                    faceCroppedImageClbk = OnFaceCroppedImage,
                };
                Status status = _authenticator.EnrollExtractFaceprints(enrollExtArgs);
                if (status != Status.Ok)
                    ShowFailedTitle(status.ToString());
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                OnStopSession();
                HideEnrollingLabelPanel();
                _authenticator.Disconnect();

            }
        }

        // Wrapper method for use with thread pool.
        private void DeleteSingleUserJob(Object threadContext)
        {
            if (!ConnectAuth()) return;
            List<string> usersIds = (List<string>)threadContext;
            OnStartSession($"Delete {usersIds.Count} users", false);
            _busy = true;
            try
            {
                ShowProgressTitle("Deleting..");
                bool successAll = true;
                int progressCounter = 0;
                foreach (string userId in usersIds)
                {
                    progressCounter++;
                    if (usersIds.Count > 1)
                    {
                        ShowProgressTitle($"Deleting {progressCounter}/{usersIds.Count}");
                    }
                    ShowLog($"Delete user {userId}");
                    var status = _authenticator.RemoveUser(userId);
                    if (status == Status.Ok)
                    {
                        ShowLog("Detele Ok");
                    }
                    else
                    {
                        ShowLog("Failed");
                        successAll = false;
                    }
                }
                VerifyResult(successAll, "Delete success", "Delete failed");
                RefreshUserList();
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                OnStopSession();
                _authenticator.Disconnect();
            }
        }

        private void DeleteUsersJob(Object threadContext)
        {
            if (!ConnectAuth()) return;
            OnStartSession("Delete Users", false);
            try
            {
                _busy = true;
                ShowProgressTitle("Deleting..");
                var status = _authenticator.RemoveAllUsers();
                VerifyResult(status == Status.Ok, "Delete success", "Delete failed", RefreshUserList);
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                OnStopSession();
                _authenticator.Disconnect();
            }
        }

        private void DeleteSingleUserServerJob(Object threadContext)
        {
            List<string> usersIds = (List<string>)threadContext;

            try
            {
                ShowProgressTitle("Deleting..");
                _busy = true;
                bool successAll = true;
                foreach (string userId in usersIds)
                {
                    ShowLog($"Delete user {userId}");
                    var success = _db.Remove(userId);
                    if (success)
                    {
                        ShowLog("Delete Ok");
                    }
                    else
                    {
                        ShowLog("Delete Failed");
                        successAll = false;
                    }
                }
                _db.Save();
                VerifyResult(successAll, "Delete success", "Delete failed");
                RefreshUserListServer();
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                OnStopSession();
                _authenticator.Disconnect();
            }
        }

        private void DeleteUsersServerJob(Object threadContext)
        {
            if (!ConnectAuth()) return;
            OnStartSession("Delete Users", false);
            try
            {
                ShowProgressTitle("Deleting..");
                var success = _db.RemoveAll();
                _db.Save();
                VerifyResult(success, "Delete all success", "Delete all failed", RefreshUserListServer);
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                OnStopSession();
                _authenticator.Disconnect();
            }
        }

        // Authenticate job
        private void AuthenticateJob(Object threadContext)
        {
            if (!ConnectAuth()) return;
            OnStartSession("Authenticate", true);
            try
            {
                var authArgs = new AuthArgs
                {
                    hintClbk = OnAuthHint,
                    resultClbk = OnAuthResult,
                    faceDetectedClbk = OnFaceDetected,
                    landmarksDetectedClbk = OnLandmarksDetected,
                    faceDistancesClbk = OnFaceDistances,
                    faceCroppedImageClbk = OnFaceCroppedImage,
                    ctx = IntPtr.Zero
                };

                _busy = true;
                PausePreview();
                var sw = Stopwatch.StartNew();
                ExecuteAlgoFlow(_operationMode, authArgs, false);
                ShowLog($"{sw.ElapsedMilliseconds} milliseconds");
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                ResumePreviewAfter(500);
                OnStopSession();
                HideAuthenticatingLabelPanel();
                _busy = false;
                _authenticator.Disconnect();
            }
        }

#if RSID_ONE2ONE
        private void ToggleOneToOne(bool isEnabled)
        {
            if (OneToOneToggle.IsChecked.GetValueOrDefault())
            {
                EnrollButton.IsEnabled = false;
                BatchEnrollButton.IsEnabled = false;
                EnrollImgButton.IsEnabled = isEnabled;
                AuthenticateImgButton.Visibility = Visibility.Visible;
                AuthenticateImgButton.IsEnabled = isEnabled && _flowMode == FlowMode.Device;
                AuthenticateLoopToggle.Visibility = Visibility.Collapsed;
                AuthenticateLoopToggle.IsEnabled = false;

            }
            else
            {
                bool isEnrollEnabled = isEnabled && IsRecogEnabled();
                EnrollButton.IsEnabled = isEnrollEnabled;
                BatchEnrollButton.IsEnabled = isEnrollEnabled;
                EnrollImgButton.IsEnabled = isEnrollEnabled;
                AuthenticateImgButton.Visibility = Visibility.Collapsed;
                AuthenticateImgButton.IsEnabled = false;
                AuthenticateLoopToggle.Visibility = Visibility.Visible;
                AuthenticateLoopToggle.IsEnabled = isEnabled;
            }
        }
#endif

        private void OneToOneCheckBox_Checked(object sender, RoutedEventArgs e)
        {
#if RSID_ONE2ONE
            ToggleOneToOne(true);
#endif
        }

        private void OneToOneCheckBox_Unchecked(object sender, RoutedEventArgs e)
        {
#if RSID_ONE2ONE
            ToggleOneToOne(true);
#endif
        }

        private void AuthenticateImgButton_Click(object sender, RoutedEventArgs e)
        {
#if RSID_ONE2ONE
            var openFileDialog = new OpenFileDialog
            {
                CheckFileExists = true,
                Multiselect = false,
                Title = "Select Image to Authenticate",
                Filter = "Images|*.png;*.jpg;*.jpeg;*.bmp;",
                FilterIndex = 1
            };
            if (openFileDialog.ShowDialog() == false)
                return;
            Task.Run(() => AuthenticateImageOne2OneJob(openFileDialog.FileName));
#endif
        }

#if RSID_ONE2ONE
        // Enroll Job
        private bool EnrollImageOneToOneJob(EnrollImageRecord enrollRecord, bool isBatch)
        {
            var success = false;
            IntPtr userIdCtx = IntPtr.Zero;
            if (!ConnectAuth()) return false;

            try
            {
                var (buffer, w, h, bitmap) = ImageHelper.ToBgr(enrollRecord.Filename, false);

                OnStartSession($"Enroll {enrollRecord.UserId}", true);
                userIdCtx = Marshal.StringToHGlobalUni(enrollRecord.UserId);

                // show uploaded image on preview panel
                NormalDispatch(() =>
                {
                    var bi = ImageHelper.BitmapToImageSource(bitmap);
                    // flip back horizontally since the preview canvas is flipped
                    var transform = new ScaleTransform { ScaleX = -1 };
                    var image = new Image
                    {
                        Source = bi,
                        RenderTransformOrigin = new Point(0.5, 0.5),
                        RenderTransform = transform,
                        Height = 250,
                    };
                    var border = new Border
                    {
                        BorderThickness = new Thickness(2),
                        BorderBrush = Brushes.White,
                        Child = image
                    };

                    PreviewCanvas.Children.Add(border);
                    Canvas.SetRight(border, 16);
                    Canvas.SetTop(border, 205);
                });

                ShowProgressTitle("Uploading To Device..");
                _busy = true;

                var status = _authenticator.EnrollImageOneToOne(enrollRecord.UserId, buffer, w, h);

                var logMsg = status == EnrollStatus.Success ? "Enroll success" : status.ToString();
                VerifyResult(status == EnrollStatus.Success, logMsg, logMsg);
                success = status == EnrollStatus.Success;
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
                OnStopSession();
            }
            finally
            {
                if (!isBatch) OnStopSession();
                HideEnrollingLabelPanel();
                _busy = false;
                _authenticator.Disconnect();
                if (userIdCtx != IntPtr.Zero)
                    Marshal.FreeHGlobal(userIdCtx);
            }
            return success;
        }

        // Authenticate Job
        private bool AuthenticateImageOne2OneJob(string filename)
        {
            var success = false;
            const int maxImageSize = 10 * 1024 * 1024;
            if (!ConnectAuth()) return false;

            try
            {
                var (buffer, w, h, bitmap) = ImageHelper.ToBgr(filename, false);

                // validate file not bigger than max allowed
                if (buffer.Length > maxImageSize * 3)
                    throw new Exception("File too big");

                // show uploaded image on preview panel
                NormalDispatch(() =>
                {
                    var bi = ImageHelper.BitmapToImageSource(bitmap);
                    // flip back horizontally since the preview canvas is flipped
                    var transform = new ScaleTransform { ScaleX = -1 };
                    var image = new Image
                    {
                        Source = bi,
                        RenderTransformOrigin = new Point(0.5, 0.5),
                        RenderTransform = transform,
                        Height = 250,
                    };
                    var border = new Border
                    {
                        BorderThickness = new Thickness(2),
                        BorderBrush = Brushes.White,
                        Child = image
                    };

                    PreviewCanvas.Children.Add(border);
                    Canvas.SetRight(border, 16);
                    Canvas.SetTop(border, 205);
                });

                ShowProgressTitle("Uploading To Device..");
                _busy = true;
                string userId = "";
                short score = 0;
                var status = _authenticator.AuthenticateImageOneToOne(buffer, w, h, ref userId, ref score);
                if (status != AuthStatus.Success)
                {
                    if (score > 0)
                        ShowFailedTitle(status.ToString() + "(" + score + ")");
                    else
                        ShowFailedTitle(status.ToString());
                }
                else
                    ShowSuccessTitle(userId + "(" + score + ")");
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                OnStopSession();
                HideAuthenticatingLabelPanel();
                _busy = false;
                _authenticator.Disconnect();
            }
            return success;
        }

        // Authenticate job
        private void AuthenticateOneToOneJob(Object threadContext)
        {
            if (!ConnectAuth()) return;
            OnStartSession("Authenticate", true);
            try
            {
                var authArgs = new AuthArgs
                {
                    hintClbk = OnAuthHint,
                    resultClbk = OnAuthResult,
                    faceDetectedClbk = OnFaceDetected,
                    landmarksDetectedClbk = OnLandmarksDetected,
                    faceDistancesClbk = OnFaceDistances,
                    faceCroppedImageClbk = OnFaceCroppedImage,
                    ctx = IntPtr.Zero
                };

                ShowProgressTitle("Authenticating..");
                _busy = true;
                // don't show refresh preview while authenticating
                PausePreview();
                var sw = Stopwatch.StartNew();
                Status status = _authenticator.AuthenticateOneToOne(authArgs);
                ShowLog($"{sw.ElapsedMilliseconds} milliseconds");
                if (status != Status.Ok)
                    ShowFailedTitle(status.ToString());
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                ResumePreviewAfter(500);
                OnStopSession();
                HideAuthenticatingLabelPanel();
                _busy = false;
                _authenticator.Disconnect();
            }
        }

        // Extract faceprints job using host only and save in memory the result
        private bool EnrollImageOne2OneHostJob(EnrollImageRecord enrollRecord, bool isBatch)
        {
            var success = false;
            try
            {
                var (buffer, w, h, bitmap) = ImageHelper.ToBgr(enrollRecord.Filename, false);
                OnStartSession($"Enroll {enrollRecord.UserId}", true);
                // show uploaded image on preview panel
                NormalDispatch(() =>
                {
                    var bi = ImageHelper.BitmapToImageSource(bitmap);
                    // flip back horizontally since the preview canvas is flipped
                    var transform = new ScaleTransform { ScaleX = -1 };
                    var image = new Image
                    {
                        Source = bi,
                        RenderTransformOrigin = new Point(0.5, 0.5),
                        RenderTransform = transform,
                        Height = 250,
                    };
                    var border = new Border
                    {
                        BorderThickness = new Thickness(2),
                        BorderBrush = Brushes.White,
                        Child = image
                    };

                    PreviewCanvas.Children.Add(border);
                    Canvas.SetRight(border, 16);
                    Canvas.SetTop(border, 205);
                });

                ShowProgressTitle("Extracting Features..");
                _busy = true;

                var features = new rsid.ExtractedFaceprints();
                var status = _authenticator.ExtractFaceprintsOnHost(buffer, w, h, ref features);

                var logMsg = status == Status.Ok ? "Extract success" : status.ToString();
                VerifyResult(status == Status.Ok, logMsg, logMsg);
                if (status == Status.Ok)
                {
                    _lastEnrolledUserId = enrollRecord.UserId;
                    // convert to Faceprints object (db format)
                    _lastOne2OneEnrolledFaceprints.featuresType = features.featuresType;
                    _lastOne2OneEnrolledFaceprints.version = features.version;
                    _lastOne2OneEnrolledFaceprints.flags = features.flags;
                    _lastOne2OneEnrolledFaceprints.adaptiveDescriptorWithoutMask = features.featuresVector;

                    RefreshUserListServer();
                }
                success = status == Status.Ok;

            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
                OnStopSession();
            }
            finally
            {
                if (!isBatch) OnStopSession();
                HideEnrollingLabelPanel();
                _busy = false;
            }
            return success;
        }

        private void OnAuthExtractionResultOne2One(AuthStatus status, IntPtr faceprintsHandle, IntPtr ctx)
        {
            ShowLog($"OnAuthExtractionResult status: {status}");
            if (_cancelWasCalled)
            {
                ShowSuccessTitle("Canceled");
            }
            else if (status == rsid.AuthStatus.Success)
            {
                var faceprints = (rsid.ExtractedFaceprints)Marshal.PtrToStructure(faceprintsHandle, typeof(rsid.ExtractedFaceprints));
                rsid.MatchElement faceprintsToMatchObject = new rsid.MatchElement
                {
                    version = faceprints.version,
                    flags = 0,
                    featuresVector = faceprints.featuresVector
                };

                MatchArgs matchArgs = new MatchArgs
                {
                    newFaceprints = faceprintsToMatchObject,
                    existingFaceprints = _lastOne2OneEnrolledFaceprints,
                    updatedFaceprints = new Faceprints(),
                    matcherConfidenceLevel = _deviceState.DeviceConfig.matcherConfidenceLevel
                };

                var matchResult = _authenticator.MatchFaceprintsToFaceprints(ref matchArgs);
                ShowLog($"Match result: {matchResult.success}, Score: {matchResult.score}");
                if (matchResult.success == 1)
                {
                    VerifyResultAuth(AuthStatus.Success, _lastEnrolledUserId, "Authenticated");
                }
                else
                {
                    VerifyResultAuth(AuthStatus.Forbidden, string.Empty, "Forbidden");
                }
            }
            else
            {
                VerifyResultAuth(status, string.Empty, status.ToString());
            }
        }

        // Authenticate faceprints extraction job using previously extracted faceprints
        private void AuthenticateExtractFaceprintsOne2OneJob(Object threadContext)
        {
            if (!ConnectAuth()) return;
            OnStartSession("Extracting Faceprints", true);
            try
            {
                var authExtArgs = new AuthExtractArgs
                {
                    hintClbk = OnAuthHint,
                    resultClbk = OnAuthExtractionResultOne2One,
                    faceDetectedClbk = OnFaceDetected,
                    landmarksDetectedClbk = OnLandmarksDetected,
                    faceDistancesClbk = OnFaceDistances,
                    faceCroppedImageClbk = OnFaceCroppedImage,
                    ctx = IntPtr.Zero
                };
                ShowProgressTitle("Extracting Faceprints");
                _busy = true;
                PausePreview();
                Status status = _authenticator.AuthenticateExtractFaceprints(authExtArgs);
                if (status != Status.Ok)
                    ShowFailedTitle(status.ToString());
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                ResumePreviewAfter(500);
                OnStopSession();
                HideAuthenticatingLabelPanel();
                _authenticator.Disconnect();
                _busy = false;

            }
        }
#endif //RSID_ONE2ONE

        // Authentication loop job
        private void AuthenticateLoopJob(Object threadContext)
        {
            if (!ConnectAuth()) return;
            OnStartSession("Authenticate Loop", true);
            try
            {
                var authArgs = new AuthArgs
                {
                    hintClbk = OnAuthHint,
                    resultClbk = OnAuthResult,
                    faceDetectedClbk = OnFaceDetected,
                    landmarksDetectedClbk = OnLandmarksDetected,
                    faceDistancesClbk = OnFaceDistances,
                    faceCroppedImageClbk = OnFaceCroppedImage,
                    ctx = IntPtr.Zero
                };

                _busy = true;
                ExecuteAlgoFlow(_operationMode, authArgs, true);
            }
            catch (Exception ex)
            {
                try
                {
                    _authenticator.Cancel();
                }
                catch
                {
                    // ignored
                }

                ShowFailedTitle(ex.Message);
            }
            finally
            {
                OnStopSession();
                HideAuthenticatingLabelPanel();
                _busy = false;
                _authenticator.Disconnect();
            }
        }

        // Authenticate faceprints extraction job
        private void AuthenticateExtractFaceprintsJob(Object threadContext)
        {
            if (!ConnectAuth()) return;
            OnStartSession("Extracting Faceprints", true);
            try
            {
                var authExtArgs = new AuthExtractArgs
                {
                    hintClbk = OnAuthHint,
                    resultClbk = OnAuthExtractionResult,
                    faceDetectedClbk = OnFaceDetected,
                    landmarksDetectedClbk = OnLandmarksDetected,
                    faceDistancesClbk = OnFaceDistances,
                    faceCroppedImageClbk = OnFaceCroppedImage,
                    ctx = IntPtr.Zero
                };
                ShowProgressTitle("Extracting Faceprints");
                _busy = true;
                PausePreview();
                Status status = _authenticator.AuthenticateExtractFaceprints(authExtArgs);
                if (status != Status.Ok)
                    ShowFailedTitle(status.ToString());
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                ResumePreviewAfter(500);
                OnStopSession();
                HideAuthenticatingLabelPanel();
                _authenticator.Disconnect();
                _busy = false;

            }
        }

        // Authenticate loop faceprints extraction job
        private void AuthenticateExtractFaceprintsLoopJob(Object threadContext)
        {
            if (!ConnectAuth()) return;

            OnStartSession("Authentication faceprints extraction loop", true);
            try
            {
                var authLoopExtArgs = new AuthExtractArgs
                {
                    hintClbk = OnAuthHint,
                    resultClbk = OnAuthLoopExtractionResult,
                    faceDetectedClbk = OnFaceDetected,
                    landmarksDetectedClbk = OnLandmarksDetected,
                    faceDistancesClbk = OnFaceDistances,
                    faceCroppedImageClbk = OnFaceCroppedImage,
                    ctx = IntPtr.Zero
                };
                ShowProgressTitle("Authenticating...");
                _busy = true;
                _authenticator.AuthenticateLoopExtractFaceprints(authLoopExtArgs);
            }
            catch (Exception ex)
            {
                try
                {
                    _authenticator.Cancel(); //try to cancel the auth loop
                }
                catch
                {
                    // ignored
                }

                ShowFailedTitle(ex.Message);
            }
            finally
            {
                OnStopSession();
                HideAuthenticatingLabelPanel();
                _busy = false;
                _authenticator.Disconnect();
            }
        }

        private void UnlockJob(Object threadContext)
        {
            if (!ConnectAuth()) return;
            OnStartSession("Unlock", false);
            try
            {
                ShowProgressTitle("Unlocking..");
                var status = _authenticator.Unlock();
                VerifyResult(status == Status.Ok, "Unlock success", "Unlock failed");
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                OnStopSession();
                _authenticator.Disconnect();
            }
        }

        private void SetHostDatabasePath()
        {
            if (File.Exists(Database.GetDatabseDefaultPath()))
            {
                var result = Application.Current.Dispatcher.Invoke(() =>
                {
                    var dialogResult = ShowWindowDialog(new OKCancelDialog("Existing DB detected!",
                        "Load existing default DB?"));
                    if (dialogResult == true)
                    {
                        _db = new Database();
                        return true;
                    }
                    return false;
                });
                if (result)
                    return;
            }

            OpenFileDialog openFileDialog = new OpenFileDialog()
            {
                Multiselect = false,
                Title = "Select Database File",
                Filter = "db files (*.db)|*.db",
                FilterIndex = 1,
                CheckFileExists = false
            };
            if (openFileDialog.ShowDialog() == true)
            {
                var dbfilename = openFileDialog.FileName;
                _db = new Database(dbfilename);
            }
            else
            {
                ShowErrorMessage("Default DB",
                    "No db file selected.\nUsing the default path (<current dir>/db.db)");
                _db = new Database();
            }

        }

        // SetDeviceConfig job
        private void SetDeviceConfigJob(Object threadContext)
        {
            if (!ConnectAuth()) return;

            (DeviceConfig? prevDeviceConfig, var deviceConfig, var prevPreviewConfig, var PreviewConfig, var flowMode, var viewerAlgoFlow) =
                ((DeviceConfig?, DeviceConfig, PreviewConfig, PreviewConfig, FlowMode, OperationMode))threadContext;
            OnStartSession("SetDeviceConfig", false);
            try
            {
                // DB on Host is not supported with Spoof Only or Face Detection Only algo flows
                if (flowMode == FlowMode.Server &&
                    (deviceConfig.algoFlow == DeviceConfig.AlgoFlow.SpoofOnly ||
                     deviceConfig.algoFlow == DeviceConfig.AlgoFlow.FaceDetectionOnly))
                {
                    ShowLog("DB on Host is not supported with Spoof Only or Face Detection Only algo flows");
                    throw new Exception("Invalid settings");                
                }
                
                ShowProgressTitle("SetDeviceConfig");

                PreviewConfig.deviceType = _deviceState.deviceType;
                // Must use raw10 if full dump mode enabled
                if (deviceConfig.dumpMode == DeviceConfig.DumpMode.FullFrame)
                {
                    PreviewConfig.previewMode = PreviewMode.RAW10_1080P;
                }
                else // if not not in full dump mode, make sure we not in raw 10 preview
                {
                    if (PreviewConfig.previewMode == PreviewMode.RAW10_1080P)
                    {
                        PreviewConfig.previewMode = PreviewMode.MJPEG_1080P;
                    }
                }

                _deviceState.PreviewConfig = PreviewConfig;
                LogDeviceConfig(deviceConfig);

                if (prevDeviceConfig.HasValue)
                {
                    DeviceConfig prevDeviceConfigValue = prevDeviceConfig.Value;
                    ShowLog("Detected changes. Updating settings on device...");
                    var status = _authenticator.SetDeviceConfig(deviceConfig);
                    if (status != Status.Ok)
                    {
                        throw new Exception(status.ToString());
                    }

                    PreviewConfig.deviceType = prevPreviewConfig.deviceType;
                    // restart preview with new config if needed
                    var previewChanged =
                        prevPreviewConfig.previewMode != PreviewConfig.previewMode ||
                        prevPreviewConfig.portraitMode != PreviewConfig.portraitMode ||
                        prevPreviewConfig.rotateRaw != PreviewConfig.rotateRaw ||
                        prevPreviewConfig.cameraNumber != PreviewConfig.cameraNumber;
                    if (previewChanged)
                    {
                        _deviceState.PreviewConfig = PreviewConfig;
                        _preview?.Stop();
                        _preview?.UpdateConfig(_deviceState.PreviewConfig);
                        _preview?.Start(OnPreview, OnSnapshot);
                    }

                    _deviceState.DeviceConfig = deviceConfig;
                    _operationMode = viewerAlgoFlow;

                    if (_deviceState.PreviewConfig.previewMode != PreviewMode.RAW10_1080P)
                        InvokePreviewVisibility(Visibility.Visible);
                    else
                        InvokePreviewVisibility(Visibility.Hidden);
                }


                if (flowMode != _flowMode)
                {
                    _flowMode = flowMode;

                    if (flowMode == FlowMode.Server)
                    {

                        SetHostDatabasePath();

                        Dispatcher.Invoke(() =>
                        {
                            ImportButton.IsEnabled = false;
                            ExportButton.IsEnabled = false;
                        });

                        int loadStatus = _db.Load();

                        if (loadStatus < 0)
                        {
                            HandleDbErrorServer();
                            ShowLog("Error occured during load the DB. This may be due to faceprints version mismatch or other error. Saved backup and started empty DB.\n");
                            string guimsg = "DB load error. Saved backup and stared empty DB.";
                            VerifyResult(false, string.Empty, guimsg);
                            return;
                        }
                        else
                        {
                            RefreshUserListServer();
                        }

                    }
                    else
                    {
                        RefreshUserList();
                    }
                }

                VerifyResult(true, "Apply settings done", "Apply settings failed");
            }
            catch (Exception ex)
            {
                ShowFailedTitle(ex.Message);
            }
            finally
            {
                OnStopSession();
                _authenticator.Disconnect();
            }
        }


        // Update fw job using the given filename.
        // Update process:
        // 1. Check if the requested fw is compatible with current host
        // 2. Stop preview
        // 3. Update and show feeback on the progress
        // 4. If success, re connect to the device query the new version
        // 5. Start preview
        private void FwUpdateJob(Object threadContext)
        {
            var args = (Tuple<string, bool>)(threadContext);
            var (binPath, forceUpdate) = args;
            var deviceType = Discover.DiscoverDeviceType(_deviceState.SerialConfig.port);
            using (var fwUpdater = new FwUpdater(deviceType))
            {
                NormalDispatch(() => _progressBar.Show());
                var versions = fwUpdater.ExtractFwVersion(binPath);
                var newFwVersion = versions?.OpfwVersion;

                if (newFwVersion == null)
                {
                    CloseProgressBar();
                    ShowErrorMessage("FW Update Error", "Unable to parse the selected firmware file.");
                    return;
                }

                var fwUpdateSettings = new FwUpdater.FwUpdateSettings
                {
                    port = _deviceState.SerialConfig.port,
                    force_full = forceUpdate ? 1 : 0
                };

                // Run hardware compatibility checks before the SDK version check so that SKU,
                // device type, and DB version warnings are always surfaced to the user even when
                // the firmware version is incompatible with the current SDK.
                fwUpdater.CheckCompatibility(fwUpdateSettings, binPath, out var compatInfo);
                if (!compatInfo.IsOtpSkuCompatible)
                {
                    CloseProgressBar();
                    string msg;
                    if (compatInfo.DeviceOtpSku < 0)
                        msg = "Failed to determine OTP encryption SKU.\nCheck device connection and try again.";
                    else
                        msg = $"Firmware OTP SKU mismatch: device is SKU{compatInfo.DeviceOtpSku}, firmware targets SKU{compatInfo.ExpectedOtpSku}.\n" +
                              $"Make sure the firmware file is for SKU{compatInfo.DeviceOtpSku} devices and try again.";
                    ShowErrorMessage("Incompatible firmware OTP SKU", msg);
                    return;
                }
                if (!compatInfo.IsSecureBootCompatible)
                {
                    CloseProgressBar();
                    string msg;
                    if (compatInfo.DeviceSecureBoot < 0)
                        msg = "Failed to determine secure boot variant.\nCheck device connection and try again.";
                    else
                    {
                        var expectedName = compatInfo.DeviceSecureBoot == 0 ? "is NOT SIGNED" : "is SIGNED";
                        msg = $"Firmware security mismatch: make sure the firmware file {expectedName} and try again.";
                    }
                    ShowErrorMessage("Incompatible firmware security variant", msg);
                    return;
                }
                if (!compatInfo.IsDeviceTypeCompatible)
                {
                    CloseProgressBar();
                    if (compatInfo.ConnectedDeviceType < 0 || compatInfo.ExpectedDeviceType < 0 || compatInfo.ConnectedDeviceType > 2 || compatInfo.ExpectedDeviceType > 2)
                    {
                        ShowErrorMessage("Incompatible firmware device type", "Failed to determine device type compatibility. Check device connection and try again.");
                        return;
                    }
                    var connectionDevice = compatInfo.ConnectedDeviceType == 0 ? "F45x" : compatInfo.ConnectedDeviceType == 1 ? "F46x" : "F50x";
                    var binaryDevice = compatInfo.ExpectedDeviceType == 0 ? "F45x" : compatInfo.ExpectedDeviceType == 1 ? "F46x" : "F50x";
                    var msg = $"Firmware device type mismatch ! connected device type ({connectionDevice}) does not match the selected firmware ({binaryDevice}).\n";
                    ShowErrorMessage("Incompatible firmware device type", msg);
                    return;
                }

                if (!compatInfo.IsDbCompatible)
                {
                    var msg = $"DB version mismatch (device: v{compatInfo.DeviceDbVer}, firmware: v{compatInfo.ExpectedDbVer}).\n\n" +
                              "The database may be erased after the update.\n Export it first if needed.\n\n" +
                              "Proceed with the firmware update?";
                    bool proceed = false;
                    Dispatcher.Invoke(() => { proceed = ShowWindowDialog(new OKCancelDialog("DB Version Mismatch", msg, isYesNo: true)) == true; });
                    if (!proceed)
                    {
                        CloseProgressBar();
                        return;
                    }
                }

                if (!forceUpdate)
                {
                    // todo gabi auto detect device type
                    var isCompatible =
                        _deviceState.deviceType == versions.Value.DeviceType &&
                        Authenticator.IsFwCompatibleWithHost(_deviceState.deviceType, newFwVersion);

                    if (!isCompatible)
                    {
                        CloseProgressBar();
                        var compatibleVer = Authenticator.CompatibleFirmwareVersion(_deviceState.deviceType);
                        var msg = "Selected firmware version is not compatible with this SDK.\n";
                        msg += $"Chosen Version:   {versions.Value.DeviceType} {newFwVersion}\n";
                        msg += $"Compatible Versions: {_deviceState.deviceType} {compatibleVer} and above\n";
                        ShowErrorMessage("Incompatible FW Version", msg);
                        return;
                    }
                }

                _authenticator?.Disconnect();
                _preview?.Stop();
                TogglePreviewOpacity(false);
                _deviceState.IsOperational = false;
                Thread.Sleep(100);

                OnStartSession("Firmware Update", false);
                bool success = false;
                try
                {
                    ShowProgressTitle("Updating Firmware..");
                    ShowLog("update to " + newFwVersion);

                    var eventHandler = new FwUpdater.EventHandler
                    {
                        progressClbk = (progress) => UpdateProgressBar(progress * 100)
                    };

                    var status = fwUpdater.Update(binPath, eventHandler, fwUpdateSettings);
                    success = status == Status.Ok;
                    if (!success)
                        throw new Exception("Update Failed");
                }
                catch (Exception ex)
                {
                    _deviceState.IsOperational = false;
                    success = false;
                    ShowFailedTitle(ex.Message);
                    ShowErrorMessage("Firmware Update Failed", "Please reconnect your device and try again.");
                }
                finally
                {
                    CloseProgressBar();
                    OnStopSession();

                    if (success)
                    {
                        ShowProgressTitle("Rebooting...");
                        Thread.Sleep(7500 + _userList.Length * 14);
                        TogglePreviewOpacity(true);
                        InitialSession(null);
                    }
                }
            }
        }

        private void PausePreview()
        {
            // if in raw10 dont pause preview so preview snapshots can be displayed
            if (_deviceState.PreviewConfig.previewMode != PreviewMode.RAW10_1080P)
                _pausePreview = true;
        }

        // Resume preview aftr the given delay
        private void ResumePreviewAfter(int delayMillis)
        {
            if (_pausePreview)
            {
                Task.Delay(delayMillis).Wait();
                _pausePreview = false;
            }
        }

#if RSID_SECURE
        private void UpdatePairingButtons(bool paired)
        {
            if (paired)
            {
                PairButton.Visibility = Visibility.Collapsed;
                PairButton.IsEnabled = false;
                UnpairButton.Visibility = Visibility.Visible;
                UnpairButton.IsEnabled = true;
            }
            else
            {
                PairButton.Visibility = Visibility.Visible;
                PairButton.IsEnabled = true;
                UnpairButton.Visibility = Visibility.Collapsed;
                UnpairButton.IsEnabled = false;
            }
        }
        private async void Unpair_Click(object sender, RoutedEventArgs e)
        {
            SetUiEnabled(false);
            var ok = await Task.Run(() =>
            {
                try
                {
                    ConnectAuth();
                    return UnpairDevice();
                }
                catch (Exception ex)
                {
                    ShowErrorMessage("Unpair", ex.Message);
                    return false;
                }
            });

            UpdatePairingButtons(!ok);
            if (ok)
            {
                new ErrorDialog("Device Successfully Unpaired", "Press the key button to pair the device again.").ShowDialog();
            }
        }

        private async void Pair_Click(object sender, RoutedEventArgs e)
        {
            SetUiEnabled(false);
            var ok = await Task.Run(() =>
            {
                try
                {
                    ConnectAuth();
                    return PairDevice();
                }
                catch (Exception ex)
                {
                    ShowErrorMessage("Pair", ex.Message);
                    return false;
                }
            });

            SetUiEnabled(ok);
            UpdatePairingButtons(ok);
        }
#else // Non secure mode. No pairing available
        private void UpdatePairingButtons(bool paired)
        {
            PairButton.Visibility = Visibility.Collapsed;
            UnpairButton.Visibility = Visibility.Collapsed;
        }

        private void Unpair_Click(object sender, RoutedEventArgs e)
        {
            throw new NotImplementedException();
        }

        private void Pair_Click(object sender, RoutedEventArgs e)
        {
            throw new NotImplementedException();
        }
#endif

        // Get temperature from device and display it
        void TemperaturePoller(Object o)
        {
            if (!_deviceState.IsOperational || _busy)
            {
                return;
            }
            var labelText = "";
            try
            {
                _busy = true;
                using (var controller = new DeviceController(_deviceState.deviceType))
                {
                    var connectStatus = controller.Connect(_deviceState.SerialConfig);
                    if (connectStatus == Status.Ok && controller.GetTemperature(out float soc, out float board) == Status.Ok)
                    {
                        labelText = $"SoC {soc:0.0} \u00B0C | Board {board:0.0} \u00B0C";
                    }
                }
            }
            catch (Exception ex)
            {
                ShowLog("TemperaturePoller: " + ex.Message);
            }
            finally
            {
                NormalDispatch(() => TemperatureLabel.Content = labelText);
                _busy = false;
            }
        }

        // Debug console support
        void CreateConsole()
        {
            AllocConsole();
            DeleteMenu(GetSystemMenu(GetConsoleWindow(), false), SC_CLOSE, MF_BYCOMMAND);
            ToggleConsoleAsync(false);
        }

        private const int MF_BYCOMMAND = 0x00000000;
        public const int SC_CLOSE = 0xF060;

        [DllImport("user32.dll")]
        public static extern int DeleteMenu(IntPtr hMenu, int nPosition, int wFlags);

        [DllImport("user32.dll")]
        private static extern IntPtr GetSystemMenu(IntPtr hWnd, bool bRevert);

        // show/hide console
        private void ToggleConsoleAsync(bool show)
        {
            const int SW_HIDE = 0;
            const int SW_SHOW = 5;
            ShowWindow(GetConsoleWindow(), show ? SW_SHOW : SW_HIDE);
        }

        void PowerButton_Click(object sender, RoutedEventArgs e)
        {
            var powerInput = new PowerDialog();
            if (ShowWindowDialog(powerInput) == true)
            {
                ThreadPool.QueueUserWorkItem(PowerJob, powerInput.Mode);
            }
        }

        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool AllocConsole();

        [DllImport("kernel32.dll")]
        static extern IntPtr GetConsoleWindow();

        [DllImport("user32.dll")]
        static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);

#if DEBUG
        private const string DllName = "rsid_secure_helper_debug";
#else
        private const string DllName = "rsid_secure_helper";
#endif //DEBUG
        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_create_example_sig_clbk();

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern void rsid_destroy_example_sig_clbk(IntPtr rsid_signature_clbk);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_get_host_pubkey_example(IntPtr rsid_signature_clbk);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_update_device_pubkey_example(IntPtr rsid_signature_clbk, IntPtr device_key);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern IntPtr rsid_create_pairing_args_example(IntPtr rsid_signature_clbk);

        [DllImport(DllName, CallingConvention = CallingConvention.Cdecl, CharSet = CharSet.Ansi)]
        static extern void rsid_destroy_pairing_args_example(IntPtr pairing_args);

    }
}