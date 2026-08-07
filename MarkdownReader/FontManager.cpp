#include "FontManager.h"
#include <dwrite_2.h>
#include <dwrite_3.h>
#include <windows.h>
#include <fileapi.h>
#include <vector>
#include <fstream>
#include <string>
#include <utility>

using Microsoft::WRL::ComPtr;

FontManager& FontManager::Instance() {
    static FontManager inst;
    return inst;
}

static std::wstring GetExeDir() {
    wchar_t exe[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    wchar_t* slash = wcsrchr(exe, L'\\');
    if (slash) *slash = 0;
    return exe;
}

// 取 IDWriteFontFamily 的 family name（优先 en-us，否则首个）。
static std::wstring GetFamilyName(IDWriteFontFamily* family) {
    ComPtr<IDWriteLocalizedStrings> names;
    if (FAILED(family->GetFamilyNames(&names)) || !names) return L"";
    UINT32 idx = 0;
    BOOL exists = FALSE;
    names->FindLocaleName(L"en-us", &idx, &exists);
    if (!exists) idx = 0;
    UINT32 len = 0;
    names->GetStringLength(idx, &len);
    if (len == 0 || len >= 256) return L"";
    std::wstring buf(len, L'\0');
    names->GetString(idx, &buf[0], len + 1);
    return buf;
}

static bool IsFamilyMonospaced(IDWriteFontFamily* family) {
    ComPtr<IDWriteFont> font;
    if (FAILED(family->GetFirstMatchingFont(
            DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
            DWRITE_FONT_STYLE_NORMAL, &font)) || !font) {
        return false;
    }
    ComPtr<IDWriteFont1> font1;
    font.As(&font1);
    return font1 && font1->IsMonospacedFont();
}

// 收集目录下所有 .ttf / .otf 文件。
static void CollectFontFiles(const std::wstring& dir, std::vector<std::wstring>& files) {
    std::wstring pattern = dir + L"\\*";
    WIN32_FIND_DATAW fd{};
    HANDLE hFind = FindFirstFileW(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring name = fd.cFileName;
        size_t dot = name.find_last_of(L'.');
        if (dot == std::wstring::npos) continue;
        std::wstring ext = name.substr(dot);
        for (auto& c : ext) c = (wchar_t)towlower(c);
        if (ext == L".ttf" || ext == L".otf") {
            files.push_back(dir + L"\\" + fd.cFileName);
        }
    } while (FindNextFileW(hFind, &fd));
    FindClose(hFind);
}

// 读简单 key=value 配置文件（UTF-8）。支持 # 和 ; 注释。无第三方库。
// 文件：exe 同级 MarkdownReader.ini
//   body = SF-Pro.ttf      （推荐：字体文件名，大小写不敏感，可省略扩展名）
//   code = SFMono-Regular.otf
// 旧式 family name（如 "SF Pro"）仍向后兼容。
void FontManager::LoadConfig(const std::wstring& exeDir) {
    std::wstring cfgPath = exeDir + L"\\MarkdownReader.ini";
    std::ifstream fin(cfgPath);
    if (!fin) return;
    std::string line;
    while (std::getline(fin, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t s = line.find_first_not_of(" \t");
        if (s == std::string::npos) continue;           // 空行
        if (line[s] == '#' || line[s] == ';') continue; // 注释
        size_t eq = line.find('=', s);
        if (eq == std::string::npos) continue;
        std::string key = line.substr(s, eq - s);
        std::string val = line.substr(eq + 1);
        // trim
        size_t ke = key.find_last_not_of(" \t");
        if (ke != std::string::npos) key = key.substr(0, ke + 1);
        size_t vs = val.find_first_not_of(" \t");
        size_t ve = val.find_last_not_of(" \t");
        std::wstring wval;
        if (vs != std::string::npos) {
            val = val.substr(vs, ve - vs + 1);
            int n = MultiByteToWideChar(CP_UTF8, 0, val.c_str(), (int)val.size(), nullptr, 0);
            wval.resize(n);
            MultiByteToWideChar(CP_UTF8, 0, val.c_str(), (int)val.size(), &wval[0], n);
        }
        if (key == "body") m_cfgBody = wval;
        else if (key == "code") m_cfgCode = wval;
    }
}

void FontManager::LoadFonts() {
    if (m_loaded) return;
    m_loaded = true;
    m_bodyFamily = L"Segoe UI";
    m_codeFamily = L"Consolas";

    if (!m_factory) {
        DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(m_factory.GetAddressOf()));
    }
    if (!m_factory) return;

    std::wstring exeDir = GetExeDir();
    LoadConfig(exeDir);

    std::wstring fontsDir = exeDir + L"\\fonts";
    std::vector<std::wstring> files;
    CollectFontFiles(fontsDir, files);
    if (files.empty()) return;

    // ---- DWrite：构建自定义 FontSet / FontCollection ----
    ComPtr<IDWriteFactory5> f5;
    m_factory->QueryInterface(__uuidof(IDWriteFactory5), reinterpret_cast<void**>(f5.GetAddressOf()));
    if (f5) {
        ComPtr<IDWriteFontSetBuilder1> builder;
        if (SUCCEEDED(f5->CreateFontSetBuilder(&builder)) && builder) {
            for (const auto& path : files) {
                ComPtr<IDWriteFontFile> fontFile;
                if (SUCCEEDED(m_factory->CreateFontFileReference(path.c_str(), nullptr, &fontFile)) && fontFile) {
                    builder->AddFontFile(fontFile.Get());  // 忽略单文件失败
                }
            }
            ComPtr<IDWriteFontSet> fontSet;
            if (SUCCEEDED(builder->CreateFontSet(&fontSet)) && fontSet) {
                ComPtr<IDWriteFontCollection1> col1;
                if (SUCCEEDED(f5->CreateFontCollectionFromFontSet(
                        fontSet.Get(), col1.GetAddressOf())) && col1) {
                    m_dwriteCollection = col1;
                }
            }
        }
    }

    if (m_dwriteCollection) {
        bool bodySet = false, codeSet = false;

        // 遍历 collection 的 family 建立 文件名 -> family name 映射。
        // family name 直接取自 collection（GetFamilyName），保证与 CreateTextFormat 用的 name 一致；
        // 文件名通过 IDWriteFontFace::GetFiles + GetReferenceKey 获取（key 即文件路径，手动截断尾 null）。
        // 解决了直接用 family name 配置的两大痛点：文件名直观、无重名歧义。
        std::vector<std::pair<std::wstring, std::wstring>> fileToFamily; // {lowerFileName, familyName}
        {
            UINT32 famCount = m_dwriteCollection->GetFontFamilyCount();
            for (UINT32 i = 0; i < famCount; ++i) {
                ComPtr<IDWriteFontFamily> fam;
                if (FAILED(m_dwriteCollection->GetFontFamily(i, &fam)) || !fam) continue;
                std::wstring famName = GetFamilyName(fam.Get());
                if (famName.empty()) continue;
                ComPtr<IDWriteFont> font;
                if (FAILED(fam->GetFirstMatchingFont(
                        DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                        DWRITE_FONT_STYLE_NORMAL, &font)) || !font) continue;
                ComPtr<IDWriteFontFace> face;
                if (FAILED(font->CreateFontFace(&face)) || !face) continue;
                UINT32 fc = 0;
                if (FAILED(face->GetFiles(&fc, nullptr)) || fc == 0) continue;
                std::vector<IDWriteFontFile*> raw(fc, nullptr);
                if (FAILED(face->GetFiles(&fc, raw.data()))) continue;
                for (UINT32 j = 0; j < fc; ++j) {
                    if (!raw[j]) continue;
                    const void* key = nullptr; UINT32 ks = 0;
                    if (SUCCEEDED(raw[j]->GetReferenceKey(&key, &ks)) && key &&
                        ks >= sizeof(wchar_t) && ks % sizeof(wchar_t) == 0) {
                        const wchar_t* p = static_cast<const wchar_t*>(key);
                        UINT32 maxChars = ks / sizeof(wchar_t);
                        size_t realLen = 0;
                        while (realLen < maxChars && p[realLen] != 0) ++realLen;
                        std::wstring path(p, realLen);
                        size_t slash = path.find_last_of(L"\\/");
                        std::wstring fn = (slash != std::wstring::npos) ? path.substr(slash + 1) : path;
                        for (auto& c : fn) c = (wchar_t)towlower(c);
                        fileToFamily.push_back({fn, famName});
                    }
                    raw[j]->Release(); // GetFiles 会 AddRef
                }
            }
        }

        // 1) 配置文件优先：按字体文件名匹配（大小写不敏感，可带/不带扩展名）
        auto matchByFile = [&](const std::wstring& cfg, std::wstring& outFamily) -> bool {
            if (cfg.empty()) return false;
            std::wstring cfgLower = cfg;
            for (auto& c : cfgLower) c = (wchar_t)towlower(c);
            // 精确匹配（含扩展名）
            for (const auto& kv : fileToFamily) {
                if (kv.first == cfgLower) { outFamily = kv.second; return true; }
            }
            // 去扩展名匹配：用户写 "SF-Pro" 可匹配 "SF-Pro.ttf"
            for (const auto& kv : fileToFamily) {
                const std::wstring& fn = kv.first;
                size_t dot = fn.find_last_of(L'.');
                if (dot != std::wstring::npos && fn.substr(0, dot) == cfgLower) {
                    outFamily = kv.second; return true;
                }
            }
            return false;
        };
        if (!m_cfgBody.empty()) bodySet = matchByFile(m_cfgBody, m_bodyFamily);
        if (!m_cfgCode.empty()) codeSet = matchByFile(m_cfgCode, m_codeFamily);

        // 向后兼容：文件名匹配失败时回退按 family name 匹配
        if (!m_cfgBody.empty() && !bodySet) {
            UINT32 idx = 0; BOOL exists = FALSE;
            if (SUCCEEDED(m_dwriteCollection->FindFamilyName(m_cfgBody.c_str(), &idx, &exists)) && exists) {
                m_bodyFamily = m_cfgBody; bodySet = true;
            }
        }
        if (!m_cfgCode.empty() && !codeSet) {
            UINT32 idx = 0; BOOL exists = FALSE;
            if (SUCCEEDED(m_dwriteCollection->FindFamilyName(m_cfgCode.c_str(), &idx, &exists)) && exists) {
                m_codeFamily = m_cfgCode; codeSet = true;
            }
        }

        // 2) 自动检测回退（配置未指定或字体不存在时）
        if (!bodySet || !codeSet) {
            UINT32 famCount = m_dwriteCollection->GetFontFamilyCount();
            for (UINT32 i = 0; i < famCount && !(bodySet && codeSet); ++i) {
                ComPtr<IDWriteFontFamily> fam;
                if (FAILED(m_dwriteCollection->GetFontFamily(i, &fam)) || !fam) continue;
                std::wstring name = GetFamilyName(fam.Get());
                if (name.empty()) continue;
                if (!bodySet) {
                    m_bodyFamily = name;
                    bodySet = true;
                }
                if (!codeSet && IsFamilyMonospaced(fam.Get())) {
                    m_codeFamily = name;
                    codeSet = true;
                }
            }
            if (!codeSet && bodySet) m_codeFamily = m_bodyFamily;
        }
        m_hasCustomFonts = true;
    }

    // ---- GDI：进程私有加载，使 CreateFontW 可用相同 family name ----
    for (const auto& path : files) {
        AddFontResourceExW(path.c_str(), FR_PRIVATE, nullptr);
    }
}
