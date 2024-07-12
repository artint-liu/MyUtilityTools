#ifndef _TEST_CANVAS3D_H_
#define _TEST_CANVAS3D_H_

class CTestCanvas3D
{
protected:
  GrapX::Graphics* m_pGraphics;
  GrapX::Canvas3D* m_pCanvas3D;
  GCamera_Trackball* m_pCamera = NULL;
  GVScene* m_pScene;

public:
  CTestCanvas3D(GrapX::Graphics* pGraphics);
  virtual ~CTestCanvas3D();

  void Render();
};

#endif // _TEST_CANVAS3D_H_