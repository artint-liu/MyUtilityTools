#include <iostream>
//#include <filesystem>
#include <fstream>
#include <sstream>
#include <vector>
#include <cmath>
#include <random>
#include <algorithm>
#include <filesystem>
#include <execution>  // 包含并行执行策略

//#define PARALLE
#if 0
#define ACTIVATION sigmoid
#define ACTIVATION_DERIVATIVE sigmoidDerivative
#else
#define ACTIVATION relu
#define ACTIVATION_DERIVATIVE reluDerivative
#endif

class Timer {
    std::string str;
    clock_t     start;

public:
    Timer(const std::string& str) : str(str) {
        start = clock();
    }

    ~Timer() {
        clock_t end = clock();
        std::cout << str << " => " << (end - start) / 1000.0 << '\n';
    }
};

typedef float real;
namespace fs = std::filesystem;

//// 激活函数 softmax
//real softmax(real x) {
//    return static_cast <real>(1.0) / (static_cast<real>(1.0) + std::exp(-x));
//}
//
//// softmax 函数的导数
//real softmaxDerivative(real x) {
//    real sig = softmax(x);
//    return sig * (1 - sig);
//}

// 激活函数 sigmoid
real sigmoid(real x) {
    return static_cast <real>(1.0) / (static_cast<real>(1.0) + std::exp(-x));
}

// sigmoid 函数的导数
real sigmoidDerivative(real x) {
    real sig = sigmoid(x);
    return sig * (1 - sig);
}

// ReLU 激活函数
real relu(real x) {
    return std::max(static_cast<real>(0.0), x);
}

// ReLU 函数的导数
real reluDerivative(real x) {
    return x > 0 ? static_cast<real>(1.0) : static_cast<real>(0.0);
}


// Softmax激活函数
void softmax(std::vector<real>& x) {
    real maxVal = *std::max_element(x.begin(), x.end());
    real sum = 0.0;
    for (auto& val : x) {
        val = std::exp(val - maxVal);
        sum += val;
    }
    for (auto& val : x) {
        val /= sum;
    }
}

// 定义神经网络层
struct Layer {
    std::vector<std::vector<real>> weights;  // 权重矩阵
    std::vector<real> biases;                // 偏置向量
    std::vector<real> outputs;               // 输出向量
    std::vector<real> inputs;                // 输入向量
    std::vector<real> errors;                // 误差向量
};

int testNetwork(std::vector<Layer> network, std::vector<std::pair<std::vector<real>, int>> testData);

// 初始化神经网络
// 改进的权重初始化（He初始化）
std::vector<Layer> initializeNetwork(size_t inputSize, const std::vector<int>& hiddenSizes, int outputSize) {
    std::vector<Layer> network;
    std::random_device rd;
    std::mt19937 gen(rd());

    // 输入层到第一个隐藏层
    Layer firstHidden;
    size_t fan_in = inputSize;
    std::normal_distribution<real> dis(0.0, static_cast<real>(std::sqrt(2.0 / fan_in)));
    firstHidden.weights.resize(hiddenSizes[0], std::vector<real>(inputSize));
    firstHidden.biases.resize(hiddenSizes[0], static_cast <real>(0.1));
    firstHidden.outputs.resize(hiddenSizes[0]);
    firstHidden.inputs.resize(inputSize);
    firstHidden.errors.resize(hiddenSizes[0]);
    for (auto& row : firstHidden.weights) {
        for (auto& val : row) {
            val = dis(gen);
        }
    }
    network.push_back(firstHidden);

    // 隐藏层之间
    for (size_t i = 1; i < hiddenSizes.size(); ++i) {
        Layer hidden;
        fan_in = hiddenSizes[i - 1];
        std::normal_distribution<real> dis(0.0, static_cast<real>(std::sqrt(2.0 / fan_in)));
        hidden.weights.resize(hiddenSizes[i], std::vector<real>(hiddenSizes[i - 1]));
        hidden.biases.resize(hiddenSizes[i], static_cast<real>(0.1));
        hidden.outputs.resize(hiddenSizes[i]);
        hidden.inputs.resize(hiddenSizes[i - 1]);
        hidden.errors.resize(hiddenSizes[i]);
        for (auto& row : hidden.weights) {
            for (auto& val : row) {
                val = dis(gen);
            }
        }
        network.push_back(hidden);
    }

    // 输出层（使用softmax）
    Layer output;
    fan_in = hiddenSizes.back();
    std::normal_distribution<real> dis_out(0.0, static_cast<real>(std::sqrt(2.0 / fan_in)));
    output.weights.resize(outputSize, std::vector<real>(hiddenSizes.back()));
    output.biases.resize(outputSize, static_cast<real>(0.1));
    output.outputs.resize(outputSize);
    output.inputs.resize(hiddenSizes.back());
    output.errors.resize(outputSize);
    for (auto& row : output.weights) {
        for (auto& val : row) {
            val = dis_out(gen);
        }
    }
    network.push_back(output);

    return network;
}

