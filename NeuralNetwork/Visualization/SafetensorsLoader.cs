using System;
using System.Collections.Generic;
using System.IO;
using System.Text;
using Newtonsoft.Json;

public class SafetensorsLoader
{
    public Dictionary<string, Array> Load(string filePath)
    {
        // 读取文件二进制数据
        byte[] fileBytes = File.ReadAllBytes(filePath);

        // 1. 提取头部长度（前8字节）
        byte[] headerSizeBytes = new byte[8];
        Array.Copy(fileBytes, 0, headerSizeBytes, 0, 8);
        ulong headerSize = BitConverter.ToUInt64(headerSizeBytes, 0);

        // 2. 提取头部JSON（UTF-8编码）
        string headerJson = Encoding.UTF8.GetString(fileBytes, 8, (int)headerSize);
        var metadata = JsonConvert.DeserializeObject<Dictionary<string, dynamic>>(headerJson);

        // 3. 提取张量数据部分
        int dataOffset = 8 + (int)headerSize;
        byte[] tensorData = new byte[fileBytes.Length - dataOffset];
        Array.Copy(fileBytes, dataOffset, tensorData, 0, tensorData.Length);

        // 4. 解析所有张量
        var tensors = new Dictionary<string, Array>();
        foreach (var entry in metadata)
        {
            if (entry.Key == "__metadata__") continue;

            var tensorInfo = entry.Value;
            string dtype = tensorInfo.dtype;
            List<int> shape = tensorInfo.shape.ToObject<List<int>>();
            ulong[] offsets = tensorInfo.data_offsets.ToObject<ulong[]>();

            // 计算数据段的位置
            int start = (int)offsets[0];
            int end = (int)offsets[1];
            byte[] rawData = new byte[end - start];
            Array.Copy(tensorData, start, rawData, 0, rawData.Length);

            // 根据数据类型转换
            Array data = ParseTensorData(rawData, dtype, shape);
            tensors.Add(entry.Key, data);
        }

        return tensors;
    }

    private Array ParseTensorData(byte[] rawData, string dtype, List<int> shape)
    {
        // 根据数据类型转换二进制数据
        switch (dtype)
        {
            case "F32":
                float[] floatArray = new float[rawData.Length / 4];
                Buffer.BlockCopy(rawData, 0, floatArray, 0, rawData.Length);
                return Reshape(floatArray, shape);
            case "I32":
                int[] intArray = new int[rawData.Length / 4];
                Buffer.BlockCopy(rawData, 0, intArray, 0, rawData.Length);
                return Reshape(intArray, shape);
            // 添加其他类型支持（如F64、I64等）
            default:
                throw new NotSupportedException($"Unsupported dtype: {dtype}");
        }
    }

    private Array Reshape(Array source, List<int> shape)
    {
        // 将一维数组转换为多维形状
        Array reshaped = Array.CreateInstance(source.GetType().GetElementType(), shape.ToArray());
        Buffer.BlockCopy(source, 0, reshaped, 0, Buffer.ByteLength(source));
        return reshaped;
    }
}