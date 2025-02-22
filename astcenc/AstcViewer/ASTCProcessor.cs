using System;
using System.Collections.Generic;
using System.Diagnostics;
using System;
using System.IO;
using System.Security.Cryptography;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Runtime.InteropServices;
using System.Drawing.Imaging;

namespace CDNTextureMgr
{

    public class ASTCWrapper
    {
        [DllImport("wrapper.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern int compress_astc(byte[] inBuf, byte[] outBuf, int outBufLen, int width, int height, int block_x, int block_y, float quality);

        [DllImport("wrapper.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern int decompress_astc(byte[] inBuf, int inSize, int width, int height, int block_x, int block_y, byte[] outBuf, out int outChannels);

        [DllImport("wrapper.dll", CallingConvention = CallingConvention.StdCall)]
        private static extern void get_last_error([MarshalAs(UnmanagedType.LPStr)] StringBuilder buffer, int length);

        public class FileHandler
        {
            [StructLayout(LayoutKind.Sequential, Pack = 1)]
            public struct ASTCHeader
            {
                [MarshalAs(UnmanagedType.ByValArray, SizeConst = 4)]
                public byte[] Magic;
                public byte _blockX;
                public byte _blockY;
                public byte _blockZ;

                [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)]
                private byte[] _dimX;

                [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)]
                private byte[] _dimY;

                [MarshalAs(UnmanagedType.ByValArray, SizeConst = 3)]
                private byte[] _dimZ;

                // 辅助属性处理实际值转换
                public int BlockX { get => _blockX; set => _blockX = (byte)value; }
                public int BlockY { get => _blockY; set => _blockY = (byte)value; }
                public int BlockZ { get => _blockZ; set => _blockZ = (byte)value; }

                // 维度属性（小端转换）
                public int DimensionX
                {
                    get => _dimX[0] | (_dimX[1] << 8) | (_dimX[2] << 16);
                    set
                    {
                        _dimX = new byte[3];
                        _dimX[0] = (byte)(value & 0xFF);
                        _dimX[1] = (byte)((value >> 8) & 0xFF);
                        _dimX[2] = (byte)((value >> 16) & 0xFF);
                    }
                }

                public int DimensionY
                {
                    get => _dimY[0] | (_dimY[1] << 8) | (_dimY[2] << 16);
                    set
                    {
                        _dimY = new byte[3];
                        _dimY[0] = (byte)(value & 0xFF);
                        _dimY[1] = (byte)((value >> 8) & 0xFF);
                        _dimY[2] = (byte)((value >> 16) & 0xFF);
                    }
                }
                public int DimensionZ
                {
                    get => _dimZ[0] | (_dimZ[1] << 8) | (_dimZ[2] << 16);
                    set
                    {
                        _dimZ = new byte[3];
                        _dimZ[0] = (byte)(value & 0xFF);
                        _dimZ[1] = (byte)((value >> 8) & 0xFF);
                        _dimZ[2] = (byte)((value >> 16) & 0xFF);
                    }
                }
            }

            public static ASTCHeader ReadHeader(string path)
            {
                byte[] headerBytes = new byte[Marshal.SizeOf(typeof(ASTCHeader))];
                Debug.Assert(headerBytes.Length == 16);

                using (FileStream fs = new FileStream(path, FileMode.Open))
                {
                    fs.Read(headerBytes, 0, headerBytes.Length);
                }

                GCHandle handle = GCHandle.Alloc(headerBytes, GCHandleType.Pinned);
                try
                {
                    ASTCHeader header = (ASTCHeader)Marshal.PtrToStructure(handle.AddrOfPinnedObject(), typeof(ASTCHeader));

                    // 验证魔数
                    if (!CheckMagic(header.Magic))
                        throw new InvalidDataException("Invalid ASTC file format");

                    return header;
                }
                finally
                {
                    handle.Free();
                }
            }

            public static void Write(string path, ASTCHeader header, byte[] data)
            {
                byte[] buffer = new byte[Marshal.SizeOf(typeof(ASTCHeader))];
                GCHandle handle = GCHandle.Alloc(buffer, GCHandleType.Pinned);

                try
                {
                    header.Magic = new byte[4];
                    header.Magic[0] = 0x13;
                    header.Magic[1] = 0xAB;
                    header.Magic[2] = 0xA1;
                    header.Magic[3] = 0x5C;
                    Marshal.StructureToPtr(header, handle.AddrOfPinnedObject(), false);
                    using (FileStream fs = new FileStream(path, FileMode.Create))
                    {
                        fs.Write(buffer, 0, buffer.Length);
                        fs.Write(data);
                    }
                    return;
                }
                finally
                {
                    handle.Free();
                }
            }

            private static bool CheckMagic(byte[] magic)
            {
                return magic.Length >= 4 &&
                       magic[0] == 0x13 &&
                       magic[1] == 0xAB &&
                       magic[2] == 0xA1 &&
                       magic[3] == 0x5C;
            }
        }


        public static byte[] Compress(byte[] input, int width, int height, int block_x, int block_y, float quality = 1.0f)
        {
            int xblocks = ((width + (block_x - 1)) / block_x);
            int yblocks = ((height + (block_y - 1)) / block_y);
            byte[] output = new byte[xblocks * yblocks * 16]; // 计算输出大小
            int result = compress_astc(input, output, output.Length, width, height, block_x, block_y, quality);

            if (result != 0)
            {
                StringBuilder errorMsg = new StringBuilder(256);
                get_last_error(errorMsg, 256);
                throw new ApplicationException($"ASTC压缩失败: {errorMsg}");
            }

            return output;
        }

        public static byte[] Decompress(byte[] compressedData, int width, int height, int block_x, int block_y)
        {
            int channels;
            byte[] output = new byte[compressedData.Length * 16]; // 预分配足够空间

            int result = decompress_astc(compressedData, compressedData.Length, width, height, block_x, block_y, output, out channels);

            if (result <= 0)
            {
                StringBuilder errorMsg = new StringBuilder(256);
                get_last_error(errorMsg, 256);
                throw new ApplicationException($"ASTC解压缩失败: {errorMsg}");
            }

            Array.Resize(ref output, width * height * channels);
            return output;
        }
    }


    public class ASTCProcessor
    {
        public static Bitmap LoadImage(string path, out byte[] rgbaData)
        {
            using var bmp = new Bitmap(path);
            var rect = new Rectangle(0, 0, bmp.Width, bmp.Height);
            var bitmapData = bmp.LockBits(rect, ImageLockMode.ReadOnly, PixelFormat.Format32bppArgb);

            try
            {
                // 转换为RGBA格式
                int byteCount = bitmapData.Stride * bmp.Height;
                rgbaData = new byte[byteCount];
                Marshal.Copy(bitmapData.Scan0, rgbaData, 0, byteCount);
                bmp.UnlockBits(bitmapData);

                return new Bitmap(bmp); // 返回深拷贝
            }
            finally
            {
            }
        }
        private static Bitmap CreateImage(byte[] rgbaData, int width, int height)
        {
            var bmp = new Bitmap(width, height, PixelFormat.Format32bppArgb);
            var rect = new Rectangle(0, 0, width, height);
            var bitmapData = bmp.LockBits(rect, ImageLockMode.WriteOnly, bmp.PixelFormat);

            try
            {
                Marshal.Copy(rgbaData, 0, bitmapData.Scan0, rgbaData.Length);
                return bmp;
            }
            finally
            {
                bmp.UnlockBits(bitmapData);
            }
        }

        public static Image ProcessImage(string sourceImagePath, string strBlockSize, out int fileLength)
        {
            //byte[] data = File.ReadAllBytes(sourceImagePath);
            if (!int.TryParse(strBlockSize.Substring(0, 1), out int block_x) || !int.TryParse(strBlockSize.Substring(2, 1), out int block_y))
            {
                fileLength = 0;
                return null;
            }

            Bitmap bitmap = LoadImage(sourceImagePath, out byte[] rgbaData);
            byte[] astcData = ASTCWrapper.Compress(rgbaData, bitmap.Width, bitmap.Height, block_x, block_y);
            fileLength = astcData.Length + 16; // astcenc产生的文件头长度为16字节

            int width = bitmap.Width;
            int height = bitmap.Height;


            byte[] decompressData = ASTCWrapper.Decompress(astcData, width, height, block_x, block_y);

            bitmap.Dispose();

            bitmap = CreateImage(decompressData, width, height);
            return bitmap;
        }
    }
}
