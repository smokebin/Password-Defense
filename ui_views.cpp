// ui_views.cpp
// View renderers: accordion items, tiles, three-pane, table, row state management
#include "ui_internal.h"

namespace ui
{
    // ============================================================
    // EditBuffers member functions (struct defined in ui_internal.h)
    // ============================================================
    void EditBuffers::BeginFrom(const AccordionItem& c, const std::string& password_value)
    {
        active = true;
        title = c.title; email = c.email; user = c.user;
        website = c.website; group = c.group; notes = c.notes;
        password = password_value;
        card_number = FormatWithPattern(StripNonDigits(c.card_number), "#### #### #### ####");
        card_expiry = FormatWithPattern(StripNonDigits(c.card_expiry), "## / ##");
        card_cvv = c.card_cvv; card_brand = c.card_brand;
        cardholder_name = c.cardholder_name;
        card_address = c.card_address; card_city = c.card_city;
        card_postal_code = c.card_postal_code;
        full_name = c.full_name; id_type_buf = c.id_type;
        id_number = c.id_number;
        date_of_birth = FormatWithPattern(StripNonDigits(c.date_of_birth), "## / ## / ####");
        expiry_date_buf = FormatWithPattern(StripNonDigits(c.expiry_date), "## / ## / ####");
        country = c.country; address = c.address;
        phone = FormatWithPattern(StripNonDigits(c.phone), "(###) ###-####");
        o_title = title; o_email = email; o_user = user;
        o_password = password;
        o_website = website; o_group = group; o_notes = notes;
        o_card_number = card_number; o_card_expiry = card_expiry;
        o_card_cvv = card_cvv; o_card_brand = card_brand;
        o_cardholder_name = cardholder_name;
        o_card_address = card_address; o_card_city = card_city;
        o_card_postal_code = card_postal_code;
        o_full_name = full_name; o_id_type_buf = id_type_buf;
        o_id_number = id_number; o_date_of_birth = date_of_birth;
        o_expiry_date_buf = expiry_date_buf; o_country = country;
        o_address = address; o_phone = phone;
        show_password = false;
    }

    void EditBuffers::Revert()
    {
        title = o_title; email = o_email; user = o_user;
        password = o_password;
        website = o_website; group = o_group; notes = o_notes;
        card_number = o_card_number; card_expiry = o_card_expiry;
        card_cvv = o_card_cvv; card_brand = o_card_brand;
        cardholder_name = o_cardholder_name;
        card_address = o_card_address; card_city = o_card_city;
        card_postal_code = o_card_postal_code;
        full_name = o_full_name; id_type_buf = o_id_type_buf;
        id_number = o_id_number; date_of_birth = o_date_of_birth;
        expiry_date_buf = o_expiry_date_buf; country = o_country;
        address = o_address; phone = o_phone;
        show_password = false;
    }

    // ============================================================
    // View state variable definitions (extern in ui_internal.h)
    // ============================================================
    std::unordered_map<uint64_t, EditBuffers> g_edit;
    std::unordered_set<uint64_t> g_open;
    std::unordered_map<uint64_t, int> g_scroll_to_open;
    std::unordered_map<uint64_t, TileCardState> g_tile_state;
    std::unordered_map<uint64_t, float> g_copy_flash;
    std::unordered_set<uint64_t> g_hover_opened;
    std::unordered_set<uint64_t> g_hover_flipped;
    bool  g_3p_sidebar_hover_expanded = false;
    bool  g_3p_list_hover_expanded = false;
    float g_3p_sidebar_leave_timer = 0.0f;
    float g_3p_list_leave_timer = 0.0f;
    float g_3p_sidebar_anim_w = -1.0f;
    float g_3p_list_anim_w    = -1.0f;
    std::unordered_set<uint64_t> g_notes_visible;
    uint64_t g_notes_reprompt_pending = 0;
    std::unordered_map<uint64_t, float> g_card_body_h;
    std::unordered_map<uint64_t, float> g_card_anim_h;
    std::unordered_set<uint64_t>        g_card_closing;
    std::unordered_set<std::string>     g_collapsed_headers; // collapsed group headers in list views

    bool GroupMatches(const AccordionItem& c, const std::string& group)
    {
        if (group == "Filter" || group == "All") return true;
        return c.group == group;
    }

    // Resolve what pill tab 0/1 actually means (could be customized)
    bool PillTabMatches(const AccordionItem& c, const std::string& tabLabel)
    {
        if (tabLabel == "Pinned")    return c.is_pinned;
        if (tabLabel == "Favorites") return c.is_favorite;
        if (tabLabel == "Recent")    return true; // recent filtering handled elsewhere
        // Custom group name
        return c.group == tabLabel;
    }

    static inline bool SortModeMatches(const AccordionItem& c, int sort_mode, const std::string& tab0, const std::string& tab1)
    {
        if (sort_mode == 0) return PillTabMatches(c, tab0);
        if (sort_mode == 1) return PillTabMatches(c, tab1);
        return true; // All (2) or Recent (3)
    }

    // Get icon pair for a pill tab label
    void GetPillTabIcons(const std::string& label, const char*& icon, const char*& iconActive)
    {
        if (label == "Pinned")         { icon = ICON_MDI_PIN;   iconActive = ICON_MDI_PIN; }
        else if (label == "Favorites") { icon = ICON_MDI_HEART; iconActive = ICON_MDI_HEART; }
        else if (label == "Recent")    { icon = ICON_MDI_HISTORY;       iconActive = ICON_MDI_HISTORY; }
        else                           { icon = ICON_MDI_FOLDER; iconActive = ICON_MDI_FOLDER; }
    }

    struct AccordionItemAction
    {
        int id = -1;
        bool requested_delete = false;
        bool edit_committed = false;
        bool edit_open_requested = false;
        int  edit_open_id_override = -1;   // if >= 0, open this credential instead of current
        bool toggle_pin_requested = false;
        bool toggle_fav_requested = false;
        AccordionItem edited{};
        std::string edited_password;
        bool selection_toggled = false;
        bool selected_now = false;
        bool interacted = false;
    };

    // 1) Row key helper
    uint64_t MakeRowKey(uint32_t activeVaultKey, int credId)
    {
        return (uint64_t(activeVaultKey) << 32) | uint32_t(credId);
    }

    uint64_t MakeCopyKey(uint64_t rowKey, int fieldIndex)
    {
        return rowKey ^ (uint64_t(fieldIndex) << 48);
    }

    std::string FormatIsoDateLabel(const std::string& iso)
    {
        if (iso.size() < 10) return iso;
        const int y = std::atoi(iso.substr(0, 4).c_str());
        const int m = std::atoi(iso.substr(5, 2).c_str());
        const int d = std::atoi(iso.substr(8, 2).c_str());
        if (y <= 0 || m <= 0 || d <= 0) return iso;

        static const char* months[] = {
            "Jan","Feb","Mar","Apr","May","Jun",
            "Jul","Aug","Sep","Oct","Nov","Dec"
        };

        if (m < 1 || m > 12) return iso;

        char buf[32];
        snprintf(buf, sizeof(buf), "%s %d, %d", months[m - 1], d, y);
        return std::string(buf);
    }

    // Format Unix milliseconds to "Dec 30, 2025" display format
    std::string FormatUnixMsDateLabel(int64_t ms)
    {
        if (ms == 0) return "";

        std::time_t sec = static_cast<std::time_t>(ms / 1000);
        std::tm tm_local;
#ifdef _WIN32
        localtime_s(&tm_local, &sec);
#else
        localtime_r(&sec, &tm_local);
#endif

        int y = tm_local.tm_year + 1900;
        int m = tm_local.tm_mon + 1;
        int d = tm_local.tm_mday;

        if (y <= 0 || m <= 0 || d <= 0) return "";

        static const char* months[] = {
            "Jan","Feb","Mar","Apr","May","Jun",
            "Jul","Aug","Sep","Oct","Nov","Dec"
        };

        if (m < 1 || m > 12) return "";

        int hour24 = tm_local.tm_hour;
        int min = tm_local.tm_min;
        const char* ampm = hour24 >= 12 ? "PM" : "AM";
        int hour12 = hour24 % 12;
        if (hour12 == 0) hour12 = 12;

        char buf[48];
        snprintf(buf, sizeof(buf), "%s %d, %d at %d:%02d %s", months[m - 1], d, y, hour12, min, ampm);
        return std::string(buf);
    }

    // Format Unix milliseconds to "Dec 30, 2025" display format (date only, no time)
    std::string FormatUnixMsDateOnly(int64_t ms)
    {
        if (ms == 0) return "";

        std::time_t sec = static_cast<std::time_t>(ms / 1000);
        std::tm tm_local;
#ifdef _WIN32
        localtime_s(&tm_local, &sec);
#else
        localtime_r(&sec, &tm_local);
#endif

        int y = tm_local.tm_year + 1900;
        int m = tm_local.tm_mon + 1;
        int d = tm_local.tm_mday;

        if (y <= 0 || m <= 0 || d <= 0) return "";

        static const char* months[] = {
            "Jan","Feb","Mar","Apr","May","Jun",
            "Jul","Aug","Sep","Oct","Nov","Dec"
        };

        if (m < 1 || m > 12) return "";

        char buf[32];
        snprintf(buf, sizeof(buf), "%s %d, %d", months[m - 1], d, y);
        return std::string(buf);
    }
    // ============================================================
    // INFO ROW HELPERS (for accordion card body)
    // ============================================================

    // Clean URL for display (remove protocol and www)
    std::string CleanUrl(const std::string& url)
    {
        std::string clean = url;
        if (clean.rfind("https://", 0) == 0) clean = clean.substr(8);
        else if (clean.rfind("http://", 0) == 0) clean = clean.substr(7);
        if (clean.rfind("www.", 0) == 0) clean = clean.substr(4);
        return clean;
    }

    // ============================================================
    // PRIVACY MODE — mask sensitive fields with bullets
    // ============================================================
    const char* MaskIfPrivate(const std::string& text)
    {
        if (!g_shell_ptr || !g_shell_ptr->privacy_mode || text.empty())
            return nullptr;
        return "\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2";
    }

    // ============================================================
    // FIELD CHANGE HIGHLIGHT (dim background on changed fields)
    // ============================================================
    bool g_field_accent_active = false;
    static const ImU32 kFieldHighlight = colors::FieldHighlight; // subtle orange wash

    void DrawFieldHighlight(ImDrawList* dl, float rowX, float rowY, float rowW, float rowH)
    {
        if (!g_field_accent_active) return;
        dl->AddRectFilled(
            ImVec2(rowX, rowY),
            ImVec2(rowX + rowW, rowY + rowH),
            kFieldHighlight, 2.0f);
    }

    // ---- Copy-flash icon effect ----
    static std::unordered_map<ImGuiID, double> g_copy_flash_time;
    static constexpr double kCopyFlashDuration = 0.55; // seconds

    void TriggerCopyFlash()
    {
        ImGuiID id = ImGui::GetID("##copy_flash");
        g_copy_flash_time[id] = ImGui::GetTime();
    }

    // Draw icon with optional copy-flash: green icon with darker outline
    void DrawRowIcon(const char* icon)
    {
        ImGuiID id = ImGui::GetID("##copy_flash");
        double now = ImGui::GetTime();
        auto it = g_copy_flash_time.find(id);
        float flash = 0.0f;
        if (it != g_copy_flash_time.end())
        {
            double elapsed = now - it->second;
            if (elapsed < kCopyFlashDuration)
                flash = 1.0f - (float)(elapsed / kCopyFlashDuration);
            else
                g_copy_flash_time.erase(it);
        }

        if (flash > 0.01f)
        {
            // Lerp from bright green back to TextDisabled
            ImVec4 green(0.39f, 0.90f, 0.47f, 1.0f);
            ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            ImVec4 col = ImLerp(dim, green, flash);
            ImGui::PushStyleColor(ImGuiCol_Text, col);
            ImGui::TextUnformatted(icon);
            ImGui::PopStyleColor();
        }
        else
        {
            ImGui::TextDisabled(icon);
        }
    }

