#include <max.h>
#include <iparamm2.h>
#include <functional>
//#include "../GetCOREInterface.h"
#include "vertexpaint.h"


// 1. 创建自定义拾取模式类
class ModelPickMode : public PickModeCallback
{
public:
    //ModelPickMode(IObjParam* ip, std::function<void(INode*)> callback)
    //  : m_ip(ip), m_callback(callback)
    //{
    //}

    // 鼠标悬停测试
    BOOL HitTest(IObjParam* ip, HWND hWnd, ViewExp* vpt, IPoint2 m, int flags) override
    {
        INode* node = ip->PickNode(hWnd, m);
        return node ? TRUE : FALSE;
    }

    // 鼠标点击确认选择
    BOOL Pick(IObjParam* ip, ViewExp* vpt) override
    {
        INode* node = vpt->GetClosestHit();
        if (node)
        {
            //m_pVertexPaint.bi
            // 找到有效节点时触发回调
            //if (m_callback) m_callback(node);
            // 退出拾取模式
            //ip->PopCommandMode();
            //ip->DeleteMode(this);
            if (m_hDlg != NULL && m_ctrlID > 0)
            {
                m_pVertexPaint->SetReferenceNode(node);
                //SendMessage(m_hDlg, WM_UPDATENAME, m_ctrlID, (LPARAM)node->GetName());
            }
            return TRUE;
        }
        return FALSE;
    }

    // 取消选择
    void ExitMode(IObjParam* ip) override
    {
        //if (m_callback) m_callback(nullptr);
        //delete this;
    }

    void Setup(VertexPaint* pModifier, HWND hDlg, UINT id)
    {
        m_pVertexPaint = pModifier;
        m_hDlg = hDlg;
        m_ctrlID = id;
    }

private:
    VertexPaint* m_pVertexPaint = nullptr;
    HWND m_hDlg = NULL;
    UINT m_ctrlID = 0;
    //IObjParam* m_ip;
    //std::function<void(INode*)> m_callback;
};

// 2. 创建命令类处理按钮点击
//class SelectModelCmd : public CommandMode {
//public:
//  void Execute() {
//    IObjParam* ip = GetCOREInterface();
//
//    // 启动拾取模式
//    ModelPickMode* picker = new ModelPickMode(
//      ip,
//      [](INode* node) {
//        if (node) {
//          // 处理选择结果 (示例：打印名称)
//          TCHAR buf[256];
//          _stprintf(buf, _T("已选择: %s"), node->GetName());
//          MessageBox(GetActiveWindow(), buf, _T("选择结果"), MB_OK);
//        }
//      }
//    );
//
//    ip->PushCommandMode(picker);
//  }
//};
//
//// 3. 在工具面板创建按钮 (UtilityObj 示例)
//class MyUtility : public UtilityObj {
//public:
//  // 界面创建
//  void BeginEditParams(Interface* ip, IUtil* iu) override {
//    IPoint2 pos{ 10, 10 };
//
//    // 创建按钮
//    m_btnSelect = GetCOREInterface()->GetIGeneral()->CreateButton(
//      m_panel,
//      _T("选择模型"),
//      100,
//      22,
//      pos,
//      TRUE,
//      IDC_SELECT_BTN
//    );
//  }
//
//  // 按钮点击处理
//  INT_PTR DlgProc(TimeValue t, IParamMap2* map, HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) override {
//    switch (msg) {
//    case WM_COMMAND:
//      if (LOWORD(wParam) == IDC_SELECT_BTN) {
//        // 触发选择命令
//        static SelectModelCmd cmd;
//        cmd.Execute();
//        return TRUE;
//      }
//      break;
//    }
//    return FALSE;
//  }
//
//private:
//  IButton* m_btnSelect = nullptr;
//  HWND m_panel = nullptr;
//};
//
//// 4. 插件入口
//class MyPlugin : public UtilityObj {
//public:
//  void Start() {
//    GetCOREInterface()->AddUtility(&m_util);
//  }
//
//private:
//  MyUtility m_util;
//};
//
//static MyPlugin g_myPlugin;