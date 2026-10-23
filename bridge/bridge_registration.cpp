#include "bridge/bridge_registration.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shlobj.h>
#include <filesystem>
#include <fstream>
#include "third_party/json.hpp"

namespace bridge {

namespace fs = std::filesystem;

static std::wstring GetDefaultBridgeExePath()
{
    WCHAR modulePath[MAX_PATH] = { 0 };
    GetModuleFileNameW(NULL, modulePath, MAX_PATH);
    fs::path p(modulePath);
    p.replace_filename(L"pw-bridge.exe");
    if (fs::exists(p)) {
        return p.wstring();
    }

    // Fallbacks if running standalone or in dev without copying bridge
    const wchar_t* fallbacks[] = {
        L"A:\\apps\\Password Defense\\pw-bridge.exe",
        L"A:\\prjcts\\pwmngr\\pw-windows-cloud\\x64\\Release\\pw-bridge.exe",
        L"A:\\prjcts\\pwmngr\\pw-windows\\x64\\Release\\pw-bridge.exe"
    };
    for (const wchar_t* fb : fallbacks) {
        if (fs::exists(fb)) {
            return fb;
        }
    }
    return p.wstring();
}

static fs::path GetManifestDirectory()
{
    PWSTR localAppData = NULL;
    if (SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, NULL, &localAppData) == S_OK) {
        fs::path dir = fs::path(localAppData) / L"PasswordDefense";
        CoTaskMemFree(localAppData);
        std::error_code ec;
        fs::create_directories(dir, ec);
        return dir;
    }
    return fs::current_path();
}

static bool SetHKCUKey(const std::wstring& subkey, const std::wstring& manifestPath)
{
    HKEY hKey = NULL;
    LSTATUS status = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        subkey.c_str(),
        0,
        NULL,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        NULL,
        &hKey,
        NULL
    );

    if (status != ERROR_SUCCESS) {
        return false;
    }

    status = RegSetValueExW(
        hKey,
        NULL, // (Default) value
        0,
        REG_SZ,
        reinterpret_cast<const BYTE*>(manifestPath.c_str()),
        static_cast<DWORD>((manifestPath.size() + 1) * sizeof(wchar_t))
    );

    RegCloseKey(hKey);
    return (status == ERROR_SUCCESS);
}

static void DeleteHKCUKey(const std::wstring& subkey)
{
    RegDeleteKeyW(HKEY_CURRENT_USER, subkey.c_str());
}

bool RegisterNativeMessagingHosts(const std::wstring& bridgeExePath)
{
    std::wstring exePath = bridgeExePath.empty() ? GetDefaultBridgeExePath() : bridgeExePath;
    if (!fs::exists(exePath)) {
        return false;
    }

    fs::path manifestDir = GetManifestDirectory();
    fs::path chromeManifestPath = manifestDir / L"com.passworddefense.bridge.json";
    fs::path firefoxManifestPath = manifestDir / L"com.passworddefense.bridge_firefox.json";

    // Convert exePath to forward slashes or escaped backslashes for JSON
    std::string exePathUtf8;
    int len = WideCharToMultiByte(CP_UTF8, 0, exePath.c_str(), -1, NULL, 0, NULL, NULL);
    if (len > 0) {
        exePathUtf8.resize(len - 1);
        WideCharToMultiByte(CP_UTF8, 0, exePath.c_str(), -1, &exePathUtf8[0], len, NULL, NULL);
    }

    // 1. Chrome / Chromium / Edge Manifest
    nlohmann::json chromeJson;
    chromeJson["name"] = "com.passworddefense.bridge";
    chromeJson["description"] = "Password Defense Native Messaging Bridge";
    chromeJson["path"] = exePathUtf8;
    chromeJson["type"] = "stdio";
    chromeJson["allowed_origins"] = nlohmann::json::array({
        "chrome-extension://ambjefgkfkdeeggkajdmnpjhlefbkpak/", // Deterministic unpacked dev extension ID (with key)
        "chrome-extension://npakpebejcicljijonidnldpljhjnjik/", // Path-based unpacked dev extension ID (without key)
        "chrome-extension://pnmfdfepbjflmbefebfjkdfjlekmghde/", // Web Store ID placeholder
        "chrome-extension://hlnfbfbkmgdeopnmfdfejlmbefebfjkd/"  // Secondary dev ID
    });

    {
        std::ofstream out(chromeManifestPath);
        if (out.is_open()) {
            out << chromeJson.dump(2);
        } else {
            return false;
        }
    }

    // 2. Firefox Manifest
    nlohmann::json firefoxJson;
    firefoxJson["name"] = "com.passworddefense.bridge";
    firefoxJson["description"] = "Password Defense Native Messaging Bridge";
    firefoxJson["path"] = exePathUtf8;
    firefoxJson["type"] = "stdio";
    firefoxJson["allowed_extensions"] = nlohmann::json::array({
        "addon@passworddefense.com"
    });

    {
        std::ofstream out(firefoxManifestPath);
        if (out.is_open()) {
            out << firefoxJson.dump(2);
        } else {
            return false;
        }
    }

    // 3. Register in HKCU for Chrome, Edge, Brave, and Firefox
    const std::wstring chromeKey = L"Software\\Google\\Chrome\\NativeMessagingHosts\\com.passworddefense.bridge";
    const std::wstring edgeKey   = L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\com.passworddefense.bridge";
    const std::wstring braveKey  = L"Software\\BraveSoftware\\Brave-Browser\\NativeMessagingHosts\\com.passworddefense.bridge";
    const std::wstring firefoxKey= L"Software\\Mozilla\\NativeMessagingHosts\\com.passworddefense.bridge";

    bool ok = true;
    ok &= SetHKCUKey(chromeKey, chromeManifestPath.wstring());
    ok &= SetHKCUKey(edgeKey, chromeManifestPath.wstring());
    ok &= SetHKCUKey(braveKey, chromeManifestPath.wstring());
    ok &= SetHKCUKey(firefoxKey, firefoxManifestPath.wstring());

    return ok;
}

bool UnregisterNativeMessagingHosts()
{
    DeleteHKCUKey(L"Software\\Google\\Chrome\\NativeMessagingHosts\\com.passworddefense.bridge");
    DeleteHKCUKey(L"Software\\Microsoft\\Edge\\NativeMessagingHosts\\com.passworddefense.bridge");
    DeleteHKCUKey(L"Software\\BraveSoftware\\Brave-Browser\\NativeMessagingHosts\\com.passworddefense.bridge");
    DeleteHKCUKey(L"Software\\Mozilla\\NativeMessagingHosts\\com.passworddefense.bridge");
    return true;
}

bool IsNativeMessagingRegistered()
{
    HKEY hKey = NULL;
    LSTATUS status = RegOpenKeyExW(
        HKEY_CURRENT_USER,
        L"Software\\Google\\Chrome\\NativeMessagingHosts\\com.passworddefense.bridge",
        0,
        KEY_READ,
        &hKey
    );
    if (status == ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return true;
    }
    return false;
}

} // namespace bridge
