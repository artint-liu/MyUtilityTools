#include "pch.h"
#include "IUnityGraphics.h"
#include "IUnityInterface.h"
#include <d3d11.h>
#include <d3d12.h>
#include "Unity/IUnityGraphicsD3D11.h"
#include "Unity/IUnityGraphicsD3D12.h"
#include "MyD3D11Device.h"
#include "redirection.h"

#define DEF_PFUNC(_NAME) _NAME##_T pfunc##_NAME
#define LOCATE_API(_NAME)  pfunc##_NAME = reinterpret_cast<_NAME##_T>(GetProcAddress(hAVProLiveCamera, #_NAME))

static IUnityInterfaces* s_UnityInterfaces = NULL;
static IUnityGraphics* s_Graphics = NULL;
static UnityGfxRenderer s_RendererType = kUnityGfxRendererNull;
static void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType);
HMODULE hAVProLiveCamera = nullptr;

ID3D12Device* s_pd3d12Device = nullptr;
ID3D11Device* s_pd3d11Device = nullptr;
IMyD3D11Device* g_pd3d11DeviceFake = nullptr;
ID3D12CommandQueue* g_pD3D12CommandQueue = nullptr;


typedef void          (*UnityPluginLoad_T)(IUnityInterfaces* unityInterfaces);
typedef void          (*UnityPluginUnload_T)();
typedef void          (*UnitySetGraphicsDevice_T)( void* device, int deviceType, int eventType);
typedef void          (*UnityRenderEvent_T)( int eventID);

typedef void          (*ApplyDeviceVideoSettingValue_T)(int deviceIndex, int settingIndex, float currentValue, bool isAutomatic);
typedef void          (*Deinit_T)();
typedef float         (*GetCaptureFrameRate_T)( int deviceIndex);
typedef unsigned int  (*GetCaptureFramesDropped_T)( int deviceIndex);
typedef bool          (*GetDeviceFormat_T)( int index, StringBuilder format);
typedef bool          (*GetDeviceGUID_T)( int index, StringBuilder nameBuffer, int nameBufferLength);
typedef bool          (*GetDeviceName_T)( int index, StringBuilder nameBuffer, int nameBufferLength);
typedef bool          (*GetDeviceVideoSettingBoolean_T)( int deviceIndex, int settingIndex, bool* defaultValue, bool* currentValue, bool* isAutomatic);
typedef bool          (*GetDeviceVideoSettingFloat_T)( int deviceIndex, int settingIndex, float* defaultValue, float* currentValue, float* minValue, float* maxValue, bool* isAutomatic);
typedef bool          (*GetDeviceVideoSettingInfo_T)( int deviceIndex, int settingIndex, int* settingType, int* dataType, StringBuilder name, bool* canAutomatic);
typedef int           (*GetFormat_T)( int index);
typedef bool          (*GetFrameAsColor32_T)( int index, INT_PTR bufferPtr, int bufferWidth, int bufferHeight);
typedef long          (*GetFrameDurationHNS_T)( int index);
typedef INT_PTR       (*GetFrameFromBufferAtTime_T)( int deviceIndex, long time);
typedef bool          (*GetFramePixels_T)( int index, INT_PTR buffer, int bufferIndex, int bufferWidth, int bufferHeight);
typedef float         (*GetFrameRate_T)( int index);
typedef int           (*GetHeight_T)( int index);
typedef unsigned int  (*GetLastFrame_T)( int index);
typedef INT_PTR       (*GetLastFrameBuffered_T)(int deviceIndex);
typedef long          (*GetLastFrameBufferedTime_T)( int deviceIndex);
typedef int           (*GetLastFrameUploaded_T)( int handle);
typedef bool          (*GetModeFrameRates_T)( int deviceIndex, int modeIndex, int frameRateCount, float* frameRates);
typedef bool          (*GetModeInfo_T)( int deviceIndex, int modeIndex, int* width, int* height, int* frameRateCount, int* frameRateIndex, StringBuilder format);
typedef int           (*GetNumDeviceVideoSettings_T)( int deviceIndex);
typedef int           (*GetNumDevices_T)( );
typedef int           (*GetNumModes_T)( int index);
typedef int           (*GetNumVideoInputs_T)( int deviceIndex);
typedef INT_PTR       (*GetPluginVersion_T)( );
typedef INT_PTR       (*GetRenderEventFunc_T)( );
typedef bool          (*GetVideoInputName_T)( int deviceIndex, int inputIndex, StringBuilder name);
typedef int           (*GetWidth_T)( int index);
typedef bool          (*HasDeviceConfigWindow_T)( int index);
typedef bool          (*Init_T)( bool supportInternalConversion);
typedef bool          (*IsDeviceConnected_T)( int index);
typedef bool          (*IsFrameTopDown_T)( int index);
typedef bool          (*IsNextFrameReadyForGrab_T)( int index);
typedef void          (*Pause_T)( int index);
typedef bool          (*Play_T)( int index);
typedef bool          (*SetActive_T)( int index, bool active);
typedef void          (*SetDeviceClockMode_T)( int deviceIndex, bool useDefaultClock);
typedef void          (*SetFrameBufferSize_T)( int deviceIndex, int read, int write);
typedef void          (*SetTexturePointer_T)( int index, int bufferIndex, INT_PTR texturePtr);
typedef void          (*SetVideoInputByIndex_T)( int deviceIndex, int inputIndex);
typedef bool          (*ShowDeviceConfigWindow_T)( int index);
typedef bool          (*StartDevice_T)( int index, int modeIndex, int frameRateIndex, int videoInputIndex, bool preferPreviewPin);
typedef void          (*Stop_T)( int index);
typedef void          (*StopDevice_T)( int index);
typedef bool          (*UpdateDevicesConnected_T)( );
typedef bool          (*UpdateDeviceVideoSettingValue_T)( int deviceIndex, int settingIndex, float* currentValue, bool* isAutomatic);
typedef bool          (*UpdateTextureGL_T)( int index, int textureID);
//typedef void          (*TUnitySetGraphicsDevice)( void* device, int deviceType, int eventType);

