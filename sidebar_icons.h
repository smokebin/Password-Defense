#pragma once
#include <d3d11.h>

// Sidebar icon textures — loaded from icons/sidebar/*.png at startup.
// Returns nullptr if the icon wasn't found or hasn't been loaded.
namespace sidebar_icons {
    void Init();      // Call once after D3D11 device is ready
    void Shutdown();   // Call at app exit

    // Icon accessors — return SRV or nullptr
    ID3D11ShaderResourceView* AllItems();
    ID3D11ShaderResourceView* Pinned();
    ID3D11ShaderResourceView* Recent();
    ID3D11ShaderResourceView* Passwords();
    ID3D11ShaderResourceView* Cards();
    ID3D11ShaderResourceView* Identity();
    ID3D11ShaderResourceView* Notes();
    ID3D11ShaderResourceView* Folder();
    ID3D11ShaderResourceView* FolderOpen();
}
