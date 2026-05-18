// ui_views_threepane.cpp
// Three-pane view: sidebar + list + detail pane
#include "ui_internal.h"

namespace ui
{

    void RenderThreePaneSidebar(ShellState& s, float animW, const std::vector<AccordionItem>& allItems)
    {
        const bool collapsed = s.three_pane_sidebar_collapsed;
        const float sidebarW = animW;

        const bool dark = IsDarkTheme();
        LippedChildColorSet lippedColors;
        lippedColors.bg        = dark ? theme::PanelHeaderBg.dark  : theme::PanelHeaderBg.light;
        lippedColors.lip_color = dark ? theme::PanelLip.dark : theme::PanelLip.light;
        SetLippedChildColors(lippedColors);
        BeginLippedChild("##3p_sidebar", ImVec2(sidebarW, 0));

        const ImVec4 accentCol = colors::SecondColor;
        const ImU32 accentU32 = ImGui::GetColorU32(accentCol);
        float fullW = ImGui::GetContentRegionAvail().x; // updated after scroll child begins
        float centerW = fullW;
        float sbContentRight = 0.0f; // updated after scroll child begins

        // Use unfiltered counts from ShellState (populated in application.cpp)
        const int countAll       = s.sb_count_all;
        const int countPinned    = s.sb_count_pinned;
        const int countFav       = s.sb_count_favorites;
        const int countPasswords = s.sb_count_passwords;
        const int countCards     = s.sb_count_cards;
        const int countIdentity  = s.sb_count_identity;
        const int countNotes     = s.sb_count_notes;
        const auto& groupCounts  = s.sb_group_counts;
        // Map pill tab labels to counts
        auto GetPillCount = [&](const std::string& label) -> int {
            if (label == "Pinned") return countPinned;
            if (label == "Favorites") return countFav;
            if (label == "Recent") return -1;
            auto git = groupCounts.find(label);
            return git != groupCounts.end() ? git->second : 0;
        };

        // Suppress selectable highlight — no hover/active/selected background
        const ImVec4 transparent(0, 0, 0, 0);
        ImGui::PushStyleColor(ImGuiCol_Header,        transparent);
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  transparent);
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,   transparent);

        // Taller selectables with vertically centered text
        const float rowH = 20.0f;
        const float sbPad = 17.0f;
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));

        // Toggle button + heading
        {
            if (!collapsed) ImGui::Indent(sbPad - 2.0f);
            const char* toggleIcon = collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_LEFT;
            float btnSz = 20.0f;
            if (collapsed)
            {
                float cx = (centerW - btnSz) * 0.5f;
                if (cx > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx);
            }
            float btnY = ImGui::GetCursorPosY();
            if (IconButtonDoubleLip("##sb_toggle", toggleIcon, btnSz))
            {
                s.three_pane_sidebar_collapsed = !collapsed;
                cfg::set_three_pane_sidebar_collapsed(s.three_pane_sidebar_collapsed);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("##sb_auto_popup");
            if (!collapsed)
            {
                ImGui::SameLine(0, 6);
                float textOffY = (btnSz - ImGui::GetTextLineHeight()) * 0.5f;
                ImGui::SetCursorPosY(btnY + textOffY);
                ImGui::TextDisabled("FILTER");
            }

            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, IsDarkTheme() ? theme::PopupBg.dark : theme::PopupBg.light);
            ImGui::PushStyleColor(ImGuiCol_Border,  IsDarkTheme() ? theme::PopupBorder.dark : theme::PopupBorder.light);
            if (ImGui::BeginPopup("##sb_auto_popup"))
            {
                PopupStyleBegin();
                ImGui::TextDisabled("Auto-collapse");
                ImGui::Separator();
                bool sbAuto = s.three_pane_sidebar_auto_collapse;
                if (ui::ToggleSwitch("##sb_auto_tgl", &sbAuto))
                {
                    s.three_pane_sidebar_auto_collapse = sbAuto;
                    cfg::set_three_pane_sidebar_auto_collapse(sbAuto);
                    if (sbAuto)
                    {
                        s.three_pane_sidebar_collapsed = true;
                        cfg::set_three_pane_sidebar_collapsed(true);
                    }
                    else
                    {
                        g_3p_sidebar_hover_expanded = false;
                        g_3p_sidebar_leave_timer = 0.0f;
                    }
                }
                PopupStyleEnd();
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);

            if (!collapsed) ImGui::Unindent(sbPad - 2.0f);
            ImGui::Spacing();
        }

        // Scrollable content area (header stays fixed above)
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 4.0f));
        ImGui::BeginChild("##3p_sb_scroll", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(4);
        fullW = ImGui::GetContentRegionAvail().x; // recompute inside scroll child
        centerW = collapsed ? fullW : fullW;
        sbContentRight = ImGui::GetWindowPos().x + fullW; // right edge of content area

        // Shared icon color for both collapsed and expanded modes
        const ImU32 sbIcoGray = IM_COL32(150, 150, 155, 255);

        if (collapsed)
        {
            // ---- COLLAPSED MODE: icon-only strip ----
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // Filters (icon-only, centered) — sync with pill tab labels
            const char* pico0 = nullptr; const char* pico0a = nullptr;
            const char* pico1 = nullptr; const char* pico1a = nullptr;
            GetPillTabIcons(s.pill_tab_0, pico0, pico0a);
            GetPillTabIcons(s.pill_tab_1, pico1, pico1a);

            struct FilterEntry { const char* icon; const char* icon_active; const char* tooltip; int sort_val; };
            FilterEntry filters[] = {
                { ICON_MDI_FORMAT_LIST_BULLETED, ICON_MDI_FORMAT_LIST_BULLETED, "All Items",  2 },
                { pico0, pico0a, s.pill_tab_0.c_str(),  0 },
                { pico1, pico1a, s.pill_tab_1.c_str(),  1 },
            };
            for (auto& f : filters)
            {
                bool active = (s.sort_mode == f.sort_val);
                const char* ico = active ? f.icon_active : f.icon;
                ImVec2 curPos = ImGui::GetCursorScreenPos();

                // Active indicator + rounded highlight (full width)
                if (active)
                {
                    ImVec2 rowMin = ImVec2(ImGui::GetWindowPos().x, curPos.y);
                    ImVec2 rowMax = ImVec2(rowMin.x + fullW, curPos.y + rowH);
                    bool dk = IsDarkTheme();
                    dl->AddLine(rowMin, ImVec2(rowMax.x, rowMin.y),
                        dk ? theme::RowSeparatorTop.dark : theme::RowSeparatorTop.light, 1.0f);
                    dl->AddLine(ImVec2(rowMin.x, rowMax.y), rowMax,
                        dk ? theme::RowSeparatorBot.dark : theme::RowSeparatorBot.light, 1.0f);
                    ImU32 hlCol = dk ? theme::RowHighlight.dark : theme::RowHighlight.light;
                    dl->AddRectFilled(rowMin, rowMax, hlCol, 0.0f);
                }

                {
                    ImVec2 selPos = ImGui::GetCursorScreenPos();
                    ImGui::PushID(f.tooltip);
                    ImGui::InvisibleButton("##cf", ImVec2(fullW, rowH));
                    if (ImGui::IsItemClicked()) s.sort_mode = f.sort_val;
                    bool itemHov = ImGui::IsItemHovered();
                    ImGui::PopID();
                    ImU32 icoCol = active ? accentU32 : (itemHov ? accentU32 : sbIcoGray);
                    float icoY = selPos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                    dl->AddText(ImVec2(selPos.x + (centerW - ImGui::CalcTextSize(ico).x) * 0.5f, icoY), icoCol, ico);
                    if (itemHov) SetTooltipPadded("%s", f.tooltip);
                }
            }

            ImGui::Spacing();

            // Types section collapse caret (centered)
            {
                const char* chevron = s.sidebar_types_collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN;
                ImVec2 icoSz = ImGui::CalcTextSize(chevron);
                float cx = (centerW - icoSz.x) * 0.5f;
                if (cx > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                if (ImGui::Selectable(chevron, false, 0, ImVec2(fullW, rowH)))
                    s.sidebar_types_collapsed = !s.sidebar_types_collapsed;
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) SetTooltipPadded("TYPES");
            }

            // Clear types X (always visible when a type filter is active)
            {
                bool anyTypeActive = false;
                for (const auto& key : s.selected_groups)
                    if (!key.empty() && key[0] == '@') { anyTypeActive = true; break; }
                if (anyTypeActive)
                {
                    const char* ico = ICON_MDI_CLOSE;
                    ImVec2 icoSz = ImGui::CalcTextSize(ico);
                    float cx = (centerW - icoSz.x) * 0.5f;
                    if (cx > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (ImGui::Selectable(ico, false, 0, ImVec2(fullW, rowH)))
                    {
                        std::erase_if(s.selected_groups, [](const std::string& k) {
                            return !k.empty() && k[0] == '@';
                        });
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) SetTooltipPadded("Clear Types");
                }
            }

            if (!s.sidebar_types_collapsed)
            {

            // Types (icon-only, centered) — outline when inactive, filled when active
            struct TypeEntry { const char* icon; const char* icon_active; const char* tooltip; const char* group_key; };
            TypeEntry types[] = {
                { ICON_MDI_KEY,         ICON_MDI_KEY,         "Passwords", "@Passwords" },
                { ICON_MDI_CREDIT_CARD, ICON_MDI_CREDIT_CARD, "Cards",     "@Cards" },
                { ICON_MDI_CARD_ACCOUNT_DETAILS, ICON_MDI_CARD_ACCOUNT_DETAILS, "Identity",  "@Identity" },
                { ICON_MDI_NOTE_TEXT,    ICON_MDI_NOTE_TEXT,   "Notes",     "@Notes" },
            };
            for (auto& t : types)
            {
                bool active = s.selected_groups.count(t.group_key) > 0;
                const char* ico = active ? t.icon_active : t.icon;
                ImVec2 curPos = ImGui::GetCursorScreenPos();

                if (active)
                {
                    ImVec2 rowMin = ImVec2(ImGui::GetWindowPos().x, curPos.y);
                    ImVec2 rowMax = ImVec2(rowMin.x + fullW, curPos.y + rowH);
                    bool dk = IsDarkTheme();
                    dl->AddLine(rowMin, ImVec2(rowMax.x, rowMin.y),
                        dk ? theme::RowSeparatorTop.dark : theme::RowSeparatorTop.light, 1.0f);
                    dl->AddLine(ImVec2(rowMin.x, rowMax.y), rowMax,
                        dk ? theme::RowSeparatorBot.dark : theme::RowSeparatorBot.light, 1.0f);
                    ImU32 hlCol = dk ? theme::RowHighlight.dark : theme::RowHighlight.light;
                    dl->AddRectFilled(rowMin, rowMax, hlCol, 0.0f);
                }

                ImVec2 selPos = ImGui::GetCursorScreenPos();
                ImGui::PushID(t.group_key);
                ImGui::InvisibleButton("##ct", ImVec2(fullW, rowH));
                if (ImGui::IsItemClicked()) {
                    if (active) s.selected_groups.erase(t.group_key);
                    else        s.selected_groups.insert(t.group_key);
                }
                bool itemHov = ImGui::IsItemHovered();
                ImGui::PopID();
                ImU32 icoCol = active ? accentU32 : (itemHov ? accentU32 : sbIcoGray);
                float icoY = selPos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                dl->AddText(ImVec2(selPos.x + (centerW - ImGui::CalcTextSize(ico).x) * 0.5f, icoY), icoCol, ico);
                if (itemHov) SetTooltipPadded("%s", t.tooltip);
            }

            } // end if (!s.sidebar_types_collapsed)

            ImGui::Spacing();
            ImGui::Spacing();

            // Groups section collapse caret (centered)
            {
                const char* chevron = s.sidebar_groups_collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN;
                ImVec2 icoSz = ImGui::CalcTextSize(chevron);
                float cx = (centerW - icoSz.x) * 0.5f;
                if (cx > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                ImGui::PushID("##grp_caret");
                if (ImGui::Selectable(chevron, false, 0, ImVec2(fullW, rowH)))
                    s.sidebar_groups_collapsed = !s.sidebar_groups_collapsed;
                ImGui::PopID();
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered()) SetTooltipPadded("GROUPS");
            }

            // Clear groups X (always visible when a group filter is active)
            {
                bool anyGroupActive = false;
                for (const auto& key : s.selected_groups)
                    if (key.empty() || key[0] != '@') { anyGroupActive = true; break; }
                if (anyGroupActive)
                {
                    ImGui::PushID("##clear_grp");
                    const char* ico = ICON_MDI_CLOSE;
                    ImVec2 icoSz = ImGui::CalcTextSize(ico);
                    float cx = (centerW - icoSz.x) * 0.5f;
                    if (cx > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (ImGui::Selectable(ico, false, 0, ImVec2(fullW, rowH)))
                    {
                        std::erase_if(s.selected_groups, [](const std::string& k) {
                            return k.empty() || k[0] != '@';
                        });
                    }
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) SetTooltipPadded("Clear Groups");
                    ImGui::PopID();
                }
            }

            if (!s.sidebar_groups_collapsed)
            {
                // Groups (centered folder icons — outline inactive, filled active)
                // Cap height so tags section isn't pushed off screen
                int groupCount = 0;
                for (const auto& g : s.groups)
                {
                    if (g == "Filter" || g == "All") continue;
                    if (!g.empty() && g[0] == '@') continue;
                    if (g == "---") continue;
                    groupCount++;
                }
                const float maxGroupH = rowH * 8.0f;
                float availH = ImGui::GetContentRegionAvail().y;
                float cappedH = ImMin(maxGroupH, availH * 0.40f);
                cappedH = floorf(cappedH / rowH) * rowH;
                float groupContentH = rowH * groupCount;
                bool needsScroll = groupContentH > cappedH;

                ImGui::PushStyleColor(ImGuiCol_ChildBg,              IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  IM_COL32(0, 0, 0, 0));
                ImGui::BeginChild("##sb_groups_scroll_c", ImVec2(fullW, cappedH), ImGuiChildFlags_None);

                {
                    ImDrawList* gdl = ImGui::GetWindowDrawList();
                    float childW = ImGui::GetContentRegionAvail().x;

                    for (const auto& g : s.groups)
                    {
                        if (g == "Filter" || g == "All") continue;
                        if (!g.empty() && g[0] == '@') continue;
                        if (g == "---") continue;

                        bool active = s.selected_groups.count(g) > 0;
                        const char* ico = ICON_MDI_FOLDER;
                        ImVec2 curPos = ImGui::GetCursorScreenPos();

                        if (active)
                        {
                            ImVec2 rowMin = ImVec2(ImGui::GetWindowPos().x, curPos.y);
                            ImVec2 rowMax = ImVec2(rowMin.x + childW, curPos.y + rowH);
                            bool dk = IsDarkTheme();
                            gdl->AddLine(rowMin, ImVec2(rowMax.x, rowMin.y),
                                dk ? theme::RowSeparatorTop.dark : theme::RowSeparatorTop.light, 1.0f);
                            gdl->AddLine(ImVec2(rowMin.x, rowMax.y), rowMax,
                                dk ? theme::RowSeparatorBot.dark : theme::RowSeparatorBot.light, 1.0f);
                            ImU32 hlCol = dk ? theme::RowHighlight.dark : theme::RowHighlight.light;
                            gdl->AddRectFilled(rowMin, rowMax, hlCol, 0.0f);
                        }

                        ImVec2 selPos = ImGui::GetCursorScreenPos();
                        ImGui::PushID(g.c_str());
                        ImGui::InvisibleButton("##cg", ImVec2(childW, rowH));
                        if (ImGui::IsItemClicked()) {
                            if (active) s.selected_groups.erase(g);
                            else        s.selected_groups.insert(g);
                        }
                        bool itemHov = ImGui::IsItemHovered();
                        ImGui::PopID();
                        ImU32 icoCol = active ? accentU32 : (itemHov ? accentU32 : sbIcoGray);
                        float icoY = selPos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                        gdl->AddText(ImVec2(selPos.x + (centerW - ImGui::CalcTextSize(ico).x) * 0.5f, icoY), icoCol, ico);
                        if (itemHov) SetTooltipPadded("%s", g.c_str());
                    }
                }

                ImGui::EndChild();
                ImGui::PopStyleColor(5);
            } // end if (!s.sidebar_groups_collapsed)

            // --- TAGS section (collapsed sidebar) ---
            if (!s.all_tags.empty())
            {
                ImGui::Spacing();
                //ImGui::Spacing();

                // Tags section header (chevron centered)
                {
                    const char* chevron = s.sidebar_tags_collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN;
                    ImVec2 icoSz = ImGui::CalcTextSize(chevron);
                    float cx = (centerW - icoSz.x) * 0.5f;
                    if (cx > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    ImGui::PushID("##tag_caret");
                    if (ImGui::Selectable(chevron, false, 0, ImVec2(fullW, rowH)))
                        s.sidebar_tags_collapsed = !s.sidebar_tags_collapsed;
                    ImGui::PopID();
                    ImGui::PopStyleColor();
                    if (ImGui::IsItemHovered()) SetTooltipPadded("TAGS");
                }

                if (!s.sidebar_tags_collapsed)
                {
                    // Clear tags X
                    bool anyTagActive = !s.selected_tags.empty();
                    if (anyTagActive)
                    {
                        const char* ico = ICON_MDI_CLOSE;
                        ImVec2 icoSz = ImGui::CalcTextSize(ico);
                        float cx = (centerW - icoSz.x) * 0.5f;
                        if (cx > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx);
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                        ImGui::PushID("##clear_tags");
                        if (ImGui::Selectable(ico, false, 0, ImVec2(fullW, rowH)))
                            s.selected_tags.clear();
                        ImGui::PopID();
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered()) SetTooltipPadded("Clear Tags");
                    }

                    // Tag items (centered icons)
                    for (const auto& tag : s.all_tags)
                    {
                        bool active = s.selected_tags.count(tag) > 0;
                        const char* ico = ICON_MDI_TAG;
                        ImVec2 curPos = ImGui::GetCursorScreenPos();

                        if (active)
                        {
                            ImVec2 rowMin = ImVec2(ImGui::GetWindowPos().x, curPos.y);
                            ImVec2 rowMax = ImVec2(rowMin.x + fullW, curPos.y + rowH);
                            bool dk = IsDarkTheme();
                            dl->AddLine(rowMin, ImVec2(rowMax.x, rowMin.y),
                                dk ? theme::RowSeparatorTop.dark : theme::RowSeparatorTop.light, 1.0f);
                            dl->AddLine(ImVec2(rowMin.x, rowMax.y), rowMax,
                                dk ? theme::RowSeparatorBot.dark : theme::RowSeparatorBot.light, 1.0f);
                            ImU32 hlCol = dk ? theme::RowHighlight.dark : theme::RowHighlight.light;
                            dl->AddRectFilled(rowMin, rowMax, hlCol, 0.0f);
                        }

                        ImVec2 selPos = ImGui::GetCursorScreenPos();
                        ImGui::PushID(tag.c_str());
                        ImGui::InvisibleButton("##ctg", ImVec2(fullW, rowH));
                        if (ImGui::IsItemClicked()) {
                            if (active) s.selected_tags.erase(tag);
                            else        s.selected_tags.insert(tag);
                        }
                        bool itemHov = ImGui::IsItemHovered();
                        ImGui::PopID();
                        ImU32 icoCol = active ? accentU32 : (itemHov ? accentU32 : sbIcoGray);
                        float icoY = selPos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                        dl->AddText(ImVec2(selPos.x + (centerW - ImGui::CalcTextSize(ico).x) * 0.5f, icoY), icoCol, ico);
                        if (itemHov) SetTooltipPadded("%s", tag.c_str());
                    }
                }
            }
        }
        else
        {
            // ---- EXPANDED MODE (existing code) ----
            ImGui::Indent(sbPad);

            // Sidebar icon colors
            const ImU32 sbIcoPin    = sbIcoGray;
            const ImU32 sbIcoHeart  = sbIcoGray;
            const ImU32 sbIcoKey    = sbIcoGray;
            const ImU32 sbIcoCard   = sbIcoGray;
            const ImU32 sbIcoId     = sbIcoGray;
            const ImU32 sbIcoNote   = sbIcoGray;
            const ImU32 sbIcoFolder = sbIcoGray;
            const ImU32 sbIcoTag    = sbIcoGray;

            // Helper: draw a sidebar row with colored icon + text (no Selectable, avoids double-draw)
            // Returns true if clicked.
            auto DrawSbRow = [&](const char* id, const char* label, ImU32 icoCol, bool hoverAccent) -> bool {
                ImVec2 selPos = ImGui::GetCursorScreenPos();
                bool hov = ImGui::IsMouseHoveringRect(selPos, ImVec2(selPos.x + fullW, selPos.y + rowH));
                ImGui::PushID(id);
                ImGui::InvisibleButton("##sbr", ImVec2(fullW, rowH));
                bool clicked = ImGui::IsItemClicked();
                ImGui::PopID();
                ImDrawList* dl = ImGui::GetWindowDrawList();
                float textY = selPos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                const char* sp = strchr(label, ' ');
                char icoBuf[16];
                int icoLen = sp ? (int)(sp - label) : (int)strlen(label);
                if (icoLen >= (int)sizeof(icoBuf)) icoLen = (int)sizeof(icoBuf) - 1;
                memcpy(icoBuf, label, icoLen); icoBuf[icoLen] = 0;
                dl->AddText(ImVec2(selPos.x, textY), icoCol, icoBuf);
                if (sp)
                {
                    float icoW = ImGui::CalcTextSize(icoBuf).x;
                    ImU32 txtCol = (hov && hoverAccent) ? accentU32 : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                    dl->AddText(ImVec2(selPos.x + icoW + ImGui::CalcTextSize(" ").x, textY), txtCol, sp + 1);
                }
                return clicked;
            };

            // Map filter label to icon color
            auto FilterIconColor = [&](const std::string& label) -> ImU32 {
                if (label == "Pinned")    return sbIcoPin;
                if (label == "Favorites") return sbIcoHeart;
                return 0;
            };

            // --- High-level filters (sync with pill tab labels) ---
            const char* eIco0 = nullptr; const char* eIco0a = nullptr;
            const char* eIco1 = nullptr; const char* eIco1a = nullptr;
            GetPillTabIcons(s.pill_tab_0, eIco0, eIco0a);
            GetPillTabIcons(s.pill_tab_1, eIco1, eIco1a);

            char label0[128], label0a[128], label1[128], label1a[128];
            snprintf(label0,  sizeof(label0),  "%s  %s", eIco0,  s.pill_tab_0.c_str());
            snprintf(label0a, sizeof(label0a), "%s  %s", eIco0a, s.pill_tab_0.c_str());
            snprintf(label1,  sizeof(label1),  "%s  %s", eIco1,  s.pill_tab_1.c_str());
            snprintf(label1a, sizeof(label1a), "%s  %s", eIco1a, s.pill_tab_1.c_str());

            struct FilterEntry { const char* label; const char* label_active; int sort_val; ImU32 icoCol; };
            FilterEntry filters[] = {
                { ICON_MDI_FORMAT_LIST_BULLETED "  All Items", ICON_MDI_FORMAT_LIST_BULLETED "  All Items",  2, 0 },
                { label0, label0a, 0, FilterIconColor(s.pill_tab_0) },
                { label1, label1a, 1, FilterIconColor(s.pill_tab_1) },
            };
            int filterCounts[] = { countAll, GetPillCount(s.pill_tab_0), GetPillCount(s.pill_tab_1) };
            for (int fi = 0; fi < 3; fi++)
            {
                auto& f = filters[fi];
                bool active = (s.sort_mode == f.sort_val);
                const char* lbl = active ? f.label_active : f.label;
                ImVec2 curPos = ImGui::GetCursorScreenPos();
                bool hovered = ImGui::IsMouseHoveringRect(curPos, ImVec2(curPos.x + fullW, curPos.y + rowH));

                // Active indicator + rounded highlight (full width)
                if (active)
                {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float hlLeft = ImGui::GetWindowPos().x;
                    // Top shadow line (lighter)
                    dl->AddLine(
                        ImVec2(hlLeft, curPos.y),
                        ImVec2(sbContentRight, curPos.y),
                        dark ? theme::RowSeparatorTop.dark : theme::RowSeparatorTop.light, 1.0f);
                    // Bottom shadow line (darker)
                    dl->AddLine(
                        ImVec2(hlLeft, curPos.y + rowH),
                        ImVec2(sbContentRight, curPos.y + rowH),
                        dark ? theme::RowSeparatorBot.dark : theme::RowSeparatorBot.light, 1.0f);
                    // Fill
                    ImU32 hlCol = dark ? theme::RowHighlight.dark : theme::RowHighlight.light;
                    dl->AddRectFilled(
                        ImVec2(hlLeft, curPos.y),
                        ImVec2(sbContentRight, curPos.y + rowH),
                        hlCol, 0.0f);
                    // Rounded accent pill on left edge
                }

                if (active)
                {
                    // Split: draw icon in accent, text in black/white
                    const char* sp = strchr(lbl, ' ');
                    ImVec2 selPos = ImGui::GetCursorScreenPos();
                    ImGui::PushID(fi);
                    ImGui::InvisibleButton("##fsel", ImVec2(fullW, rowH));
                    if (ImGui::IsItemClicked()) s.sort_mode = f.sort_val;
                    ImGui::PopID();
                    ImDrawList* dlIco = ImGui::GetWindowDrawList();
                    float textY = selPos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                    char icoBuf[64];
                    int icoLen = sp ? (int)(sp - lbl) : (int)strlen(lbl);
                    if (icoLen >= (int)sizeof(icoBuf)) icoLen = (int)sizeof(icoBuf) - 1;
                    memcpy(icoBuf, lbl, icoLen); icoBuf[icoLen] = 0;
                    dlIco->AddText(ImVec2(selPos.x, textY), f.icoCol ? f.icoCol : accentU32, icoBuf);
                    if (sp)
                    {
                        float icoW = ImGui::CalcTextSize(icoBuf).x;
                        dlIco->AddText(ImVec2(selPos.x + icoW + ImGui::CalcTextSize(" ").x, textY),
                            ImGui::GetColorU32(ImGuiCol_Text), sp + 1);
                    }
                }
                else
                {
                    if (f.icoCol)
                    {
                        char fid[16]; snprintf(fid, sizeof(fid), "f%d", fi);
                        if (DrawSbRow(fid, lbl, f.icoCol, true))
                            s.sort_mode = f.sort_val;
                    }
                    else
                    {
                        if (hovered) ImGui::PushStyleColor(ImGuiCol_Text, accentCol);
                        if (ImGui::Selectable(lbl, false, 0, ImVec2(fullW, rowH)))
                            s.sort_mode = f.sort_val;
                        if (hovered) ImGui::PopStyleColor();
                    }
                }
                // Badge count (right-aligned, skip "Recent" which has no precomputed count)
                if (filterCounts[fi] >= 0)
                {
                    char badge[16];
                    snprintf(badge, sizeof(badge), "%d", filterCounts[fi]);
                    ImVec2 bSz = ImGui::CalcTextSize(badge);
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    ImVec2 rMin = ImGui::GetItemRectMin();
                    ImVec2 rMax = ImGui::GetItemRectMax();
                    dl->AddText(render::FontSmall, render::FontSmall ? render::FontSmall->LegacySize : bSz.y,
                        ImVec2(sbContentRight - bSz.x - sbPad, rMin.y + (rowH - bSz.y) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_TextDisabled), badge);
                }
            }

            ImGui::Spacing();
            //ImGui::Spacing();

            // --- TYPES section header (collapsible) ---
            {
                const char* chevron = s.sidebar_types_collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN;
                ImGui::PushFont(render::FontSmall);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                char typesHeader[64];
                snprintf(typesHeader, sizeof(typesHeader), "%s  TYPES", chevron);
                if (ImGui::Selectable(typesHeader, false, 0, ImVec2(fullW, rowH)))
                    s.sidebar_types_collapsed = !s.sidebar_types_collapsed;
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }

            if (!s.sidebar_types_collapsed)
            {
                // Check if any type filter is active
                bool anyTypeActive = false;
                for (const auto& key : s.selected_groups)
                    if (!key.empty() && key[0] == '@') { anyTypeActive = true; break; }

                if (anyTypeActive)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (ImGui::Selectable(ICON_MDI_CLOSE "  Clear Types", false, 0, ImVec2(fullW, rowH)))
                    {
                        std::erase_if(s.selected_groups, [](const std::string& k) {
                            return !k.empty() && k[0] == '@';
                        });
                    }
                    ImGui::PopStyleColor();
                }

                // Type filters — outline when inactive, filled when active
                struct TypeEntry { const char* label; const char* label_active; const char* group_key; CredType type; ImU32 icoCol; };
                TypeEntry types[] = {
                    { ICON_MDI_KEY "  Passwords",          ICON_MDI_KEY "  Passwords",          "@Passwords",  CredType::Password,   sbIcoKey },
                    { ICON_MDI_CREDIT_CARD "  Cards",      ICON_MDI_CREDIT_CARD "  Cards",      "@Cards",      CredType::CreditCard, sbIcoCard },
                    { ICON_MDI_CARD_ACCOUNT_DETAILS "  Identity", ICON_MDI_CARD_ACCOUNT_DETAILS "  Identity", "@Identity",   CredType::Identity, sbIcoId },
                    { ICON_MDI_NOTE_TEXT "  Notes",        ICON_MDI_NOTE_TEXT "  Notes",         "@Notes",      CredType::SecureNote, sbIcoNote },
                };
                int typeCounts[] = { countPasswords, countCards, countIdentity, countNotes };
                for (int ti = 0; ti < 4; ti++)
                {
                    auto& t = types[ti];
                    bool active = s.selected_groups.count(t.group_key) > 0;
                    const char* lbl = active ? t.label_active : t.label;
                    ImVec2 curPos = ImGui::GetCursorScreenPos();
                    bool hovered = ImGui::IsMouseHoveringRect(curPos, ImVec2(curPos.x + fullW, curPos.y + rowH));

                    // Active indicator + rounded highlight (full width)
                    if (active)
                    {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        float hlLeft = ImGui::GetWindowPos().x;
                        dl->AddLine(
                            ImVec2(hlLeft, curPos.y),
                            ImVec2(sbContentRight, curPos.y),
                            dark ? theme::RowSeparatorTop.dark : theme::RowSeparatorTop.light, 1.0f);
                        dl->AddLine(
                            ImVec2(hlLeft, curPos.y + rowH),
                            ImVec2(sbContentRight, curPos.y + rowH),
                            dark ? theme::RowSeparatorBot.dark : theme::RowSeparatorBot.light, 1.0f);
                        ImU32 hlCol = dark ? theme::RowHighlight.dark : theme::RowHighlight.light;
                        dl->AddRectFilled(
                            ImVec2(hlLeft, curPos.y),
                            ImVec2(sbContentRight, curPos.y + rowH),
                            hlCol, 0.0f);
                    }

                    if (active)
                    {
                        const char* sp = strchr(lbl, ' ');
                        ImVec2 selPos = ImGui::GetCursorScreenPos();
                        ImGui::PushID(ti);
                        ImGui::InvisibleButton("##tsel", ImVec2(fullW, rowH));
                        bool clicked = ImGui::IsItemClicked();
                        ImGui::PopID();
                        if (clicked) { if (active) s.selected_groups.erase(t.group_key); else s.selected_groups.insert(t.group_key); }
                        ImDrawList* dlT = ImGui::GetWindowDrawList();
                        float textY = selPos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                        char icoBuf[64];
                        int icoLen = sp ? (int)(sp - lbl) : (int)strlen(lbl);
                        if (icoLen >= (int)sizeof(icoBuf)) icoLen = (int)sizeof(icoBuf) - 1;
                        memcpy(icoBuf, lbl, icoLen); icoBuf[icoLen] = 0;
                        dlT->AddText(ImVec2(selPos.x, textY), accentU32, icoBuf);
                        if (sp)
                        {
                            float icoW = ImGui::CalcTextSize(icoBuf).x;
                            dlT->AddText(ImVec2(selPos.x + icoW + ImGui::CalcTextSize(" ").x, textY),
                                ImGui::GetColorU32(ImGuiCol_Text), sp + 1);
                        }
                    }
                    else
                    {
                        char tid[16]; snprintf(tid, sizeof(tid), "t%d", ti);
                        if (DrawSbRow(tid, lbl, t.icoCol, true))
                            s.selected_groups.insert(t.group_key);
                    }
                    if (typeCounts[ti] >= 0)
                    {
                        char badge[16];
                        snprintf(badge, sizeof(badge), "%d", typeCounts[ti]);
                        ImVec2 bSz = ImGui::CalcTextSize(badge);
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        ImVec2 rMin = ImGui::GetItemRectMin();
                        ImVec2 rMax = ImGui::GetItemRectMax();
                        dl->AddText(render::FontSmall, render::FontSmall ? render::FontSmall->LegacySize : bSz.y,
                            ImVec2(sbContentRight - bSz.x - sbPad, rMin.y + (rowH - bSz.y) * 0.5f),
                            ImGui::GetColorU32(ImGuiCol_TextDisabled), badge);
                    }
                }
            }

            ImGui::Spacing();
            ImGui::Spacing();

            // --- GROUPS section header (collapsible) ---
            {
                const char* chevron = s.sidebar_groups_collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN;
                ImGui::PushFont(render::FontSmall);
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                char groupsHeader[64];
                snprintf(groupsHeader, sizeof(groupsHeader), "%s  GROUPS", chevron);
                if (ImGui::Selectable(groupsHeader, false, 0, ImVec2(fullW, rowH)))
                    s.sidebar_groups_collapsed = !s.sidebar_groups_collapsed;
                ImGui::PopStyleColor();
                ImGui::PopFont();
            }

            if (!s.sidebar_groups_collapsed)
            {

                // Check if any user group filter is active
                bool anyGroupActive = false;
                for (const auto& key : s.selected_groups)
                    if (key.empty() || key[0] != '@') { anyGroupActive = true; break; }

                if (anyGroupActive)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    if (ImGui::Selectable(ICON_MDI_CLOSE "  Clear Groups", false, 0, ImVec2(fullW, rowH)))
                    {
                        std::erase_if(s.selected_groups, [](const std::string& k) {
                            return k.empty() || k[0] != '@';
                        });
                    }
                    ImGui::PopStyleColor();
                }

                // Cap group container height so tags aren't pushed off screen
                const float maxGroupsH = rowH * 12.0f;
                float availH = ImGui::GetContentRegionAvail().y;
                float cappedH = ImMin(maxGroupsH, availH * 0.365f);
                ImGui::PushStyleColor(ImGuiCol_ChildBg,              IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  IM_COL32(0, 0, 0, 0));
                ImGui::BeginChild("##sb_groups_scroll", ImVec2(fullW, cappedH), ImGuiChildFlags_None);

                // User groups
                for (const auto& g : s.groups)
                {
                    if (g == "Filter" || g == "All") continue;
                    if (!g.empty() && g[0] == '@') continue;
                    if (g == "---") continue;

                    bool active = s.selected_groups.count(g) > 0;
                    const char* ico = active ? ICON_MDI_FOLDER : ICON_MDI_FOLDER;

                    ImGui::PushID(g.c_str());
                    char label[256];
                    snprintf(label, sizeof(label), "%s  %s", ico, g.c_str());
                    ImVec2 curPos = ImGui::GetCursorScreenPos();
                    bool hovered = ImGui::IsMouseHoveringRect(curPos, ImVec2(curPos.x + fullW, curPos.y + rowH));

                    // Active indicator bar + highlight (full width)
                    if (active)
                    {
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        float hlLeft = ImGui::GetWindowPos().x;
                        dl->AddLine(
                            ImVec2(hlLeft, curPos.y),
                            ImVec2(sbContentRight, curPos.y),
                            dark ? theme::RowSeparatorTop.dark : theme::RowSeparatorTop.light, 1.0f);
                        dl->AddLine(
                            ImVec2(hlLeft, curPos.y + rowH),
                            ImVec2(sbContentRight, curPos.y + rowH),
                            dark ? theme::RowSeparatorBot.dark : theme::RowSeparatorBot.light, 1.0f);
                        ImU32 hlCol = dark ? theme::RowHighlight.dark : theme::RowHighlight.light;
                        dl->AddRectFilled(
                            ImVec2(hlLeft, curPos.y),
                            ImVec2(sbContentRight, curPos.y + rowH),
                            hlCol, 0.0f);
                    }

                    if (active)
                    {
                        const char* sp = strchr(label, ' ');
                        ImVec2 selPos = ImGui::GetCursorScreenPos();
                        ImGui::InvisibleButton("##gsel", ImVec2(fullW, rowH));
                        bool clicked = ImGui::IsItemClicked();
                        if (clicked) s.selected_groups.erase(g);
                        ImDrawList* dlG = ImGui::GetWindowDrawList();
                        float textY = selPos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                        char icoBuf[64];
                        int icoLen = sp ? (int)(sp - label) : (int)strlen(label);
                        if (icoLen >= (int)sizeof(icoBuf)) icoLen = (int)sizeof(icoBuf) - 1;
                        memcpy(icoBuf, label, icoLen); icoBuf[icoLen] = 0;
                        dlG->AddText(ImVec2(selPos.x, textY), accentU32, icoBuf);
                        if (sp)
                        {
                            float icoW = ImGui::CalcTextSize(icoBuf).x;
                            dlG->AddText(ImVec2(selPos.x + icoW + ImGui::CalcTextSize(" ").x, textY),
                                ImGui::GetColorU32(ImGuiCol_Text), sp + 1);
                        }
                    }
                    else
                    {
                        bool expanded = s.sidebar_expanded_groups.count(g) > 0;
                        if (DrawSbRow(g.c_str(), label, expanded ? accentU32 : sbIcoFolder, true))
                        {
                            if (s.sidebar_expanded_groups.count(g))
                                s.sidebar_expanded_groups.erase(g);
                            else
                                s.sidebar_expanded_groups.insert(g);
                        }
                    }

                    // Drop target: credential → group
                    if (ImGui::BeginDragDropTarget())
                    {
                        if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("CRED_ID"))
                        {
                            s.drag_drop_cred_id = *(int*)payload->Data;
                            s.drag_drop_target_group = g;
                        }
                        ImGui::EndDragDropTarget();
                    }

                    // Badge count (right-aligned, matching filter/type rows)
                    {
                        auto gcIt = groupCounts.find(g);
                        int gc = gcIt != groupCounts.end() ? gcIt->second : 0;
                        if (gc > 0)
                        {
                            char badge[16];
                            snprintf(badge, sizeof(badge), "%d", gc);
                            ImVec2 bSz = ImGui::CalcTextSize(badge);
                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            ImVec2 rMin = ImGui::GetItemRectMin();
                            dl->AddText(render::FontSmall, render::FontSmall ? render::FontSmall->LegacySize : bSz.y,
                                ImVec2(sbContentRight - bSz.x - sbPad, rMin.y + (rowH - bSz.y) * 0.5f),
                                ImGui::GetColorU32(ImGuiCol_TextDisabled), badge);
                        }
                    }

                    // Inline credential items when group is expanded
                    if (s.sidebar_expanded_groups.count(g) > 0)
                    {
                        auto git = s.sb_group_items.find(g);
                        if (git != s.sb_group_items.end())
                        {
                            ImGui::Indent(12.0f);
                            const float childRowH = 24.0f;
                            const float icoSz = 16.0f;
                            static std::unordered_set<std::string> s_show_all_groups;
                            bool showAll = s_show_all_groups.count(g) > 0;
                            int maxItems = showAll ? (int)git->second.size() : 20;
                            int totalToShow = (int)git->second.size() < maxItems ? (int)git->second.size() : maxItems;

                            // Tree line X position (left of indent)
                            float treeLineX = ImGui::GetCursorScreenPos().x - 6.0f;
                            ImU32 treeCol = dark ? IM_COL32(255, 255, 255, 25) : IM_COL32(0, 0, 0, 18);

                            int shown = 0;
                            for (const auto& child : git->second)
                            {
                                if (shown >= maxItems) break;
                                ImGui::PushID(child.id);
                                bool childSelected = (s.three_pane_selected_id == child.id);
                                bool isLast = (shown == totalToShow - 1);

                                char childLabel[64];
                                snprintf(childLabel, sizeof(childLabel), "##sbc_%d", child.id);
                                ImVec2 selPos = ImGui::GetCursorScreenPos();

                                ImGui::PushStyleColor(ImGuiCol_SliderGrab, colors::SecondColor);
                                if (ImGui::Selectable3(childLabel, childSelected, 0, ImVec2(fullW - sbPad - 12.0f, childRowH)))
                                {
                                    s.three_pane_selected_id = child.id;
                                    s.recent_touch_uuid = child.uuid;
                                }
                                ImGui::PopStyleColor();

                                // Draw favicon + title
                                {
                                    ImDrawList* cdl = ImGui::GetWindowDrawList();
                                    float iconX = selPos.x + 4.0f;
                                    float iconY = selPos.y + (childRowH - icoSz) * 0.5f;

                                    auto srv = favicon::Get(child.website);
                                    if (srv)
                                    {
                                        cdl->AddImageRounded((ImTextureID)srv,
                                            ImVec2(iconX, iconY), ImVec2(iconX + icoSz, iconY + icoSz),
                                            ImVec2(0,0), ImVec2(1,1), IM_COL32(255,255,255,255), 3.0f);
                                    }
                                    else
                                    {
                                        const char* tIco = CredTypeIcon(child.type);
                                        ImVec2 tSz = ImGui::CalcTextSize(tIco);
                                        cdl->AddText(ImVec2(iconX, selPos.y + (childRowH - tSz.y) * 0.5f),
                                            ImGui::GetColorU32(childSelected ? ImGuiCol_Text : ImGuiCol_TextDisabled), tIco);
                                    }

                                    // Title
                                    float textX = iconX + icoSz + 4.0f;
                                    float textY = selPos.y + (childRowH - ImGui::GetTextLineHeight()) * 0.5f;
                                    float maxTextW = fullW - sbPad - 32.0f;
                                    ImGui::PushClipRect(ImVec2(textX, selPos.y), ImVec2(textX + maxTextW, selPos.y + childRowH), true);
                                    cdl->AddText(ImVec2(textX, textY),
                                        ImGui::GetColorU32(childSelected ? ImGuiCol_Text : ImGuiCol_TextDisabled),
                                        child.title.c_str());
                                    ImGui::PopClipRect();
                                }

                                // Tree branch: horizontal tick from vertical line to icon
                                {
                                    ImDrawList* tdl = ImGui::GetWindowDrawList();
                                    float midY = selPos.y + childRowH * 0.5f;
                                    // Horizontal branch
                                    tdl->AddLine(ImVec2(treeLineX, midY), ImVec2(treeLineX + 5.0f, midY), treeCol, 1.0f);
                                    // Vertical line segment (from top of this row to midpoint, or full row if not last)
                                    float vTop = selPos.y;
                                    float vBot = isLast ? midY : (selPos.y + childRowH);
                                    tdl->AddLine(ImVec2(treeLineX, vTop), ImVec2(treeLineX, vBot), treeCol, 1.0f);
                                }

                                if (ImGui::IsItemHovered()) SetTooltipPadded("%s", child.title.c_str());
                                ImGui::PopID();
                                shown++;
                            }
                            if (!showAll && (int)git->second.size() > 20)
                            {
                                char moreBuf[32];
                                snprintf(moreBuf, sizeof(moreBuf), "+%d more", (int)git->second.size() - 20);
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                                if (ImGui::Selectable(moreBuf, false, 0, ImVec2(fullW - sbPad - 12.0f, childRowH)))
                                    s_show_all_groups.insert(g);
                                ImGui::PopStyleColor();
                            }
                            else if (showAll && (int)git->second.size() > 20)
                            {
                                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                                if (ImGui::Selectable("Show less", false, 0, ImVec2(fullW - sbPad - 12.0f, childRowH)))
                                    s_show_all_groups.erase(g);
                                ImGui::PopStyleColor();
                            }
                            ImGui::Unindent(12.0f);
                        }
                    }

                    ImGui::PopID();
                }

                ImGui::EndChild();
                ImGui::PopStyleColor(5);

            }
            ImGui::Unindent(sbPad);

            // --- TAGS section header (collapsible) ---
            if (!s.all_tags.empty())
            {
                ImGui::Spacing();
                ImGui::Spacing();
                ImGui::Indent(sbPad);

                {
                    const char* chevron = s.sidebar_tags_collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_DOWN;
                    ImGui::PushFont(render::FontSmall);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                    char tagsHeader[64];
                    snprintf(tagsHeader, sizeof(tagsHeader), "%s  TAGS", chevron);
                    if (ImGui::Selectable(tagsHeader, false, 0, ImVec2(fullW, rowH)))
                        s.sidebar_tags_collapsed = !s.sidebar_tags_collapsed;
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }

                if (!s.sidebar_tags_collapsed)
                {
                    bool anyTagActive = !s.selected_tags.empty();

                    if (anyTagActive)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
                        if (ImGui::Selectable(ICON_MDI_CLOSE "  Clear Tags", false, 0, ImVec2(fullW, rowH)))
                            s.selected_tags.clear();
                        ImGui::PopStyleColor();
                    }

                    // Scrollable tag list
                    float tagsMaxH = ImGui::GetContentRegionAvail().y - (rowH + 8.0f);
                    if (tagsMaxH < 60.0f) tagsMaxH = 60.0f;
                    ImGui::PushStyleColor(ImGuiCol_ChildBg,              IM_COL32(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          IM_COL32(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        IM_COL32(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(0, 0, 0, 0));
                    ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  IM_COL32(0, 0, 0, 0));
                    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
                    ImGui::BeginChild("##sb_tags_scroll", ImVec2(0, tagsMaxH), false);
                    ImGui::PopStyleVar();
                    ImGui::PopStyleColor(5);

                    for (const auto& tag : s.all_tags)
                    {
                        bool active = s.selected_tags.count(tag) > 0;
                        const char* ico = active ? ICON_MDI_TAG : ICON_MDI_TAG;
                        auto tcIt = s.sb_tag_counts.find(tag);
                        int tc = tcIt != s.sb_tag_counts.end() ? tcIt->second : 0;

                        char label[256];
                        snprintf(label, sizeof(label), "%s  %s", ico, tag.c_str());
                        ImVec2 curPos = ImGui::GetCursorScreenPos();
                        bool hovered = ImGui::IsMouseHoveringRect(curPos, ImVec2(curPos.x + fullW, curPos.y + rowH));

                        if (active)
                        {
                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            float hlLeft = ImGui::GetWindowPos().x;
                            dl->AddLine(
                                ImVec2(hlLeft, curPos.y),
                                ImVec2(sbContentRight, curPos.y),
                                dark ? theme::RowSeparatorTop.dark : theme::RowSeparatorTop.light, 1.0f);
                            dl->AddLine(
                                ImVec2(hlLeft, curPos.y + rowH),
                                ImVec2(sbContentRight, curPos.y + rowH),
                                dark ? theme::RowSeparatorBot.dark : theme::RowSeparatorBot.light, 1.0f);
                            ImU32 hlCol = dark ? theme::RowHighlight.dark : theme::RowHighlight.light;
                            dl->AddRectFilled(
                                ImVec2(hlLeft, curPos.y),
                                ImVec2(sbContentRight, curPos.y + rowH),
                                hlCol, 0.0f);
                        }

                        if (active)
                        {
                            const char* sp = strchr(label, ' ');
                            ImVec2 selPos = ImGui::GetCursorScreenPos();
                            ImGui::PushID(tag.c_str());
                            ImGui::InvisibleButton("##tgsel", ImVec2(fullW, rowH));
                            if (ImGui::IsItemClicked()) s.selected_tags.erase(tag);
                            ImGui::PopID();
                            ImDrawList* dlT = ImGui::GetWindowDrawList();
                            float textY = selPos.y + (rowH - ImGui::GetTextLineHeight()) * 0.5f;
                            char icoBuf[64];
                            int icoLen = sp ? (int)(sp - label) : (int)strlen(label);
                            if (icoLen >= (int)sizeof(icoBuf)) icoLen = (int)sizeof(icoBuf) - 1;
                            memcpy(icoBuf, label, icoLen); icoBuf[icoLen] = 0;
                            dlT->AddText(ImVec2(selPos.x, textY), accentU32, icoBuf);
                            if (sp)
                            {
                                float icoW = ImGui::CalcTextSize(icoBuf).x;
                                dlT->AddText(ImVec2(selPos.x + icoW + ImGui::CalcTextSize(" ").x, textY),
                                    ImGui::GetColorU32(ImGuiCol_Text), sp + 1);
                            }
                        }
                        else
                        {
                            if (DrawSbRow(tag.c_str(), label, sbIcoTag, true))
                                s.selected_tags.insert(tag);
                        }

                        // Badge count
                        if (tc > 0)
                        {
                            char badge[16];
                            snprintf(badge, sizeof(badge), "%d", tc);
                            ImVec2 bSz = ImGui::CalcTextSize(badge);
                            ImDrawList* dl = ImGui::GetWindowDrawList();
                            ImVec2 rMin = ImGui::GetItemRectMin();
                            dl->AddText(render::FontSmall, render::FontSmall ? render::FontSmall->LegacySize : bSz.y,
                                ImVec2(sbContentRight - bSz.x - sbPad, rMin.y + (rowH - bSz.y) * 0.5f),
                                ImGui::GetColorU32(ImGuiCol_TextDisabled), badge);
                        }
                    }

                    ImGui::EndChild(); // ##sb_tags_scroll
                }

                ImGui::Unindent(sbPad);
            }


        }

        // Tag filter popup (collapsed sidebar mode only)
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(6, 2));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, dark ? theme::PopupBg.dark : theme::PopupBg.light);
        ImGui::PushStyleColor(ImGuiCol_Border,  dark ? theme::PopupBorder.dark : theme::PopupBorder.light);
        if (ImGui::BeginPopup("##tag_filter_popup"))
        {
            PopupStyleBegin();
            ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);

            // Header
            ImGui::PushFont(render::FontBold);
            ImGui::TextUnformatted("Tags");
            ImGui::PopFont();
            ImGui::Separator();

            // Clear all
            if (!s.selected_tags.empty())
            {
                if (BarMenuItem(ICON_MDI_CLOSE "   Clear All", false, true))
                    s.selected_tags.clear();
                ImGui::Separator();
            }

            // Tag list with checkboxes
            for (const auto& tag : s.all_tags)
            {
                bool active = s.selected_tags.count(tag) > 0;
                auto tcIt = s.sb_tag_counts.find(tag);
                int tc = tcIt != s.sb_tag_counts.end() ? tcIt->second : 0;

                char label[256];
                if (tc > 0)
                    snprintf(label, sizeof(label), "%s   %s  (%d)", active ? ICON_MDI_TAG : ICON_MDI_TAG, tag.c_str(), tc);
                else
                    snprintf(label, sizeof(label), "%s   %s", active ? ICON_MDI_TAG : ICON_MDI_TAG, tag.c_str());

                if (BarMenuItemToggle(label, &active))
                {
                    if (active) s.selected_tags.insert(tag);
                    else        s.selected_tags.erase(tag);
                }
            }

            ImGui::PopItemFlag();
            PopupStyleEnd();
            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);

        ImGui::PopStyleVar();    // SelectableTextAlign
        ImGui::PopStyleColor(3); // Header, HeaderHovered, HeaderActive

        float sbScrollY = ImGui::GetScrollY();
        ImVec2 sbScrollMin = ImGui::GetWindowPos();
        float sbScrollW = ImGui::GetWindowSize().x;
        ImGui::EndChild(); // ##3p_sb_scroll
        // DrawScrollTopFade(sbScrollMin, sbScrollW, sbScrollY, -3.0f);

        EndLippedChild(); // ##3p_sidebar
    }

    void RenderThreePaneMiddleList(
        const std::vector<AccordionItem>& items,
        ShellState& s,
        uint32_t activeVaultKey,
        float animW)
    {
        const bool collapsed = s.three_pane_list_collapsed;
        const float listW = animW;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 8));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0,0,0,0));
        ImGui::BeginChild("##3p_list", ImVec2(listW, 0), ImGuiChildFlags_Borders);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        const ImU32 accentU32 = ImGui::GetColorU32(colors::SecondColor);
        float fullW = ImGui::GetContentRegionAvail().x;
        const float toggleCenterW = collapsed ? 56.0f : fullW; // fixed centering during animation

        // Toggle button + heading
        {
            const float mlPad = 17.0f;
            const char* toggleIcon = collapsed ? ICON_MDI_CHEVRON_RIGHT : ICON_MDI_CHEVRON_LEFT;
            float btnSz = 20.0f;
            if (collapsed)
            {
                float cx = (toggleCenterW - btnSz) * 0.5f;
                if (cx > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx);
            }
            else
            {
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + mlPad);
            }
            float btnY = ImGui::GetCursorPosY();
            if (IconButtonDoubleLip("##ml_toggle", toggleIcon, btnSz))
            {
                s.three_pane_list_collapsed = !collapsed;
                cfg::set_three_pane_list_collapsed(s.three_pane_list_collapsed);
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
                ImGui::OpenPopup("##ml_auto_popup");
            if (!collapsed)
            {
                ImGui::SameLine(0, 6);
                float textOffY = (btnSz - ImGui::GetTextLineHeight()) * 0.5f;
                ImGui::SetCursorPosY(btnY + textOffY);

                std::string heading;
                if (s.sort_mode == 0)       heading = s.pill_tab_0;
                else if (s.sort_mode == 1)  heading = s.pill_tab_1;
                else if (s.sort_mode == 3)  heading = "Recent";

                if (!s.selected_groups.empty())
                {
                    int shown = 0;
                    for (const auto& g : s.selected_groups)
                    {
                        if (shown >= 5)
                        {
                            int remaining = (int)s.selected_groups.size() - shown;
                            heading += " +" + std::to_string(remaining);
                            break;
                        }
                        if (!heading.empty()) heading += ", ";
                        if (g == "@Passwords")       heading += "Passwords";
                        else if (g == "@Cards")      heading += "Cards";
                        else if (g == "@Identity")   heading += "Identity";
                        else if (g == "@Notes")      heading += "Notes";
                        else                         heading += g;
                        shown++;
                    }
                }

                if (!s.selected_tags.empty())
                {
                    int shown = 0;
                    for (const auto& t : s.selected_tags)
                    {
                        if (shown >= 3)
                        {
                            int remaining = (int)s.selected_tags.size() - shown;
                            heading += " +" + std::to_string(remaining);
                            break;
                        }
                        if (heading.empty())
                            heading = t;
                        else
                            heading += ", " + t;
                        shown++;
                    }
                }

                if (heading.empty()) heading = "All Items";

                char headingBuf[256];
                int credCount = 0;
                for (const auto& it : items) { if (!it.is_header) credCount++; }
                snprintf(headingBuf, sizeof(headingBuf), "%s (%d)", heading.c_str(), credCount);
                ImGui::TextDisabled("%s", headingBuf);
            }

            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
            ImGui::PushStyleColor(ImGuiCol_PopupBg, IsDarkTheme() ? theme::PopupBg.dark : theme::PopupBg.light);
            ImGui::PushStyleColor(ImGuiCol_Border,  IsDarkTheme() ? theme::PopupBorder.dark : theme::PopupBorder.light);
            if (ImGui::BeginPopup("##ml_auto_popup"))
            {
                PopupStyleBegin();
                ImGui::TextDisabled("Auto-collapse");
                ImGui::Separator();
                bool mlAuto = s.three_pane_list_auto_collapse;
                if (ui::ToggleSwitch("##ml_auto_tgl", &mlAuto))
                {
                    s.three_pane_list_auto_collapse = mlAuto;
                    cfg::set_three_pane_list_auto_collapse(mlAuto);
                    if (mlAuto)
                    {
                        s.three_pane_list_collapsed = true;
                        cfg::set_three_pane_list_collapsed(true);
                    }
                    else
                    {
                        g_3p_list_hover_expanded = false;
                        g_3p_list_leave_timer = 0.0f;
                    }
                }
                PopupStyleEnd();
                ImGui::EndPopup();
            }
            ImGui::PopStyleColor(2);
            ImGui::PopStyleVar(3);

            ImGui::Spacing();
        }

        // Scrollable content area (header stays fixed above)
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg, IM_COL32(0,0,0,0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4.0f, 4.0f));
        ImGui::BeginChild("##3p_list_scroll", ImVec2(0, 0), ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
        fullW = ImGui::GetContentRegionAvail().x;

        if (collapsed)
        {
            // ---- COLLAPSED MODE: favicon/icon strip ----
            const float iconW = fullW;
            const float centerW = iconW; // center icons within available width
            const float icoSz = 24.0f;
            const float rowH = 38.0f;

            for (const auto& it : items)
            {
                if (it.is_header)
                {
                    // Compact group separator: first letter in parens, centered
                    // e.g. "December 2025" → "(D)", "Banking" → "(B)", "A" → "(A)"
                    char badge[8];
                    if (!it.header_label.empty())
                        snprintf(badge, sizeof(badge), "%c", it.header_label[0]);
                    else
                        snprintf(badge, sizeof(badge), "-");
                    ImVec2 bSz = ImGui::CalcTextSize(badge);
                    float cx = (centerW - bSz.x) * 0.5f;
                    if (cx > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx);
                    ImGui::TextDisabled("%s", badge);
                    if (ImGui::IsItemHovered()) SetTooltipPadded("%s", it.header_label.c_str());
                    continue;
                }

                ImGui::PushID((int)MakeRowKey(activeVaultKey, it.id));

                bool selected = (s.three_pane_selected_id == it.id);
                bool hovered_row = false;

                char label[64];
                snprintf(label, sizeof(label), "##cl_%d", it.id);

                float selW = rowH;
                float indent = (iconW - selW) * 0.5f;
                if (indent > 0) ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
                const uint64_t rowKey = MakeRowKey(activeVaultKey, it.id);
                ImVec2 selPos = ImGui::GetCursorScreenPos();
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, colors::SecondColor);
                if (ImGui::Selectable3(label, selected, 0, ImVec2(selW, rowH)))
                {
                    if (ImGui::GetIO().KeyCtrl)
                        ToggleSelected(rowKey);
                    else
                    {
                        s.three_pane_selected_id = it.id;
                        s.recent_touch_uuid = it.uuid;
                    }
                }
                ImGui::PopStyleColor();

                // Drag source for credential → group
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    ImGui::SetDragDropPayload("CRED_ID", &it.id, sizeof(int));
                    ImGui::Text("%s", it.title.c_str());
                    ImGui::EndDragDropSource();
                }

                hovered_row = ImGui::IsItemHovered();

                // Multi-select highlight
                if (IsSelected(rowKey))
                {
                    ImVec2 rMin = ImGui::GetItemRectMin();
                    ImVec2 rMax = ImGui::GetItemRectMax();
                    bool dk = IsDarkTheme();
                    ImU32 multiSelTint = dk ? IM_COL32(colors::SecondColor.x * 255, colors::SecondColor.y * 255, colors::SecondColor.z * 255, 35)
                                            : IM_COL32(colors::SecondColor.x * 255, colors::SecondColor.y * 255, colors::SecondColor.z * 255, 25);
                    ImGui::GetWindowDrawList()->AddRectFilled(rMin, rMax, multiSelTint, 4.0f);
                }

                if (hovered_row)
                {
                    if (s.hover_expand)
                        s.three_pane_selected_id = it.id;
                    SetTooltipPadded("%s", it.title.c_str());
                }

                // Draw favicon or type icon centered in row
                {
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    auto srv = favicon::Get(it.website);
                    const float drawSz = icoSz;

                    if (srv)
                    {
                        float iconX = selPos.x + (selW - drawSz) * 0.5f;
                        float iconY = selPos.y + (rowH - drawSz) * 0.5f;
                        dl->AddImage((ImTextureID)srv,
                            ImVec2(iconX, iconY), ImVec2(iconX + drawSz, iconY + drawSz));
                    }
                    else
                    {
                        const char* ico = CredTypeIcon(it.type);
                        ImVec2 txtSz = ImGui::CalcTextSize(ico);
                        float iconX = selPos.x + (selW - txtSz.x) * 0.5f;
                        float iconY = selPos.y + (rowH - txtSz.y) * 0.5f;
                        ImU32 icoCol = selected ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                        dl->AddText(ImVec2(iconX, iconY), icoCol, ico);
                    }
                }

                // Changed-entry tint overlay
                if (it.is_changed)
                {
                    ImVec2 rMin = ImGui::GetItemRectMin();
                    ImVec2 rMax = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRectFilled(rMin, rMax,
                        (it.changed_fields & FCF_IsNew) ? kNewRowTint : kChangedRowTint);
                }

                ImGui::PopID();

                ImGui::Dummy(ImVec2(0, 2.0f));

                // Row divider with shadow lip (centered with icons)
                {
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float winX = ImGui::GetWindowPos().x;
                    float x0 = winX + (iconW - rowH) * 0.5f;
                    float x1 = x0 + rowH;
                    float y = p.y - 2.0f;
                    bool dark = IsDarkTheme();
                    dl->AddLine(ImVec2(x0, y), ImVec2(x1, y),
                        dark ? theme::RowSeparatorTop.dark : theme::RowSeparatorTop.light, 1.0f);
                    dl->AddLine(ImVec2(x0, y + 1.0f), ImVec2(x1, y + 1.0f),
                        dark ? theme::MenuItemHover.dark : theme::MenuItemHover.light, 1.0f);
                }
            }
        }
        else
        {
            // ---- EXPANDED MODE (existing code) ----
            ImGui::Indent(8.0f);

            bool tpSkipUntilNextHeader = false;
            for (const auto& it : items)
            {
                if (it.is_header)
                {
                    ImGui::Spacing();
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
                    tpSkipUntilNextHeader = hdrCollapsed;
                    continue;
                }

                if (tpSkipUntilNextHeader) continue;

                ImGui::PushID((int)MakeRowKey(activeVaultKey, it.id));

                bool selected = (s.three_pane_selected_id == it.id);

                // Build subtitle based on type + per-type toggles
                const auto& cols = s.detailed_header_cols;
                std::string subtitle;
                switch (it.type)
                {
                case CredType::Password:
                    if (cols.sub_password) {
                        if (cols.email && !it.email.empty())       subtitle = MaskIfPrivate(it.email) ? MaskIfPrivate(it.email) : it.email;
                        else if (cols.username && !it.user.empty()) subtitle = MaskIfPrivate(it.user) ? MaskIfPrivate(it.user) : it.user;
                    }
                    break;
                case CredType::CreditCard:
                    if (cols.sub_card) {
                        if (it.card_number.size() >= 4)
                            subtitle = std::string("****") + it.card_number.substr(it.card_number.size() - 4);
                        else
                            subtitle = MaskIfPrivate(it.cardholder_name) ? MaskIfPrivate(it.cardholder_name) : it.cardholder_name;
                    }
                    break;
                case CredType::Identity:
                    if (cols.sub_identity)
                        subtitle = MaskIfPrivate(it.full_name) ? MaskIfPrivate(it.full_name) : it.full_name;
                    break;
                case CredType::SecureNote:
                    if (cols.sub_note)
                    {
                        if (cfg::get_reprompt_reveal_notes())
                            subtitle = "Secure Note";
                        else
                            subtitle = it.notes.substr(0, 40);
                    }
                    break;
                }

                // Title line in regular font, subtitle in small font
                ImGui::PushFont(render::FontRegular);
                auto srv = favicon::Get(it.website);
                char label[64];
                snprintf(label, sizeof(label), "##3p_%d", it.id);

                const uint64_t rowKey = MakeRowKey(activeVaultKey, it.id);
                ImVec2 selPos = ImGui::GetCursorScreenPos();
                ImGui::PushStyleColor(ImGuiCol_SliderGrab, colors::SecondColor);
                if (ImGui::Selectable3(label, selected, 0, ImVec2(fullW - 16.0f, 38)))
                {
                    if (ImGui::GetIO().KeyCtrl)
                        ToggleSelected(rowKey);
                    else
                    {
                        s.three_pane_selected_id = it.id;
                        s.recent_touch_uuid = it.uuid;
                    }
                }

                ImGui::PopStyleColor();

                // Drag source for credential → group
                if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID))
                {
                    ImGui::SetDragDropPayload("CRED_ID", &it.id, sizeof(int));
                    ImGui::Text("%s", it.title.c_str());
                    ImGui::EndDragDropSource();
                }

                if (s.hover_expand && ImGui::IsItemHovered())
                    s.three_pane_selected_id = it.id;

                // Multi-select highlight
                if (IsSelected(rowKey))
                {
                    ImVec2 rMin = ImGui::GetItemRectMin();
                    ImVec2 rMax = ImGui::GetItemRectMax();
                    bool dk = IsDarkTheme();
                    ImU32 multiSelTint = dk ? IM_COL32(colors::SecondColor.x * 255, colors::SecondColor.y * 255, colors::SecondColor.z * 255, 35)
                                            : IM_COL32(colors::SecondColor.x * 255, colors::SecondColor.y * 255, colors::SecondColor.z * 255, 25);
                    ImGui::GetWindowDrawList()->AddRectFilled(rMin, rMax, multiSelTint, 4.0f);
                }

                bool hovered_exp = ImGui::IsItemHovered();

                // Draw favicon + vertically centered title/subtitle via DrawList
                {
                    ImDrawList* fdl = ImGui::GetWindowDrawList();
                    ImVec2 rMin = ImGui::GetItemRectMin();
                    float regSz  = render::FontBold ? render::FontBold->LegacySize : ImGui::GetTextLineHeight();
                    float subSz  = render::FontSmall ? render::FontSmall->LegacySize : ImGui::GetTextLineHeight();
                    float textX  = rMin.x + 44.0f;
                    const float rowH = 38.0f;
                    const float gap = 2.0f;

                    // Compute total text block height and center it
                    float blockH = regSz + (subtitle.empty() ? 0.0f : gap + subSz);
                    float blockY = rMin.y + (rowH - blockH) * 0.5f + 2.0f;

                    // Title (overwrite selectable's own text rendering with centered version)
                    {
                        const char* titleText = cols.title ? it.title.c_str() : "";
                        ImVec2 titlePos(textX, blockY);
                        fdl->AddText(render::FontBold, regSz, titlePos, ImGui::GetColorU32(ImGuiCol_Text), titleText);
                    }

                    // Subtitle (brighter when selected)
                    if (!subtitle.empty())
                    {
                        ImU32 subCol = selected
                            ? ImGui::GetColorU32(ImGuiCol_Text, 0.7f)
                            : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                        fdl->AddText(render::FontSmall, subSz,
                            ImVec2(textX, blockY + regSz + gap), subCol, subtitle.c_str());
                    }

                    // Favicon or type icon fallback
                    const float icoSz = 24.0f;

                    if (srv)
                    {
                        float iconY = rMin.y + (rowH - icoSz) * 0.5f + 2.0f;
                        float iconX = rMin.x + 12.0f;
                        fdl->AddImage((ImTextureID)srv,
                            ImVec2(iconX, iconY), ImVec2(iconX + icoSz, iconY + icoSz));
                    }
                    else
                    {
                        const char* ico = CredTypeIcon(it.type);
                        float icoFontSz = render::FontRegular ? render::FontRegular->LegacySize : regSz;
                        float iconY = rMin.y + (rowH - icoFontSz) * 0.5f + 2.0f;
                        ImU32 icoCol = selected ? ImGui::GetColorU32(ImGuiCol_Text) : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                        fdl->AddText(render::FontRegular, icoFontSz,
                            ImVec2(rMin.x + 12.0f, iconY), icoCol, ico);
                    }
                }

                // Right-click context menu on row (only if no other popup is open)
                bool threePaneCtxAllowed = !ImGui::IsPopupOpen((const char*)NULL, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel)
                                        || ImGui::IsPopupOpen("##3p_list_ctx");
                ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  8.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 8));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(4, 4));
                ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(6, 2));
                ImGui::PushStyleColor(ImGuiCol_PopupBg, IsDarkTheme() ? theme::PopupBg.dark : theme::PopupBg.light);
                ImGui::PushStyleColor(ImGuiCol_Border,  IsDarkTheme() ? theme::PopupBorder.dark : theme::PopupBorder.light);

                if (threePaneCtxAllowed && ImGui::BeginPopupContextItem("##3p_list_ctx"))
                {
                    PopupStyleBegin();
                    ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
                    ImGui::PushFont(render::FontSmall);
                    auto& mcols = s.detailed_header_cols;
                    if (BarMenuItemToggle("Show Title",    &mcols.title,    true, true)) cfg::set_detailed_header_columns(mcols);
                    if (BarMenuItemToggle("Show Username", &mcols.username, true, true)) cfg::set_detailed_header_columns(mcols);
                    if (BarMenuItemToggle("Show Email",    &mcols.email,    true, true)) cfg::set_detailed_header_columns(mcols);
                    if (BarMenuItemToggle("Show Pin/Fav",  &mcols.pin_fav,  true, true)) cfg::set_detailed_header_columns(mcols);
                    ImGui::Separator();
                    if (BarMenuItemToggle("Password Subtitle", &mcols.sub_password, true, true)) cfg::set_detailed_header_columns(mcols);
                    if (BarMenuItemToggle("Card Subtitle",     &mcols.sub_card,     true, true)) cfg::set_detailed_header_columns(mcols);
                    if (BarMenuItemToggle("Identity Subtitle", &mcols.sub_identity, true, true)) cfg::set_detailed_header_columns(mcols);
                    if (BarMenuItemToggle("Note Subtitle",     &mcols.sub_note,     true, true)) cfg::set_detailed_header_columns(mcols);
                    ImGui::PopFont();
                    ImGui::PopItemFlag();
                    PopupStyleEnd();
                    ImGui::EndPopup();
                }
                ImGui::PopStyleColor(2);
                ImGui::PopStyleVar(5);

                ImGui::PopFont();

                // Changed-entry tint overlay
                if (it.is_changed)
                {
                    ImVec2 rMin = ImGui::GetItemRectMin();
                    ImVec2 rMax = ImGui::GetItemRectMax();
                    ImGui::GetWindowDrawList()->AddRectFilled(rMin, rMax,
                        (it.changed_fields & FCF_IsNew) ? kNewRowTint : kChangedRowTint);
                }

                // Draw pin/fav + security icons on the right side of the selectable row
                {
                    ImVec2 rMin = ImGui::GetItemRectMin();
                    ImVec2 rMax = ImGui::GetItemRectMax();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float iconY = rMin.y + 2.0f;
                    float iconX = rMax.x - 12.0f;
                    ImU32 iconCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                    ImU32 icoCol = selected
                        ? ImGui::GetColorU32(ImGuiCol_Text, 0.7f)
                        : iconCol;
                    if (cols.pin_fav && it.is_favorite)
                    {
                        ImVec2 sz = ImGui::CalcTextSize(ICON_MDI_HEART);
                        iconX -= sz.x;
                        dl->AddText(ImVec2(iconX, iconY), GetFavoriteColor(), ICON_MDI_HEART);
                        iconX -= 3.0f;
                    }
                    if (cols.pin_fav && it.is_pinned)
                    {
                        ImVec2 sz = ImGui::CalcTextSize(ICON_MDI_PIN);
                        iconX -= sz.x;
                        dl->AddText(ImVec2(iconX, iconY), ImGui::GetColorU32(colors::MainColor), ICON_MDI_PIN);
                        iconX -= 3.0f;
                    }

                    // Security badges on second line (right-aligned)
                    float badgeY = rMin.y + ImGui::GetTextLineHeight() + 2.0f;
                    float badgeX = rMax.x - 12.0f;
                    if (s.sec_highlight_weak && s.sec_weak_ids.count(it.id))
                    {
                        ImVec2 sz = ImGui::CalcTextSize(ICON_MDI_ALERT);
                        badgeX -= sz.x;
                        dl->AddText(ImVec2(badgeX, badgeY), colors::StatusWeak, ICON_MDI_ALERT);
                        badgeX -= 3.0f;
                    }
                    if (s.sec_highlight_reused && s.sec_reused_ids.count(it.id))
                    {
                        ImVec2 sz = ImGui::CalcTextSize(ICON_MDI_REPEAT);
                        badgeX -= sz.x;
                        dl->AddText(ImVec2(badgeX, badgeY), colors::StatusReused, ICON_MDI_REPEAT);
                        badgeX -= 3.0f;
                    }
                    if (s.sec_highlight_exposed && s.sec_exposed_ids.count(it.id))
                    {
                        ImVec2 sz = ImGui::CalcTextSize(ICON_MDI_EARTH);
                        badgeX -= sz.x;
                        dl->AddText(ImVec2(badgeX, badgeY), colors::StatusExposed, ICON_MDI_EARTH);
                        badgeX -= 3.0f;
                    }
                    if (s.sec_highlight_aging && s.sec_aging_ids.count(it.id))
                    {
                        ImVec2 sz = ImGui::CalcTextSize(ICON_MDI_CLOCK_ALERT);
                        badgeX -= sz.x;
                        dl->AddText(ImVec2(badgeX, badgeY), colors::StatusAging, ICON_MDI_CLOCK_ALERT);
                        badgeX -= 3.0f;
                    }
                }
                // Row divider with shadow lip
                {
                    ImVec2 p = ImGui::GetCursorScreenPos();
                    ImDrawList* dl = ImGui::GetWindowDrawList();
                    float x0 = p.x;
                    float x1 = x0 + fullW - 12.0f;
                    float y = p.y - 2.0f;
                    bool dark = IsDarkTheme();
                    dl->AddLine(ImVec2(x0, y), ImVec2(x1, y),
                        dark ? theme::RowSeparatorTop.dark : theme::RowSeparatorTop.light, 1.0f);
                    dl->AddLine(ImVec2(x0, y + 1.0f), ImVec2(x1, y + 1.0f),
                        dark ? theme::MenuItemHover.dark : theme::MenuItemHover.light, 1.0f);
                }

                ImGui::PopID();
            }
            ImGui::Unindent(8.0f);
        }

        float mlScrollY = ImGui::GetScrollY();
        ImVec2 mlScrollMin = ImGui::GetWindowPos();
        float mlScrollW = ImGui::GetWindowSize().x;
        ImGui::EndChild(); // ##3p_list_scroll
        // DrawScrollTopFade(mlScrollMin, mlScrollW, mlScrollY, -3.0f);

        ImGui::EndChild(); // ##3p_list
    }

    // Security analysis card for password credentials (no toggle, always shown)
    void DrawSecurityCard(const AccordionItem& c, ShellState& s,
        uint32_t activeVaultKey, GetPasswordFn get_password_fn, float contentW)
    {
        if (c.type != CredType::Password) return;

        const float padX = 12.0f;
        const float pillGap = 12.0f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        float cardW = contentW;

        bool dk = IsDarkTheme();
        LiftedChildColorSet lc;
        lc.bg          = dk ? theme::CardSurface.dark  : theme::CardSurface.light;
        lc.borderOuter = dk ? theme::CardBorderOuter.dark      : theme::CardBorderOuter.light;
        lc.borderInner = dk ? theme::CardBorderInner.dark : theme::CardBorderInner.light;
        SetLiftedChildColors(lc);

        if (!BeginLiftedChild("##sec_card_lifted", ImVec2(contentW, 0), ImGuiWindowFlags_NoScrollbar))
        {
            EndLiftedChild();
            return;
        }

        ImGui::Dummy(ImVec2(0, 6.0f));
        ImGui::Indent(padX);

        // Strength meter
        const char* pw = get_password_fn ? get_password_fn(c.id) : "";
        std::string pwStr = pw ? pw : "";
        float meterW = cardW - padX * 2;
        DrawStrengthMeterCompact(pwStr, meterW);
        ImGui::Spacing();

        // Strength context hints + improvements summary
        if (!pwStr.empty())
        {
            helpers::PwStrength ps = helpers::analyze_password(pwStr);
            ImGui::PushFont(render::FontSmall);
            const ImU32 goodCol = colors::TimerGood;
            const ImU32 warnCol = colors::TimerWarning;

            int classes = (int)ps.hasLower + (int)ps.hasUpper + (int)ps.hasDigit + (int)ps.hasSymbol;
            int hints = 0;

            if (ps.shortPwd && hints < 3)
            { DrawHintLine(ICON_MDI_CLOSE, "Too short (< 8 chars)", warnCol); hints++; }
            if (ps.allSame && hints < 3)
            { DrawHintLine(ICON_MDI_CLOSE, "All same character", warnCol); hints++; }
            if (ps.simpleSeq && hints < 3)
            { DrawHintLine(ICON_MDI_CLOSE, "Simple sequence detected", warnCol); hints++; }
            if (classes < 2 && hints < 3)
            { DrawHintLine(ICON_MDI_CLOSE, "Only one character type", warnCol); hints++; }
            if (classes >= 3 && hints < 3)
            { DrawHintLine(ICON_MDI_CHECK, "Mixed character types", goodCol); hints++; }
            if ((int)pwStr.size() >= 16 && hints < 3)
            { DrawHintLine(ICON_MDI_CHECK, "Long password (16+)", goodCol); hints++; }

            // Count total improvements
            int improvements = 0;
            if (ps.shortPwd) improvements++;
            if (ps.allSame) improvements++;
            if (ps.simpleSeq) improvements++;
            if (!ps.hasUpper) improvements++;
            if (!ps.hasLower) improvements++;
            if (!ps.hasDigit) improvements++;
            if (!ps.hasSymbol) improvements++;

            if (improvements > 0)
            {
                char impBuf[64];
                snprintf(impBuf, sizeof(impBuf), ICON_MDI_INFORMATION " %d more improvement%s suggested", improvements, improvements == 1 ? "" : "s");
                ImGui::TextDisabled("%s", impBuf);
            }

            ImGui::PopFont();
            ImGui::Spacing();
        }

        // Issue pills
        bool isReused  = s.sec_reused_ids.count(c.id) != 0;
        bool isExposed = s.sec_exposed_ids.count(c.id) != 0;
        bool isAging   = s.sec_aging_ids.count(c.id) != 0;

        if (isExposed || isAging)
        {
            ImGui::Dummy(ImVec2(0, 2));
            float cursorX = ImGui::GetCursorScreenPos().x;
            float cursorY = ImGui::GetCursorScreenPos().y;
            float xOff = 0.0f;

            auto DrawPill = [&](const char* icon, const char* label, ImU32 col) {
                std::string text = std::string(icon) + " " + label;
                ImVec2 sz = ImGui::CalcTextSize(text.c_str());
                dl->AddText(ImVec2(cursorX + xOff, cursorY), col, text.c_str());
                xOff += sz.x + pillGap;
            };
            if (isExposed)
                DrawPill(ICON_MDI_EARTH, "Exposed", colors::StatusExposed);
            if (isAging)
            {
                int64_t pw_set_at = 0;
                if (!c.password_history.empty() && c.password_history[0].changed_at_ms > 0)
                    pw_set_at = c.password_history[0].changed_at_ms;
                else if (c.created_at_ms > 0)
                    pw_set_at = c.created_at_ms;

                std::string ageLabel = "Old";
                if (pw_set_at > 0)
                {
                    int64_t ageDays = (helpers::now_unix_ms() - pw_set_at) / (86400LL * 1000LL);
                    ageLabel = std::to_string(ageDays) + "d old";
                }
                DrawPill(ICON_MDI_CLOCK_ALERT, ageLabel.c_str(), colors::StatusAging);
            }

            float lineH = ImGui::CalcTextSize("A").y;
            ImGui::Dummy(ImVec2(0, lineH + 2));
        }

        // Reused password detail
        if (isReused && get_password_fn)
        {
            const char* thisPw = get_password_fn(c.id);
            if (thisPw && thisPw[0])
            {
                const auto& creds = GetActiveVaultCreds();
                struct ReusedEntry { int id; std::string title; };
                std::vector<ReusedEntry> reused;
                for (const auto& other : creds)
                {
                    if (other.id == c.id || other.is_deleted()) continue;
                    if (other.type != CredType::Password) continue;
                    const char* otherPw = get_password_fn(other.id);
                    if (otherPw && strcmp(thisPw, otherPw) == 0)
                        reused.push_back({ other.id, other.title.empty() ? "(Untitled)" : other.title });
                }
                if (!reused.empty())
                {
                    ImGui::PushFont(render::FontSmall);
                    char header[64];
                    snprintf(header, sizeof(header), ICON_MDI_REPEAT " Reused across %d other credential%s",
                        (int)reused.size(), reused.size() == 1 ? "" : "s");
                    ImGui::PushStyleColor(ImGuiCol_Text, colors::SyncWarning);
                    ImGui::TextUnformatted(header);
                    ImGui::PopStyleColor();

                    // Render each reused credential as a flow-wrapped clickable link list
                    bool dk = IsDarkTheme();
                    ImU32 linkCol     = dk ? colors::SyncPendingLight : colors::SyncPending;
                    ImU32 linkHoverCol = dk ? colors::SyncOnlineLight : colors::SyncOnline;
                    const float commaW = ImGui::CalcTextSize(", ").x;
                    const float lineH  = ImGui::GetTextLineHeight();
                    const float regionMinX = ImGui::GetCursorScreenPos().x;
                    const float regionMaxX = regionMinX + ImGui::GetContentRegionAvail().x;
                    float curX = regionMinX;
                    float curY = ImGui::GetCursorScreenPos().y;

                    for (size_t i = 0; i < reused.size(); i++)
                    {
                        const auto& entry = reused[i];
                        ImGui::PushID((int)i);

                        ImVec2 textSize = ImGui::CalcTextSize(entry.title.c_str());
                        float entryW = textSize.x + (i + 1 < reused.size() ? commaW : 0.0f);

                        // Wrap to next line if this entry won't fit
                        if (i > 0 && curX + entryW > regionMaxX)
                        {
                            curX = regionMinX;
                            curY += lineH + 2.0f;
                        }

                        // Position and draw the invisible button for interaction
                        ImGui::SetCursorScreenPos(ImVec2(curX, curY));
                        ImGui::InvisibleButton("##reused_link", textSize);
                        bool hovered = ImGui::IsItemHovered();
                        bool clicked = ImGui::IsItemClicked();

                        ImU32 col = hovered ? linkHoverCol : linkCol;
                        ImDrawList* dl = ImGui::GetWindowDrawList();
                        dl->AddText(ImVec2(curX, curY), col, entry.title.c_str());
                        if (hovered)
                        {
                            dl->AddLine(ImVec2(curX, curY + textSize.y), ImVec2(curX + textSize.x, curY + textSize.y), col);
                            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        }

                        if (clicked)
                        {
                            // Navigate to the reused credential
                            if (s.view_mode == ViewMode::ThreePane)
                            {
                                s.three_pane_selected_id = entry.id;
                            }
                            else
                            {
                                uint64_t targetKey = MakeRowKey(activeVaultKey, entry.id);
                                g_open.insert(targetKey);
                                g_scroll_to_open[targetKey] = 2;
                            }
                        }

                        curX += textSize.x;

                        // Comma separator
                        if (i + 1 < reused.size())
                        {
                            dl->AddText(ImVec2(curX, curY), ImGui::GetColorU32(ImGuiCol_TextDisabled), ", ");
                            curX += commaW;
                        }

                        ImGui::PopID();
                    }
                    // Advance cursor past the last line of flow layout
                    ImGui::SetCursorScreenPos(ImVec2(regionMinX, curY + lineH + 2.0f));
                    ImGui::PopFont();
                    ImGui::Spacing();
                }
            }
        }

        // Separator between password feedback and sync/backup
        {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float x0 = p.x;
            float x1 = x0 + meterW;
            bool dk = IsDarkTheme();
            ImGui::GetWindowDrawList()->AddLine(
                ImVec2(x0, p.y), ImVec2(x1, p.y),
                dk ? theme::PillDivider.dark : theme::PillDivider.light, 1.0f);
            ImGui::Dummy(ImVec2(0, 1.0f));
        }

        // Sync / Backup status (always show)
        {
            bool hasBackups = !s.footer_backup_meta.empty();

            ImGui::Dummy(ImVec2(0, 2));
            std::string statusLine;

            if (s.sync_logged_in)
                statusLine = ICON_MDI_CLOUD_CHECK " Synced";
            else
                statusLine = ICON_MDI_CLOUD_OFF " Not synced";

            statusLine += "  \xc2\xb7  ";
            statusLine += hasBackups ? (ICON_MDI_HISTORY " Backed up") : (ICON_MDI_HISTORY " No backups");

            ImGui::TextDisabled("%s", statusLine.c_str());
        }

        // Share status (view count, expiry)
        if (g_shell_ptr)
            DrawShareStatusRow(c, *g_shell_ptr);

        ImGui::Unindent(padX);
        ImGui::Dummy(ImVec2(0, 11.0f));

        EndLiftedChild();
    }

    void RenderThreePaneDetailPane(
        const std::vector<AccordionItem>& items,
        ShellState& s,
        uint32_t activeVaultKey,
        GetPasswordFn get_password_fn,
        bool read_only,
        AccordionListResult& out)
    {
        float detailW = ImGui::GetContentRegionAvail().x;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 4));
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0,0,0,0));
        ImGui::BeginChild("##3p_detail", ImVec2(detailW, 0), ImGuiChildFlags_Borders);
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        if (s.three_pane_selected_id == -1)
        {
            // Centered placeholder with icon
            ImVec2 avail = ImGui::GetContentRegionAvail();
            const char* ico = ICON_MDI_KEY;
            const char* msg = "Select an item";
            ImVec2 icoSz = ImGui::CalcTextSize(ico);
            ImVec2 msgSz = ImGui::CalcTextSize(msg);
            float cx = avail.x * 0.5f;
            float cy = avail.y * 0.4f;
            ImGui::SetCursorPos(ImVec2(cx - icoSz.x * 0.5f, cy - icoSz.y - 4.0f));
            ImGui::TextDisabled("%s", ico);
            ImGui::SetCursorPos(ImVec2(cx - msgSz.x * 0.5f, cy + 4.0f));
            ImGui::TextDisabled("%s", msg);
            ImGui::EndChild();
            return;
        }

        // Find the selected item
        const AccordionItem* sel = nullptr;
        for (const auto& it : items)
        {
            if (!it.is_header && it.id == s.three_pane_selected_id)
            { sel = &it; break; }
        }
        if (!sel)
        {
            ImGui::TextDisabled("Item not found.");
            ImGui::EndChild();
            return;
        }

        const auto& c = *sel;

        // Layout constants matching existing row helpers
        const float iconBtnSz = 16.0f;
        const float iconColW  = 30.0f;
        const float rowWidth  = ImGui::GetContentRegionAvail().x - 20.0f;
        const float notesWidth  = ImGui::GetContentRegionAvail().x - 6.0f;
        const float valueColW = rowWidth - iconColW - 10.0f;
        const float rowSpacing = 6.0f;

        // Scrollable content area (action buttons pinned at bottom)
        const float bottomBarH = 38.0f;
        ImGui::PushStyleColor(ImGuiCol_ScrollbarBg,          IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrab,        IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_ScrollbarGrabActive,  IM_COL32(0, 0, 0, 0));
        ImGui::BeginChild("##3p_detail_scroll", ImVec2(0, -bottomBarH), ImGuiChildFlags_None);
        ImGui::PopStyleColor(4);

        // --- Header: favicon + title + type subtitle ---
        ImVec2 headerMin;
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float headerH = 48.0f;
            headerMin = ImGui::GetCursorScreenPos();

            // Reserve header space
            ImGui::Dummy(ImVec2(0, headerH));

            float textX = headerMin.x;

            // Favicon or type icon
            auto srv = favicon::Get(c.website);
            const float faviconSz = 28.0f;
            if (srv)
            {
                float iconY = headerMin.y + (headerH - faviconSz) * 0.5f;
                dl->AddImage((ImTextureID)srv,
                    ImVec2(textX, iconY), ImVec2(textX + faviconSz, iconY + faviconSz));
                textX += faviconSz + 10.0f;
            }
            else
            {
                const char* typeIco = CredTypeIcon(c.type);
                ImVec2 icoSz = ImGui::CalcTextSize(typeIco);
                float iconY = headerMin.y + (headerH - icoSz.y) * 0.5f;
                dl->AddText(ImVec2(textX, iconY),
                    ImGui::GetColorU32(ImGuiCol_TextDisabled), typeIco);
                textX += icoSz.x + 10.0f;
            }

            // Two-line: title + type label
            float titleFontSz = render::FontBold ? render::FontBold->LegacySize : ImGui::GetTextLineHeight();
            float subFontSz   = render::FontSmall ? render::FontSmall->LegacySize : ImGui::GetTextLineHeight();
            const char* subText = CredTypeLabel(c.type);

            const float gap = 2.0f;
            float blockH = titleFontSz + gap + subFontSz;
            float blockY = headerMin.y + (headerH - blockH) * 0.5f;

            // Clip title to available width
            float maxTitleW = ImGui::GetContentRegionAvail().x - textX + headerMin.x - 10.0f;
            dl->PushClipRect(ImVec2(textX, blockY), ImVec2(textX + maxTitleW, blockY + titleFontSz + 1.0f), true);
            dl->AddText(render::FontBold, titleFontSz,
                ImVec2(textX, blockY), ImGui::GetColorU32(ImGuiCol_Text), c.title.c_str());
            dl->PopClipRect();

            dl->AddText(render::FontSmall, subFontSz,
                ImVec2(textX, blockY + titleFontSz + gap),
                ImGui::GetColorU32(ImGuiCol_TextDisabled), subText);
        }

        // Pin/Fav moved to action bar

        ImGui::Spacing();
        ImGui::Spacing();

        // --- Type-conditional rows (reuse existing Draw* helpers) ---
        static std::set<int> visiblePasswordsThreePane;
        const bool isNew = !!(c.changed_fields & FCF_IsNew);

        // Shared container constants for all credential types
        const float cGap = 10.0f, cBoxPad = 10.0f;
        const bool dark = IsDarkTheme();
        ImU32 cDivCol = dark ? IM_COL32(255, 255, 255, 12) : IM_COL32(0, 0, 0, 12);
        auto DrawRowDivider = [&](ImDrawList* dl, float w) {
            ImVec2 p = ImGui::GetCursorScreenPos();
            float lineY = p.y - 2.0f;
            dl->AddLine(ImVec2(p.x, lineY), ImVec2(p.x + w, lineY), cDivCol, 1.0f);
            ImGui::Dummy(ImVec2(0, 2.0f));
        };

        // Set lifted child colors for all containers
        LiftedChildColorSet liftedColors;
        liftedColors.bg          = dark ? theme::CardSurface.dark  : theme::CardSurface.light;
        liftedColors.borderOuter = dark ? theme::CardBorderOuter.dark      : theme::CardBorderOuter.light;
        liftedColors.borderInner = dark ? theme::CardBorderInner.dark : theme::CardBorderInner.light;
        SetLiftedChildColors(liftedColors);

        if (c.type == CredType::Password)
        {
            // --- About container (website + group + timestamps) ---
            {
                ImGui::PushFont(render::FontSmall);
                ImGui::TextDisabled("About");
                ImGui::PopFont();

                if (BeginLiftedChild("##tp_about", ImVec2(0, 0), ImGuiWindowFlags_NoScrollbar))
                {
                    ImGui::Dummy(ImVec2(0, 6.0f));
                    ImGui::Indent(cBoxPad);

                    g_field_accent_active = isNew || !!(c.changed_fields & FCF_Website);
                    DrawWebsiteRow("tp_website", c.website, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing);
                    DrawRowDivider(ImGui::GetWindowDrawList(), rowWidth);
                    g_field_accent_active = isNew || !!(c.changed_fields & FCF_Group);
                    if (!c.group.empty())
                    {
                        DrawInfoRow("tp_group", ICON_MDI_FOLDER, c.group, iconColW, valueColW, rowSpacing - 5);
                        DrawRowDivider(ImGui::GetWindowDrawList(), rowWidth);
                    }
                    g_field_accent_active = false;

                    // Timestamps inside About
                    if (c.created_at_ms != 0 || c.updated_at_ms != 0)
                    {
                        bool anyCollapsed = s.three_pane_sidebar_collapsed || s.three_pane_list_collapsed;
                        auto FormatTS = [anyCollapsed](int64_t ms) -> std::string {
                            return anyCollapsed ? FormatUnixMsDateLabel(ms) : FormatUnixMsDateOnly(ms);
                        };
                        std::string ts;
                        if (c.created_at_ms != 0)
                            ts = "Created " + FormatTS(c.created_at_ms);
                        if (c.updated_at_ms != 0)
                        {
                            if (!ts.empty()) ts += "  \xc2\xb7  ";
                            ts += "Updated " + FormatTS(c.updated_at_ms);
                        }
                        ImGui::PushFont(render::FontSmall);
                        ImGui::TextDisabled("%s", ts.c_str());
                        ImGui::PopFont();
                        ImGui::Dummy(ImVec2(0, 4.0f));
                    }

                    //ImGui::Dummy(ImVec2(0, 1.0f));
                    ImGui::Unindent(cBoxPad);
                }
                EndLiftedChild();
            }

            ImGui::Dummy(ImVec2(0, cGap));

            // --- Account container (user + email + password + TOTP) ---
            {
                ImGui::PushFont(render::FontSmall);
                ImGui::TextDisabled("Account");
                ImGui::PopFont();

                if (BeginLiftedChild("##tp_account", ImVec2(0, 0), ImGuiWindowFlags_NoScrollbar))
                {
                    ImGui::Dummy(ImVec2(0, 6.0f));
                    ImGui::Indent(cBoxPad);

                    g_field_accent_active = isNew || !!(c.changed_fields & FCF_User);
                    DrawCopyRow("tp_user", ICON_MDI_ACCOUNT, c.user, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing, MaskIfPrivate(c.user));
                    DrawRowDivider(ImGui::GetWindowDrawList(), rowWidth);
                    g_field_accent_active = isNew || !!(c.changed_fields & FCF_Email);
                    DrawEmailRow("tp_email", c.email, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing, MaskIfPrivate(c.email));
                    DrawRowDivider(ImGui::GetWindowDrawList(), rowWidth);
                    g_field_accent_active = isNew || !!(c.changed_fields & FCF_Password);
                    DrawPasswordRow("tp_pw", c.id, get_password_fn, c.password_history, visiblePasswordsThreePane, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing);
                    if (!c.totp_secret.empty())
                    {
                        DrawRowDivider(ImGui::GetWindowDrawList(), rowWidth);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_TotpSecret);
                        DrawTOTPRow("tp_totp", c.totp_secret, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing);
                    }

                    ImGui::Dummy(ImVec2(0, 1.0f));
                    ImGui::Unindent(cBoxPad);
                }
                EndLiftedChild();
            }
        }
        else if (c.type == CredType::CreditCard)
        {
            // --- Card Details container ---
            {
                ImGui::PushFont(render::FontSmall);
                ImGui::TextDisabled("Card Details");
                ImGui::PopFont();

                if (BeginLiftedChild("##tp_card_details", ImVec2(0, 0), ImGuiWindowFlags_NoScrollbar))
                {
                    ImGui::Dummy(ImVec2(0, 6.0f));
                    ImGui::Indent(cBoxPad);

                    g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardNumber);
                    DrawMaskedCopyRow("tp_card", ICON_MDI_CREDIT_CARD, c.card_number, 4, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing);
                    DrawRowDivider(ImGui::GetWindowDrawList(), rowWidth);
                    g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardExpiry);
                    DrawCopyRow("tp_expiry", ICON_MDI_CALENDAR, c.card_expiry, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing);
                    DrawRowDivider(ImGui::GetWindowDrawList(), rowWidth);
                    g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardCvv);
                    DrawMaskedCopyRow("tp_cvv", ICON_MDI_LOCK, c.card_cvv, 0, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing);
                    DrawRowDivider(ImGui::GetWindowDrawList(), rowWidth);
                    g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardBrand);
                    DrawInfoRow("tp_brand", ICON_MDI_TAG, c.card_brand, iconColW, valueColW, rowSpacing);

                    ImGui::Dummy(ImVec2(0, 4.0f));
                    ImGui::Unindent(cBoxPad);
                }
                EndLiftedChild();
            }

            ImGui::Dummy(ImVec2(0, cGap));

            // --- Account container (cardholder) ---
            {
                ImGui::PushFont(render::FontSmall);
                ImGui::TextDisabled("Account");
                ImGui::PopFont();

                if (BeginLiftedChild("##tp_card_account", ImVec2(0, 0), ImGuiWindowFlags_NoScrollbar))
                {
                    ImGui::Dummy(ImVec2(0, 6.0f));
                    ImGui::Indent(cBoxPad);

                    g_field_accent_active = isNew || !!(c.changed_fields & FCF_Cardholder);
                    DrawCopyRow("tp_holder", ICON_MDI_ACCOUNT, c.cardholder_name, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing, MaskIfPrivate(c.cardholder_name));
                    if (!c.card_address.empty())
                    {
                        DrawRowDivider(ImGui::GetWindowDrawList(), rowWidth);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardAddress);
                        DrawCopyRow("tp_card_addr", ICON_MDI_MAP_MARKER, c.card_address, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing, MaskIfPrivate(c.card_address));
                    }
                    if (!c.card_city.empty())
                    {
                        DrawRowDivider(ImGui::GetWindowDrawList(), rowWidth);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardCity);
                        DrawInfoRow("tp_card_city", ICON_MDI_CITY, c.card_city, iconColW, valueColW, rowSpacing);
                    }
                    if (!c.card_postal_code.empty())
                    {
                        DrawRowDivider(ImGui::GetWindowDrawList(), rowWidth);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_CardPostalCode);
                        DrawInfoRow("tp_card_postal", ICON_MDI_MAILBOX, c.card_postal_code, iconColW, valueColW, rowSpacing);
                    }
                    if (!c.address.empty())
                    {
                        DrawRowDivider(ImGui::GetWindowDrawList(), rowWidth);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_Address);
                        DrawCopyRow("tp_addr", ICON_MDI_MAP_MARKER, c.address, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing, MaskIfPrivate(c.address));
                    }

                    ImGui::Dummy(ImVec2(0, 4.0f));
                    ImGui::Unindent(cBoxPad);
                }
                EndLiftedChild();
            }

            ImGui::Dummy(ImVec2(0, cGap));

            // --- About container (website + group + timestamps) ---
            {
                ImGui::PushFont(render::FontSmall);
                ImGui::TextDisabled("About");
                ImGui::PopFont();

                if (BeginLiftedChild("##tp_card_about", ImVec2(0, 0), ImGuiWindowFlags_NoScrollbar))
                {
                    ImGui::Dummy(ImVec2(0, 6.0f));
                    ImGui::Indent(cBoxPad);

                    g_field_accent_active = isNew || !!(c.changed_fields & FCF_Website);
                    DrawInfoRow("tp_website", ICON_MDI_EARTH, c.website, iconColW, valueColW, rowSpacing);
                    if (!c.group.empty())
                    {
                        DrawRowDivider(ImGui::GetWindowDrawList(), rowWidth);
                        g_field_accent_active = isNew || !!(c.changed_fields & FCF_Group);
                        DrawInfoRow("tp_group", ICON_MDI_FOLDER, c.group, iconColW, valueColW, rowSpacing - 5);
                    }
                    g_field_accent_active = false;

                    if (c.created_at_ms != 0 || c.updated_at_ms != 0)
                    {
                        DrawRowDivider(ImGui::GetWindowDrawList(), rowWidth);
                        std::string ts;
                        if (c.created_at_ms != 0) ts += "Created " + FormatUnixMsDateOnly(c.created_at_ms);
                        if (c.updated_at_ms != 0) {
                            if (!ts.empty()) ts += "  \xc2\xb7  ";
                            ts += "Updated " + FormatUnixMsDateOnly(c.updated_at_ms);
                        }
                        ImGui::TextDisabled("%s", ts.c_str());
                    }

                    ImGui::Dummy(ImVec2(0, 4.0f));
                    ImGui::Unindent(cBoxPad);
                }
                EndLiftedChild();
            }
        }
        else if (c.type == CredType::Identity)
        {
            g_field_accent_active = isNew || !!(c.changed_fields & FCF_FullName);
            DrawCopyRow("tp_name", ICON_MDI_ACCOUNT, c.full_name, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing, MaskIfPrivate(c.full_name));
            g_field_accent_active = isNew || !!(c.changed_fields & FCF_IdType);
            DrawInfoRow("tp_idtype", ICON_MDI_CARD_ACCOUNT_DETAILS, c.id_type, iconColW, valueColW, rowSpacing);
            g_field_accent_active = isNew || !!(c.changed_fields & FCF_IdNumber);
            DrawCopyRow("tp_idnum", ICON_MDI_POUND, c.id_number, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing, MaskIfPrivate(c.id_number));
            g_field_accent_active = isNew || !!(c.changed_fields & FCF_DateOfBirth);
            DrawInfoRow("tp_dob", ICON_MDI_CAKE_VARIANT, c.date_of_birth, iconColW, valueColW, rowSpacing);
            g_field_accent_active = isNew || !!(c.changed_fields & FCF_ExpiryDate);
            DrawInfoRow("tp_expiry", ICON_MDI_CALENDAR, c.expiry_date, iconColW, valueColW, rowSpacing);
            g_field_accent_active = isNew || !!(c.changed_fields & FCF_Country);
            DrawInfoRow("tp_country", ICON_MDI_EARTH, c.country, iconColW, valueColW, rowSpacing);
            g_field_accent_active = isNew || !!(c.changed_fields & FCF_Address);
            DrawCopyRow("tp_addr", ICON_MDI_MAP_MARKER, c.address, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing, MaskIfPrivate(c.address));
            g_field_accent_active = isNew || !!(c.changed_fields & FCF_Phone);
            DrawCopyRow("tp_phone", ICON_MDI_PHONE, c.phone, iconColW, valueColW, rowWidth, iconBtnSz, rowSpacing, MaskIfPrivate(c.phone));
        }
        else if (c.type == CredType::SecureNote)
        {
            g_field_accent_active = false;
        }
        g_field_accent_active = false;

        // Group row (types that don't have their own About container)
        if (c.type != CredType::Password && c.type != CredType::CreditCard && !c.group.empty())
        {
            g_field_accent_active = isNew || !!(c.changed_fields & FCF_Group);
            DrawInfoRow("tp_group", ICON_MDI_FOLDER, c.group, iconColW, valueColW, rowSpacing);
            g_field_accent_active = false;
        }

        // Tabbed section: Security Analysis + Notes (below password/security card)
        {
            bool hasNotes = !c.notes.empty();
            bool hasSecurity = (c.type == CredType::Password);

            if (hasNotes || hasSecurity)
            {
                ImGui::Dummy(ImVec2(0, 10));

                // Track active tab per credential
                static std::unordered_map<int, int> s_detail_tab; // 0 = security, 1 = notes
                int& activeTab = s_detail_tab[c.id];

                // Default to security if available, otherwise notes
                if (!hasSecurity && activeTab == 0) activeTab = 1;

                if (!hasNotes && activeTab == 1) activeTab = 0;

                // Draw split-pill tab bar
                {
                    const bool dark = IsDarkTheme();
                    const float tabH = 28.0f;
                    const float rounding = 8.0f;
                    const float pad_x = 10.0f;
                    const float divW = 1.0f;
                    const ImU32 mutedCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);
                    const ImU32 textCol  = ImGui::GetColorU32(ImGuiCol_Text);
                    const ImU32 pillBg   = dark ? theme::PillBg.dark  : theme::PillBg.light;
                    const ImU32 pillHov  = dark ? theme::PillHoverBg.dark  : theme::PillHoverBg.light;
                    const ImU32 lipCol   = dark ? theme::PillInnerLip.dark   : theme::PillInnerLip.light;
                    const ImU32 divCol   = dark ? theme::PillDivider.dark  : theme::PillDivider.light;
                    ImDrawList* dl = ImGui::GetWindowDrawList();

                    // Build tab entries
                    struct TabEntry { const char* id; const char* icon; const char* label; int idx; };
                    TabEntry tabs[2];
                    int numTabs = 0;
                    if (hasSecurity)
                        tabs[numTabs++] = { "##tab_security", ICON_MDI_SHIELD_CHECK, "Security", 0 };
                    if (hasNotes)
                        tabs[numTabs++] = { "##tab_notes", ICON_MDI_NOTE_TEXT, "Notes", 1 };

                    // Measure regions
                    float regionW[2] = {};
                    for (int i = 0; i < numTabs; i++)
                    {
                        char full[128];
                        snprintf(full, sizeof(full), "%s %s", tabs[i].icon, tabs[i].label);
                        regionW[i] = ImGui::CalcTextSize(full).x + pad_x * 2.0f;
                    }

                    float totalW = regionW[0] + (numTabs > 1 ? regionW[1] : 0.0f);
                    ImVec2 pillPos = ImGui::GetCursorScreenPos();

                    // Lip shadow behind pill
                    const float lipOff = 1.5f;
                    dl->AddRectFilled(
                        ImVec2(pillPos.x, pillPos.y + lipOff),
                        ImVec2(pillPos.x + totalW, pillPos.y + tabH + lipOff),
                        lipCol, rounding);

                    // Pill background
                    dl->AddRectFilled(
                        pillPos,
                        ImVec2(pillPos.x + totalW, pillPos.y + tabH),
                        pillBg, rounding);

                    // Draw each region
                    float curX = pillPos.x;
                    for (int i = 0; i < numTabs; i++)
                    {
                        bool active = (activeTab == tabs[i].idx);
                        float w = regionW[i];
                        ImVec2 rMin(curX, pillPos.y);
                        ImVec2 rMax(curX + w, pillPos.y + tabH);

                        // Hit test
                        ImGui::SetCursorScreenPos(rMin);
                        ImGui::InvisibleButton(tabs[i].id, ImVec2(w, tabH));
                        bool hov = ImGui::IsItemHovered();
                        if (ImGui::IsItemClicked())
                            activeTab = tabs[i].idx;

                        // Hover highlight
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

                        // Text
                        {
                            char full[128];
                            snprintf(full, sizeof(full), "%s %s", tabs[i].icon, tabs[i].label);
                            ImVec2 sz = ImGui::CalcTextSize(full);
                            ImU32 col = active ? textCol : (hov ? textCol : mutedCol);
                            dl->AddText(ImVec2(curX + pad_x, pillPos.y + (tabH - sz.y) * 0.5f), col, full);
                        }

                        curX += w;

                        // 3D divider between regions
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

                    // Advance cursor past the pill
                    ImGui::SetCursorScreenPos(ImVec2(pillPos.x, pillPos.y + tabH + 4.0f));
                }

                // Tab content
                if (activeTab == 0 && hasSecurity)
                {
                    DrawSecurityCard(c, s, activeVaultKey, get_password_fn, ImGui::GetContentRegionAvail().x);
                }
                else if (activeTab == 1 && hasNotes)
                {
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
                        float notesW = ImGui::GetContentRegionAvail().x;
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
                        ImGui::InvisibleButton("##notes_reveal_blocker_tp", ImVec2(totalW, lblSz.y));
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
                        {
                            float notesW = ImGui::GetContentRegionAvail().x;
                            ImVec2 fpad(12.0f, 10.0f);
                            float innerW = notesW - fpad.x * 2;
                            ImVec2 tSz = ImGui::CalcTextSize(c.notes.c_str(), nullptr, false, innerW);
                            float notesH = ImMax(tSz.y + fpad.y * 2, 40.0f);
                            std::string notesDisplay = c.notes;
                            InputTextMultilineString("##tp_notes_view", &notesDisplay, ImVec2(notesW, notesH),
                                ImGuiInputTextFlags_ReadOnly);
                        }
                        ImGui::PopStyleVar(3);
                        ImGui::PopStyleColor(2);
                    }
                }

                ImGui::Spacing();
                ImGui::Spacing();
            }
        }

        // Share status row
        DrawShareStatusRow(c, s);

        // Timestamps for non-Password types (Password has them in About container)
        if (c.type != CredType::Password && c.type != CredType::CreditCard && (c.created_at_ms != 0 || c.updated_at_ms != 0))
        {
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();

            std::string ts;
            if (c.created_at_ms != 0)
                ts = "Created " + FormatUnixMsDateOnly(c.created_at_ms);
            if (c.updated_at_ms != 0)
            {
                if (!ts.empty()) ts += "  \xc2\xb7  ";
                ts += "Updated " + FormatUnixMsDateOnly(c.updated_at_ms);
            }
            ImGui::TextDisabled("%s", ts.c_str());
        }

        float dtScrollY = ImGui::GetScrollY();
        ImVec2 dtScrollMin = ImGui::GetWindowPos();
        float dtScrollW = ImGui::GetWindowSize().x;
        ImGui::EndChild(); // ##3p_detail_scroll
        // DrawScrollTopFade(dtScrollMin, dtScrollW, dtScrollY, -2.0f);

        // --- Sticky action bar ---
        //ImGui::Separator();
        ImGui::Spacing();
        {
            const float ibSz = 28.0f;
            const float btnH = 26.0f;
            const float delW = 95.0f;

            if (!read_only)
            {
                if (IconSquareBtn("##ab_pin", c.is_pinned ? ICON_MDI_PIN : ICON_MDI_PIN, c.is_pinned ? "Unpin" : "Pin", ibSz, c.is_pinned, false, ImGui::GetColorU32(colors::MainColor)))
                    out.toggle_pin_id = c.id;
                ImGui::SameLine(0, 4);
                if (IconSquareBtn("##ab_fav", c.is_favorite ? ICON_MDI_HEART : ICON_MDI_HEART, c.is_favorite ? "Unfavorite" : "Favorite", ibSz, c.is_favorite, false, GetFavoriteColor()))
                    out.toggle_fav_id = c.id;
                ImGui::SameLine(0, 4);
            }

            if (IconSquareBtn("##ab_share", ICON_MDI_SHARE_VARIANT, "Share", ibSz))
                out.anon_share_id = c.id;

            if (!read_only)
            {
                ImGui::SameLine(0, 4);
                if (IconSquareBtn("##ab_edit", ICON_MDI_PENCIL, "Edit", ibSz))
                    out.edit_open_id = c.id;

                // Delete — far right
                ImGui::SameLine();
                ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - delW);
                if (HoldToActionButton("##tp_delete", ICON_MDI_DELETE " Delete", 1.0f, ImVec2(delW, btnH), 6.0f))
                    out.delete_id = c.id;
            }
        }

        ImGui::EndChild(); // ##3p_detail
    }

    AccordionListResult RenderThreePaneView(
        const std::vector<AccordionItem>& items,
        uint32_t activeVaultKey,
        GetPasswordFn get_password_fn,
        bool read_only)
    {
        AccordionListResult out{};
        auto& s = *g_shell_ptr;
        const float dt = ImGui::GetIO().DeltaTime;

        // --- Auto-collapse: compute effective collapsed state ---
        const bool sb_base_collapsed = s.three_pane_sidebar_collapsed;
        const bool ml_base_collapsed = s.three_pane_list_collapsed;

        const bool sb_effective = (s.three_pane_sidebar_auto_collapse && sb_base_collapsed)
            ? !g_3p_sidebar_hover_expanded : sb_base_collapsed;
        const bool ml_effective = (s.three_pane_list_auto_collapse && ml_base_collapsed)
            ? !g_3p_list_hover_expanded : ml_base_collapsed;

        s.three_pane_sidebar_collapsed = sb_effective;
        s.three_pane_list_collapsed    = ml_effective;

        // --- Animate pane widths ---
        const float sbTargetW = sb_effective ? 50.0f : 170.0f;
        const float mlTargetW = ml_effective ? 72.0f : 280.0f;

        if (g_3p_sidebar_anim_w < 0) g_3p_sidebar_anim_w = sbTargetW;
        if (g_3p_list_anim_w    < 0) g_3p_list_anim_w    = mlTargetW;

        float step   = k3pAnimSpeed * dt;
        float stepML = step * 1.4f;  // middle pane collapses slightly quicker
        if (g_3p_sidebar_anim_w < sbTargetW)      g_3p_sidebar_anim_w = ImMin(g_3p_sidebar_anim_w + step, sbTargetW);
        else if (g_3p_sidebar_anim_w > sbTargetW) g_3p_sidebar_anim_w = ImMax(g_3p_sidebar_anim_w - step, sbTargetW);
        if (g_3p_list_anim_w < mlTargetW)          g_3p_list_anim_w = ImMin(g_3p_list_anim_w + stepML, mlTargetW);
        else if (g_3p_list_anim_w > mlTargetW)     g_3p_list_anim_w = ImMax(g_3p_list_anim_w - stepML, mlTargetW);

        // --- Render three columns ---
        RenderThreePaneSidebar(s, g_3p_sidebar_anim_w, items);
        ImVec2 sbMin = ImGui::GetItemRectMin();
        ImVec2 sbMax = ImGui::GetItemRectMax();

        ImGui::SameLine(0, 0);
        RenderThreePaneMiddleList(items, s, activeVaultKey, g_3p_list_anim_w);
        ImVec2 mlMin = ImGui::GetItemRectMin();
        ImVec2 mlMax = ImGui::GetItemRectMax();

        ImGui::SameLine(0, 0);
        RenderThreePaneDetailPane(items, s, activeVaultKey, get_password_fn, read_only, out);

        // Vertical divider lines between panes
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            ImU32 lineCol = ImGui::GetColorU32(ImGuiCol_Separator, 0.4f);

            // Between middle list and detail pane
            dl->AddLine(ImVec2(mlMax.x, mlMin.y), ImVec2(mlMax.x, mlMax.y), lineCol, 1.0f);
        }

        // Vertical shadow on left edge of detail pane
        {
            ImVec2 dMin = ImGui::GetItemRectMin();
            ImVec2 dMax = ImGui::GetItemRectMax();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            const float shadowW = 10.0f;
            ImU32 shadowCol = IsDarkTheme() ? IM_COL32(0, 0, 0, 50) : IM_COL32(0, 0, 0, 12);
            ImU32 clear = IM_COL32(0, 0, 0, 0);
            // Left edge shadow
            dl->AddRectFilledMultiColor(
                ImVec2(dMin.x - shadowW, dMin.y),
                ImVec2(dMin.x, dMax.y),
                clear, shadowCol, shadowCol, clear);

            // Bottom edge shadow (rendered inside, fading upward)
            dl->AddRectFilledMultiColor(
                ImVec2(dMin.x, dMax.y - shadowW),
                ImVec2(dMax.x, dMax.y),
                clear, clear, shadowCol, shadowCol);
        }


        // --- Restore base collapsed state (only if toggle wasn't clicked) ---
        if (s.three_pane_sidebar_collapsed == sb_effective)
            s.three_pane_sidebar_collapsed = sb_base_collapsed;
        if (s.three_pane_list_collapsed == ml_effective)
            s.three_pane_list_collapsed = ml_base_collapsed;

        // --- Hover detection for auto-collapse ---
        const bool anyPopupOpen = ImGui::IsPopupOpen("", ImGuiPopupFlags_AnyPopupId);
        const ImVec2 mouse = ImGui::GetIO().MousePos;

        // Sidebar hover
        if (s.three_pane_sidebar_auto_collapse && s.three_pane_sidebar_collapsed)
        {
            bool hovered = (mouse.x >= sbMin.x && mouse.x <= sbMax.x &&
                            mouse.y >= sbMin.y && mouse.y <= sbMax.y);
            if (hovered)
            {
                g_3p_sidebar_hover_expanded = true;
                g_3p_sidebar_leave_timer = 0.0f;
            }
            else if (g_3p_sidebar_hover_expanded && !anyPopupOpen)
            {
                g_3p_sidebar_leave_timer += dt;
                if (g_3p_sidebar_leave_timer >= k3pCollapseDelay)
                {
                    g_3p_sidebar_hover_expanded = false;
                    g_3p_sidebar_leave_timer = 0.0f;
                }
            }
        }
        else
        {
            g_3p_sidebar_hover_expanded = false;
            g_3p_sidebar_leave_timer = 0.0f;
        }

        // Middle list hover
        if (s.three_pane_list_auto_collapse && s.three_pane_list_collapsed)
        {
            bool hovered = (mouse.x >= mlMin.x && mouse.x <= mlMax.x &&
                            mouse.y >= mlMin.y && mouse.y <= mlMax.y);
            if (hovered)
            {
                g_3p_list_hover_expanded = true;
                g_3p_list_leave_timer = 0.0f;
            }
            else if (g_3p_list_hover_expanded && !anyPopupOpen)
            {
                g_3p_list_leave_timer += dt;
                if (g_3p_list_leave_timer >= k3pCollapseDelay)
                {
                    g_3p_list_hover_expanded = false;
                    g_3p_list_leave_timer = 0.0f;
                }
            }
        }
        else
        {
            g_3p_list_hover_expanded = false;
            g_3p_list_leave_timer = 0.0f;
        }

        // Validate selection still exists in current filtered list
        if (s.three_pane_selected_id != -1)
        {
            bool found = false;
            for (const auto& it : items)
                if (!it.is_header && it.id == s.three_pane_selected_id) { found = true; break; }
            if (!found) s.three_pane_selected_id = -1;
        }

        return out;
    }

} // namespace ui
