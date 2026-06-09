// ui_modals.cpp — toasts, clipboard auto-clear, reprompt, trash, security center, recovery key
#include "ui_internal.h"
#include "vault_db.h"

namespace ui
{
    namespace {
        struct Toast {
            std::string message;
            ToastType   type;
            float       duration;
            float       elapsed;
        };
        std::vector<Toast> g_toasts;

        ImU32 GetToastColor(ToastType type) {
            switch (type) {
                case ToastType::Success: return colors::ToastSuccess;
                case ToastType::Error:   return colors::ToastError;
                case ToastType::Info:    return colors::ToastInfo;
            }
            return colors::ToastDefault;
        }

        const char* GetToastIcon(ToastType type) {
            switch (type) {
                case ToastType::Success: return ICON_MDI_CHECK;
                case ToastType::Error:   return ICON_MDI_CLOSE;
                case ToastType::Info:    return ICON_MDI_INFORMATION;
            }
            return "";
        }
    }

    void ShowToast(const char* message, ToastType type, float duration) {
        if (g_toasts.size() >= 5) {
            g_toasts.erase(g_toasts.begin());
        }
        g_toasts.push_back({ message, type, duration, 0.0f });
    }

    void RenderToasts() {
        if (g_toasts.empty()) return;

        ImGuiIO& io = ImGui::GetIO();
        const float dt = io.DeltaTime;

        const float toastW = 220.0f;
        const float toastH = 36.0f;
        const float padding = 12.0f;
        const float spacing = 8.0f;
        const float fadeTime = 0.25f;
        const float cornerRadius = 6.0f;

        const float startX = io.DisplaySize.x - toastW - padding;
        float currentY = io.DisplaySize.y - padding;

        ImDrawList* dl = ImGui::GetForegroundDrawList();

        for (int i = (int)g_toasts.size() - 1; i >= 0; --i) {
            Toast& t = g_toasts[i];
            t.elapsed += dt;

            float alpha = 1.0f;
            if (t.elapsed < fadeTime) {
                alpha = t.elapsed / fadeTime;
            } else if (t.elapsed > t.duration - fadeTime) {
                alpha = (t.duration - t.elapsed) / fadeTime;
            }
            alpha = ImClamp(alpha, 0.0f, 1.0f);

            if (t.elapsed >= t.duration) {
                g_toasts.erase(g_toasts.begin() + i);
                continue;
            }

            currentY -= toastH;

            ImU32 bgColor = GetToastColor(t.type);
            bgColor = (bgColor & 0x00FFFFFF) | ((ImU32)(alpha * 230) << 24);

            ImVec2 pMin(startX, currentY);
            ImVec2 pMax(startX + toastW, currentY + toastH);
            dl->AddRectFilled(pMin, pMax, bgColor, cornerRadius);

            const char* icon = GetToastIcon(t.type);
            ImU32 textCol = IM_COL32(255, 255, 255, (ImU32)(alpha * 255));
            ImVec2 iconPos(startX + 10.0f, currentY + (toastH - ImGui::GetTextLineHeight()) * 0.5f);
            dl->AddText(iconPos, textCol, icon);

            ImVec2 textPos(startX + 32.0f, currentY + (toastH - ImGui::GetTextLineHeight()) * 0.5f);
            dl->AddText(textPos, textCol, t.message.c_str());

            currentY -= spacing;
        }
    }

    namespace {
        std::string g_clipboard_password;
        float g_clipboard_copy_time = 0.0f;
    }

    void ClipboardCopyPassword(const char* password) {
        ImGui::SetClipboardText(password);
        g_clipboard_password = password;
        g_clipboard_copy_time = (float)ImGui::GetTime();
        ShowToast("Password copied", ToastType::Success);
    }

    void TickClipboardClear() {
        if (g_clipboard_password.empty()) return;

        int delay = cfg::get_clipboard_clear_delay();
        if (delay == 0) {
            // delay==0 means "Never" — clear tracking only, leave clipboard alone
            g_clipboard_password.clear();
            return;
        }

        const float now = (float)ImGui::GetTime();
        if (now - g_clipboard_copy_time >= (float)delay) {
            ImGui::SetClipboardText("");
            ShowToast("Clipboard cleared", ToastType::Info);
            g_clipboard_password.clear();
        }
    }

    namespace {
        RepromptAction g_reprompt_pending = RepromptAction::None;
        RepromptAction g_reprompt_approved = RepromptAction::None;
        std::string g_reprompt_password_buf;
        std::string g_reprompt_error;
        int g_reprompt_failed_count = 0;
        bool g_reprompt_locked_out = false;
        bool g_reprompt_modal_open = false;
        std::string g_reprompt_master_password; // stored for verification

