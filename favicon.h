#pragma once
#include <d3d11.h>
#include <string>
#include <cstdint>

namespace favicon {
    void Init();
    void Shutdown();

    // Offline-first: when disabled (default), Get() never touches the network —
    // it still serves bundled + previously-cached icons from disk.
    void SetNetworkEnabled(bool enabled);
    bool IsNetworkEnabled();

    struct CacheStats { int file_count = 0; uint64_t total_bytes = 0; };
    CacheStats GetCacheStats();
    void ClearCache();   // wipes disk + in-memory fetched icons; bundled icons survive

    // Returns SRV for the website's favicon, or nullptr if unavailable (yet).
    // Automatically triggers async fetch if not cached.
    ID3D11ShaderResourceView* Get(const std::string& website);

    // Extract root domain from a URL: "https://www.Netflix.com/browse" -> "netflix.com"
    std::string ExtractDomain(const std::string& url);
}
