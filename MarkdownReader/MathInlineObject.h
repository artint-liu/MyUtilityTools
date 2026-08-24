#pragma once
#include <dwrite.h>
#include <d2d1.h>
#include <wrl/client.h>
#include <string>
#include "MathText.h"
#include "TextRenderer.h"

// 数学公式的二维内联对象：把分式（上下结构）与大运算符（放大符号 +
// 上下标排右上/右下）作为图形嵌入 DWrite 文本布局。
//
// 工作方式：实现 IDWriteInlineObject，由渲染端经 IDWriteTextLayout::
// SetInlineObject 挂到公式文本的对应 range 上：
//  - 排版测量：GetMetrics 提供宽度/高度/基线，行高自动随对象撑高
//    （大积分号可占 2~3 行高度，分式整体约 1.6 行高）
//  - 绘制回调：IDWriteTextLayout::Draw -> CustomTextRenderer::DrawInlineObject
//    -> 本类 Draw，clientDrawingContext 为 RenderContext*（含 D2D 目标与画笔），
//    分子/分母/上下标用 ID2D1RenderTarget::DrawTextLayout 绘制，分数线用 DrawLine。
class MathInlineObject : public IDWriteInlineObject {
public:
    // 按 deco 创建对象；fmt 提供字号与字体族（含字体集），italic 为公式整体斜体。
    // 创建失败返回空（调用方跳过该 deco，文本按占位字符显示）。
    static Microsoft::WRL::ComPtr<IDWriteInlineObject> Create(
        const MathDeco& deco, IDWriteFactory* dwrite,
        IDWriteTextFormat* fmt, bool italic);

    // ---- IUnknown ----
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&m_ref); }
    ULONG STDMETHODCALLTYPE Release() override;

    // ---- IDWriteInlineObject ----
    HRESULT STDMETHODCALLTYPE GetMetrics(DWRITE_INLINE_OBJECT_METRICS* metrics) override;
    HRESULT STDMETHODCALLTYPE GetOverhangMetrics(DWRITE_OVERHANG_METRICS* overhangs) override;
    HRESULT STDMETHODCALLTYPE GetBreakConditions(DWRITE_BREAK_CONDITION* breakConditionBefore,
                                                 DWRITE_BREAK_CONDITION* breakConditionAfter) override;
    HRESULT STDMETHODCALLTYPE Draw(void* clientDrawingContext, IDWriteTextRenderer* renderer,
        FLOAT originX, FLOAT originY, BOOL isSideways, BOOL isRightToLeft,
        IUnknown* clientDrawingEffect) override;

private:
    MathInlineObject() = default;
    bool Init(const MathDeco& deco, IDWriteFactory* dwrite,
              IDWriteTextFormat* fmt, bool italic);

    ULONG m_ref = 1;
    bool m_isFrac = true;
    bool m_isScript = false;           // 真上标/下标（上下标无法映射为 Unicode 时）
    float m_width = 0, m_height = 0, m_baseline = 0;   // metrics（DIP）

    // 分式布局
    Microsoft::WRL::ComPtr<IDWriteTextLayout> m_top, m_bottom;  // 分子/分母
    float m_topW = 0, m_botW = 0;      // 分子/分母宽
    float m_lineY = 0;                 // 分数线相对对象顶的 y
    float m_botTop = 0;                // 分母顶部相对对象顶的 y

    // 大运算符 / 真上下标布局
    Microsoft::WRL::ComPtr<IDWriteTextLayout> m_sym, m_sup, m_sub;  // 符号/上标/下标
    float m_symTop = 0;                // 符号顶部相对对象顶的 y
    float m_scriptX = 0;               // 上下标 x（符号右侧；Script 类型为 0）
    float m_supTop = 0, m_subTop = 0;  // 上下标顶部相对对象顶的 y
};
