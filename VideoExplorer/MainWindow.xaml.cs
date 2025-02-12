using System.Text;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Data;
using System.Windows.Documents;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Imaging;
using System.Windows.Navigation;
using System.Windows.Shapes;
using System.IO;
using Path = System.IO.Path;
using System.Collections.ObjectModel;

namespace VideoExplorer
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        ObservableCollection<ItemModel> listViewItems { get; set; }

        public class ItemModel
        {
            public string Title { get; set; }
            public string Category { get; set; }
            public string Details { get; set; }
            public string ImagePath { get; set; }
        }
        public MainWindow()
        {
            InitializeComponent();
            string appRoot = AppDomain.CurrentDomain.BaseDirectory;

            listViewItems = new ObservableCollection<ItemModel>();
            //// 创建一些示例数据
            //var items = new List<ItemModel>
            //{
            //    new ItemModel { Title = "Item 1", Category = "Category A", Details = "Details for Item 1", ImagePath = "assets\\youtube.png" },
            //    new ItemModel { Title = "Item 2", Category = "Category B", Details = "Details for Item 2", ImagePath = "assets/youtube.png" },
            //    new ItemModel { Title = "Item 3", Category = "Category C", Details = "Details for Item 3", ImagePath = "assets/youtube.png" }
            //};

            // 绑定数据到 ListBox
            listView.ItemsSource = listViewItems;
        }

        private void listView_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {

        }

        private void listView_Drop(object sender, DragEventArgs e)
        {
            // 检查拖拽的数据是否包含文件或目录
            if (e.Data.GetDataPresent(DataFormats.FileDrop))
            {
                // 获取拖拽的文件或目录路径
                string[] pathNames = (string[])e.Data.GetData(DataFormats.FileDrop);

                // 将文件或目录路径添加到 ListView 中
                foreach (string pathName in pathNames)
                {
                    if(Directory.Exists(pathName))
                    {
                        var files = Directory.GetFiles(pathName, "*", SearchOption.AllDirectories);
                        foreach (var file in files)
                        {
                            AddVideoFile(file);
                        }
                    }
                    else
                    {
                        AddVideoFile(pathName);
                    }
                }
            }
        }

        void AddVideoFile(string filepath)
        {
            string extension = Path.GetExtension(filepath).ToLower();
            if(extension == ".avi" || extension == ".mp4")
            {
                listViewItems.Add(new ItemModel { Title = Path.GetFileName(filepath)});
            }
        }
    }
}