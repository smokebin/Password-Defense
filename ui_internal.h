// ui_internal.h
// Shared internal state and helpers for UI translation units.
// Included by UI.cpp (core), ui_widgets.cpp, ui_views.cpp, ui_controls.cpp, ui_modals.cpp.
#pragma once

#include "UI.h"
#include "favicon.h"
#include "tools/totp.h"
#include "credentials/twofa_ops.h"
#include <Windows.h>

// Defined in application.cpp
extern std::vector<uint8_t> Get2FAMasterKey();
extern void ReloadVaultCredentials();

#include "credentials/credential_ops.h"
#include "credentials/vault_db.h"
#include "credentials/crypto/vault_crypto.h"

extern const std::vector<Credential>& GetActiveVaultCreds();

#include <functional>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <set>
#include "tools/save.h"
#include "GUI.h"
#include "theme_colors.h"
#include "tools/qrcodegen.hpp"
#include <d3d11.h>

// ============================================================
// Time constants (milliseconds)
// ============================================================
namespace time_ms {
    constexpr int64_t MINUTE  =       60'000LL;
    constexpr int64_t HOUR    =    3'600'000LL;
    constexpr int64_t DAY     =   86'400'000LL;
    constexpr int64_t WEEK    =  604'800'000LL;
}

namespace ui
{
    // ============================================================
    // Shared state (defined in UI.cpp, extern everywhere else)
    // ============================================================
    extern ImGuiTextFilter   g_filter;
    extern std::unordered_set<uint64_t> g_selected;
    extern ShellState* g_shell_ptr;

    // ============================================================
    // AnimatedTabBar (struct + instance, defined in UI.cpp)
    // ============================================================
    struct AnimatedTab
    {
        std::string label;
        ImGuiID     id = 0;
        bool  isNew = false;
        bool  isClosing = false;
        float currentWidth = 0.0f;
        float targetWidth = 0.0f;
        float offsetX = 0.0f;
        float targetOffsetX = 0.0f;
        float alpha = 0.0f;
        float targetAlpha = 0.0f;
        float scale = 1.0f;
        float targetScale = 1.0f;
        ImRect lastRect;
        bool   lastRectValid = false;
    };

    struct AnimatedTabBar
    {
        std::vector<AnimatedTab> tabs;
        int   activeIndex = 0;
        float barCurrentWidth = 0.0f;
        float barTargetWidth = 0.0f;
        float indicatorCurrentX = 0.0f;
        float indicatorTargetX = 0.0f;
        float indicatorCurrentWidth = 0.0f;
        float indicatorTargetWidth = 0.0f;
    };

    extern AnimatedTabBar g_dbTabsAnim;

    // ============================================================
    // Theme-aware helpers (defined in UI.cpp)
    // ============================================================
    bool   IsDarkTheme();
    ImU32  GetShadowColor(int ring, bool hovered = false);
    ImU32  GetCardBodyBg();
    ImU32  GetBadgeTextColor();
    ImU32  GetFavoriteColor();
    ImVec4 GetCardHeaderBg();
    ImVec4 GetTileBg();
    ImVec4 GetTileHoverBg();
    ImVec4 GetCardBg();
    ImVec4 GetItemBg();
    ImVec4 GetItemHoverBg();
    ImVec4 GetBorderGray();
    ImU32  GetControlsContainerBg();
    ImU32  GetSeparatorColor();

    // ============================================================
    // Drawing helpers (defined in UI.cpp)
    // ============================================================
    void SetTooltipPadded(const char* fmt, ...) IM_FMTARGS(1);
    std::string EnsureUrlProtocol(const std::string& url);
    void AddDashedLine(ImDrawList* dl, ImVec2 a, ImVec2 b, ImU32 col,
                       float thickness = 1.0f, float dash_len = 6.0f, float gap_len = 4.0f);
    void AddDashedRect(ImDrawList* dl, ImVec2 min, ImVec2 max, ImU32 col,
                       float thickness = 1.0f, float dash_len = 6.0f, float gap_len = 4.0f);

    // ============================================================
    // Selection helpers (defined in UI.cpp)
    // ============================================================
    bool IsSelected(uint64_t rowKey);
    void ToggleSelected(uint64_t rowKey);

    // ============================================================
    // Animation helpers (defined in UI.cpp)
    // ============================================================
    float AnimLerp(float current, float target, float speed, float dt);
    float UI_Lerp(float a, float b, float t);
    ImVec4 UI_LerpVec4(const ImVec4& a, const ImVec4& b, float t);

