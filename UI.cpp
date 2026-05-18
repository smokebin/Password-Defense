// UI.cpp (core)
// Shared state definitions, QR textures, theme, selection, shell layout
#include "ui_internal.h"

namespace ui
{
    // Tooltip with extra padding
    void SetTooltipPadded(const char* fmt, ...) IM_FMTARGS(1)
    {
        const bool dark = IsDarkTheme();
        const ImU32 tooltipBg = theme::TooltipBg;
        const ImU32 tooltipBorder = dark ? IM_COL32(255, 255, 255, 24) : IM_COL32(0, 0, 0, 30);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, tooltipBg);
        ImGui::PushStyleColor(ImGuiCol_Border, tooltipBorder);
        va_list args;
        va_start(args, fmt);
        ImGui::SetTooltipV(fmt, args);
        va_end(args);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    ImGuiTextFilter   g_filter;

    // ---- QR code texture for 2FA setup ----
    static ID3D11Texture2D*          s_2fa_qr_tex = nullptr;
    ID3D11ShaderResourceView* s_2fa_qr_srv = nullptr;
    int                       s_2fa_qr_img_size = 0;

    void CreateQRTexture(const std::string& text)
    {
        // Release previous
        if (s_2fa_qr_srv) { s_2fa_qr_srv->Release(); s_2fa_qr_srv = nullptr; }
        if (s_2fa_qr_tex) { s_2fa_qr_tex->Release(); s_2fa_qr_tex = nullptr; }

        qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(
            text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);

        int qrSize = qr.getSize();
        int scale  = 4;   // each module = 4x4 pixels
        int border = 2;   // quiet zone in modules
        int imgSize = (qrSize + 2 * border) * scale;
        s_2fa_qr_img_size = imgSize;

        // Rasterize to RGBA
        std::vector<uint32_t> pixels(imgSize * imgSize);
        for (int y = 0; y < imgSize; y++) {
            for (int x = 0; x < imgSize; x++) {
                int qx = x / scale - border;
                int qy = y / scale - border;
                bool dark = (qx >= 0 && qx < qrSize && qy >= 0 && qy < qrSize
                             && qr.getModule(qx, qy));
                pixels[y * imgSize + x] = dark ? 0xFF000000 : 0xFFFFFFFF;
            }
        }

        // Create DX11 texture
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width  = imgSize;
        desc.Height = imgSize;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage     = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem      = pixels.data();
        sub.SysMemPitch   = imgSize * 4;

        render::g_pd3dDevice->CreateTexture2D(&desc, &sub, &s_2fa_qr_tex);
        if (!s_2fa_qr_tex) return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        render::g_pd3dDevice->CreateShaderResourceView(s_2fa_qr_tex, &srvDesc, &s_2fa_qr_srv);
    }

    void ReleaseQRTexture()
    {
        if (s_2fa_qr_srv) { s_2fa_qr_srv->Release(); s_2fa_qr_srv = nullptr; }
        if (s_2fa_qr_tex) { s_2fa_qr_tex->Release(); s_2fa_qr_tex = nullptr; }
        s_2fa_qr_img_size = 0;
    }

    // ---- Share QR texture (separate from 2FA) ----
    static ID3D11Texture2D*          s_share_qr_tex = nullptr;
    static ID3D11ShaderResourceView* s_share_qr_srv = nullptr;
    static int                       s_share_qr_img_size = 0;

    void CreateShareQRTexture(const std::string& text)
    {
        if (s_share_qr_srv) { s_share_qr_srv->Release(); s_share_qr_srv = nullptr; }
        if (s_share_qr_tex) { s_share_qr_tex->Release(); s_share_qr_tex = nullptr; }

        qrcodegen::QrCode qr = qrcodegen::QrCode::encodeText(
            text.c_str(), qrcodegen::QrCode::Ecc::MEDIUM);

        int qrSize = qr.getSize();
        int scale  = 4;
        int border = 2;
        int imgSize = (qrSize + 2 * border) * scale;
        s_share_qr_img_size = imgSize;

        std::vector<uint32_t> pixels(imgSize * imgSize);
        for (int y = 0; y < imgSize; y++) {
            for (int x = 0; x < imgSize; x++) {
                int qx = x / scale - border;
                int qy = y / scale - border;
                bool dark = (qx >= 0 && qx < qrSize && qy >= 0 && qy < qrSize
                             && qr.getModule(qx, qy));
                pixels[y * imgSize + x] = dark ? 0xFF000000 : 0xFFFFFFFF;
            }
        }

        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width  = imgSize;
        desc.Height = imgSize;
        desc.MipLevels = 1;
        desc.ArraySize = 1;
        desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc.Count = 1;
        desc.Usage     = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

        D3D11_SUBRESOURCE_DATA sub = {};
        sub.pSysMem      = pixels.data();
        sub.SysMemPitch   = imgSize * 4;

        render::g_pd3dDevice->CreateTexture2D(&desc, &sub, &s_share_qr_tex);
        if (!s_share_qr_tex) return;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;

        render::g_pd3dDevice->CreateShaderResourceView(s_share_qr_tex, &srvDesc, &s_share_qr_srv);
    }

