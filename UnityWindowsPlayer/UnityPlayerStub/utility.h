#ifndef _UTILITY_H_
#define _UTILITY_H_

#define MESH_FILE_FORMAT "mesh_%08d."
#define TEXTURE_FILE_FORMAT "tex_%08d."
#define MATERIAL_FILE_FORMAT "mtl_%08d."
#define VS_FILE_FORMAT "code_vs_%08d."
#define PS_FILE_FORMAT "code_ps_%08d."
#define GS_FILE_FORMAT "code_gs_%08d."

DEFINE_GUID(IID_TexIndex, 0xfda44dfb, 0xa7a6, 0x4497, 0x8b, 0x29, 0x7f, 0x66, 0xb6, 0x2c, 0xdc, 0x8e);
DEFINE_GUID(IID_TexName, 0x30133614, 0xd990, 0x474f, 0xae, 0xa1, 0xd6, 0x7c, 0x88, 0x25, 0x72, 0x3c);
DEFINE_GUID(IID_BufferName, 0xf89cd0f8, 0x62a0, 0x490e, 0xa9, 0xeb, 0x6d, 0x5d, 0xe0, 0x27, 0x2d, 0x81);
DEFINE_GUID(IID_TexMapPtr, 0x7c01d51, 0x47df, 0x402d, 0xbb, 0x2c, 0x59, 0x52, 0x76, 0x66, 0x73, 0xa8);
DEFINE_GUID(IID_ShaderResourceBindArray, 0x1c664534, 0x414d, 0x4856, 0xa4, 0xe9, 0x1, 0xb6, 0x3c, 0x2a, 0x94, 0x7a); // shader index, texture count, texture view slot 0, ... 
DEFINE_GUID(IID_ResourceOperation, 0xc355dfdc, 0x70c7, 0x4aa6, 0xa7, 0x60, 0xc4, 0xa7, 0x55, 0x77, 0x5a, 0xde);


#define TEXID_TO_INDEX(_ID) (_ID & 0x7fffffff)


struct REPLACE_VTBL_ITEM
{
  size_t interface_offset;
  void* OldFunc;
  void* MyFunc;
};

enum class ShaderType : int
{
  Vertex,
  Pixel,
  Geometry,
};


LPCSTR D3DFormatToString(DXGI_FORMAT fmt);
LPCSTR D3DTopologyToString(D3D11_PRIMITIVE_TOPOLOGY topology);
LPCSTR D3DResouceDimensionToString(D3D11_RESOURCE_DIMENSION dim);
LPCSTR D3DUsageToString(D3D11_USAGE usage);
LPCSTR D3D11MapToString(D3D11_MAP _map);
LPCSTR D3DShaderInputTypeToString(D3D_SHADER_INPUT_TYPE type);
LPCSTR D3DCPUAccessFlagsToString(UINT flags);

clStringA& GUIDToString(clStringA& str, GUID guid);
clStringA& GUIDToNameString(clStringA& str, GUID guid);

void CreateOutputDirectory();
void ReplaceVtbl(void* pComObj, REPLACE_VTBL_ITEM* pDescs);

UINT GetFormatSize(DXGI_FORMAT type);
D3D11_INPUT_ELEMENT_DESC* GetDeclVertexOffset(D3D11_INPUT_ELEMENT_DESC* pElements, UINT nNumElements, UINT index, UINT slot, LPCSTR name);

UINT GetTriangleCount(UINT nIndicesCount, D3D_PRIMITIVE_TOPOLOGY topology);

float ReadScaler(const void*& pData, DXGI_FORMAT fmt);
const void* ReadFloat2(float2& v, const void* pData, DXGI_FORMAT fmt);
const void* ReadFloat3(float3& v, const void* pData, DXGI_FORMAT fmt);
const void* ReadFloat4(float4& v, const void* pData, DXGI_FORMAT fmt);
void DumpElements(clstd::File& file, LPCSTR szPrefix, D3D11_INPUT_ELEMENT_DESC* pElements, UINT nNumElements);
UINT GetMaxElementsSlot(D3D11_INPUT_ELEMENT_DESC* pElements, UINT nNumElements);
//UINT CreateArrayFromUintVector(clvector<UINT>& _array);

BOOL IsGameCapturing();
BOOL SetGameCapture();
void SaveDisassemblyShaderCode(clvector<UINT>* slot_array, ShaderType type, const void *pShaderBytecode, SIZE_T BytecodeLength, UINT nFileIndex);
void GenerateShaderTextureDesc(clvector<UINT>& slot_array, const void *pShaderBytecode, SIZE_T BytecodeLength); // slot_array只在后面追加内容，不会清除原有内容
int GenerateCRC32Array(u32* pCRC32Array, const void* pData, size_t count);
clStringA& MakeUniqueResourceName(clStringA& strOut, const void* pData, size_t count);
clStringA& MakeUniqueTextureName(clStringA& strOut, UINT width, UINT height, DXGI_FORMAT format, const void* pData);

extern WCHAR g_szOutputDir[MAX_PATH];
extern clFile g_LogFile;

#endif // _UTILITY_H_