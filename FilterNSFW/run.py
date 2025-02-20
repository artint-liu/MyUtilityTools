import tensorflow as tf
import numpy as np
import cv2
import argparse
import os

# 加载模型
# model = tf.saved_model.load("resnet_50_1by2_nsfw.caffemodel")
# model = tf.saved_model.load("open_nsfw_weights.h5")

# 加载 Caffe 模型
def load_model(prototxt_path, model_path):
    net = cv2.dnn.readNetFromCaffe(prototxt_path, model_path)
    return net

# 预处理图片
def preprocess_image(net, image_path):
    # 读取图片
    # image = cv2.imread(image_path)

    # 用二进制模式读取文件
    with open(image_path, 'rb') as f:
        image_data = np.frombuffer(f.read(), dtype=np.uint8)

    # 解码图像数据
    image = cv2.imdecode(image_data, cv2.IMREAD_COLOR)

    # 调整图片大小为 224x224，并进行归一化
    blob = cv2.dnn.blobFromImage(image, scalefactor=1.0, size=(224, 224), mean=(104.0, 117.0, 123.0), swapRB=False, crop=False)

    # 进行预测
    nsfw_score = predict_nsfw(net, blob)
    print(f"NSFW Score: {image_path}: {nsfw_score:.4f}")
    return nsfw_score

def predict_nsfw(net, image_blob):
    # 输入图片到模型
    net.setInput(image_blob)
    # 获取输出
    predictions = net.forward()
    # 获取 NSFW 分数（第二个输出值）
    nsfw_score = predictions[0][1]
    return nsfw_score


def main():
    # 设置命令行参数解析器
    parser = argparse.ArgumentParser(description="Detect NSFW image")
    parser.add_argument("image_path", type=str, help="input image file")
    args = parser.parse_args()

    # 模型文件路径
    prototxt_path = "deploy.prototxt"  # Caffe 模型配置文件
    model_path = "resnet_50_1by2_nsfw.caffemodel"  # Caffe 模型权重文件

    # 加载模型
    net = load_model(prototxt_path, model_path)

    # 获取图片路径
    image_path = args.image_path

    if os.path.isfile(image_path):
        print('file:' + image_path)
        # 处理图片
        nsfw_score = preprocess_image(net, image_path)

        if nsfw_score > 0.5:
            print("The image may contain NSFW content.")
        else:
            print("The image is safe.")
    elif os.path.isdir(image_path):
        file_list = []
        print('dir:' + image_path)
        for root, dirs, files in os.walk(image_path):
            for file in files:
                name, ext = os.path.splitext(file)
                ext = ext.lower()
                # print('ext:' + ext)
                if ext == '.jpg' or ext == '.png' or ext == '.jpeg':
                    full_path = os.path.join(root, file)
                    scrore = preprocess_image(net, full_path)
                    if scrore > 0.5:
                        print('nsfw:%s'%(full_path))
                        file_list.append(full_path)

        if len(file_list) > 0:
            with open("result.txt", 'wt') as f:
                for filename in file_list:
                    f.write(filename)


if __name__ == "__main__":
    main()