#include "favicon.h"
#include "GUI.h"
#include "third_party/stb_image.h"
#include "favicon_data.h"

#ifdef _WIN32
    #include <Windows.h>
    #include <ShlObj.h>
    #include <winhttp.h>
    #pragma comment(lib, "winhttp.lib")
#else
    #define CPPHTTPLIB_OPENSSL_SUPPORT
    #include "web/httplib.h"
#endif

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <string>
#include <algorithm>
#include <filesystem>
#include <fstream>

namespace favicon {

static std::unordered_map<std::string, ID3D11ShaderResourceView*> s_cache;   // domain -> SRV (nullptr = failed/missing)
static std::unordered_set<std::string>                            s_pending; // domains being fetched
static std::mutex                                                 s_mutex;
static std::vector<std::pair<std::string, std::vector<uint8_t>>>  s_fetched; // completed fetches for main thread
static std::string                                                s_cache_dir;
static bool                                                       s_initialized = false;
static std::atomic<bool>                                          s_network_enabled{ false }; // offline-first

static ID3D11ShaderResourceView* CreateTextureFromMemory(const unsigned char* data, int len)
{
    int w, h, channels;
    unsigned char* pixels = stbi_load_from_memory(data, len, &w, &h, &channels, 4);
    if (!pixels) return nullptr;

    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width     = w;
    desc.Height    = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format    = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage     = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA sub = {};
    sub.pSysMem    = pixels;
    sub.SysMemPitch = w * 4;

    ID3D11Texture2D* tex = nullptr;
    render::g_pd3dDevice->CreateTexture2D(&desc, &sub, &tex);
    stbi_image_free(pixels);
    if (!tex) return nullptr;

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;

    ID3D11ShaderResourceView* srv = nullptr;
    render::g_pd3dDevice->CreateShaderResourceView(tex, &srvDesc, &srv);
    tex->Release();
    return srv;
}

static std::string GetCacheDir()
{
#ifdef _WIN32
    char docs[MAX_PATH];
    if (SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs) != S_OK)
        return ".\\Favicons";
    return std::string(docs) + "\\PasswordManager\\Favicons";
#else
    const char* home = getenv("HOME");
    if (!home) home = "/tmp";
    return std::string(home) + "/Library/Application Support/PasswordDefense/Favicons";
#endif
}

static std::string DiskCachePath(const std::string& domain)
{
    return s_cache_dir + "/" + domain + ".png";
}

static std::vector<uint8_t> ReadFile(const std::string& path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return {};
    auto sz = f.tellg();
    if (sz <= 0) return {};
    std::vector<uint8_t> buf((size_t)sz);
    f.seekg(0);
    f.read(reinterpret_cast<char*>(buf.data()), sz);
    return buf;
}

static void WriteFile(const std::string& path, const std::vector<uint8_t>& data)
{
    std::ofstream f(path, std::ios::binary);
    if (f)
        f.write(reinterpret_cast<const char*>(data.data()), data.size());
}

