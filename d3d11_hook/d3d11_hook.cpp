#include "pch.h"
#include <d3d11.h>
#include "D3D11DeviceHook.h"
#include "D3D11DeviceContextHook.h"

void HookD3D11Device(ID3D11Device** ppDevice, ID3D11DeviceContext** ppImmediateContext)
{
  // 注意: ppDevice和ppImmediateContext是可以为NULL的！这里没有做判断
  //
  MyID3D11Device* pD3D11DeviceHook = new MyID3D11Device(*ppDevice);
  *ppDevice = reinterpret_cast<ID3D11Device*>(pD3D11DeviceHook);

  MyID3D11DeviceContext* pD3D11DeviceContextHook = new MyID3D11DeviceContext(pD3D11DeviceHook, *ppImmediateContext);
  pD3D11DeviceHook->SetImmediateDeviceContext(pD3D11DeviceContextHook);
  *ppImmediateContext = pD3D11DeviceContextHook;
}