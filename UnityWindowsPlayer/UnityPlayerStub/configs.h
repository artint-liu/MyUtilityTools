#ifndef _MAIN_CONFIGURE_H_
#define _MAIN_CONFIGURE_H_

#define LOG_HOOK_API
#define LOG_HOOK_D3D_API
//#define LOG_HOOK_D3DCONTEXT
//#define TRACE_OBJ_PTR // ����һ��ָ������
//#define DUMP_DYNAMIC_MESH // ������̬ģ�ͣ�һ����skinned mesh�Ķ���֡�����ӣ�UI��
//#define DUMP_MyID3D11Device_CreateShaderResourceView
//#define DUMP_SHADER

//#define DUMP_MyID3D11Device_CreateRenderTargetView

namespace Config
{
  const BOOL bSwapYZ = FALSE;
  const BOOL bFlipX = TRUE;
  const BOOL bFlipTexcoord_V = TRUE;
}

#endif // _MAIN_CONFIGURE_H_