    // ============================================================
    // Strength meter (defined in ui_widgets.cpp)
    // ============================================================
    void DrawStrengthMeterCompact(const std::string& pw, float full_w);

    // ============================================================
    // Animated tab bar functions (defined in ui_widgets.cpp)
    // ============================================================
    void EnsureAnimatedTabsFromLabels(AnimatedTabBar& bar, const std::vector<std::string>& labels, int activeIndex);
    int  DrawAnimatedTabBarInternal(AnimatedTabBar& bar, bool dirtyActive, bool allowClose);

    // ============================================================
    // Widget helpers used across TUs (defined in ui_widgets.cpp)
    // ============================================================
    bool HoldToActionButton(const char* id, const char* label, float hold_seconds,
                            ImVec2 size = ImVec2(0, 0), float rounding = 999.0f);

    // AnimatedComboDotMulti - used in controls (defined in ui_widgets.cpp)
    bool AnimatedComboDotMulti(const char* id, const char* preview_value,
                               const std::vector<std::string>& items,
                               std::set<std::string>& selected);

    // PillTabs - used in controls (defined in ui_widgets.cpp)
    struct PillTabsAnim { bool unused = true; };
    bool PillTabs(const char* id, const char* const* labels, int labelCount,
                  int& activeIndex, PillTabsAnim& anim, float spacing,
                  int* right_clicked_tab = nullptr);

    // TextEllipsisClipped - used in views (defined in ui_widgets.cpp)
    void TextEllipsisClipped(const char* text, float max_w);
    float AnimExpF(float cur, float target, float speed, float dt);
    float AnimExp(float cur, float target, float speed, float dt);

    // GetScopePreviewLabel - used in controls (defined in ui_widgets.cpp)
    const char* GetScopePreviewLabel(const std::set<std::string>& selected);

    // CheckboxBg - Checkbox with visible inactive background (defined in ui_widgets.cpp)
    bool CheckboxBg(const char* label, bool* v);

    // ToggleSwitch - iOS-style toggle (defined in ui_widgets.cpp)
    bool ToggleSwitch(const char* id, bool* value);

    // DotSlider - Discrete dot-snap slider with arrow buttons (defined in ui_widgets.cpp)
    bool DotSlider(const char* id, int* value, int count);

    // VerticalStepper - Compact vertical stepper with inline edit (defined in ui_widgets.cpp)
    bool VerticalStepper(const char* id, int* value, int min_val, int max_val, const char* suffix = "");

    // IconButtonGhost - used in views/controls (defined in ui_widgets.cpp)
    bool IconButtonGhost(const char* id, const char* glyph, float size, bool enabled);

    // BarMenuItem - popup menu item with left accent bar (defined in ui_widgets.cpp)
    bool BarMenuItem(const char* label, bool selected, bool enabled = true, bool dim_unselected = false);
    bool BarMenuItemToggle(const char* label, bool* p_selected, bool enabled = true, bool dim_unselected = true);

    // PopupStyleBegin/End - shadow, backdrop dim, fade-in (defined in ui_widgets.cpp)
    void PopupStyleBegin();
    void PopupStyleEnd();

    // LiftedChild — double 1px border container (defined in ui_widgets.cpp)
    struct LiftedChildColorSet {
        ImU32 bg = IM_COL32(0, 0, 0, 0);
        ImU32 borderOuter = IM_COL32(0, 0, 0, 0);
        ImU32 borderInner = IM_COL32(0, 0, 0, 0);
    };
    void SetLiftedChildColors(const LiftedChildColorSet& colors);
    bool BeginLiftedChild(const char* id, ImVec2 size = ImVec2(0, 0), ImGuiWindowFlags flags = 0);
    void EndLiftedChild();

    // LippedChild — rounded child with thick lip beneath (defined in ui_widgets.cpp)
    struct LippedChildColorSet {
        ImU32 bg = IM_COL32(0, 0, 0, 0);
        ImU32 lip_color = IM_COL32(0, 0, 0, 0);
    };
    void SetLippedChildColors(const LippedChildColorSet& colors);
    bool BeginLippedChild(const char* id, ImVec2 size = ImVec2(0, 0), ImGuiWindowFlags flags = 0);
    void EndLippedChild();

    // Scroll fade — draw after EndChild(), pass scrollY captured before EndChild()
    void DrawScrollTopFade(ImVec2 childMin, float childWidth, float scrollY, float yOffset = 1.0f);

    // Change-highlight tint colors (defined in UI.cpp)
    extern const ImU32 kChangedRowTint;
    extern const ImU32 kNewRowTint;