        const char* GetRepromptActionName(RepromptAction action) {
            switch (action) {
                case RepromptAction::RevealPassword: return "reveal password";
                case RepromptAction::RevealNotes: return "reveal notes";
                case RepromptAction::Export: return "export vault";
                case RepromptAction::DisableReadOnly: return "disable read-only mode";
                case RepromptAction::DisableSecuritySetting: return "disable this security setting";
                default: return "this action";
            }
        }
    }

    void SetRepromptMasterPassword(const std::string& password) {
        g_reprompt_master_password = password;
    }

    bool RequestReprompt(RepromptAction action, const std::string& master_password) {
        if (g_reprompt_locked_out) return false;

        if (!master_password.empty()) {
            g_reprompt_master_password = master_password;
        }

        bool needs_reprompt = false;
        switch (action) {
            case RepromptAction::RevealPassword:
                needs_reprompt = cfg::get_reprompt_reveal_password();
                break;
            case RepromptAction::RevealNotes:
                needs_reprompt = cfg::get_reprompt_reveal_notes();
                break;
            case RepromptAction::Export:
                needs_reprompt = cfg::get_reprompt_export();
                break;
            case RepromptAction::DisableReadOnly:
                needs_reprompt = cfg::get_reprompt_disable_readonly();
                break;
            case RepromptAction::DisableSecuritySetting:
                needs_reprompt = true;
                break;
            default:
                break;
        }

        if (!needs_reprompt) return true;

        g_reprompt_pending = action;
        g_reprompt_password_buf.clear();
        g_reprompt_error.clear();
        g_reprompt_modal_open = true;
        return false;
    }

    bool IsRepromptApproved(RepromptAction action) {
        return g_reprompt_approved == action;
    }

    void ClearRepromptApproval(RepromptAction action) {
        if (g_reprompt_approved == action) {
            g_reprompt_approved = RepromptAction::None;
        }
    }

    bool IsRepromptLockedOut() {
        return g_reprompt_locked_out;
    }

    void ResetRepromptLockout() {
        g_reprompt_failed_count = 0;
        g_reprompt_locked_out = false;
    }

