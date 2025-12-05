#include "pch.h"
#include "IUnityGraphics.h"
#include "IUnityInterface.h"
#include <d3d11.h>
#include <d3d12.h>
#include "Unity/IUnityGraphicsD3D11.h"
#include "Unity/IUnityGraphicsD3D12.h"
#include "redirection.h"

#define DEF_PFUNC(_NAME) _NAME##_T pfunc##_NAME
#define LOCATE_API(_NAME)  pfunc##_NAME = reinterpret_cast<_NAME##_T>(GetProcAddress(hRenderingPluginDll, #_NAME))

static IUnityInterfaces* s_UnityInterfaces = NULL;
static IUnityGraphics* s_Graphics = NULL;
static UnityGfxRenderer s_RendererType = kUnityGfxRendererNull;
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);
HMODULE hRenderingPluginDll = nullptr;

ID3D12Device* s_pD3D12Device = nullptr;
ID3D11Device* s_pD3D11Device = nullptr;


typedef void (*UnityPluginLoad_T)(IUnityInterfaces* unityInterfaces);
typedef void (*UnityPluginUnload_T)();

typedef uint32_t(*GetBackBufferHeight_T)();
typedef uint32_t(*GetBackBufferWidth_T)();
typedef uint32_t(*GetPresentFlags_T)();
typedef INT_PTR(*GetRenderEventFunc_T)();
typedef INT_PTR(*GetRenderTexture_T)();
typedef uint32_t(*GetSyncInterval_T)();
typedef bool    (*IsSwapChainAvailable_T)();
typedef void    (*SetMeshBuffersFromUnity_T)(INT_PTR vertexBuffer, int vertexCount, INT_PTR sourceVertices, INT_PTR sourceNormals, INT_PTR sourceUVs);
typedef void    (*SetRenderTexture_T)(INT_PTR rb);
typedef void    (*SetTextureFromUnity_T)(INT_PTR texture, int w, int h);
typedef void    (*SetTimeFromUnity_T)(float t);

DEF_PFUNC(UnityPluginLoad);
DEF_PFUNC(UnityPluginUnload);
DEF_PFUNC(GetBackBufferHeight);
DEF_PFUNC(GetBackBufferWidth);
DEF_PFUNC(GetPresentFlags);
DEF_PFUNC(GetRenderEventFunc);
DEF_PFUNC(GetRenderTexture);
DEF_PFUNC(GetSyncInterval);
DEF_PFUNC(IsSwapChainAvailable);
DEF_PFUNC(SetMeshBuffersFromUnity);
DEF_PFUNC(SetRenderTexture);
DEF_PFUNC(SetTextureFromUnity);
DEF_PFUNC(SetTimeFromUnity);

//class MyUnityInterfaces : IUnityInterfaces
//{
//    IUnityInterface* (UNITY_INTERFACE_API * GetInterface)(UnityInterfaceGUID guid);
//
//    // Registers a new interface.
//    void(UNITY_INTERFACE_API * RegisterInterface)(UnityInterfaceGUID guid, IUnityInterface * ptr);
//
//    // Split APIs for C
//    IUnityInterface* (UNITY_INTERFACE_API * GetInterfaceSplit)(unsigned long long guidHigh, unsigned long long guidLow);
//    void(UNITY_INTERFACE_API * RegisterInterfaceSplit)(unsigned long long guidHigh, unsigned long long guidLow, IUnityInterface * ptr);
//
//};

IUnityInterface* (UNITY_INTERFACE_API * oldGetInterface)(UnityInterfaceGUID guid);
UnityGfxRenderer(UNITY_INTERFACE_API * oldGetRenderer)();  // Thread safe
IUnityInterface* UNITY_INTERFACE_API MyGetInterface(UnityInterfaceGUID guid);
UnityGfxRenderer UNITY_INTERFACE_API MyGetRenderer();
ID3D11Device* CreateD3D11Device(ID3D12Device* pD3D12Device);

static ID3D11Device* s_pMyD3D11Device = nullptr;
ID3D11Device* UNITY_INTERFACE_API MyGetDevice()
{
    return s_pMyD3D11Device;
}

ID3D11Resource* UNITY_INTERFACE_API MyTextureFromRenderBuffer(UnityRenderBuffer buffer)
{
    return nullptr;
}

ID3D11Resource* UNITY_INTERFACE_API MyTextureFromNativeTexture(UnityTextureID texture)
{
    return nullptr;
}

ID3D11RenderTargetView* UNITY_INTERFACE_API MyRTVFromRenderBuffer(UnityRenderBuffer surface)
{
    return nullptr;
}

ID3D11ShaderResourceView* UNITY_INTERFACE_API MySRVFromNativeTexture(UnityTextureID texture)
{
    return nullptr;
}

IUnityGraphicsD3D11 s_MyUnityGraphicsD3D11;
//{ {
//    nullptr
//    //&MyGetDevice,
//    //&MyTextureFromRenderBuffer,
//    //&MyTextureFromNativeTexture,
//    //&MyRTVFromRenderBuffer,
//    //&MySRVFromNativeTexture,
//} };

