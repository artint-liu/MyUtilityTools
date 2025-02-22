#pragma once
#ifdef ASTCENC_WRAPPER_EXPORTS
#define ASTCENC_API __declspec(dllexport)
#else
#define ASTCENC_API __declspec(dllimport)
#endif

extern "C" {
    ASTCENC_API int __stdcall compress_astc(const unsigned char* inBuf, unsigned char* outBuf, int outBufLen, int width, int height, int block_x, int block_y, float quality);
    ASTCENC_API int __stdcall decompress_astc(const unsigned char* inBuf, int inSize, int width, int height, int block_x, int block_y, unsigned char* outBuf, int* outChannels);
    ASTCENC_API void __stdcall get_last_error(char* buffer, int length);
}