    void RenderRepromptModal() {
        if (!g_reprompt_modal_open) return;

        if (!ImGui::IsPopupOpen("Master Password###reprompt_modal"))
            ImGui::OpenPopup("Master Password###reprompt_modal");

        const float modalW = 340.0f;
        ImGui::SetNextWindowSizeConstraints(ImVec2(modalW, 0), ImVec2(modalW, FLT_MAX));

        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, colors::DimOverlay);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
        bool modal_open = true;
        if (ImGui::BeginPopupModal("Master Password###reprompt_modal", &modal_open,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
        {
            bool escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
            bool submit_shortcut = ImGui::IsKeyPressed(ImGuiKey_Enter);

            ImGui::PushFont(render::FontLarge);
            ImGui::TextUnformatted(ICON_MDI_LOCK "  Confirm Password");
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 4));
            ImGui::PushFont(render::FontSmall);
            ImGui::TextWrapped("Enter master password to %s.", GetRepromptActionName(g_reprompt_pending));
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 8));

            ImGui::SetNextItemWidth(-1);
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            InputTextPasswordReveal("##reprompt_pw", &g_reprompt_password_buf);

            if (!g_reprompt_error.empty()) {
                ImGui::Dummy(ImVec2(0, 4));
                ImGui::PushStyleColor(ImGuiCol_Text, colors::ErrorTextAlt);
                ImGui::TextWrapped("%s", g_reprompt_error.c_str());
                ImGui::PopStyleColor();
            }

            ImGui::Dummy(ImVec2(0, 12));

            const float btnW = 100.0f;
            const float btnH = 32.0f;
            float totalBtnW = btnW * 2 + 8.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (modalW - totalBtnW) * 0.5f - 8.0f);

            if (StyledButton("##reprompt_cancel", "Cancel", ImVec2(btnW, btnH)) || escape_pressed) {
                g_reprompt_pending = RepromptAction::None;
                g_reprompt_password_buf.clear();
                g_reprompt_error.clear();
                g_reprompt_modal_open = false;
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine(0, 8);

            bool can_submit = !g_reprompt_password_buf.empty() && !g_reprompt_locked_out;
            ImGui::BeginDisabled(!can_submit);
            if ((StyledButton("##reprompt_confirm", "Confirm", ImVec2(btnW, btnH)) || submit_shortcut) && can_submit) {
                if (g_reprompt_password_buf == g_reprompt_master_password) {
                    g_reprompt_approved = g_reprompt_pending;
                    g_reprompt_pending = RepromptAction::None;
                    g_reprompt_password_buf.clear();
                    g_reprompt_error.clear();
                    g_reprompt_failed_count = 0;
                    g_reprompt_modal_open = false;
                    ImGui::CloseCurrentPopup();
                } else {
                    g_reprompt_failed_count++;
                    int lockout_count = cfg::get_reprompt_lockout_count();
                    int remaining = lockout_count - g_reprompt_failed_count;

                    if (remaining <= 0) {
                        g_reprompt_locked_out = true;
                        g_reprompt_error = "Too many failed attempts. Vault locked.";
                        ShowToast("Vault locked - too many attempts", ToastType::Error, 4.0f);
                    } else {
                        g_reprompt_error = "Incorrect password. " + std::to_string(remaining) + " attempt(s) remaining.";
                    }
                    g_reprompt_password_buf.clear();
                }
            }
            ImGui::EndDisabled();

            ImGui::EndPopup();
        }
        ImGui::PopStyleVar();   // WindowPadding
        ImGui::PopStyleColor(); // ModalWindowDimBg

        if (!modal_open) {
            g_reprompt_pending = RepromptAction::None;
            g_reprompt_password_buf.clear();
            g_reprompt_error.clear();
            g_reprompt_modal_open = false;
        }
    }

    static std::vector<Credential> s_trash_items;
    static bool s_trash_needs_refresh = true;

    void RenderTrashModal(ShellState& s)
    {
        if (s.trash_modal_open)
        {
            ImGui::OpenPopup("Trash Bin###trash_modal");
            s.trash_modal_open = false;
            s_trash_needs_refresh = true;
        }

        const bool dark = s.dark_theme;
        const ImU32 popupBg = dark
            ? theme::ModalBg.dark
            : theme::ModalBg.light;
        const ImU32 dimBg = colors::DimOverlay;

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(460, 0), ImVec2(460, FLT_MAX));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, popupBg);
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, dimBg);

        bool modal_open = true;
        if (ImGui::BeginPopupModal("Trash Bin###trash_modal", &modal_open,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar))
        {
            ImGui::PushFont(render::FontLarge);
            ImGui::TextUnformatted("Trash Bin");
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 4));

            if (s_trash_needs_refresh)
            {
                auto mk = Get2FAMasterKey();
                if (!mk.empty())
                    s_trash_items = cred_ops::load_deleted(mk);
                else
                    s_trash_items.clear();
                s_trash_needs_refresh = false;
            }

            {
                int retention = cfg::get_trash_retention_days();
                const char* labels[] = { "7 days", "14 days", "30 days", "60 days", "Never" };
                int values[] = { 7, 14, 30, 60, 0 };
                int current = 2; // default 30
                for (int i = 0; i < 5; i++) {
                    if (values[i] == retention) { current = i; break; }
                }

                ImGui::TextDisabled("Auto-delete after");
                if (AnimatedComboDot("##trash_retention", labels[current], labels, 5, &current, 120.0f, 30.0f, true))
                    cfg::set_trash_retention_days(values[current]);
            }

            ImGui::Dummy(ImVec2(0, 4));

            if (s_trash_items.empty())
            {
                ImGui::Dummy(ImVec2(0, 20));
                float textW = ImGui::CalcTextSize("Trash is empty").x;
                float cx = (ImGui::GetContentRegionAvail().x - textW) * 0.5f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx);
                ImGui::TextDisabled("Trash is empty");
                ImGui::Dummy(ImVec2(0, 20));
            }
            else
            {
                float listH = ImMax(ImMin((float)s_trash_items.size() * 46.0f, 360.0f), 174.0f);
                if (ImGui::BeginChild("##trash_list", ImVec2(0, listH), ImGuiChildFlags_Borders))
                {
                    ImDrawList* tdl = ImGui::GetWindowDrawList();
                    for (size_t i = 0; i < s_trash_items.size(); i++)
                    {
                        const auto& c = s_trash_items[i];
                        ImGui::PushID((int)i);

                        const float faviconSz = 22.0f;
                        const float rowH = 42.0f;
                        float btnSz = ImGui::GetFrameHeight();
                        float rightW = btnSz * 2 + ImGui::GetStyle().ItemSpacing.x;

                        ImVec2 rowMin = ImGui::GetCursorScreenPos();
                        float rowW = ImGui::GetContentRegionAvail().x;
                        float textH = ImGui::GetTextLineHeight();
                        float cy = rowMin.y + (rowH - textH) * 0.5f;
                        float faviconY = rowMin.y + (rowH - faviconSz) * 0.5f;

                        bool rowHov = ImGui::IsMouseHoveringRect(rowMin, ImVec2(rowMin.x + rowW, rowMin.y + rowH));
                        if (rowHov)
                            tdl->AddRectFilled(rowMin, ImVec2(rowMin.x + rowW, rowMin.y + rowH),
                                dark ? IM_COL32(255, 255, 255, 15) : IM_COL32(0, 0, 0, 8), 4.0f);

                        float contentX = rowMin.x + 6.0f;

                        auto srv = favicon::Get(c.website);
                        if (srv)
                        {
                            ImGui::SetCursorScreenPos(ImVec2(contentX, faviconY));
                            ImGui::Image((ImTextureID)srv, ImVec2(faviconSz, faviconSz));
                            contentX += faviconSz + 8.0f;
                        }
                        else
                        {
                            tdl->AddText(ImVec2(contentX, cy), ImGui::GetColorU32(ImGuiCol_TextDisabled), CredTypeIcon(c.type));
                            contentX += ImGui::CalcTextSize(CredTypeIcon(c.type)).x + 8.0f;
                        }

                        std::string title = c.title.empty() ? "(Untitled)" : c.title;
                        tdl->AddText(ImVec2(contentX, cy), ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
                        contentX += ImGui::CalcTextSize(title.c_str()).x + 8.0f;

                        if (c.deleted_at_ms > 0)
                        {
                            int64_t ago_ms = helpers::now_unix_ms() - c.deleted_at_ms;
                            std::string ago_text;
                            if (ago_ms < time_ms::MINUTE)      ago_text = "just now";
                            else if (ago_ms < time_ms::HOUR)   ago_text = std::to_string(ago_ms / time_ms::MINUTE) + "m ago";
                            else if (ago_ms < time_ms::DAY)    ago_text = std::to_string(ago_ms / time_ms::HOUR) + "h ago";
                            else if (ago_ms < time_ms::WEEK)   ago_text = std::to_string(ago_ms / time_ms::DAY) + "d ago";
                            else                               ago_text = std::to_string(ago_ms / time_ms::WEEK) + "w ago";
                            tdl->AddText(ImVec2(contentX, cy), ImGui::GetColorU32(ImGuiCol_TextDisabled), ago_text.c_str());
                        }

                        float btnY = rowMin.y + (rowH - btnSz) * 0.5f;
                        ImGui::SetCursorScreenPos(ImVec2(rowMin.x + rowW - rightW, btnY));

                        if (StyledButtonLight("##restore", ICON_MDI_UNDO, ImVec2(btnSz, btnSz)))
                        {
                            vault_db::restore_credential(c.uuid);
                            s_trash_needs_refresh = true;
                            ReloadVaultCredentials();
                            ShowToast("Credential restored", ToastType::Success);
                        }
                        if (ImGui::IsItemHovered()) SetTooltipPadded("Restore");

                        ImGui::SameLine();

                        ImGui::PushStyleColor(ImGuiCol_Text, colors::ErrorTextAlt);
                        if (StyledButtonLight("##permdelete", ICON_MDI_DELETE, ImVec2(btnSz, btnSz)))
                        {
                            vault_db::hard_delete_credential(c.uuid);
                            s_trash_needs_refresh = true;
                            ShowToast("Permanently deleted", ToastType::Success);
                        }
                        ImGui::PopStyleColor();
                        if (ImGui::IsItemHovered()) SetTooltipPadded("Delete permanently");

                        ImGui::SetCursorScreenPos(ImVec2(rowMin.x, rowMin.y + rowH));
                        ImGui::Dummy(ImVec2(rowW, 0));

                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
            }

            ImGui::Dummy(ImVec2(0, 4));

            if (!s_trash_items.empty())
            {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(220, 70, 70, 255));
                if (StyledButtonLight("##empty_trash", ICON_MDI_DELETE " Empty Trash", ImVec2(0, 30)))
                {
                    for (const auto& c : s_trash_items)
                        vault_db::hard_delete_credential(c.uuid);
                    s_trash_needs_refresh = true;
                    ShowToast("Trash emptied", ToastType::Success);
                }
                ImGui::PopStyleColor();
                ImGui::SameLine();
            }

            float closeW = 80;
            ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - closeW);
            if (StyledButtonLight("##trash_close", "Close", ImVec2(closeW, 30)))
                ImGui::CloseCurrentPopup();

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    void RenderSecurityCenterModal(ShellState& s)
    {
        if (s.sec_center_open)
        {
            ImGui::OpenPopup("Security Center###sec_center_modal");
            s.sec_center_open = false;
        }

        const bool dark = s.dark_theme;
        const ImU32 popupBg = dark
            ? theme::ModalBg.dark
            : theme::ModalBg.light;
        const ImU32 dimBg = colors::DimOverlay;

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(620, 0), ImVec2(620, FLT_MAX));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, popupBg);
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, dimBg);

        bool modal_open = true;
        if (ImGui::BeginPopupModal("Security Center###sec_center_modal", &modal_open,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar))
        {
            ImDrawList* dl = ImGui::GetWindowDrawList();
            float fullW = ImGui::GetContentRegionAvail().x;

            int expired_count = 0;
            {
                const auto& creds = GetActiveVaultCreds();
                for (const auto& c : creds)
                    if (c.expires_at_ms < 0 && !c.is_deleted()) expired_count++;
            }
            int total_issues = 0;
            int total_passwords = 0;
            {
                const auto& creds = GetActiveVaultCreds();
                for (const auto& c : creds)
                {
                    if (c.is_deleted()) continue;
                    if (c.type == CredType::Password) total_passwords++;
                    bool hasIssue = s.sec_reused_ids.count(c.id)
                        || s.sec_weak_ids.count(c.id)
                        || s.sec_exposed_ids.count(c.id)
                        || c.expires_at_ms < 0;
                    if (hasIssue) total_issues++;
                }
            }

            {
                const float circleR = 36.0f;
                const float circleThick = 5.0f;
                ImVec2 circleCenter(ImGui::GetCursorScreenPos().x + circleR + 4.0f,
                                    ImGui::GetCursorScreenPos().y + circleR + 2.0f);

                int healthy = total_passwords - total_issues;
                if (healthy < 0) healthy = 0;
                float score = total_passwords > 0 ? (float)healthy / (float)total_passwords : 1.0f;

                ImU32 ringBg = dark ? IM_COL32(50, 50, 55, 255) : IM_COL32(220, 220, 225, 255);
                dl->AddCircle(circleCenter, circleR, ringBg, 36, circleThick);

                ImU32 scoreCol;
                if (score >= 0.8f) scoreCol = colors::StrengthStrong;      // green
                else if (score >= 0.5f) scoreCol = colors::StrengthMedium; // amber
                else scoreCol = colors::StrengthWeak;                      // red

                float startAngle = -IM_PI * 0.5f;
                float endAngle = startAngle + score * IM_PI * 2.0f;
                dl->PathArcTo(circleCenter, circleR, startAngle, endAngle, 36);
                dl->PathStroke(scoreCol, 0, circleThick);

                char scoreBuf[8];
                snprintf(scoreBuf, sizeof(scoreBuf), "%d%%", (int)(score * 100));
                ImVec2 scoreSz = ImGui::CalcTextSize(scoreBuf);
                dl->AddText(ImVec2(circleCenter.x - scoreSz.x * 0.5f, circleCenter.y - scoreSz.y * 0.5f),
                    ImGui::GetColorU32(ImGuiCol_Text), scoreBuf);

                // Title + subtitle right of circle
                float textX = circleCenter.x + circleR + 16.0f;
                float textY = circleCenter.y - circleR + 8.0f;

                ImGui::PushFont(render::FontLarge);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(textX, textY), ImGui::GetColorU32(ImGuiCol_Text), "Security Center");
                ImGui::PopFont();

                char summaryBuf[64];
                if (total_issues == 0)
                    snprintf(summaryBuf, sizeof(summaryBuf), "All passwords are healthy");
                else
                    snprintf(summaryBuf, sizeof(summaryBuf), "%d of %d password%s need attention",
                        total_issues, total_passwords, total_passwords == 1 ? "" : "s");
                ImGui::PushFont(render::FontSmall);
                dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(),
                    ImVec2(textX, textY + 26.0f), ImGui::GetColorU32(ImGuiCol_TextDisabled), summaryBuf);
                ImGui::PopFont();

                ImVec2 closeSz = ImGui::CalcTextSize(ICON_MDI_CLOSE);
                float closeX = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x - closeSz.x;
                float closeY = ImGui::GetCursorScreenPos().y;
                dl->AddText(ImVec2(closeX, closeY), ImGui::GetColorU32(ImGuiCol_TextDisabled), ICON_MDI_CLOSE);
                ImGui::SetCursorScreenPos(ImVec2(closeX, closeY));
                if (ImGui::InvisibleButton("##sec_close", closeSz))
                    ImGui::CloseCurrentPopup();

                ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x,
                    circleCenter.y + circleR + 12.0f));
            }

            ImGui::Dummy(ImVec2(0, 4));

            {
                struct ChipInfo { const char* icon; const char* label; int count; ImU32 color; int category; };
                ChipInfo chips[] = {
                    { ICON_MDI_FORMAT_LIST_BULLETED, "All",     total_issues,        IM_COL32(0, 0, 0, 0),       0 },
                    { ICON_MDI_REPEAT,               "Reused",  s.sec_reused_count,  colors::StatusReused, 1 },
                    { ICON_MDI_ALERT,                "Weak",    s.sec_weak_count,    colors::StatusWeak,  2 },
                    { ICON_MDI_EARTH,                "Exposed", s.sec_exposed_count, colors::StatusExposed, 3 },
                    { ICON_MDI_TIMER_SAND_COMPLETE,  "Expired", expired_count,       colors::ErrorText,  4 },
                };

                float chipH = 28.0f;
                float chipRound = 6.0f;
                float chipPadX = 10.0f;
                float chipGap = 6.0f;

                ImVec2 chipCursor = ImGui::GetCursorScreenPos();
                for (int i = 0; i < 5; i++)
                {
                    bool active = (s.sec_center_category == chips[i].category);
                    char chipText[64];
                    snprintf(chipText, sizeof(chipText), "%s  %s  %d", chips[i].icon, chips[i].label, chips[i].count);
                    ImVec2 textSz = ImGui::CalcTextSize(chipText);
                    float chipW = textSz.x + chipPadX * 2;

                    ImVec2 cMin = chipCursor;
                    ImVec2 cMax = ImVec2(cMin.x + chipW, cMin.y + chipH);

                    ImGui::SetCursorScreenPos(cMin);
                    ImGui::PushID(i);
                    ImGui::InvisibleButton("##fchip", ImVec2(chipW, chipH));
                    bool hovered = ImGui::IsItemHovered();
                    if (ImGui::IsItemClicked()) s.sec_center_category = chips[i].category;
                    ImGui::PopID();

                    if (active)
                    {
                        ImU32 activeBg = (i == 0)
                            ? (dark ? IM_COL32(60, 60, 66, 255) : IM_COL32(220, 220, 225, 255))
                            : ((chips[i].color & 0x00FFFFFF) | (dark ? 0x40000000 : 0x30000000));
                        dl->AddRectFilled(cMin, cMax, activeBg, chipRound);
                    }
                    else if (hovered)
                    {
                        dl->AddRectFilled(cMin, cMax,
                            dark ? IM_COL32(255, 255, 255, 15) : IM_COL32(0, 0, 0, 10), chipRound);
                    }
                    else
                    {
                        dl->AddRect(cMin, cMax,
                            dark ? IM_COL32(255, 255, 255, 20) : IM_COL32(0, 0, 0, 15), chipRound);
                    }

                    ImU32 txtCol;
                    if (active)
                        txtCol = (i == 0) ? ImGui::GetColorU32(ImGuiCol_Text) : chips[i].color;
                    else if (chips[i].count > 0)
                        txtCol = ImGui::GetColorU32(ImGuiCol_Text);
                    else
                        txtCol = ImGui::GetColorU32(ImGuiCol_TextDisabled);

                    dl->AddText(ImVec2(cMin.x + chipPadX, cMin.y + (chipH - textSz.y) * 0.5f), txtCol, chipText);

                    chipCursor.x += chipW + chipGap;
                }

                ImGui::SetCursorScreenPos(ImVec2(ImGui::GetWindowPos().x + ImGui::GetStyle().WindowPadding.x,
                    chipCursor.y + chipH + 10.0f));
            }

            float listH = 312.0f;
            ImGui::PushStyleColor(ImGuiCol_ChildBg, dark ? IM_COL32(28, 28, 31, 255) : IM_COL32(245, 245, 248, 255));
            if (ImGui::BeginChild("##sec_list", ImVec2(fullW, listH), ImGuiChildFlags_Borders, ImGuiWindowFlags_None))
            {
                ImDrawList* dl = ImGui::GetWindowDrawList(); // use child's draw list for clipping
                const auto& creds = GetActiveVaultCreds();

                auto matches_category = [&](const Credential& c) -> bool {
                    if (c.is_deleted()) return false;
                    switch (s.sec_center_category) {
                    case 0:
                        return s.sec_reused_ids.count(c.id) || s.sec_weak_ids.count(c.id)
                            || s.sec_exposed_ids.count(c.id) || c.expires_at_ms < 0;
                    case 1: return s.sec_reused_ids.count(c.id) > 0;
                    case 2: return s.sec_weak_ids.count(c.id) > 0;
                    case 3: return s.sec_exposed_ids.count(c.id) > 0;
                    case 4: return c.expires_at_ms < 0;
                    default: return false;
                    }
                };

                int visible = 0;
                for (const auto& c : creds)
                {
                    if (!matches_category(c)) continue;
                    visible++;

                    ImGui::PushID(c.id);

                    ImVec2 rowMin = ImGui::GetCursorScreenPos();
                    float rowW = ImGui::GetContentRegionAvail().x;
                    float faviconSz = 22.0f;
                    float rowH2 = 42.0f;
                    float textH = ImGui::GetTextLineHeight();

                    bool rowHovered = ImGui::IsMouseHoveringRect(rowMin,
                        ImVec2(rowMin.x + rowW, rowMin.y + rowH2));


                    if (rowHovered)
                        dl->AddRectFilled(rowMin, ImVec2(rowMin.x + rowW, rowMin.y + rowH2),
                            dark ? IM_COL32(255, 255, 255, 15) : IM_COL32(0, 0, 0, 8), 4.0f);

                    float contentX = rowMin.x + 10.0f;
                    float cy = rowMin.y + (rowH2 - textH) * 0.5f;
                    float faviconY = rowMin.y + (rowH2 - faviconSz) * 0.5f;

                    auto srv = favicon::Get(c.website);
                    if (srv)
                    {
                        ImGui::SetCursorScreenPos(ImVec2(contentX, faviconY));
                        ImGui::Image((ImTextureID)srv, ImVec2(faviconSz, faviconSz));
                        contentX += faviconSz + 8.0f;
                    }
                    else
                    {
                        dl->AddText(ImVec2(contentX, cy), ImGui::GetColorU32(ImGuiCol_TextDisabled), CredTypeIcon(c.type));
                        contentX += ImGui::CalcTextSize(CredTypeIcon(c.type)).x + 8.0f;
                    }

                    std::string title = c.title.empty() ? "(Untitled)" : c.title;
                    dl->AddText(ImVec2(contentX, cy), ImGui::GetColorU32(ImGuiCol_Text), title.c_str());
                    contentX += ImGui::CalcTextSize(title.c_str()).x + 8.0f;

                    std::string sub = !c.email.empty() ? c.email : c.user;
                    if (!sub.empty())
                        dl->AddText(ImVec2(contentX, cy), ImGui::GetColorU32(ImGuiCol_TextDisabled), sub.c_str());

                    {
                        float badgeX = rowMin.x + rowW - 6.0f;
                        float badgeY = rowMin.y + (rowH2 - textH) * 0.5f;
                        float gap = 4.0f;

                        if (c.expires_at_ms < 0) {
                            ImVec2 sz = ImGui::CalcTextSize(ICON_MDI_TIMER_SAND_COMPLETE);
                            badgeX -= sz.x; dl->AddText(ImVec2(badgeX, badgeY), colors::ErrorText, ICON_MDI_TIMER_SAND_COMPLETE); badgeX -= gap;
                        }
                        if (s.sec_exposed_ids.count(c.id)) {
                            ImVec2 sz = ImGui::CalcTextSize(ICON_MDI_EARTH);
                            badgeX -= sz.x; dl->AddText(ImVec2(badgeX, badgeY), colors::StatusExposed, ICON_MDI_EARTH); badgeX -= gap;
                        }
                        if (s.sec_reused_ids.count(c.id)) {
                            ImVec2 sz = ImGui::CalcTextSize(ICON_MDI_REPEAT);
                            badgeX -= sz.x; dl->AddText(ImVec2(badgeX, badgeY), colors::StatusReused, ICON_MDI_REPEAT); badgeX -= gap;
                        }
                        if (s.sec_weak_ids.count(c.id)) {
                            ImVec2 sz = ImGui::CalcTextSize(ICON_MDI_ALERT);
                            badgeX -= sz.x; dl->AddText(ImVec2(badgeX, badgeY), colors::StatusWeak, ICON_MDI_ALERT); badgeX -= gap;
                        }
                    }

                    ImGui::SetCursorScreenPos(rowMin);
                    char rowBtnId[32]; snprintf(rowBtnId, sizeof(rowBtnId), "##secrow_%d", c.id);
                    if (ImGui::InvisibleButton(rowBtnId, ImVec2(rowW, rowH2)))
                    {
                        s.sec_center_edit_id = c.id;
                        ImGui::CloseCurrentPopup();
                    }

                    ImGui::PopID();
                }

                if (visible == 0)
                {
                    ImGui::Dummy(ImVec2(0, 80));
                    const char* emptyMsg = total_issues == 0 ? "All passwords are healthy!" : "No issues in this category";
                    float textW2 = ImGui::CalcTextSize(emptyMsg).x;
                    ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - textW2) * 0.5f + ImGui::GetCursorPosX());
                    ImGui::TextDisabled("%s", emptyMsg);
                }
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();

            if (s.sec_center_category == 0 || s.sec_center_category == 3)
            {
                ImGui::Dummy(ImVec2(0, 6));
                if (s.sec_breach_checking)
                {
                    char prog[64];
                    snprintf(prog, sizeof(prog), "Checking... %d/%d",
                        s.sec_breach_checked.load(), s.sec_breach_total.load());
                    ImGui::TextDisabled("%s", prog);
                }
                else
                {
                    if (StyledButtonLight("##breach_check", ICON_MDI_SHIELD_SEARCH " Check for breached passwords", ImVec2(0, 30)))
                        s.sec_breach_trigger = true;

                    if (!s.sec_breach_error.empty())
                    {
                        ImGui::SameLine();
                        ImGui::PushStyleColor(ImGuiCol_Text, colors::ErrorText);
                        ImGui::TextUnformatted(s.sec_breach_error.c_str());
                        ImGui::PopStyleColor();
                    }
                }
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar(3);
    }

    void RenderRecoveryKeyModal(ShellState& s)
    {
        if (s.recovery_key_modal_open)
        {
            ImGui::OpenPopup("Recovery Key###recovery_key_modal");
            s.recovery_key_modal_open = false;
        }

        ImVec2 center = ImGui::GetMainViewport()->GetCenter();
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(420, 0), ImVec2(420, FLT_MAX));

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16, 14));
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8, 6));
        ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, colors::DimOverlay);

        // nullptr = no close button; user must acknowledge and click Continue
        if (ImGui::BeginPopupModal("Recovery Key###recovery_key_modal", nullptr,
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar))
        {
            ImGui::PushFont(render::FontLarge);
            ImGui::TextUnformatted("Recovery Key");
            ImGui::PopFont();
            ImGui::Dummy(ImVec2(0, 4));

            ImGui::TextUnformatted("Save your recovery key");
            ImGui::Dummy(ImVec2(0, 2));

            ImGui::TextWrapped(
                "This is the ONLY time your recovery key will be shown. "
                "Write it down or store it somewhere safe. "
                "If you forget your master password, this key is the only way to recover your vault.");

            ImGui::Dummy(ImVec2(0, 6));

            {
                ImVec2 avail = ImGui::GetContentRegionAvail();
                float pad = 8.0f;

                ImGui::PushStyleColor(ImGuiCol_ChildBg, ImGui::GetStyleColorVec4(ImGuiCol_FrameBg));
                ImGui::BeginChild("##rec_key_box", ImVec2(avail.x, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY);

                ImGui::SetCursorPos(ImVec2(pad, pad));

                if (render::FontSmall)
                    ImGui::PushFont(render::FontSmall);

                ImGui::PushTextWrapPos(avail.x - pad * 2.0f);
                ImGui::TextUnformatted(s.recovery_key_display.c_str());
                ImGui::PopTextWrapPos();

                if (render::FontSmall)
                    ImGui::PopFont();

                ImGui::Dummy(ImVec2(0, pad));
                ImGui::EndChild();
                ImGui::PopStyleColor();
            }

            ImGui::Dummy(ImVec2(0, 4));

            {
                float btnW = 70;
                ImGui::SetCursorPosX(ImGui::GetContentRegionMax().x - btnW);
                if (StyledButton("##copy_recovery", "Copy", ImVec2(btnW, 28)))
                {
                    ImGui::SetClipboardText(s.recovery_key_display.c_str());
                    ShowToast("Recovery key copied", ToastType::Info);
                }
            }

            ImGui::Dummy(ImVec2(0, 4));

            static bool s_ack = false;
            ui::CheckboxBg("I have saved my recovery key", &s_ack);

            ImGui::Dummy(ImVec2(0, 4));

            {
                float btnW = 120;
                float cx = (ImGui::GetContentRegionAvail().x - btnW) * 0.5f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + cx);

                bool disabled = !s_ack;
                if (disabled) ImGui::BeginDisabled();
                if (StyledButton("##recovery_continue", "Continue", ImVec2(btnW, 32)))
                {
                    memset(s.recovery_key_display.data(), 0, s.recovery_key_display.size());
                    s.recovery_key_display.clear();
                    s_ack = false;
                    ImGui::CloseCurrentPopup();
                }
                if (disabled) ImGui::EndDisabled();
            }

            ImGui::EndPopup();
        }

        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor(); // ModalWindowDimBg
    }


} // namespace ui
