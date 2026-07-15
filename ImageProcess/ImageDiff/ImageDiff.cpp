#include <iostream>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>
#include <cctype>
#include <filesystem>
#include "argparse.hpp"
#include "FreeImage.h"

int DiffImage(const char* inputFile1, const char* inputFile2, const char* outputFile);
int HandleSingleFile(const std::string& inputFile, const std::string& outputFile);
float multi = 0.0f;
int background = 0;
bool bLog = false;
std::string channelStr = "RGBA";      // 比较通道，默认 RGBA
std::vector<int> g_channels;          // 解析后的内部通道索引 (FreeImage 顺序: B=0,G=1,R=2,A=3)
std::string outputArg;                // -o 输出参数(可选)

// 取小写扩展名(含点)，如 ".png"；无扩展名返回空串
std::string GetExtension(const std::string& path)
{
    size_t dot = path.find_last_of('.');
    if (dot == std::string::npos || dot + 1 >= path.size()) return "";
    std::string ext = path.substr(dot);
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext;
}

// 默认输出文件名：ImageDiff + 第一个输入文件的扩展名(类型相同)
std::string GetDefaultOutputName(const std::string& firstInput)
{
    std::string ext = GetExtension(firstInput);
    if (ext.empty()) {
        // 无扩展名时根据文件实际格式推断
        FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(firstInput.c_str(), 0);
        if (fif != FIF_UNKNOWN) {
            const char* exts = FreeImage_GetFIFExtensionList(fif);
            std::string e(exts ? exts : "png");
            size_t sc = e.find(';');
            if (sc != std::string::npos) e = e.substr(0, sc);
            ext = "." + e;
        }
        else {
            ext = ".png";
        }
    }
    return "ImageDiff" + ext;
}

// 打印成功打开的文件信息
void PrintImageInfo(const char* filename, FIBITMAP* dib)
{
    FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(filename, 0);
    FREE_IMAGE_TYPE type = FreeImage_GetImageType(dib);
    unsigned bpp = FreeImage_GetBPP(dib);
    unsigned width = FreeImage_GetWidth(dib);
    unsigned height = FreeImage_GetHeight(dib);
    FREE_IMAGE_COLOR_TYPE ctype = FreeImage_GetColorType(dib);

    int channels = 0;
    const char* order = "未知";
    switch (type) {
    case FIT_BITMAP:
        switch (ctype) {
        case FIC_MINISBLACK: channels = 1; order = "灰度(单通道)"; break;
        case FIC_PALETTE:    channels = 1; order = "调色板(单通道)"; break;
        case FIC_RGB:        channels = 3; order = "BGR"; break;
        case FIC_RGBALPHA:   channels = 4; order = "BGRA"; break;
        case FIC_CMYK:       channels = 4; order = "CMYK"; break;
        default: channels = (bpp >= 24) ? 3 : 1; order = "BGR"; break;
        }
        break;
    case FIT_RGB16:  channels = 3; order = "BGR(16bit)"; break;
    case FIT_RGBA16: channels = 4; order = "BGRA(16bit)"; break;
    case FIT_RGBF:   channels = 3; order = "BGR(float)"; break;
    case FIT_RGBAF:  channels = 4; order = "BGRA(float)"; break;
    case FIT_UINT16:
    case FIT_INT16:
    case FIT_FLOAT:
    case FIT_DOUBLE: channels = 1; order = "单通道(float/灰度)"; break;
    case FIT_COMPLEX: channels = 2; order = "复数(实部+虚部)"; break;
    default: channels = 0; order = "未知"; break;
    }

    const char* fifName = FreeImage_GetFormatFromFIF(fif);
    std::cout << "文件: " << filename << std::endl;
    std::cout << "  编码格式: " << (fifName ? fifName : "未知") << std::endl;
    std::cout << "  尺寸: " << width << " x " << height << std::endl;
    std::cout << "  色深: " << bpp << " bit" << std::endl;
    std::cout << "  通道数: " << channels << std::endl;
    std::cout << "  颜色顺序: " << order << std::endl;
}

// 将用户传入的通道字符串(如 RGBA/RGB/RG/R/AB)解析为内部通道索引
std::vector<int> ParseChannels(const std::string& s)
{
    bool sel[4] = { false, false, false, false };
    for (char ch : s) {
        switch (std::tolower(static_cast<unsigned char>(ch))) {
        case 'r': sel[2] = true; break;   // R 在 FreeImage 中位于索引 2
        case 'g': sel[1] = true; break;
        case 'b': sel[0] = true; break;
        case 'a': sel[3] = true; break;
        default: break;                   // 忽略非法字符
        }
    }
    std::vector<int> channels;
    for (int i = 0; i < 4; i++) if (sel[i]) channels.push_back(i);
    if (channels.empty()) channels = { 0, 1, 2, 3 }; // 空输入回退为 RGBA
    return channels;
}

