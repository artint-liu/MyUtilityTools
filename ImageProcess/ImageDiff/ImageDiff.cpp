#include <iostream>
#include <string>
#include <string_view>
#include <tuple>
#include "argparse.hpp"
#include "FreeImage.h"

int DiffImage(const char* inputFile1, const char* inputFile2, const char* outputFile);
float multi = 0.0f;
int background = 0;
bool bLog = false;

int main(int argc, char** argv)
{

    FreeImage_Initialise();

    argparse::ArgumentParser program("ImageDiff", "1.0", argparse::default_arguments::version);
    program.add_argument("input_files").help("输入图像文件（2个）").nargs(2);
    program.add_argument("-o").required().help("输出图像文件");
    program.add_argument("-m").store_into(multi).help("正数为误差放大值, 0时标记差异");
    program.add_argument("-b").store_into(background).help("背景图，0：无，1：第一张图，2：第二张图");
    program.add_argument("-log").store_into(bLog).help("打印日志");

    try {
        program.parse_args(argc, argv); // 尝试解析参数
    }
    catch (const std::exception& err) {
        std::cerr << err.what() << std::endl; // 输出错误信息
        std::cerr << program; // 输出用法帮助
        FreeImage_DeInitialise();
        return 1;
    }

    std::vector<std::string> inputFile = program.get <std::vector< std::string >>("input_files");
    std::string outputFile = program.get<std::string>("-o");

    if (inputFile.size() != 2)
    {
        std::cout << "输入文件要求为2个" << std::endl;
    }


    DiffImage(inputFile[0].c_str(), inputFile[1].c_str(), outputFile.c_str());

    std::cout << "MXCSR:" << std::hex << _mm_getcsr();

    FreeImage_DeInitialise();
    return 0;
}

FIBITMAP* LoadBitmap(const char* inputFile)
{
    FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(inputFile, 0); // 自动检测文件格式
    FIBITMAP* bitmap = FreeImage_Load(fif, inputFile);
    return bitmap;
}

template<typename _ChannelT, typename _CompT>
void ComparePixels(BYTE* resultBits, int width, int height, BYTE* bits1, BYTE* bits2, int pitch, _ChannelT maxValue, _ChannelT mark, _ChannelT alpha)
{
    _ChannelT minDiff = 0;
    _ChannelT maxDiff = 0;

    for (int y = 0; y < height; y++) {
        _ChannelT* pixel1 = reinterpret_cast<_ChannelT*>(bits1 + y * pitch);
        _ChannelT* pixel2 = reinterpret_cast<_ChannelT*>(bits2 + y * pitch);
        _ChannelT* resultPixel = reinterpret_cast<_ChannelT*>(resultBits + y * pitch);

        for (int x = 0; x < width; x++) {
            // 计算每个通道的绝对差值
            for (int channel = 0; channel < 3; channel++) { // 处理B、G、R通道
                _ChannelT diff = std::abs(pixel1[channel] - pixel2[channel]);
                
                if (diff != 0)
                {
                    if (bLog)
                    {
                        std::cout << "(" << x << "," << y << ")[" << channel << "], delta:" << diff << ", ";
                        std::cout << std::hex << *reinterpret_cast<uint32_t*>(&pixel1[channel]) << " vs " << *reinterpret_cast<uint32_t*>(&pixel2[channel]) << std::dec << std::endl;
                    }
                    minDiff = minDiff == 0 ? diff : std::min(minDiff, diff);
                    maxDiff = std::max(maxDiff, diff);
                }

                if (multi == 0)
                {
                    resultPixel[channel] = diff == 0 ? 0 : mark;
                }
                else
                {
                    resultPixel[channel] = static_cast<_ChannelT>(std::min(static_cast<_CompT>(diff * multi), static_cast<_CompT>(maxValue)));
                }

                if ((background == 1 || background == 2) && diff == 0)
                {
                    resultPixel[channel] = (background == 1) ? pixel1[channel] : pixel2[channel];
                }

            }
            // Alpha通道设置为不透明
            resultPixel[3] = alpha;

            // 移动到下一个像素（每个像素4字节：B、G、R、A）
            pixel1 += 4;
            pixel2 += 4;
            resultPixel += 4;
        }
    }

    std::cout << "min:" << minDiff << std::endl;
    std::cout << "max:" << maxDiff << std::endl;
}

