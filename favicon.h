#pragma once
#include <d3d11.h>
#include <string>

namespace favicon {
    void Init();
    void Shutdown();

    // Returns SRV for the website's favicon, or nullptr if unavailable (yet).
    // Automatically triggers async fetch if not cached.
    ID3D11ShaderResourceView* Get(const std::string& website);

    // Extract root domain from a URL: "https://www.Netflix.com/browse" -> "netflix.com"
    std::string ExtractDomain(const std::string& url);
}
