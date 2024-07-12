#ifndef _ID3D11INPUTLAYOUT_HOOK_H_
#define _ID3D11INPUTLAYOUT_HOOK_H_
#if 0 // 不能这么用
class MyID3D11InputLayout : public ID3D11InputLayout
{
  ID3D11InputLayout* m_pD3DInput;
  clstd::Buffer m_buffer;
public:
  MyID3D11InputLayout(ID3D11InputLayout* pD3DInput, const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs, UINT NumElements, const void *pShaderBytecodeWithInputSignature, SIZE_T BytecodeLength);

public:
  HRESULT STDMETHODCALLTYPE QueryInterface(
    /* [in] */ REFIID riid,
    /* [annotation][iid_is][out] */
    _COM_Outptr_  void **ppvObject);

  ULONG STDMETHODCALLTYPE AddRef(
    );

  ULONG STDMETHODCALLTYPE Release(
    );

  void STDMETHODCALLTYPE GetDevice(
    /* [annotation] */
    _Outptr_  ID3D11Device **ppDevice);

  HRESULT STDMETHODCALLTYPE GetPrivateData(
    /* [annotation] */
    _In_  REFGUID guid,
    /* [annotation] */
    _Inout_  UINT *pDataSize,
    /* [annotation] */
    _Out_writes_bytes_opt_(*pDataSize)  void *pData);

  HRESULT STDMETHODCALLTYPE SetPrivateData(
    /* [annotation] */
    _In_  REFGUID guid,
    /* [annotation] */
    _In_  UINT DataSize,
    /* [annotation] */
    _In_reads_bytes_opt_(DataSize)  const void *pData);

  HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(
    /* [annotation] */
    _In_  REFGUID guid,
    /* [annotation] */
    _In_opt_  const IUnknown *pData);

};
#endif // 0

#endif // _ID3D11INPUTLAYOUT_HOOK_H_