    // Info row: [Icon] text (no button, for group etc.)
    void DrawInfoRow(
        const char* id,
        const char* icon,
        const std::string& value,
        float iconColW,
        float valueColW,
        float rowSpacing)
    {
        ImGui::PushID(id);
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float h = ImGui::GetTextLineHeight() + rowSpacing;
            float w = iconColW + valueColW;
            DrawFieldHighlight(ImGui::GetWindowDrawList(), p.x, p.y, w, h);
        }
        ImGui::TextDisabled(icon);
        ImGui::SameLine(iconColW);
        const char* display = value.empty() ? "—" : value.c_str();
        TextEllipsisClipped(display, valueColW);
        ImGui::Dummy(ImVec2(0, rowSpacing));
        ImGui::PopID();
    }

    // Floating icon button — no background, no bevel, just icon
    bool IconFloatingBtn(const char* id, const char* icon, const char* tip, float sz)
    {
        ImGui::PushID(id);

        bool pressed = ImGui::InvisibleButton("##fl", ImVec2(sz, sz));
        bool hovered = ImGui::IsItemHovered();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pmin = ImGui::GetItemRectMin();
        ImVec2 pmax = ImGui::GetItemRectMax();
        ImVec2 center((pmin.x + pmax.x) * 0.5f, (pmin.y + pmax.y) * 0.5f);

        ImU32 iconCol = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        ImVec2 ts = ImGui::CalcTextSize(icon);
        dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), iconCol, icon);

        if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
        {
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
            SetTooltipPadded("%s", tip);
            ImGui::PopStyleColor();
        }

        ImGui::PopID();
        return pressed;
    }

    // Website row: [Globe] website.com [Launch] (launch on hover)
    void DrawWebsiteRow(
        const char* id,
        const std::string& website,
        float iconColW,
        float valueColW,
        float rowWidth,
        float btnSz,
        float rowSpacing)
    {
        ImGui::PushID(id);

        ImVec2 startPos = ImGui::GetCursorScreenPos();
        float rowY = startPos.y;
        float rowH = ImGui::GetTextLineHeight() + rowSpacing;
        ImVec2 rowMin(ImGui::GetWindowPos().x, rowY);
        ImVec2 rowMax(rowMin.x + rowWidth, rowY + rowH);

        DrawFieldHighlight(ImGui::GetWindowDrawList(), rowMin.x, rowY, rowWidth, rowH);

        ImVec2 mp = ImGui::GetMousePos();
        bool anyModal = ImGui::IsPopupOpen((const char*)NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || (g_shell_ptr && g_shell_ptr->settings_modal_open);
        bool rowHovered = !anyModal && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows) && (mp.x >= rowMin.x && mp.x <= rowMax.x && mp.y >= rowMin.y && mp.y <= rowMax.y);

        // Hover font scale
        if (rowHovered) ImGui::SetWindowFontScale(1.05f);

        // Click to launch (only in the text area, not the button zone)
        float btnZoneX = startPos.x + rowWidth - btnSz - 8.0f;
        if (rowHovered && !website.empty() && mp.x < btnZoneX && ImGui::IsMouseClicked(0))
        {
            helpers::open_website(website);
        }

        ImGui::TextDisabled(ICON_MDI_EARTH);
        ImGui::SameLine(iconColW);

        std::string display = website.empty() ? "—" : CleanUrl(website);
        TextEllipsisClipped(display.c_str(), valueColW);

        if (rowHovered) ImGui::SetWindowFontScale(1.0f);

        // Floating icon button (absolute positioned, no chrome)
        if (rowHovered && !website.empty())
        {
            ImVec2 btnPos(startPos.x + rowWidth - btnSz - 8.0f, rowY);
            ImGui::SetCursorScreenPos(btnPos);
            if (IconFloatingBtn("launch", ICON_MDI_OPEN_IN_NEW, "Launch", btnSz))
            {
                helpers::open_website(website);
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(startPos.x, rowY + ImGui::GetTextLineHeight()));
        ImGui::Dummy(ImVec2(0, rowSpacing));
        ImGui::PopID();
    }

    // Copy row: [Icon] value [Copy] (copy on hover)
    void DrawCopyRow(
        const char* id,
        const char* icon,
        const std::string& value,
        float iconColW,
        float valueColW,
        float rowWidth,
        float btnSz,
        float rowSpacing,
        const char* display_override)
    {
        ImGui::PushID(id);

        ImVec2 startPos = ImGui::GetCursorScreenPos();
        float rowY = startPos.y;
        float rowH = ImGui::GetTextLineHeight() + rowSpacing;
        ImVec2 rowMin(ImGui::GetWindowPos().x, rowY);
        ImVec2 rowMax(rowMin.x + rowWidth, rowY + rowH);

        DrawFieldHighlight(ImGui::GetWindowDrawList(), rowMin.x, rowY, rowWidth, rowH);

        ImVec2 mp = ImGui::GetMousePos();
        bool anyModal = ImGui::IsPopupOpen((const char*)NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || (g_shell_ptr && g_shell_ptr->settings_modal_open);
        bool rowHovered = !anyModal && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows) && (mp.x >= rowMin.x && mp.x <= rowMax.x && mp.y >= rowMin.y && mp.y <= rowMax.y);

        // Hover font scale
        if (rowHovered) ImGui::SetWindowFontScale(1.05f);

        // Click to copy (only in the text area, not the button zone)
        float btnZoneX = startPos.x + rowWidth - btnSz - 8.0f;
        if (rowHovered && !value.empty() && mp.x < btnZoneX && ImGui::IsMouseClicked(0))
        {
            ImGui::SetClipboardText(value.c_str());
            TriggerCopyFlash();
            ShowToast("Copied", ToastType::Success);
        }

        DrawRowIcon(icon);
        ImGui::SameLine(iconColW);

        const char* display = value.empty() ? "\xe2\x80\x94"
                           : display_override ? display_override
                           : value.c_str();
        TextEllipsisClipped(display, valueColW);

        if (rowHovered) ImGui::SetWindowFontScale(1.0f);

        // Floating icon button (absolute positioned, no chrome)
        if (rowHovered && !value.empty())
        {
            ImVec2 btnPos(startPos.x + rowWidth - btnSz - 8.0f, rowY);
            ImGui::SetCursorScreenPos(btnPos);
            if (IconFloatingBtn("copy", ICON_MDI_CONTENT_COPY, "Copy", btnSz))
            {
                ImGui::SetClipboardText(value.c_str());
                TriggerCopyFlash();
                ShowToast("Copied", ToastType::Success);
            }

        }

        ImGui::SetCursorScreenPos(ImVec2(startPos.x, rowY + ImGui::GetTextLineHeight()));
        ImGui::Dummy(ImVec2(0, rowSpacing));
        ImGui::PopID();
    }

    // Map email domain to webmail URL (returns empty if unknown)
    std::string GetWebmailUrl(const std::string& email)
    {
        size_t at = email.rfind('@');
        if (at == std::string::npos || at + 1 >= email.size()) return "";

        std::string domain = email.substr(at + 1);
        // Lowercase for comparison
        for (auto& ch : domain) ch = (char)tolower((unsigned char)ch);

        if (domain == "gmail.com" || domain == "googlemail.com")
            return "https://mail.google.com";
        if (domain == "outlook.com" || domain == "hotmail.com" || domain == "live.com" || domain == "msn.com")
            return "https://outlook.live.com";
        if (domain == "yahoo.com" || domain == "ymail.com")
            return "https://mail.yahoo.com";
        if (domain == "protonmail.com" || domain == "proton.me" || domain == "pm.me")
            return "https://mail.proton.me";
        if (domain == "icloud.com" || domain == "me.com" || domain == "mac.com")
            return "https://www.icloud.com/mail";
        if (domain == "zoho.com")
            return "https://mail.zoho.com";
        if (domain == "aol.com")
            return "https://mail.aol.com";
        if (domain == "yandex.com" || domain == "yandex.ru")
            return "https://mail.yandex.com";
        if (domain == "tutanota.com" || domain == "tuta.io")
            return "https://app.tuta.com";
        if (domain == "gmx.com" || domain == "gmx.net")
            return "https://www.gmx.com";
        if (domain == "mail.com")
            return "https://www.mail.com";
        if (domain == "fastmail.com")
            return "https://app.fastmail.com";

        // Fallback: try https://mail.<domain>
        return "https://mail." + domain;
    }

    // Email row: [Email icon] email [Open Inbox | Copy] (on hover)
    void DrawEmailRow(
        const char* id,
        const std::string& value,
        float iconColW,
        float valueColW,
        float rowWidth,
        float btnSz,
        float rowSpacing,
        const char* display_override)
    {
        ImGui::PushID(id);

        ImVec2 startPos = ImGui::GetCursorScreenPos();
        float rowY = startPos.y;
        float rowH = ImGui::GetTextLineHeight() + rowSpacing;
        ImVec2 rowMin(ImGui::GetWindowPos().x, rowY);
        ImVec2 rowMax(rowMin.x + rowWidth, rowY + rowH);

        DrawFieldHighlight(ImGui::GetWindowDrawList(), rowMin.x, rowY, rowWidth, rowH);

        ImVec2 mp = ImGui::GetMousePos();
        bool anyModal = ImGui::IsPopupOpen((const char*)NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || (g_shell_ptr && g_shell_ptr->settings_modal_open);
        bool rowHovered = !anyModal && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows) && (mp.x >= rowMin.x && mp.x <= rowMax.x && mp.y >= rowMin.y && mp.y <= rowMax.y);

        // Hover font scale
        if (rowHovered) ImGui::SetWindowFontScale(1.05f);

        // Click to copy (only in the text area, not the button zone)
        float btnZoneX = startPos.x + rowWidth - (btnSz + 4.0f) * 2 - 8.0f;
        if (rowHovered && !value.empty() && mp.x < btnZoneX && ImGui::IsMouseClicked(0))
        {
            ImGui::SetClipboardText(value.c_str());
            TriggerCopyFlash();
            ShowToast("Copied", ToastType::Success);
        }

        DrawRowIcon(ICON_MDI_EMAIL);
        ImGui::SameLine(iconColW);

        const char* display = value.empty() ? "\xe2\x80\x94"
                           : display_override ? display_override
                           : value.c_str();
        TextEllipsisClipped(display, valueColW);

        if (rowHovered) ImGui::SetWindowFontScale(1.0f);

        // Floating buttons on hover: [Open Inbox] [Copy]
        if (rowHovered && !value.empty())
        {
            std::string webmail = GetWebmailUrl(value);
            float rightX = startPos.x + rowWidth - 8.0f;

            // Copy button (rightmost)
            ImGui::SetCursorScreenPos(ImVec2(rightX - btnSz, rowY));
            if (IconFloatingBtn("copy", ICON_MDI_CONTENT_COPY, "Copy", btnSz))
            {
                ImGui::SetClipboardText(value.c_str());
                TriggerCopyFlash();
                ShowToast("Copied", ToastType::Success);
            }

            // Open inbox button (left of copy)
            if (!webmail.empty())
            {
                ImGui::SetCursorScreenPos(ImVec2(rightX - btnSz * 2 - 4.0f, rowY));
                if (IconFloatingBtn("inbox", ICON_MDI_OPEN_IN_NEW, "Open inbox", btnSz))
                {
                    helpers::open_website(webmail);
                }
            }

        }

        ImGui::SetCursorScreenPos(ImVec2(startPos.x, rowY + ImGui::GetTextLineHeight()));
        ImGui::Dummy(ImVec2(0, rowSpacing));
        ImGui::PopID();
    }

    // Masked copy row: shows masked value, reveals on hover
    void DrawMaskedCopyRow(
        const char* id,
        const char* icon,
        const std::string& value,
        int reveal_last,        // show last N chars, rest masked (0 = fully masked)
        float iconColW,
        float valueColW,
        float rowWidth,
        float btnSz,
        float rowSpacing)
    {
        ImGui::PushID(id);

        ImVec2 startPos = ImGui::GetCursorScreenPos();
        float rowY = startPos.y;
        float rowH = ImGui::GetTextLineHeight() + rowSpacing;
        ImVec2 rowMin(ImGui::GetWindowPos().x, rowY);
        ImVec2 rowMax(rowMin.x + rowWidth, rowY + rowH);

        DrawFieldHighlight(ImGui::GetWindowDrawList(), rowMin.x, rowY, rowWidth, rowH);

        ImVec2 mp = ImGui::GetMousePos();
        bool anyModal = ImGui::IsPopupOpen((const char*)NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || (g_shell_ptr && g_shell_ptr->settings_modal_open);
        bool rowHovered = !anyModal && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows) && (mp.x >= rowMin.x && mp.x <= rowMax.x && mp.y >= rowMin.y && mp.y <= rowMax.y);

        if (rowHovered) ImGui::SetWindowFontScale(1.05f);

        float btnZoneX = startPos.x + rowWidth - btnSz - 8.0f;
        if (rowHovered && !value.empty() && mp.x < btnZoneX && ImGui::IsMouseClicked(0))
        {
            ImGui::SetClipboardText(value.c_str());
            TriggerCopyFlash();
            ShowToast("Copied", ToastType::Success);
        }

        DrawRowIcon(icon);
        ImGui::SameLine(iconColW);

        // Build masked display
        std::string display;
        if (value.empty()) {
            display = "\xe2\x80\x94"; // em dash
        } else if (rowHovered) {
            display = value; // reveal on hover
        } else {
            int len = (int)value.size();
            int show = (reveal_last > 0 && reveal_last < len) ? reveal_last : 0;
            int hide = len - show;
            display.clear();
            for (int i = 0; i < hide; i++) display += "\xe2\x80\xa2"; // bullet
            if (show > 0) display += value.substr(len - show);
        }
        TextEllipsisClipped(display.c_str(), valueColW);

        if (rowHovered) ImGui::SetWindowFontScale(1.0f);

        // Floating icon button (absolute positioned, no chrome)
        if (rowHovered && !value.empty())
        {
            ImVec2 btnPos(startPos.x + rowWidth - btnSz - 8.0f, rowY);
            ImGui::SetCursorScreenPos(btnPos);
            if (IconFloatingBtn("copy", ICON_MDI_CONTENT_COPY, "Copy", btnSz))
            {
                ImGui::SetClipboardText(value.c_str());
                TriggerCopyFlash();
                ShowToast("Copied", ToastType::Success);
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(startPos.x, rowY + ImGui::GetTextLineHeight()));
        ImGui::Dummy(ImVec2(0, rowSpacing));
        ImGui::PopID();
    }

    void DrawBadgeChip(ImDrawList* dl, const ImVec2& bMin, const ImVec2& bMax, ImU32 bg, const char* text, ImU32 textCol)
    {
        dl->AddRectFilled(bMin, bMax, bg, 8.0f);
        dl->AddText(ImVec2(bMin.x + 6.0f, bMin.y + 1.0f), textCol, text);
    }

    // Draw security warning badges (weak/reused), returns updated rightX
    float DrawSecurityBadges(ImDrawList* dl, const ShellState* shell,
        int credId, float rightX, float centerY, float chipGap)
    {
        if (!shell) return rightX;

        auto draw_icon = [&](const char* icon, ImU32 col) {
            ImVec2 sz = ImGui::CalcTextSize(icon);
            rightX -= sz.x;
            dl->AddText(ImVec2(rightX, centerY), col, icon);
            rightX -= chipGap;
        };

        if (shell->sec_highlight_weak && shell->sec_weak_ids.count(credId))
            draw_icon(ICON_MDI_ALERT, colors::StatusWeak);

        if (shell->sec_highlight_reused && shell->sec_reused_ids.count(credId))
            draw_icon(ICON_MDI_REPEAT, colors::StatusReused);

        if (shell->sec_highlight_exposed && shell->sec_exposed_ids.count(credId))
            draw_icon(ICON_MDI_EARTH, colors::StatusExposed);

        if (shell->sec_highlight_aging && shell->sec_aging_ids.count(credId))
            draw_icon(ICON_MDI_CLOCK_ALERT, colors::StatusAging);

        return rightX;
    }

    // Hint line for security context (icon + muted text)
    void DrawHintLine(const char* icon, const char* text, ImU32 iconCol)
    {
        ImGui::PushStyleColor(ImGuiCol_Text, iconCol);
        ImGui::TextUnformatted(icon);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 4);
        ImGui::TextDisabled("%s", text);
    }

    // ---- Popup tab bar (underline style) ----
    void DrawPopupTabBar(int credId, bool hasSecurityTab, int& currentTab)
    {
        const char* labels[] = { "Details", "Notes", "Security" };
        int numTabs = hasSecurityTab ? 3 : 2;
        const bool dk = IsDarkTheme();
        const float tabH = 24.0f;
        const float padX = 2.0f;
        const float gap = 2.0f;
        const float lineThick = 2.0f;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Measure each tab to text width
        float tabW[3] = {};
        float totalW = 0.0f;
        for (int i = 0; i < numTabs; i++) {
            tabW[i] = ImGui::CalcTextSize(labels[i]).x + padX * 2.0f;
            totalW += tabW[i];
        }
        totalW += gap * (numTabs - 1);

        ImVec2 origin = ImGui::GetCursorScreenPos();
        float curX = origin.x;

        // Bottom border line
        ImU32 borderCol = dk ? IM_COL32(255, 255, 255, 20) : IM_COL32(0, 0, 0, 15);
        dl->AddLine(ImVec2(origin.x, origin.y + tabH), ImVec2(origin.x + totalW, origin.y + tabH), borderCol, 1.0f);

        for (int i = 0; i < numTabs; i++)
        {
            bool active = (currentTab == i);
            float w = tabW[i];
            ImVec2 rMin(curX, origin.y);
            ImVec2 rMax(curX + w, origin.y + tabH);

            ImGui::SetCursorScreenPos(rMin);
            ImGui::PushID(i);
            ImGui::InvisibleButton("##ptab", ImVec2(w, tabH));
            bool hov = ImGui::IsItemHovered();
            if (ImGui::IsItemClicked()) currentTab = i;
            ImGui::PopID();

            if (hov && !active)
                dl->AddRectFilled(rMin, rMax, dk ? IM_COL32(255, 255, 255, 8) : IM_COL32(0, 0, 0, 6), 4.0f);

            ImVec2 tSz = ImGui::CalcTextSize(labels[i]);
            ImU32 tCol = active ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
            dl->AddText(ImVec2(curX + padX, origin.y + (tabH - tSz.y) * 0.5f), tCol, labels[i]);

            if (active)
                dl->AddRectFilled(
                    ImVec2(curX, origin.y + tabH - lineThick),
                    ImVec2(curX + w, origin.y + tabH),
                    ImGui::GetColorU32(colors::SecondColor), 1.0f);

            curX += w + gap;
        }

        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + tabH + 4.0f));
    }

    bool IconSquareBtn(const char* id, const char* icon, const char* tip,
        float sz, bool active, bool danger, ImU32 activeIconCol)
    {
        ImGui::PushID(id);

        bool pressed = ImGui::InvisibleButton("##sq", ImVec2(sz, sz));
        bool hovered = ImGui::IsItemHovered();
        bool held = ImGui::IsItemActive();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pmin = ImGui::GetItemRectMin();
        ImVec2 pmax = ImGui::GetItemRectMax();
        ImVec2 center((pmin.x + pmax.x) * 0.5f, (pmin.y + pmax.y) * 0.5f);
        const float rounding = 6.0f;
        const bool dark = IsDarkTheme();

        // SoftBevel face
        ImU32 faceBg;
        if (held)
            faceBg = theme::ToggleHeld;
        else if (hovered)
            faceBg = theme::ToggleHovered;
        else
            faceBg = theme::ToggleNormal;

        ImU32 shadowCol = theme::ToggleShadow;
        ImU32 hlCol = hovered ? theme::ToggleGlintHov : theme::ToggleGlintNorm;

        // Splitter: lips behind main body
        ImDrawListSplitter splitter;
        splitter.Split(dl, 2);
        splitter.SetCurrentChannel(dl, 1);

        // Main body
        dl->AddRectFilled(pmin, pmax, faceBg, rounding);

        // Bottom lip shadow
        {
            const float lipOff = 1.5f;
            float clipTop = pmin.y + (sz * 0.6f);
            splitter.SetCurrentChannel(dl, 0);
            dl->PushClipRect(ImVec2(pmin.x - 2, clipTop), ImVec2(pmax.x + 2, pmax.y + lipOff + 2), true);
            dl->AddRectFilled(ImVec2(pmin.x, pmin.y + lipOff), ImVec2(pmax.x, pmax.y + lipOff), shadowCol, rounding);
            dl->PopClipRect();
            splitter.SetCurrentChannel(dl, 1);
        }

        // Top highlight
        {
            const float inset = 1.0f;
            float clipBot = pmin.y + (sz * 0.35f);
            float hlR = rounding - inset;
            if (hlR < 0) hlR = 0;
            dl->PushClipRect(ImVec2(pmin.x + inset - 1, pmin.y + inset - 1), ImVec2(pmax.x - inset + 1, clipBot), true);
            dl->AddRect(ImVec2(pmin.x + inset, pmin.y + inset), ImVec2(pmax.x - inset, pmax.y - inset), hlCol, hlR, 0, 1.0f);
            dl->PopClipRect();
        }

        splitter.Merge(dl);

        // Icon centered
        ImU32 iconCol;
        if (danger)
            iconCol = IM_COL32(220, 80, 80, 255);
        else if (active && activeIconCol != 0)
            iconCol = activeIconCol;
        else
            iconCol = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);

        ImVec2 ts = ImGui::CalcTextSize(icon);
        dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), iconCol, icon);

        if (tip && ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
        {
            ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
            SetTooltipPadded("%s", tip);
            ImGui::PopStyleColor();
        }

        ImGui::PopID();

        return pressed;
    }

    // Icon for Credential type
    const char* CredTypeIcon(CredType t)
    {
        switch (t) {
        case CredType::CreditCard: return ICON_MDI_CREDIT_CARD;
        case CredType::Identity:   return ICON_MDI_CARD_ACCOUNT_DETAILS;
        case CredType::SecureNote: return ICON_MDI_NOTE_TEXT;
        default:                   return ICON_MDI_KEY;
        }
    }

    // TOTP row: [Clock] 042 837 (23s) [Copy on hover]
    void DrawTOTPRow(
        const char* id,
        const std::string& totp_secret,
        float iconColW,
        float valueColW,
        float rowWidth,
        float btnSz,
        float rowSpacing)
    {
        if (totp_secret.empty()) return;

        auto bytes = totp::base32_decode(totp_secret);
        if (bytes.size() < 10) return;

        std::string code = totp::generate_code_now(bytes);
        int secs = totp::seconds_remaining_now();

        // Format code with space in middle: "042 837"
        std::string display_code;
        if (code.size() == 6)
            display_code = code.substr(0, 3) + " " + code.substr(3);
        else
            display_code = code;

        char display[64];
        snprintf(display, sizeof(display), "%s  (%ds)", display_code.c_str(), secs);

        ImGui::PushID(id);

        ImVec2 startPos = ImGui::GetCursorScreenPos();
        float rowY = startPos.y;
        float rowH = ImGui::GetTextLineHeight() + rowSpacing;
        ImVec2 rowMin(ImGui::GetWindowPos().x, rowY);
        ImVec2 rowMax(rowMin.x + rowWidth, rowY + rowH);

        DrawFieldHighlight(ImGui::GetWindowDrawList(), rowMin.x, rowY, rowWidth, rowH);

        ImVec2 mp = ImGui::GetMousePos();
        bool anyModal = ImGui::IsPopupOpen((const char*)NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || (g_shell_ptr && g_shell_ptr->settings_modal_open);
        bool rowHovered = !anyModal && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows) && (mp.x >= rowMin.x && mp.x <= rowMax.x && mp.y >= rowMin.y && mp.y <= rowMax.y);

        if (rowHovered) ImGui::SetWindowFontScale(1.05f);

        // Click to copy code
        float btnZoneX = startPos.x + rowWidth - btnSz - 8.0f;
        if (rowHovered && mp.x < btnZoneX && ImGui::IsMouseClicked(0))
        {
            ImGui::SetClipboardText(code.c_str());
            ShowToast("TOTP copied", ToastType::Success);
        }

        // Color: green when >5s, red when <=5s
        ImVec4 col = (secs > 5) ? colors::Green : colors::Red;

        ImGui::TextDisabled(ICON_MDI_CLOCK);
        ImGui::SameLine(iconColW);
        ImGui::TextUnformatted(display_code.c_str());
        ImGui::SameLine(0, 6);
        ImGui::TextColored(col, "(%ds)", secs);

        if (rowHovered) ImGui::SetWindowFontScale(1.0f);

        // Floating copy icon on hover
        if (rowHovered)
        {
            ImVec2 btnPos(startPos.x + rowWidth - btnSz - 8.0f, rowY);
            ImGui::SetCursorScreenPos(btnPos);
            if (IconFloatingBtn("copy_totp", ICON_MDI_CONTENT_COPY, "Copy TOTP", btnSz))
            {
                ImGui::SetClipboardText(code.c_str());
                ShowToast("TOTP copied", ToastType::Success);
            }
        }

        ImGui::SetCursorScreenPos(ImVec2(startPos.x, rowY + ImGui::GetTextLineHeight()));
        ImGui::Dummy(ImVec2(0, rowSpacing));
        ImGui::PopID();
    }

    // Forward declaration for re-prompt pending Credential ID
    static int s_reprompt_pending_cred_id = -1;

    // Password row: [Key] •••••••• [Eye] [Copy] (buttons on hover)
    void DrawPasswordRow(
        const char* id,
        int credId,
        GetPasswordFn get_password_fn,
        const std::vector<PwHistoryEntry>& password_history,
        std::set<int>& visiblePasswords,
        float iconColW,
        float valueColW,
        float rowWidth,
        float btnSz,
        float rowSpacing)
    {
        ImGui::PushID(id);

        ImVec2 startPos = ImGui::GetCursorScreenPos();
        float rowY = startPos.y;
        float rowH = ImGui::GetTextLineHeight() + rowSpacing;
        ImVec2 rowMin(ImGui::GetWindowPos().x, rowY);
        ImVec2 rowMax(rowMin.x + rowWidth, rowY + rowH);

        DrawFieldHighlight(ImGui::GetWindowDrawList(), rowMin.x, rowY, rowWidth, rowH);

        ImVec2 mp = ImGui::GetMousePos();
        bool anyModal = ImGui::IsPopupOpen((const char*)NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || (g_shell_ptr && g_shell_ptr->settings_modal_open);
        bool rowHovered = !anyModal && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_ChildWindows) && (mp.x >= rowMin.x && mp.x <= rowMax.x && mp.y >= rowMin.y && mp.y <= rowMax.y);

        bool pwVisible = visiblePasswords.count(credId) > 0;
        const char* pw = get_password_fn ? get_password_fn(credId) : "";
        bool hasPw = pw && pw[0];

        // Hover font scale
        if (rowHovered) ImGui::SetWindowFontScale(1.05f);

        // Click to copy (only in the text area, not the button zone)
        float btnZoneX = startPos.x + rowWidth - btnSz * 2 - 12.0f;
        if (rowHovered && hasPw && mp.x < btnZoneX && ImGui::IsMouseClicked(0))
        {
            ClipboardCopyPassword(pw);
            TriggerCopyFlash();
        }

        DrawRowIcon(ICON_MDI_KEY);
        ImGui::SameLine(iconColW);

        std::string display = pwVisible && hasPw ? pw : "••••••••";
        TextEllipsisClipped(display.c_str(), valueColW);

        if (rowHovered) ImGui::SetWindowFontScale(1.0f);

        // Check if re-prompt was approved for this Credential
        if (IsRepromptApproved(RepromptAction::RevealPassword) && s_reprompt_pending_cred_id == credId)
        {
            visiblePasswords.insert(credId);
            ClearRepromptApproval(RepromptAction::RevealPassword);
            s_reprompt_pending_cred_id = -1;
        }

        // Floating icon buttons (absolute positioned, no chrome)
        if (rowHovered)
        {
            float btnX = startPos.x + rowWidth - btnSz * 2 - 12.0f;
            ImGui::SetCursorScreenPos(ImVec2(btnX, rowY));
            if (IconFloatingBtn("toggle", pwVisible ? ICON_MDI_EYE_OFF : ICON_MDI_EYE,
                pwVisible ? "Hide" : "Show", btnSz))
            {
                if (pwVisible)
                {
                    visiblePasswords.erase(credId);
                }
                else
                {
                    s_reprompt_pending_cred_id = credId;
                    if (RequestReprompt(RepromptAction::RevealPassword, ""))
                    {
                        visiblePasswords.insert(credId);
                        s_reprompt_pending_cred_id = -1;
                    }
                }
            }

            if (hasPw)
            {
                ImGui::SetCursorScreenPos(ImVec2(btnX + btnSz + 4.0f, rowY));
                if (IconFloatingBtn("copy", ICON_MDI_CONTENT_COPY, "Copy", btnSz))
                {
                    ClipboardCopyPassword(pw);
                    TriggerCopyFlash();
                }
            }
        }

        // Right-click: password history popup (InvisibleButton for reliable hit-testing in child windows)
        if (!password_history.empty())
        {
            ImGui::SetCursorScreenPos(rowMin);
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("##pw_row_ctx", ImVec2(rowMax.x - rowMin.x, rowH));

            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(8, 6));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(8, 6));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, IsDarkTheme() ? theme::PopupBg.dark : theme::PopupBg.light);
            ImGui::PushStyleColor(ImGuiCol_Border,  IsDarkTheme() ? theme::PopupBorder.dark : theme::PopupBorder.light);
            if (ImGui::BeginPopupContextItem("##pw_history_ctx"))
            {
                PopupStyleBegin();
                ImGui::TextDisabled(ICON_MDI_HISTORY " Password History");
                ImGui::Separator();

                for (int i = 0; i < (int)password_history.size(); i++)
                {
                    const auto& entry = password_history[i];
                    std::string dateStr = FormatUnixMsDateLabel(entry.changed_at_ms);

                    char label[256];
                    snprintf(label, sizeof(label), "%s  \xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2##hist_%d", dateStr.c_str(), i);

                    if (BarMenuItem(label, false))
                    {
                        ClipboardCopyPassword(entry.password.c_str());
                        ShowToast("Copied previous password", ToastType::Success);
                    }

                    if (ImGui::IsItemHovered())
                    {
                        SetTooltipPadded("%s", entry.password.c_str());
                    }
                }

                PopupStyleEnd();
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(5);
        }

        ImGui::SetCursorScreenPos(ImVec2(startPos.x, rowY + ImGui::GetTextLineHeight()));
        ImGui::Dummy(ImVec2(0, rowSpacing));
        ImGui::PopID();
    }


    static std::unordered_set<uint64_t> g_revealed_fields; // tracks revealed field keys

    bool DrawCopyFieldRow(
        const char* id,
        const char* label,
        const std::string& value,
        const std::string& display_value,
        uint64_t rowKey,
        int fieldIndex,
        float rowWidth,
        bool& interaction_consumed)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (!window || window->SkipItems)
            return false;

        const float rowH = 20.0f;
        const float padX = 10.0f;
        const float padY = 6.0f;
        const float eyeW = 20.0f;
        const float rightPad = 8.0f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImRect rowRect(pos, ImVec2(pos.x + rowWidth, pos.y + rowH));
        const bool rowHovered = !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId) &&
            ImGui::IsMouseHoveringRect(rowRect.Min, rowRect.Max);

        DrawFieldHighlight(dl, rowRect.Min.x, rowRect.Min.y, rowWidth, rowH);

        const ImU32 bg = ImGui::GetColorU32(GetItemBg());
        const ImU32 bgHover = ImGui::GetColorU32(GetItemHoverBg());
        const ImU32 borderCol = ImGui::GetColorU32(colors::Trans);
        dl->AddRectFilled(rowRect.Min, rowRect.Max, rowHovered ? bgHover : bg, 6.0f);
        dl->AddRect(rowRect.Min, rowRect.Max, borderCol, 6.0f);

        ImRect eyeRect(
            ImVec2(rowRect.Max.x - rightPad - eyeW, rowRect.Min.y),
            ImVec2(rowRect.Max.x - rightPad, rowRect.Max.y)
        );
        ImRect mainRect(rowRect.Min, ImVec2(eyeRect.Min.x, rowRect.Max.y));

        ImGui::PushID(id);

        // Reveal toggle
        const uint64_t revealKey = MakeCopyKey(rowKey, fieldIndex + 100);
        bool revealed = g_revealed_fields.count(revealKey) > 0;

        ImGui::SetCursorScreenPos(eyeRect.Min);
        ImGui::InvisibleButton("##eye", eyeRect.GetSize());
        const bool eye_clicked = ImGui::IsItemClicked();
        const bool eye_hovered = ImGui::IsItemHovered();
        if (eye_hovered || eye_clicked)
            interaction_consumed = true;
        if (eye_clicked)
        {
            if (revealed) g_revealed_fields.erase(revealKey);
            else          g_revealed_fields.insert(revealKey);
            revealed = !revealed;
        }

        // Main area (click to copy)
        ImGui::SetCursorScreenPos(mainRect.Min);
        ImGui::InvisibleButton("##copy_area", mainRect.GetSize());
        const bool copy_clicked = ImGui::IsItemClicked();
        const bool copy_hovered = ImGui::IsItemHovered();

        if (copy_hovered || copy_clicked)
            interaction_consumed = true;

        const uint64_t flashKey = MakeCopyKey(rowKey, fieldIndex);
        if (copy_clicked)
        {
            ImGui::SetClipboardText(value.c_str());
            g_copy_flash[flashKey] = (float)ImGui::GetTime();
            ShowToast("Copied", ToastType::Success);
        }

        // Show actual value when revealed, label/masked otherwise
        const std::string display = value.empty() ? "-" : (revealed ? value : display_value);

        ImVec2 text_sz = ImGui::CalcTextSize(display.c_str());
        float textY = rowRect.Min.y + (rowH - text_sz.y) * 0.5f;

        ImRect valueClip(
            ImVec2(rowRect.Min.x + padX, rowRect.Min.y),
            ImVec2(eyeRect.Min.x - 4.0f, rowRect.Max.y)
        );

        // Flash text green on copy
        bool copied = false;
        const float now = (float)ImGui::GetTime();
        auto it = g_copy_flash.find(flashKey);
        if (it != g_copy_flash.end())
            copied = (now - it->second) < 0.8f;

        ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
        if (copied)
        {
            float elapsed = now - it->second;
            float flash = 1.0f - elapsed / 0.8f;
            ImVec4 green(0.39f, 0.90f, 0.47f, 1.0f);
            ImVec4 normal = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            textCol = ImGui::ColorConvertFloat4ToU32(ImLerp(normal, green, flash));
        }

        ImGui::PushStyleColor(ImGuiCol_Text, textCol);
        ImGui::RenderTextClipped(
            ImVec2(rowRect.Min.x + padX, textY),
            ImVec2(valueClip.Max.x, rowRect.Max.y),
            display.c_str(), nullptr, &text_sz,
            ImVec2(0.0f, 0.0f),
            &valueClip
        );
        ImGui::PopStyleColor();

        // Eye icon + tooltip
        {
            dl->AddRectFilled(eyeRect.Min, eyeRect.Max, (rowHovered || eye_hovered) ? bgHover : bg, 0.0f);
            const char* eyeLabel = revealed ? ICON_MDI_EYE : ICON_MDI_EYE_OFF;
            ImU32 eyeCol = ImGui::GetColorU32(eye_hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
            ImVec2 eyeSize = ImGui::CalcTextSize(eyeLabel);
            ImVec2 eyePos(
                eyeRect.Min.x + (eyeRect.GetWidth() - eyeSize.x) * 0.5f,
                eyeRect.Min.y + (eyeRect.GetHeight() - eyeSize.y) * 0.5f
            );
            dl->AddText(eyePos, eyeCol, eyeLabel);

            if (eye_hovered && !value.empty())
                SetTooltipPadded("%s", value.c_str());
        }

        ImGui::PopID();

        ImGui::SetCursorScreenPos(rowRect.Min);
        ImGui::Dummy(ImVec2(rowWidth, rowH));
        return copy_clicked;
    }

    static AccordionItemAction RenderAccordionItemDetailed(
        const AccordionItem& c,
        uint32_t activeVaultKey,
        GetPasswordFn get_password_fn,
        bool read_only,
        int displayIndex)
    {
        AccordionItemAction out{};
        out.id = c.id;

        const uint64_t rowKey = MakeRowKey(activeVaultKey, c.id);
        const bool sel = IsSelected(rowKey);
        const bool anySel = HasAnySelection(activeVaultKey);

        ImDrawList* dl = ImGui::GetWindowDrawList();

        // Layout constants
        const float maxCardW = 900.0f;
        const float rawAvailW = ImGui::GetContentRegionAvail().x - 32.f;
        const float availW = ImMin(rawAvailW, maxCardW);
        const float rounding = 0.0f;
        const float headerH = 50.0f;  // Taller header for two lines
        const float padX = 12.0f;

        const float cbColW = 28.0f;  // Checkbox gutter width
        const float cbPadL = 6.0f;   // Checkbox left padding

        // Colors (theme-aware)
        const ImU32 colCard = ImGui::GetColorU32(GetCardHeaderBg());
        const ImU32 colBorder = ImGui::GetColorU32(colors::Trans);

        ImVec2 rowMin = ImGui::GetCursorScreenPos();

        // ============================================================
        // CHECKBOX (left gutter)
        // ============================================================
        ImRect cardRect(
            ImVec2(rowMin.x + cbColW, rowMin.y),
            ImVec2(rowMin.x + availW, rowMin.y + headerH)
        );

        ImGui::PushID((void*)(uintptr_t)rowKey);

        // Show checkbox on hover or when any selection exists
        ImVec2 mp = ImGui::GetMousePos();
        const bool anyPopup = ImGui::IsPopupOpen((const char*)NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || (g_shell_ptr && g_shell_ptr->settings_modal_open);
        const bool rowHovered = !anyPopup && ImRect(rowMin, ImVec2(rowMin.x + availW, rowMin.y + headerH)).Contains(mp);
        const bool showCb = rowHovered || sel || anySel;

        if (showCb)
        {
            float cbY = rowMin.y + (headerH - 15.0f) * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(rowMin.x + cbPadL, cbY));
            bool cbVal = sel;
            if (ImGui::Checkbox2("", &cbVal))
            {
                ToggleSelected(rowKey);
                out.selection_toggled = true;
                out.selected_now = IsSelected(rowKey);
                out.interacted = true;
            }
        }

        // Check if card is open (need to know for shadow height)
        const bool open = (g_open.count(rowKey) != 0);

        // ============================================================
        // CARD SHADOW (multi-ring for subtle elevation)
        // ============================================================
        const float shadowOffset = 2.0f;  // Slight downward offset
        const ImVec2 shadowStart = ImVec2(cardRect.Min.x, cardRect.Min.y + shadowOffset);

        // Ring 1 (outermost, most transparent)
        dl->AddRectFilled(
            ImVec2(shadowStart.x - 4.0f, shadowStart.y - 4.0f),
            ImVec2(cardRect.Max.x + 4.0f, cardRect.Max.y + 4.0f),
            GetShadowColor(0, rowHovered),
            rounding
        );

        // Ring 2
        dl->AddRectFilled(
            ImVec2(shadowStart.x - 2.0f, shadowStart.y - 2.0f),
            ImVec2(cardRect.Max.x + 2.0f, cardRect.Max.y + 2.0f),
            GetShadowColor(1, rowHovered),
            rounding
        );

        // Ring 3
        dl->AddRectFilled(
            ImVec2(shadowStart.x - 1.0f, shadowStart.y - 1.0f),
            ImVec2(cardRect.Max.x + 1.0f, cardRect.Max.y + 1.0f),
            GetShadowColor(2, rowHovered),
            rounding
        );

        // Ring 4 (innermost, most opaque)
        dl->AddRectFilled(
            shadowStart,
            cardRect.Max,
            GetShadowColor(3, rowHovered),
            rounding
        );

        // ============================================================
        // CARD HEADER (background + click handler)
        // ============================================================
        dl->AddRectFilled(cardRect.Min, cardRect.Max, colCard, rounding);
        if (c.is_changed)
            dl->AddRectFilled(cardRect.Min, cardRect.Max,
                (c.changed_fields & FCF_IsNew) ? kNewRowTint : kChangedRowTint, rounding);
        dl->AddRect(cardRect.Min, cardRect.Max, colBorder, rounding, 0, 1.0f);

        // Click to toggle expand
        ImGui::SetCursorScreenPos(cardRect.Min);
        ImGui::InvisibleButton("##acc_header_btn", cardRect.GetSize());

        if (ImGui::IsItemActivated())
        {
            g_hover_opened.erase(rowKey); // click-opened items persist
            if (g_open.count(rowKey)) {
                g_open.erase(rowKey);
                g_notes_visible.erase(rowKey);
                if (g_card_body_h.count(rowKey)) g_card_closing.insert(rowKey);
            }
            else {
                g_open.insert(rowKey);
                g_scroll_to_open[rowKey] = 2;  // Wait 2 frames for body to fully render
            }
            out.interacted = true;
        }

        // Hover-expand: open on header hover (close is deferred until after body render)
        if (g_shell_ptr && g_shell_ptr->hover_expand)
        {
            if (rowHovered && !g_open.count(rowKey))
            {
                g_open.insert(rowKey);
                g_hover_opened.insert(rowKey);
            }
        }

        // Right-click context menu on header (only if no other popup is open)
        bool detailedCtxAllowed = !ImGui::IsPopupOpen((const char*)NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)
                               || ImGui::IsPopupOpen("##detailed_row_ctx");
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(4, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(6, 2));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IsDarkTheme() ? theme::PopupBg.dark : theme::PopupBg.light);
        ImGui::PushStyleColor(ImGuiCol_Border,  IsDarkTheme() ? theme::PopupBorder.dark : theme::PopupBorder.light);
        if (detailedCtxAllowed && ImGui::BeginPopupContextItem("##detailed_row_ctx"))
        {
            PopupStyleBegin();
            ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
            auto& cols = g_shell_ptr->detailed_header_cols;
            if (BarMenuItemToggle("Show Title",    &cols.title,    true, true)) cfg::set_detailed_header_columns(cols);
            if (BarMenuItemToggle("Show Username", &cols.username, true, true)) cfg::set_detailed_header_columns(cols);
            if (BarMenuItemToggle("Show Email",    &cols.email,    true, true)) cfg::set_detailed_header_columns(cols);
            if (BarMenuItemToggle("Show Date",     &cols.date,     true, true)) cfg::set_detailed_header_columns(cols);
            if (BarMenuItemToggle("Show Index",    &cols.index,    true, true)) cfg::set_detailed_header_columns(cols);
            if (BarMenuItemToggle("Show Pin/Fav",  &cols.pin_fav,  true, true)) cfg::set_detailed_header_columns(cols);
            ImGui::Separator();
            if (BarMenuItemToggle("Password Subtitle", &cols.sub_password, true, true)) cfg::set_detailed_header_columns(cols);
            if (BarMenuItemToggle("Card Subtitle",     &cols.sub_card,     true, true)) cfg::set_detailed_header_columns(cols);
            if (BarMenuItemToggle("Identity Subtitle", &cols.sub_identity, true, true)) cfg::set_detailed_header_columns(cols);
            if (BarMenuItemToggle("Note Subtitle",     &cols.sub_note,     true, true)) cfg::set_detailed_header_columns(cols);
            ImGui::PopItemFlag();
            PopupStyleEnd();
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(5);

        // ============================================================
        // TITLE + SUBTITLE + STATUS ICONS (PIN/FAV)
        // ============================================================
        const auto& cols = g_shell_ptr->detailed_header_cols;
        const char* title = (!c.title.empty()) ? c.title.c_str() : "(untitled)";
        const std::string lastEdited = FormatUnixMsDateOnly(c.updated_at_ms != 0 ? c.updated_at_ms : c.created_at_ms);

        const float chipGap = 6.0f;

        float rightX = cardRect.Max.x - padX;

        // Draw from right to left: Index number first (far right)
        if (cols.index && displayIndex >= 0)
        {
            char indexBuf[16];
            snprintf(indexBuf, sizeof(indexBuf), "#%d", displayIndex);
            ImVec2 tsz = ImGui::CalcTextSize(indexBuf);
            rightX -= tsz.x;
            ImVec2 indexPos(rightX, cardRect.Min.y + (headerH - ImGui::GetTextLineHeight()) * 0.5f);
            dl->AddText(indexPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), indexBuf);
            rightX -= chipGap;
        }

        // Pin/Fav icons (to the left of index)
        if (cols.pin_fav)
        {
            if (c.is_favorite)
            {
                const char* fav = ICON_MDI_HEART;
                ImVec2 sz = ImGui::CalcTextSize(fav);
                rightX -= sz.x;
                dl->AddText(ImVec2(rightX, cardRect.Min.y + (headerH - sz.y) * 0.5f),
                    GetFavoriteColor(), fav);
                rightX -= chipGap;
            }
            if (c.is_pinned)
            {
                const char* pin = ICON_MDI_PIN;
                ImVec2 sz = ImGui::CalcTextSize(pin);
                rightX -= sz.x;
                dl->AddText(ImVec2(rightX, cardRect.Min.y + (headerH - sz.y) * 0.5f),
                    ImGui::GetColorU32(colors::MainColor), pin);
                rightX -= chipGap;
            }
        }
        // Timer badge
        if (c.expires_at_ms != 0)
        {
            if (c.expires_at_ms < 0) {
                const char* ico = ICON_MDI_TIMER_SAND_COMPLETE;
                ImVec2 sz = ImGui::CalcTextSize(ico);
                rightX -= sz.x;
                dl->AddText(ImVec2(rightX, cardRect.Min.y + (headerH - sz.y) * 0.5f),
                    colors::ErrorText, ico);
                rightX -= chipGap;
            } else {
                int64_t remain_ms = c.expires_at_ms - helpers::now_unix_ms();
                if (remain_ms < 0) remain_ms = 0;
                char tbuf[32];
                if (remain_ms >= time_ms::DAY)
                    snprintf(tbuf, sizeof(tbuf), "%lldd", (long long)(remain_ms / time_ms::DAY));
                else if (remain_ms >= time_ms::HOUR)
                    snprintf(tbuf, sizeof(tbuf), "%lldh", (long long)(remain_ms / time_ms::HOUR));
                else if (remain_ms >= time_ms::MINUTE)
                    snprintf(tbuf, sizeof(tbuf), "%lldm", (long long)(remain_ms / time_ms::MINUTE));
                else
                    snprintf(tbuf, sizeof(tbuf), "<1m");
                char label[64];
                snprintf(label, sizeof(label), ICON_MDI_TIMER " %s", tbuf);
                ImVec2 sz = ImGui::CalcTextSize(label);
                rightX -= sz.x;
                dl->AddText(ImVec2(rightX, cardRect.Min.y + (headerH - sz.y) * 0.5f),
                    ImGui::GetColorU32(colors::Orange), label);
                rightX -= chipGap;
            }
        }

        // Security badges (weak/reused)
        rightX = DrawSecurityBadges(dl, g_shell_ptr, c.id, rightX,
            cardRect.Min.y + (headerH - ImGui::GetTextLineHeight()) * 0.5f, chipGap);

        // Chevron icon
        const char* chev = open ? ICON_MDI_CHEVRON_DOWN : ICON_MDI_CHEVRON_RIGHT;
        ImVec2 chevPos(cardRect.Min.x + padX, cardRect.Min.y + (headerH - ImGui::GetTextLineHeight()) * 0.5f);
        dl->AddText(chevPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), chev);

        // Type icon / favicon before title
        float textX = cardRect.Min.x + padX + 22.0f;
        {
            auto srv = favicon::Get(c.website);
            if (srv)
            {
                float icoSz = 24.0f;
                ImVec2 icoMin(textX, cardRect.Min.y + (headerH - icoSz) * 0.5f);
                ImU32 favBg = IsDarkTheme() ? IM_COL32(255,255,255,18) : IM_COL32(0,0,0,12);
                dl->AddRectFilled(ImVec2(icoMin.x - 3, icoMin.y - 3), ImVec2(icoMin.x + icoSz + 3, icoMin.y + icoSz + 3), favBg, 6.0f);
                dl->AddImage((ImTextureID)srv, icoMin, ImVec2(icoMin.x + icoSz, icoMin.y + icoSz));
                textX += icoSz + 10.0f;
            }
            else
            {
                const char* typeIco = CredTypeIcon(c.type);
                ImVec2 icoSz = ImGui::CalcTextSize(typeIco);
                dl->AddText(ImVec2(textX, cardRect.Min.y + (headerH - icoSz.y) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), typeIco);
                textX += icoSz.x + 10.0f;
            }
        }

        // Title (line 1) + subtitle (line 2), vertically centered
        float titleFontSize = render::FontBold ? render::FontBold->LegacySize : ImGui::GetTextLineHeight();
        float subFontSize = render::FontSmall ? render::FontSmall->LegacySize : ImGui::GetTextLineHeight();

        // Build subtitle per Credential type
        static std::string subBuf; // static to keep c_str() alive for draw
        const char* subText = nullptr;
        subBuf.clear();
        if (c.type == CredType::Password && cols.sub_password)
        {
            if (cols.email && !c.email.empty())
                subText = MaskIfPrivate(c.email) ? MaskIfPrivate(c.email) : c.email.c_str();
            else if (cols.username && !c.user.empty())
                subText = MaskIfPrivate(c.user) ? MaskIfPrivate(c.user) : c.user.c_str();
        }
        else if (c.type == CredType::CreditCard && cols.sub_card)
        {
            if (c.card_number.size() >= 4)
                subBuf = std::string("\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2 ") + c.card_number.substr(c.card_number.size() - 4);
            else if (!c.cardholder_name.empty())
                subBuf = MaskIfPrivate(c.cardholder_name) ? MaskIfPrivate(c.cardholder_name) : c.cardholder_name;
            if (!subBuf.empty()) subText = subBuf.c_str();
        }
        else if (c.type == CredType::Identity && cols.sub_identity)
        {
            if (!c.full_name.empty()) subText = MaskIfPrivate(c.full_name) ? MaskIfPrivate(c.full_name) : c.full_name.c_str();
        }
        else if (c.type == CredType::SecureNote && cols.sub_note)
        {
            if (cfg::get_reprompt_reveal_notes())
                subBuf = "Secure Note";
            else
                subBuf = c.notes.substr(0, 40);
            if (!subBuf.empty()) subText = subBuf.c_str();
        }

        const float gap = 2.0f;
        float blockH = titleFontSize + (subText ? gap + subFontSize : 0.0f);
        float blockY = cardRect.Min.y + (headerH - blockH) * 0.5f;

        // Line 1: title (or username if no title)
        if (cols.title)
        {
            dl->AddText(render::FontBold, titleFontSize, ImVec2(textX, blockY), ImGui::GetColorU32(ImGuiCol_Text), title);
        }
        else if (cols.username && !c.user.empty())
        {
            dl->AddText(render::FontSmall, subFontSize,
                ImVec2(textX, blockY), ImGui::GetColorU32(ImGuiCol_TextDisabled), c.user.c_str());
        }

        // Line 2: subtitle
        if (subText)
        {
            dl->AddText(render::FontSmall, subFontSize,
                ImVec2(textX, blockY + titleFontSize + gap),
                ImGui::GetColorU32(ImGuiCol_TextDisabled), subText);
        }

        // Date in center (date only, no time)
        if (cols.date && !lastEdited.empty())
        {
            ImVec2 size = ImGui::CalcTextSize(lastEdited.c_str());
            float centerX = cardRect.Min.x + (cardRect.GetWidth() * 0.55f) - (size.x * 0.5f);
            ImVec2 pos(centerX, cardRect.Min.y + (headerH - size.y) * 0.5f);
            dl->AddText(pos, ImGui::GetColorU32(ImGuiCol_TextDisabled), lastEdited.c_str());
        }

        // ============================================================
        // CARD BODY (when expanded) - same as simple rows
        // ============================================================
        const float dt_card = ImGui::GetIO().DeltaTime;
        float& storedH = g_card_body_h[rowKey];
        float& animH   = g_card_anim_h[rowKey];
        bool isClosing  = g_card_closing.count(rowKey) != 0;

        // If close was requested during measurement frame (no stored height yet), cancel it
        if (isClosing && storedH < 1.0f) {
            g_card_closing.erase(rowKey);
            isClosing = false;
        }

        bool renderBody = open || isClosing;

        // Advance animation
        if (open) {
            float targetH = (storedH > 0.0f) ? storedH : 0.0f;
            if (targetH > 0.0f) {
                float step = kCardAnimSpeed * dt_card;
                animH = ImMin(animH + step, targetH);
            }
        } else if (isClosing) {
            float step = kCardAnimSpeed * dt_card;
            animH = ImMax(animH - step, 0.0f);
            if (animH < 0.5f) {
                animH = 0.0f;
                g_card_closing.erase(rowKey);
                isClosing = false;
                renderBody = false;
            }
        }

        if (renderBody)
        {
            ImVec2 bodyStartPos = ImVec2(cardRect.Min.x, cardRect.Max.y);
            ImGui::SetCursorScreenPos(bodyStartPos);

            // Use channel splitting so shadow draws behind body content
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);  // Content channel

            ImU32 bodyBgColor = GetCardBodyBg();
            ImGui::PushStyleColor(ImGuiCol_ChildBg, bodyBgColor);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
            ImGui::PushStyleColor(ImGuiCol_Border, colBorder);

            float childWidth = cardRect.Max.x - cardRect.Min.x;

            bool firstOpen = (open && storedH < 1.0f);
            bool animating = !firstOpen && (ImAbs(animH - (open ? storedH : 0.0f)) > 0.5f);

            if (firstOpen)
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.0f);

            if (animating)
                ImGui::BeginChild("card_body", ImVec2(childWidth, animH), ImGuiChildFlags_None, ImGuiWindowFlags_NoScrollbar);
            else
                ImGui::BeginChild("card_body", ImVec2(childWidth, 0), ImGuiChildFlags_AutoResizeY);

            ImGui::Dummy(ImVec2(0, 12));

            const float bodyPadX = 16.0f;
            ImGui::Indent(bodyPadX);

            float contentW = ImGui::GetContentRegionAvail().x;
            const float columnGap = 16.0f;
            const float leftColW = contentW * 0.45f;  // 45% for info
            const float rightColW = contentW - leftColW - columnGap;  // 55% for security/notes
            const float labelW = 90.0f;
            const float rowSpacing = 6.0f;

            // LEFT COLUMN: Action buttons + info rows
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetColorU32(colors::Trans));

            ImGui::BeginChild("##left_col", ImVec2(leftColW, 0), ImGuiChildFlags_AutoResizeY, ImGuiWindowFlags_NoScrollbar);
            ImGui::PushItemWidth(leftColW - 20.0f);

            // Action buttons at top of left column
            {
                const float ibSz = 26.0f;
                const float ibGap = 4.0f;
                if (!read_only)
                {
                    if (IconSquareBtn("##ab_pin_d", c.is_pinned ? ICON_MDI_PIN : ICON_MDI_PIN, c.is_pinned ? "Unpin" : "Pin", ibSz, c.is_pinned, false, ImGui::GetColorU32(colors::MainColor)))
                    { out.toggle_pin_requested = true; out.interacted = true; }
                    ImGui::SameLine(0, ibGap);
                    if (IconSquareBtn("##ab_fav_d", c.is_favorite ? ICON_MDI_HEART : ICON_MDI_HEART, c.is_favorite ? "Unfavorite" : "Favorite", ibSz, c.is_favorite, false, GetFavoriteColor()))
                    { out.toggle_fav_requested = true; out.interacted = true; }
                    ImGui::SameLine(0, ibGap);
                }
                if (!read_only)
                {
                    ImGui::SameLine(0, ibGap);
                    if (IconSquareBtn("##ab_edit_d", ICON_MDI_PENCIL, "Edit", ibSz))
                    { out.edit_open_requested = true; out.interacted = true; }

                    // Delete — right-aligned on same line
                    const float delW = 95.0f;
                    const float delH = ibSz;
                    ImGui::SameLine();
                    ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - delW);
                    if (HoldToActionButton("##hold_delete_detailed", ICON_MDI_DELETE " Delete", 1.0f, ImVec2(delW, delH), 6.0f))
                    { out.requested_delete = true; out.interacted = true; }
                }
                ImGui::Dummy(ImVec2(0, 4));
            }

            static std::set<int> visiblePasswordsDetailed;

            const float iconBtnSz = 16.0f;
            const float iconColW = 30.0f;
            const float rowSpacingD = 6.0f;

            {
                const float cBoxPad = 10.0f;
                const bool isNew = !!(c.changed_fields & FCF_IsNew);
                bool dk = IsDarkTheme();

                LiftedChildColorSet lc;
                lc.bg          = dk ? theme::CardSurface.dark  : theme::CardSurface.light;
                lc.borderOuter = dk ? theme::CardBorderOuter.dark      : theme::CardBorderOuter.light;
                lc.borderInner = dk ? theme::CardBorderInner.dark : theme::CardBorderInner.light;
                SetLiftedChildColors(lc);

                if (BeginLiftedChild("##det_info", ImVec2(ImGui::GetContentRegionAvail().x, 0), ImGuiWindowFlags_NoScrollbar))
                {
                    ImGui::Dummy(ImVec2(0, 6.0f));
                    ImGui::Indent(cBoxPad);

                    float innerW = ImGui::GetContentRegionAvail().x;
                    float innerValW = innerW - iconColW - 10.0f;

                    if (c.type == CredType::Password)
                    {
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_Website);
                        DrawWebsiteRow("website", c.website, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD);
                        DrawRowDivider(10.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_User);
                        DrawCopyRow("username", ICON_MDI_ACCOUNT, c.user, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD, MaskIfPrivate(c.user));
                        DrawRowDivider(10.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_Email);
                        DrawEmailRow("email", c.email, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD, MaskIfPrivate(c.email));
                        DrawRowDivider(10.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_Password);
                        DrawPasswordRow("password", c.id, get_password_fn, c.password_history, visiblePasswordsDetailed, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD);
                        if (!c.totp_secret.empty()) {
                            DrawRowDivider(10.0f);
                            g_field_accent_active = isNew || !!(c.changed_fields & FCF_TotpSecret);
                            DrawTOTPRow("totp", c.totp_secret, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD);
                        }
                    }
                    else if (c.type == CredType::CreditCard)
                    {
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_Cardholder);
                        DrawCopyRow("cardholder", ICON_MDI_ACCOUNT, c.cardholder_name, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD, MaskIfPrivate(c.cardholder_name));
                        DrawRowDivider(10.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardNumber);
                        DrawMaskedCopyRow("card_number", ICON_MDI_CREDIT_CARD, c.card_number, 4, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD);
                        DrawRowDivider(10.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardExpiry);
                        DrawCopyRow("expiry", ICON_MDI_CALENDAR, c.card_expiry, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD);
                        DrawRowDivider(10.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardCvv);
                        DrawMaskedCopyRow("cvv", ICON_MDI_LOCK, c.card_cvv, 0, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD);
                        if (!c.card_brand.empty()) {
                            DrawRowDivider(10.0f);
                            g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardBrand);
                            DrawInfoRow("brand", ICON_MDI_TAG, c.card_brand, iconColW, innerValW, rowSpacingD);
                        }
                        if (!c.card_address.empty()) {
                            DrawRowDivider(10.0f);
                            g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardAddress);
                            DrawCopyRow("card_addr", ICON_MDI_MAP_MARKER, c.card_address, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD, MaskIfPrivate(c.card_address));
                        }
                        if (!c.card_city.empty()) {
                            DrawRowDivider(10.0f);
                            g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardCity);
                            DrawInfoRow("card_city", ICON_MDI_CITY, c.card_city, iconColW, innerValW, rowSpacingD);
                        }
                        if (!c.card_postal_code.empty()) {
                            DrawRowDivider(10.0f);
                            g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardPostalCode);
                            DrawInfoRow("card_postal", ICON_MDI_MAILBOX, c.card_postal_code, iconColW, innerValW, rowSpacingD);
                        }
                    }
                    else if (c.type == CredType::Identity)
                    {
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_FullName);
                        DrawCopyRow("full_name", ICON_MDI_ACCOUNT, c.full_name, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD, MaskIfPrivate(c.full_name));
                        DrawRowDivider(10.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_IdNumber);
                        DrawCopyRow("id_number", ICON_MDI_POUND, c.id_number, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD, MaskIfPrivate(c.id_number));
                        DrawRowDivider(10.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_Phone);
                        DrawCopyRow("phone", ICON_MDI_PHONE, c.phone, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD, MaskIfPrivate(c.phone));
                        if (!c.address.empty()) {
                            DrawRowDivider(10.0f);
                            g_field_accent_active = isNew || !!(c.changed_fields & FCF_Address);
                            DrawCopyRow("address", ICON_MDI_MAP_MARKER, c.address, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD, MaskIfPrivate(c.address));
                        }
                    }
                    else if (c.type == CredType::SecureNote)
                    {
                        const uint64_t snRowKey = MakeRowKey(activeVaultKey, c.id);
                        const bool snLocked = cfg::get_reprompt_reveal_notes() && !g_notes_visible.count(snRowKey);

                        if (snLocked)
                            ImGui::BeginDisabled();

                        g_field_accent_active = false;
                        DrawCopyRow("copy_notes_d", ICON_MDI_CONTENT_COPY, c.notes, iconColW, innerValW, innerW, iconBtnSz, rowSpacingD, "Copy notes");

                        if (snLocked)
                            ImGui::EndDisabled();
                    }

                    if (!c.group.empty()) {
                        DrawRowDivider(10.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_Group);
                        DrawInfoRow("group", ICON_MDI_FOLDER, c.group, iconColW, innerValW, rowSpacingD);
                    }
                    g_field_accent_active = false;

                    ImGui::Dummy(ImVec2(0, 1.0f));
                    ImGui::Unindent(cBoxPad);
                }
                EndLiftedChild();
            }

            // Timestamps (single line)
            ImGui::Dummy(ImVec2(0, 4.0f));

            ImGui::PopItemWidth();
            ImGui::EndChild();
            const float leftColRenderedH_d = ImGui::GetItemRectSize().y;
            ImGui::PopStyleColor();

            // RIGHT COLUMN: Security / Notes Tabs
            ImGui::SameLine(0, columnGap);
            const float rcPadRight = 16.0f;
            ImGui::BeginGroup();

            {
                bool hasSecurityRC = (c.type == CredType::Password);
                bool hasNotesRC = true;

                static std::unordered_map<int, int> s_rc_tab_d;
                int& rcTab = s_rc_tab_d[c.id];
                if (!hasSecurityRC && rcTab == 0) rcTab = 1;

                // Tab bar
                {
                    const bool dark = IsDarkTheme();
                    const float tabH = 28.0f;
                    const float rounding = 8.0f;
                    const float pad_x = 6.0f;
                    const float gap = 4.0f;
                    const ImU32 mutedCol2 = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                    const ImU32 textCol  = ImGui::GetColorU32(ImGuiCol_Text);
                    const ImU32 pillBg   = dark ? theme::PillBg.dark  : theme::PillBg.light;
                    const ImU32 pillHov  = dark ? theme::PillHoverBg.dark  : theme::PillHoverBg.light;
                    const ImU32 lipCol2  = dark ? theme::PillInnerLip.dark   : theme::PillInnerLip.light;
                    const ImU32 divCol   = dark ? theme::PillDivider.dark  : theme::PillDivider.light;
                    ImDrawList* dl = ImGui::GetWindowDrawList();

                    struct TabEntry { const char* id; const char* icon; const char* label; int idx; };
                    TabEntry tabs[2];
                    int numTabs = 0;
                    if (hasSecurityRC)
                        tabs[numTabs++] = { "##rctab_sec_d", ICON_MDI_SHIELD_CHECK, "Security", 0 };
                    if (hasNotesRC)
                        tabs[numTabs++] = { "##rctab_notes_d", ICON_MDI_NOTE_TEXT, "Notes", 1 };

                    float regionW[2] = {};
                    for (int i = 0; i < numTabs; i++)
                    {
                        char full[128];
                        snprintf(full, sizeof(full), "%s %s", tabs[i].icon, tabs[i].label);
                        regionW[i] = ImGui::CalcTextSize(full).x + pad_x * 2.0f;
                    }

                    float totalW = regionW[0] + (numTabs > 1 ? regionW[1] : 0.0f);
                    ImVec2 pillPos = ImGui::GetCursorScreenPos();

                    const float lipOff = 1.5f;
                    dl->AddRectFilled(
                        ImVec2(pillPos.x, pillPos.y + lipOff),
                        ImVec2(pillPos.x + totalW, pillPos.y + tabH + lipOff),
                        lipCol2, rounding);
                    dl->AddRectFilled(
                        pillPos,
                        ImVec2(pillPos.x + totalW, pillPos.y + tabH),
                        pillBg, rounding);

                    float curX = pillPos.x;
                    for (int i = 0; i < numTabs; i++)
                    {
                        bool active = (rcTab == tabs[i].idx);
                        float w = regionW[i];
                        ImVec2 rMin(curX, pillPos.y);
                        ImVec2 rMax(curX + w, pillPos.y + tabH);

                        ImGui::SetCursorScreenPos(rMin);
                        ImGui::InvisibleButton(tabs[i].id, ImVec2(w, tabH));
                        bool hov = ImGui::IsItemHovered();
                        if (ImGui::IsItemClicked())
                            rcTab = tabs[i].idx;

                        {
                            ImDrawFlags rndFlags = 0;
                            if (i == 0) rndFlags = ImDrawFlags_RoundCornersLeft;
                            else if (i == numTabs - 1) rndFlags = ImDrawFlags_RoundCornersRight;
                            if (!active)
                            {
                                ImU32 inactiveFill = dark ? IM_COL32(0, 0, 0, 40) : IM_COL32(0, 0, 0, 18);
                                dl->AddRectFilled(rMin, rMax, inactiveFill, rounding, rndFlags);
                            }
                            if (hov && !active)
                                dl->AddRectFilled(rMin, rMax, pillHov, rounding, rndFlags);
                        }

                        {
                            char full[128];
                            snprintf(full, sizeof(full), "%s %s", tabs[i].icon, tabs[i].label);
                            ImVec2 sz = ImGui::CalcTextSize(full);
                            ImU32 col = active ? textCol : (hov ? textCol : mutedCol2);
                            dl->AddText(ImVec2(curX + pad_x, pillPos.y + (tabH - sz.y) * 0.5f), col, full);
                        }

                        curX += w;

                        // 3D divider between tabs
                        if (i < numTabs - 1)
                        {
                            ImU32 shineLine  = dark ? IM_COL32(255, 255, 255, 8) : IM_COL32(255, 255, 255, 80);
                            ImU32 shadowLine = dark ? theme::SplitterShadow.dark : theme::SplitterShadow.light;
                            // Flip divider based on which side is active
                            ImU32 leftLine  = active ? shineLine  : shadowLine;
                            ImU32 rightLine = active ? shadowLine : shineLine;
                            dl->AddLine(
                                ImVec2(curX, pillPos.y),
                                ImVec2(curX, pillPos.y + tabH + lipOff),
                                leftLine, 1.0f);
                            dl->AddLine(
                                ImVec2(curX + 1.0f, pillPos.y),
                                ImVec2(curX + 1.0f, pillPos.y + tabH + lipOff),
                                rightLine, 1.0f);
                            curX += 2.0f;
                        }
                    }

                    ImGui::SetCursorScreenPos(ImVec2(pillPos.x, pillPos.y + tabH + 4.0f));
                }

                // Tab content
                ImGui::Dummy(ImVec2(0, 3.0f));
                if (rcTab == 0 && hasSecurityRC && g_shell_ptr)
                {
                    DrawSecurityCard(c, *g_shell_ptr, activeVaultKey, get_password_fn, ImGui::GetContentRegionAvail().x - rcPadRight);
                    ImGui::Dummy(ImVec2(0, 5.0f));
                }
                else if (rcTab == 1)
                {
                    const float notesW = ImGui::GetContentRegionAvail().x - rcPadRight;
                    const uint64_t noteRowKey = MakeRowKey(activeVaultKey, c.id);
                    const bool noteRepromptActive = cfg::get_reprompt_reveal_notes();
                    const bool noteRevealed = g_notes_visible.count(noteRowKey) != 0;

                    if (g_notes_reprompt_pending == noteRowKey && IsRepromptApproved(RepromptAction::RevealNotes))
                    {
                        g_notes_visible.insert(noteRowKey);
                        ClearRepromptApproval(RepromptAction::RevealNotes);
                        g_notes_reprompt_pending = 0;
                    }

                    if (noteRepromptActive && !noteRevealed)
                    {
                        const bool dk = IsDarkTheme();
                        float frameH = 60.0f;
                        ImVec2 fpos = ImGui::GetCursorScreenPos();
                        ImDrawList* ndl = ImGui::GetWindowDrawList();
                        ndl->AddRectFilled(fpos, ImVec2(fpos.x + notesW, fpos.y + frameH),
                            dk ? IM_COL32(255,255,255,4) : IM_COL32(0,0,0,4), 6.0f);
                        ndl->AddRect(fpos, ImVec2(fpos.x + notesW, fpos.y + frameH),
                            dk ? IM_COL32(255,255,255,20) : IM_COL32(0,0,0,20), 6.0f);

                        const char* ico = ICON_MDI_LOCK;
                        const char* label = "Reveal notes";
                        ImVec2 icoSz = ImGui::CalcTextSize(ico);
                        ImVec2 lblSz = ImGui::CalcTextSize(label);
                        float totalW = icoSz.x + 4.0f + lblSz.x;
                        float cx = fpos.x + (notesW - totalW) * 0.5f;
                        float cy = fpos.y + (frameH - lblSz.y) * 0.5f;

                        ImGui::SetCursorScreenPos(ImVec2(cx, cy));
                        ImGui::InvisibleButton("##notes_reveal_blocker_d", ImVec2(totalW, lblSz.y));
                        bool hov = ImGui::IsItemHovered();
                        bool clk = ImGui::IsItemClicked();
                        ImU32 col = ImGui::GetColorU32(hov ? ImGuiCol_Text : ImGuiCol_TextDisabled);
                        ndl->AddText(ImVec2(cx, cy), col, ico);
                        ndl->AddText(ImVec2(cx + icoSz.x + 4.0f, cy), col, label);
                        if (hov)
                        {
                            float uy = cy + lblSz.y + 1.0f;
                            ndl->AddLine(ImVec2(cx + icoSz.x + 4.0f, uy), ImVec2(cx + totalW, uy), col, 1.0f);
                        }
                        if (clk)
                        {
                            g_notes_reprompt_pending = noteRowKey;
                            if (RequestReprompt(RepromptAction::RevealNotes, ""))
                            {
                                g_notes_visible.insert(noteRowKey);
                                g_notes_reprompt_pending = 0;
                            }
                        }
                        ImGui::SetCursorScreenPos(ImVec2(fpos.x, fpos.y + frameH));
                        ImGui::Dummy(ImVec2(0, 0));
                    }
                    else
                    {
                        ImGui::PushStyleColor(ImGuiCol_FrameBg, IsDarkTheme() ? IM_COL32(255,255,255,4) : IM_COL32(0,0,0,4));
                        ImGui::PushStyleColor(ImGuiCol_Border,  IsDarkTheme() ? IM_COL32(255,255,255,20) : IM_COL32(0,0,0,20));
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 10.0f));
                        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

                        std::string notesDisplay = c.notes.empty() ? "No notes." : c.notes;
                        ImVec2 fpad(12.0f, 10.0f);
                        float innerW = notesW - fpad.x * 2;
                        ImVec2 tSz = ImGui::CalcTextSize(notesDisplay.c_str(), nullptr, false, innerW);
                        float notesH = ImMax(tSz.y + fpad.y * 2, 40.0f);
                        InputTextMultilineString("##notes_view", &notesDisplay, ImVec2(notesW, notesH),
                            ImGuiInputTextFlags_ReadOnly);

                        ImGui::PopStyleVar(3);
                        ImGui::PopStyleColor(2);
                    }
                }
            }

            ImGui::EndGroup();

            ImGui::Unindent(bodyPadX);

            // Timestamps (full card width, below both columns)
            if (c.created_at_ms != 0 || c.updated_at_ms != 0)
            {
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::Indent(bodyPadX);
                ImGui::PushFont(render::FontSmall);
                std::string ts;
                if (c.created_at_ms != 0)
                    ts = "Created " + FormatUnixMsDateLabel(c.created_at_ms);
                if (c.updated_at_ms != 0)
                {
                    if (!ts.empty()) ts += "  \xc2\xb7  ";
                    ts += "Updated " + FormatUnixMsDateLabel(c.updated_at_ms);
                }
                ImGui::TextDisabled("%s", ts.c_str());
                ImGui::PopFont();
                ImGui::Unindent(bodyPadX);
            }

            ImGui::Dummy(ImVec2(0, 12));

            ImGui::EndChild();

            // Capture natural height
            float renderedH = ImGui::GetItemRectSize().y;
            if (firstOpen) {
                ImGui::PopStyleVar(); // Alpha
                storedH = renderedH;
                animH = 0.0f;
            } else if (open && !animating && renderedH > 1.0f) {
                storedH = renderedH;
                animH = renderedH;  // sync so close animation starts from correct height
            }

            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(2);

            if (!firstOpen)
            {
                // Auto-scroll if this item was just opened (wait for animation to finish)
                auto scroll_it = g_scroll_to_open.find(rowKey);
                if (scroll_it != g_scroll_to_open.end())
                {
                    if (!animating && --scroll_it->second <= 0)
                    {
                        if (g_shell_ptr && g_shell_ptr->autoscroll_enabled)
                            ImGui::SetScrollHereY(0.45f);
                        g_scroll_to_open.erase(scroll_it);
                    }
                }

                // Switch to background channel for shadow
                dl->ChannelsSetCurrent(0);

                // Body shadow — each ring starts below header shadow's corresponding extension
                ImVec2 bodyEndPos = ImGui::GetCursorScreenPos();
                ImVec2 fullCardMax = ImVec2(cardRect.Max.x, bodyEndPos.y);

                dl->AddRectFilled(ImVec2(cardRect.Min.x - 4.0f, cardRect.Max.y + 4.0f),
                    ImVec2(fullCardMax.x + 4.0f, fullCardMax.y + 4.0f), GetShadowColor(0, rowHovered), rounding, ImDrawFlags_RoundCornersBottom);
                dl->AddRectFilled(ImVec2(cardRect.Min.x - 2.0f, cardRect.Max.y + 2.0f),
                    ImVec2(fullCardMax.x + 2.0f, fullCardMax.y + 2.0f), GetShadowColor(1, rowHovered), rounding, ImDrawFlags_RoundCornersBottom);
                dl->AddRectFilled(ImVec2(cardRect.Min.x - 1.0f, cardRect.Max.y + 1.0f),
                    ImVec2(fullCardMax.x + 1.0f, fullCardMax.y + 1.0f), GetShadowColor(2, rowHovered), rounding, ImDrawFlags_RoundCornersBottom);
                dl->AddRectFilled(ImVec2(cardRect.Min.x, cardRect.Max.y), fullCardMax, GetShadowColor(3, rowHovered), rounding, ImDrawFlags_RoundCornersBottom);
            }

            // Merge channels back
            dl->ChannelsMerge();

            if (firstOpen)
            {
                // Measurement frame — reset cursor to collapsed position
                ImGui::SetCursorScreenPos(ImVec2(cardRect.Min.x, cardRect.Max.y));
                ImGui::Dummy(ImVec2(0, 8));
            }
        }
        else
        {
            ImGui::SetCursorScreenPos(ImVec2(cardRect.Min.x, cardRect.Max.y));
            ImGui::Dummy(ImVec2(0, 8));
        }

        // Hover-expand: deferred close — check full card area (header + body)
        if (g_shell_ptr && g_shell_ptr->hover_expand && g_hover_opened.count(rowKey))
        {
            ImVec2 endPos = ImGui::GetCursorScreenPos();
            ImRect fullCard(cardRect.Min, ImVec2(cardRect.Max.x, renderBody ? endPos.y : cardRect.Max.y));
            if (!fullCard.Contains(ImGui::GetMousePos()))
            {
                if (g_card_body_h.count(rowKey)) g_card_closing.insert(rowKey);
                g_open.erase(rowKey);
                g_hover_opened.erase(rowKey);
                g_notes_visible.erase(rowKey);
            }
        }

        if (read_only)
        {
            out.edit_open_requested = false;
            out.requested_delete = false;
        }

        ImGui::PopID();
        return out;
    }

    static AccordionItemAction RenderCredentialTile(
        const AccordionItem& c,
        uint32_t activeVaultKey,
        GetPasswordFn get_password_fn,
        float tileW,
        float tileH)
    {
        AccordionItemAction out{};
        out.id = c.id;

        const uint64_t rowKey = MakeRowKey(activeVaultKey, c.id);
        const bool sel = IsSelected(rowKey);
        const bool anySel = HasAnySelection(activeVaultKey);
        bool interaction_consumed = false;

        TileCardState& flipState = g_tile_state[rowKey];

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 rowMin = ImGui::GetCursorScreenPos();
        ImRect tileRect(rowMin, ImVec2(rowMin.x + tileW, rowMin.y + tileH));

        const float cbColW = 20.0f;
        const float pad = 10.0f;
        const float rounding = 8.0f;

        ImRect cardRect(ImVec2(tileRect.Min.x + cbColW, tileRect.Min.y), tileRect.Max);

        const bool anyPopup = ImGui::IsPopupOpen((const char*)NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) || (g_shell_ptr && g_shell_ptr->settings_modal_open);
        const bool rowHovered = !anyPopup && ImGui::IsMouseHoveringRect(tileRect.Min, tileRect.Max);

        // ============================================================
        // 1) SOFT SHADOW (bottom-heavy, thins toward top)
        // ============================================================
        {
            const float shadowOff = 3.0f; // push shadow down
            // Each ring: expand more on bottom/sides, less on top
            struct ShadowRing { float top; float side; float bot; };
            ShadowRing rings[] = {
                { 0.0f, 3.0f, 6.0f },  // outermost
                { 0.0f, 2.0f, 4.0f },
                { 0.0f, 1.0f, 2.5f },
                { 0.0f, 0.5f, 1.5f },  // innermost
            };
            for (int i = 0; i < 4; i++)
            {
                dl->AddRectFilled(
                    ImVec2(cardRect.Min.x - rings[i].side, cardRect.Min.y - rings[i].top + shadowOff),
                    ImVec2(cardRect.Max.x + rings[i].side, cardRect.Max.y + rings[i].bot + shadowOff),
                    GetShadowColor(i, rowHovered), rounding);
            }
        }

        // ============================================================
        // 2) CARD FILL (theme-aware)
        // ============================================================
        const ImU32 bg = ImGui::GetColorU32(GetTileBg());
        dl->AddRectFilled(cardRect.Min, cardRect.Max, bg, rounding);
        if (c.is_changed)
            dl->AddRectFilled(cardRect.Min, cardRect.Max,
                (c.changed_fields & FCF_IsNew) ? kNewRowTint : kChangedRowTint, rounding);

        // ============================================================
        // 3) STATUS (icons only, no border)
        // ============================================================
        const ImU32 colRed = GetFavoriteColor();   // Favorite red
        const ImU32 colBlue = ImGui::GetColorU32(colors::MainColor); // Pinned blue

        // ============================================================
        // CHECKBOX (selection)
        // ============================================================
        const bool showCb = rowHovered || sel || anySel;
        if (showCb)
        {
            float cbY = tileRect.Min.y + (tileH - 15.0f) * 0.5f;
            ImGui::SetCursorScreenPos(ImVec2(tileRect.Min.x + 2.0f, cbY));
            bool cbVal = sel;
            if (ImGui::Checkbox2("", &cbVal))
            {
                ToggleSelected(rowKey);
                out.selection_toggled = true;
                out.selected_now = IsSelected(rowKey);
                out.interacted = true;
                interaction_consumed = true;
            }
        }

        // ============================================================
        // CARD CONTENT
        // ============================================================
        TileCardState& state = flipState;
        ImGui::PushClipRect(cardRect.Min, cardRect.Max, true);

        if (!state.flipped)
        {
            // Front face: title, subtitle, date (bottom left), icons (bottom right)
            const char* title = (!c.title.empty()) ? c.title.c_str() : "(untitled)";

            // Type-aware subtitle
            std::string subtitle;
            if (c.type == CredType::CreditCard) {
                if (c.card_number.size() >= 4)
                    subtitle = std::string("\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2 ") + c.card_number.substr(c.card_number.size() - 4);
                else if (!c.card_number.empty())
                    subtitle = c.card_number;
            } else if (c.type == CredType::Identity) {
                subtitle = MaskIfPrivate(c.full_name) ? MaskIfPrivate(c.full_name) : c.full_name;
            } else if (c.type == CredType::SecureNote) {
                if (cfg::get_reprompt_reveal_notes() && !g_notes_visible.count(rowKey))
                    subtitle = "Secure Note";
                else
                    subtitle = c.notes.substr(0, 60);
            } else {
                const char* m = !c.email.empty() ? MaskIfPrivate(c.email) : MaskIfPrivate(c.user);
                subtitle = m ? m : (!c.email.empty() ? c.email : c.user);
            }

            const std::string lastEdited = FormatUnixMsDateOnly(c.updated_at_ms != 0 ? c.updated_at_ms : c.created_at_ms);

            // Title (top left) with type icon for non-Password
            float titleFontSize = render::FontBold ? render::FontBold->LegacySize : ImGui::GetTextLineHeight();
            float subFontSize = render::FontSmall ? render::FontSmall->LegacySize : ImGui::GetTextLineHeight();
            float titleDrawX = cardRect.Min.x + pad;

            {
                auto srv = favicon::Get(c.website);
                if (srv)
                {
                    float icoSz = 24.0f;
                    ImVec2 icoMin(titleDrawX, cardRect.Min.y + pad + 6.0f + (titleFontSize - icoSz) * 0.5f);
                    dl->AddImage((ImTextureID)srv, icoMin, ImVec2(icoMin.x + icoSz, icoMin.y + icoSz));
                    titleDrawX += icoSz + 6.0f;
                }
                else
                {
                    const char* typeIco = CredTypeIcon(c.type);
                    ImVec2 icoSz = ImGui::CalcTextSize(typeIco);
                    dl->AddText(ImVec2(titleDrawX, cardRect.Min.y + pad + 6.0f + (titleFontSize - icoSz.y) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), typeIco);
                    titleDrawX += icoSz.x + 4.0f;
                }
            }

            ImVec2 titlePos(titleDrawX, cardRect.Min.y + pad + 6.0f);
            dl->AddText(render::FontBold, titleFontSize, titlePos, ImGui::GetColorU32(ImGuiCol_Text), title);

            // Subtitle (below title)
            if (!subtitle.empty())
            {
                ImVec2 subPos(cardRect.Min.x + pad, cardRect.Min.y + pad + 6.0f + titleFontSize + 4.0f);
                dl->AddText(render::FontSmall, subFontSize, subPos, ImGui::GetColorU32(ImGuiCol_TextDisabled), subtitle.c_str());
            }

            // Bottom-right icons (pin/fav + security badges)
            {
                float iconX = cardRect.Max.x - pad;
                float iconY = cardRect.Max.y - pad - ImGui::GetTextLineHeight();

                // Heart (rightmost) - RED
                if (c.is_favorite)
                {
                    const char* fav = ICON_MDI_HEART;
                    ImVec2 sz = ImGui::CalcTextSize(fav);
                    iconX -= sz.x;
                    dl->AddText(ImVec2(iconX, iconY), colRed, fav);
                    iconX -= 6.0f;
                }

                // Thumbtack (left of heart) - BLUE
                if (c.is_pinned)
                {
                    const char* pin = ICON_MDI_PIN;
                    ImVec2 sz = ImGui::CalcTextSize(pin);
                    iconX -= sz.x;
                    dl->AddText(ImVec2(iconX, iconY), colBlue, pin);
                    iconX -= 6.0f;
                }

                // Timer badge
                if (c.expires_at_ms != 0)
                {
                    if (c.expires_at_ms < 0) {
                        const char* ico = ICON_MDI_TIMER_SAND_COMPLETE;
                        ImVec2 sz = ImGui::CalcTextSize(ico);
                        iconX -= sz.x;
                        dl->AddText(ImVec2(iconX, iconY), colors::ErrorText, ico);
                        iconX -= 6.0f;
                    } else {
                        int64_t remain_ms = c.expires_at_ms - helpers::now_unix_ms();
                        if (remain_ms < 0) remain_ms = 0;
                        char tbuf[32];
                        if (remain_ms >= time_ms::DAY)
                            snprintf(tbuf, sizeof(tbuf), "%lldd", (long long)(remain_ms / time_ms::DAY));
                        else if (remain_ms >= time_ms::HOUR)
                            snprintf(tbuf, sizeof(tbuf), "%lldh", (long long)(remain_ms / time_ms::HOUR));
                        else if (remain_ms >= time_ms::MINUTE)
                            snprintf(tbuf, sizeof(tbuf), "%lldm", (long long)(remain_ms / time_ms::MINUTE));
                        else
                            snprintf(tbuf, sizeof(tbuf), "<1m");
                        char label[64];
                        snprintf(label, sizeof(label), ICON_MDI_TIMER " %s", tbuf);
                        ImVec2 sz = ImGui::CalcTextSize(label);
                        iconX -= sz.x;
                        dl->AddText(ImVec2(iconX, iconY), ImGui::GetColorU32(colors::Orange), label);
                        iconX -= 6.0f;
                    }
                }

                // Security badges moved outside tile (drawn after PopClipRect)
            }
        }
        else
        {
            // Back face: copy field rows inside the tile
            const float headerH = 4.0f;
            const float bodyTop = cardRect.Min.y + headerH;
            const float bodyH = cardRect.Max.y - bodyTop - 4.0f;
            const float rowWidth_tile = cardRect.GetWidth() - pad;
            const bool popupBlocking = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);

            ImGui::SetCursorScreenPos(ImVec2(cardRect.Min.x + pad * 0.5f, bodyTop));
            ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 0, 0, 0));
            if (popupBlocking) ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            ImGui::BeginChild("##tile_body", ImVec2(rowWidth_tile, bodyH), false, ImGuiWindowFlags_NoScrollbar);

            float innerW = ImGui::GetContentRegionAvail().x + 2.0f;

            // Lifted child container for info rows
            {
                bool dk = IsDarkTheme();
                LiftedChildColorSet lc;
                lc.bg          = dk ? theme::CardSurface.dark  : theme::CardSurface.light;
                lc.borderOuter = dk ? theme::CardBorderOuter.dark      : theme::CardBorderOuter.light;
                lc.borderInner = dk ? theme::CardBorderInner.dark : theme::CardBorderInner.light;
                SetLiftedChildColors(lc);

                const float tileLiftPad = 6.0f;
                ImGui::Indent(tileLiftPad);
                ImGui::Spacing();
                if (BeginLiftedChild("##tile_info_lifted", ImVec2(innerW - tileLiftPad * 2.0f, 0), ImGuiWindowFlags_NoScrollbar))
                {
                    ImGui::Dummy(ImVec2(0, 2.0f));
                    float liftedW = ImGui::GetContentRegionAvail().x;
                    const bool isNew = !!(c.changed_fields & FCF_IsNew);

                    if (c.type == CredType::Password)
                    {
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_User);
                        DrawCopyFieldRow("username", "user", c.user, "user", rowKey, 0, liftedW, interaction_consumed);
                        DrawRowDivider(2.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_Email);
                        DrawCopyFieldRow("email", "email", c.email, "email", rowKey, 1, liftedW, interaction_consumed);
                        DrawRowDivider(2.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_Password);
                        const char* pw = get_password_fn ? get_password_fn(c.id) : "";
                        const std::string pwValue = pw ? pw : "";
                        DrawCopyFieldRow("password", "password", pwValue, "password", rowKey, 2, liftedW, interaction_consumed);
                        if (!c.totp_secret.empty()) {
                            auto totp_bytes = totp::base32_decode(c.totp_secret);
                            if (totp_bytes.size() >= 10) {
                                DrawRowDivider(2.0f);
                                std::string totp_code = totp::generate_code_now(totp_bytes);
                                int totp_secs = totp::seconds_remaining_now();
                                std::string totp_display = totp_code + " (" + std::to_string(totp_secs) + "s)";
                                g_field_accent_active = isNew || !!(c.changed_fields & FCF_TotpSecret);
                                DrawCopyFieldRow("totp", "totp", totp_code, totp_display, rowKey, 4, liftedW, interaction_consumed);
                            }
                        }
                    }
                    else if (c.type == CredType::CreditCard)
                    {
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_Cardholder);
                        DrawCopyFieldRow("cardholder", "cardholder", c.cardholder_name, "cardholder", rowKey, 0, liftedW, interaction_consumed);
                        DrawRowDivider(2.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardNumber);
                        DrawCopyFieldRow("card_number", "card number", c.card_number, "card number", rowKey, 1, liftedW, interaction_consumed);
                        DrawRowDivider(2.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardExpiry);
                        DrawCopyFieldRow("expiry", "expiry", c.card_expiry, "expiry", rowKey, 2, liftedW, interaction_consumed);
                        DrawRowDivider(2.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardCvv);
                        DrawCopyFieldRow("cvv", "cvv", c.card_cvv, "cvv", rowKey, 3, liftedW, interaction_consumed);
                        if (!c.card_address.empty()) {
                            DrawRowDivider(2.0f);
                            g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardAddress);
                            DrawCopyFieldRow("card_addr", "address", c.card_address, "address", rowKey, 4, liftedW, interaction_consumed);
                        }
                        if (!c.card_city.empty()) {
                            DrawRowDivider(2.0f);
                            g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardCity);
                            DrawCopyFieldRow("card_city", "city", c.card_city, "city", rowKey, 5, liftedW, interaction_consumed);
                        }
                        if (!c.card_postal_code.empty()) {
                            DrawRowDivider(2.0f);
                            g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardPostalCode);
                            DrawCopyFieldRow("card_postal", "postal code", c.card_postal_code, "postal code", rowKey, 6, liftedW, interaction_consumed);
                        }
                    }
                    else if (c.type == CredType::Identity)
                    {
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_FullName);
                        DrawCopyFieldRow("full_name", "name", c.full_name, "name", rowKey, 0, liftedW, interaction_consumed);
                        DrawRowDivider(2.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_IdNumber);
                        DrawCopyFieldRow("id_number", "id number", c.id_number, "id number", rowKey, 1, liftedW, interaction_consumed);
                        DrawRowDivider(2.0f);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_Phone);
                        DrawCopyFieldRow("phone", "phone", c.phone, "phone", rowKey, 2, liftedW, interaction_consumed);
                    }
                    else if (c.type == CredType::SecureNote && !c.notes.empty())
                    {
                        const bool noteRepromptActive = cfg::get_reprompt_reveal_notes();
                        // Check deferred reprompt approval
                        if (noteRepromptActive && g_notes_reprompt_pending == rowKey && IsRepromptApproved(RepromptAction::RevealNotes))
                        {
                            g_notes_visible.insert(rowKey);
                            ClearRepromptApproval(RepromptAction::RevealNotes);
                            g_notes_reprompt_pending = 0;
                        }
                        bool notesRevealed = !noteRepromptActive || g_notes_visible.count(rowKey) != 0;

                        const float notePadX = 10.0f;
                        const float notePadY = 6.0f;
                        ImGui::Indent(notePadX);
                        ImGui::Dummy(ImVec2(0, notePadY));

                        if (notesRevealed)
                        {
                            // "Hide notes" link
                            const char* linkLabel = "Hide notes";
                            const char* chevron = " " ICON_MDI_CHEVRON_UP;
                            ImVec2 textSz = ImGui::CalcTextSize(linkLabel);
                            ImVec2 chevSz = ImGui::CalcTextSize(chevron);
                            ImVec2 totalSz(textSz.x + chevSz.x, textSz.y);

                            ImVec2 pos = ImGui::GetCursorScreenPos();
                            ImGui::InvisibleButton("##tile_note_hide", totalSz);
                            bool clicked = ImGui::IsItemClicked();
                            bool hovered = ImGui::IsItemHovered();
                            if (clicked) interaction_consumed = true;

                            ImU32 col = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
                            ImDrawList* dl2 = ImGui::GetWindowDrawList();
                            dl2->AddText(pos, col, linkLabel);
                            dl2->AddText(ImVec2(pos.x + textSz.x, pos.y), col, chevron);
                            if (hovered)
                            {
                                float y = pos.y + textSz.y + 1.0f;
                                dl2->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + textSz.x, y), col, 1.0f);
                            }
                            if (clicked)
                                g_notes_visible.erase(rowKey);

                            ImGui::Spacing();

                            // Read-only multiline notes (selectable text)
                            std::string notesDisplay = c.notes;
                            float notesW = liftedW - notePadX * 2.0f;
                            ImVec2 fpad(8.0f, 6.0f);
                            float innerW = notesW - fpad.x * 2;
                            ImVec2 tSz = ImGui::CalcTextSize(notesDisplay.c_str(), nullptr, false, innerW);
                            float notesH = ImMax(tSz.y + fpad.y * 2, 32.0f);

                            ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
                            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, fpad);
                            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
                            InputTextMultilineString("##tile_notes_view", &notesDisplay, ImVec2(notesW, notesH),
                                ImGuiInputTextFlags_ReadOnly);
                            if (ImGui::IsItemActive()) interaction_consumed = true;
                            ImGui::PopStyleVar(2);
                            ImGui::PopStyleColor();
                        }
                        else
                        {
                            // "Reveal notes" link
                            const char* linkLabel = "Reveal notes";
                            const char* chevron = " " ICON_MDI_CHEVRON_DOWN;
                            ImVec2 textSz = ImGui::CalcTextSize(linkLabel);
                            ImVec2 chevSz = ImGui::CalcTextSize(chevron);
                            ImVec2 totalSz(textSz.x + chevSz.x, textSz.y);

                            ImVec2 pos = ImGui::GetCursorScreenPos();
                            ImGui::InvisibleButton("##tile_note_reveal", totalSz);
                            bool clicked = ImGui::IsItemClicked();
                            bool hovered = ImGui::IsItemHovered();
                            if (clicked) interaction_consumed = true;

                            ImU32 col = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
                            ImDrawList* dl2 = ImGui::GetWindowDrawList();
                            dl2->AddText(pos, col, linkLabel);
                            dl2->AddText(ImVec2(pos.x + textSz.x, pos.y), col, chevron);
                            if (hovered)
                            {
                                float y = pos.y + textSz.y + 1.0f;
                                dl2->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + textSz.x, y), col, 1.0f);
                            }
                            if (clicked)
                            {
                                g_notes_reprompt_pending = rowKey;
                                if (RequestReprompt(RepromptAction::RevealNotes, ""))
                                {
                                    g_notes_visible.insert(rowKey);
                                    g_notes_reprompt_pending = 0;
                                }
                            }
                        }

                        ImGui::Dummy(ImVec2(0, notePadY));
                        ImGui::Unindent(notePadX);
                    }
                    g_field_accent_active = false;

                    ImGui::Dummy(ImVec2(0, 1.0f));
                }
                EndLiftedChild();
                ImGui::Unindent(tileLiftPad);
            }
            if (!c.group.empty())
            {
                // Simple group label with folder icon right-aligned
                const float gPadX = 10.0f;
                ImVec2 gPos = ImGui::GetCursorScreenPos();
                float gH = ImGui::GetTextLineHeight() + 4.0f;
                ImDrawList* gdl = ImGui::GetWindowDrawList();

                // Folder icon right-aligned
                ImVec2 icoSz = ImGui::CalcTextSize(ICON_MDI_FOLDER);
                gdl->AddText(
                    ImVec2(gPos.x + innerW - icoSz.x - gPadX, gPos.y + (gH - icoSz.y) * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), ICON_MDI_FOLDER);

                // Group text left-aligned with same padding as rows
                ImGui::PushFont(render::FontSmall);
                ImGui::SetCursorScreenPos(ImVec2(gPos.x + gPadX, gPos.y));
                ImGui::TextDisabled("%s", c.group.c_str());
                ImGui::PopFont();
            }
            g_field_accent_active = false;

            ImGui::EndChild();
            if (popupBlocking) ImGui::PopItemFlag();
            ImGui::PopStyleColor();
        }

        ImGui::PopClipRect();

        // ============================================================
        // SECURITY BADGES (outside tile, right edge, stacked vertically)
        // ============================================================
        if (g_shell_ptr) {
            float badgeX = cardRect.Max.x + 3.0f;
            float badgeY = cardRect.Min.y;
            if (g_shell_ptr->sec_highlight_weak && g_shell_ptr->sec_weak_ids.count(c.id)) {
                dl->AddText(ImVec2(badgeX, badgeY), colors::StatusWeak, ICON_MDI_ALERT);
                badgeY += ImGui::GetTextLineHeight();
            }
            if (g_shell_ptr->sec_highlight_reused && g_shell_ptr->sec_reused_ids.count(c.id)) {
                dl->AddText(ImVec2(badgeX, badgeY), colors::StatusReused, ICON_MDI_REPEAT);
                badgeY += ImGui::GetTextLineHeight();
            }
            if (g_shell_ptr->sec_highlight_exposed && g_shell_ptr->sec_exposed_ids.count(c.id)) {
                dl->AddText(ImVec2(badgeX, badgeY), colors::StatusExposed, ICON_MDI_EARTH);
                badgeY += ImGui::GetTextLineHeight();
            }
            if (g_shell_ptr->sec_highlight_aging && g_shell_ptr->sec_aging_ids.count(c.id)) {
                dl->AddText(ImVec2(badgeX, badgeY), colors::StatusAging, ICON_MDI_CLOCK_ALERT);
                badgeY += ImGui::GetTextLineHeight();
            }
        }

        // ============================================================
        // INTERACTIONS
        // ============================================================
        // Right-click opens popup
        char popupId[32];
        snprintf(popupId, sizeof(popupId), "##tile_popup_%d", c.id);

        static std::unordered_map<int, int> s_tile_popup_tab;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && cardRect.Contains(ImGui::GetMousePos()))
        {
            ImGui::OpenPopup(popupId);
            s_tile_popup_tab[c.id] = 0;
            out.interacted = true;
        }

        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(8, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(8, 6));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IsDarkTheme() ? theme::PopupBg.dark : theme::PopupBg.light);
        ImGui::PushStyleColor(ImGuiCol_Border,  IsDarkTheme() ? theme::PopupBorder.dark : theme::PopupBorder.light);
        if (ImGui::BeginPopup(popupId))
        {
            PopupStyleBegin();
            ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

            bool hasSecurityTab = (c.type == CredType::Password);
            int& tileTab = s_tile_popup_tab[c.id];
            DrawPopupTabBar(c.id, hasSecurityTab, tileTab);
            if (tileTab == 1) {
                float secW = ImGui::CalcTextSize(ICON_MDI_CLOSE " Contains dictionary word").x + 16.0f;
                ImGui::Dummy(ImVec2(secW, 0));
            }

            // ============================================================ TAB 0: Details ============
            if (tileTab == 0)
            {
                ImGui::PushFont(render::FontBold);
                ImGui::TextUnformatted(c.title.empty() ? "(untitled)" : c.title.c_str());
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0, 4));

                // Quick action icon bar
                {
                    const float ibSz = 28.0f;
                    const float ibGap = 4.0f;
                    bool hasWebsite = !c.website.empty();
                    std::string webmail = GetWebmailUrl(c.email);

                    if (IconSquareBtn("##ctx_pin", c.is_pinned ? ICON_MDI_PIN : ICON_MDI_PIN,
                        c.is_pinned ? "Unpin" : "Pin", ibSz, c.is_pinned, false, ImGui::GetColorU32(colors::MainColor)))
                    { out.toggle_pin_requested = true; out.interacted = true; }
                    ImGui::SameLine(0, ibGap);
                    if (IconSquareBtn("##ctx_fav", c.is_favorite ? ICON_MDI_HEART : ICON_MDI_HEART,
                        c.is_favorite ? "Unfavorite" : "Favorite", ibSz, c.is_favorite, false, GetFavoriteColor()))
                    { out.toggle_fav_requested = true; out.interacted = true; }
                    ImGui::SameLine(0, ibGap);
                    if (!hasWebsite) ImGui::BeginDisabled();
                    if (IconSquareBtn("##ctx_launch", ICON_MDI_EARTH, "Open Website", ibSz))
                    { helpers::open_website(c.website); out.interacted = true; }
                    if (!hasWebsite) ImGui::EndDisabled();
                    ImGui::SameLine(0, ibGap);
                    if (webmail.empty()) ImGui::BeginDisabled();
                    if (IconSquareBtn("##ctx_inbox", ICON_MDI_EMAIL, "Open Inbox", ibSz))
                    { helpers::open_website(webmail); out.interacted = true; }
                    if (webmail.empty()) ImGui::EndDisabled();
                }

                ImGui::Dummy(ImVec2(0, 2));
                ImGui::Separator();

                if (BarMenuItem(ICON_MDI_PENCIL "   Edit Credential", false))
                { out.edit_open_requested = true; out.interacted = true; }
                ImGui::Separator();
                if (BarMenuItem(ICON_MDI_DELETE "   Delete Credential", false))
                { out.requested_delete = true; out.interacted = true; ImGui::CloseCurrentPopup(); }
                if (ImGui::IsItemHovered())
                {
                    ImVec2 itemMin = ImGui::GetItemRectMin(); ImVec2 itemMax = ImGui::GetItemRectMax();
                    float winX = ImGui::GetWindowPos().x; float winW = ImGui::GetWindowSize().x;
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(winX, itemMin.y), ImVec2(winX + winW, itemMax.y), colors::DeleteTint, 4.0f);
                }
            }
            // ============================================================ TAB 1: Notes ============
            else if (tileTab == 1)
            {
                const bool noteLocked = cfg::get_reprompt_reveal_notes() && !g_notes_visible.count(rowKey);
                const bool dk = IsDarkTheme();

                if (!c.notes.empty() && !noteLocked)
                {
                    if (BarMenuItem(ICON_MDI_CONTENT_COPY "   Copy Notes", false))
                    {
                        ImGui::SetClipboardText(c.notes.c_str());
                        TriggerCopyFlash();
                        ShowToast("Copied notes", ToastType::Success);
                    }
                    ImGui::Dummy(ImVec2(0, 2));
                }

                if (c.notes.empty())
                {
                    ImGui::TextDisabled("No notes.");
                }
                else if (noteLocked)
                {
                    float previewW = ImGui::GetContentRegionAvail().x;
                    float frameH = ImGui::GetTextLineHeight() + 16.0f;
                    ImVec2 fpos = ImGui::GetCursorScreenPos();
                    ImDrawList* pdl = ImGui::GetWindowDrawList();
                    pdl->AddRectFilled(fpos, ImVec2(fpos.x + previewW, fpos.y + frameH),
                        dk ? IM_COL32(255,255,255,4) : IM_COL32(0,0,0,4), 6.0f);
                    pdl->AddRect(fpos, ImVec2(fpos.x + previewW, fpos.y + frameH),
                        dk ? IM_COL32(255,255,255,20) : IM_COL32(0,0,0,20), 6.0f);
                    const char* ico = ICON_MDI_LOCK; const char* label = "Notes hidden";
                    ImVec2 icoSz = ImGui::CalcTextSize(ico); ImVec2 lblSz = ImGui::CalcTextSize(label);
                    float totalW = icoSz.x + 4.0f + lblSz.x;
                    float cx = fpos.x + (previewW - totalW) * 0.5f;
                    float cy = fpos.y + (frameH - lblSz.y) * 0.5f;
                    pdl->AddText(ImVec2(cx, cy), ImGui::GetColorU32(ImGuiCol_TextDisabled), ico);
                    pdl->AddText(ImVec2(cx + icoSz.x + 4.0f, cy), ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
                    ImGui::Dummy(ImVec2(previewW, frameH));
                }
                else
                {
                    const bool dk2 = IsDarkTheme();
                    float noteW = ImGui::GetContentRegionAvail().x;
                    float maxH = ImGui::GetTextLineHeight() * 6.0f + 12.0f;
                    ImVec2 fpad(8.0f, 6.0f);
                    float innerW = noteW - fpad.x * 2;
                    ImVec2 tSz = ImGui::CalcTextSize(c.notes.c_str(), nullptr, false, innerW);
                    float notesH = ImMin(ImMax(tSz.y + fpad.y * 2, 28.0f), maxH);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, dk2 ? IM_COL32(255,255,255,4) : IM_COL32(0,0,0,4));
                    ImGui::PushStyleColor(ImGuiCol_Border,  dk2 ? IM_COL32(255,255,255,20) : IM_COL32(0,0,0,20));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, fpad);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                    std::string notesPreview = c.notes;
                    InputTextMultilineString("##tile_popup_notes", &notesPreview, ImVec2(noteW, notesH), ImGuiInputTextFlags_ReadOnly);
                    if (ImGui::IsItemActive()) interaction_consumed = true;
                    ImGui::PopStyleVar(3);
                    ImGui::PopStyleColor(2);
                }
            }
            // ============================================================ TAB 2: Security ============
            else if (tileTab == 2 && hasSecurityTab)
            {
                const char* pw = get_password_fn ? get_password_fn(c.id) : "";
                std::string pwStr = pw ? pw : "";
                if (!pwStr.empty())
                {
                    float meterW = ImGui::GetContentRegionAvail().x;
                    DrawStrengthMeterCompact(pwStr, meterW);
                }
                ImGui::Dummy(ImVec2(0, 4));

                // Strength analysis hints
                if (!pwStr.empty())
                {
                    helpers::PwStrength ps = helpers::analyze_password(pwStr);
                    ImGui::PushFont(render::FontSmall);
                    const ImU32 goodCol = colors::TimerGood;
                    const ImU32 warnCol = colors::TimerWarning;
                    int classes = (int)ps.hasLower + (int)ps.hasUpper + (int)ps.hasDigit + (int)ps.hasSymbol;
                    int hints = 0;
                    if (ps.shortPwd && hints < 3)         { DrawHintLine(ICON_MDI_CLOSE, "Too short (< 8 chars)", warnCol); hints++; }
                    if (ps.allSame && hints < 3)           { DrawHintLine(ICON_MDI_CLOSE, "All same character", warnCol); hints++; }
                    if (ps.simpleSeq && hints < 3)         { DrawHintLine(ICON_MDI_CLOSE, "Simple sequence detected", warnCol); hints++; }
                    if (ps.isCommonPwd && hints < 3)       { DrawHintLine(ICON_MDI_CLOSE, "Common password", warnCol); hints++; }
                    if (ps.isDictWord && hints < 3)        { DrawHintLine(ICON_MDI_CLOSE, "Contains dictionary word", warnCol); hints++; }
                    if (classes < 2 && hints < 3)          { DrawHintLine(ICON_MDI_CLOSE, "Only one character type", warnCol); hints++; }
                    if (classes >= 3 && hints < 3)         { DrawHintLine(ICON_MDI_CHECK, "Mixed character types", goodCol); hints++; }
                    if ((int)pwStr.size() >= 16 && hints < 3) { DrawHintLine(ICON_MDI_CHECK, "Long password (16+)", goodCol); hints++; }
                    int improvements = 0;
                    if (ps.shortPwd) improvements++;
                    if (ps.allSame) improvements++;
                    if (ps.simpleSeq) improvements++;
                    if (ps.isCommonPwd) improvements++;
                    if (ps.isDictWord) improvements++;
                    if (!ps.hasUpper) improvements++;
                    if (!ps.hasLower) improvements++;
                    if (!ps.hasDigit) improvements++;
                    if (!ps.hasSymbol) improvements++;
                    if (improvements > 0) {
                        char impBuf[64];
                        snprintf(impBuf, sizeof(impBuf), ICON_MDI_INFORMATION " %d improvement%s suggested", improvements, improvements == 1 ? "" : "s");
                        ImGui::TextDisabled("%s", impBuf);
                    }
                    ImGui::PopFont();
                    ImGui::Spacing();
                }

                // Issue badges
                bool hasIssue = false;
                if (g_shell_ptr && g_shell_ptr->sec_weak_ids.count(c.id)) {
                    ImGui::TextColored(ImVec4(174/255.f, 41/255.f, 41/255.f, 1.0f), ICON_MDI_ALERT " Weak password");
                    hasIssue = true;
                }
                if (g_shell_ptr && g_shell_ptr->sec_reused_ids.count(c.id)) {
                    ImGui::TextColored(ImVec4(190/255.f, 156/255.f, 63/255.f, 1.0f), ICON_MDI_REPEAT " Reused password");
                    hasIssue = true;
                    // List credentials sharing same password
                    if (!pwStr.empty()) {
                        ImGui::PushFont(render::FontSmall);
                        ImGui::TextDisabled("Also used by:");
                        const auto& creds = GetActiveVaultCreds();
                        for (const auto& other : creds) {
                            if (other.id == c.id || other.is_deleted() || other.type != CredType::Password) continue;
                            if (other.password == pwStr) {
                                ImGui::PushID(other.id);
                                char label[128];
                                snprintf(label, sizeof(label), "  " ICON_MDI_ARROW_RIGHT " %s", other.title.empty() ? "(untitled)" : other.title.c_str());
                                if (ImGui::Selectable(label, false, 0, ImVec2(0, ImGui::GetTextLineHeight() + 4.0f))) {
                                    out.edit_open_requested = true;
                                    out.edit_open_id_override = other.id;
                                    out.interacted = true;
                                    ImGui::CloseCurrentPopup();
                                }
                                ImGui::PopID();
                            }
                        }
                        ImGui::PopFont();
                    }
                }
                if (g_shell_ptr && g_shell_ptr->sec_exposed_ids.count(c.id)) {
                    ImGui::TextColored(ImVec4(142/255.f, 68/255.f, 173/255.f, 1.0f), ICON_MDI_EARTH " Exposed in breach");
                    hasIssue = true;
                }
                if (g_shell_ptr && g_shell_ptr->sec_aging_ids.count(c.id)) {
                    ImGui::TextColored(ImVec4(39/255.f, 174/255.f, 157/255.f, 1.0f), ICON_MDI_CLOCK_ALERT " Password is aging");
                    hasIssue = true;
                }
                if (!hasIssue) {
                    ImGui::TextColored(ImVec4(76/255.f, 195/255.f, 100/255.f, 1.0f), ICON_MDI_CHECK_CIRCLE " No issues detected");
                }
            }

            ImGui::PopItemFlag();
            PopupStyleEnd();
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(5);

        // Hover-flip logic (skip if popup is open)
        if (g_shell_ptr && g_shell_ptr->hover_expand && !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        {
            if (rowHovered && !state.flipped)
            {
                state.flipped = true;
                g_hover_flipped.insert(rowKey);
            }
            else if (!rowHovered && g_hover_flipped.count(rowKey))
            {
                state.flipped = false;
                g_hover_flipped.erase(rowKey);
                g_notes_visible.erase(rowKey);
            }
        }

        // Left-click flips the card (skip if any popup is open)
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && cardRect.Contains(ImGui::GetMousePos()) &&
            !ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId))
        {
            if (!interaction_consumed && !ImGui::IsMouseDragging(ImGuiMouseButton_Left, 4.0f))
            {
                g_hover_flipped.erase(rowKey);
                if (state.flipped) g_notes_visible.erase(rowKey);
                state.flipped = !state.flipped;
                out.interacted = true;
            }
        }

        ImGui::SetCursorScreenPos(tileRect.Min);
        ImGui::Dummy(ImVec2(tileW, tileH + 10.0f));
        return out;
    }

    // ============================================================
    // ============================================================
    // ACCORDION LIST DISPATCHER
    // ============================================================

    AccordionListResult RenderAccordionList(
        const std::vector<AccordionItem>& items,
        uint32_t activeVaultKey,
        GetPasswordFn get_password_fn,
        bool read_only,
        ViewMode view_mode)
    {
        // Table view — flat spreadsheet
        if (view_mode == ViewMode::Table)
        {
            return RenderTableView(items, activeVaultKey, get_password_fn, read_only);
        }

        // Three-Pane view has its own layout (sidebar + list + detail)
        if (view_mode == ViewMode::ThreePane)
        {
            return RenderThreePaneView(items, activeVaultKey, get_password_fn, read_only);
        }

        AccordionListResult out{};

        if (items.empty())
        {
            ImGui::TextDisabled("No items match this view.");
            return out;
        }

        ImGui::Dummy(ImVec2(0, 6));
        bool hit = false;
        int displayIndex = 1;  // Start at 1 for user-friendly display

        if (view_mode == ViewMode::Tiles)
        {
            const float minTileW = 150.0f;
            const float maxTileW = 200.0f;
            const float tileH = 120.0f;
            const float spacing = 0.0f;

            const float avail = ImGui::GetContentRegionAvail().x -24;
            int columns = std::max(1, (int)((avail + spacing) / (minTileW + spacing)));
            float tileW = (avail - spacing * (columns - 1)) / columns;
            tileW = ImClamp(tileW, minTileW, maxTileW);

            int colIndex = 0;
            bool tileSkipUntilNextHeader = false;
            for (const auto& it : items)
            {
                if (it.is_header)
                {
                    if (colIndex != 0)
                    {
                        ImGui::Dummy(ImVec2(0, spacing));
                        colIndex = 0;
                    }
                    bool hdrCollapsed = g_collapsed_headers.count(it.header_label) > 0;
                    const char* chevron = hdrCollapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN;
                    ImGui::PushID(it.header_label.c_str());
                    ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(0,0,0,0));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  IM_COL32(0,0,0,0));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive,   IM_COL32(0,0,0,0));
                    float hdrW = ImGui::GetContentRegionAvail().x;
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (ImGui::Selectable(it.header_label.c_str(), false, 0, ImVec2(hdrW, 0)))
                    {
                        if (hdrCollapsed) g_collapsed_headers.erase(it.header_label);
                        else              g_collapsed_headers.insert(it.header_label);
                        hdrCollapsed = !hdrCollapsed;
                    }
                    ImGui::PopStyleColor();
                    // Draw chevron on far right
                    ImVec2 chevSz = ImGui::CalcTextSize(chevron);
                    ImVec2 rMin = ImGui::GetItemRectMin();
                    ImVec2 rMax = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(rMax.x - chevSz.x - 4.0f, rMin.y + (rMax.y - rMin.y - chevSz.y) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), chevron);
                    ImGui::PopStyleColor(3);
                    ImGui::PopID();
                    ImGui::Dummy(ImVec2(0, 2));
                    tileSkipUntilNextHeader = hdrCollapsed;
                    continue;
                }

                if (tileSkipUntilNextHeader) continue;

                if (colIndex > 0)
                    ImGui::SameLine(0, spacing);

                ImGui::PushID((void*)(uintptr_t)MakeRowKey(activeVaultKey, it.id));
                AccordionItemAction r = RenderCredentialTile(it, activeVaultKey, get_password_fn, tileW, tileH);
                ImGui::PopID();

                hit |= r.interacted;

                if (!read_only && r.requested_delete)
                    out.delete_id = r.id;

                if (r.edit_open_requested)
                    out.edit_open_id = (r.edit_open_id_override >= 0) ? r.edit_open_id_override : r.id;

                if (r.toggle_pin_requested)
                    out.toggle_pin_id = r.id;

                if (r.toggle_fav_requested)
                    out.toggle_fav_id = r.id;

                colIndex++;
                if (colIndex >= columns)
                {
                    colIndex = 0;
                    ImGui::Dummy(ImVec2(0, spacing));
                }
                displayIndex++;
            }
        }
        else
        {
            bool skipUntilNextHeader = false;
            for (const auto& it : items)
            {
                if (it.is_header)
                {
                    bool hdrCollapsed = g_collapsed_headers.count(it.header_label) > 0;
                    const char* chevron = hdrCollapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN;
                    ImGui::PushID(it.header_label.c_str());
                    ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(0,0,0,0));
                    ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  IM_COL32(0,0,0,0));
                    ImGui::PushStyleColor(ImGuiCol_HeaderActive,   IM_COL32(0,0,0,0));
                    float hdrW = ImGui::GetContentRegionAvail().x;
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (ImGui::Selectable(it.header_label.c_str(), false, 0, ImVec2(hdrW, 0)))
                    {
                        if (hdrCollapsed) g_collapsed_headers.erase(it.header_label);
                        else              g_collapsed_headers.insert(it.header_label);
                        hdrCollapsed = !hdrCollapsed;
                    }
                    ImGui::PopStyleColor();
                    // Draw chevron on far right
                    ImVec2 chevSz = ImGui::CalcTextSize(chevron);
                    ImVec2 rMin = ImGui::GetItemRectMin();
                    ImVec2 rMax = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddText(
                        ImVec2(rMax.x - chevSz.x - 4.0f, rMin.y + (rMax.y - rMin.y - chevSz.y) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), chevron);
                    ImGui::PopStyleColor(3);
                    ImGui::PopID();
                    ImGui::Dummy(ImVec2(0, 2));
                    skipUntilNextHeader = hdrCollapsed;
                    continue;
                }

                if (skipUntilNextHeader) continue;

                AccordionItemAction r = RenderAccordionItemDetailed(it, activeVaultKey, get_password_fn, read_only, displayIndex++);

                hit |= r.interacted;

                if (!read_only && r.requested_delete)
                {
                    out.delete_id = r.id;
                    break;
                }

                if (!read_only && r.edit_open_requested)
                {
                    out.edit_open_id = (r.edit_open_id_override >= 0) ? r.edit_open_id_override : r.id;
                    break;
                }

                if (r.toggle_pin_requested)
                {
                    out.toggle_pin_id = r.id;
                    break;
                }

                if (r.toggle_fav_requested)
                {
                    out.toggle_fav_id = r.id;
                    break;
                }

                if (!read_only && r.edit_committed)
                {
                    out.edit_commit_id = r.id;
                    out.edited = r.edited;
                    out.edited_password = r.edited_password;
                    break;
                }

                float gap = g_shell_ptr ? (float)g_shell_ptr->row_gap : 6.0f;
                ImGui::Dummy(ImVec2(0, gap));
            }
        }

        // Click blank space to clear selection:
        // Keep this allowed even in read-only (it doesn't mutate vault data)
        if (ImGui::IsMouseClicked(0) && ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem))
        {
            if (!hit && !ImGui::IsAnyItemHovered())
                ClearSelectionForVault(activeVaultKey);
        }

        return out;
    }

    void ForgetRowState(uint32_t activeVaultKey, int id) // NEW signature
    {
        uint64_t rowKey = MakeRowKey(activeVaultKey, id);
        g_selected.erase(MakeRowKey(activeVaultKey, id));
        g_edit.erase(rowKey);
        g_open.erase(rowKey);
        g_tile_state.erase(rowKey);
        g_hover_opened.erase(rowKey);
        g_hover_flipped.erase(rowKey);
        g_notes_visible.erase(rowKey);
        g_card_body_h.erase(rowKey);
        g_card_anim_h.erase(rowKey);
        g_card_closing.erase(rowKey);
    }

    // (Optional convenience)
    void ForgetRowStateRowKey(uint64_t rowKey)
    {
        g_selected.erase(rowKey);
        g_edit.erase(rowKey);
        g_open.erase(rowKey);
        g_tile_state.erase(rowKey);
        g_hover_opened.erase(rowKey);
        g_hover_flipped.erase(rowKey);
        g_notes_visible.erase(rowKey);
        g_card_body_h.erase(rowKey);
        g_card_anim_h.erase(rowKey);
        g_card_closing.erase(rowKey);
    }

    void ResetAllRowState()
    {
        g_edit.clear();
        g_open.clear();
        g_selected.clear();
        g_tile_state.clear();
        g_copy_flash.clear();
        g_hover_opened.clear();
        g_hover_flipped.clear();
        g_notes_visible.clear();
        g_card_body_h.clear();
        g_card_anim_h.clear();
        g_card_closing.clear();
    }

    // g_shell_open, g_sortAnim, g_orderAnim, g_searchAnim defined in ui_controls.cpp

    void ForgetVaultRowState(uint32_t activeVaultKey)
    {
        // g_edit: unordered_map<uint64_t, EditBuffers>
        for (auto it = g_edit.begin(); it != g_edit.end(); )
        {
            uint64_t rk = it->first;
            if ((uint32_t)(rk >> 32) == activeVaultKey)
                it = g_edit.erase(it);
            else
                ++it;
        }

        // g_open: unordered_set<uint64_t>
        for (auto it = g_open.begin(); it != g_open.end(); )
        {
            uint64_t rk = *it;
            if ((uint32_t)(rk >> 32) == activeVaultKey)
                it = g_open.erase(it);
            else
                ++it;
        }

        for (auto it = g_tile_state.begin(); it != g_tile_state.end(); )
        {
            uint64_t rk = it->first;
            if ((uint32_t)(rk >> 32) == activeVaultKey)
                it = g_tile_state.erase(it);
            else
                ++it;
        }

        for (auto it = g_hover_opened.begin(); it != g_hover_opened.end(); )
        {
            uint64_t rk = *it;
            if ((uint32_t)(rk >> 32) == activeVaultKey)
                it = g_hover_opened.erase(it);
            else
                ++it;
        }

        for (auto it = g_hover_flipped.begin(); it != g_hover_flipped.end(); )
        {
            uint64_t rk = *it;
            if ((uint32_t)(rk >> 32) == activeVaultKey)
                it = g_hover_flipped.erase(it);
            else
                ++it;
        }

        for (auto it = g_notes_visible.begin(); it != g_notes_visible.end(); )
        {
            uint64_t rk = *it;
            if ((uint32_t)(rk >> 32) == activeVaultKey)
                it = g_notes_visible.erase(it);
            else
                ++it;
        }

        for (auto it = g_card_body_h.begin(); it != g_card_body_h.end(); )
        {
            uint64_t rk = it->first;
            if ((uint32_t)(rk >> 32) == activeVaultKey)
                it = g_card_body_h.erase(it);
            else
                ++it;
        }

        for (auto it = g_card_anim_h.begin(); it != g_card_anim_h.end(); )
        {
            uint64_t rk = it->first;
            if ((uint32_t)(rk >> 32) == activeVaultKey)
                it = g_card_anim_h.erase(it);
            else
                ++it;
        }

        for (auto it = g_card_closing.begin(); it != g_card_closing.end(); )
        {
            uint64_t rk = *it;
            if ((uint32_t)(rk >> 32) == activeVaultKey)
                it = g_card_closing.erase(it);
            else
                ++it;
        }

        ClearSelectionForVault(activeVaultKey);

    }

    void GetSelectedRowKeys(uint32_t activeVaultKey, std::vector<uint64_t>& outKeys)
    {
        outKeys.clear();
        const uint64_t prefix = (uint64_t(activeVaultKey) << 32);
        for (uint64_t k : g_selected)
            if ((k & 0xFFFFFFFF00000000ull) == prefix)
                outKeys.push_back(k);
    }

} // namespace ui
