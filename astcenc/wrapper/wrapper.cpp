#define WIN32_LEAN_AND_MEAN             // 从 Windows 头文件中排除极少使用的内容
// Windows 头文件
#include <windows.h>

#include "wrapper.h"
#include "astcenc.h"
#include <string>
#include <stdexcept>

//#define ASTCENC_WRAPPER_EXPORTS
#define MAX_ERROR_LEN 256

thread_local static char lastError[MAX_ERROR_LEN] = { 0 };


ASTCENC_API int compress_astc(const unsigned char* inBuf, unsigned char* outBuf, int outBufLen, int width, int height, int block_x, int block_y, float quality)
{
    try {
        astcenc_config config;
        astcenc_profile profile = ASTCENC_PRF_LDR;

        astcenc_error status = astcenc_config_init(
            profile,
            block_x, block_y, 1, // 2D处理
            quality,
            0,
            &config
        );

        if (status != ASTCENC_SUCCESS) {
            throw std::runtime_error("Config initialization failed");
        }

        astcenc_context* context;
        status = astcenc_context_alloc(&config, 1, &context);
        if (status != ASTCENC_SUCCESS) {
            throw std::runtime_error("Context allocation failed");
        }

        astcenc_image image = {
            .dim_x = static_cast<unsigned int>(width),
            .dim_y = static_cast<unsigned int>(height),
            .dim_z = 1,
            .data_type = ASTCENC_TYPE_U8,
            .data = reinterpret_cast<void**>(const_cast<unsigned char**>(&inBuf))
        };

        astcenc_swizzle swz = {
            ASTCENC_SWZ_R, // R <- R
            ASTCENC_SWZ_G, // G <- G
            ASTCENC_SWZ_B, // B <- B
            ASTCENC_SWZ_A  // A <- A
        };

        status = astcenc_compress_image(context, &image, &swz, outBuf, (size_t)outBufLen, 0);

        astcenc_context_free(context);
        return (status == ASTCENC_SUCCESS) ? 0 : -1;
    }
    catch (const std::exception& e) {
        strncpy_s(lastError, e.what(), MAX_ERROR_LEN - 1);
        return -1;
    }
}

//// ASTC文件头结构（16字节）
//#pragma pack(push, 1)
//struct ASTCHeader {
//    uint8_t magic[4];     // 0x13 0xAB 0xA1 0x5C
//    uint8_t block_x;      // 块宽 (实际值+1)
//    uint8_t block_y;      // 块高 (实际值+1)
//    uint8_t block_z;      // 块深 (实际值+1) 
//    uint8_t dim_x[3];     // 宽度 (小端存储)
//    uint8_t dim_y[3];     // 高度
//    uint8_t dim_z[3];     // 深度
//};
//#pragma pack(pop)

ASTCENC_API int decompress_astc(
    const unsigned char* inBuf, int inSize, int width, int height, int block_x, int block_y, unsigned char* outBuf, int* outChannels)
{
    memset(lastError, 0, MAX_ERROR_LEN);

    // 参数验证
    if (!inBuf || !outBuf) {
        strcpy_s(lastError, "Null pointer in parameters");
        return -1;
    }

    //if (inSize < sizeof(ASTCHeader)) {
    //    strcpy_s(lastError, "Input buffer too small for ASTC header");
    //    return -1;
    //}

    //// 解析ASTC头部
    //const ASTCHeader* header = reinterpret_cast<const ASTCHeader*>(inBuf);

    //// 验证魔数
    //if (memcmp(header->magic, "\x13\xAB\xA1\x5C", 4) != 0) {
    //    strcpy_s(lastError, "Invalid ASTC magic number");
    //    return -1;
    //}

    // 解析块尺寸
    //int block_x = header->block_x + 1;
    //int block_y = header->block_y + 1;

    // 解析图像尺寸 (24bit小端)
    //int width = header->dim_x[0] | (header->dim_x[1] << 8) | (header->dim_x[2] << 16);
    //int height = header->dim_y[0] | (header->dim_y[1] << 8) | (header->dim_y[2] << 16);

    // 验证数据有效性
    if (block_x < 4 || block_x > 12 || block_y < 4 || block_y > 12) {
        strcpy_s(lastError, "Invalid block size");
        return -1;
    }

    if (width == 0 || height == 0) {
        strcpy_s(lastError, "Invalid image dimensions");
        return -1;
    }

    // 初始化解压配置
    astcenc_config config;
    astcenc_profile profile = ASTCENC_PRF_LDR;
    astcenc_error status = astcenc_config_init(profile, block_x, block_y, 1, 0.0f,  // 质量参数对解压无效
        0, &config);

    if (status != ASTCENC_SUCCESS) {
        snprintf(lastError, MAX_ERROR_LEN,
            "Decompress config failed: %s",
            astcenc_get_error_string(status));
        return -1;
    }

    // 创建解压上下文
    astcenc_context* context = nullptr;
    status = astcenc_context_alloc(&config, 1, &context);
    if (status != ASTCENC_SUCCESS) {
        snprintf(lastError, MAX_ERROR_LEN,
            "Context alloc failed: %s",
            astcenc_get_error_string(status));
        return -1;
    }

    // 准备输出图像 (RGBA8格式)
    astcenc_image image = {
        .dim_x = static_cast<uint32_t>(width),
        .dim_y = static_cast<uint32_t>(height),
        .dim_z = 1,
        .data_type = ASTCENC_TYPE_U8,
        .data = reinterpret_cast<void**>(&outBuf)
    };

    astcenc_swizzle swz = {
        ASTCENC_SWZ_R, // R <- R
        ASTCENC_SWZ_G, // G <- G
        ASTCENC_SWZ_B, // B <- B
        ASTCENC_SWZ_A  // A <- A
    };


    // 执行解压 (跳过16字节头)
    status = astcenc_decompress_image(
        context,
        inBuf,
        inSize,
        //inBuf + sizeof(ASTCHeader),  // 跳过头部
        //inSize - sizeof(ASTCHeader),
        &image,
        &swz, 0
    );

    // 清理资源
    astcenc_context_free(context);

    // 处理结果
    if (status != ASTCENC_SUCCESS) {
        snprintf(lastError, MAX_ERROR_LEN, "Decompress failed: %s", astcenc_get_error_string(status));
        return -1;
    }

    // 返回输出参数
    //*outWidth = width;
    //*outHeight = height;
    *outChannels = 4;  // ASTC固定输出RGBA

    // 返回解压后的数据长度
    return width * height * 4;
}

ASTCENC_API void get_last_error(char* buffer, int length) {
    strncpy_s(buffer, length, lastError, _TRUNCATE);
}