    // QR texture helpers (defined in UI.cpp)
    void CreateQRTexture(const std::string& text);
    void ReleaseQRTexture();
    extern ID3D11ShaderResourceView* s_2fa_qr_srv;
    extern int s_2fa_qr_img_size;

    // Field accent state (defined in ui_views.cpp)
    extern bool g_field_accent_active;

    // View helper functions used across TUs (defined in ui_views.cpp)
    void DrawBadgeChip(ImDrawList* dl, const ImVec2& bMin, const ImVec2& bMax, ImU32 bg, const char* text, ImU32 textCol);
    float DrawSecurityBadges(ImDrawList* dl, const ShellState* shell, int credId, float rightX, float centerY, float chipGap);
    bool IconSquareBtn(const char* id, const char* icon, const char* tip, float sz, bool active = false, bool danger = false, ImU32 activeIconCol = 0);
    const char* CredTypeIcon(CredType type);

    // Date/format helpers (defined in ui_views.cpp)
    std::string FormatIsoDateLabel(const std::string& iso);
    std::string FormatUnixMsDateLabel(int64_t ms);
    std::string FormatUnixMsDateOnly(int64_t ms);
    std::string CleanUrl(const std::string& url);
    std::string GetWebmailUrl(const std::string& email);
    const char* MaskIfPrivate(const std::string& text);

    // Filter/pill helpers (defined in ui_views.cpp)
    bool GroupMatches(const AccordionItem& c, const std::string& group);
    bool PillTabMatches(const AccordionItem& c, const std::string& tabLabel);
    void GetPillTabIcons(const std::string& label, const char*& icon, const char*& iconActive);

    // Row drawing helpers (defined in ui_views.cpp)
    void DrawFieldHighlight(ImDrawList* dl, float rowX, float rowY, float rowW, float rowH);
    void TriggerCopyFlash();
    void DrawRowIcon(const char* icon);
    void DrawInfoRow(const char* id, const char* icon, const std::string& value, float iconColW, float valueColW, float rowSpacing);
    bool IconFloatingBtn(const char* id, const char* icon, const char* tip, float sz);
    void DrawWebsiteRow(const char* id, const std::string& website, float iconColW, float valueColW, float rowWidth, float btnSz, float rowSpacing);
    void DrawCopyRow(const char* id, const char* icon, const std::string& value, float iconColW, float valueColW, float rowWidth, float btnSz, float rowSpacing, const char* display_override = nullptr);
    void DrawEmailRow(const char* id, const std::string& value, float iconColW, float valueColW, float rowWidth, float btnSz, float rowSpacing, const char* display_override = nullptr);
    void DrawMaskedCopyRow(const char* id, const char* icon, const std::string& value, int reveal_last, float iconColW, float valueColW, float rowWidth, float btnSz, float rowSpacing);
    void DrawHintLine(const char* icon, const char* text, ImU32 iconCol);
    void DrawPopupTabBar(int credId, bool hasSecurityTab, int& currentTab);
    void DrawTOTPRow(const char* id, const std::string& totp_secret, float iconColW, float valueColW, float rowWidth, float btnSz, float rowSpacing);
    void DrawPasswordRow(const char* id, int credId, GetPasswordFn get_password_fn, const std::vector<PwHistoryEntry>& password_history, std::set<int>& visiblePasswords, float iconColW, float valueColW, float rowWidth, float btnSz, float rowSpacing);
    bool DrawCopyFieldRow(const char* id, const char* label, const std::string& value, const std::string& display_value, uint64_t rowKey, int fieldIndex, float rowWidth, bool& interaction_consumed);

    // Security card (defined in ui_views_threepane.cpp)
    void DrawSecurityCard(const AccordionItem& c, ShellState& s, uint32_t activeVaultKey, GetPasswordFn get_password_fn, float contentW);

    // View renderers (defined in ui_views_threepane.cpp / ui_views_table.cpp)
    AccordionListResult RenderThreePaneView(const std::vector<AccordionItem>& items, uint32_t activeVaultKey, GetPasswordFn get_password_fn, bool read_only);
    AccordionListResult RenderTableView(const std::vector<AccordionItem>& items, uint32_t activeVaultKey, GetPasswordFn get_password_fn, bool read_only);

    // ============================================================
    // Settings widgets (defined in ui_widgets.cpp)
    // ============================================================
    extern int g_category_card_id;

    enum class SettingRightType
    {
        None, Toggle, Button, Chevron, Ellipsis, Text, Slider, Combo, AnimatedCombo,
    };

