// ui_controls.cpp — list controls, sort/view dropdowns, selection toolbar, settings page
#include "ui_internal.h"

namespace ui
{
    bool g_shell_open = false;
    UnderlineTabsAnim g_sortAnim;
    UnderlineTabsAnim g_orderAnim;
    SearchPopupAnim   g_searchAnim;

    static const char* OrderKeyLabel(ui::OrderKey k)
    {
        switch (k)
        {
        case ui::OrderKey::Title: return "Title";
        case ui::OrderKey::Created: return "Created";
        case ui::OrderKey::Updated: return "Updated";
        default: return "Title";
        }
    }
    static const char* OrderDirLabel(ui::OrderDir d) { return (d == ui::OrderDir::Asc) ? "Ascending" : "Descending"; }
    static const char* GroupModeLabel(ui::GroupMode g)
    {
        switch (g)
        {
        case ui::GroupMode::None: return "None";
        case ui::GroupMode::Group: return "Group";
        case ui::GroupMode::MonthCreated: return "Month Created";
        case ui::GroupMode::MonthUpdated: return "Month Edited";
        case ui::GroupMode::PinnedFavorites: return "Pinned/Favorites";
        case ui::GroupMode::Alphabetical: return "Alphabetical";
        default: return "None";
        }
    }

    static bool RenderVaultDropdown(
        ui::ShellState& s,
        const std::vector<std::string>& openVaults)
    {
        const int count = (int)openVaults.size();
        if (count == 0)
            return false;

        s.active_db = ImClamp(s.active_db, 0, count - 1);

        const bool can_switch = (count > 1);
        bool changed = false;

        ImGui::PushID("##vault_dropdown");

        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetColorU32(ImGuiCol_HeaderHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetColorU32(ImGuiCol_HeaderActive));

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

        ImVec2 groupStart = ImGui::GetCursorPos();
        ImGui::BeginGroup();

        ImGui::TextUnformatted(openVaults[s.active_db].c_str());

        if (can_switch)
        {
            ImGui::SameLine(0.0f, 6.0f);
            ImGui::TextUnformatted(ICON_MDI_CHEVRON_DOWN);
        }

        ImGui::EndGroup();

        ImRect bb(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImGui::SetCursorPos(groupStart);
        if (ImGui::InvisibleButton("##vault_btn", bb.GetSize()) && can_switch)
        {
            ImGui::OpenPopup("##vault_popup");
        }

        if (can_switch && ImGui::BeginPopup("##vault_popup"))
        {
            for (int i = 0; i < count; ++i)
            {
                const bool selected = (i == s.active_db);
                if (ImGui::Selectable(openVaults[i].c_str(), selected))
                {
                    s.active_db = i;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::EndPopup();
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        ImGui::PopID();

        return changed;
    }

    static void RenderVaultHeader(ui::ShellState& s, bool show_back_button = false)
    {
        float startY = ImGui::GetCursorPosY();
        float availW = ImGui::GetContentRegionAvail().x;
        ImVec2 headerStart = ImGui::GetCursorScreenPos();
        float headerH = 30.0f;

        if (!s.db_labels.empty())
        {
            float textW = ImGui::CalcTextSize(s.db_labels[s.active_db].c_str()).x;
            float chevronW = (s.db_labels.size() > 1) ? ImGui::CalcTextSize(ICON_MDI_CHEVRON_DOWN).x + 6.0f : 0.0f;
            float totalW = textW + chevronW + 12.0f;
            ImGui::SetCursorPosX((availW - totalW) * 0.5f);
        }
        RenderVaultDropdown(s, s.db_labels);

        float exitW = ImGui::CalcTextSize(ICON_MDI_CLOSE).x;
        float backW = ImGui::CalcTextSize(ICON_MDI_ARROW_LEFT).x;
        float spacing = 12.0f;
        float rightX = availW + 4;

        rightX -= exitW;
        ImGui::SetCursorPos(ImVec2(rightX, startY));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
        ImGui::TextUnformatted(ICON_MDI_CLOSE);
        ImGui::PopStyleColor();
        if (ImGui::IsItemClicked())
        {
            if (show_back_button)
                exit(EXIT_SUCCESS);    // Settings: lock vault
            else
                exit(EXIT_SUCCESS);            // Other screens: exit app
        }

        if (show_back_button)
        {
            rightX -= (spacing + backW);
            ImGui::SetCursorPos(ImVec2(rightX, startY));
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(ICON_MDI_ARROW_LEFT);
            ImGui::PopStyleColor();
            if (ImGui::IsItemClicked())
                s.back_clicked = true;
        }

    }

    static void DrawSortDropdown(ui::ShellState& s)
    {
        static const char* sortLabels[] = { "Title", "Created", "Updated" };
        static const char* dirLabels[]  = { "Asc", "Desc" };
        static const char* grpLabels[]  = { "None", "Group", "Month Created", "Month Edited", "Pinned/Fav", "A-Z" };

        const bool dark = s.dark_theme;
        const ImVec4 col_text  = dark ? ImVec4(0.92f,0.92f,0.92f,1.f) : ImVec4(0.10f,0.10f,0.10f,1.f);
        const ImVec4 col_muted = dark ? ImVec4(0.92f,0.92f,0.92f,0.55f) : ImVec4(0.10f,0.10f,0.10f,0.55f);
        const ImVec4 col_popup_bg = dark ? ImVec4(0.10f,0.10f,0.11f,1.f) : ImVec4(0.98f,0.98f,0.98f,1.f);

        const char* label = ICON_MDI_SORT " Sort";
        ImVec2 labelSz = ImGui::CalcTextSize(label);
        ImVec2 chevSz  = ImGui::CalcTextSize(ICON_MDI_CHEVRON_DOWN);
        float totalW = labelSz.x + 4.f + chevSz.x;
        float h = ImGui::GetFrameHeight();
        ImVec2 pos = ImGui::GetCursorScreenPos();

        ImGuiIO& io = ImGui::GetIO();
        ImGui::PushID("##sort_drop");
        ImGui::InvisibleButton("##sort_btn", ImVec2(totalW, h));
        bool hovered = ImGui::IsItemHovered();
        bool pressed = ImGui::IsItemClicked();

        ImGuiID base_id = ImGui::GetItemID();
        ImGuiID popup_id = ImGui::GetID("##sort_popup");
        bool popup_open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
        if (pressed) ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_None);

        ImGuiStorage* st = ImGui::GetStateStorage();
        float hover_t = st->GetFloat(base_id + 0x2001, 0.0f);
        float open_t  = st->GetFloat(base_id + 0x2002, 0.0f);
        hover_t = ImClamp(hover_t + (hovered ? 1.f : -1.f) * io.DeltaTime * 14.f, 0.f, 1.f);
        open_t  = ImClamp(open_t  + (popup_open ? 1.f : -1.f) * io.DeltaTime * 18.f, 0.f, 1.f);
        st->SetFloat(base_id + 0x2001, hover_t);
        st->SetFloat(base_id + 0x2002, open_t);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        float alpha = 0.7f + 0.3f * hover_t;
        ImU32 text_u32 = ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x, col_text.y, col_text.z, alpha));

        float textY = pos.y + (h - labelSz.y) * 0.5f;
        dl->AddText(ImVec2(pos.x, textY), text_u32, label);

        float chevX = pos.x + labelSz.x + 4.f;
        float chevY = pos.y + (h - chevSz.y) * 0.5f;
        dl->AddText(ImVec2(chevX, chevY), ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x,col_text.y,col_text.z,(1.f-open_t)*alpha)), ICON_MDI_CHEVRON_DOWN);
        dl->AddText(ImVec2(chevX, chevY), ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x,col_text.y,col_text.z,open_t*alpha)),       ICON_MDI_CHEVRON_UP);

        const float winPadX = 8.f;
        float popupW = 0.f;
        auto measure = [&](const char* const* items, int count) {
            for (int i = 0; i < count; i++) {
                float w = ImGui::CalcTextSize(items[i]).x + 22.f + 10.f + winPadX * 2.f;
                if (w > popupW) popupW = w;
            }
        };
        measure(sortLabels, 3);
        measure(dirLabels, 2);
        measure(grpLabels, 6);
        popupW = ImMax(popupW, totalW);

        const float rounding = 6.0f;
        ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + h + 4.f));
        ImGui::SetNextWindowSize(ImVec2(popupW, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 10.f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertFloat4ToU32(col_popup_bg));
        ImGui::PushStyleColor(ImGuiCol_Border, dark ? theme::PopupBorder.dark : theme::PopupBorder.light);

        if (ImGui::BeginPopupEx(popup_id, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar))
        {
            ImDrawList* pdl = ImGui::GetWindowDrawList();
            const float row_h = ImGui::GetTextLineHeight() + 6.0f;
            const float dot_r = 3.0f;
            ImU32 tu32 = ImGui::ColorConvertFloat4ToU32(col_text);
            ImU32 mu32 = ImGui::ColorConvertFloat4ToU32(col_muted);

            auto draw_section = [&](const char* header, const char* const* labels, int count, int current, int* out_clicked) {
                ImGui::TextDisabled("%s", header);
                ImGui::Dummy(ImVec2(0, 2.f));
                for (int i = 0; i < count; i++)
                {
                    ImGui::Dummy(ImVec2(0, 2.5f));
                    bool sel = (current == i);
                    ImGui::PushID(labels[i]);
                    ImVec2 rpos = ImGui::GetCursorScreenPos();
                    ImRect rbb(rpos, ImVec2(rpos.x + ImGui::GetContentRegionAvail().x, rpos.y + row_h));
                    ImGui::InvisibleButton("##o", rbb.GetSize());
                    bool rh = ImGui::IsItemHovered();

                    if (rh || sel)
                        pdl->AddRectFilled(rbb.Min, rbb.Max, ImGui::ColorConvertFloat4ToU32(
                            ImVec4(col_text.x,col_text.y,col_text.z, rh ? 0.08f : 0.04f)), 4.f);

                    if (sel)
                    {
                        float dot_x = rbb.Min.x + 10.f;
                        float dot_y = (rbb.Min.y + rbb.Max.y) * 0.5f;
                        pdl->AddCircleFilled(ImVec2(dot_x, dot_y), dot_r,
                            ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x,col_text.y,col_text.z, 0.9f)));
                    }

                    pdl->AddText(ImVec2(rbb.Min.x + 22.f, rbb.Min.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                        sel ? tu32 : mu32, labels[i]);

                    if (ImGui::IsItemClicked()) *out_clicked = i;
                    ImGui::PopID();
                }
            };

            int newSort = -1, newDir = -1, newGrp = -1;
            draw_section("Sort by", sortLabels, 3, (int)s.order_key, &newSort);
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            draw_section("Direction", dirLabels, 2, (int)s.order_dir, &newDir);
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            draw_section("Group", grpLabels, 6, (int)s.group_mode, &newGrp);

            if (newSort >= 0) { s.order_key = (ui::OrderKey)newSort; cfg::set_order_key(newSort); }
            if (newDir >= 0)  { s.order_dir = (ui::OrderDir)newDir;  cfg::set_order_dir(newDir); }
            if (newGrp >= 0)  { s.group_mode = (ui::GroupMode)newGrp; cfg::set_group_mode(newGrp); }

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
        ImGui::PopID();
    }

    // label + chevron only, no pill background; popup auto-sizes to content
    static bool NakedCombo(const char* id, const char* label, const char* const* items, int count, int* current,
        bool dark, bool sectioned = false, const char* header = nullptr)
    {
        ImGuiIO& io = ImGui::GetIO();
        ImGui::PushID(id);

        const ImVec4 col_text  = dark ? ImVec4(0.92f,0.92f,0.92f,1.f) : ImVec4(0.10f,0.10f,0.10f,1.f);
        const ImVec4 col_muted = dark ? ImVec4(0.92f,0.92f,0.92f,0.55f) : ImVec4(0.10f,0.10f,0.10f,0.55f);
        const ImVec4 col_popup_bg = dark ? ImVec4(0.10f,0.10f,0.11f,1.f) : ImVec4(0.98f,0.98f,0.98f,1.f);

        ImVec2 labelSz = ImGui::CalcTextSize(label);
        ImVec2 chevSz  = ImGui::CalcTextSize(ICON_MDI_CHEVRON_DOWN);
        float totalW = labelSz.x + 4.f + chevSz.x;
        float h = ImGui::GetFrameHeight();
        ImVec2 pos = ImGui::GetCursorScreenPos();

        ImGui::InvisibleButton("##btn", ImVec2(totalW, h));
        bool hovered = ImGui::IsItemHovered();
        bool pressed = ImGui::IsItemClicked();

        ImGuiID popup_id = ImGui::GetID("##popup");
        bool popup_open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);
        if (pressed) ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_None);

        ImGuiStorage* st = ImGui::GetStateStorage();
        ImGuiID base_id = ImGui::GetItemID();
        float hover_t = st->GetFloat(base_id + 0x3001, 0.0f);
        float open_t  = st->GetFloat(base_id + 0x3002, 0.0f);
        hover_t = ImClamp(hover_t + (hovered ? 1.f : -1.f) * io.DeltaTime * 14.f, 0.f, 1.f);
        open_t  = ImClamp(open_t  + (popup_open ? 1.f : -1.f) * io.DeltaTime * 18.f, 0.f, 1.f);
        st->SetFloat(base_id + 0x3001, hover_t);
        st->SetFloat(base_id + 0x3002, open_t);

        ImDrawList* dl = ImGui::GetWindowDrawList();
        float alpha = 0.7f + 0.3f * hover_t;
        ImU32 textCol = ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x, col_text.y, col_text.z, alpha));

        float textY = pos.y + (h - labelSz.y) * 0.5f;
        dl->AddText(ImVec2(pos.x, textY), textCol, label);

        float chevX = pos.x + labelSz.x + 4.f;
        float chevY = pos.y + (h - chevSz.y) * 0.5f;
        float a_down = (1.f - open_t) * alpha;
        float a_up   = open_t * alpha;
        dl->AddText(ImVec2(chevX, chevY), ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x,col_text.y,col_text.z,a_down)), ICON_MDI_CHEVRON_DOWN);
        dl->AddText(ImVec2(chevX, chevY), ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x,col_text.y,col_text.z,a_up)),   ICON_MDI_CHEVRON_UP);

        const float winPadX = 8.f;
        float popupW = 0.f;
        for (int i = 0; i < count; i++)
        {
            float w = ImGui::CalcTextSize(items[i]).x + 22.f + 10.f + winPadX * 2.f; // dot + right pad + win padding
            if (w > popupW) popupW = w;
        }
        popupW = ImMax(popupW, totalW);

        const float rounding = 6.0f;
        ImGui::SetNextWindowPos(ImVec2(pos.x, pos.y + h + 4.f));
        ImGui::SetNextWindowSize(ImVec2(popupW, 0.f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.f, 10.f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertFloat4ToU32(col_popup_bg));
        ImGui::PushStyleColor(ImGuiCol_Border, dark ? theme::PopupBorder.dark : theme::PopupBorder.light);

        bool changed = false;
        if (ImGui::BeginPopupEx(popup_id, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar))
        {
            ImDrawList* pdl = ImGui::GetWindowDrawList();
            const float row_h = ImGui::GetTextLineHeight() + 6.0f;
            const float dot_r = 3.f;
            ImU32 text_u32 = ImGui::ColorConvertFloat4ToU32(col_text);
            ImU32 muted_u32 = ImGui::ColorConvertFloat4ToU32(col_muted);

            if (header)
            {
                ImGui::TextDisabled("%s", header);
                ImGui::Dummy(ImVec2(0, 2.f));
            }
            else
            {
                ImGui::Dummy(ImVec2(0, 2.f));
            }
            for (int i = 0; i < count; i++)
            {
                ImGui::Dummy(ImVec2(0, 2.5f));
                bool sel = (*current == i);
                ImGui::PushID(i);
                ImVec2 rpos = ImGui::GetCursorScreenPos();
                ImRect rbb(rpos, ImVec2(rpos.x + ImGui::GetContentRegionAvail().x, rpos.y + row_h));
                ImGui::InvisibleButton("##o", rbb.GetSize());
                bool rh = ImGui::IsItemHovered();

                if (rh || sel)
                    pdl->AddRectFilled(rbb.Min, rbb.Max, ImGui::ColorConvertFloat4ToU32(
                        ImVec4(col_text.x,col_text.y,col_text.z, rh ? 0.08f : 0.04f)), 4.f);

                if (sel)
                {
                    float dot_x = rbb.Min.x + 10.f;
                    float dot_y = (rbb.Min.y + rbb.Max.y) * 0.5f;
                    pdl->AddCircleFilled(ImVec2(dot_x, dot_y), dot_r,
                        ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x,col_text.y,col_text.z, 0.9f)));
                }

                pdl->AddText(ImVec2(rbb.Min.x + 22.f, rbb.Min.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f),
                    sel ? text_u32 : muted_u32, items[i]);

                if (ImGui::IsItemClicked()) { *current = i; changed = true; ImGui::CloseCurrentPopup(); }
                ImGui::PopID();
            }
            ImGui::Dummy(ImVec2(0, 2.f));

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
        ImGui::PopID();
        return changed;
    }

    static void DrawViewDropdown(ui::ShellState& s)
    {
        static const char* viewLabels[] = { "List", "Tiles", "Table", "Three-Pane" };
        // dropdown order != enum values: Detailed=1, Tiles=2, ThreePane=3, Table=4
        static const ui::ViewMode dropdownToMode[] = {
            ui::ViewMode::Detailed, ui::ViewMode::Tiles, ui::ViewMode::Table, ui::ViewMode::ThreePane
        };
        int viewIdx = 0;
        for (int i = 0; i < 4; i++)
            if (s.view_mode == dropdownToMode[i]) { viewIdx = i; break; }
        if (NakedCombo("##view_combo", ICON_MDI_FORMAT_LIST_BULLETED " View", viewLabels, 4, &viewIdx, s.dark_theme, false, "View layout"))
        {
            s.view_mode = dropdownToMode[viewIdx];
            cfg::set_view_mode((int)s.view_mode);
        }
    }

    static void DrawVaultCombo(ui::ShellState& s)
    {
        if (s.db_labels.empty()) return;
        const int count = (int)s.db_labels.size();
        s.active_db = ImClamp(s.active_db, 0, count - 1);

        const char* labels[16];
        int n = ImMin(count, 16);
        for (int i = 0; i < n; ++i)
            labels[i] = s.db_labels[i].c_str();

        int idx = s.active_db;
        if (NakedCombo("##vault_combo", ICON_MDI_DATABASE " Vault", labels, n, &idx, s.dark_theme, false, "Switch vault"))
        {
            s.active_db = idx;
        }
    }

    static void DrawSelectionMicroToolbar(ui::ShellState& s, uint32_t activeVaultKey)
    {
        const int count = CountSelection(activeVaultKey);
        if (count <= 0) return;

        ImGui::PushID("##sel_toolbar");

        const float h = 34.0f;
        if (ImGui::BeginChild("##selbar", ImVec2(0, h), false,
            ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse))
        {
            const float btnH = 26.0f;
            const float btnR = 4.0f;
            const float gap = 6.0f;

            ImGui::AlignTextToFramePadding();
            ImGui::TextDisabled("%d selected", count);

            const ImGuiHoveredFlags tipFlags =
                ImGuiHoveredFlags_Stationary | ImGuiHoveredFlags_AllowWhenDisabled;

            ImGui::SameLine(0, gap);

            ImGui::BeginDisabled(s.read_only);
            if (StyledButton("##sel_delete", ICON_MDI_DELETE, ImVec2(0, btnH), btnR))
                s.bulk_delete_clicked = true;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(tipFlags))
                SetTooltipPadded(s.read_only ? "Read-only enabled" : "Delete selected");

            ImGui::SameLine(0, gap);

            ImGui::BeginDisabled(s.read_only);
            if (StyledButton("##sel_pin", ICON_MDI_PIN, ImVec2(0, btnH), btnR))
                s.bulk_pin_clicked = true;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(tipFlags))
                SetTooltipPadded(s.read_only ? "Read-only enabled" : "Pin selected");

            ImGui::SameLine(0, gap);

            ImGui::BeginDisabled(s.read_only);
            if (StyledButton("##sel_unpin", ICON_MDI_PIN_OFF, ImVec2(0, btnH), btnR))
                s.bulk_unpin_clicked = true;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(tipFlags))
                SetTooltipPadded(s.read_only ? "Read-only enabled" : "Unpin selected");

            ImGui::SameLine(0, gap);

            ImGui::BeginDisabled(s.read_only);
            if (StyledButton("##sel_fav", ICON_MDI_HEART, ImVec2(0, btnH), btnR))
                s.bulk_fav_clicked = true;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(tipFlags))
                SetTooltipPadded(s.read_only ? "Read-only enabled" : "Favorite selected");

            ImGui::SameLine(0, gap);

            ImGui::BeginDisabled(s.read_only);
            if (StyledButton("##sel_unfav", ICON_MDI_HEART_OFF, ImVec2(0, btnH), btnR))
                s.bulk_unfav_clicked = true;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(tipFlags))
                SetTooltipPadded(s.read_only ? "Read-only enabled" : "Unfavorite selected");

            ImGui::SameLine(0, 12);

            ImGui::BeginDisabled(s.read_only);
            if (StyledButton("##sel_group", ICON_MDI_FOLDER, ImVec2(0, btnH), btnR))
                ImGui::OpenPopup("##bulk_group_popup");
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(tipFlags))
                SetTooltipPadded(s.read_only ? "Read-only enabled" : "Set group");

            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 2));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, IsDarkTheme() ? theme::PopupBg.dark : theme::PopupBg.light);
            ImGui::PushStyleColor(ImGuiCol_Border, IsDarkTheme() ? theme::PopupBorder.dark : theme::PopupBorder.light);
            if (ImGui::BeginPopup("##bulk_group_popup"))
            {
                PopupStyleBegin();

                ImGui::PushFont(render::FontBold);
                ImGui::TextUnformatted("Set Group");
                ImGui::PopFont();
                ImGui::Separator();

                for (const auto& g : s.groups)
                {
                    if (g == "Filter" || g == "All") continue;
                    if (!g.empty() && g[0] == '@') continue;
                    if (g == "---") continue;

                    auto gcIt = s.sb_group_counts.find(g);
                    int gc = gcIt != s.sb_group_counts.end() ? gcIt->second : 0;

                    char label[256];
                    snprintf(label, sizeof(label), ICON_MDI_FOLDER "   %s  (%d)", g.c_str(), gc);

                    if (BarMenuItem(label, false, true))
                    {
                        s.bulk_group_index = 0;
                        for (int i = 0; i < (int)s.groups.size(); i++)
                            if (s.groups[i] == g) { s.bulk_group_index = i; break; }
                        s.bulk_set_group_clicked = true;
                        ImGui::CloseCurrentPopup();
                    }
                }

                PopupStyleEnd();
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(4);

            ImGui::SameLine(0, gap);
            ImGui::BeginDisabled(s.read_only);
            if (StyledButton("##sel_tag", ICON_MDI_TAG, ImVec2(0, btnH), btnR))
                ImGui::OpenPopup("##bulk_tag_popup");
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(tipFlags))
                SetTooltipPadded(s.read_only ? "Read-only enabled" : "Add/remove tags");

            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(6, 2));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, IsDarkTheme() ? theme::PopupBg.dark : theme::PopupBg.light);
            ImGui::PushStyleColor(ImGuiCol_Border, IsDarkTheme() ? theme::PopupBorder.dark : theme::PopupBorder.light);
            if (ImGui::BeginPopup("##bulk_tag_popup"))
            {
                PopupStyleBegin();
                ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

                ImGui::PushFont(render::FontBold);
                ImGui::TextUnformatted("Add Tag");
                ImGui::PopFont();
                ImGui::Separator();

                for (const auto& tag : s.all_tags)
                {
                    auto tcIt = s.sb_tag_counts.find(tag);
                    int tc = tcIt != s.sb_tag_counts.end() ? tcIt->second : 0;

                    char label[256];
                    snprintf(label, sizeof(label), ICON_MDI_TAG "   %s  (%d)", tag.c_str(), tc);

                    if (BarMenuItem(label, false, true))
                        s.bulk_tag_to_add = tag;
                }

                if (!s.all_tags.empty())
                    ImGui::Separator();

                {
                    static std::string s_new_tag_buf;
                    ImGui::SetNextItemWidth(140.0f);
                    bool entered = InputTextString("New tag##bulk_new_tag", &s_new_tag_buf,
                        ImGuiInputTextFlags_EnterReturnsTrue);
                    if (entered && !s_new_tag_buf.empty())
                    {
                        while (!s_new_tag_buf.empty() && s_new_tag_buf.front() == ' ') s_new_tag_buf.erase(s_new_tag_buf.begin());
                        while (!s_new_tag_buf.empty() && s_new_tag_buf.back() == ' ') s_new_tag_buf.pop_back();
                        if (!s_new_tag_buf.empty())
                            s.bulk_tag_to_add = s_new_tag_buf;
                        s_new_tag_buf.clear();
                    }
                }

                ImGui::PopItemFlag();
                PopupStyleEnd();
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(4);

            ImGui::SameLine(0, 12);

            if (StyledButton("##sel_expand", ICON_MDI_ARROW_EXPAND_VERTICAL, ImVec2(0, btnH), btnR))
            {
                for (auto idx : g_selected)
                    g_open.insert(idx);
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
                SetTooltipPadded("Expand all selected");

            ImGui::SameLine(0, gap);

            if (StyledButton("##sel_collapse", ICON_MDI_ARROW_COLLAPSE_VERTICAL, ImVec2(0, btnH), btnR))
            {
                g_open.clear();
                g_tile_state.clear();
                g_hover_opened.clear();
                g_hover_flipped.clear();
                g_notes_visible.clear();
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
                SetTooltipPadded("Collapse all");

            ImGui::SameLine(0, gap);

            if (StyledButton("##sel_all", ICON_MDI_SELECT_ALL, ImVec2(0, btnH), btnR))
                s.select_all_clicked = true;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
                SetTooltipPadded("Select all");

            ImGui::SameLine(0, gap);

            if (StyledButton("##sel_clear", ICON_MDI_CLOSE, ImVec2(0, btnH), btnR))
                s.clear_selection_clicked = true;
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
                SetTooltipPadded("Clear selection");
        }
        ImGui::EndChild();

        //ImGui::Separator();
        ImGui::PopID();
    }

    void DrawListControlsRow(ui::ShellState& s, uint32_t activeVaultKey)
    {
        ImGui::Indent(6.f);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetColorU32(colors::Trans));
        ImGui::PushStyleColor(ImGuiCol_Border, ImGui::GetColorU32(colors::Trans));

        const float containerPad = 6.0f;
        const float innerGap = 14.0f;
        const float btnSize = 24.0f;
        const float btnGap = 10.0f;
        const float containerRounding = 8.0f;

        const float scopeComboW = 95.0f;
        const float pillTabsW = 180.0f;
        const float searchW = 120.0f + 20.0f; // input + icon
        const float sortDropW = ImGui::CalcTextSize(ICON_MDI_SORT " Sort").x + 4.f + ImGui::CalcTextSize(ICON_MDI_CHEVRON_DOWN).x;
        const float viewDropW = ImGui::CalcTextSize(ICON_MDI_FORMAT_LIST_BULLETED " View").x + 4.f + ImGui::CalcTextSize(ICON_MDI_CHEVRON_DOWN).x;
        const float vaultDropW = ImGui::CalcTextSize(ICON_MDI_DATABASE " Vault").x + 4.f + ImGui::CalcTextSize(ICON_MDI_CHEVRON_DOWN).x;
        const float actionsW = (btnSize * 3) + (btnGap * 2);

        const bool isThreePane = (s.view_mode == ui::ViewMode::ThreePane);
        const bool isTable     = (s.view_mode == ui::ViewMode::Table);

        const float container1W = isThreePane ? 0.0f : (scopeComboW + innerGap + pillTabsW + (containerPad * 2));
        const float container2W = searchW + innerGap + sortDropW + innerGap + viewDropW + (containerPad * 2);
        const float actionsRowW = (btnSize * 3) + (btnGap * 2);
        const float container3W = actionsRowW + (containerPad * 2);

        const float availW = ImGui::GetContentRegionAvail().x - 2.0f;
        const float startX = ImGui::GetCursorPosX();

        const float container3Right = startX + availW;
        const float actionsX = container3Right - container3W + containerPad;

        s.sort_mode = ImClamp(s.sort_mode, 0, 3);
        // sort_mode 3 (Recent) only valid in three-pane sidebar
        if (!isThreePane && s.sort_mode == 3) s.sort_mode = 2;
        const float rowY = ImGui::GetCursorPosY();

        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);

        ImVec2 c1Min{}, c1Max{};
        if (!isThreePane)
        {
            static int s_pill_right_clicked = -1;
            ImGui::BeginGroup();
            {
                const char* scopeLabel = GetScopePreviewLabel(s.selected_groups);
                AnimatedComboDotMulti("##scope_combo", scopeLabel, s.groups, s.selected_groups);

                ImGui::SameLine(0, innerGap);

                static PillTabsAnim pillAnim;
                const char* filterLabels[] = { s.pill_tab_0.c_str(), s.pill_tab_1.c_str(), "All" };
                int filterIdx = s.sort_mode;
                int rightClicked = -1;
                PillTabs("##filter_tabs", filterLabels, 3, filterIdx, pillAnim, 4.0f, &rightClicked);
                s.sort_mode = filterIdx;

                if (rightClicked == 0 || rightClicked == 1)
                {
                    s_pill_right_clicked = rightClicked;
                    ImGui::OpenPopup("##pill_tab_popup");
                }
            }

            // outside BeginGroup so popup can overflow
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(8, 6));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(8, 6));
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_PopupBg, IsDarkTheme() ? theme::PopupBg.dark : theme::PopupBg.light);
            ImGui::PushStyleColor(ImGuiCol_Border, s.dark_theme ? theme::PopupBorder.dark : theme::PopupBorder.light);
            if (s_pill_right_clicked >= 0 && ImGui::BeginPopup("##pill_tab_popup"))
            {
                PopupStyleBegin();
                // skip the other tab's current value to prevent duplicates
                const std::string& otherTab = (s_pill_right_clicked == 0) ? s.pill_tab_1 : s.pill_tab_0;

                ImGui::TextDisabled("Set filter tab");

                if (otherTab != "Pinned")
                {
                    if (BarMenuItem(ICON_MDI_PIN " Pinned", false))
                    {
                        if (s_pill_right_clicked == 0) { s.pill_tab_0 = "Pinned"; cfg::set_pill_tab_0("Pinned"); }
                        else                           { s.pill_tab_1 = "Pinned"; cfg::set_pill_tab_1("Pinned"); }
                    }
                }
                if (otherTab != "Favorites")
                {
                    if (BarMenuItem(ICON_MDI_HEART " Favorites", false))
                    {
                        if (s_pill_right_clicked == 0) { s.pill_tab_0 = "Favorites"; cfg::set_pill_tab_0("Favorites"); }
                        else                           { s.pill_tab_1 = "Favorites"; cfg::set_pill_tab_1("Favorites"); }
                    }
                }
                if (otherTab != "Recent")
                {
                    if (BarMenuItem(ICON_MDI_HISTORY " Recent", false))
                    {
                        if (s_pill_right_clicked == 0) { s.pill_tab_0 = "Recent"; cfg::set_pill_tab_0("Recent"); }
                        else                           { s.pill_tab_1 = "Recent"; cfg::set_pill_tab_1("Recent"); }
                    }
                }

                bool hasGroups = false;
                for (const auto& g : s.groups)
                {
                    if (g.empty() || g == "Filter" || g == "---") continue;
                    if (g[0] == '@') continue;
                    if (g == otherTab) continue;
                    if (!hasGroups) { ImGui::Separator(); hasGroups = true; }
                    if (BarMenuItem(g.c_str(), false))
                    {
                        if (s_pill_right_clicked == 0) { s.pill_tab_0 = g; cfg::set_pill_tab_0(g); }
                        else                           { s.pill_tab_1 = g; cfg::set_pill_tab_1(g); }
                    }
                }

                PopupStyleEnd();
                ImGui::EndPopup();
            }
            else
            {
                s_pill_right_clicked = -1;
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(5);

            ImGui::EndGroup();
            c1Min = ImGui::GetItemRectMin();
            c1Max = ImGui::GetItemRectMax();
        }

        float c2SpaceStart = isThreePane ? startX : (c1Max.x - ImGui::GetWindowPos().x + containerPad);
        float c2SpaceEnd   = actionsX - containerPad;
        float c2SpaceW     = c2SpaceEnd - c2SpaceStart;
        float container2X  = c2SpaceStart + (c2SpaceW - container2W) * 0.5f;
        ImGui::SetCursorPos(ImVec2(container2X, rowY));

        ImGui::BeginGroup();
        {
            {
                float inputH = ImGui::GetFrameHeight();
                ImVec2 iconPos = ImGui::GetCursorScreenPos();
                float iconW = ImGui::CalcTextSize(ICON_MDI_MAGNIFY).x;
                ImGui::Dummy(ImVec2(iconW, inputH));
                float iconDrawY = iconPos.y + (inputH - ImGui::GetTextLineHeight()) * 0.5f + 1.8f;
                ImGui::GetWindowDrawList()->AddText(
                    ImVec2(iconPos.x, iconDrawY),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), ICON_MDI_MAGNIFY);
                ImGui::SameLine(0, 4);
                float clearBtnW = g_filter.IsActive() ? 18.0f : 0.0f;
                ImGui::SetNextItemWidth(120.0f - ImGui::CalcTextSize(ICON_MDI_MAGNIFY).x - 4.0f - clearBtnW);
                if (ImGui::InputTextWithHint("##search", " Search vault", g_filter.InputBuf, IM_ARRAYSIZE(g_filter.InputBuf)))
                    g_filter.Build();
                if (g_filter.IsActive())
                {
                    ImGui::SameLine(0, 2);
                    ImVec2 xPos = ImGui::GetCursorScreenPos();
                    float xSz = ImGui::CalcTextSize(ICON_MDI_CLOSE).x;
                    float xY = xPos.y + (inputH - ImGui::GetTextLineHeight()) * 0.5f;
                    ImGui::PushID("##clear_search");
                    if (ImGui::InvisibleButton("##x", ImVec2(xSz + 4, inputH)))
                        g_filter.Clear();
                    ImU32 xCol = ImGui::IsItemHovered()
                        ? ImGui::GetColorU32(ImGuiCol_Text)
                        : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                    ImGui::GetWindowDrawList()->AddText(ImVec2(xPos.x + 2, xY), xCol, ICON_MDI_CLOSE);
                    ImGui::PopID();
                }
            }

            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("##search_filter_popup");
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 10));
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
            ImGui::PushStyleColor(ImGuiCol_PopupBg, s.dark_theme ? theme::PopupBg.dark : theme::ModalBgAlt.light);
            ImGui::PushStyleColor(ImGuiCol_Text, s.dark_theme ? IM_COL32(220, 220, 220, 255) : IM_COL32(30, 30, 30, 255));
            ImGui::PushStyleColor(ImGuiCol_Border, s.dark_theme ? theme::PopupBorder.dark : theme::PopupBorder.light);
            if (ImGui::BeginPopup("##search_filter_popup"))
            {
                PopupStyleBegin();
                ImGui::TextDisabled("Search by");
                const char* labels[] = { "Title", "Email", "Username" };
                for (int i = 0; i < 3; ++i)
                {
                    bool selected = (s.search_filter == i);
                    if (BarMenuItem(labels[i], selected, true, true))
                        s.search_filter = i;
                }
                PopupStyleEnd();
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(3);

            ImGui::SameLine(0, innerGap);
            DrawSortDropdown(s);

            ImGui::SameLine(0, innerGap);
            DrawViewDropdown(s);

        }
        ImGui::EndGroup();
        ImVec2 c2Min = ImGui::GetItemRectMin();
        ImVec2 c2Max = ImGui::GetItemRectMax();

        ImGui::SetCursorPos(ImVec2(actionsX, rowY));

        ImGui::BeginGroup();
        {
            if (IconButtonGhost("ctrlrow_add", ICON_MDI_ACCOUNT_PLUS, btnSize, !s.read_only))
                s.add_clicked = true;

            ImGui::SameLine(0, btnGap);

            if (IconButtonGhost("ctrlrow_settings", ICON_MDI_COG, btnSize, true))
                s.settings_clicked = true;

            ImGui::SameLine(0, btnGap);

            if (IconButtonGhost("ctrlrow_more", ICON_MDI_DOTS_VERTICAL, btnSize, true))
                ImGui::OpenPopup("##ctrlrow_more_popup");
        }
        ImGui::EndGroup();

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 10));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, s.dark_theme ? theme::PopupBg.dark : theme::ModalBgAlt.light);
        ImGui::PushStyleColor(ImGuiCol_Text, s.dark_theme ? IM_COL32(220, 220, 220, 255) : IM_COL32(30, 30, 30, 255));
        ImGui::PushStyleColor(ImGuiCol_Border, s.dark_theme ? theme::PopupBorder.dark : theme::PopupBorder.light);
        if (ImGui::BeginPopup("##ctrlrow_more_popup"))
        {
            PopupStyleBegin();

            auto DrawPopupSep = [&]() {
                ImGui::Spacing();
                ImVec2 p = ImGui::GetCursorScreenPos();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                float x0 = ImGui::GetWindowPos().x;
                float x1 = x0 + ImGui::GetWindowSize().x;
                dl->AddLine(ImVec2(x0, p.y), ImVec2(x1, p.y),
                    ImGui::GetColorU32(ImGuiCol_Separator, 0.4f), 1.0f);
                ImGui::Spacing();
            };

            if (BarMenuItem(ICON_MDI_LOCK " Lock Vault", false))
                s.footer_close_clicked = true;

            DrawPopupSep();

            if (BarMenuItem(ICON_MDI_SHIELD_CHECK " Security Center", false))
                s.sec_center_open = true;

            if (BarMenuItem(ICON_MDI_TRASH_CAN " Trash Bin", false))
                s.trash_modal_open = true;

            {
                DrawPopupSep();
                ImGui::Spacing();
                bool canSave = (s.dirty && !s.read_only);
                bool canUndo = s.can_undo && !s.read_only;
                float avail = ImGui::GetContentRegionAvail().x;
                float btnW = (avail - 6.0f) * 0.5f;
                float btnH = 26.0f;

                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

                if (!canSave) ImGui::BeginDisabled();
                if (StyledButton("##ctrl_save", ICON_MDI_CONTENT_SAVE, ImVec2(btnW, btnH)))
                    s.footer_save_clicked = true;
                if (ImGui::IsItemHovered()) SetTooltipPadded("Save");
                if (!canSave) ImGui::EndDisabled();

                ImGui::SameLine(0, 6.0f);

                if (!canUndo) ImGui::BeginDisabled();
                if (StyledButton("##ctrl_undo", ICON_MDI_HISTORY, ImVec2(btnW, btnH)))
                    s.undo_clicked = true;
                if (ImGui::IsItemHovered()) SetTooltipPadded("Undo");
                if (!canUndo) ImGui::EndDisabled();

                ImGui::PopStyleVar();
            }

            PopupStyleEnd();
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);

        ImGui::PopStyleColor(2);
        dl->ChannelsSetCurrent(0);
        const ImU32 containerBg = GetControlsContainerBg();
        if (!isThreePane)
        {
            ImVec2 bgMin(c1Min.x - containerPad, c1Min.y - containerPad);
            ImVec2 bgMax(c1Max.x + containerPad, c1Max.y + containerPad);
            dl->AddRectFilled(bgMin, bgMax, containerBg, containerRounding);
        }
        {
            ImVec2 bgMin(c2Min.x - containerPad, c2Min.y - containerPad);
            ImVec2 bgMax(c2Max.x + containerPad, c2Max.y + containerPad);
            dl->AddRectFilled(bgMin, bgMax, containerBg, containerRounding);
        }
        dl->ChannelsMerge();

        ImGui::Dummy(ImVec2(0, 7));

        ImGui::BeginDisabled(s.read_only);
        DrawSelectionMicroToolbar(s, activeVaultKey);
        ImGui::EndDisabled();
		ImGui::Unindent(6.f);

        {
            ImVec2 winPos = ImGui::GetWindowPos();
            ImVec2 winSize = ImGui::GetWindowSize();
            float lineY = ImGui::GetCursorScreenPos().y + 3;
            dl->AddLine(
                ImVec2(winPos.x, lineY),
                ImVec2(winPos.x + winSize.x, lineY),
                GetSeparatorColor(),
                1.0f);
        }

        //ImGui::Dummy(ImVec2(0, 6));
    }

    void RenderSettingsPage(ui::ShellState& s)
    {
        if (!s.settings_modal_open) return;

        if (!ImGui::IsPopupOpen("Settings###settings_modal"))
            ImGui::OpenPopup("Settings###settings_modal");

        g_category_card_id = 0;
        static ImGuiID g_settings_selected_row = 0;

        // slightly lighter card bg inside the modal
        ImVec4 savedCardDark  = s.card_bg_dark;
        ImVec4 savedCardLight = s.card_bg_light;
        s.card_bg_dark  = ImVec4(44/255.0f, 43/255.0f, 48/255.0f, 1.0f);
        s.card_bg_light = ImVec4(243/255.0f, 243/255.0f, 246/255.0f, 1.0f);

        bool dark = IsDarkTheme();
        ImU32 popupBg = dark ? theme::ModalBg.dark : theme::ModalBg.light;
        ImU32 dimBg   = colors::DimOverlayLight;

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowSize(ImVec2(660, 580), ImGuiCond_Always);
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImColor(popupBg).Value);
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, ImColor(dimBg).Value);

        if (!ImGui::BeginPopupModal("Settings###settings_modal", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar))
        {
            s.card_bg_dark = savedCardDark;
            s.card_bg_light = savedCardLight;
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            s.settings_modal_open = false;
            return;
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            s.settings_modal_open = false;
            s.card_bg_dark = savedCardDark;
            s.card_bg_light = savedCardLight;
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);
            if (s.theme_changed) { ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = s.dark_theme ? s.window_bg_dark : s.window_bg_light; }
            return;
        }

        ImGui::PushFont(render::FontLarge);
        ImGui::TextUnformatted("Settings");
        ImGui::PopFont();
        {
            ImVec2 closeSz = ImGui::CalcTextSize(ICON_MDI_CLOSE);
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - closeSz.x);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(ICON_MDI_CLOSE);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0))
            {
                s.settings_modal_open = false;
                s.card_bg_dark = savedCardDark;
                s.card_bg_light = savedCardLight;
                ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(3);
                if (s.theme_changed) { ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = s.dark_theme ? s.window_bg_dark : s.window_bg_light; }
                return;
            }
        }

        ImGui::Spacing();

        static char s_settings_search_buf[128] = {};
        static bool s_settings_search_focus = false;

        const char* tabLabels[] = { "Vault", "Security", "Backup", "Transfer" };
        const char* tabIcons[] = { ICON_MDI_COG, ICON_MDI_SHIELD_CHECK, ICON_MDI_HISTORY, ICON_MDI_SWAP_HORIZONTAL };
        const int tabCount = 4;

        {
            const float tabH = 30.0f;
            const float pillR = 7.0f;
            const float cardR = 5.0f;
            const float pillPad = 2.0f;
            const float searchGap = 12.0f;
            bool dk = IsDarkTheme();

            ImU32 pillBg = dk ? IM_COL32(40, 40, 44, 255) : IM_COL32(232, 232, 236, 255);
            ImU32 cardBg = dk ? IM_COL32(60, 60, 66, 255) : IM_COL32(255, 255, 255, 255);
            ImU32 cardShadow = dk ? IM_COL32(0, 0, 0, 70) : IM_COL32(0, 0, 0, 35);

            float pillW = 0.0f;
            for (int i = 0; i < tabCount; i++)
            {
                char buf[64];
                snprintf(buf, sizeof(buf), "%s %s", tabIcons[i], tabLabels[i]);
                pillW += ImGui::CalcTextSize(buf).x + 24.0f;
            }
            float iconBtnSize = tabH;
            float totalW = pillW + searchGap + iconBtnSize;

            float availWidth = ImGui::GetContentRegionAvail().x;
            float offsetX = (availWidth - totalW) * 0.5f;
            if (offsetX > 0.0f) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offsetX);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImVec2 pillPos = ImGui::GetCursorScreenPos();

            dl->AddRectFilled(pillPos, ImVec2(pillPos.x + pillW, pillPos.y + tabH), pillBg, pillR);

            float segW = pillW / tabCount;
            int activeIdx = s.settings_tab_index;

            {
                float cardX = pillPos.x + activeIdx * segW + pillPad;
                float cardY = pillPos.y + pillPad;
                float cardW = segW - pillPad * 2.0f;
                float cardH = tabH - pillPad * 2.0f;
                dl->AddRectFilled(
                    ImVec2(cardX + 0.5f, cardY + 1.0f),
                    ImVec2(cardX + cardW + 0.5f, cardY + cardH + 1.0f),
                    cardShadow, cardR);
                dl->AddRectFilled(
                    ImVec2(cardX, cardY),
                    ImVec2(cardX + cardW, cardY + cardH),
                    cardBg, cardR);
            }

            for (int i = 0; i < tabCount; i++)
            {
                bool active = (i == activeIdx);
                float segX = pillPos.x + i * segW;

                ImGui::SetCursorScreenPos(ImVec2(segX, pillPos.y));
                ImGui::PushID(i);
                ImGui::InvisibleButton("##stab", ImVec2(segW, tabH));
                bool hovered = ImGui::IsItemHovered();
                if (ImGui::IsItemClicked()) s.settings_tab_index = i;
                ImGui::PopID();

                if (hovered && !active)
                {
                    ImU32 hovCol = dk ? IM_COL32(255, 255, 255, 10) : IM_COL32(0, 0, 0, 8);
                    dl->AddRectFilled(
                        ImVec2(segX + pillPad, pillPos.y + pillPad),
                        ImVec2(segX + segW - pillPad, pillPos.y + tabH - pillPad),
                        hovCol, cardR);
                }

                char buf[64];
                snprintf(buf, sizeof(buf), "%s %s", tabIcons[i], tabLabels[i]);
                ImVec2 tSz = ImGui::CalcTextSize(buf);
                float tx = segX + (segW - tSz.x) * 0.5f;
                float ty = pillPos.y + (tabH - tSz.y) * 0.5f;
                ImU32 textCol = active
                    ? ImGui::GetColorU32(ImGuiCol_Text)
                    : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                dl->AddText(ImVec2(tx, ty), textCol, buf);
            }

            ImGui::SetCursorScreenPos(ImVec2(pillPos.x + pillW, pillPos.y));

            ImGui::SameLine(0, searchGap);
            float iconNudge = (iconBtnSize - tabH) * 0.5f;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + (tabH - iconBtnSize) * 0.5f);
        }

        float iconBtnSize = 30.0f;
        if (IconButtonGhost("settings_search", ICON_MDI_MAGNIFY, iconBtnSize, true))
        {
            ImGui::OpenPopup("##settings_search_popup");
            s_settings_search_buf[0] = '\0';
            s_settings_search_focus = true;
        }

        struct SettingsSearchEntry {
            const char* title;
            const char* subtitle;
            const char* icon;
            const char* category;
            int tab_index;
        };

        static const SettingsSearchEntry s_search_entries[] = {
            // Tab 0 - Vault
            { "Lock Vault",       "Lock and secure your vault",                ICON_MDI_LOCK,              "Vault",    0 },
            { "Create New Vault", "Start fresh with a new vault",              ICON_MDI_FILE_PLUS,  "Vault",    0 },
            { "Open Vault",       "Open an existing vault file",               ICON_MDI_FOLDER_OPEN,       "Vault",    0 },
            { "Read-only Mode",   "Prevent all edits and deletions",           ICON_MDI_EYE,               "Vault",    0 },
            { "Auto-save",        "Save automatically when changes are made",  ICON_MDI_CONTENT_SAVE,       "Vault",    0 },
            { "Expand on Hover",  "Automatically expand items when hovered",   ICON_MDI_CURSOR_DEFAULT_CLICK,      "Vault",    0 },
            { "Always on Top",    "Window stays above others",                 ICON_MDI_PIN,         "Vault",    0 },
            { "Dark Mode",        "Switch between dark and light theme",       ICON_MDI_WEATHER_NIGHT,              "Vault",    0 },
            { "Group Count",      "Show item count in group headers",          ICON_MDI_POUND,           "Vault",    0 },
            { "Font Scale",       "Adjust global text size",                   ICON_MDI_FORMAT_SIZE,      "Vault",    0 },
            { "Privacy Mode",     "Mask personal info with bullets",           ICON_MDI_EYE_OFF,  "Vault",    0 },
            { "Blur on Unfocus",  "Dim the app when it loses focus",           ICON_MDI_BLUR,             "Vault",    0 },
            // Tab 1 - Security
            { "Clear Clipboard",  "Auto-clear copied passwords",               ICON_MDI_CLIPBOARD,         "Security", 1 },
            { "Auto-lock",        "Lock vault after inactivity",               ICON_MDI_CLOCK,             "Security", 1 },
            { "Two-Factor Auth",  "Require authenticator code to unlock",      ICON_MDI_SHIELD_HALF_FULL,     "Security", 1 },
            { "Website Icons",    "Fetch site icons over the internet",        ICON_MDI_IMAGE_OUTLINE,     "Security", 1 },
            { "Online Breach Check", "HIBP k-anonymity password check",        ICON_MDI_SHIELD_SEARCH,     "Security", 1 },
            { "Favicon Cache",    "View or clear downloaded site icons",       ICON_MDI_DELETE_SWEEP,      "Security", 1 },
            // Tab 2 - Backup
            { "Auto-backup",      "Create backup when saving vault",           ICON_MDI_HISTORY, "Backup",   2 },
            { "Keep backups",     "Maximum backup files to retain",            ICON_MDI_DATABASE,          "Backup",   2 },
            // Tab 3 - Transfer
            { "Export as CSV",     "Spreadsheet compatible",                    ICON_MDI_FILE_EXCEL,        "Transfer", 3 },
            { "Export as PWM",     "Encrypted backup format",                   ICON_MDI_LOCK,              "Transfer", 3 },
            { "Export as KDBX",    "KeePass compatible",                        ICON_MDI_FILE_EXPORT,       "Transfer", 3 },
            { "Import PWM",        "Restore from encrypted backup",             ICON_MDI_FILE_IMPORT,       "Transfer", 3 },
            { "Import CSV",        "Chrome, Bitwarden, LastPass, 1Password",    ICON_MDI_FILE_DELIMITED,          "Transfer", 3 },
        };
        static const int s_search_entry_count = IM_ARRAYSIZE(s_search_entries);

        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IsDarkTheme() ? theme::PopupBg.dark : theme::PopupBg.light);
        ImGui::SetNextWindowSizeConstraints(ImVec2(300, 0), ImVec2(300, FLT_MAX));
        // right edge of popup aligns with the search button
        ImVec2 btnMax = ImGui::GetItemRectMax();
        ImGui::SetNextWindowPos(ImVec2(btnMax.x - 300, btnMax.y + 4.0f), ImGuiCond_Appearing);
        if (ImGui::BeginPopup("##settings_search_popup"))
        {
            PopupStyleBegin();
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
            float clearW = (s_settings_search_buf[0] != '\0') ? 20.0f : 0.0f;
            ImGui::SetNextItemWidth(-1.0f - clearW);
            if (s_settings_search_focus)
            {
                ImGui::SetKeyboardFocusHere();
                s_settings_search_focus = false;
            }
            ImGui::InputTextWithHint("##settings_search", ICON_MDI_MAGNIFY " Search settings...", s_settings_search_buf, IM_ARRAYSIZE(s_settings_search_buf));
            if (s_settings_search_buf[0] != '\0')
            {
                ImGui::SameLine(0, 2);
                float frameH = ImGui::GetFrameHeight();
                ImVec2 xPos = ImGui::GetCursorScreenPos();
                float xSz = ImGui::CalcTextSize(ICON_MDI_CLOSE).x;
                float xY = xPos.y + (frameH - ImGui::GetTextLineHeight()) * 0.5f;
                ImGui::PushID("##clear_settings_search");
                if (ImGui::InvisibleButton("##x", ImVec2(xSz + 4, frameH)))
                    s_settings_search_buf[0] = '\0';
                ImU32 xCol = ImGui::IsItemHovered()
                    ? ImGui::GetColorU32(ImGuiCol_Text)
                    : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                ImGui::GetWindowDrawList()->AddText(ImVec2(xPos.x + 2, xY), xCol, ICON_MDI_CLOSE);
                ImGui::PopID();
            }
            ImGui::PopStyleVar();

            if (s_settings_search_buf[0] != '\0')
            {
                ImGui::Spacing();
                ImGui::Separator();
                ImGui::Spacing();

                static ImGuiID g_search_selected_row = 0;
                int matchCount = 0;

                for (int i = 0; i < s_search_entry_count; ++i)
                {
                    const auto& e = s_search_entries[i];

                    bool titleMatch = ImStristr(e.title, nullptr, s_settings_search_buf, nullptr) != nullptr;
                    bool subMatch   = e.subtitle && ImStristr(e.subtitle, nullptr, s_settings_search_buf, nullptr) != nullptr;
                    if (!titleMatch && !subMatch) continue;

                    if (matchCount > 0) DrawRowDivider();

                    ImGui::PushID(i);
                    float rowW = ImGui::GetContentRegionAvail().x;
                    float rowH = 44.0f;
                    ImVec2 rMin = ImGui::GetCursorScreenPos();
                    ImRect rr(rMin, ImVec2(rMin.x + rowW, rMin.y + rowH));
                    ImGui::ItemSize(ImVec2(rowW, rowH));
                    ImGuiID rid = ImGui::GetID("##sr");
                    if (ImGui::ItemAdd(rr, rid))
                    {
                        bool hov = false, held2 = false;
                        bool click = ImGui::ButtonBehavior(rr, rid, &hov, &held2);
                        if (hov)
                        {
                            ImU32 hlCol = ImGui::GetColorU32(ImGuiCol_Text, 0.06f);
                            ImGui::GetWindowDrawList()->AddRectFilled(rr.Min, rr.Max, hlCol, 4.0f);
                        }

                        float cx = rr.Min.x + 6.0f;
                        float cy = rr.Min.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                        ImGui::GetWindowDrawList()->AddText(ImVec2(cx, cy), ImGui::GetColorU32(ImGuiCol_TextDisabled), e.icon);
                        cx += ImGui::CalcTextSize(e.icon).x + 8.0f;
                        float textMaxX = rr.Max.x - 6.0f;
                        ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(cx, rr.Min.y + 6.0f),
                            ImGui::GetColorU32(ImGuiCol_Text), e.title, nullptr, textMaxX - cx);
                        if (e.subtitle)
                        {
                            ImGui::PushFont(render::FontSmall);
                            ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(cx, rr.Min.y + 24.0f),
                                ImGui::GetColorU32(ImGuiCol_TextDisabled), e.subtitle, nullptr, textMaxX - cx);
                            ImGui::PopFont();
                        }

                        if (click)
                        {
                            s.settings_tab_index = e.tab_index;
                            g_settings_scroll_target = e.title;
                            s_settings_search_buf[0] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::PopID();

                    ++matchCount;
                }

                if (matchCount == 0)
                    ImGui::TextDisabled("No settings found.");
            }

            PopupStyleEnd();
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::Dummy(ImVec2(0, 16));

        float avail_w = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::BeginChild("##settings_content", ImVec2(avail_w, 0), ImGuiChildFlags_None);

        // close combo popups when user scrolls
        {
            static float s_prev_scroll_y = 0.0f;
            float curScroll = ImGui::GetScrollY();
            if (curScroll != s_prev_scroll_y)
            {
                ImGuiContext& g = *ImGui::GetCurrentContext();
                if (g.OpenPopupStack.Size > 1)
                    ImGui::ClosePopupToLevel(1, true);
                s_prev_scroll_y = curScroll;
            }
        }

        if (s.settings_tab_index == 2)
        {
            BeginCategoryCard("Local Backup", "Configure offline backups");

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("auto_backup");
                row.title = "Auto-backup";
                row.subtitle = "Create backup when saving vault";
                row.leftIcon = ICON_MDI_HISTORY;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s.auto_backup;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used) cfg::set_auto_backup(s.auto_backup);
                DrawRowDivider();
            }

            {
                const float rowH = 80.0f;
                const float padX = 18.0f;
                ImVec2 rowMin = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(0, rowH));

                ImDrawList* sdl = ImGui::GetWindowDrawList();
                float cy = rowMin.y + rowH * 0.5f;

                float iconX = rowMin.x + padX;
                ImVec2 iconSz = ImGui::CalcTextSize(ICON_MDI_DATABASE);
                sdl->AddText(ImVec2(iconX, cy - iconSz.y * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), ICON_MDI_DATABASE);

                float textX = iconX + iconSz.x + 10.0f;
                sdl->AddText(ImVec2(textX, rowMin.y + 28.0f), ImGui::GetColorU32(ImGuiCol_Text), "Keep backups");
                ImGui::PushFont(render::FontSmall);
                sdl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(textX, rowMin.y + 46.0f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), "Maximum backup files to retain");
                ImGui::PopFont();

                float stepW = 56.0f;
                float stepX = ImGui::GetContentRegionMax().x + ImGui::GetWindowPos().x - stepW - padX;
                ImGui::SetCursorScreenPos(ImVec2(stepX, rowMin.y + (rowH - 70.0f) * 0.5f));
                if (VerticalStepper("##backup_keep_vs", &s.backup_keep_count, 1, 50))
                    cfg::set_backup_keep_count(s.backup_keep_count);

                ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMin.y + rowH));
                DrawRowDivider();
            }

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("last_backup");
                row.title = "Last backup";
                row.subtitle = s.last_backup_label.empty() ? "No backups yet" : s.last_backup_label.c_str();
                row.leftIcon = ICON_MDI_CLOCK;
                row.rightType = SettingRightType::None;

                RenderSettingRow(row, &g_settings_selected_row, 54.0f);
            }

            EndCategoryCard();

            BeginCategoryCard("Local Backup Files", "Select a backup to restore");

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("manual_backup");
                row.title = "Create Backup";
                row.subtitle = "Save a backup of your vault now";
                row.leftIcon = ICON_MDI_DOWNLOAD;
                row.rightType = SettingRightType::Button;
                row.buttonLabel = "Backup";

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                    s.footer_create_backup_clicked = true;
                DrawRowDivider();
            }

            {
                std::string sub;
                if (!s.footer_browse_backup_path.empty())
                {
                    size_t pos = s.footer_browse_backup_path.find_last_of("\\/");
                    sub = (pos != std::string::npos)
                        ? s.footer_browse_backup_path.substr(pos + 1)
                        : s.footer_browse_backup_path;
                }
                else
                    sub = "No file selected";

                SettingRowSpec row{};
                row.id = ImGui::GetID("backup_browse");
                row.title = "Backup File";
                row.subtitle = sub.c_str();
                row.leftIcon = ICON_MDI_FOLDER_ZIP;
                row.rightType = SettingRightType::Button;
                row.buttonLabel = "Browse";

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                    s.footer_browse_backup_clicked = true;
                DrawRowDivider();
            }

            {
                bool canRestore = !s.footer_browse_backup_path.empty();

                std::string sub;
                if (canRestore)
                {
                    size_t pos = s.footer_browse_backup_path.find_last_of("\\/");
                    sub = (pos != std::string::npos)
                        ? s.footer_browse_backup_path.substr(pos + 1)
                        : s.footer_browse_backup_path;
                }
                else
                    sub = "Browse for a backup first";

                SettingRowSpec row{};
                row.id = ImGui::GetID("restore_backup");
                row.title = "Restore Selected";
                row.subtitle = sub.c_str();
                row.leftIcon = ICON_MDI_REFRESH;
                row.rightType = SettingRightType::Button;
                row.buttonLabel = "Restore";
                row.enabled = canRestore;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used && canRestore)
                    s.footer_restore_clicked = true;
            }

            EndCategoryCard();

        }
        else if (s.settings_tab_index == 0)
        {
            BeginCategoryCard("Vault Actions", "Manage your vault");

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("lock_vault");
                row.title = "Lock Vault";
                row.subtitle = "Lock and secure your vault";
                row.leftIcon = ICON_MDI_LOCK;
                row.rightType = SettingRightType::Button;
                row.buttonLabel = "Lock";

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                    s.footer_close_clicked = true;
                DrawRowDivider();
            }

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("create_vault");
                row.title = "Create New Vault";
                row.subtitle = "Start fresh with a new vault";
                row.leftIcon = ICON_MDI_FILE_PLUS;
                row.rightType = SettingRightType::Button;
                row.buttonLabel = "Create";

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                    s.goto_locked_clicked = true;
                DrawRowDivider();
            }

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("open_vault");
                row.title = "Open Vault";
                row.subtitle = "Open an existing vault file";
                row.leftIcon = ICON_MDI_FOLDER_OPEN;
                row.rightType = SettingRightType::Button;
                row.buttonLabel = "Open";

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                    s.footer_open_db_clicked = true;
                DrawRowDivider();
            }

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("delete_vault");
                row.title = "Delete Vault";
                row.subtitle = "Permanently delete vault and all credentials";
                row.leftIcon = ICON_MDI_DELETE_FOREVER;
                row.rightType = SettingRightType::Button;
                row.buttonLabel = "Delete";

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                    s.delete_vault_clicked = true;
            }

            EndCategoryCard();

            BeginCategoryCard("Settings", "Vault behavior");

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("always_on_top");
                row.title = "Always on Top";
                row.subtitle = s.always_on_top ? "Window stays above others" : "Normal window behavior";
                row.leftIcon = ICON_MDI_PIN;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s.always_on_top;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    s.always_on_top_changed = true;
                    cfg::set_always_on_top(s.always_on_top);
                }
                DrawRowDivider();
            }

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("autosave");
                row.title = "Auto-save";
                row.subtitle = "Save automatically when changes are made";
                row.leftIcon = ICON_MDI_CONTENT_SAVE;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s.autosave_enabled;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used) cfg::set_autosave_enabled(s.autosave_enabled);
                DrawRowDivider();
            }

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("auto_open_vault");
                row.title = "Auto-open Last Vault";
                row.subtitle = s.auto_open_vault ? "Pre-selects last opened vault on startup" : "Start with no vault selected";
                row.leftIcon = ICON_MDI_DATABASE_ARROW_RIGHT;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s.auto_open_vault;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used) cfg::set_auto_open_vault(s.auto_open_vault);
                DrawRowDivider();
            }

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("hover_expand");
                row.title = "Expand on Hover";
                row.subtitle = "Automatically expand items when hovered";
                row.leftIcon = ICON_MDI_CURSOR_DEFAULT_CLICK;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s.hover_expand;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used) cfg::set_hover_expand(s.hover_expand);
                DrawRowDivider();
            }

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("minimize_to_tray");
                row.title = "Minimize to Tray";
                row.subtitle = s.minimize_to_tray ? "Close & minimize hide to system tray" : "Normal close & minimize behavior";
                row.leftIcon = ICON_MDI_TRAY_ARROW_DOWN;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s.minimize_to_tray;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used) cfg::set_minimize_to_tray(s.minimize_to_tray);
                DrawRowDivider();
            }

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("start_on_boot");
                row.title = "Start on Boot";
                row.subtitle = s.start_on_boot ? "Launches at Windows startup" : "Manual launch only";
                row.leftIcon = ICON_MDI_ROCKET_LAUNCH;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s.start_on_boot;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used) cfg::set_start_on_boot(s.start_on_boot);
                DrawRowDivider();
            }

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("start_minimized");
                row.title = "Start Minimized";
                row.subtitle = s.start_minimized ? "Launches hidden in system tray" : "Launches with window visible";
                row.leftIcon = ICON_MDI_WINDOW_MINIMIZE;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s.start_minimized;
                row.enabled = s.minimize_to_tray;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used) cfg::set_start_minimized(s.start_minimized);
                DrawRowDivider();
            }

            {
                static bool s_read_only = false;
                static bool s_read_only_pending = false;
                s_read_only = s.read_only;

                if (s_read_only_pending && IsRepromptApproved(RepromptAction::DisableReadOnly))
                {
                    s.footer_set_read_only = true;
                    s.footer_set_read_only_value = false;
                    s_read_only_pending = false;
                    ClearRepromptApproval(RepromptAction::DisableReadOnly);
                }

                SettingRowSpec row{};
                row.id = ImGui::GetID("read_only");
                row.title = "Read-only Mode";
                row.subtitle = "Prevent all edits and deletions";
                row.leftIcon = ICON_MDI_EYE;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s_read_only;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    // turning off requires re-prompt
                    if (s.read_only && !s_read_only)
                    {
                        s_read_only_pending = true;
                        if (RequestReprompt(RepromptAction::DisableReadOnly, ""))
                        {
                            s.footer_set_read_only = true;
                            s.footer_set_read_only_value = false;
                            s_read_only_pending = false;
                        }
                        else
                        {
                            s_read_only = true; // revert toggle while waiting for re-prompt
                        }
                    }
                    else
                    {
                        s.footer_set_read_only = true;
                        s.footer_set_read_only_value = s_read_only;
                    }
                }
            }

            EndCategoryCard();

            BeginCategoryCard("Style", "Appearance and layout");

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("theme_toggle");
                row.title = "Dark Mode";
                row.subtitle = s.dark_theme ? "Dark theme enabled" : "Light theme enabled";
                row.leftIcon = s.dark_theme ? ICON_MDI_WEATHER_NIGHT : ICON_MDI_WHITE_BALANCE_SUNNY;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s.dark_theme;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    s.theme_changed = true;
                    cfg::set_theme(s.dark_theme ? 0 : 1);
                    render::ApplyTheme(s.dark_theme);

                    // re-apply user's custom WindowBg on top of theme defaults
                    const ImVec4& wb = s.dark_theme ? s.window_bg_dark : s.window_bg_light;
                    ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = wb;
                }
                DrawRowDivider();
            }

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("show_group_count");
                row.title = "Group Count";
                row.subtitle = "Show item count in group headers";
                row.leftIcon = ICON_MDI_POUND;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s.show_group_count;
                auto r = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (r.action_used) cfg::set_show_group_count(s.show_group_count);
                DrawRowDivider();
            }

            {
                static const float fontScaleSteps[] = { 0.8f, 0.9f, 1.0f, 1.1f, 1.2f, 1.3f, 1.4f };
                static const int fontScaleCount = IM_ARRAYSIZE(fontScaleSteps);

                static int fontIdx = 2;
                for (int i = 0; i < fontScaleCount; i++)
                    if (fabsf(s.font_scale - fontScaleSteps[i]) < 0.01f) { fontIdx = i; break; }

                const float rowH = 54.0f;
                const float padX = 18.0f;
                ImVec2 rowMin = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(0, rowH));

                ImDrawList* dl = ImGui::GetWindowDrawList();
                float cy = rowMin.y + rowH * 0.5f;

                float iconX = rowMin.x + padX;
                ImVec2 iconSz = ImGui::CalcTextSize(ICON_MDI_FORMAT_SIZE);
                dl->AddText(ImVec2(iconX, cy - iconSz.y * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), ICON_MDI_FORMAT_SIZE);

                float textX = iconX + iconSz.x + 10.0f;
                char fontSubBuf[32];
                snprintf(fontSubBuf, sizeof(fontSubBuf), "%.1fx", fontScaleSteps[fontIdx]);
                dl->AddText(ImVec2(textX, rowMin.y + 12.0f), ImGui::GetColorU32(ImGuiCol_Text), "Font Scale");
                ImGui::PushFont(render::FontSmall);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(textX, rowMin.y + 30.0f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), fontSubBuf);
                ImGui::PopFont();

                float dotW = (fontScaleCount - 1) * 22.0f + 24.0f + 2 * 24.0f + 2 * 4.0f;
                float dotX = ImGui::GetContentRegionMax().x + ImGui::GetWindowPos().x - dotW - padX;
                float dotH = 24.0f;
                ImGui::SetCursorScreenPos(ImVec2(dotX, rowMin.y + (rowH - dotH) * 0.5f));
                if (DotSlider("##font_dots", &fontIdx, fontScaleCount))
                {
                    s.font_scale = fontScaleSteps[fontIdx];
                    cfg::set_font_scale(s.font_scale);
                }
                ImGui::GetIO().FontGlobalScale = s.font_scale;
                ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMin.y + rowH));
                DrawRowDivider();
            }

            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("privacy_mode");
                row.title = "Privacy Mode";
                row.subtitle = "Mask personal info with bullets";
                row.leftIcon = ICON_MDI_EYE_OFF;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s.privacy_mode;
                auto r = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (r.action_used) cfg::set_privacy_mode(s.privacy_mode);
            }

            EndCategoryCard();
        }
        else if (s.settings_tab_index == 1)
        {
            BeginCategoryCard("Session Security", "Clipboard and lock behavior");

            {
                static const char* clipboard_options[] = { "Never", "10s", "20s", "60s" };
                static const int clipboard_values[] = { 0, 10, 20, 60 };
                static int s_clipboard_index = 2; // default 20s

                static bool s_clipboard_init = false;
                if (!s_clipboard_init) {
                    int delay = cfg::get_clipboard_clear_delay();
                    for (int i = 0; i < 4; ++i) {
                        if (clipboard_values[i] == delay) { s_clipboard_index = i; break; }
                    }
                    s_clipboard_init = true;
                }

                SettingRowSpec row{};
                row.id = ImGui::GetID("clipboard_clear");
                row.title = "Clear Clipboard";
                row.subtitle = "Auto-clear copied passwords";
                row.leftIcon = ICON_MDI_CLIPBOARD;
                row.rightType = SettingRightType::AnimatedCombo;
                row.comboItems = clipboard_options;
                row.comboCount = 4;
                row.comboIndex = &s_clipboard_index;
                row.comboWidth = 95.0f;
                row.comboHeight = 30.0f;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    cfg::set_clipboard_clear_delay(clipboard_values[s_clipboard_index]);
                }
                DrawRowDivider();
            }

            {
                static const char* autolock_options[] = { "Never", "1 min", "5 min", "15 min" };
                static const int autolock_values[] = { 0, 60, 300, 900 };
                static int s_autolock_index = 0; // default Never

                static bool s_autolock_init = false;
                if (!s_autolock_init) {
                    int timeout = cfg::get_auto_lock_timeout();
                    for (int i = 0; i < 4; ++i) {
                        if (autolock_values[i] == timeout) { s_autolock_index = i; break; }
                    }
                    s_autolock_init = true;
                }

                SettingRowSpec row{};
                row.id = ImGui::GetID("autolock");
                row.title = "Auto-lock";
                row.subtitle = "Lock vault after inactivity";
                row.leftIcon = ICON_MDI_CLOCK;
                row.rightType = SettingRightType::AnimatedCombo;
                row.comboItems = autolock_options;
                row.comboCount = 4;
                row.comboIndex = &s_autolock_index;
                row.comboWidth = 95.0f;
                row.comboHeight = 30.0f;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    cfg::set_auto_lock_timeout(autolock_values[s_autolock_index]);
                }
            }

            EndCategoryCard();

            BeginCategoryCard("Two-Factor Authentication", "Require authenticator code to unlock");
            {
                bool twofa_on = twofa_ops::is_enabled();

                // Turning 2FA off needs the master password, like the other security toggles.
                // The confirm popup only opens once the reprompt has been approved.
                static bool s_twofa_disable_pending = false;
                if (s_twofa_disable_pending && IsRepromptApproved(RepromptAction::DisableSecuritySetting))
                {
                    ClearRepromptApproval(RepromptAction::DisableSecuritySetting);
                    s_twofa_disable_pending = false;
                    ImGui::OpenPopup("Disable 2FA###disable_2fa_modal");
                }

                SettingRowSpec row{};
                row.id = ImGui::GetID("twofa_toggle");
                row.title = "Two-Factor Auth";
                row.subtitle = twofa_on ? "Enabled" : "Disabled";
                row.leftIcon = ICON_MDI_SHIELD_HALF_FULL;
                row.rightType = SettingRightType::Button;
                row.buttonLabel = twofa_on ? "Disable" : "Enable";

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    if (twofa_on)
                    {
                        // Locked-out reprompts never open the modal, so don't wait on an approval that can't come
                        if (!IsRepromptLockedOut())
                        {
                            s_twofa_disable_pending = true;
                            RequestReprompt(RepromptAction::DisableSecuritySetting, "");
                        }
                    }
                    else
                    {
                        s.twofa_setup_open = true;
                    }
                }
            }
            EndCategoryCard();

            if (s.twofa_setup_open)
            {
                ImGui::OpenPopup("Enable 2FA###enable_2fa_modal");
                s.twofa_setup_open = false;
            }

            {
                static int s_2fa_step = 0; // 0=show secret, 1=verify, 2=recovery codes
                static twofa_ops::TwoFactorData s_2fa_data;
                static std::string s_2fa_verify_code;
                static std::string s_2fa_verify_error;

                ImVec2 center = ImGui::GetMainViewport()->GetCenter();
                ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                ImGui::SetNextWindowSizeConstraints(ImVec2(420, 0), ImVec2(420, FLT_MAX));

                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
                ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, colors::DimOverlay);
                bool modal_open = true;
                if (ImGui::BeginPopupModal("Enable 2FA###enable_2fa_modal", &modal_open,
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
                {
                    bool escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);

                    ImGui::TextUnformatted(ICON_MDI_SHIELD_HALF_FULL "  Enable Two-Factor Authentication");
                    {
                        ImVec2 closeSz = ImGui::CalcTextSize(ICON_MDI_CLOSE);
                        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - closeSz.x);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                        ImGui::TextUnformatted(ICON_MDI_CLOSE);
                        ImGui::PopStyleColor();
                        if ((ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) || escape_pressed)
                        {
                            s_2fa_step = 0;
                            s_2fa_data = twofa_ops::TwoFactorData{};
                            s_2fa_verify_code.clear();
                            s_2fa_verify_error.clear();
                            ReleaseQRTexture();
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::Dummy(ImVec2(0, 8));

                    if (s_2fa_step == 0 && s_2fa_data.totp_secret_b32.empty())
                    {
                        s_2fa_data = twofa_ops::generate_new();
                        s_2fa_verify_code.clear();
                        s_2fa_verify_error.clear();
                        std::string qr_uri = totp::generate_otpauth_uri(s_2fa_data.totp_secret_b32, "Password Defense", "vault");
                        CreateQRTexture(qr_uri);
                    }

                    if (s_2fa_step == 0)
                    {
                        ImGui::TextWrapped("Scan the QR code below in your authenticator app, or copy the secret manually.");
                        ImGui::Dummy(ImVec2(0, 6));

                        if (s_2fa_qr_srv)
                        {
                            float displaySize = ImMin(200.0f, ImGui::GetContentRegionAvail().x - 16.0f);
                            float indent = (ImGui::GetContentRegionAvail().x - displaySize) * 0.5f;
                            if (indent > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
                            ImGui::Image((ImTextureID)(intptr_t)s_2fa_qr_srv, ImVec2(displaySize, displaySize));
                            ImGui::Dummy(ImVec2(0, 6));
                        }

                        ImGui::TextUnformatted("Secret:");
                        ImGui::PushFont(render::FontSmall);
                        ImGui::TextWrapped("%s", s_2fa_data.totp_secret_b32.c_str());
                        ImGui::PopFont();

                        ImGui::SameLine();
                        if (ImGui::SmallButton("Copy##secret"))
                        {
                            ImGui::SetClipboardText(s_2fa_data.totp_secret_b32.c_str());
                            ShowToast("Secret copied", ToastType::Success);
                        }

                        ImGui::Dummy(ImVec2(0, 4));
                        std::string uri = totp::generate_otpauth_uri(s_2fa_data.totp_secret_b32, "Password Defense", "vault");
                        ImGui::TextUnformatted("OTP Auth URI:");
                        ImGui::PushFont(render::FontSmall);
                        ImGui::TextWrapped("%s", uri.c_str());
                        ImGui::PopFont();

                        ImGui::SameLine();
                        if (ImGui::SmallButton("Copy##uri"))
                        {
                            ImGui::SetClipboardText(uri.c_str());
                            ShowToast("URI copied", ToastType::Success);
                        }

                        ImGui::Dummy(ImVec2(0, 8));
                        float btnW = ImGui::GetContentRegionAvail().x;
                        if (StyledButtonLight("##2fa_next", "Next", ImVec2(btnW, 34.0f)))
                        {
                            s_2fa_step = 1;
                            s_2fa_verify_code.clear();
                            s_2fa_verify_error.clear();
                        }
                    }
                    else if (s_2fa_step == 1)
                    {
                        ImGui::TextWrapped("Enter the 6-digit code from your authenticator to verify setup.");
                        ImGui::Dummy(ImVec2(0, 6));

                        ImGui::SetNextItemWidth(-1);
                        bool enter = InputTextString("##2fa_verify_input", &s_2fa_verify_code,
                            ImGuiInputTextFlags_EnterReturnsTrue);

                        if (!s_2fa_verify_error.empty())
                        {
                            ImGui::Dummy(ImVec2(0, 2));
                            ImGui::TextColored(colors::Red, "%s", s_2fa_verify_error.c_str());
                        }

                        ImGui::Dummy(ImVec2(0, 6));
                        float btnW = ImGui::GetContentRegionAvail().x;
                        bool doVerify = StyledButtonLight("##2fa_do_verify", "Verify", ImVec2(btnW, 34.0f));

                        if ((enter || doVerify) && !s_2fa_verify_code.empty())
                        {
                            if (totp::verify_code_now(s_2fa_data.totp_secret_b32, s_2fa_verify_code))
                            {
                                s_2fa_step = 2;
                                s_2fa_verify_error.clear();
                            }
                            else
                            {
                                s_2fa_verify_error = "Invalid code. Check your authenticator and try again.";
                            }
                        }
                    }
                    else if (s_2fa_step == 2)
                    {
                        ImGui::TextWrapped("Save these recovery codes in a safe place. Each can be used once if you lose your authenticator.");
                        ImGui::Dummy(ImVec2(0, 6));

                        ImGui::PushFont(render::FontSmall);
                        for (const auto& code : s_2fa_data.recovery_codes)
                            ImGui::TextUnformatted(code.c_str());
                        ImGui::PopFont();

                        ImGui::Dummy(ImVec2(0, 4));
                        if (ImGui::SmallButton("Copy All"))
                        {
                            std::string all;
                            for (const auto& code : s_2fa_data.recovery_codes)
                            {
                                if (!all.empty()) all += '\n';
                                all += code;
                            }
                            ImGui::SetClipboardText(all.c_str());
                            ShowToast("Recovery codes copied", ToastType::Success);
                        }

                        ImGui::Dummy(ImVec2(0, 8));
                        float btnW = ImGui::GetContentRegionAvail().x;
                        if (StyledButtonLight("##2fa_finish", "Finish Setup", ImVec2(btnW, 34.0f)))
                        {
                            std::vector<uint8_t> mk = Get2FAMasterKey();
                            if (!mk.empty() && twofa_ops::save(s_2fa_data, mk))
                            {
                                ShowToast("2FA enabled", ToastType::Success);
                            }
                            else
                            {
                                ShowToast("Failed to enable 2FA", ToastType::Error);
                            }

                            s_2fa_step = 0;
                            s_2fa_data = twofa_ops::TwoFactorData{};
                            s_2fa_verify_code.clear();
                            s_2fa_verify_error.clear();
                            ReleaseQRTexture();
                            ImGui::CloseCurrentPopup();
                        }
                    }

                    ImGui::EndPopup();
                }
                else
                {
                    s_2fa_step = 0;
                    s_2fa_data = twofa_ops::TwoFactorData{};
                    s_2fa_verify_code.clear();
                    s_2fa_verify_error.clear();
                    ReleaseQRTexture();
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }

            {
                ImVec2 center = ImGui::GetMainViewport()->GetCenter();
                ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
                ImGui::SetNextWindowSizeConstraints(ImVec2(340, 0), ImVec2(340, FLT_MAX));

                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
                ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, colors::DimOverlay);
                bool modal_open = true;
                if (ImGui::BeginPopupModal("Disable 2FA###disable_2fa_modal", &modal_open,
                    ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
                {
                    bool escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);

                    ImGui::TextUnformatted(ICON_MDI_SHIELD_HALF_FULL "  Disable Two-Factor Authentication");
                    {
                        ImVec2 closeSz = ImGui::CalcTextSize(ICON_MDI_CLOSE);
                        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - closeSz.x);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                        ImGui::TextUnformatted(ICON_MDI_CLOSE);
                        ImGui::PopStyleColor();
                        if ((ImGui::IsItemHovered() && ImGui::IsMouseClicked(0)) || escape_pressed)
                        {
                            ImGui::CloseCurrentPopup();
                        }
                    }
                    ImGui::Dummy(ImVec2(0, 8));

                    ImGui::TextWrapped("Are you sure you want to disable two-factor authentication? Your vault will only be protected by the master password.");
                    ImGui::Dummy(ImVec2(0, 8));

                    float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

                    if (StyledButtonLight("##2fa_disable_yes", "Disable", ImVec2(btnW, 34.0f)))
                    {
                        twofa_ops::remove();
                        ShowToast("2FA disabled", ToastType::Success);
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::SameLine();

                    if (StyledButtonLight("##2fa_disable_no", "Cancel", ImVec2(btnW, 34.0f)))
                    {
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::EndPopup();
                }
                ImGui::PopStyleVar();
                ImGui::PopStyleColor();
            }

            BeginCategoryCard("Network & Privacy", "This app is offline by default");
            {
                // favicon stats; refreshed on first show, toggle, and clear
                static favicon::CacheStats s_np_stats;
                static bool s_np_stats_ready = false;
                if (!s_np_stats_ready) { s_np_stats = favicon::GetCacheStats(); s_np_stats_ready = true; }

                {
                    SettingRowSpec row{};
                    row.id = ImGui::GetID("online_favicons");
                    row.title = "Website Icons";
                    row.subtitle = s.online_favicons
                        ? "Fetches site icons from google.com as entries are shown"
                        : "Off — no icons are downloaded (offline)";
                    row.leftIcon = ICON_MDI_IMAGE_OUTLINE;
                    row.rightType = SettingRightType::Toggle;
                    row.toggleValue = &s.online_favicons;

                    auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                    if (result.action_used)
                    {
                        cfg::set_online_favicons(s.online_favicons);
                        favicon::SetNetworkEnabled(s.online_favicons);
                    }
                    DrawRowDivider();
                }

                {
                    SettingRowSpec row{};
                    row.id = ImGui::GetID("online_breach");
                    row.title = "Online Breach Check";
                    row.subtitle = s.online_breach_check
                        ? "Checks api.pwnedpasswords.com (k-anonymity — no password sent)"
                        : "Off — passwords are never checked online";
                    row.leftIcon = ICON_MDI_SHIELD_SEARCH;
                    row.rightType = SettingRightType::Toggle;
                    row.toggleValue = &s.online_breach_check;

                    auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                    if (result.action_used)
                        cfg::set_online_breach_check(s.online_breach_check);
                    DrawRowDivider();
                }

                {
                    static char s_np_sub[80];
                    double bytes = (double)s_np_stats.total_bytes;
                    if (bytes < 1024.0)
                        snprintf(s_np_sub, sizeof(s_np_sub), "%d file%s on disk \xC2\xB7 %.0f B",
                                 s_np_stats.file_count, s_np_stats.file_count == 1 ? "" : "s", bytes);
                    else if (bytes < 1024.0 * 1024.0)
                        snprintf(s_np_sub, sizeof(s_np_sub), "%d file%s on disk \xC2\xB7 %.1f KB",
                                 s_np_stats.file_count, s_np_stats.file_count == 1 ? "" : "s", bytes / 1024.0);
                    else
                        snprintf(s_np_sub, sizeof(s_np_sub), "%d file%s on disk \xC2\xB7 %.2f MB",
                                 s_np_stats.file_count, s_np_stats.file_count == 1 ? "" : "s", bytes / (1024.0 * 1024.0));

                    SettingRowSpec row{};
                    row.id = ImGui::GetID("favicon_cache_clear");
                    row.title = "Favicon Cache";
                    row.subtitle = s_np_sub;
                    row.leftIcon = ICON_MDI_DELETE_SWEEP;
                    row.rightType = SettingRightType::Button;
                    row.buttonLabel = "Clear";
                    row.enabled = s_np_stats.file_count > 0;

                    auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                    if (result.action_used && s_np_stats.file_count > 0)
                    {
                        favicon::ClearCache();
                        s_np_stats = favicon::GetCacheStats();
                    }
                }
            }
            EndCategoryCard();

            BeginCategoryCard("Password Information", "Password health overview");

            {
                char sub[64]; snprintf(sub, sizeof(sub), "%d credential%s sharing a password",
                    s.sec_reused_count, s.sec_reused_count == 1 ? "" : "s");
                SettingRowSpec row{};
                row.id = ImGui::GetID("sec_reused");
                row.title = "Reused";
                row.subtitle = sub;
                row.leftIcon = ICON_MDI_REPEAT;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s.sec_highlight_reused;
                row.enabled = true;
                RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                DrawRowDivider();
            }

            // Weak passwords
            {
                char sub[64]; snprintf(sub, sizeof(sub), "%d credential%s with weak password",
                    s.sec_weak_count, s.sec_weak_count == 1 ? "" : "s");
                SettingRowSpec row{};
                row.id = ImGui::GetID("sec_weak");
                row.title = "Weak";
                row.subtitle = sub;
                row.leftIcon = ICON_MDI_ALERT;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s.sec_highlight_weak;
                row.enabled = true;
                RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                DrawRowDivider();
            }

            // Aging passwords
            {
                char sub[64];
                snprintf(sub, sizeof(sub), "%d credential%s older than %d days",
                    s.sec_aging_count, s.sec_aging_count == 1 ? "" : "s",
                    s.password_max_age_days);

                SettingRowSpec row{};
                row.id = ImGui::GetID("sec_aging");
                row.title = "Aging";
                row.subtitle = sub;
                row.leftIcon = ICON_MDI_CLOCK_ALERT;

                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s.sec_highlight_aging;
                row.enabled = true;
                RenderSettingRow(row, &g_settings_selected_row, 54.0f);
            }

            // Aging threshold with vertical stepper
            {
                const float rowH = 80.0f;
                const float padX = 18.0f;
                ImVec2 rowMin = ImGui::GetCursorScreenPos();
                ImGui::Dummy(ImVec2(0, rowH));

                ImDrawList* sdl = ImGui::GetWindowDrawList();
                float cy = rowMin.y + rowH * 0.5f;

                // Icon
                float iconX = rowMin.x + padX;
                ImVec2 iconSz = ImGui::CalcTextSize(ICON_MDI_CALENDAR_CLOCK);
                sdl->AddText(ImVec2(iconX, cy - iconSz.y * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), ICON_MDI_CALENDAR_CLOCK);

                // Title + subtitle
                float textX = iconX + iconSz.x + 10.0f;
                sdl->AddText(ImVec2(textX, rowMin.y + 28.0f), ImGui::GetColorU32(ImGuiCol_Text), "Max age");
                ImGui::PushFont(render::FontSmall);
                sdl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(textX, rowMin.y + 46.0f),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), "Flag passwords older than this");
                ImGui::PopFont();

                // Stepper right-aligned
                float stepW = 56.0f;
                float stepX = ImGui::GetContentRegionMax().x + ImGui::GetWindowPos().x - stepW - padX;
                ImGui::SetCursorScreenPos(ImVec2(stepX, rowMin.y + (rowH - 70.0f) * 0.5f));
                if (VerticalStepper("##age_vs", &s.password_max_age_days, 30, 365, "d"))
                    cfg::set_password_max_age_days(s.password_max_age_days);

                ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMin.y + rowH));
            }

            EndCategoryCard();

            BeginCategoryCard("Self-Destruct", "Delete app data on exit");
            {
                static const char* sd_options[] = { "Off", "Exe Only", "Exe + Config", "Everything" };
                static int s_sd_index = 0;

                static bool s_sd_init = false;
                if (!s_sd_init) {
                    s_sd_index = s.self_destruct_mode;
                    if (s_sd_index < 0 || s_sd_index > 3) s_sd_index = 0;
                    s_sd_init = true;
                }

                SettingRowSpec row{};
                row.id = ImGui::GetID("self_destruct");
                row.title = "On Exit";
                row.subtitle = s_sd_index == 0 ? "Nothing will be deleted"
                             : s_sd_index == 1 ? "Exe will be deleted on exit"
                             : s_sd_index == 2 ? "Exe and config deleted on exit"
                             :                   "Exe, config, and vaults deleted on exit";
                row.leftIcon = ICON_MDI_BOMB;
                row.rightType = SettingRightType::AnimatedCombo;
                row.comboItems = sd_options;
                row.comboCount = 4;
                row.comboIndex = &s_sd_index;
                row.comboWidth = 110.0f;
                row.comboHeight = 30.0f;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    s.self_destruct_mode = s_sd_index;
                    cfg::set_self_destruct_mode(s_sd_index);
                }
            }
            EndCategoryCard();

            BeginCategoryCard("Security", "Master password re-verification");

            // 0=none, 1=reveal, 2=export, 3=readonly, 4=notes
            static int s_pending_disable_setting = 0;

            if (s_pending_disable_setting != 0 && IsRepromptApproved(RepromptAction::DisableSecuritySetting))
            {
                switch (s_pending_disable_setting)
                {
                case 1: cfg::set_reprompt_reveal_password(false); break;
                case 2: cfg::set_reprompt_export(false); break;
                case 3: cfg::set_reprompt_disable_readonly(false); break;
                case 4: cfg::set_reprompt_reveal_notes(false); break;
                }
                ClearRepromptApproval(RepromptAction::DisableSecuritySetting);
                s_pending_disable_setting = 0;
            }

            {
                static bool s_reprompt_reveal = cfg::get_reprompt_reveal_password();
                s_reprompt_reveal = cfg::get_reprompt_reveal_password();

                SettingRowSpec row{};
                row.id = ImGui::GetID("reprompt_reveal");
                row.title = "Reveal Password";
                row.subtitle = "Require master password to reveal";
                row.leftIcon = ICON_MDI_EYE;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s_reprompt_reveal;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    if (s_reprompt_reveal)
                    {
                        cfg::set_reprompt_reveal_password(true);
                    }
                    else
                    {
                        s_reprompt_reveal = true; // revert until approved
                        s_pending_disable_setting = 1;
                        RequestReprompt(RepromptAction::DisableSecuritySetting, "");
                    }
                }
                DrawRowDivider();
            }

            // Require password to export
            {
                static bool s_reprompt_export = cfg::get_reprompt_export();
                s_reprompt_export = cfg::get_reprompt_export();

                SettingRowSpec row{};
                row.id = ImGui::GetID("reprompt_export");
                row.title = "Export Vault";
                row.subtitle = "Require master password to export";
                row.leftIcon = ICON_MDI_FILE_EXPORT;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s_reprompt_export;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    if (s_reprompt_export)
                    {
                        // Enabling - allow immediately
                        cfg::set_reprompt_export(true);
                    }
                    else
                    {
                        // Disabling - require re-prompt
                        s_reprompt_export = true; // Revert UI until approved
                        s_pending_disable_setting = 2;
                        RequestReprompt(RepromptAction::DisableSecuritySetting, "");
                    }
                }
                DrawRowDivider();
            }

            // Require password to disable read-only
            {
                static bool s_reprompt_readonly = cfg::get_reprompt_disable_readonly();
                s_reprompt_readonly = cfg::get_reprompt_disable_readonly();

                SettingRowSpec row{};
                row.id = ImGui::GetID("reprompt_readonly");
                row.title = "Disable Read-only";
                row.subtitle = "Require master password to disable";
                row.leftIcon = ICON_MDI_LOCK;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s_reprompt_readonly;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    if (s_reprompt_readonly)
                    {
                        // Enabling - allow immediately
                        cfg::set_reprompt_disable_readonly(true);
                    }
                    else
                    {
                        // Disabling - require re-prompt
                        s_reprompt_readonly = true; // Revert UI until approved
                        s_pending_disable_setting = 3;
                        RequestReprompt(RepromptAction::DisableSecuritySetting, "");
                    }
                }
                DrawRowDivider();
            }

            // Require password to reveal notes
            {
                static bool s_reprompt_notes = cfg::get_reprompt_reveal_notes();
                s_reprompt_notes = cfg::get_reprompt_reveal_notes();

                SettingRowSpec row{};
                row.id = ImGui::GetID("reprompt_notes");
                row.title = "Reveal Notes";
                row.subtitle = "Require master password to reveal";
                row.leftIcon = ICON_MDI_NOTE_TEXT;
                row.rightType = SettingRightType::Toggle;
                row.toggleValue = &s_reprompt_notes;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    if (s_reprompt_notes)
                    {
                        cfg::set_reprompt_reveal_notes(true);
                    }
                    else
                    {
                        s_reprompt_notes = true;
                        s_pending_disable_setting = 4;
                        RequestReprompt(RepromptAction::DisableSecuritySetting, "");
                    }
                }
                DrawRowDivider();
            }

            // Lockout count (AnimatedComboDot)
            {
                static const char* lockout_options[] = { "3 attempts", "5 attempts", "10 attempts", "Never" };
                static const int lockout_values[] = { 3, 5, 10, 0 }; // 0 = Never (disabled)
                static int s_lockout_index = 0; // default 3 attempts

                // Initialize from config
                static bool s_lockout_init = false;
                if (!s_lockout_init) {
                    int count = cfg::get_reprompt_lockout_count();
                    s_lockout_index = 0; // default
                    for (int i = 0; i < 4; ++i) {
                        if (lockout_values[i] == count) { s_lockout_index = i; break; }
                    }
                    s_lockout_init = true;
                }

                SettingRowSpec row{};
                row.id = ImGui::GetID("lockout_combo");
                row.title = "Lockout Attempts";
                row.subtitle = "Lock after failed password attempts";
                row.leftIcon = ICON_MDI_CANCEL;
                row.rightType = SettingRightType::AnimatedCombo;
                row.comboItems = lockout_options;
                row.comboCount = 4;
                row.comboIndex = &s_lockout_index;
                row.comboWidth = 110.0f;

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    cfg::set_reprompt_lockout_count(lockout_values[s_lockout_index]);
                }
            }

            EndCategoryCard();
        }
        else if (s.settings_tab_index == 3)
        {
            // ============================================================
            // TRANSFER TAB
            // ============================================================

            // ---- Export Card ----
            BeginCategoryCard("Export", "Export your credentials");

            // Track pending export for re-prompt
            static bool s_export_csv_pending = false;
            static bool s_export_pwm_pending = false;
            static bool s_export_kdbx_pending = false;

            // Check if re-prompt was approved
            if (s_export_csv_pending && IsRepromptApproved(RepromptAction::Export))
            {
                s.export_csv_clicked = true;
                s_export_csv_pending = false;
                ClearRepromptApproval(RepromptAction::Export);
            }
            if (s_export_pwm_pending && IsRepromptApproved(RepromptAction::Export))
            {
                s.export_pwm_clicked = true;
                s_export_pwm_pending = false;
                ClearRepromptApproval(RepromptAction::Export);
            }
            if (s_export_kdbx_pending && IsRepromptApproved(RepromptAction::Export))
            {
                s.export_kdbx_clicked = true;
                s_export_kdbx_pending = false;
                ClearRepromptApproval(RepromptAction::Export);
            }

            // Export CSV row
            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("export_csv");
                row.title = "Export as CSV";
                row.subtitle = "Spreadsheet compatible";
                row.leftIcon = ICON_MDI_FILE_EXCEL;
                row.rightType = SettingRightType::Button;
                row.buttonLabel = "Export";

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    s_export_csv_pending = true;
                    s_export_pwm_pending = false;
                    s_export_kdbx_pending = false;
                    if (RequestReprompt(RepromptAction::Export, ""))
                    {
                        s.export_csv_clicked = true;
                        s_export_csv_pending = false;
                    }
                }
                DrawRowDivider();
            }

            // Export PWM row
            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("export_pwm");
                row.title = "Export as PWM";
                row.subtitle = "Encrypted backup format";
                row.leftIcon = ICON_MDI_LOCK;
                row.rightType = SettingRightType::Button;
                row.buttonLabel = "Export";

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    s_export_pwm_pending = true;
                    s_export_csv_pending = false;
                    s_export_kdbx_pending = false;
                    if (RequestReprompt(RepromptAction::Export, ""))
                    {
                        s.export_pwm_clicked = true;
                        s_export_pwm_pending = false;
                    }
                }
                DrawRowDivider();
            }

            // Export KDBX row
            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("export_kdbx");
                row.title = "Export as KDBX";
                row.subtitle = "KeePass / KeePassXC compatible";
                row.leftIcon = ICON_MDI_FILE_EXPORT;
                row.rightType = SettingRightType::Button;
                row.buttonLabel = "Export";

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    s_export_kdbx_pending = true;
                    s_export_csv_pending = false;
                    s_export_pwm_pending = false;
                    if (RequestReprompt(RepromptAction::Export, ""))
                    {
                        s.export_kdbx_clicked = true;
                        s_export_kdbx_pending = false;
                    }
                }
            }

            EndCategoryCard();

            // ---- Import Card ----
            BeginCategoryCard("Import", "Import credentials from file");

            static bool s_import_pwm_pending = false;
            static bool s_import_csv_pending = false;

            if (s_import_pwm_pending && IsRepromptApproved(RepromptAction::Export))
            {
                s.import_pwm_clicked = true;
                s_import_pwm_pending = false;
                ClearRepromptApproval(RepromptAction::Export);
            }

            if (s_import_csv_pending && IsRepromptApproved(RepromptAction::Export))
            {
                s.import_csv_clicked = true;
                s_import_csv_pending = false;
                ClearRepromptApproval(RepromptAction::Export);
            }

            // Import PWM row
            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("import_pwm");
                row.title = "Import PWM";
                row.subtitle = "Restore from encrypted backup";
                row.leftIcon = ICON_MDI_FILE_IMPORT;
                row.rightType = SettingRightType::Button;
                row.buttonLabel = "Import";

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    s_import_pwm_pending = true;
                    s_import_csv_pending = false;
                    if (RequestReprompt(RepromptAction::Export, ""))
                    {
                        s.import_pwm_clicked = true;
                        s_import_pwm_pending = false;
                    }
                }
                DrawRowDivider();
            }

            // Import CSV row
            {
                SettingRowSpec row{};
                row.id = ImGui::GetID("import_csv");
                row.title = "Import CSV";
                row.subtitle = "Chrome, Bitwarden, LastPass, 1Password, KeePass";
                row.leftIcon = ICON_MDI_FILE_DELIMITED;
                row.rightType = SettingRightType::Button;
                row.buttonLabel = "Import";

                auto result = RenderSettingRow(row, &g_settings_selected_row, 54.0f);
                if (result.action_used)
                {
                    s_import_csv_pending = true;
                    s_import_pwm_pending = false;
                    if (RequestReprompt(RepromptAction::Export, ""))
                    {
                        s.import_csv_clicked = true;
                        s_import_csv_pending = false;
                    }
                }
            }

            EndCategoryCard();
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();

        // Restore original card bg
        s.card_bg_dark  = savedCardDark;
        s.card_bg_light = savedCardLight;

        ImGui::EndPopup();
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        if (s.theme_changed) { ImGui::GetStyle().Colors[ImGuiCol_WindowBg] = s.dark_theme ? s.window_bg_dark : s.window_bg_light; }
    }

} // namespace ui
