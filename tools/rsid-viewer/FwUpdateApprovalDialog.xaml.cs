// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

using System.Windows;
using System.Windows.Input;

namespace rsid_wrapper_csharp
{
    public partial class FwUpdateInput : Window
    {
        private bool updateRecognition;

        public FwUpdateInput(string title, string message)
        {
            this.Owner = Application.Current.MainWindow;
            InitializeComponent();

            UserApprovalTitle.Text = title;
            UserApprovalMessage.Text = message;
        }

        public bool UpdateRecognition()
        {
            return updateRecognition;
        }

        private void YesButton_Click(object sender, RoutedEventArgs e)
        {
            updateRecognition = true;
            DialogResult = true;
        }

        private void NoButton_Click(object sender, RoutedEventArgs e)
        {
            updateRecognition = false;
            DialogResult = true;
        }

        private void CancelButton_Click(object sender, RoutedEventArgs e)
        {
            DialogResult = false;
        }

        private void Window_MouseDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton == MouseButton.Left)
                this.DragMove();
        }
    }
}
