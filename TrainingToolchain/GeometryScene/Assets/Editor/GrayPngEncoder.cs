using System;
using System.IO;
using System.IO.Compression;

/// <summary>
/// 灰度 PNG 编码器（纯 C#，无外部依赖）。
/// 输出 8-bit 灰度 PNG（color type 0），与 ImageToScene 训练管线
/// （PIL Image.fromarray(mode="L")）生成的图片格式一致，
/// 保证 PIL 解码后得到 (H, W) 的单通道 uint8 数组。
/// </summary>
internal static class GrayPngEncoder
{
    private static readonly byte[] PngSignature =
        { 137, 80, 78, 71, 13, 10, 26, 10 };

    /// <summary>
    /// 编码 8-bit 灰度 PNG。
    /// </summary>
    /// <param name="gray">灰度像素，行优先、自顶向下（top-down）排列。</param>
    /// <param name="width">宽度（像素）。</param>
    /// <param name="height">高度（像素）。</param>
    public static byte[] Encode(byte[] gray, int width, int height)
    {
        if (gray == null || gray.Length != (long)width * height)
        {
            throw new ArgumentException("灰度像素数组长度与尺寸不符");
        }

        // 原始扫描线：每行前置 filter byte 0（None），自顶向下
        byte[] raw = new byte[height * (width + 1)];
        for (int y = 0; y < height; y++)
        {
            int src = y * width;
            int dst = y * (width + 1);
            raw[dst] = 0; // filter type: None
            Buffer.BlockCopy(gray, src, raw, dst + 1, width);
        }

        byte[] idat = ZlibCompress(raw);

        using (var ms = new MemoryStream(raw.Length / 2 + 256))
        {
            ms.Write(PngSignature, 0, PngSignature.Length);

            byte[] ihdr = new byte[13];
            WriteUInt32Be(ihdr, 0, (uint)width);
            WriteUInt32Be(ihdr, 4, (uint)height);
            ihdr[8] = 8;  // bit depth
            ihdr[9] = 0;  // color type: grayscale
            ihdr[10] = 0; // compression: deflate
            ihdr[11] = 0; // filter: adaptive
            ihdr[12] = 0; // interlace: none
            WriteChunk(ms, "IHDR", ihdr);
            WriteChunk(ms, "IDAT", idat);
            WriteChunk(ms, "IEND", new byte[0]);

            return ms.ToArray();
        }
    }

    private static void WriteUInt32Be(byte[] buf, int offset, uint value)
    {
        buf[offset] = (byte)(value >> 24);
        buf[offset + 1] = (byte)(value >> 16);
        buf[offset + 2] = (byte)(value >> 8);
        buf[offset + 3] = (byte)value;
    }

    private static void WriteChunk(MemoryStream ms, string type, byte[] data)
    {
        byte[] typeBytes =
        {
            (byte)type[0], (byte)type[1], (byte)type[2], (byte)type[3]
        };

        WriteUInt32(ms, (uint)data.Length);
        ms.Write(typeBytes, 0, 4);
        ms.Write(data, 0, data.Length);

        // CRC 覆盖 type+data（PNG 规范）；Crc32 内部对入参与返回值各做一次取反，
        // 因此把 type 的最终 CRC 作为 data 的运行值传入，等价于连续计算 type||data
        uint crc = Crc32(data, Crc32(typeBytes, 0u));
        WriteUInt32(ms, crc);
    }

    private static void WriteUInt32(MemoryStream ms, uint value)
    {
        ms.WriteByte((byte)(value >> 24));
        ms.WriteByte((byte)(value >> 16));
        ms.WriteByte((byte)(value >> 8));
        ms.WriteByte((byte)value);
    }

    // ================= zlib（RFC 1950/1951） =================

    private static byte[] ZlibCompress(byte[] data)
    {
        using (var ms = new MemoryStream())
        {
            // zlib 头：CMF=0x78（deflate, 32K 窗口），FLG=0x9C（保证 (CMF<<8|FLG) % 31 == 0）
            ms.WriteByte(0x78);
            ms.WriteByte(0x9C);
            using (var deflate = new DeflateStream(ms, CompressionLevel.Optimal, true))
            {
                deflate.Write(data, 0, data.Length);
            }
            // zlib 尾：未压缩数据的 Adler-32（大端）
            uint adler = Adler32(data);
            ms.WriteByte((byte)(adler >> 24));
            ms.WriteByte((byte)(adler >> 16));
            ms.WriteByte((byte)(adler >> 8));
            ms.WriteByte((byte)adler);
            return ms.ToArray();
        }
    }

    private static uint Adler32(byte[] data)
    {
        const uint mod = 65521;
        uint a = 1, b = 0;
        for (int i = 0; i < data.Length; i++)
        {
            a = (a + data[i]) % mod;
            b = (b + a) % mod;
        }
        return (b << 16) | a;
    }

    // ================= CRC-32（PNG 规范） =================

    private static readonly uint[] CrcTable = BuildCrcTable();

    private static uint[] BuildCrcTable()
    {
        var table = new uint[256];
        for (uint n = 0; n < 256; n++)
        {
            uint c = n;
            for (int k = 0; k < 8; k++)
            {
                c = (c & 1) != 0 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            }
            table[n] = c;
        }
        return table;
    }

    private static uint Crc32(byte[] data, uint crc)
    {
        crc ^= 0xFFFFFFFFu;
        for (int i = 0; i < data.Length; i++)
        {
            crc = CrcTable[(crc ^ data[i]) & 0xFF] ^ (crc >> 8);
        }
        return crc ^ 0xFFFFFFFFu;
    }
}
