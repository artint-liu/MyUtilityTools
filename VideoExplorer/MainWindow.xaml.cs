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
using System.ComponentModel;
using System.Windows.Controls.Primitives;
using System.Text.Json;
using System.Text.Json.Serialization.Metadata;
using System.Runtime.CompilerServices;
using System.Security.Cryptography;

namespace VideoExplorer
{
    /// <summary>
    /// Interaction logic for MainWindow.xaml
    /// </summary>
    public partial class MainWindow : Window
    {
        private ObservableCollection<ItemModel> listViewItems { get; set; }
        private HashSet<string> _categoryWordsList = new();
        public HashSet<string> CategoryWordsList { get => _categoryWordsList; }

        public event PropertyChangedEventHandler PropertyChanged;
        string thumbDirectory;
        readonly string videoFiles = "videoFiles.json";
        //bool bVideoFilesChanged = false;

        public class ItemModel : INotifyPropertyChanged
        {
            public string _title;
            public string _actor;
            public string _category;
            public string _details;
            public string _imagePath;
            public string Title
            {
                get { return _title;}
                set
                {
                    _title = value;
                    OnPropertyChanged();
                }
            }
            public string Category
            {
                get { return _category; }
                set
                {
                    _category = value;
                    OnPropertyChanged();
                }
            }

            public string Actor
            {
                get { return _actor; }
                set
                {
                    _actor = value;
                    OnPropertyChanged();
                }
            }

            public string Details
            {
                get { return _details; }
                set
                {
                    _details = value; OnPropertyChanged();
                }
            }

            public string ImagePath
            {
                get { return _imagePath; }
                set
                {
                    _imagePath = value;
                    OnPropertyChanged();
                }
            }

            public string fullPath { get; set; }
            public string sha1 { get; set; }

            public event PropertyChangedEventHandler PropertyChanged;
            protected virtual void OnPropertyChanged([CallerMemberName] string propertyName = null)
            {
                PropertyChanged?.Invoke(this, new PropertyChangedEventArgs(propertyName));
            }
        }

        public MainWindow()
        {
            InitializeComponent();

            thumbDirectory = Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Thumb");
            // 检查并创建“Thumb”目录
            if (!Directory.Exists(thumbDirectory))
            {
                Directory.CreateDirectory(thumbDirectory);
                Console.WriteLine("Thumb 目录已创建。");
            }

            listViewItems = new ObservableCollection<ItemModel>();
            //StatusMessage = "hello world";
            //statusBar_TextBlock.Text = "hello world";
            //statusBar_ProgressBar.Value

            // 绑定数据到 ListBox
            listView.ItemsSource = listViewItems;
        }

        private int FindFilename(string fullpath)
        {
            int count = listViewItems.Count;
            for (int i = 0; i < count; i++)
            {
                if (listViewItems[i].fullPath == fullpath)
                { return i; }
            }
            return -1;
        }

        private static bool IsVideoFile(string filePath)
        {
            string extension = Path.GetExtension(filePath).ToLower();
            return (extension == ".avi" || extension == ".mp4" || extension == ".mkv" || extension == ".wmv");
        }

        private void Window_Loaded(object sender, RoutedEventArgs e)
        {
            //var itemArray = listViewItems.ToArray();
            try
            {
                if (File.Exists(videoFiles))
                {
                    string jsonText = File.ReadAllText(videoFiles);
                    var itemsArray = JsonSerializer.Deserialize<ItemModel[]>(jsonText);
                    if (itemsArray != null)
                    {
                        foreach (var item in itemsArray)
                        {
                            listViewItems.Add(item);
                            string[] categoryList = item.Category?.Split(',');
                            if (categoryList != null && categoryList.Length > 0)
                            {
                                foreach (string category in categoryList)
                                    if(category != string.Empty)
                                        _categoryWordsList.Add(category);
                            }
                        }
                    }
                }
            }
            catch(Exception ex)
            {
            }
        }

        private void Window_Closing(object sender, CancelEventArgs e)
        {
            var itemsArray = listViewItems.ToArray();
            var option = new JsonSerializerOptions
            {
                WriteIndented = true,
            };
            string jsonText = JsonSerializer.Serialize(itemsArray, option);
            string jsonOldText = File.ReadAllText(videoFiles);
            
            if (jsonOldText != jsonText) // 减少文件写入
            {
                File.WriteAllText(videoFiles, jsonText);
            }
        }

        private void listView_SelectionChanged(object sender, SelectionChangedEventArgs e)
        {
        }

        private void listView_Drop(object sender, DragEventArgs e)
        {
            // 检查拖拽的数据是否包含文件或目录
            if (e.Data.GetDataPresent(DataFormats.FileDrop))
            {
                List<string> fileList = new List<string>();
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
                            if(IsVideoFile(file))
                                fileList.Add(file);
                        }
                    }
                    else
                    {
                        if(IsVideoFile(pathName))
                            fileList.Add(pathName);
                    }
                }

                AddVideoFileAsync(fileList);
            }
        }

        private async void AddVideoFileAsync(List<string> fileList)
        {
            int index = 0;
            foreach (var fullpath in fileList)
            {
                if (FindFilename(fullpath) >= 0) continue;
                statusBar_TextBlock.Text = $"{Path.GetFileName(fullpath)}({++index}/{fileList.Count})";
                await AddVideoFileAsync(fullpath);
            }
            statusBar_TextBlock.Text = "就绪";
            statusBar_ProgressBar.Value = 0;
        }

        private async Task AddVideoFileAsync(string fullpath)
        {
            var progress = new Progress<float>(p => { statusBar_ProgressBar.Value = p * statusBar_ProgressBar.Maximum; });
            string thumbnailPath = await Task.Run(() =>
            {
                string sha1Hash = Utils.GenerateSHA1Async(fullpath, progress).Result;
                string thumbnailPath = Path.Combine(thumbDirectory, $"{sha1Hash}.jpg");
                return thumbnailPath;
            });

            // 使用 FFmpeg 生成缩略图
            thumbnailPath = VideoUtils.GenerateThumbnail(fullpath, thumbnailPath, 10, 512);

            listViewItems.Add(new ItemModel()
            {
                Title = Path.GetFileNameWithoutExtension(fullpath),
                ImagePath = thumbnailPath,
                fullPath = fullpath
            });
            //bVideoFilesChanged = true;
        }

        private void MenuItem_Click(object sender, RoutedEventArgs e)
        {
            if(listView.SelectedIndex >= 0)
            {
                VideoInfoSettings videoInfoSettings = new();
                videoInfoSettings.Owner = this;
                videoInfoSettings.SetVideoInfo(listViewItems[listView.SelectedIndex]);
                videoInfoSettings.ShowDialog();
                string[] categorys = listViewItems[listView.SelectedIndex].Category.Split(',');
                foreach (string category in categorys)
                {
                    if(category != string.Empty)
                        _categoryWordsList.Add(category);
                }
            }
        }

        private void listView_MouseDoubleClick(object sender, MouseButtonEventArgs e)
        {
            int selectedIndex = listView.SelectedIndex;
            Utils.OpenFileWithDefaultProgram(listViewItems[selectedIndex].fullPath);
        }

        private void listView_MouseRightButtonDown(object sender, MouseButtonEventArgs e)
        {
        }

        private void MenuItem_OpenFolder(object sender, RoutedEventArgs e)
        {
            int selectedIndex = listView.SelectedIndex;
            Utils.OpenFileInExplorer(listViewItems[selectedIndex].fullPath);
        }
    }
}