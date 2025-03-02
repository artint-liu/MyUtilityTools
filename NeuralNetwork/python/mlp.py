# 导入必要的库
import numpy as np
import matplotlib.pyplot as plt
from tensorflow.keras.datasets import mnist
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import Dense, Flatten
from tensorflow.keras.utils import to_categorical
from safetensors.torch import save_file
import torch
from tensorflow.keras.models import load_model

# 1. 加载MNIST数据集
(train_images, train_labels), (test_images, test_labels) = mnist.load_data()

# 2. 数据预处理
# 归一化像素值到[0,1]
train_images = train_images.astype('float32') / 255.0
test_images = test_images.astype('float32') / 255.0

# 将图像从28x28展平为784维向量
train_images = train_images.reshape(-1, 28 * 28) # (60000, 784)
test_images = test_images.reshape(-1, 28 * 28)

# 将标签转换为One-Hot编码
train_labels = to_categorical(train_labels, num_classes=10)
test_labels = to_categorical(test_labels, num_classes=10)

# 3. 构建MLP模型
model = Sequential([
    Dense(256, activation='relu', input_shape=(784,)),  # 输入层+第一个隐藏层
    # Dense(256, activation='relu'),                      # 第二个隐藏层
    # Dense(128, activation='relu'),                      # 第三个隐藏层
    Dense(10, activation='softmax')                     # 输出层（10个类别）
])

# 4. 编译模型
model.compile(optimizer='adam',
              loss='categorical_crossentropy',
              metrics=['accuracy'])

# 5. 训练模型
history = model.fit(train_images, train_labels,
                    batch_size=128,
                    epochs=25,
                    validation_split=0.1)  # 用10%训练数据作为验证集

# 6. 评估测试集
test_loss, test_acc = model.evaluate(test_images, test_labels)
print(f'\n测试集准确率: {test_acc:.4f}')

# 7. 可视化训练过程
plt.rcParams['font.sans-serif'] = ['SimHei']  # 指定默认字体[3,5,6,8,9](@ref)
plt.rcParams['axes.unicode_minus'] = False    # 解决负号显示为方框的问题[3,4,6](@ref)
plt.figure(figsize=(12, 4))
plt.subplot(1, 2, 1)
plt.plot(history.history['accuracy'], label='训练准确率')
plt.plot(history.history['val_accuracy'], label='验证准确率')
plt.title('训练和验证准确率')
plt.xlabel('Epoch')
plt.ylabel('Accuracy')
plt.legend()

plt.subplot(1, 2, 2)
plt.plot(history.history['loss'], label='训练损失')
plt.plot(history.history['val_loss'], label='验证损失')
plt.title('训练和验证损失')
plt.xlabel('Epoch')
plt.ylabel('Loss')
plt.legend()

# 8. 保存模型
model.save('mnist_mlp_model.h5')
# save_file(model, "minst_mlp_model.safetensors")
# 提取Keras模型权重并转换为torch张量
weights_dict = {}
keras_model = load_model('mnist_mlp_model.h5')
for idx, layer in enumerate(keras_model.layers):
    if layer.weights:
        # 将TensorFlow权重转换为numpy再转为torch张量
        weights = [torch.from_numpy(w.numpy()) for w in layer.weights]
        # 使用层名作为键（避免索引冲突）
        weights_dict[f"layer{idx}_{layer.name}_weights"] = weights[0]
        if len(weights) > 1:
            weights_dict[f"layer{idx}_{layer.name}_biases"] = weights[1]

# 保存为safetensors文件
save_file(weights_dict, "mnist_mlp_model.safetensors")


# 9. 预测示例（使用测试集中的第一张图）
sample_image = test_images[0].reshape(1, 784)
prediction = model.predict(sample_image)
predicted_label = np.argmax(prediction)

print(f'\n预测结果: {predicted_label}')
print(f'实际标签: {np.argmax(test_labels[0])}')

plt.show() # 最后显示窗口
