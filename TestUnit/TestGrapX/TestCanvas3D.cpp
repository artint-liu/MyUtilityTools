#include <GrapX.h>
#include <GrapX/GResource.H>
#include <GrapX/GXGraphics.H>
#include <GrapX/GXRenderTarget.h>
#include <GrapX/GXCanvas.h>
#include <GrapX/GXCanvas3D.h>
#include <GrapX/GTexture.h>
#include <GrapX/GShader.h>
#include <GrapX/GRegion.h>

#include <clTree.h>
#include <clTransform.h>
#include <GrapX/gvNode.h>
#include <GrapX/GCamera.h>
#include <GrapX/GVScene.h>
#include "TestCanvas3D.h"
using namespace GrapX;

CTestCanvas3D::CTestCanvas3D(GrapX::Graphics* pGraphics)
  : m_pGraphics(pGraphics)
{
  pGraphics->CreateCanvas3D(&m_pCanvas3D, NULL);

  GCamera_Trackball::Create(&m_pCamera);
  m_pCamera->Initialize(float3(5.5f, 3.0f, 5.5f), float3(0.0f, -0.2f, 0.0f), 1.0f, CL_AGNLE2RAD(20.0f));
  GVScene::Create(m_pGraphics, NULL, _CLTEXT("shaders\\Diffuse_shader.txt"), &m_pScene);
  m_pCanvas3D->SetCamera(m_pCamera);
}

CTestCanvas3D::~CTestCanvas3D()
{
  SAFE_RELEASE(m_pScene);
  SAFE_RELEASE(m_pCamera);
  SAFE_RELEASE(m_pCanvas3D);
}

void CTestCanvas3D::Render()
{
  m_pScene->RenderAll(m_pCanvas3D, GVRT_Normal);
}
