import struct

# 读取图像文件
def read_images(filename):
    with open(filename, 'rb') as f:
        magic, num, rows, cols = struct.unpack('>IIII', f.read(16))
        images = []
        for i in range(num):
            image = []
            for j in range(rows * cols):
                pixel = struct.unpack('>B', f.read(1))[0]
                image.append(pixel)
            images.append(image)
    return images

# 读取标签文件
def read_labels(filename):
    with open(filename, 'rb') as f:
        magic, num = struct.unpack('>II', f.read(8))
        labels = []
        for i in range(num):
            label = struct.unpack('>B', f.read(1))[0]
            labels.append(label)
    return labels

# 将数据保存为文本文件
def save_to_text(images, labels, filename):
    with open(filename, 'w') as f:
        for i in range(len(images)):
            label = labels[i]
            image = images[i]
            line = str(label)
            for pixel in image:
                line += ' ' + str(pixel)
            f.write(line + '\n')

# 读取训练数据
train_images = read_images('train-images.idx3-ubyte')
train_labels = read_labels('train-labels.idx1-ubyte')

# 读取测试数据
test_images = read_images('t10k-images.idx3-ubyte')
test_labels = read_labels('t10k-labels.idx1-ubyte')

# 保存为文本文件
save_to_text(train_images, train_labels, 'train.txt')
save_to_text(test_images, test_labels, 'test.txt')