    struct SettingRowSpec
    {
        ImGuiID id;
        const char* title;
        const char* subtitle;
        const char* leftIcon;
        SettingRightType rightType = SettingRightType::None;
        bool* toggleValue = nullptr;
        const char* buttonLabel = nullptr;
        const char* rightText = nullptr;
        int* sliderValue = nullptr;
        int sliderMin = 0;
        int sliderMax = 100;
        const char* sliderFormat = "%d";
        float sliderWidth = 150.0f;
        float* sliderValueF = nullptr;
        float sliderMinF = 0.0f;
        float sliderMaxF = 1.0f;
        const char* sliderFormatF = "%.1f";
        const char** comboItems = nullptr;
        int comboCount = 0;
        int* comboIndex = nullptr;
        float comboWidth = 120.0f;
        float comboHeight = 30.0f;
        bool enabled = true;
    };

    struct SettingRowResult
    {
        bool row_selected = false;
        bool action_used = false;
    };

    extern const char* g_settings_scroll_target;

    SettingRowResult RenderSettingRow(const SettingRowSpec& r, ImGuiID* selected_id, float row_h = 56.0f);
    void BeginCategoryCard(const char* title, const char* subtitle = nullptr);
    void EndCategoryCard();
    void DrawRowDivider();
    void DrawRowDivider(float rightMargin);

    // RenderSettingsPage - called from EndShell (defined in ui_controls.cpp)
    void RenderSettingsPage(ui::ShellState& s);

    // ============================================================
    // View row-state maps (defined in ui_views.cpp)
    // ============================================================
    struct EditBuffers
    {
        bool active = false;
        bool show_password = false;
        helpers::GenOptions gen_opt;

        std::string title, email, user, password, website, group, notes;
        std::string o_title, o_email, o_user, o_password, o_website, o_group, o_notes;

        std::string card_number, card_expiry, card_cvv, card_brand, cardholder_name;
        std::string card_address, card_city, card_postal_code;
        std::string full_name, id_type_buf, id_number, date_of_birth, expiry_date_buf, country, address, phone;
        std::string o_card_number, o_card_expiry, o_card_cvv, o_card_brand, o_cardholder_name;
        std::string o_card_address, o_card_city, o_card_postal_code;
        std::string o_full_name, o_id_type_buf, o_id_number, o_date_of_birth, o_expiry_date_buf, o_country, o_address, o_phone;

        void BeginFrom(const AccordionItem& c, const std::string& password_value);
        void Revert();
    };

    struct TileCardState
    {
        bool flipped = false;
        float flip_t = 0.0f;
    };

    extern std::unordered_map<uint64_t, EditBuffers> g_edit;
    extern std::unordered_set<uint64_t> g_open;
    extern std::unordered_map<uint64_t, int> g_scroll_to_open;
    extern std::unordered_map<uint64_t, TileCardState> g_tile_state;
    extern std::unordered_map<uint64_t, float> g_copy_flash;
    extern std::unordered_set<uint64_t> g_hover_opened;
    extern std::unordered_set<uint64_t> g_hover_flipped;
    extern bool  g_3p_sidebar_hover_expanded;
    extern bool  g_3p_list_hover_expanded;
    extern float g_3p_sidebar_leave_timer;
    extern float g_3p_list_leave_timer;
    constexpr float k3pCollapseDelay = 0.15f;
    extern float g_3p_sidebar_anim_w;
    extern float g_3p_list_anim_w;
    constexpr float k3pAnimSpeed = 500.0f;
    extern std::unordered_set<uint64_t> g_notes_visible;
    extern uint64_t g_notes_reprompt_pending;
    extern std::unordered_map<uint64_t, float> g_card_body_h;
    extern std::unordered_map<uint64_t, float> g_card_anim_h;
    extern std::unordered_set<uint64_t> g_card_closing;
    extern std::unordered_set<std::string> g_collapsed_headers;
    constexpr float kCardAnimSpeed = 1500.0f;

    // Row key helpers (defined in ui_views.cpp)
    uint64_t MakeRowKey(uint32_t vaultKey, int id);
    uint64_t MakeCopyKey(uint64_t rowKey, int field);

    // ============================================================
    // Controls shared state (defined in ui_controls.cpp)
    // ============================================================
    extern bool g_shell_open;
    extern UnderlineTabsAnim g_sortAnim;
    extern UnderlineTabsAnim g_orderAnim;
    extern SearchPopupAnim   g_searchAnim;

} // namespace ui
