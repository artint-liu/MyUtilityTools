using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Globalization;
using System.Linq;
using System.Runtime.CompilerServices;
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

namespace VideoExplorer
{
    /// <summary>
    /// VideoInfoSettings.xaml 的交互逻辑
    /// </summary>
    public partial class VideoInfoSettings : Window
    {
        public class MyViewModel : INotifyPropertyChanged
        {
            private string _imagePath;
            public string ImagePath
            {
                get => _imagePath;
                set
                {
                    _imagePath = value;
                    OnPropertyChanged();
                }
            }

            public event PropertyChangedEventHandler PropertyChanged;
            protected virtual void OnPropertyChanged([CallerMemberName] string propertyName = null)
            {
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
            }
        }

       

        //public string ImagePath { get; set; }
        public VideoInfoSettings()
        {
            InitializeComponent();
            DataContext = new MyViewModel();
        }

        public void SetVideoInfo(MainWindow.ItemModel itemModel)
        {
            //DataContext
            var vm = (MyViewModel)DataContext;
            vm.ImagePath = itemModel.ImagePath;

            //ImagePath = itemModel.ImagePath;
            //image_Cover.Source = new ImageSource(itemModel.fullPath);
        }
    }
}