    void ReleaseShareQRTexture()
    {
        if (s_share_qr_srv) { s_share_qr_srv->Release(); s_share_qr_srv = nullptr; }
        if (s_share_qr_tex) { s_share_qr_tex->Release(); s_share_qr_tex = nullptr; }
        s_share_qr_img_size = 0;
    }

    ImTextureID GetShareQRTexture() { return (ImTextureID)s_share_qr_srv; }
    int GetShareQRSize() { return s_share_qr_img_size; }

    // Helper: ensure URL has protocol for ShellExecute
    std::string EnsureUrlProtocol(const std::string& url)
    {
        if (url.empty()) return url;
        // Check if already has protocol
        if (url.rfind("http://", 0) == 0 || url.rfind("https://", 0) == 0 ||
            url.rfind("ftp://", 0) == 0 || url.rfind("file://", 0) == 0)
            return url;
        // Default to https://
        return "https://" + url;
    }

    // Helper: draw a dashed line between two points
    void AddDashedLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col,
                       float thickness, float dash_len, float gap_len)
    {
        ImVec2 dir = ImVec2(b.x - a.x, b.y - a.y);
        float len = sqrtf(dir.x * dir.x + dir.y * dir.y);
        if (len < 0.001f) return;
        dir.x /= len; dir.y /= len;

        float t = 0.0f;
        bool drawing = true;
        while (t < len)
        {
            float seg = drawing ? dash_len : gap_len;
            float end_t = (t + seg > len) ? len : t + seg;
            if (drawing)
            {
                ImVec2 p0(a.x + dir.x * t, a.y + dir.y * t);
                ImVec2 p1(a.x + dir.x * end_t, a.y + dir.y * end_t);
                dl->AddLine(p0, p1, col, thickness);
            }
            t = end_t;
            drawing = !drawing;
        }
    }

    // Helper: draw a dashed rectangle
    void AddDashedRect(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col,
                       float thickness, float dash_len, float gap_len)
    {
        ImVec2 a(min.x, min.y), b(max.x, min.y);
        ImVec2 c(max.x, max.y), d(min.x, max.y);

        AddDashedLine(dl, a, b, col, thickness, dash_len, gap_len); // top
        AddDashedLine(dl, b, c, col, thickness, dash_len, gap_len); // right
        AddDashedLine(dl, c, d, col, thickness, dash_len, gap_len); // bottom
        AddDashedLine(dl, d, a, col, thickness, dash_len, gap_len); // left
    }

    // AnimatedTab, AnimatedTabBar structs defined in ui_internal.h

    std::unordered_set<uint64_t> g_selected;

    ShellState* g_shell_ptr = nullptr;

    // ============================================================
    // THEME-AWARE COLORS
    // ============================================================
    bool IsDarkTheme()
    {
        return g_shell_ptr ? g_shell_ptr->dark_theme : true;
    }

    // Shadow colors for card elevation (4 rings, outer to inner)
    ImU32 GetShadowColor(int ring, bool hovered) // ring 0-3, 0=outermost
    {
        if (IsDarkTheme())
        {
            const int normal[] = { 8, 12, 16, 20 };
            const int hover[]  = { 20, 30, 40, 50 };
            const int* alphas = hovered ? hover : normal;
            return IM_COL32(0, 0, 0, alphas[ring]);
        }
        else
        {
            const int normal[] = { 12, 18, 24, 30 };
            const int hover[]  = { 30, 45, 60, 75 };
            const int* alphas = hovered ? hover : normal;
            return IM_COL32(0, 0, 0, alphas[ring]);
        }
    }

    // Change-highlight tint colors
    const ImU32 kChangedRowTint   = colors::ChangedRowTint;
    const ImU32 kNewRowTint       = colors::NewRowTint;

    // Card body background (expanded accordion)
    ImU32 GetCardBodyBg()
    {
        if (g_shell_ptr) {
            const ImVec4& c = IsDarkTheme() ? g_shell_ptr->card_body_bg_dark : g_shell_ptr->card_body_bg_light;
            return IM_COL32((int)(c.x*255), (int)(c.y*255), (int)(c.z*255), (int)(c.w*255));
        }
        if (IsDarkTheme())
            return IM_COL32(27, 27, 30, 255);
        else
            return IM_COL32(246, 246, 246, 255);
    }

    // Badge chip text color
    ImU32 GetBadgeTextColor()
    {
        if (IsDarkTheme())
            return IM_COL32(0, 0, 0, 220);
        else
            return IM_COL32(255, 255, 255, 240);
    }

    // Favorite heart color
    ImU32 GetFavoriteColor()
    {
        return IM_COL32(220, 60, 60, 255);  // Same red for both themes
    }

    // Card header background (accordion rows)
    ImVec4 GetCardHeaderBg()
    {
        if (g_shell_ptr)
            return IsDarkTheme() ? g_shell_ptr->card_header_bg_dark : g_shell_ptr->card_header_bg_light;
        if (IsDarkTheme())
            return ImVec4(0.086f, 0.086f, 0.086f, 0.95f);
        else
            return ImVec4(0.98f, 0.98f, 0.99f, 1.0f);
    }

    // Tile background
    ImVec4 GetTileBg()
    {
        if (IsDarkTheme())
            return ImVec4(0.086f, 0.086f, 0.086f, 0.95f);
        else
            return ImVec4(0.98f, 0.98f, 0.99f, 1.0f);
    }

    // Tile hover background
    ImVec4 GetTileHoverBg()
    {
        if (IsDarkTheme())
            return ImVec4(0.235f, 0.235f, 0.235f, 1.0f);
        else
            return ImVec4(0.92f, 0.92f, 0.94f, 1.0f);
    }

    // Settings card body background (user-customizable)
    ImVec4 GetCardBg()
    {
        if (g_shell_ptr)
            return IsDarkTheme() ? g_shell_ptr->card_bg_dark : g_shell_ptr->card_bg_light;
        if (IsDarkTheme())
            return ImVec4(45/255.0f, 44/255.0f, 46/255.0f, 1.0f);
        else
            return ImVec4(238/255.0f, 238/255.0f, 240/255.0f, 1.0f);
    }

    // Item background (copy rows, etc.)
    ImVec4 GetItemBg()
    {
        if (IsDarkTheme())
            return ImVec4(0.1098f, 0.1059f, 0.1176f, 1.0f); // RGB(28, 27, 30)
        else
            return ImVec4(0.95f, 0.95f, 0.96f, 1.0f);
    }

    // Item hover background
    ImVec4 GetItemHoverBg()
    {
        if (IsDarkTheme())
            return ImVec4(0.235f, 0.235f, 0.235f, 1.0f);
        else
            return ImVec4(0.88f, 0.88f, 0.90f, 1.0f);
    }

    // Checkbox/border gray
    ImVec4 GetBorderGray()
    {
        if (IsDarkTheme())
            return ImVec4(0.235f, 0.235f, 0.235f, 1.0f);
        else
            return ImVec4(0.75f, 0.75f, 0.78f, 1.0f);
    }

    // Controls row container background
    ImU32 GetControlsContainerBg()
    {
        if (g_shell_ptr) {
            const ImVec4& c = IsDarkTheme() ? g_shell_ptr->controls_bg_dark : g_shell_ptr->controls_bg_light;
            return IM_COL32((int)(c.x*255), (int)(c.y*255), (int)(c.z*255), (int)(c.w*255));
        }
        if (IsDarkTheme())
            return IM_COL32(25, 24, 28, 255);
        else
            return IM_COL32(235, 235, 240, 255);
    }

    // Separator line color
    ImU32 GetSeparatorColor()
    {
        if (IsDarkTheme())
            return IM_COL32(40, 40, 40, 255);
        else
            return IM_COL32(200, 200, 205, 255);
    }

    // ============================================================
    // App Style Editor (color customization window)
    // ============================================================
    void RenderAppStyleEditor(ShellState& s)
    {
        if (!s.style_editor_open) return;

        ImGui::SetNextWindowSize(ImVec2(420, 520), ImGuiCond_FirstUseEver);
        if (!ImGui::Begin("App Style Editor", &s.style_editor_open))
        {
            ImGui::End();
            return;
        }

        const bool dark = s.dark_theme;
        ImGui::TextDisabled(dark ? "Editing: Dark Theme" : "Editing: Light Theme");
        ImGui::Separator();
        ImGui::Spacing();

        struct ColorEntry {
            const char* label;
            ImVec4*     dark_col;
            ImVec4*     light_col;
            void (*save_dark)(float, float, float, float);
            void (*save_light)(float, float, float, float);
            ImVec4      default_dark;
            ImVec4      default_light;
        };

        ColorEntry entries[] = {
            {
                "Window Background",
                &s.window_bg_dark, &s.window_bg_light,
                cfg::set_window_bg_dark, cfg::set_window_bg_light,
                ImVec4(28/255.f, 27/255.f, 30/255.f, 1.f),
                ImVec4(245/255.f, 245/255.f, 247/255.f, 1.f)
            },
            {
                "Card Header",
                &s.card_header_bg_dark, &s.card_header_bg_light,
                cfg::set_card_header_bg_dark, cfg::set_card_header_bg_light,
                ImVec4(0.086f, 0.086f, 0.086f, 0.95f),
                ImVec4(0.98f, 0.98f, 0.99f, 1.f)
            },
            {
                "Card Body",
                &s.card_body_bg_dark, &s.card_body_bg_light,
                cfg::set_card_body_bg_dark, cfg::set_card_body_bg_light,
                ImVec4(27/255.f, 27/255.f, 30/255.f, 1.f),
                ImVec4(246/255.f, 246/255.f, 246/255.f, 1.f)
            },
            {
                "Card Background",
                &s.card_bg_dark, &s.card_bg_light,
                cfg::set_card_bg_dark, cfg::set_card_bg_light,
                ImVec4(24/255.f, 24/255.f, 24/255.f, 1.f),
                ImVec4(236/255.f, 236/255.f, 240/255.f, 1.f)
            },
            {
                "Soft Container",
                &s.soft_container_dark, &s.soft_container_light,
                cfg::set_soft_container_dark, cfg::set_soft_container_light,
                ImVec4(36/255.f, 35/255.f, 39/255.f, 1.f),
                ImVec4(1.f, 1.f, 1.f, 1.f)
            },
            {
                "Controls Bar",
                &s.controls_bg_dark, &s.controls_bg_light,
                cfg::set_controls_bg_dark, cfg::set_controls_bg_light,
                ImVec4(25/255.f, 24/255.f, 28/255.f, 1.f),
                ImVec4(235/255.f, 235/255.f, 240/255.f, 1.f)
            },
        };

        const int count = sizeof(entries) / sizeof(entries[0]);

        for (int i = 0; i < count; i++)
        {
            auto& e = entries[i];
            ImVec4* col = dark ? e.dark_col : e.light_col;

            ImGui::PushID(i);
            if (ImGui::ColorEdit4(e.label, &col->x,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf))
            {
                if (dark)
                    e.save_dark(col->x, col->y, col->z, col->w);
                else
                    e.save_light(col->x, col->y, col->z, col->w);

                // Live-update window background
                if (i == 0)
                    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = *col;
            }
            ImGui::PopID();
        }

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::Button("Reset to Defaults"))
        {
            for (int i = 0; i < count; i++)
            {
                auto& e = entries[i];
                *e.dark_col = e.default_dark;
                *e.light_col = e.default_light;
                e.save_dark(e.default_dark.x, e.default_dark.y, e.default_dark.z, e.default_dark.w);
                e.save_light(e.default_light.x, e.default_light.y, e.default_light.z, e.default_light.w);
            }
            const ImVec4& wb = dark ? s.window_bg_dark : s.window_bg_light;
            ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = wb;
        }

        ImGui::End();
    }

    AnimatedTabBar g_dbTabsAnim;

    bool IsSelected(uint64_t rowKey)
    {
        return g_selected.find(rowKey) != g_selected.end();
    }

    void ToggleSelected(uint64_t rowKey)
    {
        auto it = g_selected.find(rowKey);
        if (it != g_selected.end()) g_selected.erase(it);
        else g_selected.insert(rowKey);
    }

    void ClearSelectionForVault(uint32_t activeVaultKey)
    {
        const uint64_t prefix = (uint64_t(activeVaultKey) << 32);
        for (auto it = g_selected.begin(); it != g_selected.end(); )
        {
            if ((*it & 0xFFFFFFFF00000000ull) == prefix) it = g_selected.erase(it);
            else ++it;
        }
    }

    void SelectRow(uint32_t activeVaultKey, int id)
    {
        g_selected.insert((uint64_t(activeVaultKey) << 32) | uint64_t(uint32_t(id)));
    }

    bool HasAnySelection(uint32_t activeVaultKey)
    {
        const uint64_t prefix = (uint64_t(activeVaultKey) << 32);
        for (uint64_t k : g_selected)
            if ((k & 0xFFFFFFFF00000000ull) == prefix)
                return true;
        return false;
    }

    int CountSelection(uint32_t activeVaultKey)
    {
        const uint64_t prefix = (uint64_t(activeVaultKey) << 32);
        int n = 0;
        for (uint64_t k : g_selected)
            if ((k & 0xFFFFFFFF00000000ull) == prefix)
                ++n;
        return n;
    }

    float AnimLerp(float current, float target, float speed, float dt)
    {
        const float t = 1.0f - std::exp(-speed * dt);
        return current + (target - current) * t;
    }


    void ApplyTheme()
    {
        render::ApplyTheme(cfg::is_dark_theme());
    }

    bool Initialize()
    {
        static bool inited = false;
        if (inited) return true;
        ApplyTheme();
        inited = true;
        return true;
    }

    const char* GetSearchText()
    {
        // ImGuiTextFilter::InputBuf is always null-terminated.
        // Returns "" when empty, which your code already handles.
        return g_filter.InputBuf;
    }

    int GetSearchFilter()
    {
        return g_shell_ptr ? g_shell_ptr->search_filter : 0;
    }

    bool GetShowGroupCount()
    {
        return g_shell_ptr ? g_shell_ptr->show_group_count : true;
    }

    void BeginShell(ShellState& s, const char* window_title)
    {
        Initialize();

        g_shell_ptr = &s;

        // One-time config load
        static bool s_cfg_init = false;
        if (!s_cfg_init)
        {
            s.hover_expand      = cfg::get_hover_expand();
            s.view_mode         = (ui::ViewMode)cfg::get_view_mode();
            s.order_key         = (ui::OrderKey)cfg::get_order_key();
            s.order_dir         = (ui::OrderDir)cfg::get_order_dir();
            s.group_mode        = (ui::GroupMode)cfg::get_group_mode();
            s.autosave_enabled  = cfg::get_autosave_enabled();
            s.autoscroll_enabled = cfg::get_autoscroll_enabled();
            s.show_group_count   = cfg::get_show_group_count();
            s.row_gap            = cfg::get_row_gap();
            s.self_destruct_mode = cfg::get_self_destruct_mode();
            s.always_on_top     = cfg::get_always_on_top();
            s.minimize_to_tray  = cfg::get_minimize_to_tray();
            s.start_on_boot     = cfg::get_start_on_boot();
            s.start_minimized   = cfg::get_start_minimized();
            s.auto_open_vault   = cfg::get_auto_open_vault();
            s.pill_tab_0        = cfg::get_pill_tab_0();
            s.pill_tab_1        = cfg::get_pill_tab_1();
            s.auto_backup       = cfg::get_auto_backup();
            s.backup_keep_count = cfg::get_backup_keep_count();
            s.detailed_header_cols = cfg::get_detailed_header_columns();
            s.local_server_enabled = cfg::get_local_server_enabled();
            s.local_server_port    = cfg::get_local_server_port();
            s.three_pane_sidebar_collapsed = cfg::get_three_pane_sidebar_collapsed();
            s.three_pane_list_collapsed    = cfg::get_three_pane_list_collapsed();
            s.three_pane_sidebar_auto_collapse = cfg::get_three_pane_sidebar_auto_collapse();
            s.three_pane_list_auto_collapse    = cfg::get_three_pane_list_auto_collapse();
            if (s.three_pane_sidebar_auto_collapse) s.three_pane_sidebar_collapsed = true;
            if (s.three_pane_list_auto_collapse)    s.three_pane_list_collapsed = true;
            if (s.always_on_top) s.always_on_top_changed = true;
            s_cfg_init = true;
        }

        // reset one-frame intents
        s.add_clicked = false;
        s.undo_clicked = false;
        s.save_clicked = false;
        s.back_clicked = false;

        s.bulk_pin_clicked = false;
        s.bulk_unpin_clicked = false;
        s.bulk_fav_clicked = false;
        s.bulk_unfav_clicked = false;
        s.bulk_set_group_clicked = false;

        s.bulk_delete_clicked = false;
        s.clear_selection_clicked = false;

        s.footer_open_db_clicked = false;
        s.footer_new_db_clicked = false;
        s.footer_save_clicked = false;
        s.footer_options_clicked = false;
        s.footer_close_clicked = false;
        s.footer_close_anyway = false;
        s.footer_close_cancel = false;

        s.settings_clicked = false;
        s.goto_locked_clicked = false;

        // Set window to fill entire viewport
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 10));
        ImGui::Begin(window_title, nullptr,
            ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoTitleBar |
            ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus);

        ImGuiWindow* w = ImGui::GetCurrentWindow();
        if (w->ScrollbarY)
        {
            float sb = ImGui::GetStyle().ScrollbarSize;
            w->WorkRect.Max.x += sb;
            w->ContentRegionRect.Max.x += sb;
        }

        g_shell_open = true;

        // ============================================================
        // TOP BAR: Vault header (dropdown + back/exit)
        // ============================================================
        if (s.active_screen != Screen::Locked)
        {
            // Settings shows back button, other screens just exit
            bool show_back = (s.active_screen == Screen::Settings);
            //RenderVaultHeader(s, show_back);
            ImGui::Dummy(ImVec2(0, 8));
        }

        // Note: No longer opening body child here. Caller uses BeginShellHeader/BeginShellScroll.
    }

    void BeginShellHeader()
    {
        // Fixed header area (not scrollable): just regular ImGui content
        // No child window needed since it doesn't scroll
    }

    void EndShellHeader()
    {
        // Small gap between header and scroll area (skip for edge-to-edge views)
        bool edgeToEdge = g_shell_ptr &&
            (g_shell_ptr->view_mode == ViewMode::ThreePane ||
             g_shell_ptr->view_mode == ViewMode::Table);
        if (!edgeToEdge)
            ImGui::Dummy(ImVec2(0, 5));
    }

    void BeginShellScroll()
    {
        float pad = ImGui::GetStyle().WindowPadding.x;
        float gap = 4.0f; // spacing between scrollbar and border

        bool edgeToEdge = g_shell_ptr &&
            (g_shell_ptr->view_mode == ViewMode::ThreePane ||
             g_shell_ptr->view_mode == ViewMode::Table);

        float avail_w = ImGui::GetContentRegionAvail().x + pad - gap;
        float avail_h = 0; // 0 = fill remaining

        if (edgeToEdge)
        {
            // Eat into left padding
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() - pad);
            avail_w += pad + gap; // also remove right gap
            avail_h = ImGui::GetContentRegionAvail().y + pad; // eat into bottom padding
        }

        ImGuiWindowFlags scrollFlags = edgeToEdge
            ? (ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)
            : 0;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##scroll", ImVec2(avail_w, avail_h), false, scrollFlags);

        ImGui::Dummy(ImVec2(0, edgeToEdge ? 0.0f : 1.0f));
    }

    void EndShellScroll()
    {
        float scrollY = ImGui::GetScrollY();
        ImVec2 childMin = ImGui::GetWindowPos();
        float childW = ImGui::GetWindowSize().x;
        ImGui::EndChild();
        ImGui::PopStyleVar();
        DrawScrollTopFade(childMin, childW, scrollY);
    }

    void EndShell()
    {
        // Note: Caller is responsible for ending scroll child via EndShellScroll().

        // End window
        g_shell_ptr = nullptr;
        g_shell_open = false;
        ImGui::End();
        ImGui::PopStyleVar();
    }

    void RenderShell(ShellState& s, const char* window_title)
    {
        BeginShell(s, window_title);
        ImGui::TextDisabled("Body hook: render your list here from application.cpp.");
        ImGui::TextDisabled("Use ui::BeginShell(state); ... draw ...; ui::EndShell();");
        EndShell();
    }

} // namespace ui