// 前向传播
void forwardPropagation(std::vector<Layer>& network, const std::vector<real>& input) {
    // 输入层到第一个隐藏层
    network[0].inputs = input;
#ifdef PARALLE
    std::transform(std::execution::par_unseq, network[0].biases.begin(), network[0].biases.end(), network[0].weights.begin(), network[0].outputs.begin(),
        [&input](real sum, std::vector<real>& b)
        {
            for (size_t j = 0; j < input.size(); ++j)
            {
                sum += b[j] * input[j];
            }
            return ACTIVATION(sum);
        });
#else
    for (size_t i = 0; i < network[0].outputs.size(); ++i) {
        real sum = network[0].biases[i];
        for (size_t j = 0; j < input.size(); ++j) {
            sum += network[0].weights[i][j] * input[j];
        }
        network[0].outputs[i] = ACTIVATION(sum);
    }
#endif

    // 隐藏层之间
    for (size_t i = 1; i < network.size() - 1; ++i)
    {
        network[i].inputs = network[i - 1].outputs;
#ifdef PARALLE
        std::vector<real>& inputs = network[i].inputs;

        std::transform(std::execution::par_unseq, network[i].biases.begin(), network[i].biases.end(), network[i].weights.begin(), network[i].outputs.begin(),
            [i, &inputs](real sum, std::vector<real>& b)
            {
                for (size_t k = 0; k < inputs.size(); ++k) {
                    sum += b[k] * inputs[k];
                }
                return ACTIVATION(sum);
            });
#else
        for (size_t j = 0; j < network[i].outputs.size(); ++j) {
            real sum = network[i].biases[j];
            for (size_t k = 0; k < network[i].inputs.size(); ++k) {
                sum += network[i].weights[j][k] * network[i].inputs[k];
            }
            network[i].outputs[j] = ACTIVATION(sum);
        }
#endif
    }

    // 输出层使用softmax
    Layer& outputLayer = network.back();
    outputLayer.inputs = network[network.size() - 2].outputs;
#ifdef PARALLE
    std::vector<real>& outputLayer_inputs = outputLayer.inputs;
    std::transform(std::execution::par_unseq, outputLayer.biases.begin(), outputLayer.biases.end(), outputLayer.weights.begin(), outputLayer.outputs.begin(),
        [&outputLayer_inputs](real sum, std::vector<real>& b)
        {
            for (size_t k = 0; k < outputLayer_inputs.size(); ++k) {
                sum += b[k] * outputLayer_inputs[k];
            }
            return sum; // 先存储线性输出
        });
#else
    for (size_t j = 0; j < outputLayer.outputs.size(); ++j) {
        real sum = outputLayer.biases[j];
        for (size_t k = 0; k < outputLayer.inputs.size(); ++k) {
            sum += outputLayer.weights[j][k] * outputLayer.inputs[k];
        }
        outputLayer.outputs[j] = sum; // 先存储线性输出
    }
#endif
    softmax(outputLayer.outputs); // 应用softmax
}

