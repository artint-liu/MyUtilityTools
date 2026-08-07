
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

    bool HasCustomFonts() const { return m_hasCustomFonts; }

private:
    FontManager() = default;
    void LoadConfig(const std::wstring& exeDir);
    bool m_loaded = false;
    bool m_hasCustomFonts = false;
    std::wstring m_cfgBody;   // 配置文件指定的正文字体（文件名或 family name，可空）
    std::wstring m_cfgCode;    // 配置文件指定的代码字体（文件名或 family name，可空）
    Microsoft::WRL::ComPtr<IDWriteFactory> m_factory;
    Microsoft::WRL::ComPtr<IDWriteFontCollection> m_dwriteCollection;
    std::wstring m_bodyFamily;
    std::wstring m_codeFamily;
};
