// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;

namespace rsid_wrapper_csharp
{
    public class ReleaseInfo
    {
        public string host_ver;
        public string fw_ver;
        public string release_url;
        public string release_notes_url;
        public bool fw_known = true;

        // Parse a dotted version string for component-wise comparison. Missing build/revision are
        // normalized to 0 so "9.16.0" and "9.16.0.0" compare equal. Throws on malformed input.
        public static Version ParseVersion(string label, string versionStr)
        {
            if (!Version.TryParse(versionStr, out var v))
                throw new FormatException($"Invalid {label} version '{versionStr}'");
            return new Version(v.Major, v.Minor, Math.Max(v.Build, 0), Math.Max(v.Revision, 0));
        }
    }

    public partial class UpdateAvailableDialog : Window
    {
        public UpdateAvailableDialog(ReleaseInfo localInfo, ReleaseInfo remoteInfo)
        {
            this.Owner = Application.Current.MainWindow;
            InitializeComponent();

            var fwVisible = localInfo.fw_known && remoteInfo.fw_known;
            var hostUpdate = ReleaseInfo.ParseVersion("host", remoteInfo.host_ver)
                           > ReleaseInfo.ParseVersion("host", localInfo.host_ver);
            var fwUpdate = fwVisible
                        && ReleaseInfo.ParseVersion("firmware", remoteInfo.fw_ver)
                         > ReleaseInfo.ParseVersion("firmware", localInfo.fw_ver);
            var updateAvailable = hostUpdate || fwUpdate;
            MainTitle.Text = updateAvailable ? "Update available" : "Software is up to date";

            SoftwareUpdateInfo.Text = hostUpdate
                ? $"{localInfo.host_ver} => {remoteInfo.host_ver}"
                : $"{localInfo.host_ver} (up to date)";

            if (fwVisible)
            {
                FirmwareUpdateInfo.Text = fwUpdate
                    ? $"{localInfo.fw_ver} => {remoteInfo.fw_ver}"
                    : $"{localInfo.fw_ver} (up to date)";
            }
            else
            {
                FirmwareUpdateTitle.Visibility = Visibility.Collapsed;
                FirmwareUpdateInfo.Visibility = Visibility.Collapsed;
            }

            UpdateUrlLink.NavigateUri = new Uri(remoteInfo.release_url);
            ReleaseNotesLink.NavigateUri = new Uri(remoteInfo.release_notes_url);
        }

        private void OKButton_Click(object sender, RoutedEventArgs e)
        {
            DialogResult = true;
        }

        private void Hyperlink_RequestNavigate(object sender, System.Windows.Navigation.RequestNavigateEventArgs e)
        {
            // Security fix: validate URI scheme to prevent command injection via Process.Start.
            // Only allow http/https URIs to be opened in the browser; reject all other schemes
            // (e.g. file://, cmd://, ms-settings:, etc.) that could execute arbitrary commands.
            var uri = e.Uri;
            if (uri.Scheme != Uri.UriSchemeHttp && uri.Scheme != Uri.UriSchemeHttps)
            {
                e.Handled = true;
                return;
            }

            // Open the URL in the system's default web browser
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = uri.AbsoluteUri,
                UseShellExecute = true
            });

            // Optionally, handle the navigation event so it doesn't propagate further
            e.Handled = true;
        }


        private void Window_MouseDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton == MouseButton.Left)
                this.DragMove();
        }
    }
}