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

#if 1
#define ACTIVATION sigmoid
#define ACTIVATION_DERIVATIVE sigmoidDerivative
#else
#define ACTIVATION relu
#define ACTIVATION_DERIVATIVE reluDerivative
#endif

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
std::vector<Layer> initializeNetwork(size_t inputSize, const std::vector<int>& hiddenSizes, int outputSize) {
    std::vector<Layer> network;
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(static_cast<real>(-0.5), static_cast <real>(0.5));

    // 输入层到第一个隐藏层
    Layer firstHidden;
    firstHidden.weights.resize(hiddenSizes[0], std::vector<real>(inputSize));
    firstHidden.biases.resize(hiddenSizes[0]);
    firstHidden.outputs.resize(hiddenSizes[0]);
    firstHidden.inputs.resize(inputSize);
    firstHidden.errors.resize(hiddenSizes[0]);
    for (int i = 0; i < hiddenSizes[0]; ++i) {
        for (size_t j = 0; j < inputSize; ++j) {
            firstHidden.weights[i][j] = static_cast<real>(dis(gen));
        }
        firstHidden.biases[i] = static_cast<real>(dis(gen));
    }
    network.push_back(firstHidden);

    // 隐藏层之间
    for (size_t i = 1; i < hiddenSizes.size(); ++i) {
        Layer hidden;
        hidden.weights.resize(hiddenSizes[i], std::vector<real>(hiddenSizes[i - 1]));
        hidden.biases.resize(hiddenSizes[i]);
        hidden.outputs.resize(hiddenSizes[i]);
        hidden.inputs.resize(hiddenSizes[i - 1]);
        hidden.errors.resize(hiddenSizes[i]);
        for (int j = 0; j < hiddenSizes[i]; ++j) {
            for (int k = 0; k < hiddenSizes[i - 1]; ++k) {
                hidden.weights[j][k] = static_cast<real>(dis(gen));
            }
            hidden.biases[j] = static_cast<real>(dis(gen));
        }
        network.push_back(hidden);
    }

    // 最后一个隐藏层到输出层
    Layer output;
    output.weights.resize(outputSize, std::vector<real>(hiddenSizes.back()));
    output.biases.resize(outputSize);
    output.outputs.resize(outputSize);
    output.inputs.resize(hiddenSizes.back());
    output.errors.resize(outputSize);
    for (int i = 0; i < outputSize; ++i) {
        for (int j = 0; j < hiddenSizes.back(); ++j) {
            output.weights[i][j] = static_cast<real>(dis(gen));
        }
        output.biases[i] = static_cast<real>(dis(gen));
    }
    network.push_back(output);

    return network;
}

// 前向传播

void forwardPropagation(std::vector<Layer>& network, const std::vector<real>& input) {
    // 输入层到第一个隐藏层
    network[0].inputs = input;
    for (size_t i = 0; i < network[0].outputs.size(); ++i)
    {
        real sum = network[0].biases[i];
        for (size_t j = 0; j < input.size(); ++j)
        {
            sum += network[0].weights[i][j] * input[j];
        }
        network[0].outputs[i] = ACTIVATION(sum);
    }



    // 隐藏层之间
    for (size_t i = 1; i < network.size(); ++i)
    {
        network[i].inputs = network[i - 1].outputs;
        for (size_t j = 0; j < network[i].outputs.size(); ++j)
        {
            real sum = network[i].biases[j];
            for (size_t k = 0; k < network[i - 1].outputs.size(); ++k)
            {
                sum += network[i].weights[j][k] * network[i - 1].outputs[k];
            }
            network[i].outputs[j] = ACTIVATION(sum);
        }
    }
}

// 反向传播
void backPropagation(std::vector<Layer>& network, const std::vector<real>& target, real learningRate) {
    // 输出层误差
    for (size_t i = 0; i < network.back().outputs.size(); ++i) {
        //network.back().errors[i] = (target[i] - network.back().outputs[i]) * ACTIVATION_DERIVATIVE(network.back().outputs[i]);
        network.back().errors[i] = (target[i] - network.back().outputs[i]); // 去掉了sigmoid导数项
    }

    // 隐藏层误差
    for (int i = static_cast<int>(network.size()) - 2; i >= 0; --i) {
        for (size_t j = 0; j < network[i].outputs.size(); ++j) {
            real error = 0.0;
            for (size_t k = 0; k < network[i + 1].errors.size(); ++k) {
                error += network[i + 1].errors[k] * network[i + 1].weights[k][j];
            }
            network[i].errors[j] = error * ACTIVATION_DERIVATIVE(network[i].outputs[j]);
        }
    }

    // 更新权重和偏置
    for (size_t i = 0; i < network.size(); ++i) {
        for (size_t j = 0; j < network[i].weights.size(); ++j) {
            for (size_t k = 0; k < network[i].weights[j].size(); ++k) {
                network[i].weights[j][k] += learningRate * network[i].errors[j] * network[i].inputs[k];
            }
            network[i].biases[j] += learningRate * network[i].errors[j];
        }
    }
}

// 训练神经网络
void trainNetwork(std::vector<Layer>& network, const std::vector<std::pair<std::vector<real>, int>>& trainingData, std::vector<std::pair<std::vector<real>, int>> testData, int epochs, real learningRate)
{
    int lastCorrectCount = 0;
    for (int epoch = 0; epoch < epochs; ++epoch) {
        std::cout << "第" << (epoch + 1) << "轮学习" << std::endl;

        size_t i = 0;
        for (const auto& data : trainingData) {
            const std::vector<real>& input = data.first;
            int label = data.second;

            // 构建目标向量
            std::vector<real> target(10, 0.0);
            target[label] = 1.0;

            // 前向传播
            forwardPropagation(network, input);

            // 反向传播
            backPropagation(network, target, learningRate);

            std::cout << "进度:" << i++ << "/" << trainingData.size() << "\r";
        }

        int correctCount = testNetwork(network, testData);
        if (lastCorrectCount == correctCount)
        {
            break;
        }
        else if (lastCorrectCount > correctCount)
        {
            learningRate *= static_cast<real>(0.1);
            std::cout << "准确率下降，调低学习进度" << std::endl;
        }
        lastCorrectCount = correctCount;
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

// 加载数据集
//std::vector<std::pair<std::vector<real>, int>> loadData(const std::string& filename) {
//    std::vector<std::pair<std::vector<real>, int>> data;
//
//    //std::filesystem::exists()
//    
//    std::ifstream file(filename);
//    if (!file.is_open()) {
//        std::cerr << "Failed to open file: " << filename << std::endl;
//        return data;
//    }
//
//    std::string line;
//    while (std::getline(file, line)) {
//        std::istringstream iss(line);
//        int label;
//        iss >> label;
//
//        std::vector<real> features;
//        real value;
//        while (iss >> value) {
//            features.push_back(value / static_cast <real>(255.0));  // 归一化
//        }
//
//        data.emplace_back(features, label);
//    }
//
//    file.close();
//    return data;
//}

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
    std::vector<int> hiddenSizes = { 128, 64, 64, 64 };
    int outputSize = 10;

    // 初始化神经网络
    std::cout << "初始化神经网络" << std::endl;
    std::vector<Layer> network = 
        initializeNetwork(inputSize, hiddenSizes, outputSize);

    // 训练神经网络
    int epochs = 40;
    real learningRate = static_cast <real>(0.001);
    std::cout << "训练神经网络" << std::endl;
    trainNetwork(network, trainingData, testData, epochs, learningRate);

   
    std::cout << "测试神经网络" << std::endl;
    testNetwork(network, testData);

    return 0;
}