// ui_views_table.cpp
// Table view: spreadsheet-style credential list
#include "ui_internal.h"
#include "tools/totp.h"

namespace ui
{
    AccordionListResult RenderTableView(
        const std::vector<AccordionItem>& items,
        uint32_t activeVaultKey,
        GetPasswordFn get_password_fn,
        bool read_only)
    {
        AccordionListResult out{};
        auto& s = *g_shell_ptr;

        static std::vector<int> sortedIdx;
        static int              lastItemCount = -1;
        bool needsSort = false;

        {
            int totalCount = (int)items.size();

            bool rebuild = false;
            if (totalCount != lastItemCount)
                rebuild = true;
            else if (!sortedIdx.empty() && sortedIdx.back() >= totalCount)
                rebuild = true;

            if (rebuild)
            {
                sortedIdx.clear();
                for (int i = 0; i < totalCount; i++)
                    sortedIdx.push_back(i);
                lastItemCount = totalCount;
                needsSort = true;
            }
        }

        if (sortedIdx.empty())
        {
            ImGui::TextDisabled("No items match this view.");
            return out;
        }

        enum TableColID {
            Col_Checkbox = 0, Col_Index, Col_Type, Col_Title, Col_Username,
            Col_Email, Col_Website, Col_Group, Col_Date, Col_PinFav, Col_TOTP
        };

        // cols 3-6 swap their headers/data based on which single type is filtered
        enum TableTypeFilter { TF_Mixed = 0, TF_Passwords, TF_Cards, TF_Identity, TF_Notes };
        TableTypeFilter typeFilter = TF_Mixed;
        {
            bool hasP = s.selected_groups.count("@Passwords") > 0;
            bool hasC = s.selected_groups.count("@Cards") > 0;
            bool hasI = s.selected_groups.count("@Identity") > 0;
            bool hasN = s.selected_groups.count("@Notes") > 0;
            int count = (int)hasP + (int)hasC + (int)hasI + (int)hasN;
            if (count == 1)
            {
                if (hasP) typeFilter = TF_Passwords;
                else if (hasC) typeFilter = TF_Cards;
                else if (hasI) typeFilter = TF_Identity;
                else if (hasN) typeFilter = TF_Notes;
            }
        }

        const char* col3Hdr = "Username";
        const char* col4Hdr = "Email";
        const char* col5Hdr = "Website";
        const char* col6Hdr = "Group";
        switch (typeFilter)
        {
        case TF_Cards:    col3Hdr = "Cardholder"; col4Hdr = "Card Number"; col5Hdr = "Expiry"; col6Hdr = "Brand"; break;
        case TF_Identity: col3Hdr = "Full Name";  col4Hdr = "ID Number";   col5Hdr = "Phone";  col6Hdr = "Country"; break;
        case TF_Notes:    col3Hdr = "Notes";      col4Hdr = "";            col5Hdr = "";       col6Hdr = ""; break;
        default: break;
        }

        const ImGuiTableFlags tableFlags =
            ImGuiTableFlags_Resizable |
            ImGuiTableFlags_Reorderable | ImGuiTableFlags_Hideable |
            ImGuiTableFlags_ScrollY | ImGuiTableFlags_RowBg |
            ImGuiTableFlags_SizingStretchProp;

        const float avail_w = ImGui::GetContentRegionAvail().x;
        const float avail_h = ImGui::GetContentRegionAvail().y;
        const bool dark = IsDarkTheme();

        // Soften table visuals
        ImGui::PushStyleColor(ImGuiCol_TableBorderLight, IM_COL32(0, 0, 0, 0));   // hide row dividers (we draw our own)
        ImGui::PushStyleColor(ImGuiCol_TableBorderStrong, IM_COL32(0, 0, 0, 0));  // hide outer border
        ImGui::PushStyleColor(ImGuiCol_TableRowBg,    dark ? IM_COL32(0, 0, 0, 0) : IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, dark ? IM_COL32(255, 255, 255, 6) : IM_COL32(0, 0, 0, 8));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 8));  // right-click popup padding
        if (!ImGui::BeginTable("##cred_table", 10, tableFlags, ImVec2(avail_w, avail_h)))
        {
            ImGui::PopStyleVar();  // WindowPadding
            ImGui::PopStyleColor(4);
            return out;
        }

        ImGui::TableSetupColumn("###cb", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoReorder | ImGuiTableColumnFlags_NoHeaderLabel, 28.0f, Col_Checkbox);
        ImGui::TableSetupColumn("Icon",     ImGuiTableColumnFlags_WidthFixed, 30.0f, Col_Type);
        ImGui::TableSetupColumn("Title",    ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultSort, 3.0f, Col_Title);
        ImGui::TableSetupColumn(col3Hdr, ImGuiTableColumnFlags_WidthStretch, 2.0f, Col_Username);
        ImGui::TableSetupColumn(col4Hdr, ImGuiTableColumnFlags_WidthStretch | (typeFilter == TF_Notes ? ImGuiTableColumnFlags_Disabled : 0), 2.5f, Col_Email);
        ImGui::TableSetupColumn(col5Hdr, ImGuiTableColumnFlags_WidthStretch | (typeFilter == TF_Notes ? ImGuiTableColumnFlags_Disabled : 0), 2.0f, Col_Website);
        ImGui::TableSetupColumn(col6Hdr, ImGuiTableColumnFlags_WidthStretch | ImGuiTableColumnFlags_DefaultHide | (typeFilter == TF_Notes ? ImGuiTableColumnFlags_Disabled : 0), 1.5f, Col_Group);
        ImGui::TableSetupColumn("Modified", ImGuiTableColumnFlags_WidthFixed, 90.0f, Col_Date);
        ImGui::TableSetupColumn("#",   ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_NoResize, 36.0f, Col_Index);
        // Live TOTP code; off by default — toggle via the column-toggle menu
        // in the header. Click the cell to copy the current code.
        ImGui::TableSetupColumn("TOTP", ImGuiTableColumnFlags_WidthFixed | ImGuiTableColumnFlags_NoSort | ImGuiTableColumnFlags_DefaultHide, 110.0f, Col_TOTP);
        ImGui::TableSetupScrollFreeze(0, 1);

        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(4, 4));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(6, 2));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, dark ? theme::PopupBg.dark : theme::PopupBg.light);
        ImGui::PushStyleColor(ImGuiCol_Border,  dark ? theme::PopupBorder.dark : theme::PopupBorder.light);

        // we draw header text manually for tight height control
        ImGui::PushStyleColor(ImGuiCol_TableHeaderBg, dark ? IM_COL32(255, 255, 255, 6) : IM_COL32(0, 0, 0, 4));
        ImGui::TableHeadersRow();  // register headers for sorting (required by ImGui)
        ImGui::PopStyleColor();

        // Draw custom header overlay on top
        {
            ImDrawList* hdl = ImGui::GetWindowDrawList();
            // Bottom separator
            float hdrY = ImGui::GetCursorScreenPos().y;
            float hdrX0 = ImGui::GetWindowPos().x;
            float hdrX1 = hdrX0 + ImGui::GetWindowWidth();
            float lineY = IM_ROUND(hdrY + 6.0f);
            hdl->AddLine(ImVec2(hdrX0, lineY), ImVec2(hdrX1, lineY),
                dark ? IM_COL32(0, 0, 0, 60) : IM_COL32(0, 0, 0, 20), 1.0f);
        }



        // Sorting
        if (ImGuiTableSortSpecs* sortSpecs = ImGui::TableGetSortSpecs())
        {
            if (sortSpecs->SpecsDirty || needsSort)
            {
                // sort data items within each group, preserving header positions
                sortedIdx.clear();
                for (int i = 0; i < (int)items.size(); i++)
                    sortedIdx.push_back(i);

                if (sortSpecs->SpecsCount > 0)
                {
                    const ImGuiTableColumnSortSpecs& spec = sortSpecs->Specs[0];
                    const bool asc = (spec.SortDirection == ImGuiSortDirection_Ascending);

                    int groupStart = 0;
                    for (int i = 0; i <= (int)sortedIdx.size(); i++)
                    {
                        bool atEnd = (i == (int)sortedIdx.size());
                        bool isHdr = !atEnd && items[sortedIdx[i]].is_header;

                        if (atEnd || isHdr)
                        {
                            if (i > groupStart)
                            {
                                std::sort(sortedIdx.begin() + groupStart, sortedIdx.begin() + i,
                                    [&](int a, int b) -> bool
                                    {
                                        const AccordionItem& ia = items[a];
                                        const AccordionItem& ib = items[b];
                                        if (ia.is_pinned != ib.is_pinned) return ia.is_pinned;
                                        if (ia.is_favorite != ib.is_favorite) return ia.is_favorite;
                                        int cmp = 0;
                                        switch (spec.ColumnUserID)
                                        {
                                        case Col_Type:     cmp = (int)ia.type - (int)ib.type; break;
                                        case Col_Title:    cmp = _stricmp(ia.title.c_str(), ib.title.c_str()); break;
                                        case Col_Username: cmp = _stricmp(ia.user.c_str(), ib.user.c_str()); break;
                                        case Col_Email:    cmp = _stricmp(ia.email.c_str(), ib.email.c_str()); break;
                                        case Col_Website:  cmp = _stricmp(ia.website.c_str(), ib.website.c_str()); break;
                                        case Col_Group:    cmp = _stricmp(ia.group.c_str(), ib.group.c_str()); break;
                                        case Col_Date:
                                            if (ia.updated_at_ms < ib.updated_at_ms) cmp = -1;
                                            else if (ia.updated_at_ms > ib.updated_at_ms) cmp = 1;
                                            break;
                                        default: break;
                                        }
                                        return asc ? (cmp < 0) : (cmp > 0);
                                    });
                            }
                            groupStart = i + 1; // skip the header
                        }
                    }
                }
                sortSpecs->SpecsDirty = false;
            }
        }

        // Deferred popup state (by credential ID, not row index)
        static int s_table_popup_id = -1;
        static int s_tbl_popup_tab = 0;

        // Security highlight colors
        const ImU32 weakTint    = colors::TintWeak;
        const ImU32 reusedTint  = colors::TintReused;
        const ImU32 exposedTint = colors::TintExposed;
        const ImU32 agingTint   = colors::TintAging;
        const ImU32 selTint    = IM_COL32(
            (int)(colors::SecondColor.x * 255),
            (int)(colors::SecondColor.y * 255),
            (int)(colors::SecondColor.z * 255),
            38); // ~15% alpha
        const ImU32 hoverTint  = IsDarkTheme() ? IM_COL32(255, 255, 255, 15) : IM_COL32(0, 0, 0, 15);

        const ImVec2 mp = ImGui::GetMousePos();

        // Taller data rows
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 10));

        // Clipper for virtualized rows (extra padding only when group headers are present)
        // Also precompute credential-only index (excluding header rows) for # column
        bool hasGroupHeaders = false;
        int headerCountSoFar = 0;
        std::vector<int> credIndex(sortedIdx.size());
        for (int i = 0; i < (int)sortedIdx.size(); i++)
        {
            if (items[sortedIdx[i]].is_header)
            {
                hasGroupHeaders = true;
                headerCountSoFar++;
                credIndex[i] = 0; // unused for headers
            }
            else
                credIndex[i] = i - headerCountSoFar + 1;
        }
        int paddingRows = 0;
        if (hasGroupHeaders)
        {
            switch (s.group_mode)
            {
                case ui::GroupMode::Alphabetical:    paddingRows = 4; break;
                case ui::GroupMode::MonthCreated:
                case ui::GroupMode::MonthUpdated:     paddingRows = 5; break;
                case ui::GroupMode::Group:            paddingRows = 5; break;
                case ui::GroupMode::PinnedFavorites:  paddingRows = 5; break;
                default:                             paddingRows = 0; break;
            }
        }
        const int totalRows = (int)sortedIdx.size() + paddingRows;
        ImGuiListClipper clipper;
        clipper.Begin(totalRows);
        while (clipper.Step())
        {
            for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; row++)
            {
                if (row >= (int)sortedIdx.size())
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Dummy(ImVec2(0, 0));
                    continue;
                }

                const AccordionItem& c = items[sortedIdx[row]];

                // Group header row
                if (c.is_header)
                {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::Dummy(ImVec2(0, 2.0f));
                    if (ImGui::TableSetColumnIndex(2))
                    {
                        ImGui::PushFont(render::FontSmall);
                        ImGui::TextDisabled("%s", c.header_label.c_str());
                        ImGui::PopFont();
                    }
                    continue;
                }

                const uint64_t rowKey = MakeRowKey(activeVaultKey, c.id);
                const bool selected = IsSelected(rowKey);

                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                float rowStartY = ImGui::GetCursorScreenPos().y;

                if (c.is_changed)
                    ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0,
                        (c.changed_fields & FCF_IsNew) ? kNewRowTint : kChangedRowTint);

                ImGui::PushID((int)rowKey);

                {
                    ImVec2 pos = ImGui::GetCursorScreenPos();
                    float offsetY = (ImGui::GetTextLineHeightWithSpacing() - 15.0f) * 0.5f;
                    ImGui::SetCursorScreenPos(ImVec2(pos.x + 4.0f, pos.y + offsetY));
                    bool cbVal = selected;
                    if (ImGui::Checkbox2("", &cbVal))
                        ToggleSelected(rowKey);
                }

                if (ImGui::TableSetColumnIndex(1))
                {
                    auto srv = favicon::Get(c.website);
                    if (srv)
                    {
                        const float icoSz = 22.0f;
                        ImVec2 pos = ImGui::GetCursorScreenPos();
                        float offsetY = (ImGui::GetTextLineHeightWithSpacing() - icoSz) * 0.5f;
                        ImGui::GetWindowDrawList()->AddImageRounded(
                            (ImTextureID)srv,
                            ImVec2(pos.x, pos.y + offsetY),
                            ImVec2(pos.x + icoSz, pos.y + offsetY + icoSz),
                            ImVec2(0, 0), ImVec2(1, 1),
                            IM_COL32(255, 255, 255, 255), 4.0f);
                        ImGui::Dummy(ImVec2(icoSz, icoSz));
                    }
                    else
                        ImGui::TextUnformatted(CredTypeIcon(c.type));
                    if (ImGui::IsItemHovered())
                        SetTooltipPadded("%s", CredTypeLabel(c.type));
                }

                if (ImGui::TableSetColumnIndex(2))
                {
                    float colW = ImGui::GetContentRegionAvail().x;
                    ImVec2 cellPos = ImGui::GetCursorScreenPos();
                    float lineH = ImGui::GetTextLineHeight();
                    const float icoGap = 2.0f;
                    ImDrawList* tdl = ImGui::GetWindowDrawList();

                    float pinFavW = 0.0f;
                    if (c.is_pinned)   pinFavW += ImGui::CalcTextSize(ICON_MDI_PIN).x + icoGap;
                    if (c.is_favorite) pinFavW += ImGui::CalcTextSize(ICON_MDI_HEART).x + icoGap;

                    bool isWeak    = s.sec_highlight_weak    && s.sec_weak_ids.count(c.id);
                    bool isReused  = s.sec_highlight_reused  && s.sec_reused_ids.count(c.id);
                    bool isExposed = s.sec_highlight_exposed && s.sec_exposed_ids.count(c.id);
                    bool isAging   = s.sec_highlight_aging   && s.sec_aging_ids.count(c.id);
                    float secW = 0.0f;
                    if (isWeak)    secW += ImGui::CalcTextSize(ICON_MDI_ALERT).x + icoGap;
                    if (isReused)  secW += ImGui::CalcTextSize(ICON_MDI_REPEAT).x + icoGap;
                    if (isExposed) secW += ImGui::CalcTextSize(ICON_MDI_EARTH).x + icoGap;
                    if (isAging)   secW += ImGui::CalcTextSize(ICON_MDI_CLOCK_ALERT).x + icoGap;

                    float maxIconsW = ImMax(pinFavW, secW);
                    float titleW = maxIconsW > 0 ? colW - maxIconsW - 4.0f : colW;
                    ImGui::PushFont(render::FontBold);
                    TextEllipsisClipped(c.title.c_str(), titleW);
                    ImGui::PopFont();

                    if (pinFavW > 0)
                    {
                        float iconX = cellPos.x + colW - pinFavW;
                        float iconY = cellPos.y;
                        if (c.is_pinned) {
                            tdl->AddText(ImVec2(iconX, iconY), ImGui::GetColorU32(colors::MainColor), ICON_MDI_PIN);
                            iconX += ImGui::CalcTextSize(ICON_MDI_PIN).x + icoGap;
                        }
                        if (c.is_favorite) {
                            tdl->AddText(ImVec2(iconX, iconY), GetFavoriteColor(), ICON_MDI_HEART);
                        }
                    }

                    if (secW > 0)
                    {
                        float iconX = cellPos.x + colW - secW;
                        float iconY = cellPos.y + lineH + 1.0f;
                        if (isWeak) {
                            tdl->AddText(ImVec2(iconX, iconY), colors::StatusWeak, ICON_MDI_ALERT);
                            iconX += ImGui::CalcTextSize(ICON_MDI_ALERT).x + icoGap;
                        }
                        if (isReused) {
                            tdl->AddText(ImVec2(iconX, iconY), colors::StatusReused, ICON_MDI_REPEAT);
                            iconX += ImGui::CalcTextSize(ICON_MDI_REPEAT).x + icoGap;
                        }
                        if (isExposed) {
                            tdl->AddText(ImVec2(iconX, iconY), colors::StatusExposed, ICON_MDI_EARTH);
                            iconX += ImGui::CalcTextSize(ICON_MDI_EARTH).x + icoGap;
                        }
                        if (isAging) {
                            tdl->AddText(ImVec2(iconX, iconY), colors::StatusAging, ICON_MDI_CLOCK_ALERT);
                        }
                    }
                }

                if (typeFilter == TF_Cards)
                {
                    if (ImGui::TableSetColumnIndex(3))
                    {
                        float colW = ImGui::GetContentRegionAvail().x;
                        const char* d = MaskIfPrivate(c.cardholder_name) ? MaskIfPrivate(c.cardholder_name) : c.cardholder_name.c_str();
                        TextEllipsisClipped(d, colW);
                    }
                    if (ImGui::TableSetColumnIndex(4))
                    {
                        float colW = ImGui::GetContentRegionAvail().x;
                        std::string masked = c.card_number.size() >= 4
                            ? std::string("\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2\xe2\x80\xa2 ") + c.card_number.substr(c.card_number.size() - 4)
                            : c.card_number;
                        TextEllipsisClipped(masked.c_str(), colW);
                    }
                    if (ImGui::TableSetColumnIndex(5))
                    {
                        float colW = ImGui::GetContentRegionAvail().x;
                        TextEllipsisClipped(c.card_expiry.c_str(), colW);
                    }
                    if (ImGui::TableSetColumnIndex(6))
                    {
                        float colW = ImGui::GetContentRegionAvail().x;
                        TextEllipsisClipped(c.card_brand.c_str(), colW);
                    }
                }
                else if (typeFilter == TF_Identity)
                {
                    if (ImGui::TableSetColumnIndex(3))
                    {
                        float colW = ImGui::GetContentRegionAvail().x;
                        const char* d = MaskIfPrivate(c.full_name) ? MaskIfPrivate(c.full_name) : c.full_name.c_str();
                        TextEllipsisClipped(d, colW);
                    }
                    if (ImGui::TableSetColumnIndex(4))
                    {
                        float colW = ImGui::GetContentRegionAvail().x;
                        const char* d = MaskIfPrivate(c.id_number) ? MaskIfPrivate(c.id_number) : c.id_number.c_str();
                        TextEllipsisClipped(d, colW);
                    }
                    if (ImGui::TableSetColumnIndex(5))
                    {
                        float colW = ImGui::GetContentRegionAvail().x;
                        const char* d = MaskIfPrivate(c.phone) ? MaskIfPrivate(c.phone) : c.phone.c_str();
                        TextEllipsisClipped(d, colW);
                    }
                    if (ImGui::TableSetColumnIndex(6))
                    {
                        float colW = ImGui::GetContentRegionAvail().x;
                        TextEllipsisClipped(c.country.c_str(), colW);
                    }
                }
                else if (typeFilter == TF_Notes)
                {
                    if (ImGui::TableSetColumnIndex(3))
                    {
                        float colW = ImGui::GetContentRegionAvail().x;
                        std::string preview = c.notes.substr(0, 80);
                        TextEllipsisClipped(preview.c_str(), colW);
                    }
                }
                else
                {
                    // Mixed / Passwords — default columns
                    if (ImGui::TableSetColumnIndex(3))
                    {
                        float colW = ImGui::GetContentRegionAvail().x;
                        const char* userDisp = MaskIfPrivate(c.user) ? MaskIfPrivate(c.user) : c.user.c_str();
                        TextEllipsisClipped(userDisp, colW);
                    }
                    if (ImGui::TableSetColumnIndex(4))
                    {
                        float colW = ImGui::GetContentRegionAvail().x;
                        const char* emailDisp = MaskIfPrivate(c.email) ? MaskIfPrivate(c.email) : c.email.c_str();
                        TextEllipsisClipped(emailDisp, colW);
                    }
                    if (ImGui::TableSetColumnIndex(5))
                    {
                        float colW = ImGui::GetContentRegionAvail().x;
                        const char* webDisp = c.website.c_str();
                        if (strncmp(webDisp, "https://", 8) == 0) webDisp += 8;
                        else if (strncmp(webDisp, "http://", 7) == 0) webDisp += 7;
                        TextEllipsisClipped(webDisp, colW);
                    }
                    if (ImGui::TableSetColumnIndex(6))
                    {
                        float colW = ImGui::GetContentRegionAvail().x;
                        TextEllipsisClipped(c.group.c_str(), colW);
                    }
                }

                // Col 7: Modified date
                if (ImGui::TableSetColumnIndex(7))
                {
                    std::string dateStr = FormatUnixMsDateOnly(c.updated_at_ms);
                    ImGui::TextUnformatted(dateStr.c_str());
                }

                // Col 8: Index number (credential-only, excludes group headers)
                if (ImGui::TableSetColumnIndex(8))
                {
                    ImGui::TextDisabled("%d", credIndex[row]);
                }

                // Col 9: Live TOTP code (hidden by default). Click to copy.
                if (ImGui::TableSetColumnIndex(9))
                {
                    if (!c.totp_secret.empty()) {
                        auto totp_bytes = totp::base32_decode(c.totp_secret);
                        if (totp_bytes.size() >= 10) {
                            std::string code = totp::generate_code_now(totp_bytes);
                            int secs = totp::seconds_remaining_now();
                            std::string disp = (code.size() == 6 ? code.substr(0,3) + " " + code.substr(3) : code);
                            ImVec4 col = (secs > 5) ? colors::Green : colors::Red;
                            ImGui::TextColored(col, "%s  %ds", disp.c_str(), secs);
                            if (ImGui::IsItemClicked(0)) {
                                ImGui::SetClipboardText(code.c_str());
                                ShowToast("TOTP copied", ToastType::Success);
                            }
                            if (ImGui::IsItemHovered())
                                ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                        }
                    }
                }

                ImGui::PopID();

                // cell bg rect gives accurate row bounds for hover detection
                ImRect cellRect = ImGui::TableGetCellBgRect(ImGui::GetCurrentTable(), 0);
                float rowStartYActual = cellRect.Min.y;
                float rowEndY = cellRect.Max.y;
                rowStartY = rowStartYActual;  // update for hover detection
                float rowMinX = ImGui::GetWindowPos().x;
                float rowMaxX = rowMinX + ImGui::GetWindowWidth();
                bool rowHovered = (mp.y >= rowStartY && mp.y < rowEndY &&
                                   mp.x >= rowMinX && mp.x < rowMaxX &&
                                   ImGui::IsWindowHovered(ImGuiHoveredFlags_None));

                {
                    ImDrawList* rdl = ImGui::GetWindowDrawList();

                    if (selected)
                    {
                        ImU32 fillCol = dark ? IM_COL32(255, 255, 255, 25) : IM_COL32(0, 0, 0, 15);
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, fillCol);
                        rdl->AddLine(ImVec2(rowMinX, rowStartY), ImVec2(rowMaxX, rowStartY),
                            dark ? IM_COL32(255, 255, 255, 22) : IM_COL32(255, 255, 255, 255), 1.0f);
                        rdl->AddLine(ImVec2(rowMinX, rowEndY - 1.0f), ImVec2(rowMaxX, rowEndY - 1.0f),
                            dark ? IM_COL32(0, 0, 0, 60) : IM_COL32(0, 0, 0, 35), 1.0f);
                    }
                    else if (rowHovered)
                    {
                        ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, hoverTint);
                        rdl->AddLine(ImVec2(rowMinX, rowStartY), ImVec2(rowMaxX, rowStartY),
                            dark ? IM_COL32(255, 255, 255, 14) : IM_COL32(255, 255, 255, 220), 1.0f);
                        rdl->AddLine(ImVec2(rowMinX, rowEndY - 1.0f), ImVec2(rowMaxX, rowEndY - 1.0f),
                            dark ? IM_COL32(0, 0, 0, 35) : IM_COL32(0, 0, 0, 20), 1.0f);
                    }
                    else
                    {
                        rdl->AddLine(ImVec2(rowMinX, rowStartY), ImVec2(rowMaxX, rowStartY),
                            dark ? IM_COL32(255, 255, 255, 8) : IM_COL32(255, 255, 255, 160), 1.0f);
                        rdl->AddLine(ImVec2(rowMinX, rowEndY - 1.0f), ImVec2(rowMaxX, rowEndY - 1.0f),
                            dark ? IM_COL32(0, 0, 0, 20) : IM_COL32(0, 0, 0, 14), 1.0f);
                    }
                }

                // Double-click to edit
                if (rowHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                    out.edit_open_id = c.id;

                if (rowHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                    !ImGui::IsPopupOpen("##tbl_col_hdr_popup"))
                {
                    s_table_popup_id = items[sortedIdx[row]].id;
                    s_tbl_popup_tab = 0;
                    ImGui::OpenPopup("##tbl_row_popup");
                }
            }
        }

        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding,  8.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(12, 10));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,  ImVec2(8, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,   ImVec2(8, 6));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IsDarkTheme() ? theme::PopupBg.dark : theme::PopupBg.light);
        ImGui::PushStyleColor(ImGuiCol_Border,  IsDarkTheme() ? theme::PopupBorder.dark : theme::PopupBorder.light);
        const AccordionItem* popup_item = nullptr;
        if (s_table_popup_id >= 0)
        {
            for (const auto& it : items)
                if (!it.is_header && it.id == s_table_popup_id) { popup_item = &it; break; }
        }
        if (popup_item && ImGui::BeginPopup("##tbl_row_popup"))
        {
            PopupStyleBegin();
            ImGui::PushItemFlag(ImGuiItemFlags_AutoClosePopups, false);
            const AccordionItem& c = *popup_item;

            bool hasSecurityTab = (c.type == CredType::Password);
            DrawPopupTabBar(c.id, hasSecurityTab, s_tbl_popup_tab);
            if (s_tbl_popup_tab == 1) {
                float secW = ImGui::CalcTextSize(ICON_MDI_CLOSE " Contains dictionary word").x + 16.0f;
                ImGui::Dummy(ImVec2(secW, 0));
            }

            if (s_tbl_popup_tab == 0)
            {
                ImGui::PushFont(render::FontBold);
                ImGui::TextUnformatted(c.title.empty() ? "(untitled)" : c.title.c_str());
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0, 4));

                {
                    const float ibSz = 28.0f;
                    const float ibGap = 4.0f;
                    bool hasWebsite = !c.website.empty();
                    std::string webmail = GetWebmailUrl(c.email);

                    if (IconSquareBtn("##ctx_pin", c.is_pinned ? ICON_MDI_PIN : ICON_MDI_PIN,
                        c.is_pinned ? "Unpin" : "Pin", ibSz, c.is_pinned, false, ImGui::GetColorU32(colors::MainColor)))
                        out.toggle_pin_id = c.id;
                    ImGui::SameLine(0, ibGap);
                    if (IconSquareBtn("##ctx_fav", c.is_favorite ? ICON_MDI_HEART : ICON_MDI_HEART,
                        c.is_favorite ? "Unfavorite" : "Favorite", ibSz, c.is_favorite, false, GetFavoriteColor()))
                        out.toggle_fav_id = c.id;
                    ImGui::SameLine(0, ibGap);
                    if (!hasWebsite) ImGui::BeginDisabled();
                    if (IconSquareBtn("##ctx_launch", ICON_MDI_EARTH, "Open Website", ibSz))
                        helpers::open_website(c.website);
                    if (!hasWebsite) ImGui::EndDisabled();
                    ImGui::SameLine(0, ibGap);
                    if (webmail.empty()) ImGui::BeginDisabled();
                    if (IconSquareBtn("##ctx_inbox", ICON_MDI_EMAIL, "Open Inbox", ibSz))
                        helpers::open_website(webmail);
                    if (webmail.empty()) ImGui::EndDisabled();
                }

                ImGui::Dummy(ImVec2(0, 2));
                ImGui::Separator();

                static std::unordered_map<uint64_t, float> s_copied_timers;
                const float copiedDuration = 1.5f;
                float now = (float)ImGui::GetTime();
                // Hash key + credential id so timers are per-row
                auto makeKey = [&](const char* field) -> uint64_t {
                    return (uint64_t)c.id * 31 + (uint64_t)ImHashStr(field);
                };
                auto showCopied = [&](const char* key) -> bool {
                    auto it2 = s_copied_timers.find(makeKey(key));
                    return it2 != s_copied_timers.end() && (now - it2->second) < copiedDuration;
                };
                auto markCopied = [&](const char* key) { s_copied_timers[makeKey(key)] = now; };

                bool hasUser = !c.user.empty();
                if (BarMenuItem(showCopied("user") ? ICON_MDI_CHECK "   Copied!##user" : ICON_MDI_ACCOUNT "   Copy Username", false, hasUser))
                { ImGui::SetClipboardText(c.user.c_str()); markCopied("user"); }

                bool hasEmail = !c.email.empty();
                if (BarMenuItem(showCopied("email") ? ICON_MDI_CHECK "   Copied!##email" : ICON_MDI_EMAIL "   Copy Email", false, hasEmail))
                { ImGui::SetClipboardText(c.email.c_str()); markCopied("email"); }

                bool hasPw = (get_password_fn != nullptr);
                const char* pw = hasPw ? get_password_fn(c.id) : nullptr;
                hasPw = (pw && pw[0] != '\0');
                if (BarMenuItem(showCopied("pw") ? ICON_MDI_CHECK "   Copied!##pw" : ICON_MDI_KEY "   Copy Password", false, hasPw))
                { ClipboardCopyPassword(pw); markCopied("pw"); }

                bool hasTotp = !c.totp_secret.empty();
                std::string totp_code;
                bool totpValid = false;
                if (hasTotp) {
                    auto totp_bytes = totp::base32_decode(c.totp_secret);
                    if (totp_bytes.size() >= 10) {
                        totp_code = totp::generate_code_now(totp_bytes);
                        totpValid = true;
                    }
                }

                std::string totp_label;
                if (showCopied("totp")) {
                    totp_label = ICON_MDI_CHECK "   Copied!##totp";
                } else if (totpValid) {
                    // Show the live code inline. Click copies it.
                    std::string disp = (totp_code.size() == 6)
                        ? (totp_code.substr(0, 3) + " " + totp_code.substr(3))
                        : totp_code;
                    int secs = totp::seconds_remaining_now();
                    char buf[64];
                    snprintf(buf, sizeof(buf), ICON_MDI_CLOCK "   %s   (%ds)##totp", disp.c_str(), secs);
                    totp_label = buf;
                } else {
                    totp_label = ICON_MDI_CLOCK "   Copy TOTP##totp";
                }

                if (BarMenuItem(totp_label.c_str(), false, hasTotp && totpValid))
                {
                    ImGui::SetClipboardText(totp_code.c_str());
                    markCopied("totp");
                }

                if (c.type == CredType::CreditCard) {
                    bool hasCard = !c.card_number.empty();
                    if (BarMenuItem(showCopied("card") ? ICON_MDI_CHECK "   Copied!##card" : ICON_MDI_CREDIT_CARD "   Copy Card Number", false, hasCard))
                    { ImGui::SetClipboardText(c.card_number.c_str()); markCopied("card"); }
                    bool hasCvv = !c.card_cvv.empty();
                    if (BarMenuItem(showCopied("cvv") ? ICON_MDI_CHECK "   Copied!##cvv" : ICON_MDI_LOCK "   Copy CVV", false, hasCvv))
                    { ImGui::SetClipboardText(c.card_cvv.c_str()); markCopied("cvv"); }
                }
                if (c.type == CredType::Identity) {
                    bool hasName = !c.full_name.empty();
                    if (BarMenuItem(showCopied("name") ? ICON_MDI_CHECK "   Copied!##name" : ICON_MDI_ACCOUNT "   Copy Full Name", false, hasName))
                    { ImGui::SetClipboardText(c.full_name.c_str()); markCopied("name"); }
                    bool hasPhone = !c.phone.empty();
                    if (BarMenuItem(showCopied("phone") ? ICON_MDI_CHECK "   Copied!##phone" : ICON_MDI_PHONE "   Copy Phone", false, hasPhone))
                    { ImGui::SetClipboardText(c.phone.c_str()); markCopied("phone"); }
                }

                ImGui::Separator();
                if (BarMenuItem(ICON_MDI_PENCIL "   Edit Credential", false, !read_only))
                    out.edit_open_id = c.id;
                ImGui::Separator();
                if (BarMenuItem(ICON_MDI_DELETE "   Delete Credential", false, !read_only))
                    out.delete_id = c.id;
                if (ImGui::IsItemHovered()) {
                    ImVec2 itemMin = ImGui::GetItemRectMin(); ImVec2 itemMax = ImGui::GetItemRectMax();
                    float winX = ImGui::GetWindowPos().x; float winW = ImGui::GetWindowSize().x;
                    ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(winX, itemMin.y), ImVec2(winX + winW, itemMax.y), colors::DeleteTint, 4.0f);
                }
            }
            else if (s_tbl_popup_tab == 1)
            {
                const uint64_t noteRowKey = MakeRowKey(activeVaultKey, c.id);
                const bool noteLocked = cfg::get_reprompt_reveal_notes() && !g_notes_visible.count(noteRowKey);
                const bool dk = IsDarkTheme();

                if (!c.notes.empty() && !noteLocked) {
                    static std::unordered_map<std::string, float> s_copied_timers2;
                    float now2 = (float)ImGui::GetTime();
                    bool justCopied = s_copied_timers2.count("tbl_notes") && (now2 - s_copied_timers2["tbl_notes"]) < 1.5f;
                    if (BarMenuItem(justCopied ? ICON_MDI_CHECK "   Copied!" : ICON_MDI_CONTENT_COPY "   Copy Notes", false))
                    { ImGui::SetClipboardText(c.notes.c_str()); s_copied_timers2["tbl_notes"] = now2; TriggerCopyFlash(); }
                    ImGui::Dummy(ImVec2(0, 2));
                }

                if (c.notes.empty()) {
                    ImGui::TextDisabled("No notes.");
                } else if (noteLocked) {
                    float previewW = ImGui::GetContentRegionAvail().x;
                    float frameH = ImGui::GetTextLineHeight() + 16.0f;
                    ImVec2 fpos = ImGui::GetCursorScreenPos();
                    ImDrawList* pdl = ImGui::GetWindowDrawList();
                    pdl->AddRectFilled(fpos, ImVec2(fpos.x + previewW, fpos.y + frameH), dk ? IM_COL32(255,255,255,4) : IM_COL32(0,0,0,4), 6.0f);
                    pdl->AddRect(fpos, ImVec2(fpos.x + previewW, fpos.y + frameH), dk ? IM_COL32(255,255,255,20) : IM_COL32(0,0,0,20), 6.0f);
                    const char* ico = ICON_MDI_LOCK; const char* label = "Notes hidden";
                    ImVec2 icoSz = ImGui::CalcTextSize(ico); ImVec2 lblSz = ImGui::CalcTextSize(label);
                    float totalW = icoSz.x + 4.0f + lblSz.x;
                    pdl->AddText(ImVec2(fpos.x + (previewW - totalW) * 0.5f, fpos.y + (frameH - lblSz.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), ico);
                    pdl->AddText(ImVec2(fpos.x + (previewW - totalW) * 0.5f + icoSz.x + 4.0f, fpos.y + (frameH - lblSz.y) * 0.5f), ImGui::GetColorU32(ImGuiCol_TextDisabled), label);
                    ImGui::Dummy(ImVec2(previewW, frameH));
                } else {
                    float noteW = ImGui::GetContentRegionAvail().x;
                    float maxH = ImGui::GetTextLineHeight() * 6.0f + 12.0f;
                    ImVec2 fpad(8.0f, 6.0f);
                    float innerW = noteW - fpad.x * 2;
                    ImVec2 tSz = ImGui::CalcTextSize(c.notes.c_str(), nullptr, false, innerW);
                    float notesH = ImMin(ImMax(tSz.y + fpad.y * 2, 28.0f), maxH);
                    ImGui::PushStyleColor(ImGuiCol_FrameBg, dk ? IM_COL32(255,255,255,4) : IM_COL32(0,0,0,4));
                    ImGui::PushStyleColor(ImGuiCol_Border,  dk ? IM_COL32(255,255,255,20) : IM_COL32(0,0,0,20));
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.0f);
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, fpad);
                    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);
                    std::string notesPreview = c.notes;
                    InputTextMultilineString("##tbl_popup_notes", &notesPreview, ImVec2(noteW, notesH), ImGuiInputTextFlags_ReadOnly);
                    ImGui::PopStyleVar(3);
                    ImGui::PopStyleColor(2);
                }
            }
            else if (s_tbl_popup_tab == 2 && hasSecurityTab)
            {
                const char* pw = get_password_fn ? get_password_fn(c.id) : "";
                std::string pwStr = pw ? pw : "";
                if (!pwStr.empty()) {
                    float meterW = ImGui::GetContentRegionAvail().x;
                    DrawStrengthMeterCompact(pwStr, meterW);
                }
                ImGui::Dummy(ImVec2(0, 4));

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

                bool hasIssue = false;
                if (g_shell_ptr && g_shell_ptr->sec_weak_ids.count(c.id)) {
                    ImGui::TextColored(ImVec4(174/255.f, 41/255.f, 41/255.f, 1.0f), ICON_MDI_ALERT " Weak password");
                    hasIssue = true;
                }
                if (g_shell_ptr && g_shell_ptr->sec_reused_ids.count(c.id)) {
                    ImGui::TextColored(ImVec4(190/255.f, 156/255.f, 63/255.f, 1.0f), ICON_MDI_REPEAT " Reused password");
                    hasIssue = true;
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
                                    out.edit_open_id = other.id;
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
                if (!hasIssue)
                    ImGui::TextColored(ImVec4(76/255.f, 195/255.f, 100/255.f, 1.0f), ICON_MDI_CHECK_CIRCLE " No issues detected");
            }

            ImGui::PopItemFlag();
            PopupStyleEnd();
            ImGui::EndPopup();
        }
        else
        {
            s_table_popup_id = -1;
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(5);

        ImGui::PopStyleVar();  // CellPadding (data rows)
        ImGui::PopStyleColor(2);  // header popup colors
        ImGui::PopStyleVar(5);    // header popup vars
        ImGui::EndTable();
        ImGui::PopStyleColor(4);  // table border/row colors
        ImGui::PopStyleVar();  // WindowPadding (popup)
        return out;
    }
} // namespace ui
