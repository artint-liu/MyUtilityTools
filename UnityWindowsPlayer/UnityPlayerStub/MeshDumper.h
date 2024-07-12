#ifndef _MESH_DUMPER_H_
#define _MESH_DUMPER_H_

class MyID3D11Device;

struct DRAWCALL
{
  ID3D11Buffer* pD3DVertices0Buffer;
  ID3D11Buffer* pD3DVertices1Buffer;
  ID3D11Buffer* pD3DIndicesBuffer;
  //ID3D11PixelShader* pD3DPixelShader;
  UINT tex_id[4];
  //UINT idTexture1;
  UINT IndexCount;
  UINT StartIndexLocation;
  clStringA strStaticName;

  DRAWCALL() = default;

  DRAWCALL(ID3D11Buffer* v0, ID3D11Buffer* v1, ID3D11Buffer* i, /*ID3D11PixelShader* pps, */UINT _count, UINT _start)
    : pD3DVertices0Buffer(v0)
    , pD3DVertices1Buffer(v1)
    , pD3DIndicesBuffer(i)
    //, pD3DPixelShader(pps)
    //, idTexture0(t0)
    //, idTexture1(t1)
    , IndexCount(_count)
    , StartIndexLocation(_start)
  {
    memset(tex_id, 0xffffffff, sizeof(tex_id));
  }

  bool operator<(const DRAWCALL& v) const
  {
    //g_LogFile.Writef("%s vs %s\r\n", strStaticString.CStr(), v.strStaticString.CStr());
    //ASSERT(strStaticString.IsNotEmpty() || v.strStaticString.IsNotEmpty());

    //return strStaticString.Compare(v.strStaticString) < 0;
    return (memcmp(this, &v, sizeof(DRAWCALL)) < 0);
  }

  BOOL GererateMeshStaticString(clStringA& strOut, MyID3D11Device* pDevice) const;
};

struct DRAWCALL_CONTEXT
{
  //DRAWCALL dc;
  const clstd::Buffer* pVertices0Buffer;
  const clstd::Buffer* pVertices1Buffer;
  const clstd::Buffer* pIndicesBuffer;

  void* pVertices0Obj;
  void* pVertices1Obj;
  void* pIndicesObj;
  void* pPixelShaderObj;
  UINT  nShaderIndex;

  UINT start_index;
  UINT vertex_stride[2];
  DXGI_FORMAT indices_format;
  D3D11_PRIMITIVE_TOPOLOGY topology;
  D3D11_INPUT_ELEMENT_DESC* pElementDesc;
  UINT num_elements;
  UINT num_triangles;
  UINT tex_id[countof(DRAWCALL::tex_id)];
  clStringA tex_name[countof(DRAWCALL::tex_id)];
  clStringA strExtraInformation;
  //UINT tex1_id;
  //UINT tex2_id;
  //UINT tex3_id;
};

void SaveMeshFile(const DRAWCALL_CONTEXT* pContext, LPCWSTR szFilename);


#endif // _MESH_ DUMPER_H_