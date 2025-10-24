#include <iostream>
#include <string>
#include <string_view>
#include <tuple>
#include "argparse.hpp"
#include "FreeImage.h"

int CropImage(const char* inputFile, const char* outputFile, int left, int top, int right, int bottom);

int main(int argc, char** argv)
{
    int x = 0, y = 0, right = 0, bottom = 0;
    int w = 0, h = 0;

    FreeImage_Initialise();

    argparse::ArgumentParser program("ImageCrop", "1.0", argparse::default_arguments::version);
    program.add_argument("-i").required().help("输入图像文件");
    program.add_argument("-o").required().help("输出图像文件");
    program.add_argument("-x").required().store_into(x).help("图像左上角x");
    program.add_argument("-y").required().store_into(y).help("图像左上角y");
    //program.add_argument("-w").help("图像宽度");
    //program.add_argument("-h").help("图像高度");

    {
        auto& group = program.add_mutually_exclusive_group(true);
        group.add_argument("-w").store_into(w).help("图像宽度");
        group.add_argument("-r").store_into(right).help("图像右下角坐标x");
    }

    {
        auto& group = program.add_mutually_exclusive_group(true);
        group.add_argument("-h").store_into(h).help("图像高度");
        group.add_argument("-b").store_into(bottom).help("图像右下角坐标y");
    }

    try {
        program.parse_args(argc, argv); // 尝试解析参数
    }
    catch (const std::exception& err) {
        std::cerr << err.what() << std::endl; // 输出错误信息
        std::cerr << program; // 输出用法帮助
        FreeImage_DeInitialise();
        return 1;
    }

    std::string inputFile = program.get<std::string>("-i");
    std::string outputFile = program.get<std::string>("-o");
    
    if (program.is_used("-w"))
    {
        right = x + w;
    }

    if (program.is_used("-h"))
    {
        bottom = y + h;
    }

    CropImage(inputFile.c_str(), outputFile.c_str(), x, y, right, bottom);


    FreeImage_DeInitialise();
    return 0;
}


int CropImage(const char* inputFile, const char* outputFile, int left, int top, int right, int bottom)
{
    // 加载源图像
    FREE_IMAGE_FORMAT fif = FreeImage_GetFileType(inputFile, 0); // 自动检测文件格式
    FIBITMAP* bitmap = FreeImage_Load(fif, inputFile);

    if (!bitmap) {
        std::cerr << "错误：无法加载图像文件 " << inputFile << std::endl;
        return -1;
    }

    // 执行裁剪操作
    FIBITMAP* croppedBitmap = FreeImage_Copy(bitmap, left, top, right, bottom);
    if (!croppedBitmap) {
        std::cerr << "错误：图像裁剪失败！" << std::endl;
        FreeImage_Unload(bitmap);
        return -2;
    }

    // 保存裁剪后的图像
    fif = FreeImage_GetFIFFromFilename(outputFile);
    if (!FreeImage_Save(fif, croppedBitmap, outputFile)) {
        std::cerr << "错误：无法保存图像到 " << outputFile << std::endl;
    }
    else {
        std::cout << "图片已成功裁剪并保存为: " << outputFile << std::endl;
    }

    FreeImage_Unload(croppedBitmap);
    FreeImage_Unload(bitmap);
    return 0;
}

