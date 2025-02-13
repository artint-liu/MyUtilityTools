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

            public List<string> _imagePaths;
            public List<string> ImagePaths 
            {
                get => _imagePaths;
                set
                {
                    _imagePaths = value;
                    OnPropertyChanged();
                }
            }

            private string _actor;
            private string _category;
            private string _details;

            public string Actor
            {
                get => _actor;
                set
                {
                    _actor = value;
                    OnPropertyChanged();
                }
            }
            public string Category
            {
                get => _category;
                set
                {
                    _category = value;
                    OnPropertyChanged();
                }
            }
            public string Details
            {
                get => _details;
                set
                {  
                    _details = value;
                    OnPropertyChanged();
                }
            }

            public event PropertyChangedEventHandler PropertyChanged;
            protected virtual void OnPropertyChanged([CallerMemberName] string propertyName = null)
            {
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
            }
        }


        MainWindow.ItemModel itemModel;

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
            textBox_Title.Text = itemModel.Title;
            textBox_Actor.Text = itemModel.Actor;
            textBox_Category.Text = itemModel.Category;
            textBox_Detail.Text = itemModel.Details;

            vm.ImagePaths = Utils.GenerateSequentialFiles(itemModel.ImagePath);

            this.itemModel = itemModel;

            if (this.Owner is MainWindow mainWindow)
            {
                var categoryWordsList = mainWindow.CategoryWordsList;
                foreach (var word in categoryWordsList)
                {
                    Button newButton = new Button
                    {
                        Content = word,
                        Margin = new Thickness(5),
                    };
                    newButton.Click += (s, e) =>
                    {
                        textBox_Category.Text += $",{(s as Button).Content}";
                    };
                    ButtonPanel.Children.Add(newButton);
                }
            }
           
        }

        private void listBox_Cover_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
            var vm = (MyViewModel)DataContext;
            vm.ImagePath = vm.ImagePaths[listBox_Cover.SelectedIndex];
        }

        private void Window_Closing(object sender, CancelEventArgs e)
        {
            var vm = (MyViewModel)DataContext;
            itemModel.ImagePath = vm.ImagePath;
            itemModel.Title = textBox_Title.Text;
            itemModel.Actor = textBox_Actor.Text;
            itemModel.Category = Utils.NormalizeCategory(textBox_Category.Text);
            itemModel.Details = textBox_Detail.Text;
        }
    }
}
