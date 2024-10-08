#include <tchar.h>
#include <GrapX.h>
#include <GXApp.h>
#include <GrapX/GResource.h>
#include <GrapX/GXGraphics.h>
#include <GrapX/GXRenderTarget.h>
#include <GrapX/GRegion.h>
#include <GrapX/GTexture.h>
#include <GrapX/GXFont.h>
#include <GrapX/GXCanvas.h>
#include <GrapX/GXCanvas3D.h>
#include <GrapX/gxDevice.h>
//#include "Resource.h"
#include <clPathFile.H>

#include "AppMain.h"
#include <clTree.h>
#include <clTransform.h>
#include <GrapX/gvNode.h>
#include <GrapX/GVScene.h>
#include <GrapX/GCamera.h>
#include "TestCanvas3D.h"
using namespace std;

void TestBasic_Normal(GrapX::Graphics* pGraphics);

class MyGraphicsTest : public GXApp
{
private:
  GXFont* m_pFont;
  CTestCanvas3D* m_p3D;
public:
  MyGraphicsTest()
    : m_pFont(NULL)
  {
  }

  virtual GXHRESULT OnCreate()
  {
    TestShader(m_pGraphics);
    TestPrimitive(m_pGraphics);
    TestTexture(m_pGraphics);
    TestRenderTarget(m_pGraphics);
    TestCanvas(m_pGraphics);
    m_p3D = new CTestCanvas3D(m_pGraphics);
    m_pFont = m_pGraphics->CreateFont(0, 20, "fonts/wqy-microhei.ttc");
    return GXApp::OnCreate();
  }

  virtual GXHRESULT OnDestroy()
  {
    SAFE_DELETE(m_p3D);
    SAFE_RELEASE(m_pFont);
    return GXApp::OnDestroy();
  }

  virtual GXHRESULT ActionBegin(GXAPPACTIONINFO* pActionInfo)
  {
    return GXApp::ActionBegin(pActionInfo);
  }

  virtual GXHRESULT ActionMove(GXAPPACTIONINFO* pActionInfo)
  {
    return GXApp::ActionMove(pActionInfo);
  }

  virtual GXHRESULT ActionEnd(GXAPPACTIONINFO* pActionInfo)
  {
    return GXApp::ActionEnd(pActionInfo);
  }

  virtual GXHRESULT KeyMessage(GXAPPKEYINFO* pKeyInfo)
  {
    return GXApp::KeyMessage(pKeyInfo);
  }

  
  virtual GXHRESULT Render();
};


GXHRESULT MyGraphicsTest::Render()
{
  if(GXSUCCEEDED(m_pGraphics->Begin()))
  {
    GrapX::Canvas* pCanvas = m_pGraphics->LockCanvas(NULL, NULL, NULL);
    clStringW str = _CLTEXT("测试GrapX接口");
    GXSIZE sDimension;
    GXRECT rect;
    pCanvas->GetTargetDimension(&sDimension);
    rect.set(0, 0, sDimension.cx, sDimension.cy);

    pCanvas->Clear(0xffffffff);
    pCanvas->DrawTextW(m_pFont, str, str.GetLength(), &rect, GXDT_CENTER | GXDT_VCENTER | GXDT_SINGLELINE, 0xff000000);
    if((clstd::GetTime() % 500) < 200)
    {
      pCanvas->FillRectangle(&rect, 0xffffffff);
    }

    //TestBasic(m_pGraphics, pCanvas);
    m_p3D->Render();

    pCanvas->SetCompositingMode(GrapX::CompositingMode_InvertTarget);
    rect.Inflate(-50, -50);
    pCanvas->FillRectangle(&rect, 0xffffffff);
    pCanvas->Flush();
    rect.Inflate(-50, -50);
    pCanvas->FillRectangle(&rect, 0xffffffff);


    SAFE_RELEASE(pCanvas);
    m_pGraphics->End();
    m_pGraphics->Present();
  }
  return GX_OK;
}

#if defined(_WIN32) || defined(_WINDOWS)
int APIENTRY _tWinMain(HINSTANCE hInstance,
  HINSTANCE hPrevInstance,
  LPTSTR    lpCmdLine,
  int       nCmdShow)
#else
int main(int argc, char *argv[])
#endif // #if defined(_WIN32) || defined(_WINDOWS)
{
  clpathfile::LocalWorkingDirW(_CLTEXT(".."));

  GXBYTE AppStruct[BYTE_ALIGN_4(sizeof(MyGraphicsTest))];
  MyGraphicsTest* appTest = new(AppStruct) MyGraphicsTest;
  GXAPP_DESC sAppDesc = { 0 };
  sAppDesc.cbSize = sizeof(GXAPP_DESC);
  sAppDesc.lpName = _CLTEXT("Test Graphics Interface");
  sAppDesc.nWidth = 512;
  sAppDesc.nHeight = 512;
  sAppDesc.dwStyle = GXADS_SIZABLE;
  sAppDesc.pParameter = NULL;

  if(MOUICreatePlatformSelectedDlg(hInstance, &sAppDesc))
  {
    appTest->Go(&sAppDesc);
  }
  return 0;
}