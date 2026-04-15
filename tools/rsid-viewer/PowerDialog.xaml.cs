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
    /// <summary>
    /// Interaction logic for EnrollInput.xaml
    /// </summary>
    public partial class PowerDialog : Window
    {
        public enum PowerMode
        {
            Standby,
            Hibernate,
            MountAndDump
        }

        public PowerDialog()
        {
            this.Owner = Application.Current.MainWindow;
            InitializeComponent();
        }

        public Nullable<PowerMode> Mode;

        private void OKButton_Click(object sender, RoutedEventArgs e)
        {
            DialogResult = true;
        }

        private void Window_MouseDown(object sender, MouseButtonEventArgs e)
        {
            if (e.ChangedButton == MouseButton.Left)
                this.DragMove();
        }

        private void Standby_Click(object sender, RoutedEventArgs e)
        {
            Mode = PowerMode.Standby;
            DialogResult = true;
        }

        private void Hibernate_Click(object sender, RoutedEventArgs e)
        {
            Mode = PowerMode.Hibernate;
            DialogResult = true;
        }

        private void CancelButton_Click(object sender, RoutedEventArgs e)
        {
            Mode = null;
            DialogResult = false;
        }

        private void DumpAndMount_Click(object sender, RoutedEventArgs e)
        {
            Mode = PowerMode.MountAndDump;
            DialogResult = true;

        }
    }
}