// 反向传播
void backPropagation(std::vector<Layer>& network, const std::vector<real>& target, real learningRate, real lambda)
{
    // 初始化动量存储（改为延迟初始化）
    static std::vector<std::vector<std::vector<real>>> prevWeightUpdates;
    static std::vector<std::vector<real>> prevBiasUpdates;
    const real momentum = static_cast<real>(0.9);

    // 第一次运行时初始化动量存储
    if (prevWeightUpdates.empty()) {
        prevWeightUpdates.resize(network.size());
        for (size_t i = 0; i < network.size(); ++i) {
            prevWeightUpdates[i].resize(network[i].weights.size());
            for (size_t j = 0; j < network[i].weights.size(); ++j) {
                prevWeightUpdates[i][j].resize(network[i].weights[j].size(), 0.0f);
            }
        }
    }
    if (prevBiasUpdates.empty()) {
        prevBiasUpdates.resize(network.size());
        for (size_t i = 0; i < network.size(); ++i) {
            prevBiasUpdates[i].resize(network[i].biases.size(), 0.0f);
        }
    }

    // 输出层误差计算（保持不变）
    Layer& outputLayer = network.back();
    for (size_t i = 0; i < outputLayer.outputs.size(); ++i) {
        outputLayer.errors[i] = (outputLayer.outputs[i] - target[i]);
    }

    // 隐藏层误差传播（保持不变）
    for (int i = static_cast<int>(network.size()) - 2; i >= 0; --i) {
        for (size_t j = 0; j < network[i].outputs.size(); ++j) {
            real error = 0.0;
            for (size_t k = 0; k < network[i + 1].errors.size(); ++k) {
                error += network[i + 1].errors[k] * network[i + 1].weights[k][j];
            }
            network[i].errors[j] = error * ACTIVATION_DERIVATIVE(network[i].outputs[j]);
        }
    }

    // 更新权重和偏置（添加L2正则化）
    for (size_t i = 0; i < network.size(); ++i) {
        for (size_t j = 0; j < network[i].weights.size(); ++j) {
            for (size_t k = 0; k < network[i].weights[j].size(); ++k) {
                real gradient = network[i].errors[j] * network[i].inputs[k];
                real regularization = lambda * network[i].weights[j][k];
                real delta = learningRate * (gradient + regularization);

                network[i].weights[j][k] -= delta + momentum * prevWeightUpdates[i][j][k];
                prevWeightUpdates[i][j][k] = delta;  // 存储当前更新量供下次使用
            }
            // 更新偏置
            real biasDelta = learningRate * network[i].errors[j];
            network[i].biases[j] -= biasDelta + momentum * prevBiasUpdates[i][j];
            prevBiasUpdates[i][j] = biasDelta;  // 存储当前更新量
        }
    }
}


// 训练神经网络
void trainNetwork(std::vector<Layer>& network, const std::vector<std::pair<std::vector<real>, int>>& trainingData,
    std::vector<std::pair<std::vector<real>, int>> testData, int epochs, real learningRate, real lambda, int batchSize = 256)
{
    real bestAccuracy = 0.0;
    for (int epoch = 0; epoch < epochs; ++epoch) {
        Timer timer("训练耗时");
        std::cout << "Epoch " << (epoch + 1) << "/" << epochs << std::endl;
        size_t i = 0;
        // 打乱训练数据
        auto shuffledData = trainingData;
        std::shuffle(shuffledData.begin(), shuffledData.end(), std::mt19937{ std::random_device{}() });

        // 批量训练
        for (size_t start = 0; start < shuffledData.size(); start += batchSize) {
            auto end = std::min(start + batchSize, shuffledData.size());
            //std::vector<std::vector<real>> batchInputs;
            //std::vector<std::vector<real>> batchTargets;

            // 累积梯度
            for (size_t i = start; i < end; ++i) {
                const auto& data = shuffledData[i];
                std::vector<real> target(10, 0.0);
                target[data.second] = 1.0;

                forwardPropagation(network, data.first);
                backPropagation(network, target, learningRate / (end - start), lambda);
                std::cout << "进度:" << i++ << "/" << trainingData.size() << "\r";
            }
        }

        // 动态调整学习率
        real currentAccuracy = static_cast<real>(testNetwork(network, testData));
        if (currentAccuracy > bestAccuracy) {
            bestAccuracy = currentAccuracy;
            //learningRate *= static_cast<real>(1.05);
        }
        //else {
        //    learningRate *= static_cast<real>(0.5);
        //}
        //learningRate = std::max(learningRate, 1e-5f);
    }
}

// 预测
int predict(const std::vector<Layer>& network, const std::vector<real>& input) {
    std::vector<real> outputs;
    // 复制网络进行前向传播
    std::vector<Layer> tempNetwork = network;
    forwardPropagation(tempNetwork, input);
    outputs = tempNetwork.back().outputs;

    // 找出最大输出对应的索引
    int predictedLabel = 0;
    real maxOutput = outputs[0];
    for (size_t i = 1; i < outputs.size(); ++i) {
        if (outputs[i] > maxOutput) {
            maxOutput = outputs[i];
            predictedLabel = static_cast<int>(i + 0.5);
        }
    }

    return predictedLabel;
}