DEF_PFUNC(UnityPluginLoad);
DEF_PFUNC(UnityPluginUnload);
DEF_PFUNC(UnitySetGraphicsDevice);
DEF_PFUNC(UnityRenderEvent);

DEF_PFUNC(ApplyDeviceVideoSettingValue);
DEF_PFUNC(Deinit);
DEF_PFUNC(GetCaptureFrameRate);
DEF_PFUNC(GetCaptureFramesDropped);
DEF_PFUNC(GetDeviceFormat);
DEF_PFUNC(GetDeviceGUID);
DEF_PFUNC(GetDeviceName);
DEF_PFUNC(GetDeviceVideoSettingBoolean);
DEF_PFUNC(GetDeviceVideoSettingFloat);
DEF_PFUNC(GetDeviceVideoSettingInfo);
DEF_PFUNC(GetFormat);
DEF_PFUNC(GetFrameAsColor32);
DEF_PFUNC(GetFrameDurationHNS);
DEF_PFUNC(GetFrameFromBufferAtTime);
DEF_PFUNC(GetFramePixels);
DEF_PFUNC(GetFrameRate);
DEF_PFUNC(GetHeight);
DEF_PFUNC(GetLastFrame);
DEF_PFUNC(GetLastFrameBuffered);
DEF_PFUNC(GetLastFrameBufferedTime);
DEF_PFUNC(GetLastFrameUploaded);
DEF_PFUNC(GetModeFrameRates);
DEF_PFUNC(GetModeInfo);
DEF_PFUNC(GetNumDeviceVideoSettings);
DEF_PFUNC(GetNumDevices);
DEF_PFUNC(GetNumModes);
DEF_PFUNC(GetNumVideoInputs);
DEF_PFUNC(GetPluginVersion);
DEF_PFUNC(GetRenderEventFunc);
DEF_PFUNC(GetVideoInputName);
DEF_PFUNC(GetWidth);
DEF_PFUNC(HasDeviceConfigWindow);
DEF_PFUNC(Init);
DEF_PFUNC(IsDeviceConnected);
DEF_PFUNC(IsFrameTopDown);
DEF_PFUNC(IsNextFrameReadyForGrab);
DEF_PFUNC(Pause);
DEF_PFUNC(Play);
DEF_PFUNC(SetActive);
DEF_PFUNC(SetDeviceClockMode);
DEF_PFUNC(SetFrameBufferSize);
DEF_PFUNC(SetTexturePointer);
DEF_PFUNC(SetVideoInputByIndex);
DEF_PFUNC(ShowDeviceConfigWindow);
DEF_PFUNC(StartDevice);
DEF_PFUNC(Stop);
DEF_PFUNC(StopDevice);
//DEF_PFUNC(UnitySetGraphicsDevice);
DEF_PFUNC(UpdateDeviceVideoSettingValue);
DEF_PFUNC(UpdateDevicesConnected);
DEF_PFUNC(UpdateTextureGL);
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
    // 因为插件没用调用该接口，可以直接返回空
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
    if (GetUnityInterfaceGUID<IUnityGraphics>() == guid)
    {
        IUnityGraphics* pUnityGraphics = static_cast<IUnityGraphics*>(oldGetInterface(guid));
        if (oldGetRenderer == nullptr)
        {
            oldGetRenderer = pUnityGraphics->GetRenderer;
            pUnityGraphics->GetRenderer = MyGetRenderer; // 更换为自己的Renderer枚举
        }
        return pUnityGraphics;
    }
    else if (GetUnityInterfaceGUID<IUnityGraphicsD3D11>() == guid)
    {
        UnityGfxRenderer realRenderer = oldGetRenderer();

        // 如果Renderer是D3D12，则伪造一个D3D11的Renderer
        if (realRenderer == kUnityGfxRendererD3D12)
        {
            if (s_pMyD3D11Device == nullptr)
            {
                IUnityGraphicsD3D12v2* d3dv2 = static_cast<IUnityGraphicsD3D12v2*>(oldGetInterface(GetUnityInterfaceGUID<IUnityGraphicsD3D12v2>()));
                s_pMyD3D11Device = CreateD3D11Device(d3dv2->GetDevice());

                s_MyUnityGraphicsD3D11.GetDevice                = MyGetDevice; // 返回ID3D11Device*
                s_MyUnityGraphicsD3D11.TextureFromRenderBuffer  = MyTextureFromRenderBuffer; // 可以直接返回nullptr
                s_MyUnityGraphicsD3D11.TextureFromNativeTexture = MyTextureFromNativeTexture;
                s_MyUnityGraphicsD3D11.RTVFromRenderBuffer      = MyRTVFromRenderBuffer;
                s_MyUnityGraphicsD3D11.SRVFromNativeTexture     = MySRVFromNativeTexture;
            }
            return &s_MyUnityGraphicsD3D11;
        }
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

    // 不关心的接口直接跳过
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


template<class _InterfaceT> ID3D12CommandQueue* GetCommandQueue(IUnityInterfaces* unityInterfaces)
{
  _InterfaceT* pUnityGraphicsD3D12 = unityInterfaces->Get<_InterfaceT>( );
  if (pUnityGraphicsD3D12)
  {
    return pUnityGraphicsD3D12->GetCommandQueue( );
  }
  return nullptr;
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

    //g_pD3D12CommandQueue = GetCommandQueue<IUnityGraphicsD3D12v5>(unityInterfaces);
    //if (g_pD3D12CommandQueue)
    //{
    //  return;
    //}

    //g_pD3D12CommandQueue = GetCommandQueue<IUnityGraphicsD3D12v4>(unityInterfaces);
    //if (g_pD3D12CommandQueue)
    //{
    //  return;
    //}

    //g_pD3D12CommandQueue = GetCommandQueue<IUnityGraphicsD3D12>(unityInterfaces);
    //if (g_pD3D12CommandQueue)
    //{
    //  return;
    //}


    if (pfuncUnityPluginLoad)
    {
        pfuncUnityPluginLoad(unityInterfaces);
    }
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
		    s_pd3d11Device = d3d->GetDevice();
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
		        s_pd3d12Device = d3d->GetDevice();
            }
            else if (d3dv2)
            {
		        s_pd3d12Device = d3dv2->GetDevice();
            }
            else if (d3dv3)
            {
		        s_pd3d12Device = d3dv3->GetDevice();
            }
            else if (d3dv4)
            {
		        s_pd3d12Device = d3dv4->GetDevice();
            }
            else if (d3dv5)
            {
		        s_pd3d12Device = d3dv5->GetDevice();
            }
            else if (d3dv6)
            {
		        s_pd3d12Device = d3dv6->GetDevice();
            }
            else if (d3dv7)
            {
		        s_pd3d12Device = d3dv7->GetDevice();
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
  void DLL_API ApplyDeviceVideoSettingValue(int deviceIndex, int settingIndex, float currentValue, bool isAutomatic)
  {
    pfuncApplyDeviceVideoSettingValue(deviceIndex, settingIndex, currentValue, isAutomatic);
  }

  //Init
  bool DLL_API Init(bool supportInternalConversion)
  {
    return pfuncInit(supportInternalConversion);
  }

  //Deinit
  void DLL_API Deinit( )
  {
    pfuncDeinit( );
  }

  //GetCaptureFrameRate
  float DLL_API GetCaptureFrameRate(int deviceIndex)
  {
    return pfuncGetCaptureFrameRate(deviceIndex);
  }

  //GetCaptureFramesDropped
  unsigned int DLL_API GetCaptureFramesDropped(int deviceIndex)
  {
    return pfuncGetCaptureFramesDropped(deviceIndex);
  }

  //GetCurrentVideoInputIndex

  //GetDeviceFormat
  bool DLL_API GetDeviceFormat(int index, StringBuilder format)
  {
    return pfuncGetDeviceFormat(index, format);
  }

  //GetDeviceGUID
  bool DLL_API GetDeviceGUID(int index, StringBuilder nameBuffer, int nameBufferLength)
  {
    return pfuncGetDeviceGUID(index, nameBuffer, nameBufferLength);
  }

  //GetDeviceName
  bool DLL_API GetDeviceName(int index, StringBuilder nameBuffer, int nameBufferLength)
  {
    return pfuncGetDeviceName(index, nameBuffer, nameBufferLength);
  }

  //GetDeviceVideoSettingBoolean
  bool DLL_API GetDeviceVideoSettingBoolean(int deviceIndex, int settingIndex, bool* defaultValue, bool* currentValue, bool* isAutomatic)
  {
    return pfuncGetDeviceVideoSettingBoolean(deviceIndex, settingIndex, defaultValue, currentValue, isAutomatic);
  }

  //GetDeviceVideoSettingFloat
  bool DLL_API GetDeviceVideoSettingFloat(int deviceIndex, int settingIndex, float* defaultValue, float* currentValue, float* minValue, float* maxValue, bool* isAutomatic)
  {
    return pfuncGetDeviceVideoSettingFloat(deviceIndex, settingIndex, defaultValue, currentValue, minValue, maxValue, isAutomatic);
  }

  //GetDeviceVideoSettingInfo
  bool DLL_API GetDeviceVideoSettingInfo(int deviceIndex, int settingIndex, int* settingType, int* dataType, StringBuilder name, bool* canAutomatic)
  {
    return pfuncGetDeviceVideoSettingInfo(deviceIndex, settingIndex, settingType, dataType, name, canAutomatic);
  }

  //GetFormat
  int DLL_API GetFormat(int index)
  {
    return pfuncGetFormat(index);
  }

  //GetFrameAsColor32
  bool DLL_API GetFrameAsColor32(int index, INT_PTR bufferPtr, int bufferWidth, int bufferHeight)
  {
    return pfuncGetFrameAsColor32(index, bufferPtr, bufferWidth, bufferHeight);
  }

  //GetFrameDurationHNS
  long DLL_API GetFrameDurationHNS(int index)
  {
    return pfuncGetFrameDurationHNS(index);
  }

  //GetFrameFromBufferAtTime
  INT_PTR DLL_API GetFrameFromBufferAtTime(int deviceIndex, long time)
  {
    return pfuncGetFrameFromBufferAtTime(deviceIndex, time);
  }

  //GetFramePixels
  bool DLL_API GetFramePixels(int index, INT_PTR buffer, int bufferIndex, int bufferWidth, int bufferHeight)
  {
    return pfuncGetFramePixels(index, buffer, bufferIndex, bufferWidth, bufferHeight);
  }

  //GetFrameRate
  float DLL_API GetFrameRate(int index)
  {
    return pfuncGetFrameRate(index);
  }

  //GetHeight
  int DLL_API GetHeight(int index)
  {
    return pfuncGetHeight(index);
  }

  //GetLastFrame
  unsigned int DLL_API GetLastFrame(int index)
  {
    return pfuncGetLastFrame(index);
  }

  //GetLastFrameBuffered
  INT_PTR DLL_API GetLastFrameBuffered(int deviceIndex)
  {
    return pfuncGetLastFrameBuffered(deviceIndex);
  }

  //GetLastFrameBufferedTime
  long DLL_API GetLastFrameBufferedTime(int deviceIndex)
  {
    return pfuncGetLastFrameBufferedTime(deviceIndex);
  }

  //GetLastFrameUploaded
  int DLL_API GetLastFrameUploaded(int handle)
  {
    return pfuncGetLastFrameUploaded(handle);
  }

  //GetModeFrameRates
  bool DLL_API GetModeFrameRates(int deviceIndex, int modeIndex, int frameRateCount, float* frameRates)
  {
    return pfuncGetModeFrameRates(deviceIndex, modeIndex, frameRateCount, frameRates);
  }

  //GetModeInfo
  bool DLL_API GetModeInfo(int deviceIndex, int modeIndex, int* width, int* height, int* frameRateCount, int* frameRateIndex, StringBuilder format)
  {
    return pfuncGetModeInfo(deviceIndex, modeIndex, width, height, frameRateCount, frameRateIndex, format);
  }

  //GetNumDeviceVideoSettings
  int DLL_API GetNumDeviceVideoSettings(int deviceIndex)
  {
    return pfuncGetNumDeviceVideoSettings(deviceIndex);
  }

  //GetNumDevices
  int DLL_API GetNumDevices( )
  {
    return pfuncGetNumDevices( );
  }

  //GetNumModes
  int DLL_API GetNumModes(int index)
  {
    return pfuncGetNumModes(index);
  }

  //GetNumVideoInputs
  int DLL_API GetNumVideoInputs(int deviceIndex)
  {
    return pfuncGetNumVideoInputs(deviceIndex);
  }

  //GetPluginVersion
  INT_PTR DLL_API GetPluginVersion( )
  {
    return pfuncGetPluginVersion( );
  }

  //GetRenderEventFunc
  INT_PTR DLL_API GetRenderEventFunc( )
  {
    return pfuncGetRenderEventFunc( );
  }

  //GetVideoInputName
  bool DLL_API GetVideoInputName(int deviceIndex, int inputIndex, StringBuilder name)
  {
    return pfuncGetVideoInputName(deviceIndex, inputIndex, name);
  }

  //GetWidth
  int DLL_API GetWidth(int index)
  {
    return pfuncGetWidth(index);
  }

  //HasDeviceConfigWindow
  bool DLL_API HasDeviceConfigWindow(int index)
  {
    return pfuncHasDeviceConfigWindow(index);
  }

  //IsDeviceConnected
  bool DLL_API IsDeviceConnected(int index)
  {
    return pfuncIsDeviceConnected(index);
  }

  //IsFrameTopDown
  bool DLL_API IsFrameTopDown(int index)
  {
    return pfuncIsFrameTopDown(index);
  }

  //IsNextFrameReadyForGrab
  bool DLL_API IsNextFrameReadyForGrab(int index)
  {
    return pfuncIsNextFrameReadyForGrab(index);
  }

  //Pause
  void DLL_API Pause(int index)
  {
    return pfuncPause(index);
  }

  //Play
  bool DLL_API Play(int index)
  {
    return pfuncPlay(index);
  }

  //SetActive
  bool DLL_API SetActive(int index, bool active)
  {
    return pfuncSetActive(index, active);
  }

  //SetDeviceClockMode
  void DLL_API SetDeviceClockMode(int deviceIndex, bool useDefaultClock)
  {
    return pfuncSetDeviceClockMode(deviceIndex, useDefaultClock);
  }

  //SetFrameBufferSize
  void DLL_API SetFrameBufferSize(int deviceIndex, int read, int write)
  {
    return pfuncSetFrameBufferSize(deviceIndex, read, write);
  }

  //SetTexturePointer
  void DLL_API SetTexturePointer(int index, int bufferIndex, INT_PTR texturePtr)
  {
    return pfuncSetTexturePointer(index, bufferIndex, texturePtr);
  }

  //SetVideoInputByIndex
  void DLL_API SetVideoInputByIndex(int deviceIndex, int inputIndex)
  {
    return pfuncSetVideoInputByIndex(deviceIndex, inputIndex);
  }

  //ShowDeviceConfigWindow
  bool DLL_API ShowDeviceConfigWindow(int index)
  {
    return pfuncShowDeviceConfigWindow(index);
  }

  //StartDevice
  bool DLL_API StartDevice(int index, int modeIndex, int frameRateIndex, int videoInputIndex, bool preferPreviewPin)
  {
    return pfuncStartDevice(index, modeIndex, frameRateIndex, videoInputIndex, preferPreviewPin);
  }

  //Stop
  void DLL_API Stop(int index)
  {
    pfuncStop(index);
  }

  //StopDevice
  void DLL_API StopDevice(int index)
  {
    pfuncStopDevice(index);
  }

  //UpdateDevicesConnected
  bool DLL_API UpdateDevicesConnected( )
  {
    return pfuncUpdateDevicesConnected( );
  }

  //UpdateDeviceVideoSettingValue
  bool DLL_API UpdateDeviceVideoSettingValue(int deviceIndex, int settingIndex, float* currentValue, bool* isAutomatic)
  {
    return pfuncUpdateDeviceVideoSettingValue(deviceIndex, settingIndex, currentValue, isAutomatic);
  }

  //UpdateTextureGL
  bool DLL_API UpdateTextureGL(int index, int textureID)
  {
    return pfuncUpdateTextureGL(index, textureID);
  }

  ///////////////////////////////////////////////////////////////////

  //UnityRenderEvent
  void DLL_API UnityRenderEvent(int eventID)
  {
      NOT_IMPLEMENT;
      pfuncUnityRenderEvent(eventID);
  }


  //UnitySetGraphicsDevice
  void DLL_API UnitySetGraphicsDevice(void* device, int deviceType, int eventType)
  {
      //NOT_IMPLEMENT;
      if (deviceType == kUnityGfxRendererD3D12)
      {
          s_pd3d12Device = reinterpret_cast<ID3D12Device*>(device);
          if (device)
          {
              g_pd3d11DeviceFake = new IMyD3D11Device(s_pd3d12Device);
              g_pd3d11DeviceFake->AddRef();
          }
          else
          {
              SAFE_RELEASE(g_pd3d11DeviceFake);
          }
          pfuncUnitySetGraphicsDevice(g_pd3d11DeviceFake, kUnityGfxRendererD3D11, eventType);
      }
      else if (deviceType == kUnityGfxRendererD3D11)
      {
          pfuncUnitySetGraphicsDevice(device, deviceType, eventType);
      }
      else // 这么写是为了调试方便
      {
          pfuncUnitySetGraphicsDevice(device, deviceType, eventType);
      }
  }

  //void UNITY_INTERFACE_EXPORT UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces)
  //{
  //  g_pD3D12CommandQueue = GetCommandQueue<IUnityGraphicsD3D12v5>(unityInterfaces);
  //  if (g_pD3D12CommandQueue)
  //  {
  //    return;
  //  }

  //  g_pD3D12CommandQueue = GetCommandQueue<IUnityGraphicsD3D12v4>(unityInterfaces);
  //  if (g_pD3D12CommandQueue)
  //  {
  //    return;
  //  }

  //  g_pD3D12CommandQueue = GetCommandQueue<IUnityGraphicsD3D12>(unityInterfaces);
  //  if (g_pD3D12CommandQueue)
  //  {
  //    return;
  //  }
  //}

/////////////////////////////////////////////////////////////////////////////

void OnProcessAttach()
{
    if (hAVProLiveCamera == nullptr)
    {
        hAVProLiveCamera = LoadLibrary(L"AVProLiveCamera");
        if (hAVProLiveCamera)
        {
            LOCATE_API(UnityPluginLoad);
            LOCATE_API(UnityPluginUnload);
            LOCATE_API(UnityRenderEvent);
            LOCATE_API(UnitySetGraphicsDevice);

            LOCATE_API(ApplyDeviceVideoSettingValue);
            LOCATE_API(Deinit);
            LOCATE_API(GetCaptureFrameRate);
            LOCATE_API(GetCaptureFramesDropped);
            LOCATE_API(GetDeviceFormat);
            LOCATE_API(GetDeviceGUID);
            LOCATE_API(GetDeviceName);
            LOCATE_API(GetDeviceVideoSettingBoolean);
            LOCATE_API(GetDeviceVideoSettingFloat);
            LOCATE_API(GetDeviceVideoSettingInfo);
            LOCATE_API(GetFormat);
            LOCATE_API(GetFrameAsColor32);
            LOCATE_API(GetFrameDurationHNS);
            LOCATE_API(GetFrameFromBufferAtTime);
            LOCATE_API(GetFramePixels);
            LOCATE_API(GetFrameRate);
            LOCATE_API(GetHeight);
            LOCATE_API(GetLastFrame);
            LOCATE_API(GetLastFrameBuffered);
            LOCATE_API(GetLastFrameBufferedTime);
            LOCATE_API(GetLastFrameUploaded);
            LOCATE_API(GetModeFrameRates);
            LOCATE_API(GetModeInfo);
            LOCATE_API(GetNumDeviceVideoSettings);
            LOCATE_API(GetNumDevices);
            LOCATE_API(GetNumModes);
            LOCATE_API(GetNumVideoInputs);
            LOCATE_API(GetPluginVersion);
            LOCATE_API(GetRenderEventFunc);
            LOCATE_API(GetVideoInputName);
            LOCATE_API(GetWidth);
            LOCATE_API(HasDeviceConfigWindow);
            LOCATE_API(Init);
            LOCATE_API(IsDeviceConnected);
            LOCATE_API(IsFrameTopDown);
            LOCATE_API(IsNextFrameReadyForGrab);
            LOCATE_API(Pause);
            LOCATE_API(Play);
            LOCATE_API(SetActive);
            LOCATE_API(SetDeviceClockMode);
            LOCATE_API(SetFrameBufferSize);
            LOCATE_API(SetTexturePointer);
            LOCATE_API(SetVideoInputByIndex);
            LOCATE_API(ShowDeviceConfigWindow);
            LOCATE_API(StartDevice);
            LOCATE_API(Stop);
            LOCATE_API(StopDevice);
            LOCATE_API(UpdateDeviceVideoSettingValue);
            LOCATE_API(UpdateDevicesConnected);
            LOCATE_API(UpdateTextureGL);
        }
    }
}

void OnProcessDetach()
{
    if (hAVProLiveCamera)
    {
        FreeLibrary(hAVProLiveCamera);
        hAVProLiveCamera = nullptr;
    }
}
