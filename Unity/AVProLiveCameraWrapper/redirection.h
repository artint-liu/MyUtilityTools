#pragma once

#define StringBuilder void*

extern "C" {
    void DLL_API UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces);// Unity 插件加载事件
    void DLL_API UNITY_INTERFACE_API UnityPluginUnload();    // Unity 插件卸载事件
    
    void DLL_API UNITY_INTERFACE_API UnitySetGraphicsDevice(void* device, int deviceType, int eventType);
    void DLL_API UNITY_INTERFACE_API UnityRenderEvent(int eventID);


  //ApplyDeviceVideoSettingValue
  void DLL_API ApplyDeviceVideoSettingValue(int deviceIndex, int settingIndex, float currentValue, bool isAutomatic);

  //Deinit
  void DLL_API Deinit( );

  //GetCaptureFrameRate
  float DLL_API GetCaptureFrameRate(int deviceIndex);

  //GetCaptureFramesDropped
  unsigned int DLL_API GetCaptureFramesDropped(int deviceIndex);

  //GetCurrentVideoInputIndex

  //GetDeviceFormat
  bool DLL_API GetDeviceFormat(int index, StringBuilder format);

  //GetDeviceGUID
  bool DLL_API GetDeviceGUID(int index, StringBuilder nameBuffer, int nameBufferLength);

  //GetDeviceName
  bool DLL_API GetDeviceName(int index, StringBuilder nameBuffer, int nameBufferLength);

  //GetDeviceVideoSettingBoolean
  bool DLL_API GetDeviceVideoSettingBoolean(int deviceIndex, int settingIndex, bool* defaultValue, bool* currentValue, bool* isAutomatic);

  //GetDeviceVideoSettingFloat
  bool DLL_API GetDeviceVideoSettingFloat(int deviceIndex, int settingIndex, float* defaultValue, float* currentValue, float* minValue, float* maxValue, bool* isAutomatic);

  //GetDeviceVideoSettingInfo
  bool DLL_API GetDeviceVideoSettingInfo(int deviceIndex, int settingIndex, int* settingType, int* dataType, StringBuilder name, bool* canAutomatic);

  //GetFormat
  int DLL_API GetFormat(int index);

  //GetFrameAsColor32
  bool DLL_API GetFrameAsColor32(int index, INT_PTR bufferPtr, int bufferWidth, int bufferHeight);

  //GetFrameDurationHNS
  long DLL_API GetFrameDurationHNS(int index);

  //GetFrameFromBufferAtTime
  INT_PTR DLL_API GetFrameFromBufferAtTime(int deviceIndex, long time);

  //GetFramePixels
  bool DLL_API GetFramePixels(int index, INT_PTR buffer, int bufferIndex, int bufferWidth, int bufferHeight);

  //GetFrameRate
  float DLL_API GetFrameRate(int index);

  //GetHeight
  int DLL_API GetHeight(int index);

  //GetLastFrame
  unsigned int DLL_API GetLastFrame(int index);

  //GetLastFrameBuffered
  INT_PTR DLL_API GetLastFrameBuffered(int deviceIndex);

  //GetLastFrameBufferedTime
  long DLL_API GetLastFrameBufferedTime(int deviceIndex);

  //GetLastFrameUploaded
  int DLL_API GetLastFrameUploaded(int handle);

  //GetModeFrameRates
  bool DLL_API GetModeFrameRates(int deviceIndex, int modeIndex, int frameRateCount, float* frameRates);

  //GetModeInfo
  bool DLL_API GetModeInfo(int deviceIndex, int modeIndex, int* width, int* height, int* frameRateCount, int* frameRateIndex, StringBuilder format);

  //GetNumDeviceVideoSettings
  int DLL_API GetNumDeviceVideoSettings(int deviceIndex);

  //GetNumDevices
  int DLL_API GetNumDevices( );

  //GetNumModes
  int DLL_API GetNumModes(int index);

  //GetNumVideoInputs
  int DLL_API GetNumVideoInputs(int deviceIndex);

  //GetPluginVersion
  INT_PTR DLL_API GetPluginVersion( );

  //GetRenderEventFunc
  INT_PTR DLL_API GetRenderEventFunc( );

  //GetVideoInputName
  bool DLL_API GetVideoInputName(int deviceIndex, int inputIndex, StringBuilder name);

  //GetWidth
  int DLL_API GetWidth(int index);

  //HasDeviceConfigWindow
  bool DLL_API HasDeviceConfigWindow(int index);

  //Init
  bool DLL_API Init(bool supportInternalConversion);

  //IsDeviceConnected
  bool DLL_API IsDeviceConnected(int index);

  //IsFrameTopDown
  bool DLL_API IsFrameTopDown(int index);

  //IsNextFrameReadyForGrab
  bool DLL_API IsNextFrameReadyForGrab(int index);

  //Pause
  void DLL_API Pause(int index);

  //Play
  bool DLL_API Play(int index);

  //SetActive
  bool DLL_API SetActive(int index, bool active);

  //SetDeviceClockMode
  void DLL_API SetDeviceClockMode(int deviceIndex, bool useDefaultClock);

  //SetFrameBufferSize
  void DLL_API SetFrameBufferSize(int deviceIndex, int read, int write);

  //SetTexturePointer
  void DLL_API SetTexturePointer(int index, int bufferIndex, INT_PTR texturePtr);

  //SetVideoInputByIndex
  void DLL_API SetVideoInputByIndex(int deviceIndex, int inputIndex);

  //ShowDeviceConfigWindow
  bool DLL_API ShowDeviceConfigWindow(int index);

  //StartDevice
  bool DLL_API StartDevice(int index, int modeIndex, int frameRateIndex, int videoInputIndex, bool preferPreviewPin);

  //Stop
  void DLL_API Stop(int index);

  //StopDevice
  void DLL_API StopDevice(int index);

  //UpdateDevicesConnected
  bool DLL_API UpdateDevicesConnected( );

  //UpdateDeviceVideoSettingValue
  bool DLL_API UpdateDeviceVideoSettingValue(int deviceIndex, int settingIndex, float* currentValue, bool* isAutomatic);

  //UpdateTextureGL
  bool DLL_API UpdateTextureGL(int index, int textureID);}