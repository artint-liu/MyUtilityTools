
#pragma once
#include <dwrite.h>
#include <wrl/client.h>
#include <string>

// 字体管理器：从 exe 同级 fonts\*.ttf 加载自定义字体。
//  - DWrite：构建自定义 FontCollection（不依赖系统字体），供 CreateTextFormat 使用。
//  - GDI   ：AddFontResourceExW(FR_PRIVATE) 进程私有加载，供 CreateFontW 使用。
//
// 用法：
//   FontManager::Instance().LoadFonts();            // 幂等，首次真正加载
//   IDWriteFontCollection* c = FontManager::Instance().GetDWriteCollection();
//   const std::wstring& body = FontManager::Instance().GetBodyFamily();
//   const std::wstring& code = FontManager::Instance().GetCodeFamily();
//
// 若 fonts 目录不存在或为空，回退系统字体（Segoe UI / Consolas），
// GetDWriteCollection() 返回 nullptr，调用方应传 nullptr 给 CreateTextFormat（系统字体集）。
class FontManager {
public:
    static FontManager& Instance();

    // 幂等。内部用 GetModuleFileName 定位 exe 目录的 fonts 子目录。
    // 需先 CoInitialize（程序入口已做）。
    void LoadFonts();

    // 自定义字体集；未加载或 fonts 为空时返回 nullptr。
    IDWriteFontCollection* GetDWriteCollection() const { return m_dwriteCollection.Get(); }

    // 正文字体 family name（由 ini 中 body 字段按文件名解析；否则系统默认）。
    const std::wstring& GetBodyFamily() const { return m_bodyFamily; }
    // 代码字体 family name（由 ini 中 code 字段按文件名解析；否则系统 Consolas）。
    const std::wstring& GetCodeFamily() const { return m_codeFamily; }

    // 目录字体 family name（由 ini 中 toc 字段解析；未配置或匹配失败则回退正文字体）。
    const std::wstring& GetTocFamily() const { return m_tocFamily; }
    // 与 GetTocFamily 相同，但去掉 family name 前导 '.'，供 GDI CreateFontW 使用
    // （DWrite 返回的 name 可能以 '.' 开头，GDI 无法匹配，如 ".PingFang SC"）。
    const std::wstring& GetTocFamilyGdi() const { return m_tocFamilyGdi; }
    // 目录字号数值（默认 15，对应 CreateFontW 中 N*60 的输入；越大字号越大）。
    int GetTocSizePt() const { return m_tocSizePt > 0 ? m_tocSizePt : 15; }
    // 目录行高数值（默认 40，数值越大行间距越疏）。
    int GetTocLineSpacing() const { return m_tocLineSpacing > 0 ? m_tocLineSpacing : 40; }

    bool HasCustomFonts() const { return m_hasCustomFonts; }

private:
    FontManager() = default;
    void LoadConfig(const std::wstring& exeDir);
    bool m_loaded = false;
    bool m_hasCustomFonts = false;
    std::wstring m_cfgBody;   // 配置文件指定的正文字体（文件名或 family name，可空）
    std::wstring m_cfgCode;    // 配置文件指定的代码字体（文件名或 family name，可空）
    std::wstring m_cfgToc;     // 配置文件指定的目录字体（可空，回退 body）
    Microsoft::WRL::ComPtr<IDWriteFactory> m_factory;
    Microsoft::WRL::ComPtr<IDWriteFontCollection> m_dwriteCollection;
    std::wstring m_bodyFamily;
    std::wstring m_codeFamily;
    std::wstring m_tocFamily;   // 目录字体（回退 body）
    std::wstring m_tocFamilyGdi; // 目录字体 GDI 版（去前导 '.'）
    int m_tocSizePt = 0;        // 目录字号（0=默认15）
    int m_tocLineSpacing = 0;   // 目录行高数值（0=默认40）
};