int main(int argc, char** argv)
{

    FreeImage_Initialise();

    argparse::ArgumentParser program("ImageDiff", "1.0", argparse::default_arguments::version);
    program.add_description("图像对比 / 通道过滤 / 格式转换工具：支持两张图比对差异，单文件复制、转换或按通道过滤输出。");
    program.add_epilog(R"(示例:
  比对两张图，输出 ImageDiff.png:       ImageDiff a.png b.png
  比对两张图并指定输出文件名:           ImageDiff a.png b.png -o diff.png
  仅比较红色通道:                       ImageDiff a.png b.png -c R
  仅比较 R、G 通道:                     ImageDiff a.png b.png -c RG
  单文件复制(输出 ImageDiff.png):       ImageDiff a.png
  单文件格式转换(png -> bmp):           ImageDiff a.png -o out.bmp
  单文件通道过滤(仅保留 R、G 并转 bmp): ImageDiff a.png -o out.bmp -c RG
  单文件仅保留红色通道:                 ImageDiff a.png -c R)");
    program.add_argument("input_files").help("输入图像文件（1个或2个）").nargs(1, 2);
    program.add_argument("-o").store_into(outputArg).help("输出图像文件(可选, 默认 ImageDiff+首个文件扩展名)");
    program.add_argument("-m").store_into(multi).help("正数为误差放大值, 0时标记差异(默认)");
    program.add_argument("-b").store_into(background).help("背景图，0：无，1：第一张图，2：第二张图");
    program.add_argument("-log").store_into(bLog).help("打印日志");
    program.add_argument("-c").store_into(channelStr).help("比较通道, 默认RGBA, 可设 RGB/RG/R/AB 等任意组合");

    try {
        program.parse_args(argc, argv); // 尝试解析参数
    }
    catch (const std::exception& err) {
        std::cerr << err.what() << std::endl; // 输出错误信息
        std::cerr << program; // 输出用法帮助
        FreeImage_DeInitialise();
        return 1;
    }

    g_channels = ParseChannels(channelStr);

    std::vector<std::string> inputFile = program.get <std::vector< std::string >>("input_files");

    // 确定输出文件名：未提供 -o 时默认 ImageDiff + 第一个输入文件的扩展名
    std::string outputFile = outputArg.empty() ? GetDefaultOutputName(inputFile[0]) : outputArg;

    if (inputFile.size() == 1)
    {
        HandleSingleFile(inputFile[0], outputFile);
    }
    else
    {
        DiffImage(inputFile[0].c_str(), inputFile[1].c_str(), outputFile.c_str());
    }

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
    // 根据解析出的通道列表构建选中标记 (FreeImage 顺序 B=0,G=1,R=2,A=3)
    bool sel[4] = { false, false, false, false };
    for (int c : g_channels) { if (c >= 0 && c < 4) sel[c] = true; }

    _ChannelT minDiff = 0;
    _ChannelT maxDiff = 0;

    for (int y = 0; y < height; y++) {
        _ChannelT* pixel1 = reinterpret_cast<_ChannelT*>(bits1 + y * pitch);
        _ChannelT* pixel2 = reinterpret_cast<_ChannelT*>(bits2 + y * pitch);
        _ChannelT* resultPixel = reinterpret_cast<_ChannelT*>(resultBits + y * pitch);

        for (int x = 0; x < width; x++) {
            // 依次处理 B、G、R、A 四个通道
            for (int channel = 0; channel < 4; channel++) {
                if (!sel[channel]) {
                    resultPixel[channel] = 0; // 未选中通道结果置 0
                    continue;
                }

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

            // 未选中 Alpha 通道时，强制设为不透明
            if (!sel[3]) resultPixel[3] = alpha;

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
        std::cout << "图片已成功对比并保存为: " << outputFile << std::endl;
    }

}

// 是否选择了全部 4 个通道(B,G,R,A)，用于判断是否需要过滤
bool IsFullRGBA()
{
    if (g_channels.size() != 4) return false;
    bool sel[4] = { false, false, false, false };
    for (int c : g_channels) if (c >= 0 && c < 4) sel[c] = true;
    return sel[0] && sel[1] && sel[2] && sel[3];
}

// 按 g_channels 从 4 通道源(src, BGR(A)顺序)过滤到 dst(dstChannels: 3 或 4)
template<typename T>
void FilterPixels(BYTE* dstBits, BYTE* srcBits, int width, int height, int dstPitch, int srcPitch, int dstChannels)
{
    bool sel[4] = { false, false, false, false };
    for (int c : g_channels) if (c >= 0 && c < 4) sel[c] = true;

    for (int y = 0; y < height; y++) {
        T* src = reinterpret_cast<T*>(srcBits + y * srcPitch);
        T* dst = reinterpret_cast<T*>(dstBits + y * dstPitch);
        for (int x = 0; x < width; x++) {
            for (int c = 0; c < dstChannels; c++) {
                dst[c] = sel[c] ? src[c] : T(0);
            }
            src += 4;            // 源始终为 4 通道 (B,G,R,A)
            dst += dstChannels;  // 目标为 3 或 4 通道
        }
    }
}

// 根据 -c 参数生成过滤后的图像：未选中通道置 0，未选中 Alpha 时输出 3 通道
FIBITMAP* ApplyChannelFilter(FIBITMAP* src)
{
    bool alphaSel = false;
    for (int c : g_channels) if (c == 3) alphaSel = true;

    FREE_IMAGE_TYPE type = FreeImage_GetImageType(src);
    int width = FreeImage_GetWidth(src);
    int height = FreeImage_GetHeight(src);
    int dstChannels = alphaSel ? 4 : 3;

    FIBITMAP* conv = nullptr;
    if (type == FIT_BITMAP || type == FIT_RGB16 || type == FIT_RGBA16) {
        conv = FreeImage_ConvertTo32Bits(src);
    }
    else {
        conv = FreeImage_ConvertToRGBAF(src);
    }
    if (!conv) return nullptr;

    FIBITMAP* out = nullptr;
    if (type == FIT_BITMAP || type == FIT_RGB16 || type == FIT_RGBA16) {
        out = FreeImage_Allocate(width, height, dstChannels == 4 ? 32 : 24, 0, 0, 0);
    }
    else {
        unsigned bpp = dstChannels == 4 ? 128 : 96; // float: 4*32 或 3*32 bit
        out = FreeImage_AllocateT(dstChannels == 4 ? FIT_RGBAF : FIT_RGBF,
            width, height, bpp, 0, 0, 0);
    }
    if (!out) {
        FreeImage_Unload(conv);
        return nullptr;
    }

    if (type == FIT_BITMAP || type == FIT_RGB16 || type == FIT_RGBA16) {
        FilterPixels<BYTE>(FreeImage_GetBits(out), FreeImage_GetBits(conv),
            width, height, FreeImage_GetPitch(out), FreeImage_GetPitch(conv), dstChannels);
    }
    else {
        FilterPixels<float>(FreeImage_GetBits(out), FreeImage_GetBits(conv),
            width, height, FreeImage_GetPitch(out), FreeImage_GetPitch(conv), dstChannels);
    }

    FreeImage_Unload(conv);
    return out;
}

int HandleSingleFile(const std::string& inputFile, const std::string& outputFile)
{
    FIBITMAP* bitmap = LoadBitmap(inputFile.c_str());
    if (!bitmap)
    {
        std::cout << "加载失败:" << inputFile << std::endl;
        return 1;
    }

    // 成功打开后输出文件信息
    PrintImageInfo(inputFile.c_str(), bitmap);

    bool filtering = !IsFullRGBA();               // -c 指定了非全部通道则需要过滤
    std::string inExt = GetExtension(inputFile);
    std::string outExt = GetExtension(outputFile);

    if (!filtering && inExt == outExt)
    {
        // 同格式且无需过滤：直接复制文件
        FreeImage_Unload(bitmap);
        std::error_code ec;
        std::filesystem::copy_file(inputFile, outputFile,
            std::filesystem::copy_options::overwrite_existing, ec);
        if (ec) {
            std::cerr << "错误：复制文件失败 - " << ec.message() << std::endl;
            return 1;
        }
        std::cout << "已复制为: " << outputFile << std::endl;
    }
    else
    {
        // 需要过滤或扩展名不同：加载后处理再保存
        FIBITMAP* out = bitmap;
        if (filtering)
        {
            out = ApplyChannelFilter(bitmap);
            FreeImage_Unload(bitmap);
            if (!out) {
                std::cerr << "错误：通道过滤失败" << std::endl;
                return 1;
            }
            std::cout << "已按通道(" << channelStr << ")过滤" << std::endl;
        }
        SaveImage(outputFile.c_str(), out);
        FreeImage_Unload(out);
    }

    return 0;
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
            std::cout << "加载失败:" << inputFile2 << std::endl;
            break;
        }

        // 成功打开后输出文件信息
        PrintImageInfo(inputFile1, bitmap1);
        PrintImageInfo(inputFile2, bitmap2);

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