IUnityInterface* UNITY_INTERFACE_API MyGetInterface(UnityInterfaceGUID guid)
{
    if (GetUnityInterfaceGUID<IUnityGraphicsD3D11>() == guid)
    {
        UnityGfxRenderer realRenderer = oldGetRenderer();
        if (realRenderer == kUnityGfxRendererD3D12)
        {
            if (s_pMyD3D11Device == nullptr)
            {
                IUnityGraphicsD3D12v2* d3dv2 = static_cast<IUnityGraphicsD3D12v2*>(oldGetInterface(GetUnityInterfaceGUID<IUnityGraphicsD3D12v2>()));
                s_pMyD3D11Device = CreateD3D11Device(d3dv2->GetDevice());

                s_MyUnityGraphicsD3D11.GetDevice                = MyGetDevice;
                s_MyUnityGraphicsD3D11.TextureFromRenderBuffer  = MyTextureFromRenderBuffer;
                s_MyUnityGraphicsD3D11.TextureFromNativeTexture = MyTextureFromNativeTexture;
                s_MyUnityGraphicsD3D11.RTVFromRenderBuffer      = MyRTVFromRenderBuffer;
                s_MyUnityGraphicsD3D11.SRVFromNativeTexture     = MySRVFromNativeTexture;
            }
            return &s_MyUnityGraphicsD3D11;
        }
        return oldGetInterface(guid);

        //OutputDebugStringA("Replace D3D11Device ptr\n");
    }
    else if (GetUnityInterfaceGUID<IUnityGraphicsD3D12>() == guid)
    {
        OutputDebugStringA("Replace D3D12Device ptr\n");
    }
    else if (GetUnityInterfaceGUID<IUnityGraphicsD3D12v2>() == guid)
    {
        OutputDebugStringA("Replace D3D12Device v2 ptr\n");
    }
    else if (GetUnityInterfaceGUID<IUnityGraphicsD3D12v3>() == guid)
    {
        OutputDebugStringA("Replace D3D12Device v3 ptr\n");
    }
    else if (GetUnityInterfaceGUID<IUnityGraphicsD3D12v4>() == guid)
    {
        OutputDebugStringA("Replace D3D12Device v4 ptr\n");
    }
    else if (GetUnityInterfaceGUID<IUnityGraphicsD3D12v5>() == guid)
    {
        OutputDebugStringA("Replace D3D12Device v5 ptr\n");
    }
    else if (GetUnityInterfaceGUID<IUnityGraphicsD3D12v6>() == guid)
    {
        OutputDebugStringA("Replace D3D12Device v6 ptr\n");
    }
    else if (GetUnityInterfaceGUID<IUnityGraphicsD3D12v7>() == guid)
    {
        OutputDebugStringA("Replace D3D12Device v7 ptr\n");
    }
    else if (GetUnityInterfaceGUID<IUnityGraphics>() == guid)
    {
        IUnityGraphics* pUnityGraphics = static_cast<IUnityGraphics*>(oldGetInterface(guid));
        if (oldGetRenderer == nullptr)
        {
            oldGetRenderer = pUnityGraphics->GetRenderer;
            pUnityGraphics->GetRenderer = MyGetRenderer;
        }
        return pUnityGraphics;
    }
    return oldGetInterface(guid);
}

UnityGfxRenderer UNITY_INTERFACE_API MyGetRenderer()
{
    UnityGfxRenderer renderer = oldGetRenderer();
    if (renderer == kUnityGfxRendererD3D12)
    {
        return kUnityGfxRendererD3D11;
    }
    return renderer;
}


void DLL_API UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces)
{
    if (oldGetInterface == nullptr)
    {
        oldGetInterface = unityInterfaces->GetInterface;
        unityInterfaces->GetInterface = MyGetInterface;
    }

    s_UnityInterfaces = unityInterfaces;
    s_Graphics = unityInterfaces->Get<IUnityGraphics>();
    s_Graphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

    // 在插件加载时运行 OnGraphicsDeviceEvent(initialize)
    // 在图形设备已初始化的情况下不错过该事件
    OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);

    pfuncUnityPluginLoad(unityInterfaces);
}

// Unity 插件卸载事件
void DLL_API UNITY_INTERFACE_API UnityPluginUnload()
{
    s_Graphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
    pfuncUnityPluginUnload();
}

