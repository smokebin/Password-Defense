// ui_widgets.cpp — inputs, buttons, combos, tabs, search, password fields
#include "ui_internal.h"

namespace ui
{
    bool IconButtonSquare2(const char* id, const char* glyph, float size);

    bool CheckboxBg(const char* label, bool* v)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;

        const ImGuiStyle& style = ImGui::GetStyle();
        const ImGuiID id = window->GetID(label);
        const ImVec2 label_size = ImGui::CalcTextSize(label, NULL, true);
        const bool dark = g_shell_ptr ? g_shell_ptr->dark_theme : true;

        const float square_sz = 15.0f;
        const ImVec2 pos = window->DC.CursorPos;
        const ImRect total_bb(pos, ImVec2(pos.x + square_sz + 12 + label_size.x, pos.y + ImMax(label_size.y, square_sz)));
        ImGui::ItemSize(total_bb, style.FramePadding.y);
        if (!ImGui::ItemAdd(total_bb, id))
            return false;

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(total_bb, id, &hovered, &held);
        if (pressed)
        {
            *v = !(*v);
            ImGui::MarkItemEdited(id);
        }

        ImGuiStorage* st = ImGui::GetStateStorage();
        ImGuiID animKey = id + 0x9900;
        float t = st->GetFloat(animKey, *v ? 1.0f : 0.0f);
        float target = *v ? 1.0f : (hovered ? 0.2f : 0.0f);
        t += (target - t) * ImMin(1.0f, ImGui::GetIO().DeltaTime * 12.0f);
        st->SetFloat(animKey, t);

        ImDrawList* dl = window->DrawList;
        ImVec2 boxMin = total_bb.Min;
        ImVec2 boxMax(boxMin.x + square_sz, boxMin.y + square_sz);

