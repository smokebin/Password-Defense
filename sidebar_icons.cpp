#include "sidebar_icons.h"
#include "GUI.h"
#include "third_party/stb_image.h"

#include <Windows.h>
#include <string>
#include <filesystem>

namespace sidebar_icons {

// ---- Internal ----
static ID3D11ShaderResourceView* s_all_items   = nullptr;
static ID3D11ShaderResourceView* s_pinned      = nullptr;
static ID3D11ShaderResourceView* s_recent      = nullptr;
static ID3D11ShaderResourceView* s_passwords   = nullptr;
static ID3D11ShaderResourceView* s_cards       = nullptr;
static ID3D11ShaderResourceView* s_identity    = nullptr;
static ID3D11ShaderResourceView* s_notes       = nullptr;
static ID3D11ShaderResourceView* s_folder      = nullptr;
static ID3D11ShaderResourceView* s_folder_open = nullptr;

static ID3D11ShaderResourceView* LoadPNG(const std::string& path)
{
    int w, h, channels;
    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
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

static std::string GetIconsDir()
{
    char exe[MAX_PATH];
    GetModuleFileNameA(NULL, exe, MAX_PATH);
    auto dir = std::filesystem::path(exe).parent_path() / "icons" / "sidebar";
    return dir.string();
}

void Init()
{
    std::string dir = GetIconsDir();
    if (!std::filesystem::is_directory(dir)) return;

    auto load = [&](const char* filename) -> ID3D11ShaderResourceView* {
        std::string path = dir + "\\" + filename;
        if (!std::filesystem::exists(path)) return nullptr;
        return LoadPNG(path);
    };

    s_all_items   = load("all_items.png");
    s_pinned      = load("pinned.png");
    s_recent      = load("recent.png");
    s_passwords   = load("passwords.png");
    s_cards       = load("cards.png");
    s_identity    = load("identity.png");
    s_notes       = load("notes.png");
    s_folder      = load("folder.png");
    s_folder_open = load("folder_open.png");
}

static void SafeRelease(ID3D11ShaderResourceView*& srv)
{
    if (srv) { srv->Release(); srv = nullptr; }
}

void Shutdown()
{
    SafeRelease(s_all_items);
    SafeRelease(s_pinned);
    SafeRelease(s_recent);
    SafeRelease(s_passwords);
    SafeRelease(s_cards);
    SafeRelease(s_identity);
    SafeRelease(s_notes);
    SafeRelease(s_folder);
    SafeRelease(s_folder_open);
}

ID3D11ShaderResourceView* AllItems()   { return s_all_items; }
ID3D11ShaderResourceView* Pinned()     { return s_pinned; }
ID3D11ShaderResourceView* Recent()     { return s_recent; }
ID3D11ShaderResourceView* Passwords()  { return s_passwords; }
ID3D11ShaderResourceView* Cards()      { return s_cards; }
ID3D11ShaderResourceView* Identity()   { return s_identity; }
ID3D11ShaderResourceView* Notes()      { return s_notes; }
ID3D11ShaderResourceView* Folder()     { return s_folder; }
ID3D11ShaderResourceView* FolderOpen() { return s_folder_open; }

} // namespace sidebar_icons