FIBITMAP* CalculateImageDifference(FIBITMAP* dib1, FIBITMAP* dib2)
{
    // 1. 检查输入有效性
    if (!dib1 || !dib2) {
        return nullptr;
    }

    // 2. 获取图像尺寸并检查一致性
    int width1 = FreeImage_GetWidth(dib1);
    int height1 = FreeImage_GetHeight(dib1);
    int width2 = FreeImage_GetWidth(dib2);
    int height2 = FreeImage_GetHeight(dib2);

    if (width1 != width2 || height1 != height2) {
        return nullptr; // 图像尺寸必须相同
    }

    FREE_IMAGE_TYPE image_type = FreeImage_GetImageType(dib1);
    FIBITMAP* dib1_32bpp = nullptr;
    FIBITMAP* dib2_32bpp = nullptr;
    FIBITMAP* resultDib = nullptr;

    if ((image_type == FIT_BITMAP) || (image_type == FIT_RGB16) || (image_type == FIT_RGBA16))
    {
        // 3. 统一图像格式（转换为32位深度以确保处理一致性）
        dib1_32bpp = FreeImage_ConvertTo32Bits(dib1);
        dib2_32bpp = FreeImage_ConvertTo32Bits(dib2);

        // 4. 创建新图像用于存储差值结果
        resultDib = FreeImage_Allocate(width1, height1, 32, 0, 0, 0);
        if (!resultDib) {
            FreeImage_Unload(dib1_32bpp);
            FreeImage_Unload(dib2_32bpp);
            return nullptr;
        }

        // 5. 获取图像数据指针
        BYTE* bits1 = FreeImage_GetBits(dib1_32bpp);
        BYTE* bits2 = FreeImage_GetBits(dib2_32bpp);
        BYTE* resultBits = FreeImage_GetBits(resultDib);

        // 计算每行的字节数（步长）
        int pitch = FreeImage_GetPitch(resultDib);

        // 6. 逐像素计算差值
        ComparePixels<BYTE, uint32_t>(resultBits, width1, height1, bits1, bits2, pitch, 255, 255, 255);
    }
    else
    {
        // 3. 统一图像格式（转换为32位深度以确保处理一致性）
        dib1_32bpp = FreeImage_ConvertToRGBAF(dib1);
        dib2_32bpp = FreeImage_ConvertToRGBAF(dib2);


        unsigned bpp = FreeImage_GetBPP(dib1_32bpp);

        // 4. 创建新图像用于存储差值结果
        if (image_type == FIT_RGBF)
        {
            image_type = FIT_RGBAF;
        }

        resultDib = FreeImage_AllocateT(image_type, width1, height1, bpp, 0, 0, 0);
        if (!resultDib) {
            FreeImage_Unload(dib1_32bpp);
            FreeImage_Unload(dib2_32bpp);
            return nullptr;
        }

        // 5. 获取图像数据指针
        BYTE* bits1 = FreeImage_GetBits(dib1_32bpp);
        BYTE* bits2 = FreeImage_GetBits(dib2_32bpp);
        BYTE* resultBits = FreeImage_GetBits(resultDib);

        // 计算每行的字节数（步长）
        int pitch = FreeImage_GetPitch(resultDib);

        // 6. 逐像素计算差值
        ComparePixels<float, float>(resultBits, width1, height1, bits1, bits2, pitch, FLT_MAX, 1.0f, 1.0f);
    }

    // 7. 清理临时资源
    FreeImage_Unload(dib1_32bpp);
    FreeImage_Unload(dib2_32bpp);

    return resultDib;
}

void SaveImage(const char* outputFile, FIBITMAP* resultDib)
{
    // 保存裁剪后的图像
    FREE_IMAGE_FORMAT fif = FreeImage_GetFIFFromFilename(outputFile);
    if (!FreeImage_Save(fif, resultDib, outputFile)) {
        std::cerr << "错误：无法保存图像到 " << outputFile << std::endl;
    }
    else {
        std::cout << "图片已成功裁剪并保存为: " << outputFile << std::endl;
    }

}

int DiffImage(const char* inputFile1, const char* inputFile2, const char* outputFile)
{
    FIBITMAP* bitmap1 = LoadBitmap(inputFile1);
    FIBITMAP* bitmap2 = LoadBitmap(inputFile2);
    FIBITMAP* differenceImage = nullptr;
    do
    {
        if (bitmap1 == nullptr)
        {
            std::cout << "加载失败:" << inputFile1 << std::endl;
            break;
        }

        if (bitmap2 == nullptr)
        {
            std::cout << "加载失败:" << inputFile1 << std::endl;
            break;
        }

        if (FreeImage_GetImageType(bitmap1) != FreeImage_GetImageType(bitmap2))
        {
            std::cout << "像素格式不一致" << std::endl;
            break;
        }

        differenceImage = CalculateImageDifference(bitmap1, bitmap2);

        SaveImage(outputFile, differenceImage);
    } while (false);

    if(bitmap1)
        FreeImage_Unload(bitmap1);
    
    if(bitmap2)
        FreeImage_Unload(bitmap2);
    
    if(differenceImage)
        FreeImage_Unload(differenceImage);

    return 0;
}

