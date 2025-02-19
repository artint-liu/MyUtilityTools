using System.Drawing;
using DlibDotNet;
using ImageMagick;
using OpenCvSharp;
using DlibDotNet.Dnn;
using System;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text.Json;
using System.Windows.Forms;
//using Emgu.CV;
//using Emgu.CV.CvEnum;

// https://dlib.net/files/shape_predictor_68_face_landmarks.dat.bz2

namespace AlignFace
{
    public partial class Form1 : Form
    {
        class FaceInfo
        {
            public string? filename { get; set; }
            public System.Drawing.Point[]? points { get; set; }
        }

        private readonly string faceInfoFilename = "faceinfo.json";
        List<string> files = new();
        FrontalFaceDetector faceDetector = Dlib.GetFrontalFaceDetector();
        ShapePredictor? shapePredictor = null;
        List<System.Drawing.Point>? points = null;
        Dictionary<string, FaceInfo>? faceInfos = new();
        int index = 0;
        private readonly int heigh_limit = 500;

        public Form1()
        {
            InitializeComponent();
            ScanFiles();

            if(File.Exists("shape_predictor_68_face_landmarks.dat"))
                shapePredictor = ShapePredictor.Deserialize("shape_predictor_68_face_landmarks.dat");
        }

        private async void ScanFiles()
        {
            try
            {
                var imageFiles = Directory.GetFiles("images");
                int i = 0;
                foreach (var file in imageFiles)
                {
                    string extension = Path.GetExtension(file).ToLower();
                    if (extension == ".heic")
                    {
                        string pngFilepath = GetJpegFilepath(file);
                        await Task.Run(() =>
                        {
                            if (!File.Exists(pngFilepath))
                            {
                                LoadImage(file, pngFilepath);
                            }
                        });
                        toolStripStatusLabel1.Text = $"{Path.GetFileName(pngFilepath)}, {i++} / {imageFiles.Length}";
                        files.Add(pngFilepath);
                    }
                    else
                    {
                        i++;
                    }
                }

                if (files.Count > 0)
                {
                    UpdateImage(0);
                }
            }
            catch (Exception ex)
            {
                Console.WriteLine(ex.Message);
            }
        }

        private void UpdateImage(int dir)
        {
            if ((dir < 0 && index + dir >= 0) || (dir > 0 && index + dir < files.Count))
                index += dir;

            string strPngPath = GetJpegFilepath(files[index]); // 备用路径
            points = Detect(strPngPath);

            this.Invoke(new Action(() =>
            {
                pictureBox_Photo.Image = LoadImage(files[index], strPngPath);
                button_Prev.Enabled = index > 0;
                button_Next.Enabled = index < files.Count - 1;
            }));

            string filename = Path.GetFileName(strPngPath);
            if (!faceInfos.ContainsKey(filename))
            {
                faceInfos.Add(filename, new FaceInfo()
                {
                    filename = filename,
                    points = points.ToArray()
                });
            }
            toolStripStatusLabel1.Text = strPngPath;
        }

        private static string GetJpegFilepath(string path)
        {
            return Path.ChangeExtension(path, ".jpg");
        }

        private static Image LoadImage(string imagePath, string jpegPath)
        {
            if (File.Exists(jpegPath))
            {
                return Image.FromFile(jpegPath);
            }

            Image image = LoadHeicWithMagick(imagePath);
            image.Save(jpegPath, System.Drawing.Imaging.ImageFormat.Jpeg);
            return image;
        }


        private static Image LoadHeicWithMagick(string filePath)
        {
            using (MagickImage image = new MagickImage(filePath))
            {
                image.Format = MagickFormat.Png; // 转换为PNG格式
                using (MemoryStream ms = new MemoryStream())
                {
                    image.Write(ms);
                    Bitmap bitmap = new Bitmap(ms);
                    return bitmap;
                    //pictureBox_Photo.Image = bitmap;
                    //string strPngPath = Path.ChangeExtension(filePath, ".png");
                    //bitmap.Save(strPngPath);
                }
            }
        }