static std::vector<uint8_t> FetchFavicon(const std::string& domain)
{
    std::vector<uint8_t> result;

#ifdef _WIN32
    HINTERNET hSession = WinHttpOpen(L"PwMngr/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;

    HINTERNET hConnect = WinHttpConnect(hSession,
        L"www.google.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); return result; }

    std::wstring path = L"/s2/favicons?domain=";
    for (char c : domain) path += (wchar_t)c;
    path += L"&sz=32";

    HINTERNET hRequest = WinHttpOpenRequest(hConnect,
        L"GET", path.c_str(), nullptr,
        WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hRequest) {
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    DWORD opt = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(hRequest, WINHTTP_OPTION_REDIRECT_POLICY, &opt, sizeof(opt));

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr))
    {
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return result;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, WINHTTP_NO_HEADER_INDEX);

    if (statusCode == 200)
    {
        DWORD bytesAvailable = 0;
        while (WinHttpQueryDataAvailable(hRequest, &bytesAvailable) && bytesAvailable > 0)
        {
            std::vector<uint8_t> chunk(bytesAvailable);
            DWORD bytesRead = 0;
            WinHttpReadData(hRequest, chunk.data(), bytesAvailable, &bytesRead);
            result.insert(result.end(), chunk.begin(), chunk.begin() + bytesRead);
        }
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

#else // macOS / Linux — use cpp-httplib
    try {
        httplib::SSLClient cli("www.google.com", 443);
        cli.set_connection_timeout(10, 0);
        cli.set_read_timeout(10, 0);
        cli.set_follow_location(true);

        std::string path = "/s2/favicons?domain=" + domain + "&sz=32";
        auto res = cli.Get(path);

        if (res && res->status == 200) {
            const auto& body = res->body;
            result.assign(body.begin(), body.end());
        }
    }
    catch (...) {} // best-effort
#endif

    return result;
}

static void DrainFetched()
{
    std::lock_guard<std::mutex> lock(s_mutex);
    for (auto& [domain, bytes] : s_fetched)
    {
        s_pending.erase(domain);
        if (!bytes.empty())
        {
            auto* srv = CreateTextureFromMemory(bytes.data(), (int)bytes.size());
            s_cache[domain] = srv; // may be nullptr if decode failed
        }
        else
        {
            s_cache[domain] = nullptr; // mark as failed
        }
    }
    s_fetched.clear();
}

std::string ExtractDomain(const std::string& url)
{
    std::string s = url;

    auto pos = s.find("://");
    if (pos != std::string::npos) s = s.substr(pos + 3);

    if (s.substr(0, 4) == "www.") s = s.substr(4);

    pos = s.find('/');
    if (pos != std::string::npos) s = s.substr(0, pos);

    pos = s.find(':');
    if (pos != std::string::npos) s = s.substr(0, pos);

    std::transform(s.begin(), s.end(), s.begin(), ::tolower);

    while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());

    return s;
}

static void LoadBundled()
{
    for (int i = 0; i < s_bundled_favicon_count; i++)
    {
        const auto& b = s_bundled_favicons[i];
        auto* srv = CreateTextureFromMemory(b.data, b.size);
        if (srv)
            s_cache[b.domain] = srv;
    }
}

void Init()
{
    if (s_initialized) return;
    s_initialized = true;

    s_cache_dir = GetCacheDir();
    std::filesystem::create_directories(s_cache_dir);

    LoadBundled();
}

void SetNetworkEnabled(bool enabled)
{
    s_network_enabled.store(enabled, std::memory_order_relaxed);
}

bool IsNetworkEnabled()
{
    return s_network_enabled.load(std::memory_order_relaxed);
}

CacheStats GetCacheStats()
{
    CacheStats st;
    if (s_cache_dir.empty()) return st;
    std::error_code ec;
    if (!std::filesystem::exists(s_cache_dir, ec)) return st;
    for (std::filesystem::directory_iterator it(s_cache_dir, ec), end; it != end; it.increment(ec))
    {
        if (ec) break;
        if (!it->is_regular_file(ec)) continue;
        if (it->path().extension() != ".png") continue;
        st.file_count++;
        st.total_bytes += (uint64_t)it->file_size(ec);
    }
    return st;
}

void ClearCache()
{
    if (!s_cache_dir.empty())
    {
        std::error_code ec;
        for (std::filesystem::directory_iterator it(s_cache_dir, ec), end; it != end; it.increment(ec))
        {
            if (ec) break;
            if (it->path().extension() == ".png")
                std::filesystem::remove(it->path(), ec);
        }
    }

    // reload bundled icons (they have no disk file)
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_pending.clear();
        s_fetched.clear();
    }
    for (auto& [domain, srv] : s_cache)
        if (srv) srv->Release();
    s_cache.clear();
    if (s_initialized) LoadBundled();
}

void Shutdown()
{
    {
        std::lock_guard<std::mutex> lock(s_mutex);
        s_pending.clear();
        s_fetched.clear();
    }

    for (auto& [domain, srv] : s_cache)
    {
        if (srv) srv->Release();
    }
    s_cache.clear();
    s_initialized = false;
}

ID3D11ShaderResourceView* Get(const std::string& website)
{
    if (!s_initialized || website.empty()) return nullptr;

    DrainFetched();

    std::string domain = ExtractDomain(website);
    if (domain.empty()) return nullptr;

    auto it = s_cache.find(domain);
    if (it != s_cache.end())
        return it->second; // nullptr = previously failed

    std::string diskPath = DiskCachePath(domain);
    auto fileData = ReadFile(diskPath);
    if (!fileData.empty())
    {
        auto* srv = CreateTextureFromMemory(fileData.data(), (int)fileData.size());
        s_cache[domain] = srv;
        return srv;
    }

    // never hit the network unless the user opted in; bundled/disk icons still work
    if (!s_network_enabled.load(std::memory_order_relaxed))
        return nullptr;

    {
        std::lock_guard<std::mutex> lock(s_mutex);
        if (s_pending.count(domain)) return nullptr;
        s_pending.insert(domain);
    }

    std::string cachePath = diskPath;
    std::thread([domain, cachePath]()
    {
        auto bytes = FetchFavicon(domain);
        if (!bytes.empty())
            WriteFile(cachePath, bytes);
        {
            std::lock_guard<std::mutex> lock(s_mutex);
            s_fetched.push_back({ domain, std::move(bytes) });
            // s_pending cleaned up in DrainFetched
        }
    }).detach();

    return nullptr; // not available yet, will appear next frame
}

} // namespace favicon
