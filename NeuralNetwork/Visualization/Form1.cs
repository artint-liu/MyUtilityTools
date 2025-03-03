using NumSharp;
using Tensorflow.Keras.Engine;
using Tensorflow.NumPy;
using Tensorflow;
using System.Collections.Generic;
using MathNet.Numerics.LinearAlgebra;
using MathNet.Numerics.LinearAlgebra.Single;
using System.Drawing.Drawing2D;
using System.Drawing;

namespace Visualization
{
    public partial class Form1 : Form
    {
        private Bitmap drawingBitmap;
        private Bitmap outputBitmap;
        private Graphics graphics;
        private bool isDrawing = false;
        private Point lastPoint;

        private Dictionary<string, Array> tensors;
        public Form1()
        {
            InitializeComponent();

            var loader = new SafetensorsLoader();
            tensors = loader.Load("mnist_mlp_model.safetensors");

            // 获取第一层的权重（假设键名为 "layer0_dense_weights"）
            float[,] weights = (float[,])tensors["layer0_dense_weights"];

            // 打印权重形状（例如：256x784）
            toolStripStatusLabel.Text = $"权重形状: [{weights.GetLength(0)}, {weights.GetLength(1)}]";
            InitializeDrawingSurface();
        }


        private Vector<float>[] ConvertToMatrix(float[] data, int rows, int cols)
        {
            Vector<float>[] matrix = new Vector<float>[cols];
            for (int i = 0; i < cols; i++)
            {
                float[] column = new float[rows];
                Array.Copy(data, i * rows, column, 0, rows);
                matrix[i] = DenseVector.OfArray(column);
            }
            return matrix;
        }

        float[] MultiplyArray(float[] inputs, float[,] matrix, float[] biases)
        {
            int count = matrix.GetLength(1);
            float[] result = new float[count];
            for(int i = 0; i < count; i++)
            {
                result[i] = biases[i];
                for(int j = 0; j < inputs.Length; j++)
                {
                    result[i] += inputs[j] * matrix[j, i];
                }
            }
            return result;
        }

        // 前向传播推理（假设为 784 → 256 → 10 的全连接网络）
        public float[] ForwardPass(Dictionary<string, Array> tensors, float[] input)
        {
            // 输入层 → 隐藏层
            var inputVector = DenseVector.OfArray(input);
            var fc1Weights = DenseMatrix.OfColumnArrays(Convert2DArrayToJaggedArray(tensors["layer0_dense_weights"] as float[,]));
            var fc1Bias = DenseVector.OfArray(tensors["layer0_dense_biases"] as float[]);
            var hidden = fc1Weights.Multiply(inputVector) + fc1Bias;
            hidden.MapInplace(x => x > 0 ? x : 0); // ReLU 激活

            //var hidden1 = MultiplyArray(input, tensors["layer0_dense_weights"] as float[,], tensors["layer0_dense_biases"] as float[]);

            // 隐藏层 → 输出层
            var fc2Weights = DenseMatrix.OfColumnArrays(Convert2DArrayToJaggedArray(tensors["layer1_dense_1_weights"] as float[,]));
            var fc2Bias = DenseVector.OfArray(tensors["layer1_dense_1_biases"] as float[]);
            var output = fc2Weights.Multiply(hidden) + fc2Bias;

            // SoftMax激活
            float maxValue = output.Max();
            float sum = output.Sum(x => (float)Math.Exp(x - maxValue));
            output.MapInplace(x => (float)(Math.Exp(x - maxValue) / sum));

            //output.MapInplace(x => x / sum);
            return output.ToArray();
        }

        public static float[][] Convert2DArrayToJaggedArray(float[,] rectArray)
        {
            // 参数校验
            if (rectArray == null)
                throw new ArgumentNullException(nameof(rectArray));

            // 获取数组维度
            int rows = rectArray.GetLength(0);
            int cols = rectArray.GetLength(1);

            // 创建交错数组
            float[][] jaggedArray = new float[rows][];

            // 逐行复制数据
            for (int i = 0; i < rows; i++)
            {
                jaggedArray[i] = new float[cols];
                for (int j = 0; j < cols; j++)
                {
                    jaggedArray[i][j] = rectArray[i, j];
                }
            }

            return jaggedArray;
        }

        // 获取概率最大的类别索引
        public int ArgMax(float[] output)
        {
            int maxIndex = 0;
            float maxValue = output[0];
            for (int i = 1; i < output.Length; i++)
            {
                if (output[i] > maxValue)
                {
                    maxValue = output[i];
                    maxIndex = i;
                }
            }
            return maxIndex;
        }

