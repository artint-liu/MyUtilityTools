#pragma once

extern "C" {
    void DLL_API UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces);// Unity 插件加载事件
    void DLL_API UNITY_INTERFACE_API UnityPluginUnload();    // Unity 插件卸载事件

    uint32_t DLL_API UNITY_INTERFACE_API GetBackBufferHeight();
    uint32_t DLL_API UNITY_INTERFACE_API GetBackBufferWidth();
    uint32_t DLL_API UNITY_INTERFACE_API GetPresentFlags();
    INT_PTR DLL_API UNITY_INTERFACE_API GetRenderEventFunc();
    INT_PTR DLL_API UNITY_INTERFACE_API GetRenderTexture();
    uint32_t DLL_API UNITY_INTERFACE_API GetSyncInterval();
    bool DLL_API UNITY_INTERFACE_API IsSwapChainAvailable();
    void DLL_API UNITY_INTERFACE_API SetMeshBuffersFromUnity(INT_PTR vertexBuffer, int vertexCount, INT_PTR sourceVertices, INT_PTR sourceNormals, INT_PTR sourceUVs);
    void DLL_API UNITY_INTERFACE_API SetRenderTexture(INT_PTR rb);
    void DLL_API UNITY_INTERFACE_API SetTextureFromUnity(INT_PTR texture, int w, int h);
    void DLL_API UNITY_INTERFACE_API SetTimeFromUnity(float t);
}