// u8path.h — the app's single string encoding for file paths is UTF-8.
//
// Every path held in a std::string (dialogs, config, recent list, vault_path) is
// UTF-8: that is what SQLite's sqlite3_open* expects and what ImGui renders. Never
// hand such a string straight to a narrow Win32/CRT/std::filesystem API, which
// would read it in the ANSI code page (C:\Users\José → mojibake, or a different
// file). Convert at the boundary with these helpers instead.
//
// The app manifest also sets the process code page to UTF-8 (Windows 10 1903+),
// which makes any remaining narrow call agree; these helpers keep the critical
// paths correct even where that manifest setting isn't honored.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include <filesystem>
#include <string>

namespace helpers
{
    inline std::wstring Utf8ToWide(const std::string& s)
    {
        if (s.empty()) return {};
        const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
        if (n <= 0) return {};
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
        return w;
    }

    inline std::string WideToUtf8(const std::wstring& w)
    {
        if (w.empty()) return {};
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
        if (n <= 0) return {};
        std::string s((size_t)n, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
        return s;
    }

    // UTF-8 string → filesystem path (stored as UTF-16 internally; no ANSI step)
    inline std::filesystem::path U8Path(const std::string& utf8)
    {
        return std::filesystem::path(Utf8ToWide(utf8));
    }

    // filesystem path → UTF-8 string (path::string() would convert via ANSI and
    // throws for characters the ANSI code page can't represent, e.g. CJK)
    inline std::string PathToU8(const std::filesystem::path& p)
    {
        return WideToUtf8(p.native());
    }
}