        byte[,] LoadImage()
        {
            byte[,] pixels = new byte[28, 28];
            for (int y = 0; y < 28; y++)
                for (int x = 0; x < 28; x++)
                    pixels[y, x] = outputBitmap.GetPixel(x, y).R; // 假设为灰度图
            return pixels;
        }
        public float[] NormalizeAndFlatten(byte[,] image)
        {
            float[] flattened = new float[28 * 28];
            for (int y = 0; y < 28; y++)
            {
                for (int x = 0; x < 28; x++)
                {
                    // 归一化到 [0, 1]
                    flattened[y * 28 + x] = image[y, x] / 255.0f;
                }
            }
            return flattened;
        }
        private void InitializeDrawingSurface()
        {
            // 初始化绘图画布 (280x280)
            drawingBitmap = new Bitmap(280, 280);
            graphics = Graphics.FromImage(drawingBitmap);
            graphics.Clear(Color.White);
            graphics.SmoothingMode = SmoothingMode.AntiAlias;

            // 配置PictureBox
            pictureBox1.Size = new Size(280, 280);
            pictureBox1.Image = drawingBitmap;

            // 初始化输出位图 (28x28)
            outputBitmap = new Bitmap(28, 28);
        }

        #region 鼠标事件处理
        private void pictureBox1_MouseDown(object sender, MouseEventArgs e)
        {
            isDrawing = true;
            lastPoint = e.Location;
            DrawPoint(e.Location);
        }

        private void pictureBox1_MouseMove(object sender, MouseEventArgs e)
        {
            if (isDrawing)
            {
                DrawLine(e.Location);
                lastPoint = e.Location;
            }
        }

        private void pictureBox1_MouseUp(object sender, MouseEventArgs e)
        {
            isDrawing = false;
            UpdateOutputBitmap();
        }
        #endregion
        #region 绘图逻辑
        private void DrawPoint(Point point)
        {
            using (var pen = new Pen(Color.Black, 16))
            {
                graphics.DrawEllipse(pen, point.X - 8, point.Y - 8, 16, 16);
            }
            pictureBox1.Invalidate();
        }

        private void DrawLine(Point endPoint)
        {
            using (var pen = new Pen(Color.Black, 16))
            {
                graphics.DrawLine(pen, lastPoint, endPoint);
            }
            pictureBox1.Invalidate(new Rectangle(
                Math.Min(lastPoint.X, endPoint.X) - 10,
                Math.Min(lastPoint.Y, endPoint.Y) - 10,
                Math.Abs(endPoint.X - lastPoint.X) + 20,
                Math.Abs(endPoint.Y - lastPoint.Y) + 20));
        }
        #endregion

        #region 输出处理
        private void UpdateOutputBitmap()
        {
            for (int y = 0; y < 28; y++)
            {
                for (int x = 0; x < 28; x++)
                {
                    // 采样中心点像素
                    int srcX = x * 10 + 5;
                    int srcY = y * 10 + 5;
                    Color color = drawingBitmap.GetPixel(srcX, srcY);

                    // 转换为灰度并反转颜色（MNIST风格：黑底白字）
                    int grayValue = 255 - (int)(color.R * 0.3 + color.G * 0.59 + color.B * 0.11);
                    //int grayValue = (int)(color.R * 0.3 + color.G * 0.59 + color.B * 0.11);
                    outputBitmap.SetPixel(x, y, Color.FromArgb(grayValue, grayValue, grayValue));
                }
            }
        }
        #endregion

        private void pictureBox1_Paint(object sender, PaintEventArgs e)
        {
            // 绘制10x10像素网格
            using (var gridPen = new Pen(Color.LightGray) { DashStyle = DashStyle.Dash })
            {
                for (int i = 0; i < 28; i++)
                {
                    int pos = i * 10;
                    e.Graphics.DrawLine(gridPen, pos, 0, pos, 279); // 垂直线
                    e.Graphics.DrawLine(gridPen, 0, pos, 279, pos); // 水平线
                }
            }
        }

        private void button_Clear_Click(object sender, EventArgs e)
        {
            graphics.Clear(Color.White);
            pictureBox1.Invalidate();
        }

        private void button_Do_Click(object sender, EventArgs e)
        {
            outputBitmap.Save("test.bmp");
            byte[,] image = LoadImage();
            float[] input = NormalizeAndFlatten(image);

            // 执行推理
            float[] output = ForwardPass(tensors, input);
            int predictedDigit = ArgMax(output);
            toolStripStatusLabel.Text = $"result:{predictedDigit}";
        }
    }
}
