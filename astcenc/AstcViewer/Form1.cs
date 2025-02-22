using CDNTextureMgr;
using System.Text.Json;
using System.Text.Json.Nodes;
using System.Windows.Forms;

namespace AstcViewer
{
    public partial class Form1 : Form
    {
        class SavedData
        {
            private string _filepath;
            public string filepath {
                get => _filepath;
                set
                {
                    _filepath = value;
                    if (File.Exists(_filepath))
                        form1.pictureBox_Origin.Image = Image.FromFile(_filepath);
                }
            }
        }

        private SavedData settings = new SavedData();
        private readonly string savedDataPath = "settings.json";
        private static Form1 form1;

        public Form1()
        {
            form1 = this;
            InitializeComponent();
            AllowDrop = true;
        }

        private void UpdateAstc()
        {
            Image image = ASTCProcessor.ProcessImage(settings.filepath, "4x4", out int fileLength);
            pictureBox_Astc.Image = image;
        }

        private void Form1_Load(object sender, EventArgs e)
        {
            if (File.Exists(savedDataPath))
            {
                string text = File.ReadAllText(savedDataPath);
                settings = JsonSerializer.Deserialize<SavedData>(text);
                if (File.Exists(settings.filepath))
                {
                    //pictureBox_Origin.Image = Image.FromFile(settings.filepath);
                    UpdateAstc();
                }
            }
        }
        private void Form1_FormClosing(object sender, FormClosingEventArgs e)
        {
            string text = JsonSerializer.Serialize(settings);
            string oldText = string.Empty;
            if (File.Exists(savedDataPath))
                oldText = File.ReadAllText(savedDataPath);

            if (text != oldText)
                File.WriteAllText(savedDataPath, text);
        }

        private void Form1_DragEnter(object sender, DragEventArgs e)
        {
            if (e.Data.GetDataPresent(DataFormats.FileDrop))
            {
                e.Effect = DragDropEffects.Copy; // 显示复制光标
            }
            else
            {
                e.Effect = DragDropEffects.None; // 拒绝非文件拖拽
            }
        }
        private void Form1_DragDrop(object sender, DragEventArgs e)
        {
            string[] files = (string[])e.Data.GetData(DataFormats.FileDrop);
            if (files.Length > 0)
            {
                settings.filepath = files[0];
                UpdateAstc();
            }
        }
    }
}
