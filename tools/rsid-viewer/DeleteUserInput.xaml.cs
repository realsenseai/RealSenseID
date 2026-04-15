// License: Apache 2.0. See LICENSE file in root directory.
// Copyright(c) 2020-2021 RealSense, Inc. All Rights Reserved.

using System;
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
    public partial class DeleteUserInput : Window
    {
        public DeleteUserInput()
        {
            this.Owner = Application.Current.MainWindow;            
            InitializeComponent();            
        }

        private void DeleteUserOKButton_Click(object sender, RoutedEventArgs e)
        {
            DialogResult = true;
        }

        private void DeleteUserCancelButton_Click(object sender, RoutedEventArgs e)
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
