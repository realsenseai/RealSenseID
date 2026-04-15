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
        public ulong sw_version;
        public ulong fw_version;
        public string sw_version_str;
        public string fw_version_str;
        public string release_url;
        public string release_notes_url;
    }

    public partial class UpdateAvailableDialog : Window
    {
        public UpdateAvailableDialog(ReleaseInfo localInfo, ReleaseInfo remoteInfo)
        {
            this.Owner = Application.Current.MainWindow;
            InitializeComponent();            
            var updateAvailable = (remoteInfo.sw_version > localInfo.sw_version) || (remoteInfo.fw_version > localInfo.fw_version);            
            MainTitle.Text = updateAvailable ? "Update available" : "Software is up to date";

            if (remoteInfo.sw_version > localInfo.sw_version)
            {
                SoftwareUpdateInfo.Text = $"{localInfo.sw_version_str} => {remoteInfo.sw_version_str}";
            }
            else
            {
                SoftwareUpdateInfo.Text = $"{localInfo.sw_version_str} (up to date)";
            }

            if (remoteInfo.fw_version > localInfo.fw_version)
            {
                FirmwareUpdateInfo.Text = $"{localInfo.fw_version_str} => {remoteInfo.fw_version_str}";
            }
            else
            {
                FirmwareUpdateInfo.Text = $"{localInfo.fw_version_str} (up to date)";
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
            // Open the URL in the system's default web browser
            System.Diagnostics.Process.Start(new System.Diagnostics.ProcessStartInfo
            {
                FileName = e.Uri.AbsoluteUri,
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