static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
    switch (eventType)
    {
    case kUnityGfxDeviceEventInitialize:
    {
        s_RendererType = s_Graphics->GetRenderer();
        if (s_RendererType == kUnityGfxRendererD3D11)
        {
            IUnityGraphicsD3D11* d3d = s_UnityInterfaces->Get<IUnityGraphicsD3D11>();
		    s_pD3D11Device = d3d->GetDevice();
        }
        else if (s_RendererType == kUnityGfxRendererD3D12)
        {
            IUnityGraphicsD3D12* d3d = s_UnityInterfaces->Get<IUnityGraphicsD3D12>();
            IUnityGraphicsD3D12v2* d3dv2 = s_UnityInterfaces->Get<IUnityGraphicsD3D12v2>();
            IUnityGraphicsD3D12v3* d3dv3 = s_UnityInterfaces->Get<IUnityGraphicsD3D12v3>();
            IUnityGraphicsD3D12v4* d3dv4 = s_UnityInterfaces->Get<IUnityGraphicsD3D12v4>();
            IUnityGraphicsD3D12v5* d3dv5 = s_UnityInterfaces->Get<IUnityGraphicsD3D12v5>();
            IUnityGraphicsD3D12v6* d3dv6 = s_UnityInterfaces->Get<IUnityGraphicsD3D12v6>();
            IUnityGraphicsD3D12v7* d3dv7 = s_UnityInterfaces->Get<IUnityGraphicsD3D12v7>();
            if (d3d)
            {
		        s_pD3D12Device = d3d->GetDevice();
            }
            else if (d3dv2)
            {
		        s_pD3D12Device = d3dv2->GetDevice();
            }
            else if (d3dv3)
            {
		        s_pD3D12Device = d3dv3->GetDevice();
            }
            else if (d3dv4)
            {
		        s_pD3D12Device = d3dv4->GetDevice();
            }
            else if (d3dv5)
            {
		        s_pD3D12Device = d3dv5->GetDevice();
            }
            else if (d3dv6)
            {
		        s_pD3D12Device = d3dv6->GetDevice();
            }
            else if (d3dv7)
            {
		        s_pD3D12Device = d3dv7->GetDevice();
            }
        }
        break;
    }
    case kUnityGfxDeviceEventShutdown:
    {
        s_RendererType = kUnityGfxRendererNull;
        //TODO：用户关闭代码
        break;
    }
    case kUnityGfxDeviceEventBeforeReset:
    {
        //TODO：用户 Direct3D 9 代码
        break;
    }
    case kUnityGfxDeviceEventAfterReset:
    {
        //TODO：用户 Direct3D 9 代码
        break;
    }
    };
}

/////////////////////////////////////////////////////////////////////////////

uint32_t DLL_API UNITY_INTERFACE_API GetBackBufferHeight()
{
    return pfuncGetBackBufferHeight();
}

uint32_t DLL_API UNITY_INTERFACE_API GetBackBufferWidth()
{
    return pfuncGetBackBufferWidth();
}

uint32_t DLL_API UNITY_INTERFACE_API GetPresentFlags()
{
    return pfuncGetPresentFlags();
}

INT_PTR DLL_API UNITY_INTERFACE_API GetRenderEventFunc()
{
    return pfuncGetRenderEventFunc();
}

INT_PTR DLL_API UNITY_INTERFACE_API GetRenderTexture()
{
    return pfuncGetRenderTexture();
}

uint32_t DLL_API UNITY_INTERFACE_API GetSyncInterval()
{
    return pfuncGetSyncInterval();
}

bool DLL_API UNITY_INTERFACE_API IsSwapChainAvailable()
{
    return pfuncIsSwapChainAvailable();
}

void DLL_API UNITY_INTERFACE_API SetMeshBuffersFromUnity(INT_PTR vertexBuffer, int vertexCount, INT_PTR sourceVertices, INT_PTR sourceNormals, INT_PTR sourceUVs)
{
    pfuncSetMeshBuffersFromUnity(vertexBuffer, vertexCount, sourceVertices, sourceNormals, sourceUVs);
}

void DLL_API UNITY_INTERFACE_API SetRenderTexture(INT_PTR rb)
{
    pfuncSetRenderTexture(rb);
}

void DLL_API UNITY_INTERFACE_API SetTextureFromUnity(INT_PTR texture, int w, int h)
{
    pfuncSetTextureFromUnity(texture, w, h);
}

void DLL_API UNITY_INTERFACE_API SetTimeFromUnity(float t)
{
    pfuncSetTimeFromUnity(t);
}
/////////////////////////////////////////////////////////////////////////////

void OnProcessAttach()
{
    if (hRenderingPluginDll == nullptr)
    {
        hRenderingPluginDll = LoadLibrary(L"RenderingPlugin");
        if (hRenderingPluginDll)
        {
            LOCATE_API(UnityPluginLoad);
            LOCATE_API(UnityPluginUnload);

            LOCATE_API(GetBackBufferHeight);
            LOCATE_API(GetBackBufferWidth);
            LOCATE_API(GetPresentFlags);
            LOCATE_API(GetRenderEventFunc);
            LOCATE_API(GetRenderTexture);
            LOCATE_API(GetSyncInterval);
            LOCATE_API(IsSwapChainAvailable);
            LOCATE_API(SetMeshBuffersFromUnity);
            LOCATE_API(SetRenderTexture);
            LOCATE_API(SetTextureFromUnity);
            LOCATE_API(SetTimeFromUnity);
        }
    }
}

void OnProcessDetach()
{
    if (hRenderingPluginDll)
    {
        FreeLibrary(hRenderingPluginDll);
        hRenderingPluginDll = nullptr;
    }
}
