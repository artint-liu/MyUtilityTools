using System;
using System.Net;
using ICSharpCode.SharpZipLib.BZip2;

namespace AlignFace
{
    public partial class FormLaunch : Form
    {
        public FormLaunch()
        {
            InitializeComponent();
            if(!File.Exists("shape_predictor_68_face_landmarks.dat") && DownloadModelFile("https://dlib.net/files/shape_predictor_68_face_landmarks.dat.bz2", "shape_predictor_68_face_landmarks.dat.bz2"))
            {
                TryDecompressBZip2("shape_predictor_68_face_landmarks.dat.bz2", "shape_predictor_68_face_landmarks.dat");
            }
        }

        private void button_FacePicture_Click(object sender, EventArgs e)
        {
            Form1 form1 = new Form1();
            //Visible = false;
            form1.ShowDialog();
        }

        private void button_WebCamera_Click(object sender, EventArgs e)
        {
            Form_WebCamera form1 = new Form_WebCamera();
            //Visible = false;
            form1.ShowDialog();
        }

        private bool DownloadModelFile(string url, string filePath)
        {
            try
            {
                using (WebClient client = new WebClient())
                {
                    // 开始下载文件
                    client.DownloadFile(url, filePath);
                    Console.WriteLine("文件下载完成。");
                    return true;
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine($"下载过程中出现错误: {ex.Message}");
            }
            return false;
        }

        public static void DecompressBZip2(string inputFile, string outputFile)
        {
            using (FileStream inputStream = File.OpenRead(inputFile))
            using (FileStream outputStream = File.Create(outputFile))
            using (BZip2InputStream bz2Stream = new BZip2InputStream(inputStream))
            {
                bz2Stream.CopyTo(outputStream);
            }
        }

        public static bool TryDecompressBZip2(string inputFile, string outputFile)
        {
            try
            {
                DecompressBZip2(inputFile, outputFile);
                return true;
            }
            catch (FileNotFoundException ex)
            {
                Console.WriteLine($"文件未找到: {ex.FileName}");
            }
            catch (Exception ex)
            {
                Console.WriteLine($"解压失败: {ex.Message}");
            }
            return false;
        }
    }
}
