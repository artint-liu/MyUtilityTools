using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using AForge.Video.DirectShow;
using AForge.Video;
using System.Windows.Forms;

namespace AlignFace
{
    public partial class Form_WebCamera : Form
    {
        private VideoCaptureDevice videoSource = null;
        private FilterInfoCollection videoDevices;
        private bool _isClosing = false;  // 新增关闭标志
        public Form_WebCamera()
        {
            InitializeComponent();
            InitializeCamera();
            //pictureBox_Video.Image = Image.FromFile("");
        }

        private void InitializeCamera()
        {
            // 获取所有视频输入设备
            videoDevices = new FilterInfoCollection(FilterCategory.VideoInputDevice);

            if (videoDevices.Count == 0)
            {
                MessageBox.Show("未找到摄像头设备");
                return;
            }

            // 使用第一个摄像头（可根据需要修改）
            var deviceMoniker = videoDevices[0].MonikerString;
            videoSource = new VideoCaptureDevice(deviceMoniker);

            // 设置分辨率（根据设备支持情况调整）
            videoSource.VideoResolution = videoSource.VideoCapabilities[0];
        }

        private void video_NewFrame(object sender, NewFrameEventArgs eventArgs)
        {
            if (_isClosing || pictureBox_Video.IsDisposed)
                return;

            // TODO: 解决内存增长问题
            Image image = pictureBox_Video.Image;
            pictureBox_Video.Image = (Bitmap)eventArgs.Frame.Clone();
            image?.Dispose();
        }

        private void button_Start_Click(object sender, EventArgs e)
        {
            if (videoSource != null)
            {
                videoSource.NewFrame += video_NewFrame;
                videoSource.Start();
                button_Start.Enabled = false;
                button_Stop.Enabled = true;
            }
        }

        private void button_Stop_Click(object sender, EventArgs e)
        {
            if (videoSource != null && videoSource.IsRunning)
            {
                videoSource.SignalToStop();
                videoSource.WaitForStop();
                videoSource.NewFrame -= video_NewFrame;
                //videoSource.Stop();
                button_Start.Enabled = true;
                button_Stop.Enabled = false;
            }
        }

        private void Form_WebCamera_FormClosing(object sender, FormClosingEventArgs e)
        {
            _isClosing = true;
            //base.OnFormClosing(e);
            //videoSource.NewFrame -= video_NewFrame;
            button_Stop_Click(null, null);
            videoSource = null;
            //videoSource?.Dispose();
            pictureBox_Video.Image?.Dispose();
        }

        private void Form_WebCamera_FormClosed(object sender, FormClosedEventArgs e)
        {
        }
    }
}