        private List<System.Drawing.Point> Detect(string imagepath)
        {
            // 加载图像
            Mat image = Cv2.ImRead(imagepath, ImreadModes.Color);
            Mat imageResize = new Mat();
            Cv2.Resize(image, imageResize, new OpenCvSharp.Size((image.Width * heigh_limit / image.Height), heigh_limit));
            //Cv2.CvtColor(imageResize, image, ColorConversionCodes.RGB2GRAY);
            int channels = imageResize.Channels();

            // 将OpenCV的Mat转换为Dlib的Array2D
            byte[] imageData = new byte[imageResize.Rows * imageResize.Cols * imageResize.Channels()];
            Marshal.Copy(imageResize.Data, imageData, 0, imageData.Length);

            List<System.Drawing.Point> _points = new();
            using (Array2D<RgbPixel> dlibImage = Dlib.LoadImageData<RgbPixel>(imageData, (uint)imageResize.Rows, (uint)imageResize.Cols, (uint)imageResize.Step()))
            {
                // 检测人脸
                var faces = faceDetector.Operator(dlibImage);
                // 遍历检测到的人脸
                foreach (var face in faces)
                {
                    // 检测人脸关键点
                    var shape = shapePredictor.Detect(dlibImage, face);

                    // 在图像上绘制关键点
                    for (uint i = 0; i < shape.Parts; i++)
                    {
                        var point = shape.GetPart(i);
                        _points.Add(new System.Drawing.Point(point.X, point.Y));
                    }

                    // 绘制人脸矩形框
                    //Cv2.Rectangle(imageResize, new Rect(face.Left, face.Top, (int)face.Width, (int)face.Height), new Scalar(255, 0, 0), 2);
                }
            }

            return _points;
        }

        private void pictureBox_Photo_Paint(object sender, PaintEventArgs e)
        {
            Graphics g = e.Graphics;
            if (points != null)
            {
                Pen pen = new Pen(Color.Red);
                Font font = new Font("Arial", 10);
                Brush solidBrush = new SolidBrush(Color.FromArgb(255, 0, 255, 0));
                int radius = 5;
                float scale = (float)pictureBox_Photo.Height / heigh_limit;
                float display_width = (float)pictureBox_Photo.Image.Width / pictureBox_Photo.Image.Height * pictureBox_Photo.Height;
                float offsetx = (pictureBox_Photo.Width - display_width) / 2;
                int i = 0;
                bool showIndex = checkBox_ShowIndex.Checked;
                foreach (var point in points)
                {
                    float x = point.X * scale + offsetx;
                    float y = point.Y * scale;
                    g.DrawEllipse(pen, x - radius, y - radius, radius * 2, radius * 2);
                    if (showIndex)
                        g.DrawString($"{i++}", font, solidBrush, new PointF(x, y));
                }
            }
        }

        private void Form1_FormClosing(object sender, FormClosingEventArgs e)
        {
            JsonSerializerOptions options = new JsonSerializerOptions()
            {
                WriteIndented = true
            };

            string jsonText = JsonSerializer.Serialize(faceInfos, options);
            string jsonTextOld = File.Exists(faceInfoFilename) ? File.ReadAllText(faceInfoFilename) : "";
            if (jsonText != jsonTextOld)
            {
                File.WriteAllText(faceInfoFilename, jsonText);
            }

            faceDetector.Dispose();
            shapePredictor?.Dispose();
        }

        private void button_Prev_Click(object sender, EventArgs e)
        {
            UpdateImage(-1);
        }

        private void button_Next_Click(object sender, EventArgs e)
        {
            UpdateImage(1);
        }

        private void checkBox_ShowIndex_CheckedChanged(object sender, EventArgs e)
        {
            pictureBox_Photo.Invalidate();
        }

        private void Form1_Load(object sender, EventArgs e)
        {
            if (File.Exists(faceInfoFilename))
            {
                string jsonText = File.ReadAllText(faceInfoFilename);
                if (jsonText != string.Empty)
                {
                    faceInfos = JsonSerializer.Deserialize<Dictionary<string, FaceInfo>>(jsonText);
                }
            }
        }

        private async void checkBox_Auto_CheckedChanged(object sender, EventArgs e)
        {
            while (checkBox_Auto.Checked && index < files.Count - 1)
            {
                await Task.Run(() =>
                {
                    UpdateImage(1);
                });
            }
            checkBox_Auto.Checked = false;
        }
    }
}
