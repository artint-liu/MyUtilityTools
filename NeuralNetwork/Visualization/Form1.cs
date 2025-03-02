using NumSharp;
using Tensorflow.Keras.Engine;
using Tensorflow.NumPy;
using Tensorflow;

namespace Visualization
{
    public partial class Form1 : Form
    {
        public Form1()
        {
            InitializeComponent();

            var loader = new SafetensorsLoader();
            var tensors = loader.Load("mnist_mlp_model.safetensors");

            // 获取第一层的权重（假设键名为 "layer0_dense_weights"）
            float[,] weights = (float[,])tensors["layer0_dense_weights"];

            // 打印权重形状（例如：256x784）
            toolStripStatusLabel.Text = $"权重形状: [{weights.GetLength(0)}, {weights.GetLength(1)}]";
        }
    }
}