        ImU32 inactiveBg = theme::ToggleInactiveBg;
        dl->AddRectFilled(boxMin, boxMax, inactiveBg, 3.0f);
        dl->AddRectFilled(boxMin, boxMax, ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 0.384f, 0.016f, t)), 3.0f);
        ImGui::RenderCheckMark(dl, ImVec2(boxMin.x + 3, (boxMin.y + boxMax.y) * 0.5f - 5), IM_COL32(255, 255, 255, (ImU8)(t * 255)), 9.0f);
        dl->AddText(ImVec2(total_bb.Max.x - label_size.x - 5, total_bb.Min.y), ImGui::GetColorU32(ImGuiCol_Text), label);

        return pressed;
    }

    bool ToggleSwitch(const char* id, bool* value)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;

        ImDrawList* dl = window->DrawList;

        const float trackW = 38.0f;
        const float trackH = 13.0f;
        const float trackR = trackH * 0.5f;
        const float thumbD = 19.0f;
        const float thumbR = thumbD * 0.5f;
        const float totalH = thumbD; // thumb taller than track

        ImGui::PushID(id);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        float lineH = ImMax(ImGui::GetTextLineHeightWithSpacing(), totalH);
        float offsetY = (lineH - totalH) * 0.5f;
        pos.y += offsetY;

        ImRect bb(pos, ImVec2(pos.x + trackW, pos.y + totalH)); // hit rect covers full thumb height
        ImGui::ItemSize(ImVec2(trackW, lineH));
        ImGuiID wid = ImGui::GetID("##toggle");
        if (!ImGui::ItemAdd(bb, wid))
        {
            ImGui::PopID();
            return false;
        }

        bool hovered, held;
        bool pressed = ImGui::ButtonBehavior(bb, wid, &hovered, &held);
        bool changed = false;
        if (pressed)
        {
            *value = !(*value);
            changed = true;
            ImGui::MarkItemEdited(wid);
        }

        ImGuiStorage* st = ImGui::GetStateStorage();
        ImGuiID animKey = wid + 0x7700;
        float t = st->GetFloat(animKey, *value ? 1.0f : 0.0f);
        float target = *value ? 1.0f : 0.0f;
        float speed = ImGui::GetIO().DeltaTime * 10.0f;
        if (t < target) t = ImMin(t + speed, target);
        else if (t > target) t = ImMax(t - speed, target);
        st->SetFloat(animKey, t);

        float trackTop = bb.Min.y + (totalH - trackH) * 0.5f;
        ImVec2 trkMin(bb.Min.x, trackTop);
        ImVec2 trkMax(bb.Max.x, trackTop + trackH);

        ImVec4 offCol(0x8e/255.0f, 0x8e/255.0f, 0x93/255.0f, 1.0f); // #8e8e93
        ImVec4 onCol(0xff/255.0f, 0x8c/255.0f, 0x32/255.0f, 1.0f);  // #ff8c32
        ImVec4 trackCol = UI_LerpVec4(offCol, onCol, t);
        dl->AddRectFilled(trkMin, trkMax, ImGui::ColorConvertFloat4ToU32(trackCol), trackR);

        float thumbMinX = bb.Min.x + thumbR;
        float thumbMaxX = bb.Max.x - thumbR;
        float thumbX = UI_Lerp(thumbMinX, thumbMaxX, t);
        float thumbY = trackTop + trackH * 0.5f;

        dl->AddCircleFilled(ImVec2(thumbX, thumbY + 1.5f), thumbR + 0.5f, IM_COL32(0, 0, 0, 40), 24); // shadow
        dl->AddCircleFilled(ImVec2(thumbX, thumbY), thumbR, IM_COL32(255, 255, 255, 255), 24);

        ImGui::PopID();
        return changed;
    }

    bool DotSlider(const char* id, int* value, int count)
    {
        if (count <= 0) return false;
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;

        ImDrawList* dl = window->DrawList;
        const int oldVal = *value;

        const float dotSpacing = 22.0f;
        const float trackH = 12.0f;
        const float trackPadX = 12.0f;
        const float trackW = (count - 1) * dotSpacing + trackPadX * 2.0f;
        const float arrowBtnW = 24.0f;
        const float arrowBtnH = 24.0f;
        const float gap = 4.0f;
        const float totalW = arrowBtnW + gap + trackW + gap + arrowBtnW;
        const float totalH = arrowBtnH;
        const float trackR = trackH * 0.5f;

        const float dotInactiveR = 3.0f;
        const float dotActiveR = 6.0f;

        const bool dark = g_shell_ptr ? g_shell_ptr->dark_theme : true;

        ImGui::PushID(id);

        ImVec2 startPos = ImGui::GetCursorScreenPos();
        float lineH = ImMax(ImGui::GetTextLineHeightWithSpacing(), totalH);
        float offsetY = (lineH - totalH) * 0.5f;

        // left arrow
        {
            ImVec2 btnPos(startPos.x, startPos.y + offsetY);
            ImRect btnBB(btnPos, ImVec2(btnPos.x + arrowBtnW, btnPos.y + arrowBtnH));
            ImGuiID bid = ImGui::GetID("##dot_left");
            ImGui::SetCursorScreenPos(btnPos);
            bool atMin = (*value <= 0);

            if (ImGui::InvisibleButton("##dl", ImVec2(arrowBtnW, arrowBtnH)) && !atMin)
                (*value)--;

            bool hov = ImGui::IsItemHovered() && !atMin;
            ImU32 arrowCol = dark
                ? IM_COL32(255, 255, 255, atMin ? 50 : (hov ? 255 : 160))
                : IM_COL32(30, 30, 30, atMin ? 50 : (hov ? 255 : 160));
            const char* ico = ICON_MDI_CHEVRON_LEFT;
            ImVec2 icoSz = ImGui::CalcTextSize(ico);
            dl->AddText(ImVec2(btnPos.x + (arrowBtnW - icoSz.x) * 0.5f, btnPos.y + (arrowBtnH - icoSz.y) * 0.5f), arrowCol, ico);
        }

        float trackX = startPos.x + arrowBtnW + gap;
        float trackY = startPos.y + offsetY + (arrowBtnH - trackH) * 0.5f;
        ImVec2 trkMin(trackX, trackY);
        ImVec2 trkMax(trackX + trackW, trackY + trackH);
        ImU32 trackCol = dark ? theme::ScrollTrack.dark : theme::ScrollTrack.light;
        dl->AddRectFilled(trkMin, trkMax, trackCol, trackR);

        float dotY = trackY + trackH * 0.5f;
        float dotStartX = trackX + trackPadX;
        {
            ImVec2 hitMin(trackX, startPos.y + offsetY);
            ImVec2 hitMax(trackX + trackW, startPos.y + offsetY + arrowBtnH);
            ImGui::SetCursorScreenPos(hitMin);
            ImGui::InvisibleButton("##track", ImVec2(trackW, arrowBtnH));
            bool trackHov = ImGui::IsItemHovered();
            bool trackActive = ImGui::IsItemActive();

            if (trackActive || (trackHov && ImGui::IsMouseClicked(0)))
            {
                float mx = ImGui::GetMousePos().x;
                float rel = (mx - dotStartX) / dotSpacing;
                int nearest = ImClamp((int)(rel + 0.5f), 0, count - 1);
                *value = nearest;
            }

        }

        ImVec2 mousePos = ImGui::GetMousePos();
        bool trackHovered = (mousePos.x >= trackX && mousePos.x <= trackX + trackW &&
                             mousePos.y >= startPos.y + offsetY && mousePos.y <= startPos.y + offsetY + arrowBtnH);
        for (int i = 0; i < count; i++)
        {
            float dx = dotStartX + i * dotSpacing;
            bool isActive = (i == *value);

            bool dotHov = trackHovered && fabsf(mousePos.x - dx) < dotSpacing * 0.45f;

            if (isActive)
            {
                ImU32 activeCol = dark ? IM_COL32(230, 230, 230, 255) : IM_COL32(28, 28, 28, 255);
                dl->AddCircleFilled(ImVec2(dx, dotY), dotActiveR, activeCol, 20);
            }
            else
            {
                ImU32 inactiveCol = dark
                    ? IM_COL32(140, 140, 145, dotHov ? 220 : 160)
                    : IM_COL32(160, 160, 165, dotHov ? 255 : 180);
                dl->AddCircleFilled(ImVec2(dx, dotY), dotInactiveR, inactiveCol, 16);
            }
        }

        // right arrow
        {
            ImVec2 btnPos(trackX + trackW + gap, startPos.y + offsetY);
            ImGui::SetCursorScreenPos(btnPos);
            bool atMax = (*value >= count - 1);

            if (ImGui::InvisibleButton("##dr", ImVec2(arrowBtnW, arrowBtnH)) && !atMax)
                (*value)++;

            bool hov = ImGui::IsItemHovered() && !atMax;
            ImU32 arrowCol = dark
                ? IM_COL32(255, 255, 255, atMax ? 50 : (hov ? 255 : 160))
                : IM_COL32(30, 30, 30, atMax ? 50 : (hov ? 255 : 160));
            const char* ico = ICON_MDI_CHEVRON_RIGHT;
            ImVec2 icoSz = ImGui::CalcTextSize(ico);
            dl->AddText(ImVec2(btnPos.x + (arrowBtnW - icoSz.x) * 0.5f, btnPos.y + (arrowBtnH - icoSz.y) * 0.5f), arrowCol, ico);
        }

        ImGui::SetCursorScreenPos(ImVec2(startPos.x, startPos.y));
        ImGui::ItemSize(ImVec2(totalW, lineH));

        *value = ImClamp(*value, 0, count - 1);

        ImGui::PopID();
        return *value != oldVal;
    }

    bool VerticalStepper(const char* id, int* value, int min_val, int max_val, const char* suffix)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return false;

        ImDrawList* dl = window->DrawList;
        const int oldVal = *value;
        const bool dark = g_shell_ptr ? g_shell_ptr->dark_theme : true;

        const float w = 56.0f;
        const float h = 70.0f;
        const float rounding = 14.0f;
        const float chevronH = 18.0f;
        const float valueH = h - chevronH * 2.0f;

        ImGui::PushID(id);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImRect bb(pos, ImVec2(pos.x + w, pos.y + h));
        ImGui::ItemSize(ImVec2(w, h));
        ImGuiID wid = ImGui::GetID("##vs");
        if (!ImGui::ItemAdd(bb, wid))
        {
            ImGui::PopID();
            return false;
        }

        ImU32 bgCol = dark ? IM_COL32(44, 44, 48, 255) : IM_COL32(240, 240, 243, 255);
        dl->AddRectFilled(bb.Min, bb.Max, bgCol, rounding);

        ImGuiStorage* st = ImGui::GetStateStorage();
        ImGuiID editKey = wid + 0x8800;
        ImGuiID prevKey = wid + 0x8801;
        ImGuiID focusKey = wid + 0x8802;  // 0=need focus, 1=focusing, 2+=focused
        bool editing = st->GetInt(editKey, 0) != 0;
        static char s_edit_buf[16];
        static ImGuiID s_editing_id = 0;

        bool atMin = (*value <= min_val);
        bool atMax = (*value >= max_val);

        // up chevron
        {
            ImVec2 cPos(bb.Min.x, bb.Min.y);
            ImGui::SetCursorScreenPos(cPos);
            if (ImGui::InvisibleButton("##up", ImVec2(w, chevronH)) && !atMax)
                (*value)++;

            bool hov = ImGui::IsItemHovered() && !atMax;
            ImU32 col = dark
                ? IM_COL32(255, 255, 255, atMax ? 40 : (hov ? 220 : 120))
                : IM_COL32(30, 30, 30, atMax ? 40 : (hov ? 220 : 120));
            const char* ico = ICON_MDI_CHEVRON_UP;
            ImVec2 icoSz = ImGui::CalcTextSize(ico);
            dl->AddText(ImVec2(cPos.x + (w - icoSz.x) * 0.5f, cPos.y + (chevronH - icoSz.y) * 0.5f), col, ico);
        }

        {
            float valTop = bb.Min.y + chevronH;
            ImVec2 valPos(bb.Min.x, valTop);

            if (editing && s_editing_id == wid)
            {
                ImGui::SetCursorScreenPos(ImVec2(bb.Min.x + 4.0f, valTop + (valueH - ImGui::GetFrameHeight()) * 0.5f));
                ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
                ImGui::PushStyleColor(ImGuiCol_Text, dark ? IM_COL32(255, 255, 255, 255) : IM_COL32(20, 20, 20, 255));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2, 2));
                ImGui::SetNextItemWidth(w - 8.0f);

                ImGuiInputTextFlags flags = ImGuiInputTextFlags_CharsDecimal | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll;
                int focusFrame = st->GetInt(focusKey, 0);
                if (focusFrame < 2)
                {
                    ImGui::SetKeyboardFocusHere();
                    st->SetInt(focusKey, focusFrame + 1);
                }
                bool enter = ImGui::InputText("##edit", s_edit_buf, sizeof(s_edit_buf), flags);
                bool focused = ImGui::IsItemFocused();
                bool escape = ImGui::IsKeyPressed(ImGuiKey_Escape);

                if (enter)
                {
                    int parsed = atoi(s_edit_buf);
                    *value = ImClamp(parsed, min_val, max_val);
                    st->SetInt(editKey, 0);
                    st->SetInt(focusKey, 0);
                    s_editing_id = 0;
                }
                else if (escape)
                {
                    *value = st->GetInt(prevKey, *value);
                    st->SetInt(editKey, 0);
                    st->SetInt(focusKey, 0);
                    s_editing_id = 0;
                }
                else if (focusFrame >= 2 && !focused)
                {
                    int parsed = atoi(s_edit_buf);
                    *value = ImClamp(parsed, min_val, max_val);
                    st->SetInt(editKey, 0);
                    st->SetInt(focusKey, 0);
                    s_editing_id = 0;
                }

                ImGui::PopStyleVar();
                ImGui::PopStyleColor(2);
            }
            else
            {
                // click to edit
                ImGui::SetCursorScreenPos(valPos);
                if (ImGui::InvisibleButton("##val", ImVec2(w, valueH)))
                {
                    st->SetInt(editKey, 1);
                    st->SetInt(prevKey, *value);
                    s_editing_id = wid;
                    snprintf(s_edit_buf, sizeof(s_edit_buf), "%d", *value);
                }

                char dispBuf[24];
                if (suffix && suffix[0])
                    snprintf(dispBuf, sizeof(dispBuf), "%d%s", *value, suffix);
                else
                    snprintf(dispBuf, sizeof(dispBuf), "%d", *value);

                ImU32 textCol = dark ? IM_COL32(240, 240, 240, 255) : IM_COL32(20, 20, 20, 255);
                ImFont* font = render::FontLarge ? render::FontLarge : ImGui::GetFont();
                float fontSize = font->LegacySize > 0.0f ? font->LegacySize : ImGui::GetFontSize();
                ImVec2 tSz = font->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, dispBuf);
                dl->AddText(font, fontSize,
                    ImVec2(bb.Min.x + (w - tSz.x) * 0.5f, valTop + (valueH - tSz.y) * 0.5f),
                    textCol, dispBuf);
            }
        }

        // down chevron
        {
            ImVec2 cPos(bb.Min.x, bb.Max.y - chevronH);
            ImGui::SetCursorScreenPos(cPos);
            if (ImGui::InvisibleButton("##dn", ImVec2(w, chevronH)) && !atMin)
                (*value)--;

            bool hov = ImGui::IsItemHovered() && !atMin;
            ImU32 col = dark
                ? IM_COL32(255, 255, 255, atMin ? 40 : (hov ? 220 : 120))
                : IM_COL32(30, 30, 30, atMin ? 40 : (hov ? 220 : 120));
            const char* ico = ICON_MDI_CHEVRON_DOWN;
            ImVec2 icoSz = ImGui::CalcTextSize(ico);
            dl->AddText(ImVec2(cPos.x + (w - icoSz.x) * 0.5f, cPos.y + (chevronH - icoSz.y) * 0.5f), col, ico);
        }

        ImGui::SetCursorScreenPos(ImVec2(pos.x + w, pos.y));

        *value = ImClamp(*value, min_val, max_val);

        ImGui::PopID();
        return *value != oldVal;
    }

    float UI_Lerp(float a, float b, float t) { return a + (b - a) * t; }
    ImVec4 UI_LerpVec4(const ImVec4& a, const ImVec4& b, float t)
    {
        return ImVec4(UI_Lerp(a.x,b.x,t), UI_Lerp(a.y,b.y,t), UI_Lerp(a.z,b.z,t), UI_Lerp(a.w,b.w,t));
    }

    bool AnimatedComboDot(
        const char* id,
        const char* preview_value,
        const char* const* items,
        int items_count,
        int* current_index,
        float width,
        float height,
        bool light)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (!window) return false;

        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = window->DrawList;

        const float rounding = 6.0f;
        const ImVec2 pad = ImVec2(10.0f, 0.0f);
        const float chevron_w = 16.0f;
        const float dot_r = 3.0f;

        const bool dark = g_shell_ptr ? g_shell_ptr->dark_theme : true;
        const ImVec4 col_bg       = dark ? (light ? ImVec4(0.22f, 0.22f, 0.24f, 1.00f) : ImVec4(0.12f, 0.12f, 0.13f, 1.00f)) : ImVec4(0.95f, 0.95f, 0.96f, 1.00f);
        const ImVec4 col_bg_hov   = dark ? (light ? ImVec4(0.26f, 0.26f, 0.28f, 1.00f) : ImVec4(0.16f, 0.16f, 0.18f, 1.00f)) : ImVec4(0.90f, 0.90f, 0.92f, 1.00f);
        const ImVec4 col_bg_open  = dark ? (light ? ImVec4(0.28f, 0.28f, 0.30f, 1.00f) : ImVec4(0.18f, 0.18f, 0.20f, 1.00f)) : ImVec4(0.88f, 0.88f, 0.90f, 1.00f);
        const ImVec4 col_border   = dark ? ImVec4(1.00f, 1.00f, 1.00f, 0.06f) : ImVec4(0.00f, 0.00f, 0.00f, 0.10f);
        const ImVec4 col_text     = dark ? ImVec4(0.92f, 0.92f, 0.92f, 1.00f) : ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        const ImVec4 col_muted    = dark ? ImVec4(0.92f, 0.92f, 0.92f, 0.55f) : ImVec4(0.10f, 0.10f, 0.10f, 0.55f);
        const ImVec4 col_popup_bg = dark ? ImVec4(0.10f, 0.10f, 0.11f, 1.00f) : ImVec4(0.98f, 0.98f, 0.98f, 1.00f);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 size(width, height);

        ImGui::PushID(id);
        ImGui::InvisibleButton("##combo_btn", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool pressed = ImGui::IsItemClicked();

        ImGuiID base_id = ImGui::GetItemID();
        const ImGuiID popup_id = ImGui::GetID("##combo_popup");
        const bool popup_open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);

        if (pressed)
            ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_None);

        ImGuiStorage* st = ImGui::GetStateStorage();
        const ImGuiID key_hover = base_id + 0x1001;
        const ImGuiID key_open = base_id + 0x1002;

        float hover_t = st->GetFloat(key_hover, 0.0f);
        float open_t = st->GetFloat(key_open, 0.0f);

        const float dt = io.DeltaTime;
        const float hover_speed = 14.0f;
        const float open_speed = 18.0f;

        hover_t = ImClamp(hover_t + (hovered ? 1.0f : -1.0f) * dt * hover_speed, 0.0f, 1.0f);
        open_t = ImClamp(open_t + (popup_open ? 1.0f : -1.0f) * dt * open_speed, 0.0f, 1.0f);

        st->SetFloat(key_hover, hover_t);
        st->SetFloat(key_open, open_t);

        ImVec4 bg = col_bg;
        bg = UI_LerpVec4(bg, col_bg_hov, hover_t);
        bg = UI_LerpVec4(bg, col_bg_open, open_t);

        ImU32 bg_u32 = ImGui::ColorConvertFloat4ToU32(bg);
        ImU32 border_u32 = ImGui::ColorConvertFloat4ToU32(col_border);
        ImU32 text_u32 = ImGui::ColorConvertFloat4ToU32(col_text);
        ImU32 muted_u32 = ImGui::ColorConvertFloat4ToU32(col_muted);

        ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));

        dl->PushClipRect(
            ImVec2(bb.Min.x - 2.0f, bb.Min.y - 1.0f),
            ImVec2(bb.Max.x + 2.0f, bb.Max.y + 3.0f), true);

        // Soft bevel: bottom/right lip shadow behind main rect
        {
            const float lipOff = 1.0f;
            ImU32 lipCol = dark ? IM_COL32(0, 0, 0, 80) : IM_COL32(0, 0, 0, 25);
            // Bottom lip
            dl->AddRectFilled(
                ImVec2(bb.Min.x, bb.Min.y + lipOff),
                ImVec2(bb.Max.x, bb.Max.y + lipOff),
                lipCol, rounding);
        }

        dl->AddRectFilled(bb.Min, bb.Max, bg_u32, rounding);

        // Top inner shine
        {
            ImU32 shineCol = dark ? IM_COL32(255, 255, 255, 12) : IM_COL32(255, 255, 255, 200);
            dl->AddLine(
                ImVec2(bb.Min.x + rounding, bb.Min.y + 0.5f),
                ImVec2(bb.Max.x - rounding, bb.Min.y + 0.5f),
                shineCol, 1.0f);
        }

        // Bottom inner shadow
        {
            ImU32 shadowCol = dark ? IM_COL32(0, 0, 0, 30) : IM_COL32(0, 0, 0, 12);
            dl->AddLine(
                ImVec2(bb.Min.x + rounding, bb.Max.y - 0.5f),
                ImVec2(bb.Max.x - rounding, bb.Max.y - 0.5f),
                shadowCol, 1.0f);
        }

        dl->AddRect(bb.Min, bb.Max, border_u32, rounding, 0, 1.0f);

        // text clipped to not overlap chevron
        ImVec2 text_pos = ImVec2(bb.Min.x + pad.x, bb.Min.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f);
        float text_max_x = bb.Max.x - pad.x - chevron_w - 4.0f;
        dl->PushClipRect(bb.Min, ImVec2(text_max_x, bb.Max.y), true);
        dl->AddText(text_pos, text_u32, preview_value ? preview_value : "");
        dl->PopClipRect();

        // chevron crossfades DOWN↔UP when open
        const char* chev_down = ICON_MDI_CHEVRON_DOWN;
        const char* chev_up = ICON_MDI_CHEVRON_UP;
        ImVec2 chev_pos = ImVec2(bb.Max.x - pad.x - chevron_w, bb.Min.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f);

        {
            float a_down = (1.0f - open_t) * 0.85f;
            float a_up = open_t * 0.85f;
            ImU32 down_col = ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x, col_text.y, col_text.z, a_down));
            ImU32 up_col = ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x, col_text.y, col_text.z, a_up));
            dl->AddText(chev_pos, down_col, chev_down);
            dl->AddText(chev_pos, up_col, chev_up);
        }

        dl->PopClipRect();

        bool changed = false;

        const float popup_h = items_count * 26.0f + 12.0f;
        const float space_below = io.DisplaySize.y - bb.Max.y;
        const bool flip_up = space_below < popup_h + 8.0f; // flip above if no room below
        float popup_y = flip_up ? (bb.Min.y - popup_h - 4.0f) : (bb.Max.y + 4.0f);
        ImGui::SetNextWindowPos(ImVec2(bb.Min.x, popup_y));
        ImGui::SetNextWindowSize(ImVec2(size.x, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertFloat4ToU32(col_popup_bg));
        ImGui::PushStyleColor(ImGuiCol_Border, dark ? theme::PopupBorder.dark : theme::PopupBorder.light);

        if (ImGui::BeginPopupEx(popup_id, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar))
        {
            ImDrawList* popup_dl = ImGui::GetWindowDrawList(); // must use popup's dl, not parent's

            for (int i = 0; i < items_count; i++)
            {
                const bool selected = (*current_index == i);

                ImGui::PushID(i);

                const float row_h = 26.0f;
                ImVec2 row_start = ImGui::GetCursorScreenPos();
                ImRect row_bb(row_start, ImVec2(row_start.x + ImGui::GetContentRegionAvail().x, row_start.y + row_h));

                ImGui::InvisibleButton("##opt", row_bb.GetSize());
                bool row_hovered = ImGui::IsItemHovered();
                bool row_clicked = ImGui::IsItemClicked();

                if (row_hovered || selected)
                {
                    ImU32 hi = ImGui::ColorConvertFloat4ToU32(
                        row_hovered ? ImVec4(col_text.x, col_text.y, col_text.z, 0.08f)
                                    : ImVec4(col_text.x, col_text.y, col_text.z, 0.04f));
                    popup_dl->AddRectFilled(row_bb.Min, row_bb.Max, hi, 4.0f);
                }

                const float dot_x = row_bb.Min.x + 10.0f;
                const float dot_y = (row_bb.Min.y + row_bb.Max.y) * 0.5f;
                if (selected)
                    popup_dl->AddCircleFilled(ImVec2(dot_x, dot_y), dot_r, ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x, col_text.y, col_text.z, 0.90f)));
                else
                    popup_dl->AddCircleFilled(ImVec2(dot_x, dot_y), dot_r, ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x, col_text.y, col_text.z, 0.18f)));

                ImU32 opt_col = selected ? text_u32 : muted_u32;
                popup_dl->AddText(ImVec2(row_bb.Min.x + 22.0f, row_bb.Min.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f), opt_col, items[i]);

                if (row_clicked)
                {
                    *current_index = i;
                    changed = true;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::PopID();
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
        ImGui::PopID();

        return changed;
    }

    bool AnimatedComboDotMulti(
        const char* id,
        const char* preview_label,
        const std::vector<std::string>& items,  // items[0] = "Filter"
        std::set<std::string>& selected)        // empty = show all
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (!window) return false;

        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = window->DrawList;

        const float rounding = 4.0f;
        const ImVec2 pad = ImVec2(4.0f, 0.0f);
        const float dot_r = 3.0f;

        const bool dark = g_shell_ptr ? g_shell_ptr->dark_theme : true;
        const ImVec4 col_text     = dark ? ImVec4(0.92f, 0.92f, 0.92f, 1.00f) : ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
        const ImVec4 col_muted    = dark ? ImVec4(0.92f, 0.92f, 0.92f, 0.55f) : ImVec4(0.10f, 0.10f, 0.10f, 0.55f);
        const ImVec4 col_popup_bg = dark ? ImVec4(0.10f, 0.10f, 0.11f, 1.00f) : ImVec4(0.98f, 0.98f, 0.98f, 1.00f);
        const ImVec4 col_hover_bg = dark ? ImVec4(1.00f, 1.00f, 1.00f, 0.08f) : ImVec4(0.00f, 0.00f, 0.00f, 0.08f);

        const float chevW  = ImGui::CalcTextSize(ICON_MDI_CHEVRON_DOWN).x;

        // wide enough for "Passwords" label + chevron
        const float baseW = ImGui::CalcTextSize("Passwords").x;
        const float width = pad.x + baseW + 6.0f + chevW + pad.x;
        const float height = ImGui::GetFrameHeight();

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImVec2 size(width, height);

        ImGui::PushID(id);
        ImGui::InvisibleButton("##combo_btn", size);
        const bool hovered = ImGui::IsItemHovered();
        const bool pressed = ImGui::IsItemClicked();

        ImGuiID base_id = ImGui::GetItemID();
        const ImGuiID popup_id = ImGui::GetID("##combo_popup");
        bool popup_open = ImGui::IsPopupOpen(popup_id, ImGuiPopupFlags_None);

        if (pressed)
            ImGui::OpenPopupEx(popup_id, ImGuiPopupFlags_None);

        ImGuiStorage* st = ImGui::GetStateStorage();
        const ImGuiID key_open = base_id + 0x1002;

        float open_t = st->GetFloat(key_open, 0.0f);
        const float dt = io.DeltaTime;
        const float open_speed = 18.0f;

        open_t = ImClamp(open_t + (popup_open ? 1.0f : -1.0f) * dt * open_speed, 0.0f, 1.0f);
        st->SetFloat(key_open, open_t);

        ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));

        if (hovered || popup_open)
        {
            dl->AddRectFilled(bb.Min, bb.Max,
                ImGui::ColorConvertFloat4ToU32(col_hover_bg), rounding);
        }

        ImU32 text_u32 = ImGui::ColorConvertFloat4ToU32(col_text);
        ImU32 muted_u32 = ImGui::ColorConvertFloat4ToU32(col_muted);
        ImVec2 text_pos = ImVec2(bb.Min.x + pad.x, bb.Min.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f);
        const float maxTextX = bb.Max.x - pad.x - chevW - 4.0f;
        ImVec4 clip_rect(text_pos.x, bb.Min.y, maxTextX, bb.Max.y);
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), text_pos, text_u32, preview_label, nullptr, 0.0f, &clip_rect);

        ImVec2 chev_pos = ImVec2(bb.Max.x - pad.x - chevW, bb.Min.y + (size.y - ImGui::GetTextLineHeight()) * 0.5f);
        {
            float a_down = (1.0f - open_t) * 0.7f;
            float a_up   = open_t * 0.7f;
            dl->AddText(chev_pos, ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x, col_text.y, col_text.z, a_down)), ICON_MDI_CHEVRON_DOWN);
            dl->AddText(chev_pos, ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x, col_text.y, col_text.z, a_up)),   ICON_MDI_CHEVRON_UP);
        }

        bool changed = false;

        const float popup_w = 190.0f;
        ImGui::SetNextWindowPos(ImVec2(bb.Min.x, bb.Max.y + 4.0f));
        ImGui::SetNextWindowSize(ImVec2(popup_w, 0.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 6.0f));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, ImGui::ColorConvertFloat4ToU32(col_popup_bg));
        ImGui::PushStyleColor(ImGuiCol_Border, dark ? theme::PopupBorder.dark : theme::PopupBorder.light);

        if (ImGui::BeginPopupEx(popup_id, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar))
        {
            ImDrawList* popup_dl = ImGui::GetWindowDrawList(); // must use popup's dl

            // @-prefixed items map to type icons
            auto TypeIconForFilter = [](const std::string& s) -> const char* {
                if (s == "@Passwords")    return ICON_MDI_KEY;
                if (s == "@Cards")    return ICON_MDI_CREDIT_CARD;
                if (s == "@Identity") return ICON_MDI_CARD_ACCOUNT_DETAILS;
                if (s == "@Notes")    return ICON_MDI_NOTE_TEXT;
                return nullptr;
            };

            int selectable_count = 0; // excludes "All" and separators
            for (int i = 1; i < (int)items.size(); i++)
                if (items[i] != "---") selectable_count++;

            for (int i = 0; i < (int)items.size(); i++)
            {
                if (items[i] == "---") // separator sentinel
                {
                    ImVec2 sep_start = ImGui::GetCursorScreenPos();
                    float sep_w = ImGui::GetContentRegionAvail().x;
                    popup_dl->AddLine(
                        ImVec2(sep_start.x + 6.0f, sep_start.y + 4.0f),
                        ImVec2(sep_start.x + sep_w - 6.0f, sep_start.y + 4.0f),
                        ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x, col_text.y, col_text.z, 0.12f)));
                    ImGui::Dummy(ImVec2(0, 9));
                    continue;
                }

                const bool is_all = (i == 0);
                const bool item_selected = is_all
                    ? selected.empty()
                    : (selected.find(items[i]) != selected.end());

                ImGui::PushID(i);

                const float row_h = 26.0f;
                ImVec2 row_start = ImGui::GetCursorScreenPos();
                ImRect row_bb(row_start, ImVec2(row_start.x + ImGui::GetContentRegionAvail().x, row_start.y + row_h));

                ImGui::InvisibleButton("##opt", row_bb.GetSize());
                bool row_hovered = ImGui::IsItemHovered();
                bool row_clicked = ImGui::IsItemClicked();

                if (row_hovered || item_selected)
                {
                    ImU32 hi = ImGui::ColorConvertFloat4ToU32(
                        row_hovered ? ImVec4(col_text.x, col_text.y, col_text.z, 0.08f)
                                    : ImVec4(col_text.x, col_text.y, col_text.z, 0.04f));
                    popup_dl->AddRectFilled(row_bb.Min, row_bb.Max, hi, 4.0f);
                }

                const float dot_x = row_bb.Min.x + 10.0f;
                const float dot_y = (row_bb.Min.y + row_bb.Max.y) * 0.5f;
                if (item_selected)
                    popup_dl->AddCircleFilled(ImVec2(dot_x, dot_y), dot_r, ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x, col_text.y, col_text.z, 0.90f)));
                else
                    popup_dl->AddCircleFilled(ImVec2(dot_x, dot_y), dot_r, ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x, col_text.y, col_text.z, 0.18f)));

                // type items get icon + label with '@' stripped
                ImU32 opt_col = item_selected ? text_u32 : muted_u32;
                float textX = row_bb.Min.x + 22.0f;
                float textY = row_bb.Min.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f;

                const char* typeIcon = TypeIconForFilter(items[i]);
                if (typeIcon)
                {
                    popup_dl->AddText(ImVec2(textX, textY), opt_col, typeIcon);
                    textX += ImGui::CalcTextSize(typeIcon).x + 4.0f;
                    popup_dl->AddText(ImVec2(textX, textY), opt_col, items[i].c_str() + 1); // skip '@'
                }
                else
                {
                    // user groups get folder icon
                    if (!is_all)
                    {
                        const char* folderIco = ICON_MDI_FOLDER;
                        popup_dl->AddText(ImVec2(textX, textY), opt_col, folderIco);
                        textX += ImGui::CalcTextSize(folderIco).x + 4.0f;
                    }
                    popup_dl->AddText(ImVec2(textX, textY), opt_col, items[i].c_str());
                }

                if (row_clicked)
                {
                    changed = true;
                    if (is_all)
                    {
                        selected.clear(); // "All" clears specific filters
                    }
                    else
                    {
                        auto it = selected.find(items[i]);
                        if (it != selected.end())
                            selected.erase(it);
                        else
                            selected.insert(items[i]);

                        // all specific items selected → revert to All
                        if ((int)selected.size() >= selectable_count)
                            selected.clear();
                    }
                }

                ImGui::PopID();
            }

            if (g_shell_ptr && !g_shell_ptr->all_tags.empty())
            {
                ImVec2 sep_start = ImGui::GetCursorScreenPos();
                float sep_w = ImGui::GetContentRegionAvail().x;
                popup_dl->AddLine(
                    ImVec2(sep_start.x + 6.0f, sep_start.y + 4.0f),
                    ImVec2(sep_start.x + sep_w - 6.0f, sep_start.y + 4.0f),
                    ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x, col_text.y, col_text.z, 0.12f)));
                ImGui::Dummy(ImVec2(0, 9));

                for (const auto& tag : g_shell_ptr->all_tags)
                {
                    const bool tag_selected = g_shell_ptr->selected_tags.count(tag) > 0;

                    ImGui::PushID(tag.c_str());

                    const float row_h = 26.0f;
                    ImVec2 row_start = ImGui::GetCursorScreenPos();
                    ImRect row_bb(row_start, ImVec2(row_start.x + ImGui::GetContentRegionAvail().x, row_start.y + row_h));

                    ImGui::InvisibleButton("##tag_opt", row_bb.GetSize());
                    bool row_hovered = ImGui::IsItemHovered();
                    bool row_clicked = ImGui::IsItemClicked();

                    if (row_hovered || tag_selected)
                    {
                        ImU32 hi = ImGui::ColorConvertFloat4ToU32(
                            row_hovered ? ImVec4(col_text.x, col_text.y, col_text.z, 0.08f)
                                        : ImVec4(col_text.x, col_text.y, col_text.z, 0.04f));
                        popup_dl->AddRectFilled(row_bb.Min, row_bb.Max, hi, 4.0f);
                    }

                    const float dot_x = row_bb.Min.x + 10.0f;
                    const float dot_y = (row_bb.Min.y + row_bb.Max.y) * 0.5f;
                    if (tag_selected)
                        popup_dl->AddCircleFilled(ImVec2(dot_x, dot_y), dot_r, ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x, col_text.y, col_text.z, 0.90f)));
                    else
                        popup_dl->AddCircleFilled(ImVec2(dot_x, dot_y), dot_r, ImGui::ColorConvertFloat4ToU32(ImVec4(col_text.x, col_text.y, col_text.z, 0.18f)));

                    ImU32 opt_col = tag_selected ? text_u32 : muted_u32;
                    float textX = row_bb.Min.x + 22.0f;
                    float textY = row_bb.Min.y + (row_h - ImGui::GetTextLineHeight()) * 0.5f;
                    const char* tagIco = tag_selected ? ICON_MDI_TAG : ICON_MDI_TAG;
                    popup_dl->AddText(ImVec2(textX, textY), opt_col, tagIco);
                    textX += ImGui::CalcTextSize(tagIco).x + 4.0f;
                    popup_dl->AddText(ImVec2(textX, textY), opt_col, tag.c_str());

                    if (row_clicked)
                    {
                        changed = true;
                        auto it = g_shell_ptr->selected_tags.find(tag);
                        if (it != g_shell_ptr->selected_tags.end())
                            g_shell_ptr->selected_tags.erase(it);
                        else
                            g_shell_ptr->selected_tags.insert(tag);
                    }

                    ImGui::PopID();
                }
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(4);
        ImGui::PopID();

        return changed;
    }

    // returns true once when hold completes; resets if released early or mouse leaves
    bool HoldToActionButton(const char* id, const char* label, float hold_seconds,
                                   ImVec2 size,
                                   float rounding)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (!window) return false;

        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = window->DrawList;

        if (size.x <= 0.0f) size.x = ImGui::CalcTextSize(label).x + 28.0f * 2.0f;
        if (size.y <= 0.0f) size.y = 34.0f;

        ImGui::PushID(id);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##hold_btn", size, ImGuiButtonFlags_MouseButtonLeft);

        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();

        ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));

        ImGuiStorage* st = ImGui::GetStateStorage(); // progress 0..1
        ImGuiID wid = ImGui::GetItemID();
        ImGuiID key = wid + 0x4A11;
        float t = st->GetFloat(key, 0.0f);

        const bool cancel_if_leave = true;
        const float dt = io.DeltaTime;
        const float speed = (hold_seconds <= 0.001f) ? 9999.0f : (1.0f / hold_seconds);

        bool triggered = false;

        if (held && (!cancel_if_leave || hovered))
        {
            t = ImClamp(t + dt * speed, 0.0f, 1.0f);
            if (t >= 1.0f)
            {
                triggered = true;
                t = 0.0f;
                ImGui::ClearActiveID();
            }
        }
        else
        {
            const float reset_speed = 10.0f;
            t = ImClamp(t - dt * reset_speed, 0.0f, 1.0f);
        }

        st->SetFloat(key, t);

        const bool dark = g_shell_ptr ? g_shell_ptr->dark_theme : true;

        const ImU32 col_fill    = ImGui::GetColorU32(ImVec4(1.00f, 0.25f, 0.25f, 0.20f)); // danger red tint
        const ImU32 col_fill_hi = ImGui::GetColorU32(ImVec4(1.00f, 0.25f, 0.25f, 0.28f));

        const ImU32 col_text    = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);

        ImU32 faceBg;
        if (held)
            faceBg = theme::ToggleHeld;
        else if (hovered)
            faceBg = theme::ToggleHovered;
        else
            faceBg = theme::ToggleNormal;

        ImU32 shadowCol = theme::ToggleShadow;
        ImU32 hlCol = hovered ? theme::ToggleGlintHov : theme::ToggleGlintNorm;

        ImDrawListSplitter splitter;
        splitter.Split(dl, 2);
        splitter.SetCurrentChannel(dl, 1);

        dl->AddRectFilled(bb.Min, bb.Max, faceBg, rounding);

        // bottom lip shadow (split channel 0, clipped to lower half)
        {
            const float lipOff = 1.5f;
            float clipTop = bb.Min.y + (size.y * 0.6f);
            splitter.SetCurrentChannel(dl, 0);
            dl->PushClipRect(ImVec2(bb.Min.x - 2, clipTop), ImVec2(bb.Max.x + 2, bb.Max.y + lipOff + 2), true);
            dl->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + lipOff), ImVec2(bb.Max.x, bb.Max.y + lipOff), shadowCol, rounding);
            dl->PopClipRect();
            splitter.SetCurrentChannel(dl, 1);
        }

        // top highlight (clipped to upper third)
        {
            const float inset = 1.0f;
            float clipBot = bb.Min.y + (size.y * 0.35f);
            float hlR = rounding - inset;
            if (hlR < 0) hlR = 0;
            dl->PushClipRect(ImVec2(bb.Min.x + inset - 1, bb.Min.y + inset - 1), ImVec2(bb.Max.x - inset + 1, clipBot), true);
            dl->AddRect(ImVec2(bb.Min.x + inset, bb.Min.y + inset), ImVec2(bb.Max.x - inset, bb.Max.y - inset), hlCol, hlR, 0, 1.0f);
            dl->PopClipRect();
        }

        splitter.Merge(dl);

        // hold progress fill, left-to-right
        if (t > 0.001f)
        {
            ImRect fill = bb;
            fill.Max.x = UI_Lerp(bb.Min.x, bb.Max.x, t);
            dl->AddRectFilled(fill.Min, fill.Max, held ? col_fill_hi : col_fill, rounding);
        }

        const float pad_x = 18.0f;
        ImVec2 text_sz = ImGui::CalcTextSize(label);
        ImVec2 text_pos(bb.Min.x + pad_x, bb.Min.y + (size.y - text_sz.y) * 0.5f);

        dl->AddText(text_pos, col_text, label);

        ImGui::PopID();
        return triggered;
    }

    bool StyledButton(const char* id, const char* label,
                      ImVec2 size,
                      float rounding)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (!window) return false;

        ImDrawList* dl = window->DrawList;

        if (size.x <= 0.0f) size.x = ImGui::CalcTextSize(label).x + 36.0f;
        if (size.y <= 0.0f) size.y = 30.0f;
        if (rounding < 0.0f) rounding = 7.0f;

        ImGui::PushID(id);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##styled_btn", size, ImGuiButtonFlags_MouseButtonLeft);

        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const bool clicked = ImGui::IsItemClicked();

        ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));

        const bool dark = g_shell_ptr ? g_shell_ptr->dark_theme : true;

        ImU32 faceBg;
        if (held)
            faceBg = theme::ToggleHeld;
        else if (hovered)
            faceBg = theme::ToggleHovered;
        else
            faceBg = theme::ToggleNormal;

        ImU32 shadowCol = theme::ToggleShadow;
        ImU32 hlCol = hovered ? theme::ToggleGlintHov : theme::ToggleGlintNorm;

        ImDrawListSplitter splitter;
        splitter.Split(dl, 2);
        splitter.SetCurrentChannel(dl, 1);

        dl->AddRectFilled(bb.Min, bb.Max, faceBg, rounding);

        {
            const float lipOff = 1.5f;
            float clipTop = bb.Min.y + (size.y * 0.6f);
            splitter.SetCurrentChannel(dl, 0);
            dl->PushClipRect(ImVec2(bb.Min.x - 2, clipTop), ImVec2(bb.Max.x + 2, bb.Max.y + lipOff + 2), true);
            dl->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + lipOff), ImVec2(bb.Max.x, bb.Max.y + lipOff), shadowCol, rounding);
            dl->PopClipRect();
            splitter.SetCurrentChannel(dl, 1);
        }

        {
            const float inset = 1.0f;
            float clipBot = bb.Min.y + (size.y * 0.35f);
            float hlR = rounding - inset;
            if (hlR < 0) hlR = 0;
            dl->PushClipRect(ImVec2(bb.Min.x + inset - 1, bb.Min.y + inset - 1), ImVec2(bb.Max.x - inset + 1, clipBot), true);
            dl->AddRect(ImVec2(bb.Min.x + inset, bb.Min.y + inset), ImVec2(bb.Max.x - inset, bb.Max.y - inset), hlCol, hlR, 0, 1.0f);
            dl->PopClipRect();
        }

        splitter.Merge(dl);

        ImU32 col_text = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
        ImVec2 text_sz = ImGui::CalcTextSize(label);
        ImVec2 text_pos(bb.Min.x + (size.x - text_sz.x) * 0.5f, bb.Min.y + (size.y - text_sz.y) * 0.5f);
        dl->AddText(text_pos, col_text, label);

        ImGui::PopID();
        return clicked;
    }

    // lighter variant, animated — matches modal/settings context
    bool StyledButtonLight(const char* id, const char* label, ImVec2 size, float rounding)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (!window) return false;

        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = window->DrawList;

        if (size.x <= 0.0f) size.x = ImGui::CalcTextSize(label).x + 36.0f;
        if (size.y <= 0.0f) size.y = 30.0f;
        if (rounding < 0.0f) rounding = 7.0f;

        ImGui::PushID(id);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##styled_btn_l", size, ImGuiButtonFlags_MouseButtonLeft);

        const bool hovered = ImGui::IsItemHovered();
        const bool held = ImGui::IsItemActive();
        const bool clicked = ImGui::IsItemClicked();
        const ImGuiID base_id = ImGui::GetItemID();

        ImRect bb(pos, ImVec2(pos.x + size.x, pos.y + size.y));

        ImGuiStorage* st = ImGui::GetStateStorage();
        float hover_t = st->GetFloat(base_id + 0x3001, 0.0f);
        float active_t = st->GetFloat(base_id + 0x3002, 0.0f);
        hover_t = ImClamp(hover_t + (hovered ? 1.0f : -1.0f) * io.DeltaTime * 14.0f, 0.0f, 1.0f);
        active_t = ImClamp(active_t + (held ? 1.0f : -1.0f) * io.DeltaTime * 18.0f, 0.0f, 1.0f);
        st->SetFloat(base_id + 0x3001, hover_t);
        st->SetFloat(base_id + 0x3002, active_t);

        const bool dark = g_shell_ptr ? g_shell_ptr->dark_theme : true;
        const ImVec4 col_bg      = dark ? ImVec4(0.22f, 0.22f, 0.24f, 1.0f) : ImVec4(0.94f, 0.94f, 0.95f, 1.0f);
        const ImVec4 col_bg_hov  = dark ? ImVec4(0.26f, 0.26f, 0.28f, 1.0f) : ImVec4(0.91f, 0.91f, 0.91f, 1.0f);
        const ImVec4 col_bg_act  = dark ? ImVec4(0.19f, 0.19f, 0.21f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f);
        const ImVec4 col_text    = dark ? ImVec4(0.93f, 0.93f, 0.93f, 1.0f) : ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
        const ImVec4 col_shadow  = dark ? ImVec4(0.0f, 0.0f, 0.0f, 0.20f)  : ImVec4(0.0f, 0.0f, 0.0f, 0.18f);
        const ImVec4 col_hl      = dark ? ImVec4(1.0f, 1.0f, 1.0f, 0.04f)  : ImVec4(1.0f, 1.0f, 1.0f, 0.50f);
        const ImVec4 col_hl_hov  = dark ? ImVec4(1.0f, 1.0f, 1.0f, 0.05f)  : ImVec4(1.0f, 1.0f, 1.0f, 0.70f);
        const ImVec4 col_hl_act  = dark ? ImVec4(0.45f, 0.45f, 0.45f, 0.05f): ImVec4(0.0f, 0.0f, 0.0f, 0.05f);

        ImVec4 bg_v = col_bg;
        bg_v = UI_LerpVec4(bg_v, col_bg_hov, hover_t);
        bg_v = UI_LerpVec4(bg_v, col_bg_act, active_t);

        ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(bg_v);
        ImU32 shadowCol = ImGui::ColorConvertFloat4ToU32(col_shadow);

        ImDrawListSplitter splitter;
        splitter.Split(dl, 2);
        splitter.SetCurrentChannel(dl, 1);

        dl->AddRectFilled(bb.Min, bb.Max, bgCol, rounding);

        // bottom lip
        {
            const float lipOffset = 1.8f;
            float clipTop = bb.Min.y + (size.y * 0.62f);
            splitter.SetCurrentChannel(dl, 0);
            dl->PushClipRect(ImVec2(bb.Min.x - 3.0f, clipTop), ImVec2(bb.Max.x + 3.0f, bb.Max.y + lipOffset + 2.0f), true);
            dl->AddRectFilled(ImVec2(bb.Min.x, bb.Min.y + lipOffset), ImVec2(bb.Max.x, bb.Max.y + lipOffset), shadowCol, rounding);
            dl->PopClipRect();
            splitter.SetCurrentChannel(dl, 1);
        }

        // right lip
        {
            const float lipOffset = 1.8f;
            float clipLeft = bb.Min.x + (size.x * 0.40f);
            splitter.SetCurrentChannel(dl, 0);
            dl->PushClipRect(ImVec2(clipLeft, bb.Min.y - 1.0f), ImVec2(bb.Max.x + lipOffset + 2.0f, bb.Max.y + 1.0f), true);
            dl->AddRectFilled(ImVec2(bb.Min.x + lipOffset, bb.Min.y), ImVec2(bb.Max.x + lipOffset, bb.Max.y), shadowCol, rounding);
            dl->PopClipRect();
            splitter.SetCurrentChannel(dl, 1);
        }

        // top highlight
        {
            ImVec4 hlV = col_hl;
            hlV = UI_LerpVec4(hlV, col_hl_hov, hover_t);
            hlV = UI_LerpVec4(hlV, col_hl_act, active_t);
            const float inset = 1.0f;
            float clipBottom = bb.Min.y + (size.y * 0.30f);
            float hlRound = rounding - inset;
            if (hlRound < 0.0f) hlRound = 0.0f;
            dl->PushClipRect(ImVec2(bb.Min.x + inset - 1.0f, bb.Min.y + inset - 1.0f), ImVec2(bb.Max.x - inset + 1.0f, clipBottom), true);
            dl->AddRect(ImVec2(bb.Min.x + inset, bb.Min.y + inset), ImVec2(bb.Max.x - inset, bb.Max.y - inset), ImGui::ColorConvertFloat4ToU32(hlV), hlRound, 0, 1.0f);
            dl->PopClipRect();
        }

        splitter.Merge(dl);

        float text_alpha = UI_Lerp(0.85f, 1.0f, hover_t);
        ImVec4 txt_v = ImVec4(col_text.x, col_text.y, col_text.z, text_alpha);
        ImVec2 text_sz = ImGui::CalcTextSize(label);
        ImVec2 text_pos(bb.Min.x + (size.x - text_sz.x) * 0.5f, bb.Min.y + (size.y - text_sz.y) * 0.5f);
        dl->AddText(text_pos, ImGui::ColorConvertFloat4ToU32(txt_v), label);

        ImGui::PopID();
        return clicked;
    }

    void EnsureAnimatedTabsFromLabels(AnimatedTabBar& bar, const std::vector<std::string>& labels, int active)
    {
        if ((int)bar.tabs.size() != (int)labels.size())
        {
            bar.tabs.clear();
            bar.tabs.reserve(labels.size());

            for (const auto& s : labels)
            {
                AnimatedTab t;
                t.label = s;
                t.isNew = true;
                t.alpha = 0.0f;
                t.scale = 0.8f;
                t.currentWidth = 0.0f;
                t.targetWidth = 0.0f;
                bar.tabs.push_back(std::move(t));
            }

            bar.activeIndex = ImClamp(active, 0, (int)labels.size() - 1);
            bar.barCurrentWidth = bar.barTargetWidth = 0.0f;
            bar.indicatorCurrentX = bar.indicatorTargetX = 0.0f;
            bar.indicatorCurrentWidth = bar.indicatorTargetWidth = 0.0f;
            return;
        }

        // update labels in-place (keeps animation state)
        for (int i = 0; i < (int)labels.size(); ++i)
        {
            if (bar.tabs[i].label != labels[i])
            {
                bar.tabs[i].label = labels[i];
                bar.tabs[i].id = 0; // force GetID() refresh
            }
        }

        bar.activeIndex = ImClamp(active, 0, (int)labels.size() - 1);
    }

    struct AnimatedTabDrawResult { int activatedIndex = -1; };

    static AnimatedTabDrawResult DrawAnimatedTabBarInternal(
        AnimatedTabBar& bar,
        float height = 28.0f,
        float iconWidth = 22.0f,
        float spacing = 8.0f,
        float animSpeed = 16.0f,
        bool dirtyActive = false)
    {
        AnimatedTabDrawResult result;

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (!window || window->SkipItems) return result;

        ImGuiContext& g = *GImGui;
        ImGuiStyle& style = g.Style;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float dt = g.IO.DeltaTime;

        ImVec2 barPos = ImGui::GetCursorScreenPos();
        bar.barTargetWidth = 0.0f;

        for (int i = 0; i < (int)bar.tabs.size(); ++i)
        {
            AnimatedTab& t = bar.tabs[i];
            if (t.id == 0)
                t.id = window->GetID(t.label.c_str());

            const bool isActive = (i == bar.activeIndex) && !t.isClosing;
            ImVec2 labelSize = ImGui::CalcTextSize(t.label.c_str(), nullptr, true);

            if (t.isNew) {
                t.currentWidth = 0.0f;
                t.targetWidth = iconWidth;
                t.offsetX = 20.0f;
                t.targetOffsetX = 0.0f;
                t.alpha = 0.0f;
                t.targetAlpha = 1.0f;
                t.scale = 0.85f;
                t.targetScale = 1.0f;
                t.isNew = false;
            }
            else if (t.isClosing) {
                t.targetWidth = 0.0f;
                t.targetAlpha = 0.0f;
                t.targetOffsetX = -20.0f;
                t.targetScale = 0.85f;
            }
            else if (isActive) {
                const float padX = style.FramePadding.x * 2.0f;
                t.targetWidth = iconWidth + labelSize.x + padX;
                t.targetAlpha = 1.0f;
                t.targetScale = 1.08f;
                t.targetOffsetX = 0.0f;
            }
            else {
                t.targetWidth = iconWidth;
                t.targetAlpha = 0.0f;
                t.targetScale = 1.0f;
                t.targetOffsetX = 0.0f;
            }

            t.currentWidth = AnimLerp(t.currentWidth, t.targetWidth, animSpeed, dt);
            t.offsetX = AnimLerp(t.offsetX, t.targetOffsetX, animSpeed, dt);
            t.alpha = AnimLerp(t.alpha, t.targetAlpha, animSpeed, dt);
            t.scale = AnimLerp(t.scale, t.targetScale, animSpeed, dt);

            bar.barTargetWidth += t.currentWidth;
            if (i > 0) bar.barTargetWidth += spacing;
        }

        bar.barCurrentWidth = AnimLerp(bar.barCurrentWidth, bar.barTargetWidth, animSpeed, dt);

        const float barHeight = height + style.FramePadding.y * 2.0f;
        ImRect barRect(barPos, ImVec2(barPos.x + bar.barCurrentWidth, barPos.y + barHeight));
        float barRounding = barHeight * 0.5f;

        // reserve layout space once — must happen before the per-tab ItemAdd calls
        ImGui::ItemSize(ImVec2(bar.barCurrentWidth, barHeight));
        ImGuiID barId = window->GetID("##animated_tabbar");
        ImGui::ItemAdd(barRect, barId);

        dl->AddRectFilled(barRect.Min, barRect.Max, ImGui::GetColorU32(style.Colors[ImGuiCol_ChildBg]), barRounding);

        float x = barPos.x;
        float y = barPos.y + style.FramePadding.y;

        for (int i = 0; i < (int)bar.tabs.size(); ++i)
        {
            AnimatedTab& t = bar.tabs[i];
            float w = t.currentWidth;
            if (w <= 0.5f && t.isClosing) { x += w + spacing; continue; }

            ImVec2 tabPos(x + t.offsetX, y);
            ImVec2 tabSize(w, height);
            ImRect tabRect(tabPos, ImVec2(tabPos.x + tabSize.x, tabPos.y + tabSize.y));

            t.lastRect = tabRect;
            t.lastRectValid = true;

            // ❌ REMOVE THIS:
            // ImGui::ItemSize(tabSize);

            if (!ImGui::ItemAdd(tabRect, t.id))
            {
                x += w + spacing;
                continue;
            }

            bool hovered = false, held = false;
            bool pressed = ImGui::ButtonBehavior(tabRect, t.id, &hovered, &held);
            if (pressed && !t.isClosing)
                result.activatedIndex = i;

            const bool isActive = (i == bar.activeIndex) && !t.isClosing;

            ImVec4 baseCol = isActive
                ? (hovered ? style.Colors[ImGuiCol_ButtonHovered] : style.Colors[ImGuiCol_Button])
                : (hovered ? style.Colors[ImGuiCol_FrameBgHovered] : style.Colors[ImGuiCol_FrameBg]);

            baseCol.w *= ImClamp(t.alpha, 0.55f, 1.0f);
            dl->AddRectFilled(tabRect.Min, tabRect.Max, ImGui::GetColorU32(baseCol), tabSize.y * 0.5f);

            // icon: filled circle + first letter
            if (w > 0.0f)
            {
                ImVec2 iconCenter(tabRect.Min.x + iconWidth * 0.5f, tabRect.Min.y + tabSize.y * 0.5f);
                float radius = iconWidth * 0.35f;

                ImVec4 iconCol = style.Colors[ImGuiCol_ButtonActive];
                iconCol.w *= ImClamp(t.alpha, 0.85f, 1.0f);
                dl->AddCircleFilled(iconCenter, radius, ImGui::GetColorU32(iconCol), 24);

                if (!t.label.empty())
                {
                    char c[2] = { t.label[0], 0 };
                    ImVec2 cs = ImGui::CalcTextSize(c);
                    dl->AddText(ImVec2(iconCenter.x - cs.x * 0.5f, iconCenter.y - cs.y * 0.5f),
                        IM_COL32(10, 10, 10, 255), c);
                }
            }

            if (t.alpha > 0.01f)
            {
                ImVec2 labelSize = ImGui::CalcTextSize(t.label.c_str(), nullptr, true);
                float scaledX = labelSize.x * t.scale;
                float scaledY = labelSize.y * t.scale;

                float textX = tabRect.Min.x + iconWidth + style.FramePadding.x +
                    ((w - iconWidth - style.FramePadding.x * 2.0f - scaledX) * 0.5f);
                float textY = tabRect.Min.y + (tabSize.y - scaledY) * 0.5f;

                dl->AddText(ImVec2(textX, textY), ImGui::GetColorU32(style.Colors[ImGuiCol_Text]), t.label.c_str());
            }

            x += w + spacing;
        }

        if (bar.activeIndex >= 0 && bar.activeIndex < (int)bar.tabs.size())
        {
            float xAccum = barPos.x;
            for (int i = 0; i < bar.activeIndex; ++i)
                xAccum += bar.tabs[i].currentWidth + spacing;

            AnimatedTab& activeTab = bar.tabs[bar.activeIndex];
            bar.indicatorTargetX = xAccum + activeTab.offsetX;
            bar.indicatorTargetWidth = activeTab.currentWidth;
        }

        bar.indicatorCurrentX = AnimLerp(bar.indicatorCurrentX, bar.indicatorTargetX, animSpeed, dt);
        bar.indicatorCurrentWidth = AnimLerp(bar.indicatorCurrentWidth, bar.indicatorTargetWidth, animSpeed, dt);

        if (bar.indicatorCurrentWidth > 2.0f)
        {
            float underlineHeight = 3.0f;
            ImRect ul(
                ImVec2(bar.indicatorCurrentX, barPos.y + barHeight - underlineHeight - 2.0f),
                ImVec2(bar.indicatorCurrentX + bar.indicatorCurrentWidth, barPos.y + barHeight - 2.0f)
            );

            dl->AddRectFilled(ul.Min, ul.Max, ImGui::GetColorU32(colors::SecondColor), underlineHeight * 0.5f);
        }

        // dirty dot on active tab (overlay only, doesn't affect layout)
        if (dirtyActive && bar.activeIndex >= 0 && bar.activeIndex < (int)bar.tabs.size())
        {
            AnimatedTab& a = bar.tabs[bar.activeIndex];
            if (a.lastRectValid)
            {
                const float r = 4.0f;
                ImVec2 p(a.lastRect.Max.x - 10.0f, a.lastRect.Min.y + 8.0f);
                dl->AddCircleFilled(p, r + 2.0f, ImGui::GetColorU32(ImVec4(colors::SecondColor.x, colors::SecondColor.y, colors::SecondColor.z, 0.25f)), 24); // glow
                dl->AddCircleFilled(p, r, ImGui::GetColorU32(colors::SecondColor), 24);
                dl->AddCircle(p, r, ImGui::GetColorU32(ImVec4(0, 0, 0, 0.35f)), 24, 1.0f);
            }
        }


        return result;
    }

    // SettingRightType, SettingRowSpec, SettingRowResult — see ui_internal.h

    const char* g_settings_scroll_target = nullptr;

    SettingRowResult RenderSettingRow(
        const SettingRowSpec& r,
        ImGuiID* selected_id,            // your selection state
        float row_h)
    {
        SettingRowResult out{};

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (window->SkipItems) return out;

        const float fullW = ImGui::GetContentRegionAvail().x;
        if (fullW <= 1.0f) return out;

        const bool is_selected = (selected_id && *selected_id == r.id);

        ImVec2 rowMin = ImGui::GetCursorScreenPos();
        ImRect rowRect(rowMin, ImVec2(rowMin.x + fullW, rowMin.y + row_h));
        ImGui::ItemSize(ImVec2(fullW, row_h));

        // check before clipping so off-screen rows can still scroll into view
        if (g_settings_scroll_target && r.title && strcmp(r.title, g_settings_scroll_target) == 0)
        {
            ImGui::SetScrollHereY(0.3f);
            g_settings_scroll_target = nullptr;
        }

        ImGui::PushID((int)r.id);

        ImGuiID rowItemId = ImGui::GetID("##row_hit");
        if (!ImGui::ItemAdd(rowRect, rowItemId))
        {
            ImGui::PopID();
            return out;
        }

        float rightWidgetW = 170.0f;
        if (r.rightType == SettingRightType::Combo || r.rightType == SettingRightType::AnimatedCombo)
            rightWidgetW = ImMax(rightWidgetW, r.comboWidth + 20.0f);
        else if (r.rightType == SettingRightType::Slider)
            rightWidgetW = ImMax(rightWidgetW, r.sliderWidth + 20.0f);
        const float rightColStart = rowRect.Max.x - rightWidgetW - 12.0f;
        bool mouseInRightCol = (ImGui::GetMousePos().x >= rightColStart);
        bool hasInteractiveRight = (r.rightType == SettingRightType::Toggle ||
                                    r.rightType == SettingRightType::Button ||
                                    r.rightType == SettingRightType::Ellipsis ||
                                    r.rightType == SettingRightType::Slider ||
                                    r.rightType == SettingRightType::Combo ||
                                    r.rightType == SettingRightType::AnimatedCombo);

        bool hovered = false, held = false;
        bool pressed = false;

        // don't eat clicks on interactive right-side controls
        if (!(hasInteractiveRight && mouseInRightCol))
        {
            pressed = ImGui::ButtonBehavior(rowRect, rowItemId, &hovered, &held);
        }
        else
        {
            hovered = ImGui::IsMouseHoveringRect(rowRect.Min, rowRect.Max);
        }

        if (is_selected)
        {
            ImU32 bg = ImGui::GetColorU32(ImGuiCol_Header, 0.5f);
            ImGui::GetWindowDrawList()->AddRectFilled(rowRect.Min, rowRect.Max, bg, 0.0f);
        }

        const float padX = 18.0f;
        const float padY = 10.0f;
        ImVec2 contentStart(rowRect.Min.x + padX, rowRect.Min.y + padY);

        ImGui::SetCursorScreenPos(contentStart);

        bool rightHoveredOrActive = false;

        const float contentH = row_h - padY * 2.0f;

        ImGuiTableFlags tf =
            ImGuiTableFlags_SizingFixedFit |
            ImGuiTableFlags_NoPadOuterX |
            ImGuiTableFlags_NoBordersInBody;

        if (ImGui::BeginTable("##row_tbl", 3, tf, ImVec2(fullW - padX * 2.0f, contentH)))
        {
            ImGui::TableSetupColumn("icon", ImGuiTableColumnFlags_WidthFixed, 26.0f);
            ImGui::TableSetupColumn("text", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("right", ImGuiTableColumnFlags_WidthFixed, 170.0f);

            ImGui::TableNextRow(ImGuiTableRowFlags_None, contentH);

            ImGui::TableSetColumnIndex(0);
            if (r.leftIcon && r.leftIcon[0])
            {
                float y = ImGui::GetCursorPosY();
                ImGui::SetCursorPosY(y + (contentH - ImGui::GetTextLineHeight()) * 0.5f);
                ImGui::TextUnformatted(r.leftIcon);
            }

            ImGui::TableSetColumnIndex(1);
            {
                float titleH = render::FontRegular ? render::FontRegular->LegacySize : ImGui::GetTextLineHeight();
                float subH = (r.subtitle && r.subtitle[0] && render::FontSmall) ? render::FontSmall->LegacySize : 0.0f;
                float spacing = (r.subtitle && r.subtitle[0]) ? ImGui::GetStyle().ItemSpacing.y : 0.0f;
                float totalTextH = titleH + subH + spacing;

                float y = ImGui::GetCursorPosY();
                float offset = (contentH - totalTextH) * 0.5f;
                if (offset > 0) ImGui::SetCursorPosY(y + offset);

                ImGui::BeginGroup();
                ImGui::PushFont(render::FontRegular);
                ImGui::TextUnformatted(r.title ? r.title : "");
                ImGui::PopFont();
                if (r.subtitle && r.subtitle[0])
                {
                    ImGui::PushFont(render::FontSmall);
                    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                    ImGui::TextUnformatted(r.subtitle);
                    ImGui::PopStyleColor();
                    ImGui::PopFont();
                }
                ImGui::EndGroup();
            }

            ImGui::TableSetColumnIndex(2);
            {
                float colW = ImGui::GetColumnWidth();
                float curX = ImGui::GetCursorPosX();

                float wantW = 0.0f;
                switch (r.rightType)
                {
                case SettingRightType::Toggle:   wantW = 48.0f; break;
                case SettingRightType::Button:   wantW = 80.0f; break;
                case SettingRightType::Chevron:  wantW = 18.0f + (r.rightText ? ImGui::CalcTextSize(r.rightText).x + 6.0f : 0.0f); break;
                case SettingRightType::Ellipsis: wantW = 30.0f; break;
                case SettingRightType::Text:     wantW = (r.rightText ? ImGui::CalcTextSize(r.rightText).x : 0.0f); break;
                case SettingRightType::Slider:   wantW = r.sliderWidth + 12.0f; break; // +12 for right padding
                case SettingRightType::Combo:    wantW = r.comboWidth + 12.0f; break;
                case SettingRightType::AnimatedCombo: wantW = r.comboWidth + 12.0f; break;
                default: break;
                }

                if (wantW > 0.0f)
                    ImGui::SetCursorPosX(curX + (colW - wantW - 8.0f));

                float y = ImGui::GetCursorPosY();
                ImGui::SetCursorPosY(y + (contentH - 28.0f) * 0.5f);

                ImGui::BeginDisabled(!r.enabled);

                if (r.rightType == SettingRightType::Toggle && r.toggleValue)
                {
                    if (ToggleSwitch("##tgl", r.toggleValue))
                        out.action_used = true;

                    rightHoveredOrActive |= ImGui::IsItemHovered() || ImGui::IsItemActive();
                }
                else if (r.rightType == SettingRightType::Button && r.buttonLabel)
                {
                    if (StyledButtonLight("##setting_btn", r.buttonLabel, ImVec2(80, 28)))
                        out.action_used = true;

                    rightHoveredOrActive |= ImGui::IsItemHovered() || ImGui::IsItemActive();
                }
                else if (r.rightType == SettingRightType::Ellipsis)
                {
                    if (StyledButtonLight("##setting_ellipsis", "...", ImVec2(30, 28)))
                        out.action_used = true;

                    rightHoveredOrActive |= ImGui::IsItemHovered() || ImGui::IsItemActive();
                }
                else if (r.rightType == SettingRightType::Chevron)
                {
                    if (r.rightText)
                    {
                        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
                        ImGui::TextUnformatted(r.rightText);
                        ImGui::PopStyleColor();
                        ImGui::SameLine(0, 6.0f);
                    }
                    ImGui::TextUnformatted(">");
                }
                else if (r.rightType == SettingRightType::Text && r.rightText)
                {
                    ImGui::TextUnformatted(r.rightText);
                }
                else if (r.rightType == SettingRightType::Slider && r.sliderValue)
                {
                    ImGui::SetNextItemWidth(r.sliderWidth);
                    if (ImGui::SliderInt5(" ", r.sliderValue, r.sliderMin, r.sliderMax, r.sliderFormat))
                        out.action_used = true;

                    rightHoveredOrActive |= ImGui::IsItemHovered() || ImGui::IsItemActive();
                }
                else if (r.rightType == SettingRightType::Slider && r.sliderValueF)
                {
                    ImGui::SetNextItemWidth(r.sliderWidth);
                    if (ImGui::SliderScalar2(" ", ImGuiDataType_Float, r.sliderValueF, &r.sliderMinF, &r.sliderMaxF, r.sliderFormatF, 0))
                        out.action_used = true;

                    rightHoveredOrActive |= ImGui::IsItemHovered() || ImGui::IsItemActive();
                }
                else if (r.rightType == SettingRightType::Combo && r.comboItems && r.comboIndex)
                {
                    ImGui::SetNextItemWidth(r.comboWidth);
                    if (ImGui::BeginCombo("##combo", r.comboItems[*r.comboIndex], ImGuiComboFlags_HeightSmall))
                    {
                        for (int i = 0; i < r.comboCount; ++i)
                        {
                            bool selected = (i == *r.comboIndex);
                            if (ImGui::Selectable(r.comboItems[i], selected))
                            {
                                *r.comboIndex = i;
                                out.action_used = true;
                            }
                            if (selected)
                                ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    rightHoveredOrActive |= ImGui::IsItemHovered() || ImGui::IsItemActive();
                }
                else if (r.rightType == SettingRightType::AnimatedCombo && r.comboItems && r.comboIndex)
                {
                    float comboX = rowRect.Max.x - padX - r.comboWidth - 8.0f;
                    float comboY = rowRect.Min.y + (row_h - r.comboHeight) * 0.5f;
                    ImGui::SetCursorScreenPos(ImVec2(comboX, comboY));
                    if (AnimatedComboDot("##anim_combo", r.comboItems[*r.comboIndex], r.comboItems, r.comboCount, r.comboIndex, r.comboWidth, r.comboHeight, true))
                        out.action_used = true;
                    rightHoveredOrActive |= ImGui::IsItemHovered() || ImGui::IsItemActive();
                }

                ImGui::EndDisabled();
            }

            ImGui::EndTable();
        }

        if (pressed && !rightHoveredOrActive)
        {
            if (selected_id)
                *selected_id = r.id;
            out.row_selected = true;
        }

        ImGui::PopID();
        return out;
    }

    int g_category_card_id = 0;
    static ImDrawListSplitter s_cardSplitter;
    static ImDrawList* s_cardDL = nullptr;
    static ImVec2 s_cardStartPos;

    void BeginCategoryCard(const char* title, const char* subtitle)
    {
        ImGui::PushFont(render::FontLarge);
        ImGui::TextUnformatted(title);
        ImGui::PopFont();
        if (subtitle && subtitle[0])
        {
            ImGui::PushFont(render::FontSmall);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(subtitle);
            ImGui::PopStyleColor();
            ImGui::PopFont();
        }
        ImGui::Dummy(ImVec2(0, 1));

        // record top before content so EndCategoryCard can draw bg behind it
        s_cardStartPos = ImGui::GetCursorScreenPos();
        s_cardDL = ImGui::GetWindowDrawList();
        s_cardSplitter.Split(s_cardDL, 2);
        s_cardSplitter.SetCurrentChannel(s_cardDL, 1);
    }

    void EndCategoryCard()
    {
        ImGui::Dummy(ImVec2(0, 6));

        float cardW = ImGui::GetContentRegionAvail().x - 6.0f;
        ImVec2 cardMax(s_cardStartPos.x + cardW, ImGui::GetCursorScreenPos().y);
        s_cardSplitter.SetCurrentChannel(s_cardDL, 0);
        s_cardDL->AddRectFilled(s_cardStartPos, cardMax, ImGui::GetColorU32(GetCardBg()), 8.0f);
        s_cardSplitter.Merge(s_cardDL);
        s_cardDL = nullptr;

        ImGui::Dummy(ImVec2(0, 10));
    }


    void DrawRowDivider(float rightMargin)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x - rightMargin;

        ImU32 divCol = IsDarkTheme() ? IM_COL32(255, 255, 255, 25) : IM_COL32(0, 0, 0, 30);
        dl->AddLine(ImVec2(a.x, a.y), ImVec2(a.x + w, a.y), divCol, 1.0f);
        ImGui::Dummy(ImVec2(0, 4));
    }

    void DrawRowDivider()
    {
        DrawRowDivider(6.0f);
    }

    struct InputState {
        ImVec4 TextColor;
        ImVec4 ShadowColor;
        float Shadow_size;
    };

    bool InputText3(const char* label,
        char* buf,
        size_t buf_size,
        ImGuiInputTextFlags flags,
        ImGuiInputTextCallback callback,
        void* user_data)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (!window) return false;

        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = window->DrawList;

        // split "Visible##id" — pass only the ##id part to ImGui so it draws no right-side label
        const char* hash = strstr(label, "##");

        std::string visible;
        std::string id_storage;
        const char* id_label = nullptr;

        if (hash)
        {
            if (hash != label)
                visible.assign(label, size_t(hash - label));
            else
                visible.clear();

            id_label = hash;
        }
        else
        {
            visible = label;
            id_storage = "##";
            id_storage += label;
            id_label = id_storage.c_str();
        }

        const float W = ImGui::CalcItemWidth();
        const float H = 28.0f;

        ImVec2 pos = window->DC.CursorPos;
        ImRect bb(pos, ImVec2(pos.x + W, pos.y + H));

        ImGui::Dummy(ImVec2(W, H));
        ImGui::SetCursorScreenPos(bb.Min);

        ImGuiID id = window->GetID(id_label);

        // window state storage avoids memory leaks from static maps
        ImGuiStorage* storage = window->DC.StateStorage;
        const ImGuiID t_id = id + 1;
        const ImGuiID glow_id = id + 2;
        float t = storage->GetFloat(t_id, 0.0f);
        float glow = storage->GetFloat(glow_id, 0.0f);
        
        bool has_text = (buf && buf[0] != '\0');

        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));

        // guard against null/zero buf — ImGui will crash without this
        if (!buf || buf_size < 2)
        {
            IM_ASSERT(false && "InputText3 called with invalid buf/buf_size. Set a breakpoint here.");
            static char s_fallback[2] = { 0, 0 };
            buf = s_fallback;
            buf_size = 2;
        }

        bool changed = ImGui::InputTextEx(
            id_label,  // ID-only: no right-side label rendered by ImGui
            nullptr,
            buf,
            (int)buf_size,
            ImVec2(W, H),
            flags,
            callback,
            user_data
        );

        bool active = ImGui::IsItemActive();
        bool hovered = ImGui::IsItemHovered();

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();

        if (has_text)
        {
            const float underlineY = bb.Max.y - 8.0f;
            const float fieldH = underlineY - bb.Min.y;
            const float clearSz = 18.0f;
            const float clearPad = 2.0f;
            ImVec2 clearCenter(bb.Max.x - clearSz * 0.5f - clearPad,
                               bb.Min.y + fieldH * 0.5f);
            ImRect clearRect(ImVec2(clearCenter.x - clearSz * 0.5f, clearCenter.y - clearSz * 0.5f),
                             ImVec2(clearCenter.x + clearSz * 0.5f, clearCenter.y + clearSz * 0.5f));
            bool clearHov = ImGui::IsMouseHoveringRect(clearRect.Min, clearRect.Max);

            if (clearHov)
            {
                ImU32 circleBg = IsDarkTheme() ? IM_COL32(255, 255, 255, 25)
                                               : IM_COL32(0, 0, 0, 20);
                dl->AddCircleFilled(clearCenter, clearSz * 0.5f, circleBg);
            }

            ImU32 clearCol = clearHov
                ? (IsDarkTheme() ? IM_COL32(255, 255, 255, 230) : IM_COL32(30, 30, 30, 230))
                : (IsDarkTheme() ? IM_COL32(255, 255, 255, 100) : IM_COL32(80, 80, 80, 140));
            ImVec2 ts = ImGui::CalcTextSize(ICON_MDI_CLOSE);
            dl->AddText(
                ImVec2(clearCenter.x - ts.x * 0.5f, clearCenter.y - ts.y * 0.5f),
                clearCol, ICON_MDI_CLOSE);

            if (clearHov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                buf[0] = '\0';
                has_text = false;
                changed = true;
                ImGui::ClearActiveID();
            }
        }

        float float_target = (active || hovered || has_text) ? 1.0f : 0.0f;
        t = ImLerp(t, float_target, io.DeltaTime * 12.0f);

        float glow_target = active ? 1.0f : (hovered ? 0.4f : 0.0f);
        glow = ImLerp(glow, glow_target, io.DeltaTime * 10.0f);

        storage->SetFloat(t_id, t);
        storage->SetFloat(glow_id, glow);

        if (!visible.empty())
        {
            const float inside_y = bb.Min.y + (H - ImGui::GetTextLineHeight()) * 0.5f;
            const float outside_y = bb.Min.y - 10.0f;

            float label_y = ImLerp(inside_y, outside_y, t);
            float scale = ImLerp(1.0f, 0.85f, t);

            ImVec4 labelTarget = IsDarkTheme() ? colors::White : ImVec4(0.10f, 0.10f, 0.10f, 1.0f);
            ImVec4 col = ImLerp(colors::Gray, labelTarget, t);
            dl->AddText(nullptr,
                ImGui::GetFontSize() * scale,
                ImVec2(bb.Min.x + 4.0f, label_y),
                ImGui::GetColorU32(col),
                visible.c_str());
        }

        ImVec2 a(bb.Min.x + 2.0f, bb.Max.y - 8.0f); // was -3.0f
        ImVec2 b(bb.Max.x - 2.0f, bb.Max.y - 8.0f);

        ImVec4 underlineCol = IsDarkTheme() ? ImVec4(1, 1, 1, 0.25f) : ImVec4(0, 0, 0, 0.20f);
        dl->AddLine(a, b, ImGui::GetColorU32(underlineCol), 1.0f);

        if (glow > 0.001f)
        {
            ImVec4 g = colors::MainColor;
            g.w = 0.15f * glow;
            dl->AddLine(a, b, ImGui::GetColorU32(g), 4.0f);

            g.w = 0.06f * glow;
            dl->AddLine(a, b, ImGui::GetColorU32(g), 7.0f);
        }

        return changed;
    }

    // Right-click paste for the string-backed inputs. The old version memcpy'd
    // into the string's existing buffer capped at buf_size — a fresh libc++ SSO
    // string is ~21 bytes, so long pastes (a generated password) got truncated.
    // Go through the std::string so it grows, insert at the caret, then reload
    // ImGui's buffer so the edit isn't reverted while the field's focused.
    static std::unordered_map<ImGuiID, ImVec2> s_input_caret;  // x=selStart, y=selEnd

    static void RememberCaret(ImGuiID id)
    {
        // popup steals focus and can drop the live state, so stash it while active
        if (ImGuiInputTextState* st = ImGui::GetInputTextState(id))
            s_input_caret[id] = ImVec2((float)st->GetSelectionStart(),
                                       (float)st->GetSelectionEnd());
    }

    static bool PasteAtCaret(std::string* str, ImGuiID id)
    {
        const char* clip = ImGui::GetClipboardText();
        if (!clip || !*clip) return false;

        const int len = (int)str->size();
        int a = len, b = len;   // no caret remembered -> append at end
        auto it = s_input_caret.find(id);
        if (it != s_input_caret.end())
        {
            const int s0 = (int)it->second.x, s1 = (int)it->second.y;
            a = ImClamp(ImMin(s0, s1), 0, len);
            b = ImClamp(ImMax(s0, s1), 0, len);
        }

        str->replace((size_t)a, (size_t)(b - a), clip);   // grows as needed
        const int caret = a + (int)strlen(clip);

        if (ImGuiInputTextState* st = ImGui::GetInputTextState(id))
        {
            st->WantReloadUserBuf    = true;
            st->ReloadSelectionStart = caret;
            st->ReloadSelectionEnd   = caret;
        }
        s_input_caret[id] = ImVec2((float)caret, (float)caret);
        return true;
    }

    bool InputText4(const char* label,
        char* buf,
        size_t buf_size,
        ImGuiInputTextFlags flags,
        ImGuiInputTextCallback callback,
        void* user_data,
        std::string* str_backing,
        float reserveRight)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (!window) return false;

        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = window->DrawList;

        // split "Visible##id" — same pattern as InputText3
        const char* hash = strstr(label, "##");

        std::string visible;
        std::string id_storage;
        const char* id_label = nullptr;

        if (hash)
        {
            if (hash != label)
                visible.assign(label, size_t(hash - label));
            else
                visible.clear();

            id_label = hash;
        }
        else
        {
            visible = label;
            id_storage = "##";
            id_storage += label;
            id_label = id_storage.c_str();
        }

        const float W = ImGui::CalcItemWidth();
        const float fieldH = 34.0f;
        const float rounding = 6.0f;
        const float labelFontScale = 0.82f;
        const float labelH = ImGui::GetFontSize() * labelFontScale;
        const float labelPadTop = 2.0f;
        const float totalH = labelH + labelPadTop + fieldH;

        ImVec2 pos = window->DC.CursorPos;
        ImRect fieldBB(ImVec2(pos.x, pos.y + labelH + labelPadTop),
                       ImVec2(pos.x + W, pos.y + totalH));

        // BeginGroup so EndGroup registers full widget bounds — prevents InputTextEx
        // from overwriting line tracking (fixes SameLine after this widget)
        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(W, totalH));

        ImGuiID id = window->GetID(id_label);
        ImGuiStorage* storage = window->DC.StateStorage;
        const ImGuiID t_id = id + 1;
        const ImGuiID glow_id = id + 2;
        float t = storage->GetFloat(t_id, 0.0f);
        float glow = storage->GetFloat(glow_id, 0.0f);

        bool has_text = (buf && buf[0] != '\0');

        bool dark = IsDarkTheme();

        // lift shadow — slightly right-biased for depth
        {
            float rightBias = 1.0f;
            float shadowOff = 1.0f;
            ImU32 s1 = dark ? IM_COL32(0, 0, 0, 22) : IM_COL32(0, 0, 0, 12);
            dl->AddRectFilled(
                ImVec2(fieldBB.Min.x + rightBias, fieldBB.Min.y + shadowOff),
                ImVec2(fieldBB.Max.x + rightBias, fieldBB.Max.y + shadowOff),
                s1, rounding);
        }

        ImU32 fieldBg = dark ? theme::PanelBg.dark : theme::PanelBg.light;

        dl->AddRectFilled(fieldBB.Min, fieldBB.Max, fieldBg, rounding);

        // top-left inner shine: left edge + arc + top edge
        {
            ImU32 shineCol = dark ? IM_COL32(255, 255, 255, 8) : IM_COL32(255, 255, 255, 180);
            ImU32 sideCol  = dark ? IM_COL32(255, 255, 255, 5) : IM_COL32(255, 255, 255, 100);
            float inset = 1.5f;
            float r = rounding - inset;
            dl->PathLineTo(ImVec2(fieldBB.Min.x + inset, fieldBB.Max.y - rounding));
            dl->PathLineTo(ImVec2(fieldBB.Min.x + inset, fieldBB.Min.y + rounding));
            dl->PathStroke(sideCol, 0, 1.0f);
            dl->PathArcTo(ImVec2(fieldBB.Min.x + rounding, fieldBB.Min.y + rounding), r, IM_PI, IM_PI * 1.5f, 8);
            dl->PathStroke(shineCol, 0, 1.0f);
            dl->PathLineTo(ImVec2(fieldBB.Min.x + rounding, fieldBB.Min.y + inset));
            dl->PathLineTo(ImVec2(fieldBB.Max.x - rounding, fieldBB.Min.y + inset));
            dl->PathStroke(shineCol, 0, 1.0f);
        }

        // outline: subtle at rest, accent on focus
        ImVec4 borderRest = dark ? ImVec4(1, 1, 1, 0.14f) : ImVec4(0, 0, 0, 0.18f);
        ImVec4 borderFocus = ImVec4(colors::SecondColor.x, colors::SecondColor.y,
                                    colors::SecondColor.z, 0.85f);
        ImVec4 borderCol = ImLerp(borderRest, borderFocus, glow);
        float borderThickness = ImLerp(1.0f, 1.6f, glow);
        dl->AddRect(fieldBB.Min, fieldBB.Max,
                    ImGui::GetColorU32(borderCol), rounding, 0, borderThickness);

        if (glow > 0.01f)
        {
            ImVec4 outerGlow = ImVec4(colors::SecondColor.x, colors::SecondColor.y,
                                      colors::SecondColor.z, 0.08f * glow);
            dl->AddRect(ImVec2(fieldBB.Min.x - 1, fieldBB.Min.y - 1),
                        ImVec2(fieldBB.Max.x + 1, fieldBB.Max.y + 1),
                        ImGui::GetColorU32(outerGlow), rounding + 1, 0, 2.0f);
        }

        const float textPadX = 10.0f;
        ImGui::SetCursorScreenPos(ImVec2(fieldBB.Min.x + textPadX,
                                         fieldBB.Min.y));

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(0, (fieldH - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));

        if (!buf || buf_size < 2)
        {
            IM_ASSERT(false && "InputText4 called with invalid buf/buf_size.");
            static char s_fallback[2] = { 0, 0 };
            buf = s_fallback;
            buf_size = 2;
        }

        float inputW = W - textPadX * 2 - (has_text ? 20.0f : 0.0f) - reserveRight; // reserve room for clear btn (+ embedded trailing widget)

        bool changed = ImGui::InputTextEx(
            id_label,
            nullptr,
            buf,
            (int)buf_size,
            ImVec2(inputW, fieldH),
            flags,
            callback,
            user_data
        );

        bool active = ImGui::IsItemActive();
        bool hovered = ImGui::IsItemHovered();
        RememberCaret(id);

        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(3);

        {
            char ctx_id[64];
            snprintf(ctx_id, sizeof(ctx_id), "##input4_ctx_%u", id);
            ImGui::OpenPopupOnItemClick(ctx_id, ImGuiPopupFlags_MouseButtonRight);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
            if (ImGui::BeginPopup(ctx_id))
            {
                if (ImGui::MenuItem(ICON_MDI_CONTENT_COPY "  Copy"))
                {
                    if (buf[0] != '\0')
                        ImGui::SetClipboardText(buf);
                }
                if (ImGui::MenuItem(ICON_MDI_CONTENT_PASTE "  Paste"))
                {
                    if (str_backing)
                    {
                        if (PasteAtCaret(str_backing, id))
                        {
                            buf = (char*)str_backing->data();  // may have reallocated
                            changed = true;
                        }
                    }
                    else if (const char* clip = ImGui::GetClipboardText())
                    {
                        // fixed-buffer fallback (no current callers)
                        size_t clip_len = strlen(clip);
                        size_t cur_len = strlen(buf);
                        size_t space = buf_size - 1 - cur_len;
                        if (clip_len > space) clip_len = space;
                        if (clip_len > 0)
                        {
                            memcpy(buf + cur_len, clip, clip_len);
                            buf[cur_len + clip_len] = '\0';
                            changed = true;
                        }
                    }
                }
                if (ImGui::MenuItem(ICON_MDI_KEYBOARD "  On-Screen Keyboard"))
                {
                    g_show_osk = !g_show_osk;
                }

                // gradient border on popup window
                {
                    const float r = 8.0f;
                    ImVec2 pMin = ImGui::GetWindowPos();
                    ImVec2 pMax = ImVec2(pMin.x + ImGui::GetWindowSize().x, pMin.y + ImGui::GetWindowSize().y);
                    ImDrawList* pdl = ImGui::GetWindowDrawList();
                    ImU32 colTL = theme::BevelTL;
                    ImU32 colBR = theme::BevelBR;
                    ImU32 colTR = theme::BevelTR;
                    ImU32 colBL = theme::BevelBL;
                    pdl->AddRectFilledMultiColor(
                        ImVec2(pMin.x + r, pMin.y),
                        ImVec2(pMax.x - r, pMin.y + 1.0f),
                        colTL, colBR, colBR, colTL);
                    pdl->AddRectFilled(
                        ImVec2(pMin.x + r, pMax.y - 1.0f),
                        ImVec2(pMax.x - r, pMax.y),
                        colBR);
                    pdl->AddRectFilledMultiColor(
                        ImVec2(pMin.x, pMin.y + r),
                        ImVec2(pMin.x + 1.0f, pMax.y - r),
                        colTL, colTL, colBR, colBR);
                    pdl->AddRectFilledMultiColor(
                        ImVec2(pMax.x - 1.0f, pMin.y + r),
                        ImVec2(pMax.x, pMax.y - r),
                        colBR, colBR, colBR, colBR);
                    float cr = r - 0.5f;
                    pdl->PathArcTo(ImVec2(pMin.x + r, pMin.y + r), cr, IM_PI, IM_PI * 1.5f, 8);
                    pdl->PathStroke(colTL, 0, 1.0f);
                    pdl->PathArcTo(ImVec2(pMax.x - r, pMin.y + r), cr, IM_PI * 1.5f, IM_PI * 2.0f, 8);
                    pdl->PathStroke(colTR, 0, 1.0f);
                    pdl->PathArcTo(ImVec2(pMin.x + r, pMax.y - r), cr, IM_PI * 0.5f, IM_PI, 8);
                    pdl->PathStroke(colBL, 0, 1.0f);
                    pdl->PathArcTo(ImVec2(pMax.x - r, pMax.y - r), cr, 0.0f, IM_PI * 0.5f, 8);
                    pdl->PathStroke(colBR, 0, 1.0f);
                }

                ImGui::EndPopup();
            }
            ImGui::PopStyleVar(4);
        }

        if (has_text)
        {
            const float clearSz = 18.0f;
            const float clearPad = 8.0f;
            ImVec2 clearCenter(fieldBB.Max.x - clearSz * 0.5f - clearPad - reserveRight,
                fieldBB.Min.y + fieldH * 0.5f);
            ImRect clearRect(ImVec2(clearCenter.x - clearSz * 0.5f,
                                    clearCenter.y - clearSz * 0.5f),
                             ImVec2(clearCenter.x + clearSz * 0.5f,
                                    clearCenter.y + clearSz * 0.5f));
            bool clearHov = ImGui::IsMouseHoveringRect(clearRect.Min, clearRect.Max);

            if (clearHov)
            {
                ImU32 circleBg = dark ? IM_COL32(255, 255, 255, 25)
                                      : IM_COL32(0, 0, 0, 20);
                dl->AddCircleFilled(clearCenter, clearSz * 0.5f, circleBg);
            }

            ImU32 clearCol = clearHov
                ? (dark ? IM_COL32(255, 255, 255, 230) : IM_COL32(30, 30, 30, 230))
                : (dark ? IM_COL32(255, 255, 255, 100) : IM_COL32(80, 80, 80, 140));
            ImVec2 ts = ImGui::CalcTextSize(ICON_MDI_CLOSE_CIRCLE);
            dl->AddText(
                ImVec2(clearCenter.x - ts.x * 0.5f, clearCenter.y - ts.y * 0.5f),
                clearCol, ICON_MDI_CLOSE_CIRCLE);

            if (clearHov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                buf[0] = '\0';
                has_text = false;
                changed = true;
                ImGui::ClearActiveID();
            }
        }

        float float_target = (active || hovered || has_text) ? 1.0f : 0.0f;
        t = ImLerp(t, float_target, io.DeltaTime * 12.0f);

        float glow_target = active ? 1.0f : (hovered ? 0.4f : 0.0f);
        glow = ImLerp(glow, glow_target, io.DeltaTime * 10.0f);

        storage->SetFloat(t_id, t);
        storage->SetFloat(glow_id, glow);

        // floating label: centered inside at rest, floats above border when active/filled
        if (!visible.empty())
        {
            const float inside_y = fieldBB.Min.y + (fieldH - ImGui::GetTextLineHeight()) * 0.5f;
            const float outside_y = fieldBB.Min.y - labelH - 2.0f;

            float label_y = ImLerp(inside_y, outside_y, t);
            float label_x = ImLerp(fieldBB.Min.x + textPadX, fieldBB.Min.x + 1.0f, t);
            float scale = ImLerp(1.0f, labelFontScale, t);

            ImVec4 restCol = dark ? ImVec4(1, 1, 1, 0.35f)
                                  : ImVec4(0, 0, 0, 0.38f);
            ImVec4 activeCol = dark ? ImVec4(1, 1, 1, 0.75f)
                                    : ImVec4(0.10f, 0.10f, 0.10f, 0.85f);
            ImVec4 col = ImLerp(restCol, activeCol, t);

            dl->AddText(nullptr,
                ImGui::GetFontSize() * scale,
                ImVec2(label_x, label_y),
                ImGui::GetColorU32(col),
                visible.c_str());
        }

        ImGui::EndGroup();
        return changed;
    }

    struct InputTextStringUserData
    {
        std::string* Str = nullptr;
    };

    static int InputTextCallback_Resize(ImGuiInputTextCallbackData* data)
    {
        if (data->EventFlag != ImGuiInputTextFlags_CallbackResize)
            return 0;

        auto* ud = (InputTextStringUserData*)data->UserData;
        IM_ASSERT(ud && ud->Str);

        std::string* str = ud->Str;

        // BufSize includes null terminator; keep string size == actual text length
        str->reserve((size_t)data->BufSize);
        str->resize((size_t)data->BufTextLen);

        data->Buf = (char*)str->data();
        return 0;
    }

    bool InputTextString(const char* label, std::string* str, ImGuiInputTextFlags flags, float reserveRight)
    {
        IM_ASSERT(str);

        flags |= ImGuiInputTextFlags_CallbackResize;

        if (str->capacity() < 2)
            str->reserve(64);
        if (str->capacity() < 2)
            str->reserve(2);

        // size = capacity-1 so buf_size = size+1 >= 2 always
        str->resize(str->capacity() - 1);

        InputTextStringUserData ud;
        ud.Str = str;

        char* buf = (char*)str->data();
        int buf_size = (int)str->size() + 1;

        IM_ASSERT(buf != nullptr);
        IM_ASSERT(buf_size >= 2);

        bool changed = ui::InputText4(
            label,
            buf,
            (size_t)buf_size,
            flags,
            InputTextCallback_Resize,
            &ud,
            str,
            reserveRight
        );

        str->resize(strlen(str->c_str()));
        return changed;
    }

    // plain input + chevron button that opens a filtered item list
    bool SearchableCombo(const char* label, std::string& value,
                         const std::vector<std::string>& items,
                         const std::unordered_map<std::string, int>* counts,
                         const char* hint)
    {
        bool changed = false;
        bool dark = IsDarkTheme();
        const float fieldH = 34.0f;  // InputText4 field box height
        const float chevW  = 30.0f;  // chevron hit-zone reserved inside the field box

        // The field spans the full width and the chevron lives *inside* its right
        // edge, so the two read as one merged control. reserveRight keeps the typed
        // text and the clear-x clear of the chevron zone.
        float totalW = ImGui::CalcItemWidth();
        ImGui::SetNextItemWidth(totalW);
        InputTextString(label, &value, 0, chevW);
        if (ImGui::IsItemDeactivatedAfterEdit())
            changed = true;

        // Field box geometry: InputText4 wraps itself in a group, so the item rect
        // spans the floating label + box; the box is the bottom fieldH of it.
        ImVec2 gmin = ImGui::GetItemRectMin();
        ImVec2 gmax = ImGui::GetItemRectMax();
        float boxTop = gmax.y - fieldH;
        ImVec2 zoneMin(gmax.x - chevW, boxTop);
        ImVec2 zoneMax(gmax.x, gmax.y);

        ImGui::PushID(label);

        // Invisible button over the chevron zone. Submitted last so external
        // IsItemActive()/Enter checks key off it exactly like the old trailing button.
        ImGui::SetCursorScreenPos(zoneMin);
        if (ImGui::InvisibleButton("##sc_btn", ImVec2(chevW, fieldH)))
            ImGui::OpenPopup("##sc_pick");
        bool chevHov = ImGui::IsItemHovered();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        if (chevHov)
        {
            ImU32 hovBg = dark ? IM_COL32(255, 255, 255, 14) : IM_COL32(0, 0, 0, 12);
            dl->AddRectFilled(ImVec2(zoneMin.x, zoneMin.y + 1.0f),
                              ImVec2(zoneMax.x - 1.5f, zoneMax.y - 1.0f),
                              hovBg, 6.0f, ImDrawFlags_RoundCornersRight);
        }

        ImVec2 chSz = ImGui::CalcTextSize(ICON_MDI_CHEVRON_DOWN);
        ImU32 chCol = chevHov ? ImGui::GetColorU32(ImGuiCol_Text)
                              : ImGui::GetColorU32(ImGuiCol_TextDisabled);
        dl->AddText(ImVec2(zoneMin.x + (chevW - chSz.x) * 0.5f,
                           boxTop + (fieldH - chSz.y) * 0.5f),
                    chCol, ICON_MDI_CHEVRON_DOWN);

        // Anchor the dropdown under the field, matched to its width.
        ImGui::SetNextWindowPos(ImVec2(gmin.x, gmax.y + 2.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(gmax.x - gmin.x, 0.0f),
                                            ImVec2(gmax.x - gmin.x, FLT_MAX));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 1.0f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, dark ? theme::ChildBg.dark : theme::ChildBg.light);
        ImGui::PushStyleColor(ImGuiCol_Border, dark ? IM_COL32(255,255,255,25) : IM_COL32(0,0,0,25));
        if (ImGui::BeginPopup("##sc_pick"))
        {
            for (const auto& item : items)
            {
                char display[256];
                if (counts)
                {
                    auto it = counts->find(item);
                    snprintf(display, sizeof(display), "%s  (%d)", item.c_str(),
                             it != counts->end() ? it->second : 0);
                }
                else
                    snprintf(display, sizeof(display), "%s", item.c_str());

                if (ImGui::Selectable(display, item == value))
                {
                    value = item;
                    changed = true;
                }
            }

            if (items.empty())
                ImGui::TextDisabled("No items");

            ImGui::EndPopup();
        }
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
        ImGui::PopID();

        return changed;
    }

    std::string StripNonDigits(const std::string& s)
    {
        std::string out;
        for (char ch : s)
            if (ch >= '0' && ch <= '9')
                out += ch;
        return out;
    }

    std::string FormatWithPattern(const std::string& digits, const char* pattern)
    {
        std::string out;
        size_t di = 0;
        for (const char* p = pattern; *p && di < digits.size(); ++p)
        {
            if (*p == '#')
                out += digits[di++];
            else
                out += *p;
        }
        return out;
    }

    struct InputTextFormattedUserData
    {
        std::string*  Str     = nullptr;
        const char*   Pattern = nullptr;
        int           PrevDigitCount = 0;
    };

    static int InputTextCallback_Format(ImGuiInputTextCallbackData* data)
    {
        auto* ud = (InputTextFormattedUserData*)data->UserData;

        if (data->EventFlag == ImGuiInputTextFlags_CallbackResize)
        {
            std::string* str = ud->Str;
            str->reserve((size_t)data->BufSize);
            str->resize((size_t)data->BufTextLen);
            data->Buf = (char*)str->data();
            return 0;
        }

        if (data->EventFlag == ImGuiInputTextFlags_CallbackCharFilter)
        {
            if (data->EventChar < '0' || data->EventChar > '9')
                return 1; // block non-digits
            return 0;
        }

        if (data->EventFlag == ImGuiInputTextFlags_CallbackEdit)
        {
            const char* pattern = ud->Pattern;

            int maxDigits = 0; // '#' chars in pattern
            for (const char* p = pattern; *p; ++p)
                if (*p == '#') ++maxDigits;

            std::string current(data->Buf, (size_t)data->BufTextLen);

            int digitsBefore = 0; // digits before cursor, used to reposition after reformat
            for (int i = 0; i < data->CursorPos && i < (int)current.size(); ++i)
                if (current[i] >= '0' && current[i] <= '9')
                    ++digitsBefore;

            std::string digits = StripNonDigits(current);

            // if digit count didn't change, user deleted a separator — eat the digit before cursor
            if ((int)digits.size() == ud->PrevDigitCount && (int)digits.size() > 0 && digitsBefore > 0)
            {
                digits.erase((size_t)digitsBefore - 1, 1);
                --digitsBefore;
            }

            if ((int)digits.size() > maxDigits)
                digits.resize((size_t)maxDigits);

            std::string formatted = FormatWithPattern(digits, pattern);

            // walk formatted string until we've counted digitsBefore digits, place cursor after
            int newCursor = (int)formatted.size(); // default: end
            int seen = 0;
            for (int i = 0; i < (int)formatted.size(); ++i)
            {
                if (formatted[i] >= '0' && formatted[i] <= '9')
                {
                    ++seen;
                    if (seen == digitsBefore)
                    {
                        newCursor = i + 1;
                        // skip trailing separators
                        while (newCursor < (int)formatted.size() &&
                               !(formatted[newCursor] >= '0' && formatted[newCursor] <= '9'))
                            ++newCursor;
                        break;
                    }
                }
            }
            if (digitsBefore == 0)
                newCursor = 0;

            ud->PrevDigitCount = (int)digits.size();

            int len = (int)formatted.size();
            memcpy(data->Buf, formatted.c_str(), (size_t)len + 1);
            data->BufTextLen = len;
            data->BufDirty   = true;
            data->CursorPos  = newCursor;
            *ud->Str = formatted;
        }

        return 0;
    }

    bool InputTextFormatted(const char* label, std::string* str, const char* pattern, ImGuiInputTextFlags flags)
    {
        IM_ASSERT(str);

        flags |= ImGuiInputTextFlags_CallbackResize
              |  ImGuiInputTextFlags_CallbackCharFilter
              |  ImGuiInputTextFlags_CallbackEdit;

        if (str->capacity() < 2)
            str->reserve(64);
        if (str->capacity() < 2)
            str->reserve(2);

        str->resize(str->capacity() - 1);

        ImGuiID id = ImGui::GetID(label);
        static std::unordered_map<ImGuiID, int> s_prev_digits; // per-widget backspace detection
        int& prevDigits = s_prev_digits[id];

        InputTextFormattedUserData ud;
        ud.Str            = str;
        ud.Pattern         = pattern;
        ud.PrevDigitCount  = prevDigits;

        char* buf      = (char*)str->data();
        int   buf_size = (int)str->size() + 1;

        IM_ASSERT(buf != nullptr);
        IM_ASSERT(buf_size >= 2);

        bool changed = ui::InputText4(
            label,
            buf,
            (size_t)buf_size,
            flags,
            InputTextCallback_Format,
            &ud,
            str
        );

        str->resize(strlen(str->c_str()));

        // raw right-click paste skips the edit callback — re-apply the mask.
        // no-op for typed input (the callback already produced this exact string).
        std::string formatted = FormatWithPattern(StripNonDigits(*str), pattern);
        if (formatted != *str)
        {
            *str = formatted;
            ud.PrevDigitCount = (int)StripNonDigits(formatted).size();
        }

        prevDigits = ud.PrevDigitCount;
        return changed;
    }

    bool InputTextMultilineString(const char* label, std::string* str, const ImVec2& size, ImGuiInputTextFlags flags)
    {
        IM_ASSERT(str);

        flags |= ImGuiInputTextFlags_CallbackResize;

        if (str->capacity() < 2)
            str->reserve(64);
        if (str->capacity() < 2)
            str->reserve(2);

        str->resize(str->capacity() - 1);

        InputTextStringUserData ud;
        ud.Str = str;

        char* buf = (char*)str->data();
        int buf_size = (int)str->size() + 1;

        IM_ASSERT(buf != nullptr);
        IM_ASSERT(buf_size >= 2);

        bool changed = ImGui::InputTextMultiline(
            label,
            buf,
            buf_size,
            size,
            flags,
            InputTextCallback_Resize,
            &ud
        );

        str->resize(strlen(str->c_str()));
        return changed;
    }

    // bullet-masked input with brief last-char reveal; cursor drawn manually
    bool InputTextPasswordReveal(const char* label, std::string* str, ImGuiInputTextFlags extra_flags, float reveal_duration_ms)
    {
        IM_ASSERT(str);

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (!window) return false;

        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = window->DrawList;

        const char* hash = strstr(label, "##");
        std::string visible;
        std::string id_storage;
        const char* id_label = nullptr;

        if (hash)
        {
            if (hash != label)
                visible.assign(label, size_t(hash - label));
            id_label = hash;
        }
        else
        {
            visible = label;
            id_storage = "##";
            id_storage += label;
            id_label = id_storage.c_str();
        }

        const float W = ImGui::CalcItemWidth();
        const float fieldH = 34.0f;
        const float rounding = 6.0f;
        const float labelFontScale = 0.82f;
        const float labelH = ImGui::GetFontSize() * labelFontScale;
        const float labelPadTop = 2.0f;
        const float totalH = labelH + labelPadTop + fieldH;
        const float textPadX = 10.0f;

        ImVec2 pos = window->DC.CursorPos;
        ImRect fieldBB(ImVec2(pos.x, pos.y + labelH + labelPadTop),
                       ImVec2(pos.x + W, pos.y + totalH));

        ImGui::BeginGroup();
        ImGui::Dummy(ImVec2(W, totalH));

        ImGuiID id = window->GetID(id_label);
        ImGuiStorage* storage = window->DC.StateStorage;

        const ImGuiID t_id = id + 1;
        const ImGuiID glow_id = id + 2;
        const ImGuiID prev_len_id = id + 3;
        const ImGuiID reveal_time_id = id + 4;
        const ImGuiID reveal_pos_id = id + 5;

        float t = storage->GetFloat(t_id, 0.0f);
        float glow = storage->GetFloat(glow_id, 0.0f);
        int prev_len = storage->GetInt(prev_len_id, 0);
        float reveal_time = storage->GetFloat(reveal_time_id, -1000.0f);
        int reveal_pos = storage->GetInt(reveal_pos_id, -1);

        if (str->capacity() < 2)
            str->reserve(64);
        if (str->capacity() < 2)
            str->reserve(2);
        str->resize(str->capacity() - 1);

        InputTextStringUserData ud;
        ud.Str = str;

        char* buf = (char*)str->data();
        int buf_size = (int)str->size() + 1;

        bool dark = IsDarkTheme();

        ImU32 fieldBg = dark ? theme::PanelBg.dark : theme::PanelBg.light;
        dl->AddRectFilled(fieldBB.Min, fieldBB.Max, fieldBg, rounding);

        ImVec4 borderRest = dark ? ImVec4(1, 1, 1, 0.14f) : ImVec4(0, 0, 0, 0.18f);
        ImVec4 borderFocus = ImVec4(colors::SecondColor.x, colors::SecondColor.y,
                                    colors::SecondColor.z, 0.85f);
        ImVec4 borderCol = ImLerp(borderRest, borderFocus, glow);
        float borderThickness = ImLerp(1.0f, 1.6f, glow);
        dl->AddRect(fieldBB.Min, fieldBB.Max,
                    ImGui::GetColorU32(borderCol), rounding, 0, borderThickness);

        if (glow > 0.01f)
        {
            ImVec4 outerGlow = ImVec4(colors::SecondColor.x, colors::SecondColor.y,
                                      colors::SecondColor.z, 0.08f * glow);
            dl->AddRect(ImVec2(fieldBB.Min.x - 1, fieldBB.Min.y - 1),
                        ImVec2(fieldBB.Max.x + 1, fieldBB.Max.y + 1),
                        ImGui::GetColorU32(outerGlow), rounding + 1, 0, 2.0f);
        }

        // transparent frame + text hidden — we draw masked text manually below
        ImGui::SetCursorScreenPos(ImVec2(fieldBB.Min.x + textPadX, fieldBB.Min.y));

        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(0, (fieldH - ImGui::GetTextLineHeight()) * 0.5f));
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, rounding);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_FrameBgActive, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_InputTextCursor, IM_COL32(0, 0, 0, 0));

        ImGuiInputTextFlags flags = ImGuiInputTextFlags_CallbackResize | extra_flags;

        float inputW = W - textPadX * 2;

        bool changed = ImGui::InputTextEx(
            id_label,
            nullptr,
            buf,
            buf_size,
            ImVec2(inputW, fieldH),
            flags,
            InputTextCallback_Resize,
            &ud
        );

        bool active = ImGui::IsItemActive();
        bool hovered = ImGui::IsItemHovered();
        RememberCaret(id);

        ImGui::PopStyleColor(5);
        ImGui::PopStyleVar(3);

        str->resize(strlen(str->c_str()));
        int cur_len = (int)str->length();
        bool has_text = cur_len > 0;

        {
            char ctx_id[64];
            snprintf(ctx_id, sizeof(ctx_id), "##inputpw_ctx_%u", id);
            ImGui::OpenPopupOnItemClick(ctx_id, ImGuiPopupFlags_MouseButtonRight);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
            ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 4));
            ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 8.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0.0f);
            if (ImGui::BeginPopup(ctx_id))
            {
                if (ImGui::MenuItem(ICON_MDI_CONTENT_COPY "  Copy"))
                {
                    if (!str->empty())
                        ImGui::SetClipboardText(str->c_str());
                }
                if (ImGui::MenuItem(ICON_MDI_CONTENT_PASTE "  Paste"))
                {
                    if (PasteAtCaret(str, id))
                        changed = true;
                }
                if (ImGui::MenuItem(ICON_MDI_KEYBOARD "  On-Screen Keyboard"))
                {
                    g_show_osk = !g_show_osk;
                }

                // gradient border on popup window
                {
                    const float r = 8.0f;
                    ImVec2 pMin = ImGui::GetWindowPos();
                    ImVec2 pMax = ImVec2(pMin.x + ImGui::GetWindowSize().x, pMin.y + ImGui::GetWindowSize().y);
                    ImDrawList* pdl = ImGui::GetWindowDrawList();
                    ImU32 colTL = theme::BevelTL;
                    ImU32 colBR = theme::BevelBR;
                    ImU32 colTR = theme::BevelTR;
                    ImU32 colBL = theme::BevelBL;
                    pdl->AddRectFilledMultiColor(
                        ImVec2(pMin.x + r, pMin.y),
                        ImVec2(pMax.x - r, pMin.y + 1.0f),
                        colTL, colBR, colBR, colTL);
                    pdl->AddRectFilled(
                        ImVec2(pMin.x + r, pMax.y - 1.0f),
                        ImVec2(pMax.x - r, pMax.y),
                        colBR);
                    pdl->AddRectFilledMultiColor(
                        ImVec2(pMin.x, pMin.y + r),
                        ImVec2(pMin.x + 1.0f, pMax.y - r),
                        colTL, colTL, colBR, colBR);
                    pdl->AddRectFilledMultiColor(
                        ImVec2(pMax.x - 1.0f, pMin.y + r),
                        ImVec2(pMax.x, pMax.y - r),
                        colBR, colBR, colBR, colBR);
                    float cr = r - 0.5f;
                    pdl->PathArcTo(ImVec2(pMin.x + r, pMin.y + r), cr, IM_PI, IM_PI * 1.5f, 8);
                    pdl->PathStroke(colTL, 0, 1.0f);
                    pdl->PathArcTo(ImVec2(pMax.x - r, pMin.y + r), cr, IM_PI * 1.5f, IM_PI * 2.0f, 8);
                    pdl->PathStroke(colTR, 0, 1.0f);
                    pdl->PathArcTo(ImVec2(pMin.x + r, pMax.y - r), cr, IM_PI * 0.5f, IM_PI, 8);
                    pdl->PathStroke(colBL, 0, 1.0f);
                    pdl->PathArcTo(ImVec2(pMax.x - r, pMax.y - r), cr, 0.0f, IM_PI * 0.5f, 8);
                    pdl->PathStroke(colBR, 0, 1.0f);
                }

                ImGui::EndPopup();
            }
            ImGui::PopStyleVar(4);
        }

        if (has_text)
        {
            const float clearSz = 18.0f;
            const float clearPad = 8.0f;
            ImVec2 clearCenter(fieldBB.Max.x - clearSz * 0.5f - clearPad,
                               fieldBB.Min.y + fieldH * 0.5f);
            ImRect clearRect(ImVec2(clearCenter.x - clearSz * 0.5f, clearCenter.y - clearSz * 0.5f),
                             ImVec2(clearCenter.x + clearSz * 0.5f, clearCenter.y + clearSz * 0.5f));
            bool clearHov = ImGui::IsMouseHoveringRect(clearRect.Min, clearRect.Max);

            if (clearHov)
            {
                ImU32 circleBg = dark ? IM_COL32(255, 255, 255, 25)
                                      : IM_COL32(0, 0, 0, 20);
                dl->AddCircleFilled(clearCenter, clearSz * 0.5f, circleBg);
            }

            ImU32 clearCol = clearHov
                ? (dark ? IM_COL32(255, 255, 255, 230) : IM_COL32(30, 30, 30, 230))
                : (dark ? IM_COL32(255, 255, 255, 100) : IM_COL32(80, 80, 80, 140));
            ImVec2 ts = ImGui::CalcTextSize(ICON_MDI_CLOSE_CIRCLE);
            dl->AddText(
                ImVec2(clearCenter.x - ts.x * 0.5f, clearCenter.y - ts.y * 0.5f),
                clearCol, ICON_MDI_CLOSE_CIRCLE);

            if (clearHov && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                str->clear();
                has_text = false;
                cur_len = 0;
                changed = true;
                ImGui::ClearActiveID();
            }
        }

        float now = (float)ImGui::GetTime();
        if (cur_len > prev_len && active)
        {
            reveal_pos = cur_len - 1;
            while (reveal_pos > 0 && ((*str)[reveal_pos] & 0xC0) == 0x80)
                reveal_pos--; // walk back to start of UTF-8 codepoint
            reveal_time = now;
        }
        storage->SetInt(prev_len_id, cur_len);
        storage->SetFloat(reveal_time_id, reveal_time);
        storage->SetInt(reveal_pos_id, reveal_pos);

        std::string masked;
        if (has_text)
        {
            float elapsed_ms = (now - reveal_time) * 1000.0f;
            bool revealing = (reveal_pos >= 0 && reveal_pos < cur_len && elapsed_ms < reveal_duration_ms);

            const char* p = str->c_str();
            const char* end = p + cur_len;
            int byte_idx = 0;

            while (p < end)
            {
                int cp_len = 1;
                if ((*p & 0xF8) == 0xF0) cp_len = 4;
                else if ((*p & 0xF0) == 0xE0) cp_len = 3;
                else if ((*p & 0xE0) == 0xC0) cp_len = 2;

                if (revealing && byte_idx == reveal_pos)
                    masked.append(p, cp_len);
                else
                    masked.append("\xe2\x80\xa2"); // bullet U+2022

                p += cp_len;
                byte_idx += cp_len;
            }
        }

        {
            float text_x = fieldBB.Min.x + textPadX;
            float text_y = fieldBB.Min.y + (fieldH - ImGui::GetTextLineHeight()) * 0.5f;

            if (!masked.empty())
            {
                dl->AddText(
                    ImVec2(text_x, text_y),
                    ImGui::GetColorU32(ImGuiCol_Text),
                    masked.c_str()
                );
            }

            if (active)
            {
                ImGuiInputTextState* state = ImGui::GetInputTextState(id);
                if (state)
                {
                    int cursor_byte = state->GetCursorPos();
                    int cp_count = 0;
                    {
                        const char* p = str->c_str();
                        const char* pend = p + ImMin(cursor_byte, cur_len);
                        while (p < pend)
                        {
                            int cp_len = 1;
                            if ((*p & 0xF8) == 0xF0) cp_len = 4;
                            else if ((*p & 0xF0) == 0xE0) cp_len = 3;
                            else if ((*p & 0xE0) == 0xC0) cp_len = 2;
                            p += cp_len;
                            cp_count++;
                        }
                    }

                    std::string masked_to_cursor;
                    {
                        const char* mp = masked.c_str();
                        const char* mend = mp + masked.size();
                        int n = 0;
                        while (mp < mend && n < cp_count)
                        {
                            int cp_len = 1;
                            if ((*mp & 0xF8) == 0xF0) cp_len = 4;
                            else if ((*mp & 0xF0) == 0xE0) cp_len = 3;
                            else if ((*mp & 0xE0) == 0xC0) cp_len = 2;
                            masked_to_cursor.append(mp, cp_len);
                            mp += cp_len;
                            n++;
                        }
                    }

                    float cursor_x = text_x + ImGui::CalcTextSize(masked_to_cursor.c_str()).x;

                    bool visible_cursor = (state->CursorAnim <= 0.0f) || ImFmod(state->CursorAnim, 1.20f) <= 0.80f;
                    if (visible_cursor)
                    {
                        dl->AddLine(
                            ImVec2(cursor_x, text_y),
                            ImVec2(cursor_x, text_y + ImGui::GetTextLineHeight()),
                            ImGui::GetColorU32(ImGuiCol_Text), 1.0f);
                    }
                }
            }
        }

        float float_target = (active || hovered || has_text) ? 1.0f : 0.0f;
        t = ImLerp(t, float_target, io.DeltaTime * 12.0f);

        float glow_target = active ? 1.0f : (hovered ? 0.4f : 0.0f);
        glow = ImLerp(glow, glow_target, io.DeltaTime * 10.0f);

        storage->SetFloat(t_id, t);
        storage->SetFloat(glow_id, glow);

        if (!visible.empty())
        {
            const float inside_y = fieldBB.Min.y + (fieldH - ImGui::GetTextLineHeight()) * 0.5f;
            const float outside_y = fieldBB.Min.y - labelH - 2.0f;

            float label_y = ImLerp(inside_y, outside_y, t);
            float label_x = ImLerp(fieldBB.Min.x + textPadX, fieldBB.Min.x + 1.0f, t);
            float scale = ImLerp(1.0f, labelFontScale, t);

            ImVec4 restCol = dark ? ImVec4(1, 1, 1, 0.35f)
                                  : ImVec4(0, 0, 0, 0.38f);
            ImVec4 activeCol = dark ? ImVec4(1, 1, 1, 0.75f)
                                    : ImVec4(0.10f, 0.10f, 0.10f, 0.85f);
            ImVec4 col = ImLerp(restCol, activeCol, t);

            dl->AddText(nullptr,
                ImGui::GetFontSize() * scale,
                ImVec2(label_x, label_y),
                ImGui::GetColorU32(col),
                visible.c_str());
        }

        ImGui::EndGroup();
        return changed;
    }

    static bool IconButtonSquare_Impl(
        const char* id,
        const char* glyph,
        float size,
        bool enabled,
        ImU32 accent)
    {
        ImGui::PushID(id);

        const float rounding = 8.0f;
        const bool dark = IsDarkTheme();

        ImVec4 col_bg   = dark ? ImVec4(0.15f, 0.15f, 0.16f, 1.00f) : ImVec4(248/255.0f, 248/255.0f, 250/255.0f, 1.00f);
        ImVec4 col_hov  = dark ? ImVec4(0.20f, 0.20f, 0.22f, 1.00f) : ImVec4(240/255.0f, 240/255.0f, 243/255.0f, 1.00f);
        ImVec4 col_act  = dark ? ImVec4(0.24f, 0.24f, 0.26f, 1.00f) : ImVec4(232/255.0f, 232/255.0f, 236/255.0f, 1.00f);
        ImVec4 col_brd  = dark ? ImVec4(1,1,1,0.06f) : ImVec4(0,0,0,0.08f);

        if (!enabled)
            ImGui::BeginDisabled(true);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##icon_btn", ImVec2(size, size));
        bool pressed = ImGui::IsItemClicked();
        bool hovered = ImGui::IsItemHovered();
        bool held    = ImGui::IsItemActive();

        if (!enabled)
            ImGui::EndDisabled();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pmin = pos;
        ImVec2 pmax(pos.x + size, pos.y + size);

        ImVec4 bg = held ? col_act : (hovered ? col_hov : col_bg);
        dl->AddRectFilled(pmin, pmax, ImGui::ColorConvertFloat4ToU32(bg), rounding);
        dl->AddRect(pmin, pmax, ImGui::ColorConvertFloat4ToU32(col_brd), rounding, 0, 1.0f);

        ImVec2 center((pmin.x + pmax.x) * 0.5f, (pmin.y + pmax.y) * 0.5f);

        ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);

        if (!enabled)
        {
            ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            c.w *= 0.9f;
            textCol = ImGui::ColorConvertFloat4ToU32(c);
        }
        else if (accent != 0)
        {
            textCol = accent;
        }

        ImVec2 ts = ImGui::CalcTextSize(glyph);
        dl->AddText(
            ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
            textCol,
            glyph
        );

        ImGui::PopID();
        return pressed && enabled;
    }

    bool IconButtonSquare(
        const char* id,
        const char* glyph,
        float size,
        bool enabled,
        ImU32 accent)
    {
        return IconButtonSquare_Impl(id, glyph, size, enabled, accent);
    }

    static bool IconButtonSquare_Impl2(const char* id, const char* glyph, float size = 30.0f)
    {
        ImGui::PushID(id);

        const float rounding = 8.0f;
        const bool dark = IsDarkTheme();

        // same bg as InputText4 field so buttons sit flush in light mode
        ImVec4 col_bg   = dark ? ImVec4(0.15f, 0.15f, 0.16f, 1.00f) : ImVec4(248/255.0f, 248/255.0f, 250/255.0f, 1.00f);
        ImVec4 col_hov  = dark ? ImVec4(0.20f, 0.20f, 0.22f, 1.00f) : ImVec4(240/255.0f, 240/255.0f, 243/255.0f, 1.00f);
        ImVec4 col_act  = dark ? ImVec4(0.24f, 0.24f, 0.26f, 1.00f) : ImVec4(232/255.0f, 232/255.0f, 236/255.0f, 1.00f);
        ImVec4 col_brd  = dark ? ImVec4(1,1,1,0.06f) : ImVec4(0,0,0,0.08f);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##icon_btn", ImVec2(size, size));
        bool pressed = ImGui::IsItemClicked();
        bool hovered = ImGui::IsItemHovered();
        bool held    = ImGui::IsItemActive();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 pmin = pos;
        ImVec2 pmax(pos.x + size, pos.y + size);

        ImVec4 bg = held ? col_act : (hovered ? col_hov : col_bg);
        dl->AddRectFilled(pmin, pmax, ImGui::ColorConvertFloat4ToU32(bg), rounding);
        dl->AddRect(pmin, pmax, ImGui::ColorConvertFloat4ToU32(col_brd), rounding, 0, 1.0f);

        ImVec2 center((pmin.x + pmax.x) * 0.5f, (pmin.y + pmax.y) * 0.5f);
        ImVec2 ts = ImGui::CalcTextSize(glyph);
        dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f),
            ImGui::GetColorU32(ImGuiCol_Text),
            glyph);

        ImGui::PopID();
        return pressed;
    }

    bool IconButtonSquare2(const char* id, const char* glyph, float size)
    {
        return IconButtonSquare_Impl2(id, glyph, size);
    }

    bool IconButtonGhost(const char* id, const char* glyph, float size, bool enabled)
    {
        ImGui::PushID(id);
        if (!enabled) ImGui::BeginDisabled(true);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##g", ImVec2(size, size));
        bool pressed = ImGui::IsItemClicked();
        bool hovered = ImGui::IsItemHovered();

        if (!enabled) ImGui::EndDisabled();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 center(pos.x + size * 0.5f, pos.y + size * 0.5f);
        ImVec2 ts = ImGui::CalcTextSize(glyph);

        ImU32 col;
        if (!enabled)
        {
            ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
            c.w *= 0.7f;
            col = ImGui::ColorConvertFloat4ToU32(c);
        }
        else
        {
            float alpha = hovered ? 1.0f : 0.65f;
            ImVec4 c = ImGui::GetStyleColorVec4(ImGuiCol_Text);
            c.w *= alpha;
            col = ImGui::ColorConvertFloat4ToU32(c);
        }

        dl->AddText(ImVec2(center.x - ts.x * 0.5f, center.y - ts.y * 0.5f), col, glyph);

        ImGui::PopID();
        return pressed && enabled;
    }

    bool IconButtonDoubleLip(const char* id, const char* glyph, float size)
    {
        ImGui::PushID(id);

        const bool dark = g_shell_ptr ? g_shell_ptr->dark_theme : true;
        const float rounding = 9.0f;

        const ImVec4 bg     = dark ? ImVec4(0.14f, 0.14f, 0.15f, 1.0f) : ImVec4(0.91f, 0.91f, 0.92f, 1.0f);
        const ImVec4 bgHov  = dark ? ImVec4(0.17f, 0.17f, 0.18f, 1.0f) : ImVec4(0.86f, 0.86f, 0.87f, 1.0f);
        const ImVec4 bgAct  = dark ? ImVec4(0.12f, 0.12f, 0.13f, 1.0f) : ImVec4(0.82f, 0.82f, 0.83f, 1.0f);

        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##dlip", ImVec2(size, size));
        bool pressed = ImGui::IsItemClicked();
        bool hovered = ImGui::IsItemHovered();
        bool active  = ImGui::IsItemActive();

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 bMin = pos;
        ImVec2 bMax(pos.x + size, pos.y + size);

        ImVec4 bgCol = active ? bgAct : (hovered ? bgHov : bg);
        auto Clamp01 = [](float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); };
        auto Lighten = [&](const ImVec4& c, float amt) {
            return ImVec4(Clamp01(c.x + amt), Clamp01(c.y + amt), Clamp01(c.z + amt), c.w);
        };
        auto Darken = [&](const ImVec4& c, float amt) {
            return ImVec4(Clamp01(c.x - amt), Clamp01(c.y - amt), Clamp01(c.z - amt), c.w);
        };

        float lipOff = 1.0f;
        ImVec4 lipCol = Darken(bgAct, 0.15f);
        lipCol.w = 0.32f;

        ImDrawListSplitter splitter;
        splitter.Split(dl, 2);
        splitter.SetCurrentChannel(dl, 1);

        dl->AddRectFilled(bMin, bMax, ImGui::ColorConvertFloat4ToU32(bgCol), rounding);

        // bottom lip
        splitter.SetCurrentChannel(dl, 0);
        float clipTop = bMin.y + (size * 0.62f);
        dl->PushClipRect(ImVec2(bMin.x - 3.0f, clipTop), ImVec2(bMax.x + 3.0f, bMax.y + lipOff + 2.0f), true);
        dl->AddRectFilled(ImVec2(bMin.x, bMin.y + lipOff), ImVec2(bMax.x, bMax.y + lipOff),
            ImGui::ColorConvertFloat4ToU32(lipCol), rounding);
        dl->PopClipRect();

        // right lip
        float clipLeft = bMin.x + (size * 0.40f);
        dl->PushClipRect(ImVec2(clipLeft, bMin.y - 1.0f), ImVec2(bMax.x + lipOff + 2.0f, bMax.y + 1.0f), true);
        dl->AddRectFilled(ImVec2(bMin.x + lipOff, bMin.y), ImVec2(bMax.x + lipOff, bMax.y),
            ImGui::ColorConvertFloat4ToU32(lipCol), rounding);
        dl->PopClipRect();

        splitter.SetCurrentChannel(dl, 1);

        // inset panel — slightly lighter, creates the double-lip look
        float insetX = 1.0f, insetTop = 1.0f, insetBot = 2.2f;
        ImVec4 panelCol = Lighten(bgCol, active ? 0.05f : (hovered ? 0.07f : 0.06f));
        ImVec2 iMin(bMin.x + insetX, bMin.y + insetTop);
        ImVec2 iMax(bMax.x - insetX, bMax.y - insetBot);
        if (iMax.x > iMin.x && iMax.y > iMin.y)
        {
            dl->AddRectFilled(iMin, iMax, ImGui::ColorConvertFloat4ToU32(panelCol), rounding - 2.0f);

            ImVec2 ts = ImGui::CalcTextSize(glyph);
            ImVec2 tp(iMin.x + (iMax.x - iMin.x - ts.x) * 0.5f,
                      iMin.y + (iMax.y - iMin.y - ts.y) * 0.5f + 2.0f);
            ImU32 textCol = ImGui::GetColorU32(ImGuiCol_Text);
            dl->AddText(tp, textCol, glyph);
        }

        splitter.Merge(dl);

        ImGui::PopID();
        return pressed;
    }


    void TextEllipsisClipped(const char* text, float max_w)
    {
        if (!text) text = "";
        if (max_w <= 0.0f) return;

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        ImDrawList* dl = window->DrawList;

        ImVec2 p = ImGui::GetCursorScreenPos();
        ImVec2 clipMin = p;
        ImVec2 clipMax = ImVec2(p.x + max_w, p.y + ImGui::GetTextLineHeight());

        ImVec2 tsz = ImGui::CalcTextSize(text);
        if (tsz.x <= max_w)
        {
            ImGui::TextUnformatted(text);
            return;
        }

        const char* dots = "...";
        ImVec2 dsz = ImGui::CalcTextSize(dots);
        ImVec2 textClipMax = ImVec2(p.x + max_w - dsz.x, clipMax.y);
        dl->PushClipRect(clipMin, textClipMax, true);
        dl->AddText(p, ImGui::GetColorU32(ImGuiCol_Text), text);
        dl->PopClipRect();

        dl->AddText(ImVec2(textClipMax.x, p.y), ImGui::GetColorU32(ImGuiCol_TextDisabled), dots);

        ImGui::Dummy(ImVec2(max_w, ImGui::GetTextLineHeight()));

        if (ImGui::IsItemHovered())
            SetTooltipPadded("%s", text);
    }


    float AnimExpF(float cur, float target, float speed, float dt)
    {
        const float t = 1.0f - std::exp(-speed * dt);
        return cur + (target - cur) * t;
    }

    bool SearchIconPopup(const char* id,
        ImGuiTextFilter& filter,
        SearchPopupAnim& anim,
        float targetWidth,
        float height)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (!window || window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        const float dt = g.IO.DeltaTime;

        const bool hasText = (filter.InputBuf[0] != 0);

        ImGui::PushID(id);

        if (hasText)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(colors::SecondColor));

        float btnW;
        float btnSize = btnW = 24.0f, btnH = 24.0f;

        bool pressed = IconButtonSquare2("srch_search", ICON_MDI_MAGNIFY, btnSize);

        if (hasText)
            ImGui::PopStyleColor();

        const ImVec2 btnMin = ImGui::GetItemRectMin();
        const ImVec2 btnMax = ImGui::GetItemRectMax();

        const char* popupName = "##search_popup";

        if (pressed)
        {
            if (ImGui::IsPopupOpen(popupName))
                ImGui::CloseCurrentPopup();
            else
                ImGui::OpenPopup(popupName);

            anim.init = false;
        }

        const bool wantOpen = ImGui::IsPopupOpen(popupName);
        const float wTarget = wantOpen ? targetWidth : 0.0f;

        if (!anim.init) { anim.w = wTarget; anim.init = true; }
        anim.w = AnimExpF(anim.w, wTarget, 22.0f, dt);

        bool changed = false;

        if (wantOpen)
        {
            const float popupW = ImMax(120.0f, anim.w);
            const float popupH = height;

            ImVec2 popupPos(btnMax.x - popupW, btnMin.y - 2.0f);
            ImGui::SetNextWindowPos(popupPos, ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(popupW, popupH), ImGuiCond_Always);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 6));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);

            ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoScrollbar;

            if (ImGui::BeginPopup(popupName, flags))
            {
                if (ImGui::IsWindowAppearing())
                    ImGui::SetKeyboardFocusHere();

                if (ImGui::IsKeyPressed(ImGuiKey_Escape))
                    ImGui::CloseCurrentPopup();

                ImGui::SetNextItemWidth(-1.0f);

                if (filter.Draw(ICON_MDI_MAGNIFY, 120))
                    changed = true;

                if (hasText)
                {
                    ImGui::SameLine();
                    if (ImGui::SmallButton("X"))
                    {
                        filter.Clear();
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }
                }

                ImGui::EndPopup();
            }

            ImGui::PopStyleVar(2);
        }

        ImGui::PopID();
        return changed;
    }

    float AnimExp(float cur, float target, float speed, float dt)
    {
        const float t = 1.0f - std::exp(-speed * dt);
        return cur + (target - cur) * t;
    }

    bool UnderlineTabs(const char* id,
        const char* const* labels, int labelCount,
        int& activeIndex,
        UnderlineTabsAnim& anim,
        float spacing)
    {
        if (labelCount <= 0) return false;
        activeIndex = ImClamp(activeIndex, 0, labelCount - 1);

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (!window || window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        ImDrawList* dl = ImGui::GetWindowDrawList();

        const float dt = g.IO.DeltaTime;
        const float speed = 18.0f;

        ImVec2 rowStart = ImGui::GetCursorScreenPos();

        bool changed = false;

        float targetX = 0.f;
        float targetW = 0.f;

        const ImVec2 pad(8.0f, 4.0f);

        ImGui::PushID(id);
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(0,0,0,0));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, pad);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.0f);

        for (int i = 0; i < labelCount; ++i)
        {
            const char* label = labels[i] ? labels[i] : "";
            ImVec2 textSz = ImGui::CalcTextSize(label, nullptr, true);

            bool isActive = (i == activeIndex);
            if (!isActive)
                ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));

            if (ImGui::Button(label, ImVec2(textSz.x + pad.x * 2.0f, 0)))
            {
                if (!isActive)
                {
                    activeIndex = i;
                    changed = true;
                }
            }

            if (!isActive)
                ImGui::PopStyleColor();

            if (isActive)
            {
                ImVec2 rMin = ImGui::GetItemRectMin();
                targetX = rMin.x + pad.x;
                targetW = textSz.x;
            }

            if (i != labelCount - 1)
                ImGui::SameLine(0.0f, spacing);
        }

        ImGui::PopStyleVar(2);
        ImGui::PopStyleColor(3);
        ImGui::PopID();

        if (!anim.init)
        {
            anim.x = targetX;
            anim.w = targetW;
            anim.init = true;
        }
        else
        {
            anim.x = AnimExp(anim.x, targetX, speed, dt);
            anim.w = AnimExp(anim.w, targetW, speed, dt);
        }

        const float thickness = 2.0f;
        const float radius = thickness * 0.5f;

        const float y = rowStart.y + ImGui::GetTextLineHeight() + pad.y * 2.0f + 1.0f;
        ImVec2 a(anim.x, y);
        ImVec2 b(anim.x + anim.w, y + thickness);

        ImU32 col = ImGui::GetColorU32(colors::SecondColor);
        dl->AddRectFilled(a, b, col, radius);

        return changed;
    }

    // PillTabsAnim defined in ui_internal.h

    bool PillTabs(const char* id,
        const char* const* labels, int labelCount,
        int& activeIndex,
        PillTabsAnim& /*anim*/,
        float spacing,
        int* right_clicked_tab)
    {
        if (labelCount <= 0) return false;
        activeIndex = ImClamp(activeIndex, 0, labelCount - 1);
        if (right_clicked_tab) *right_clicked_tab = -1;

        bool changed = false;
        ImGui::PushID(id);

        for (int i = 0; i < labelCount; ++i)
        {
            const char* label = (labels && labels[i]) ? labels[i] : "?";
            const bool isActive = (i == activeIndex);

            ImGui::PushID(i);
            if (isActive)
                ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));

            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
            if (ImGui::Button(label))
            {
                if (!isActive)
                {
                    activeIndex = i;
                    changed = true;
                }
            }
			ImGui::PopStyleVar();

            if (right_clicked_tab && ImGui::IsItemClicked(ImGuiMouseButton_Right))
                *right_clicked_tab = i;

            if (isActive)
                ImGui::PopStyleColor();
            ImGui::PopID();

            if (i < labelCount - 1)
                ImGui::SameLine(0.0f, spacing);
        }

        ImGui::PopID();
        return changed;
    }


    struct underline_combo_state
    {
        ImVec4 underline_color = colors::Gray;
        float  underline_thickness = 1.0f;
        bool   init = false;
    };

    static inline float _anim_exp(float cur, float target, float speed, float dt)
    {
        const float t = 1.0f - std::exp(-speed * dt);
        return cur + (target - cur) * t;
    }

    bool UnderlineCombo(const char* id,
        const std::vector<std::string>& items,
        int& current_index,
        float width,
        float height)
    {
        ImGuiWindow* window = ImGui::GetCurrentWindow();
        if (!window || window->SkipItems) return false;

        ImGuiContext& g = *GImGui;
        ImGuiIO& io = g.IO;
        ImGuiStyle& style = g.Style;
        ImDrawList* dl = window->DrawList;

        if (items.empty()) current_index = -1;
        else current_index = ImClamp(current_index, 0, (int)items.size() - 1);

        const char* preview = (current_index >= 0 && current_index < (int)items.size())
            ? items[current_index].c_str()
            : "—";

        const char* popup = "##underline_combo_popup";
        const bool open_now = ImGui::IsPopupOpen(popup);
        const char* caret = open_now ? ICON_MDI_CHEVRON_UP : ICON_MDI_CHEVRON_DOWN;

        const float avail = ImGui::GetContentRegionAvail().x;

        const float pad_x = 8.0f;
        const float caret_pad_r = 8.0f;
        const float inner_gap = 10.0f;

        const float preview_w = ImGui::CalcTextSize(preview).x;
        const float caret_w = ImGui::CalcTextSize(caret).x;

        float required_w = pad_x + preview_w + inner_gap + caret_w + caret_pad_r;
        float w = (width > 0.0f) ? width : required_w;
        w = (avail > 0.0f) ? ImMin(w, avail) : w;

        const ImVec2 pos = ImGui::GetCursorScreenPos();
        const ImRect bb(pos, ImVec2(pos.x + w, pos.y + height));

        ImGui::PushID(id);
        const ImGuiID wid = window->GetID("##underline_combo");

        ImGui::ItemSize(bb);
        if (!ImGui::ItemAdd(bb, wid)) { ImGui::PopID(); return false; }

        bool hovered = false, held = false;
        const bool pressed = ImGui::ButtonBehavior(bb, wid, &hovered, &held);

        if (pressed)
        {
            if (ImGui::IsPopupOpen(popup))
                ImGui::CloseCurrentPopup();
            else
                ImGui::OpenPopup(popup);
        }

        const bool open = ImGui::IsPopupOpen(popup);

        static std::unordered_map<ImGuiID, underline_combo_state> s_states;
        underline_combo_state& st = s_states[wid];

        const float dt = io.DeltaTime;
        const float speed = 18.0f;

        const ImVec4 target_col = (open || hovered) ? colors::White : colors::Gray;
        const float  target_thk = (open) ? 2.0f : (hovered ? 1.5f : 1.0f);

        if (!st.init) { st.underline_color = target_col; st.underline_thickness = target_thk; st.init = true; }
        st.underline_color.x = _anim_exp(st.underline_color.x, target_col.x, speed, dt);
        st.underline_color.y = _anim_exp(st.underline_color.y, target_col.y, speed, dt);
        st.underline_color.z = _anim_exp(st.underline_color.z, target_col.z, speed, dt);
        st.underline_color.w = _anim_exp(st.underline_color.w, target_col.w, speed, dt);
        st.underline_thickness = _anim_exp(st.underline_thickness, target_thk, speed, dt);

        ImU32 text_col = ImGui::GetColorU32(ImGuiCol_Text);
        ImU32 muted_col = ImGui::GetColorU32(ImGuiCol_TextDisabled);

        const float pad_y = 6.0f;
        ImVec2 text_pos(bb.Min.x + pad_x, bb.Min.y + pad_y);
        float text_clip_max_x = bb.Max.x - caret_pad_r - caret_w - inner_gap;
        text_clip_max_x = ImMax(text_clip_max_x, bb.Min.x + pad_x);
        ImVec2 text_clip_max(text_clip_max_x, bb.Max.y);
        dl->PushClipRect(bb.Min, bb.Max, true);
        dl->PushClipRect(ImVec2(bb.Min.x + pad_x, bb.Min.y), text_clip_max, true);
        dl->AddText(text_pos, (current_index >= 0 ? text_col : muted_col), preview);
        dl->PopClipRect();
        dl->PopClipRect();

        ImVec2 caret_sz = ImGui::CalcTextSize(caret);
        ImVec2 caret_pos(bb.Max.x - caret_pad_r - caret_sz.x, bb.Min.y + (height - caret_sz.y) * 0.5f);
        dl->AddText(caret_pos, hovered || open ? ImGui::GetColorU32(colors::SecondColor) : muted_col, caret);

        const float underline_y = bb.Max.y - 6.0f;
        dl->AddLine(ImVec2(bb.Min.x, underline_y),
            ImVec2(bb.Max.x, underline_y),
            ImGui::GetColorU32(st.underline_color),
            st.underline_thickness);

        bool changed = false;
        if (open)
        {
            const int itemCount = (int)items.size();
            const float row_h = ImGui::GetTextLineHeight() + style.FramePadding.y * 2.0f;
            const float pop_pad_y = style.WindowPadding.y;

            const float desired_h = itemCount * row_h + pop_pad_y * 2.0f;
            const float max_h = 180.0f;
            const float popup_h = ImMin(desired_h, max_h);

            ImGui::SetNextWindowPos(ImVec2(bb.Min.x, bb.Max.y + 2.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(ImVec2(bb.GetWidth(), popup_h), ImGuiCond_Always);

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 8));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);

            ImGuiWindowFlags flags =
                ImGuiWindowFlags_NoTitleBar |
                ImGuiWindowFlags_NoResize |
                ImGuiWindowFlags_NoMove |
                ImGuiWindowFlags_NoSavedSettings;

            if (ImGui::BeginPopup(popup, flags))
            {
                const bool needs_scroll = (desired_h > max_h);

                if (needs_scroll)
                    ImGui::BeginChild("##combo_list",
                        ImVec2(0, popup_h - pop_pad_y * 2.0f),
                        false,
                        ImGuiWindowFlags_AlwaysVerticalScrollbar);

                for (int i = 0; i < itemCount; ++i)
                {
                    const bool is_sel = (i == current_index);
                    if (is_sel) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(colors::SecondColor));

                    if (ImGui::Selectable(items[i].c_str(), is_sel))
                    {
                        current_index = i;
                        changed = true;
                        ImGui::CloseCurrentPopup();
                    }

                    if (is_sel) ImGui::PopStyleColor();
                }

                if (needs_scroll)
                    ImGui::EndChild();

                ImGui::EndPopup();
            }

            ImGui::PopStyleVar(2);
        }

        ImGui::PopID();
        return changed;
    }


    const char* GetScopePreviewLabel(const std::set<std::string>& selected)
    {
        static char buf[64];
        if (selected.empty()) return "Filter";

        int types = 0, groups = 0;
        for (const auto& s : selected) {
            if (!s.empty() && s[0] == '@') types++;
            else if (s != "---") groups++;
        }

        if (selected.size() == 1) {
            const std::string& s = *selected.begin();
            if (!s.empty() && s[0] == '@')
                return s.c_str() + 1; // skip '@'
            return s.c_str();
        }

        if (types > 0 && groups == 0)
            snprintf(buf, sizeof(buf), "%d types", types);
        else if (types == 0 && groups > 0)
            snprintf(buf, sizeof(buf), "%d groups", groups);
        else
            snprintf(buf, sizeof(buf), "%d filters", types + groups);
        return buf;
    }

    static const char* StrengthLabelNice(int score)
    {
        switch (score)
        {
        case 0: return "Very Weak";
        case 1: return "Weak";
        case 2: return "Fair";
        case 3: return "Strong";
        default:return "Excellent";
        }
    }

    void DrawStrengthMeterCompact(const std::string& pw, float full_w)
    {
        const float barH = 6.0f;
        const int numSegments = 4;
        const float segmentGap = 4.0f;
        const float rounding = 2.0f;
        const float labelGap = 10.0f;

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 bg = IsDarkTheme() ? IM_COL32(50, 50, 50, 130) : IM_COL32(180, 180, 180, 130);

        if (pw.empty())
            return;

        helpers::PwStrength s = helpers::analyze_password(pw);

        int issues = 0;
        if (s.shortPwd) issues++;
        if (s.allSame) issues++;
        if (s.simpleSeq) issues++;
        if (s.isCommonPwd) issues += 3;
        if (s.isDictWord) issues += 2;
        if (!s.hasUpper) issues++;
        if (!s.hasLower) issues++;
        if (!s.hasDigit) issues++;
        if (!s.hasSymbol) issues++;

        int numBars;
        const char* lbl;
        if (issues >= 4)       { numBars = 1; lbl = "Very Weak"; }
        else if (issues == 3)  { numBars = 2; lbl = "Fair"; }
        else if (issues == 2)  { numBars = 3; lbl = "Strong"; }
        else if (issues == 1)  { numBars = 3; lbl = "Very Strong"; }
        else                   { numBars = 4; lbl = "Excellent"; }

        ImU32 lblColor = (ImU32)helpers::strength_color(s.score);

        ImVec4 baseCol;
        if (issues >= 4)       baseCol = ImVec4(220/255.f,  50/255.f,  47/255.f, 1.0f); // red
        else if (issues == 3)  baseCol = ImVec4(230/255.f, 160/255.f,  30/255.f, 1.0f); // orange
        else if (issues == 2)  baseCol = ImVec4(160/255.f, 200/255.f,  50/255.f, 1.0f); // yellow-green
        else                   baseCol = ImVec4( 60/255.f, 180/255.f,  75/255.f, 1.0f); // green

        // ramp brightness dim→full across filled segments
        ImU32 segColors[4];
        for (int i = 0; i < 4; i++)
        {
            float t = (numBars <= 1) ? 1.0f : (float)i / (float)(numBars - 1);
            float brightness = 0.35f + 0.65f * t;  // range from 35% to 100%
            segColors[i] = IM_COL32(
                (int)(baseCol.x * brightness * 255),
                (int)(baseCol.y * brightness * 255),
                (int)(baseCol.z * brightness * 255), 255);
        }

        struct CheckItem { bool pass; const char* text; };
        std::vector<CheckItem> checks;
        bool hasWarnings = false;
        if (s.shortPwd)       { checks.push_back({false, "Too short (min 8 characters)"}); hasWarnings = true; }
        else                    checks.push_back({true,  "Length (8+ characters)"});
        if (s.isCommonPwd)    { checks.push_back({false, "Common password"}); hasWarnings = true; }
        if (s.isDictWord)     { checks.push_back({false, "Contains common word or name"}); hasWarnings = true; }
        if (s.allSame)        { checks.push_back({false, "All characters are the same"}); hasWarnings = true; }
        if (s.repeatedPattern){ checks.push_back({false, "Repeated pattern detected"}); hasWarnings = true; }
        if (s.tooSequential)  { checks.push_back({false, "Sequential characters (abc, 123)"}); hasWarnings = true; }
        if (s.hasUpper)         checks.push_back({true,  "Uppercase letters"});
        else                  { checks.push_back({false, "No uppercase letters"}); hasWarnings = true; }
        if (s.hasLower)         checks.push_back({true,  "Lowercase letters"});
        else                  { checks.push_back({false, "No lowercase letters"}); hasWarnings = true; }
        if (s.hasDigit)         checks.push_back({true,  "Numbers"});
        else                  { checks.push_back({false, "No numbers"}); hasWarnings = true; }
        if (s.hasSymbol)        checks.push_back({true,  "Special characters"});
        else                  { checks.push_back({false, "No special characters (!@#$...)"}); hasWarnings = true; }

        const char* warnIcon = (issues <= 1) ? ICON_MDI_CHECK_CIRCLE : ICON_MDI_ALERT;
        float warnW = ImGui::CalcTextSize(warnIcon).x + 4.0f;

        ImVec2 lblSz = ImGui::CalcTextSize(lbl);
        float barW = full_w - lblSz.x - labelGap - warnW;
        if (barW < 60.0f) barW = 60.0f;

        ImGui::Dummy(ImVec2(0, 5));

        {
            ImU32 iconCol;
            if (issues >= 4)      iconCol = colors::VerifyBad;
            else if (issues >= 2) iconCol = colors::VerifyWarn;
            else                  iconCol = colors::VerifyGood;
            ImGui::PushStyleColor(ImGuiCol_Text, ImColor(iconCol).Value);
            ImGui::TextUnformatted(warnIcon);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered())
            {
                bool dk = IsDarkTheme();
                ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10, 8));
                ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
                ImGui::PushStyleColor(ImGuiCol_PopupBg, dk ? theme::PopupBg.dark : theme::PopupBg.light);
                ImGui::BeginTooltip();
                for (const auto& ci : checks)
                {
                    const char* ico = ci.pass ? ICON_MDI_CHECK : ICON_MDI_CLOSE;
                    ImU32 icoCol = ci.pass ? colors::VerifyGood : colors::VerifyBad;
                    ImGui::PushStyleColor(ImGuiCol_Text, ImColor(icoCol).Value);
                    ImGui::TextUnformatted(ico);
                    ImGui::PopStyleColor();
                    ImGui::SameLine(0, 6);
                    ImGui::TextUnformatted(ci.text);
                }
                ImGui::EndTooltip();
                ImGui::PopStyleColor();
                ImGui::PopStyleVar(2);
            }
            ImGui::SameLine(0.0f, 4.0f);
        }

        ImGui::TextUnformatted(lbl);
        ImGui::SameLine(0.0f, labelGap);

        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float barY = p0.y + (lblSz.y - barH) * 0.5f;
        float totalGaps = segmentGap * (numSegments - 1);
        float segmentW = (barW - totalGaps) / numSegments;

        for (int i = 0; i < numSegments; ++i)
        {
            float segX = p0.x + i * (segmentW + segmentGap);
            ImVec2 segMin(segX, barY);
            ImVec2 segMax(segX + segmentW, barY + barH);

            dl->AddRectFilled(segMin, segMax, bg, rounding);

            if (i < numBars)
                dl->AddRectFilled(segMin, segMax, segColors[i], rounding);
        }

        //ImGui::Dummy(ImVec2(barW, lblSz.y + 10));
        //ImGui::Spacing();
    }

    bool PasswordFieldRow(
        const char* id,
        std::string& pw,
        bool& show_passwword,
        helpers::GenOptions& genOpt,
        float full_w)
    {
        bool changed = false;

        ImGui::PushID(id);

        const float gap = 10.0f;
        const float btnSz = 34.0f; // matches InputText4 field height
        const float iconsTotal = btnSz * 4.0f + gap * 3.0f;

        float inputW = full_w - iconsTotal - gap;
        if (inputW < 120.0f) inputW = 120.0f;

        ImGui::PushItemWidth(inputW);
        if (show_passwword)
        {
            if (ui::InputTextString("Enter Password##pw", &pw, 0))
                changed = true;
        }
        else
        {
            if (ui::InputTextPasswordReveal("Enter Password##pw", &pw))
                changed = true;
        }
        ImGui::PopItemWidth();

        ImGui::SameLine(0, gap);

        // skip floating label area so buttons align with the field box
        float labelAreaH = ImGui::GetFontSize() * 0.82f + 2.0f;
        float btnY = ImGui::GetCursorPosY() + labelAreaH;
        ImGui::SetCursorPosY(btnY);

        if (ui::IconButtonSquare2("gen", ICON_MDI_REFRESH, btnSz))
        {
            std::string g = helpers::generate_password(genOpt);
            if (!g.empty())
            {
                pw = g;
                changed = true;
            }
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
                SetTooltipPadded("Generate");
        }
        else if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
            SetTooltipPadded("Generate");

        ImGui::SameLine(0, gap);
        ImGui::SetCursorPosY(btnY);

        const char* eye = show_passwword ? ICON_MDI_EYE_OFF : ICON_MDI_EYE;
        if (ui::IconButtonSquare2("eye", eye, btnSz))
            show_passwword = !show_passwword;

        if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
            SetTooltipPadded(show_passwword ? "Hide" : "Show");

        ImGui::SameLine(0, gap);
        ImGui::SetCursorPosY(btnY);

        // copy with brief checkmark feedback
        {
            static double s_copy_time = 0.0;
            double now = ImGui::GetTime();
            bool showCheck = (now - s_copy_time) < 1.5;
            const char* cpyIcon = showCheck ? ICON_MDI_CHECK : ICON_MDI_CONTENT_COPY;

            if (showCheck)
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.39f, 0.90f, 0.47f, 1.0f));
            if (ui::IconButtonSquare2("cpy", cpyIcon, btnSz))
            {
                ClipboardCopyPassword(pw.c_str());
                s_copy_time = now;
            }
            if (showCheck)
                ImGui::PopStyleColor();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
                SetTooltipPadded(showCheck ? "Copied!" : "Copy");
        }

        ImGui::SameLine(0, gap);
        ImGui::SetCursorPosY(btnY);

        if (ui::IconButtonSquare2("gen_settings", ICON_MDI_TUNE_VERTICAL, btnSz))
            ImGui::OpenPopup("##gen_options_popup");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
            SetTooltipPadded("Generator Settings");

        ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, 12.f);
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IsDarkTheme() ? theme::PopupBg.dark : theme::PopupBg.light);
        if (ImGui::BeginPopup("##gen_options_popup"))
        {
            ImGui::PushFont(render::FontRegular);
            ImGui::Text("Password Generator");
            ImGui::PopFont();
            ImGui::Spacing();
            ImGui::Spacing();

            auto prev = genOpt;

            ImGui::SetNextItemWidth(150.0f);
            ImGui::SliderInt5("Length", &genOpt.length, 4, 64);
            ImGui::Spacing();
            ToggleSwitch("##gen_upper", &genOpt.use_upper); ImGui::SameLine(0, 4); ImGui::TextUnformatted("Uppercase (A-Z)");
            ToggleSwitch("##gen_lower", &genOpt.use_lower); ImGui::SameLine(0, 4); ImGui::TextUnformatted("Lowercase (a-z)");
            ToggleSwitch("##gen_digit", &genOpt.use_digit); ImGui::SameLine(0, 4); ImGui::TextUnformatted("Digits (0-9)");
            ToggleSwitch("##gen_sym", &genOpt.use_symbol); ImGui::SameLine(0, 4); ImGui::TextUnformatted("Symbols (!@#...)");
            ImGui::Spacing();
            ToggleSwitch("##gen_ambig", &genOpt.avoid_ambiguous); ImGui::SameLine(0, 4); ImGui::TextUnformatted("Avoid Ambiguous (0O, 1lI)");

            bool optChanged = genOpt.length != prev.length
                || genOpt.use_upper != prev.use_upper
                || genOpt.use_lower != prev.use_lower
                || genOpt.use_digit != prev.use_digit
                || genOpt.use_symbol != prev.use_symbol
                || genOpt.avoid_ambiguous != prev.avoid_ambiguous;

            if (optChanged)
            {
                std::string g = helpers::generate_password(genOpt);
                if (!g.empty())
                {
                    pw = g;
                    changed = true;
                }
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor();
        ImGui::PopStyleVar();

        ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 4.0f);
        DrawStrengthMeterCompact(pw, full_w);

        ImGui::PopID();
        return changed;
    }

    bool BarMenuItem(const char* label, bool selected, bool enabled, bool dim_unselected)
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float rounding = 4.0f;
        const float dot_r = 3.0f;
        const bool has_indicator = dim_unselected || selected;
        const float dot_col_w = has_indicator ? 16.0f : 0.0f;

        ImGui::Dummy(ImVec2(0, 2.5f));

        if (!enabled) ImGui::BeginDisabled();

        if (dim_unselected && !selected)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_Text, 0.55f));

        if (dot_col_w > 0.0f) ImGui::Indent(dot_col_w);
        ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered,  IM_COL32(0,0,0,0));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,   IM_COL32(0,0,0,0));
        ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, 0.5f));
        bool clicked = ImGui::Selectable(label, false, ImGuiSelectableFlags_None, ImVec2(0, ImGui::GetTextLineHeight() + 6.0f));
        ImGui::PopStyleVar(1);
        ImGui::PopStyleColor(3);
        if (dot_col_w > 0.0f) ImGui::Unindent(dot_col_w);

        if (dim_unselected && !selected)
            ImGui::PopStyleColor();

        if (!enabled) ImGui::EndDisabled();

        ImVec2 itemMin = ImGui::GetItemRectMin();
        ImVec2 itemMax = ImGui::GetItemRectMax();
        bool hovered = enabled && ImGui::IsItemHovered();

        if (hovered)
        {
            float winX = ImGui::GetWindowPos().x;
            float winW = ImGui::GetWindowSize().x;
            ImVec2 hMin(winX, itemMin.y);
            ImVec2 hMax(winX + winW, itemMax.y);
            dl->AddRectFilled(hMin, hMax, ImGui::GetColorU32(ImGuiCol_Text, 0.08f), rounding);
        }

        if (selected)
        {
            float dot_x = itemMin.x - dot_col_w * 0.5f;
            float dot_y = (itemMin.y + itemMax.y) * 0.5f;
            dl->AddCircleFilled(ImVec2(dot_x, dot_y), dot_r, ImGui::GetColorU32(ImGuiCol_Text, 0.9f));
        }

        return clicked;
    }

    bool BarMenuItemToggle(const char* label, bool* p_selected, bool enabled, bool dim_unselected)
    {
        bool clicked = BarMenuItem(label, *p_selected, enabled, dim_unselected);
        if (clicked) *p_selected = !*p_selected;
        return clicked;
    }

    void DrawScrollTopFade(ImVec2 childMin, float childWidth, float scrollY, float yOffset)
    {
        if (scrollY <= 0.0f) return;
        const float fadeH = 8.0f;
        float alpha = ImClamp(scrollY / 40.0f, 0.0f, 1.0f);
        ImU8 a = (ImU8)(25 * alpha);
        ImU32 top = IM_COL32(0, 0, 0, a);
        ImU32 bot = IM_COL32(0, 0, 0, 0);
        ImVec2 start(childMin.x, childMin.y + yOffset);
        ImGui::GetWindowDrawList()->AddRectFilledMultiColor(
            start,
            ImVec2(start.x + childWidth, start.y + fadeH),
            top, top, bot, bot);
    }

    // PopupStyleBegin/End: call right after BeginPopup succeeds / right before EndPopup
    static float s_popup_fade = 0.0f;

    void PopupStyleBegin()
    {
        ImGuiWindow* win = ImGui::GetCurrentWindow();
        ImDrawList* dl = win->DrawList;

        if (ImGui::IsWindowAppearing()) s_popup_fade = 0.0f; // ~120ms fade-in
        s_popup_fade = ImMin(s_popup_fade + ImGui::GetIO().DeltaTime * 8.5f, 1.0f);
        float fade = s_popup_fade;

        ImVec2 popMin = win->Pos;
        ImVec2 popMax(popMin.x + win->Size.x, popMin.y + win->Size.y);
        float rounding = win->WindowRounding;

        dl->ChannelsSplit(2); // ch0 = shadow (behind), ch1 = content
        dl->ChannelsSetCurrent(0);
        dl->PushClipRectFullScreen();

        {
            const float shadowOff = 1.0f;
            for (int i = 1; i <= 3; i++)
            {
                float expand = (float)i * 1.5f;
                ImU8 a = (ImU8)((14 - i * 3) * fade);
                dl->AddRectFilled(
                    ImVec2(popMin.x - expand * 0.2f, popMin.y + shadowOff),
                    ImVec2(popMax.x + expand * 0.2f, popMax.y + expand * 0.4f + shadowOff),
                    IM_COL32(0, 0, 0, a),
                    rounding + expand * 0.3f);
            }
        }

        ImU32 popupBg = ImGui::GetColorU32(ImGuiCol_PopupBg);
        dl->AddRectFilled(popMin, popMax, popupBg, rounding);

        // gradient border: TL bright → BR dark
        {
            bool dark = g_shell_ptr ? g_shell_ptr->dark_theme : true;
            ImU32 colTL = theme::BevelTL;
            ImU32 colBR = theme::BevelBR;
            dl->AddRectFilledMultiColor(
                ImVec2(popMin.x + rounding, popMin.y),
                ImVec2(popMax.x - rounding, popMin.y + 1.0f),
                colTL, colBR, colBR, colTL);
            dl->AddRectFilled(
                ImVec2(popMin.x + rounding * 1.f, popMax.y + 0.0f),
                ImVec2(popMax.x - rounding * 1.f, popMax.y + 1.0f),
                colBR);
            dl->AddRectFilledMultiColor(
                ImVec2(popMin.x, popMin.y + rounding),
                ImVec2(popMin.x + 1.0f, popMax.y - rounding + 1.0f),
                colTL, colTL, colBR, colBR);
            dl->AddRectFilledMultiColor(
                ImVec2(popMax.x - 1.0f, popMin.y + rounding),
                ImVec2(popMax.x, popMax.y - rounding + 1.0f),
                colBR, colBR, colBR, colBR);
            float r = rounding - 0.5f; // arcs inset 0.5px to align with 1px border lines
            ImU32 colTR = theme::BevelTR;
            ImU32 colBL = theme::BevelBL;
            dl->PathArcTo(ImVec2(popMin.x + rounding, popMin.y + rounding), r, IM_PI, IM_PI * 1.5f, 8);
            dl->PathStroke(colTL, 0, 1.0f);
            dl->PathArcTo(ImVec2(popMax.x - rounding, popMin.y + rounding), r, IM_PI * 1.5f, IM_PI * 2.0f, 8);
            dl->PathStroke(colTR, 0, 1.0f);
            dl->PathArcTo(ImVec2(popMin.x + rounding, popMax.y - rounding + 1.0f), r, IM_PI * 0.5f, IM_PI, 8);
            dl->PathStroke(colBL, 0, 1.0f);
            dl->PathArcTo(ImVec2(popMax.x - rounding, popMax.y - rounding + 1.0f), r, 0.0f, IM_PI * 0.5f, 8);
            dl->PathStroke(colBR, 0, 1.0f);
        }

        dl->PopClipRect();
        dl->ChannelsSetCurrent(1);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, fade);
    }

    void PopupStyleEnd()
    {
        ImGui::PopStyleVar(); // Alpha
        ImGui::GetWindowDrawList()->ChannelsMerge();
    }

    static LiftedChildColorSet g_lifted_colors = {};

    void SetLiftedChildColors(const LiftedChildColorSet& colors)
    {
        g_lifted_colors = colors;
    }

    bool BeginLiftedChild(const char* id, ImVec2 size, ImGuiWindowFlags flags)
    {
        ImGui::PushStyleColor(ImGuiCol_Border, g_lifted_colors.borderOuter);
        ImGui::PushStyleColor(ImGuiCol_ChildBg, g_lifted_colors.bg);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, 1.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 6.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        bool visible = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY, flags);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(2);

        return visible;
    }

    void EndLiftedChild()
    {
        ImVec2 wMin = ImGui::GetWindowPos();
        ImVec2 wMax(wMin.x + ImGui::GetWindowSize().x, wMin.y + ImGui::GetWindowSize().y);

        // inner highlight border (inset 1px) — top/left brighter for lift effect
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRect(
            ImVec2(wMin.x + 1.0f, wMin.y + 1.0f),
            ImVec2(wMax.x - 1.0f, wMax.y - 1.0f),
            g_lifted_colors.borderInner, 5.0f, 0, 1.0f);

        ImGui::EndChild();

        ImDrawList* pdl = ImGui::GetWindowDrawList();
        ImVec2 rMin = ImGui::GetItemRectMin();
        ImVec2 rMax = ImGui::GetItemRectMax();
        pdl->AddLine(ImVec2(rMin.x + 3.0f, rMax.y), ImVec2(rMax.x - 3.0f, rMax.y), IM_COL32(0, 0, 0, 20), 1.0f);
        pdl->AddLine(ImVec2(rMin.x + 5.0f, rMax.y + 1.0f), ImVec2(rMax.x - 5.0f, rMax.y + 1.0f), IM_COL32(0, 0, 0, 12), 1.0f);
    }

    static LippedChildColorSet g_lipped_colors = {};

    void SetLippedChildColors(const LippedChildColorSet& colors)
    {
        g_lipped_colors = colors;
    }

    bool BeginLippedChild(const char* id, ImVec2 size, ImGuiWindowFlags flags)
    {
        const float lip_offset = 3.0f;
        const float rounding = 10.0f;

        if (size.y == 0.0f)
            size.y = ImGui::GetContentRegionAvail().y - lip_offset; // leave room for lip

        // draw lip rect first (behind child), shifted down
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float lip_inset = 2.0f;
        dl->AddRectFilled(
            ImVec2(pos.x - lip_inset, pos.y + lip_offset),
            ImVec2(pos.x + size.x, pos.y + lip_offset + size.y),
            g_lipped_colors.lip_color,
            rounding);

        ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, rounding);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 8));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, g_lipped_colors.bg);
        ImGui::PushStyleColor(ImGuiCol_Border, IM_COL32(0, 0, 0, 0));
        bool visible = ImGui::BeginChild(id, size, ImGuiChildFlags_AlwaysUseWindowPadding, flags);
        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(2);
        return visible;
    }

    void EndLippedChild()
    {
        ImGui::EndChild();
    }

    // on-screen keyboard — foreground draw list overlay, no ImGui window
    bool g_show_osk = false;

    static int    s_osk_layout = 0;  // 0=lower, 1=upper, 2=numbers/symbols
    static ImVec2 s_osk_pos(0, 0);
    static ImVec2 s_osk_size(440, 220);
    static bool   s_osk_initialized = false;
    static bool   s_osk_dragging = false;
    static ImVec2 s_osk_drag_offset(0, 0);

    static const char* s_osk_rows_lower[] = {
        "q", "w", "e", "r", "t", "y", "u", "i", "o", "p",
        "a", "s", "d", "f", "g", "h", "j", "k", "l",
        "z", "x", "c", "v", "b", "n", "m",
    };
    static const char* s_osk_rows_upper[] = {
        "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
        "A", "S", "D", "F", "G", "H", "J", "K", "L",
        "Z", "X", "C", "V", "B", "N", "M",
    };
    static const char* s_osk_rows_sym[] = {
        "1", "2", "3", "4", "5", "6", "7", "8", "9", "0",
        "!", "@", "#", "$", "%", "^", "&", "*", "(", ")",
        "-", "_", "=", "+", "[", "]", "{", "}",
        ";", ":", "'", "\"", ",", ".", "/", "?",
    };

    static const int s_row_starts_alpha[] = { 0, 10, 19 };
    static const int s_row_lens_alpha[]   = { 10, 9, 7 };
    static const int s_row_starts_sym[]   = { 0, 10, 20, 28 };
    static const int s_row_lens_sym[]     = { 10, 10, 8, 8 };

    // OSK click state — set by OskPreFrame, consumed by RenderOnScreenKeyboard
    static bool s_osk_click_swallowed = false;  // true = we ate a left-click this frame
    static bool s_osk_mouse_down = false;       // mouse held over OSK

    void OskPreFrame()
    {
        s_osk_click_swallowed = false;
        s_osk_mouse_down = false;
        if (!g_show_osk || !s_osk_initialized) return;

        ImGuiIO& io = ImGui::GetIO();
        ImVec2 mouse = io.MousePos;
        ImVec2 pMin = s_osk_pos;
        ImVec2 pMax = ImVec2(pMin.x + s_osk_size.x, pMin.y + s_osk_size.y);

        bool mouseInOsk = (mouse.x >= pMin.x && mouse.x <= pMax.x &&
                           mouse.y >= pMin.y && mouse.y <= pMax.y);
        if (!mouseInOsk) return;

        // Swallow the click so input fields don't see it and deactivate
        if (io.MouseClicked[0])
        {
            io.MouseClicked[0] = false;
            s_osk_click_swallowed = true;
        }
        s_osk_mouse_down = io.MouseDown[0];
    }

    void RenderOnScreenKeyboard()
    {
        if (!g_show_osk) return;

        const bool dark = g_shell_ptr ? g_shell_ptr->dark_theme : true;
        ImGuiIO& io = ImGui::GetIO();
        ImDrawList* dl = ImGui::GetForegroundDrawList();

        if (!s_osk_initialized)
        {
            s_osk_pos = ImVec2(io.DisplaySize.x * 0.5f - s_osk_size.x * 0.5f,
                               io.DisplaySize.y - s_osk_size.y - 20);
            s_osk_initialized = true;
        }

        const float rounding = 12.0f;
        const float titleH = 28.0f;
        const float pad = 10.0f;
        const float keyGap = 4.0f;
        const float keyRounding = 6.0f;

        ImVec2 pMin = s_osk_pos;
        ImVec2 pMax = ImVec2(pMin.x + s_osk_size.x, pMin.y + s_osk_size.y);
        ImVec2 mouse = io.MousePos;
        bool mouseInOsk = (mouse.x >= pMin.x && mouse.x <= pMax.x &&
                           mouse.y >= pMin.y && mouse.y <= pMax.y);

        ImVec2 titleMin = pMin;
        ImVec2 titleMax = ImVec2(pMax.x, pMin.y + titleH);
        bool mouseInTitle = (mouse.x >= titleMin.x && mouse.x <= titleMax.x &&
                             mouse.y >= titleMin.y && mouse.y <= titleMax.y);

        if (mouseInTitle && s_osk_click_swallowed)
        {
            s_osk_dragging = true;
            s_osk_drag_offset = ImVec2(mouse.x - pMin.x, mouse.y - pMin.y);
        }
        if (s_osk_dragging)
        {
            if (s_osk_mouse_down)
            {
                s_osk_pos = ImVec2(mouse.x - s_osk_drag_offset.x, mouse.y - s_osk_drag_offset.y);
                // Clamp to screen
                if (s_osk_pos.x < 0) s_osk_pos.x = 0;
                if (s_osk_pos.y < 0) s_osk_pos.y = 0;
                if (s_osk_pos.x + s_osk_size.x > io.DisplaySize.x)
                    s_osk_pos.x = io.DisplaySize.x - s_osk_size.x;
                if (s_osk_pos.y + s_osk_size.y > io.DisplaySize.y)
                    s_osk_pos.y = io.DisplaySize.y - s_osk_size.y;
                pMin = s_osk_pos;
                pMax = ImVec2(pMin.x + s_osk_size.x, pMin.y + s_osk_size.y);
            }
            else
                s_osk_dragging = false;
        }

        dl->AddRectFilled(ImVec2(pMin.x + 2, pMin.y + 3), ImVec2(pMax.x + 2, pMax.y + 3),
            IM_COL32(0, 0, 0, dark ? 80 : 40), rounding);
        ImU32 bgCol = dark ? theme::DropdownBg.dark : theme::DropdownBg.light;
        dl->AddRectFilled(pMin, pMax, bgCol, rounding);

        // gradient border (same bevel scheme as popups)
        {
            ImU32 colTL = theme::BevelTL;
            ImU32 colBR = theme::BevelBR;
            ImU32 colTR = theme::BevelTR;
            ImU32 colBL = theme::BevelBL;
            float r = rounding;
            dl->AddRectFilledMultiColor(
                ImVec2(pMin.x + r, pMin.y), ImVec2(pMax.x - r, pMin.y + 1.0f),
                colTL, colBR, colBR, colTL);
            dl->AddRectFilled(
                ImVec2(pMin.x + r, pMax.y - 1.0f), ImVec2(pMax.x - r, pMax.y), colBR);
            dl->AddRectFilledMultiColor(
                ImVec2(pMin.x, pMin.y + r), ImVec2(pMin.x + 1.0f, pMax.y - r),
                colTL, colTL, colBR, colBR);
            dl->AddRectFilledMultiColor(
                ImVec2(pMax.x - 1.0f, pMin.y + r), ImVec2(pMax.x, pMax.y - r),
                colBR, colBR, colBR, colBR);
            float cr = r - 0.5f;
            dl->PathArcTo(ImVec2(pMin.x + r, pMin.y + r), cr, IM_PI, IM_PI * 1.5f, 8);
            dl->PathStroke(colTL, 0, 1.0f);
            dl->PathArcTo(ImVec2(pMax.x - r, pMin.y + r), cr, IM_PI * 1.5f, IM_PI * 2.0f, 8);
            dl->PathStroke(colTR, 0, 1.0f);
            dl->PathArcTo(ImVec2(pMin.x + r, pMax.y - r), cr, IM_PI * 0.5f, IM_PI, 8);
            dl->PathStroke(colBL, 0, 1.0f);
            dl->PathArcTo(ImVec2(pMax.x - r, pMax.y - r), cr, 0.0f, IM_PI * 0.5f, 8);
            dl->PathStroke(colBR, 0, 1.0f);
        }

        {
            ImU32 titleBg = dark ? theme::InputBg.dark : theme::InputBg.light;
            dl->AddRectFilled(titleMin, titleMax, titleBg, rounding, ImDrawFlags_RoundCornersTop);
            const char* title = ICON_MDI_KEYBOARD "  Keyboard";
            ImVec2 tSz = ImGui::CalcTextSize(title);
            ImU32 titleText = dark ? IM_COL32(200, 200, 200, 255) : IM_COL32(60, 60, 60, 255);
            dl->AddText(ImVec2(titleMin.x + 10, titleMin.y + (titleH - tSz.y) * 0.5f), titleText, title);

            const char* closeIco = ICON_MDI_CLOSE;
            ImVec2 cSz = ImGui::CalcTextSize(closeIco);
            float closeX = titleMax.x - cSz.x - 10;
            float closeY = titleMin.y + (titleH - cSz.y) * 0.5f;
            ImVec2 closeMin(closeX - 4, titleMin.y + 2);
            ImVec2 closeMax(closeX + cSz.x + 4, titleMax.y - 2);
            bool closeHov = (mouse.x >= closeMin.x && mouse.x <= closeMax.x &&
                             mouse.y >= closeMin.y && mouse.y <= closeMax.y);
            if (closeHov)
                dl->AddRectFilled(closeMin, closeMax,
                    dark ? theme::ErrorHighlight.dark : theme::ErrorHighlight.light, 4.0f);
            dl->AddText(ImVec2(closeX, closeY),
                closeHov ? theme::DeleteBtnText.dark : (dark ? IM_COL32(160, 160, 160, 255) : IM_COL32(120, 120, 120, 255)),
                closeIco);
            if (closeHov && s_osk_click_swallowed)
            {
                g_show_osk = false;
                return;
            }
        }

        ImU32 keyBg     = dark ? IM_COL32(55, 55, 60, 255)  : IM_COL32(255, 255, 255, 255);
        ImU32 keyBgHov  = dark ? IM_COL32(75, 75, 82, 255)  : IM_COL32(230, 230, 235, 255);
        ImU32 keyBgAct  = dark ? IM_COL32(90, 90, 100, 255) : IM_COL32(210, 210, 218, 255);
        ImU32 specKeyBg = dark ? IM_COL32(45, 45, 50, 255)  : IM_COL32(210, 210, 218, 255);
        ImU32 keyText   = dark ? IM_COL32(240, 240, 240, 255) : IM_COL32(30, 30, 30, 255);

        static ImVec2 s_pressed_pos(0, 0);
        static int s_pressed_frames = 0;
        if (s_pressed_frames > 0) s_pressed_frames--;

        auto DrawKey = [&](const char* label, ImVec2 kMin, float w, float h, ImU32 bg) -> bool
        {
            ImVec2 kMax(kMin.x + w, kMin.y + h);
            bool hov = (mouse.x >= kMin.x && mouse.x <= kMax.x &&
                        mouse.y >= kMin.y && mouse.y <= kMax.y);
            bool clicked = hov && s_osk_click_swallowed;
            bool held = hov && s_osk_mouse_down;
            bool flash = (s_pressed_frames > 0 &&
                          s_pressed_pos.x == kMin.x && s_pressed_pos.y == kMin.y);

            if (clicked) { s_pressed_pos = kMin; s_pressed_frames = 3; }

            ImU32 col = (held || flash) ? keyBgAct : (hov ? keyBgHov : bg);

            dl->AddRectFilled(ImVec2(kMin.x, kMin.y + 1.0f), ImVec2(kMax.x, kMax.y + 1.0f),
                dark ? theme::ComboShadow.dark : theme::ComboShadow.light, keyRounding);
            dl->AddRectFilled(kMin, kMax, col, keyRounding);
            ImVec2 tSz = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(kMin.x + (w - tSz.x) * 0.5f, kMin.y + (h - tSz.y) * 0.5f), keyText, label);

            return clicked;
        };

        const char** keys;
        const int* rowStarts;
        const int* rowLens;
        int numCharRows;

        if (s_osk_layout == 0)      { keys = s_osk_rows_lower; rowStarts = s_row_starts_alpha; rowLens = s_row_lens_alpha; numCharRows = 3; }
        else if (s_osk_layout == 1) { keys = s_osk_rows_upper; rowStarts = s_row_starts_alpha; rowLens = s_row_lens_alpha; numCharRows = 3; }
        else                        { keys = s_osk_rows_sym;   rowStarts = s_row_starts_sym;   rowLens = s_row_lens_sym;   numCharRows = 4; }

        float contentX = pMin.x + pad;
        float contentY = pMin.y + titleH + pad * 0.5f;
        float contentW = s_osk_size.x - pad * 2;
        float contentH = s_osk_size.y - titleH - pad;

        int totalRows = numCharRows + 1; // +1 for bottom row
        float keyH = (contentH - keyGap * (totalRows - 1)) / (float)totalRows;
        if (keyH < 22.0f) keyH = 22.0f;
        if (keyH > 42.0f) keyH = 42.0f;

        float curY = contentY;

        for (int row = 0; row < numCharRows; row++)
        {
            int start = rowStarts[row];
            int len = rowLens[row];
            float keyW = (contentW - keyGap * (len - 1)) / (float)len;
            float rowTotalW = keyW * len + keyGap * (len - 1);
            float offsetX = contentX + (contentW - rowTotalW) * 0.5f;

            for (int k = 0; k < len; k++)
            {
                ImVec2 kPos(offsetX + k * (keyW + keyGap), curY);
                if (DrawKey(keys[start + k], kPos, keyW, keyH, keyBg))
                {
                    const char* ch = keys[start + k];
                    if (ch[0] && !ch[1])
                        io.AddInputCharacter((unsigned int)ch[0]);
                    else
                        for (const char* p = ch; *p; p++)
                            io.AddInputCharacter((unsigned int)*p);
                }
            }
            curY += keyH + keyGap;
        }

        {
            float specialW = contentW * 0.13f;
            float spaceW = contentW - specialW * 4 - keyGap * 4;
            float bx = contentX;

            if (DrawKey(ICON_MDI_APPLE_KEYBOARD_SHIFT, ImVec2(bx, curY), specialW, keyH, specKeyBg))
                s_osk_layout = (s_osk_layout == 1) ? 0 : 1;
            bx += specialW + keyGap;

            const char* togLabel = (s_osk_layout == 2) ? "ABC" : "123";
            if (DrawKey(togLabel, ImVec2(bx, curY), specialW, keyH, specKeyBg))
                s_osk_layout = (s_osk_layout == 2) ? 0 : 2;
            bx += specialW + keyGap;

            if (DrawKey(" ", ImVec2(bx, curY), spaceW, keyH, keyBg))
                io.AddInputCharacter(' ');
            bx += spaceW + keyGap;

            if (DrawKey(ICON_MDI_BACKSPACE_OUTLINE, ImVec2(bx, curY), specialW, keyH, specKeyBg))
            {
                io.AddKeyEvent(ImGuiKey_Backspace, true);
                io.AddKeyEvent(ImGuiKey_Backspace, false);
            }
            bx += specialW + keyGap;

            if (DrawKey(ICON_MDI_KEYBOARD_RETURN, ImVec2(bx, curY), specialW, keyH, specKeyBg))
            {
                io.AddKeyEvent(ImGuiKey_Enter, true);
                io.AddKeyEvent(ImGuiKey_Enter, false);
            }
        }

        if (mouseInOsk) // prevent click-through to widgets behind the keyboard
            io.WantCaptureMouse = true;
    }

} // namespace ui
