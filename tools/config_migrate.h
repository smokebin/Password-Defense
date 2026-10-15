// config_migrate.h — one-time re-encoding of a legacy config.json to UTF-8.
//
// Builds before the UTF-8 switch used ANSI file dialogs, so a non-ASCII path
// (C:\Users\José\...) existed in memory in the system ANSI code page. The JSON
// writer rejects such bytes, so those builds should never have written them, but a
// hand-edited config, or one written by another tool, can still hold ANSI bytes.
// Read as UTF-8 that is invalid: every JSON parse fails and each setter would then
// rebuild config.json from an empty object, silently dropping all settings.
//
// This converts such a file once, from the LEGACY system code page (not GetACP():
// the UTF-8 manifest makes GetACP() return 65001), to UTF-8. Valid UTF-8 (every
// config this or any earlier build wrote successfully) is left byte-for-byte alone.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <string>

namespace helpers
{
    inline bool IsValidUtf8(const std::string& s)
    {
        if (s.empty()) return true;
        return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0) > 0;
    }

    // The machine's ANSI code page as the OLD build saw it. Read from the registry
    // (authoritative, includes the "Beta: Use Unicode UTF-8" system setting), falling
    // back to the system locale's default ANSI code page.
    inline UINT LegacyAnsiCodePage()
    {
        wchar_t buf[16] = {};
        DWORD sz = sizeof(buf);
        if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\Nls\\CodePage",
                         L"ACP", RRF_RT_REG_SZ, nullptr, buf, &sz) == ERROR_SUCCESS)
        {
            const UINT cp = (UINT)wcstoul(buf, nullptr, 10);
            if (cp != 0) return cp;
        }
        UINT cp = 0;
        if (GetLocaleInfoW(LOCALE_SYSTEM_DEFAULT, LOCALE_IDEFAULTANSICODEPAGE | LOCALE_RETURN_NUMBER,
                           reinterpret_cast<LPWSTR>(&cp), sizeof(cp) / sizeof(wchar_t)) > 0 && cp != 0)
            return cp;
        return 1252;
    }

    // If `text` is not valid UTF-8, re-decode it from `legacy_cp` into UTF-8 and
    // return true. Valid UTF-8 is never touched (returns false). Returns false too if
    // the text can't be decoded in `legacy_cp` (left as is rather than guessed at).
    inline bool MigrateLegacyAnsiText(std::string& text, UINT legacy_cp)
    {
        if (IsValidUtf8(text) || legacy_cp == CP_UTF8) return false;
        const int n = MultiByteToWideChar(legacy_cp, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), nullptr, 0);
        if (n <= 0) return false;
        std::wstring w((size_t)n, L'\0');
        MultiByteToWideChar(legacy_cp, MB_ERR_INVALID_CHARS, text.data(), (int)text.size(), w.data(), n);
        const int m = WideCharToMultiByte(CP_UTF8, 0, w.data(), n, nullptr, 0, nullptr, nullptr);
        if (m <= 0) return false;
        std::string out((size_t)m, '\0');
        WideCharToMultiByte(CP_UTF8, 0, w.data(), n, out.data(), m, nullptr, nullptr);
        text.swap(out);
        return true;
    }
}