std::vector<std::pair<std::vector<real>, int>> loadData(const std::string& filename)
{
    std::vector<std::pair<std::vector<real>, int>> data;
    fs::path txtPath(filename);
    fs::path binPath = txtPath;
    binPath.replace_extension(".bin");

    bool loadFromBin = false;
    if (fs::exists(binPath) && fs::exists(txtPath)) {
        auto binTime = fs::last_write_time(binPath);
        auto txtTime = fs::last_write_time(txtPath);
        loadFromBin = binTime > txtTime;
    }

    if (loadFromBin) {
        // 从二进制文件加载数据
        std::ifstream binFile(binPath, std::ios::binary);
        if (!binFile) {
            std::cerr << "Failed to open binary file: " << binPath << std::endl;
            return data;
        }

        size_t numSamples;
        binFile.read(reinterpret_cast<char*>(&numSamples), sizeof(numSamples));

        for (size_t i = 0; i < numSamples; ++i) {
            int label;
            binFile.read(reinterpret_cast<char*>(&label), sizeof(label));

            size_t numFeatures;
            binFile.read(reinterpret_cast<char*>(&numFeatures), sizeof(numFeatures));

            std::vector<real> features(numFeatures);
            binFile.read(reinterpret_cast<char*>(features.data()), numFeatures * sizeof(real));

            data.emplace_back(features, label);
        }
        binFile.close();
    }
    else {
        // 从文本文件加载数据
        std::ifstream txtFile(txtPath);
        if (!txtFile) {
            std::cerr << "Failed to open text file: " << txtPath << std::endl;
            return data;
        }

        std::string line;
        while (std::getline(txtFile, line)) {
            std::istringstream iss(line);
            int label;
            iss >> label;

            std::vector<real> features;
            real value;
            while (iss >> value) {
                features.push_back(value / static_cast <real>(255.0));  // 归一化
            }

            data.emplace_back(features, label);
        }
        txtFile.close();

        // 将数据保存为二进制文件
        std::ofstream binFile(binPath, std::ios::binary);
        if (!binFile) {
            std::cerr << "Failed to open binary file for writing: " << binPath << std::endl;
            return data;
        }

        size_t numSamples = data.size();
        binFile.write(reinterpret_cast<const char*>(&numSamples), sizeof(numSamples));

        for (const auto& sample : data) {
            int label = sample.second;
            binFile.write(reinterpret_cast<const char*>(&label), sizeof(label));

            size_t numFeatures = sample.first.size();
            binFile.write(reinterpret_cast<const char*>(&numFeatures), sizeof(numFeatures));

            binFile.write(reinterpret_cast<const char*>(sample.first.data()), numFeatures * sizeof(real));
        }
        binFile.close();
    }

    return data;
}

int testNetwork(std::vector<Layer> network, std::vector<std::pair<std::vector<real>, int>> testData)
{
    // 测试神经网络
    int correctCount = 0;
    for (const auto& data : testData) {
        const std::vector<real>& input = data.first;
        int label = data.second;

        int predictedLabel = predict(network, input);
        if (predictedLabel == label) {
            correctCount++;
        }
    }

    // 计算准确率
    std::cout << "计算准确率(" << correctCount << "/" << testData.size() << ")" << std::endl;
    real accuracy = static_cast<real>(correctCount) / testData.size();
    std::cout << "Accuracy: " << accuracy * 100 << "%" << std::endl;
    return correctCount;
}

int main() {
    // 加载训练数据和测试数据
    std::cout << "加载训练数据" << std::endl;
    std::vector<std::pair<std::vector<real>, int>> trainingData = loadData("train.txt");
    std::cout << "加载测试数据" << std::endl;
    std::vector<std::pair<std::vector<real>, int>> testData = loadData("test.txt");

    if (trainingData.empty() || testData.empty()) {
        return 1;
    }

    size_t inputSize = trainingData[0].first.size();
    std::vector<int> hiddenSizes = { 256, 128, 64 };
    int outputSize = 10;

    // 初始化神经网络
    std::cout << "初始化神经网络" << std::endl;
    std::vector<Layer> network = 
        initializeNetwork(inputSize, hiddenSizes, outputSize);

    // 训练神经网络
    int epochs = 100;
    real learningRate = static_cast <real>(0.005);
    real lambda = static_cast<real>(0.0005);
    std::cout << "训练神经网络" << std::endl;
    trainNetwork(network, trainingData, testData, epochs, learningRate, lambda);

   
    std::cout << "测试神经网络" << std::endl;
    testNetwork(network, testData);

    return 0;
}