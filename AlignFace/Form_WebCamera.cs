using AForge.Video.DirectShow;
using AForge.Video;
using System.Windows.Forms;
using DlibDotNet;
using OpenCvSharp;
using System.Runtime.InteropServices;
using DlibDotNet.Extensions;
using System;
using System.Drawing;
using AForge.Controls;
using System.Diagnostics;
using AForge;
//using Emgu.CV;
//using Emgu.CV.CvEnum;
//using Emgu.CV.Structure;

namespace AlignFace
{
    public partial class Form_WebCamera : Form
    {
        //private VideoCaptureDevice videoSource = null;
        private FilterInfoCollection videoDevices;
        FrontalFaceDetector faceDetector = Dlib.GetFrontalFaceDetector();
        ShapePredictor? shapePredictor = null;
        const int heigh_limit = 500;
        //private bool _isClosing = false;  // 新增关闭标志

        public Form_WebCamera()
        {
            InitializeComponent();
            InitializeCamera();

            if (File.Exists("shape_predictor_68_face_landmarks.dat"))
                shapePredictor = ShapePredictor.Deserialize("shape_predictor_68_face_landmarks.dat");

            //pictureBox_Video.Image = Image.FromFile("");
        }

        private void InitializeCamera()
        {
            //// 获取所有视频输入设备
            //videoDevices = new FilterInfoCollection(FilterCategory.VideoInputDevice);

            //if (videoDevices.Count == 0)
            //{
            //    MessageBox.Show("未找到摄像头设备");
            //    return;
            //}

            //// 使用第一个摄像头（可根据需要修改）
            //var deviceMoniker = videoDevices[0].MonikerString;
            //videoSource = new VideoCaptureDevice(deviceMoniker);
            //pictureBox_Video.VideoSource = new AsyncVideoSource(videoSource);

            //// 设置分辨率（根据设备支持情况调整）
            //videoSource.VideoResolution = videoSource.VideoCapabilities[0];
        }

        private void OpenVideoSource(IVideoSource source)
        {
            // set busy cursor
            this.Cursor = Cursors.WaitCursor;

            // stop current video source
            CloseCurrentVideoSource();

            // start new video source
            videoSourcePlayer.VideoSource = new AsyncVideoSource(source);
            videoSourcePlayer.NewFrame += video_NewFrame;
            videoSourcePlayer.Start();

            // reset stop watch
            //stopWatch = null;

            // start timer
            //timer.Start();

            this.Cursor = Cursors.Default;
        }


        private void CloseCurrentVideoSource()
        {
            if (videoSourcePlayer.VideoSource != null)
            {
                videoSourcePlayer.NewFrame -= video_NewFrame;
                videoSourcePlayer.SignalToStop();

                // wait ~ 3 seconds
                for (int i = 0; i < 30; i++)
                {
                    if (!videoSourcePlayer.IsRunning)
                        break;
                    System.Threading.Thread.Sleep(100);
                }

                if (videoSourcePlayer.IsRunning)
                {
                    videoSourcePlayer.Stop();
                }

                videoSourcePlayer.VideoSource = null;
            }
        }


        private List<System.Drawing.Point> Detect(Image image)
        {
            // 加载图像
            using (Bitmap bitmap = new Bitmap(image))
            {
                var bitmapData = bitmap.LockBits(new System.Drawing.Rectangle(0, 0, bitmap.Width, bitmap.Height), System.Drawing.Imaging.ImageLockMode.ReadOnly, System.Drawing.Imaging.PixelFormat.Format24bppRgb);
                if (bitmapData == null)
                {
                    bitmap.Dispose();
                    return null;
                }


                Mat matImage = Mat.FromPixelData(bitmap.Height, bitmap.Width, MatType.CV_8UC3, bitmapData.Scan0, bitmapData.Stride);
                Mat imageResize = new Mat();
                Cv2.Resize(matImage, imageResize, new OpenCvSharp.Size((matImage.Width * heigh_limit / matImage.Height), heigh_limit));
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
                    }
                }
                return _points;
            }
        }

        private void video_NewFrame(object sender, ref Bitmap image)
        {
            var points = Detect(image);
            //if (_isClosing || pictureBox_Video.IsDisposed)
            //    return;

            //DateTime now = DateTime.Now;
            //using (Graphics g = Graphics.FromImage(eventArgs.Frame))
            //{
            //    //绘制当前日期
            //    SolidBrush brush = new SolidBrush(Color.Red);
            //    g.DrawString(now.ToString(), this.Font, brush, new PointF(5, 5));
            //    brush.Dispose();
            //}

            if (points != null && points.Count > 0)
            {
                //videoSourcePlayer.Invoke((MethodInvoker)delegate
                //{
                using (Graphics g = Graphics.FromImage(image))
                {
                    Pen pen = new Pen(Color.Red);
                    int radius = 5;
                    float scale = (float)image.Height / heigh_limit;

                    foreach (var p in points)
                    {
                        float x = p.X * scale;
                        float y = p.Y * scale;

                        g.DrawEllipse(pen, x - radius, y - radius, radius * 2, radius * 2);
                    }
                    //g.DrawImage(eventArgs.Frame, PointF.Empty);

                }
                //});
            }


            //Bitmap bitmap = (Bitmap)eventArgs.Frame.Clone();

            //// TODO: 解决内存增长问题
            //this.Invoke(new Action(() =>
            //{
            //    pictureBox_Video.Image?.Dispose();
            //    pictureBox_Video.Image = bitmap;
            //}));
        }

        private void button_Start_Click(object sender, EventArgs e)
        {
            VideoCaptureDeviceForm form = new VideoCaptureDeviceForm();

            if (form.ShowDialog(this) == DialogResult.OK)
            {
                // create video source
                VideoCaptureDevice videoSource = form.VideoDevice;

                // open it
                OpenVideoSource(videoSource);
                button_Start.Enabled = false;
                button_Stop.Enabled = true;
            }

            //if (videoSource != null)
            //{
            //    //videoSource.NewFrame += video_NewFrame;
            //    //videoSource.Vode
            //    //videoSource.Start();
            //    pictureBox_Video.VideoSource.Start();
            //    button_Start.Enabled = false;
            //    button_Stop.Enabled = true;
            //}
        }

        private void button_Stop_Click(object sender, EventArgs e)
        {
            CloseCurrentVideoSource();
            button_Start.Enabled = true;
            button_Stop.Enabled = false;
            
            //if (videoSource != null && videoSource.IsRunning)
            //{
            //    //videoSource.NewFrame -= video_NewFrame;
            //    //videoSource.SignalToStop();
            //    //videoSource.WaitForStop();
            //    //videoSource.Stop();
            //    pictureBox_Video.VideoSource.SignalToStop();
            //    pictureBox_Video.VideoSource.WaitForStop();

            //}
        }

        private void Form_WebCamera_FormClosing(object sender, FormClosingEventArgs e)
        {
            //_isClosing = true;
            //button_Stop_Click(null, null);
            //videoSource = null;
            //pictureBox_Video.Image?.Dispose();
            CloseCurrentVideoSource();
        }

        private void Form_WebCamera_FormClosed(object sender, FormClosedEventArgs e)
        {
        }
    }
}
