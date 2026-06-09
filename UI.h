// UI.h — shell state, accordion types, widget declarations
#pragma once

#include "third_party/imgui/imgui.h"
#include "third_party/imgui/imgui_internal.h"

#include <string>
#include <vector>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <atomic>
#include <cmath>
#include <algorithm>
#include "IconsMaterialDesignIcons.h"
#include "utility.h"
#include "save.h"
#include "cred_type.h"
namespace colors {
    inline const ImVec4 SecondColor = { 1.0f, 0.384f, 0.016f, 0.80f };
    inline const ImVec4 MainColor = { SecondColor.x, SecondColor.y, SecondColor.z, SecondColor.w * 3.0f };

    inline const ImVec4 White = { 1, 1, 1, 1 };
    inline const ImVec4 Gray = { 0.235f, 0.235f, 0.235f, 1 };
    inline const ImVec4 Trans = { 0, 0, 0, 0 };
    inline const ImVec4 Green = { 0.2f, 0.72f, 0.3f, 1.0f };
    inline const ImVec4 Red = { 0.6824f, 0.1608f, 0.1608f, 1.0f };
    inline const ImVec4 Orange = { 0.739f, 0.288f, 0.129f, 1.000f };
}

namespace ui
{
    const char* GetSearchText(); // returns "" if none
    int GetSearchFilter();       // 0=title, 1=email, 2=username
    bool GetShowGroupCount();

    void ForgetVaultRowState(uint32_t activeVaultKey);

    void ForgetRowState(uint32_t activeVaultKey, int id);
    void ForgetRowStateRowKey(uint64_t rowKey);
    void ResetAllRowState(); // use on DB switch

    void ClearSelectionForVault(uint32_t activeVaultKey);
    void SelectRow(uint32_t activeVaultKey, int id);
    void GetSelectedRowKeys(uint32_t activeVaultKey, std::vector<uint64_t>& outKeys);
    int  CountSelection(uint32_t activeVaultKey);
    bool HasAnySelection(uint32_t activeVaultKey);

    enum class OrderKey : int { Title = 0, Created = 1, Updated = 2 };
    enum class OrderDir : int { Asc = 0, Desc = 1 };
    enum class GroupMode : int { None = 0, Group = 1, MonthCreated = 2, MonthUpdated = 3, PinnedFavorites = 4, Alphabetical = 5 };
    enum class ViewMode : int { Detailed = 1, Tiles = 2, ThreePane = 3, Table = 4 };
    enum class Screen : int { Locked = 0, Unlocked = 1, Settings = 2, AddCredential = 3, Login = 4, EditCredential = 5 };

    struct ShellState
    {
        std::vector<std::string> db_labels;
        int active_db = 0;

        bool search_open = false;
        bool options_open = false;

        int filter_mode;
        int search_filter = 0; // 0=title, 1=email, 2=username
        int sort_mode = 2; // 0=tab0, 1=tab1, 2=all
        std::string pill_tab_0 = "Pinned";    // configurable filter tab 0
        std::string pill_tab_1 = "Favorites"; // configurable filter tab 1
        std::vector<std::string> groups = { "All" };
        std::set<std::string> selected_groups;  // empty = show all
        std::vector<std::string> all_tags;       // sorted unique tag list
        std::set<std::string> selected_tags;     // empty = show all; OR logic

        bool add_clicked = false;
        bool undo_clicked = false;
        bool back_clicked = false;

        bool open_db_clicked = false;
        bool new_db_clicked = false;

        bool save_clicked = false;     // set true when Save pressed
        bool dirty = false;            // set by app when vault has unsaved changes
        bool can_save = true;
        bool read_only = false;
        bool can_undo = false;         // true when undo stack is non-empty

        std::string footer_status_text;
        bool        footer_status_is_error = false;

        std::string last_backup_label;   // e.g. "Last backup: 2025-12-27 14:12 • 1.2 MB"
        std::string last_save_label;     // e.g. "Last save: 2025-12-27 14:17"


        bool footer_open_db_clicked = false;
        bool footer_new_db_clicked = false;
        bool footer_save_clicked = false;
        bool footer_options_clicked = false;
        bool footer_close_clicked = false;

        bool footer_close_confirming = false; // stateful, persists until resolved
        bool footer_close_anyway = false; // one-frame intent
        bool footer_close_cancel = false; // one-frame intent

        bool delete_vault_clicked = false;
        bool delete_vault_confirming = false;   // modal open
        bool delete_vault_confirmed = false;

        std::vector<std::string> footer_backups;      // display labels
        std::vector<std::string> footer_backup_paths; // same size as footer_backups
        int  footer_backup_index = 0;

        bool footer_restore_clicked = false;
        bool footer_refresh_backups_clicked = false;
        bool footer_browse_backup_clicked = false;
        std::string footer_browse_backup_path;  // file picked via Browse

        bool footer_options_popup_open = false;
        bool footer_create_backup_clicked = false;
        bool footer_toggle_read_only_clicked = false;

        bool footer_set_read_only = false;
        bool footer_set_read_only_value = false;


        int  backup_keep_count = 25;
        bool auto_backup = true;
        bool backup_select_newest_on_refresh = true;

        struct BackupMeta
        {
            uint64_t sizeBytes = 0;
            std::string localTime; // "YYYY-MM-DD HH:MM"
        };

        std::vector<BackupMeta> footer_backup_meta; // same length as footer_backup_paths

        bool bulk_delete_clicked = false;
        bool clear_selection_clicked = false;
        bool select_all_clicked = false;



        OrderKey  order_key = OrderKey::Title;
        OrderDir  order_dir = OrderDir::Asc;
        GroupMode group_mode = GroupMode::None;
        ViewMode  view_mode = ViewMode::ThreePane;
        int       three_pane_selected_id = -1;  // credential shown in detail pane
        bool      three_pane_sidebar_collapsed = false;
        bool      three_pane_list_collapsed = false;
        bool      three_pane_sidebar_auto_collapse = false;
        bool      three_pane_list_auto_collapse = false;
        bool      sidebar_types_collapsed = false;
        bool      sidebar_groups_collapsed = false;
        bool      sidebar_tags_collapsed = false;


        bool bulk_pin_clicked = false;
        bool bulk_unpin_clicked = false;
        bool bulk_fav_clicked = false;
        bool bulk_unfav_clicked = false;
        int  bulk_group_index = 0;
        bool bulk_set_group_clicked = false;
        std::string bulk_tag_to_add;     // one-frame: tag to add to selected
        std::string bulk_tag_to_remove;  // one-frame: tag to remove from selected

        int  drag_drop_cred_id = -1;
        std::string drag_drop_target_group;

        int  settings_tab_index = 0;
        bool settings_clicked = false;
        bool autosave_enabled = true;
        bool autoscroll_enabled = true;
        bool hover_expand = false;
        bool goto_locked_clicked = false;
        bool dark_theme = true;
        bool theme_changed = false;
        bool always_on_top = false;
        bool always_on_top_changed = false;
        bool minimize_to_tray = false;
        bool start_on_boot = false;
        bool start_minimized = false;       // requires minimize_to_tray
        bool auto_open_vault = true;

        // both off by default — offline first
        bool online_favicons = false;
        bool online_breach_check = false; // HIBP k-anonymity; no password leaves the machine

        Screen active_screen = Screen::Locked;
        std::vector<Screen> screen_stack;

        bool offline_mode = false;

        bool export_csv_clicked = false;
        bool export_pwm_clicked = false;
        bool export_kdbx_clicked = false;
        bool import_pwm_clicked = false;
        bool import_csv_clicked = false;

        cfg::DetailedHeaderColumns detailed_header_cols;

        bool show_group_count = true;
        int row_gap = 6;

        ImVec4 card_bg_dark  = ImVec4(24/255.0f, 24/255.0f, 24/255.0f, 1.0f);
        ImVec4 card_bg_light = ImVec4(236/255.0f, 236/255.0f, 240/255.0f, 1.0f);

        float font_scale = 1.0f;
        bool privacy_mode = false;
        bool blur_on_unfocus = false;

        // uuid → last-access timestamp, newest-first, capped at 25
        struct RecentEntry { std::string uuid; int64_t accessed_at_ms = 0; };
        std::vector<RecentEntry> recent_items;
        std::string recent_touch_uuid;  // one-frame: set by UI when credential is accessed

        bool style_editor_open = false;


        ImVec4 card_header_bg_dark   = ImVec4(0.086f, 0.086f, 0.086f, 0.95f);
        ImVec4 card_header_bg_light  = ImVec4(0.98f, 0.98f, 0.99f, 1.0f);
        ImVec4 card_body_bg_dark     = ImVec4(27/255.0f, 27/255.0f, 30/255.0f, 1.0f);
        ImVec4 card_body_bg_light    = ImVec4(246/255.0f, 246/255.0f, 246/255.0f, 1.0f);
        ImVec4 soft_container_dark   = ImVec4(36/255.0f, 35/255.0f, 39/255.0f, 1.0f);
        ImVec4 soft_container_light  = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
        ImVec4 controls_bg_dark      = ImVec4(25/255.0f, 24/255.0f, 28/255.0f, 1.0f);
        ImVec4 controls_bg_light     = ImVec4(235/255.0f, 235/255.0f, 240/255.0f, 1.0f);
        ImVec4 window_bg_dark        = ImVec4(28/255.0f, 27/255.0f, 30/255.0f, 1.0f);
        ImVec4 window_bg_light       = ImVec4(245/255.0f, 245/255.0f, 247/255.0f, 1.0f);

        int self_destruct_mode = 0; // 0=Off, 1=Exe Only, 2=Exe+Config, 3=Everything

        // populated by app layer each frame
        int sec_reused_count = 0;
        int sec_weak_count   = 0;
        int sec_exposed_count = 0;

        bool sec_highlight_reused  = false;
        bool sec_highlight_weak    = false;
        bool sec_highlight_exposed = false;

        std::unordered_set<int> sec_reused_ids;
        std::unordered_set<int> sec_weak_ids;
        std::unordered_set<int> sec_exposed_ids;

        int sec_aging_count = 0;
        bool sec_highlight_aging = false;
        std::unordered_set<int> sec_aging_ids;
        int password_max_age_days = 90;  // synced with cfg on startup + slider change

        // sidebar badge counts (unfiltered)
        int sb_count_all = 0;
        int sb_count_pinned = 0;
        int sb_count_favorites = 0;
        int sb_count_passwords = 0;
        int sb_count_cards = 0;
        int sb_count_identity = 0;
        int sb_count_notes = 0;
        std::unordered_map<std::string, int> sb_group_counts;

        struct SidebarCredItem {
            int id = 0;
            std::string title;
            std::string website;
            CredType type = CredType::Password;
            std::string uuid;
        };
        std::unordered_map<std::string, std::vector<SidebarCredItem>> sb_group_items;
        std::unordered_set<std::string> sidebar_expanded_groups;
        std::unordered_map<std::string, int> sb_tag_counts;  // per-tag credential counts

        std::atomic<bool> sec_breach_checking{false};
        std::atomic<int>  sec_breach_checked{0};
        std::atomic<int>  sec_breach_total{0};
        std::string sec_breach_error;
        bool sec_breach_trigger = false;

        // staging written by background thread; main thread consumes on sec_breach_done
        std::unordered_set<int> sec_exposed_ids_staging;
        int                     sec_exposed_count_staging = 0;
        std::string             sec_breach_error_staging;
        std::atomic<bool>       sec_breach_done{false};

        bool twofa_setup_open = false;
        bool trash_modal_open = false;

        bool sec_center_open = false;
        int  sec_center_category = 0;   // 0=All, 1=Reused, 2=Weak, 3=Exposed, 4=Expired
        int  sec_center_edit_id = -1;

        bool settings_modal_open = false;

        bool recovery_key_modal_open = false;
        std::string recovery_key_display; // hex with dashes; zeroed and cleared after modal closes

    };

    void RenderAppStyleEditor(ShellState& s);
    bool Initialize();
    void ApplyTheme();

    bool IconButtonSquare(
        const char* id,
        const char* glyph,
        float size,
        bool enabled,
        ImU32 accent = 0);

    bool IconButtonDoubleLip(const char* id, const char* glyph, float size);

    void DrawListControlsRow(ShellState& s, uint32_t activeVaultKey);

    void BeginShell(ShellState& s, const char* window_title = "Password Manager");
    void EndShell();

    void BeginShellHeader();
    void EndShellHeader();
    void BeginShellScroll();
    void EndShellScroll();

    void RenderShell(ShellState& s, const char* window_title = "Password Manager");

    bool InputTextString(const char* label, std::string* str, ImGuiInputTextFlags flags = 0);
    bool InputTextFormatted(const char* label, std::string* str, const char* pattern, ImGuiInputTextFlags flags = 0);
    std::string StripNonDigits(const std::string& s);
    std::string FormatWithPattern(const std::string& digits, const char* pattern);
    bool InputTextMultilineString(const char* label, std::string* str, const ImVec2& size, ImGuiInputTextFlags flags = 0);
    bool SearchableCombo(const char* label, std::string& value,
                         const std::vector<std::string>& items,
                         const std::unordered_map<std::string, int>* counts = nullptr,
                         const char* hint = "Select...");
    bool InputText3(const char* label,
        char* buf,
        size_t buf_size,
        ImGuiInputTextFlags flags,
        ImGuiInputTextCallback callback,
        void* user_data);
    bool InputText4(const char* label,
        char* buf,
        size_t buf_size,
        ImGuiInputTextFlags flags,
        ImGuiInputTextCallback callback,
        void* user_data);
    // Password input that briefly reveals the last typed character (mobile-style)
    bool InputTextPasswordReveal(const char* label, std::string* str, ImGuiInputTextFlags extra_flags = 0, float reveal_duration_ms = 400.0f);

    bool StyledButton(const char* id, const char* label, ImVec2 size = ImVec2(0, 0), float rounding = -1.0f);
    bool StyledButtonLight(const char* id, const char* label, ImVec2 size = ImVec2(0, 0), float rounding = -1.0f);

    bool AnimatedComboDot(const char* id, const char* preview_value, const char* const* items, int items_count, int* current_index, float width, float height, bool light = false);

    // Search popup animation
    struct SearchPopupAnim
    {
        bool  init = false;
        float w = 0.0f;
    };

    bool SearchIconPopup(const char* id,
        ImGuiTextFilter& filter,
        SearchPopupAnim& anim,
        float targetWidth = 160.0f,
        float height = 30.0f);

    enum class UnderlineTabMode : int { Pinned = 0, Favorites = 1, All = 2 };

    struct UnderlineTabsAnim
    {
        float x = 0.f;
        float w = 0.f;
        bool  init = false;
    };

    bool UnderlineTabs(const char* id,
        const char* const* labels, int labelCount,
        int& activeIndex,
        UnderlineTabsAnim& anim,
        float spacing = 10.0f);

    // Underline combo
    bool UnderlineCombo(const char* id,
        const std::vector<std::string>& items,
        int& current_index,
        float width = 0.0f,
        float height = 28.0f);

    bool PasswordFieldRow(
        const char* id,
        std::string& pw,
        bool& show_password,
        helpers::GenOptions& gen_opt,
        float width);

    struct PwHistoryEntry {
        std::string password;
        int64_t     changed_at_ms = 0;
    };

    enum FieldChangedFlags : uint32_t {
        FCF_None        = 0,
        FCF_Title       = 1u<<0,
        FCF_User        = 1u<<1,
        FCF_Email       = 1u<<2,
        FCF_Password    = 1u<<3,
        FCF_Website     = 1u<<4,
        FCF_Group       = 1u<<5,
        FCF_Notes       = 1u<<6,
        FCF_TotpSecret  = 1u<<7,
        FCF_CardNumber  = 1u<<8,
        FCF_CardExpiry  = 1u<<9,
        FCF_CardCvv     = 1u<<10,
        FCF_CardBrand   = 1u<<11,
        FCF_Cardholder  = 1u<<12,
        FCF_FullName    = 1u<<13,
        FCF_IdType      = 1u<<14,
        FCF_IdNumber    = 1u<<15,
        FCF_DateOfBirth = 1u<<16,
        FCF_ExpiryDate  = 1u<<17,
        FCF_Country     = 1u<<18,
        FCF_Address     = 1u<<19,
        FCF_Phone       = 1u<<20,
        FCF_IsFavorite  = 1u<<21,
        FCF_IsPinned    = 1u<<22,
        FCF_ExpiresAt      = 1u<<23,
        FCF_CardAddress    = 1u<<24,
        FCF_CardCity       = 1u<<25,
        FCF_CardPostalCode = 1u<<26,
        FCF_IsNew          = 1u<<31,
    };

    struct AccordionItem
    {
        int id = 0;
        std::string uuid;

        std::string title;
        std::string user;
        std::string email;
        std::string website;
        std::string group;
        std::string notes;
        std::string totp_secret;
        std::vector<std::string> tags;

        CredType type = CredType::Password;

        // Credit Card
        std::string card_number;
        std::string card_expiry;
        std::string card_cvv;
        std::string card_brand;
        std::string cardholder_name;
        std::string card_address;
        std::string card_city;
        std::string card_postal_code;

        // Identity
        std::string full_name;
        std::string id_type;
        std::string id_number;
        std::string date_of_birth;
        std::string expiry_date;
        std::string country;
        std::string address;
        std::string phone;

        bool is_pinned = false;
        bool is_favorite = false;
        int64_t expires_at_ms = 0;  // 0=none, >0=countdown, <0=expired+flagged
        int     expiry_action = 0;  // 0=auto-trash, 1=flag only
        int64_t created_at_ms = 0;  // display only (Unix ms UTC)
        int64_t updated_at_ms = 0;  // display only (Unix ms UTC)
        bool        is_header = false;
        std::string header_label;     // e.g. "December 2025"

        std::vector<PwHistoryEntry> password_history;  // max 5, newest first

        uint32_t changed_fields = 0;  // FieldChangedFlags bitmask
        bool     is_changed = false;  // any field differs from saved snapshot
    };

    struct AccordionListResult
    {
        int delete_id = -1;
        int edit_commit_id = -1;
        int edit_open_id = -1;
        int toggle_pin_id = -1;
        int toggle_fav_id = -1;

        AccordionItem edited{};
        std::string edited_password{};
    };

    using GetPasswordFn = const char* (*)(int id);  // return nullptr/"" to disable password editing

    AccordionListResult RenderAccordionList(
        const std::vector<AccordionItem>& items,
        uint32_t activeVaultKey,
        GetPasswordFn get_password_fn,
        bool read_only,
        ViewMode view_mode
    );

    enum class ToastType { Success, Error, Info };

    void ShowToast(const char* message, ToastType type = ToastType::Success, float duration = 2.5f);
    void RenderToasts();

    void ClipboardCopyPassword(const char* password); // copies + schedules auto-clear
    void TickClipboardClear();

    enum class RepromptAction { None, RevealPassword, RevealNotes, Export, DisableReadOnly, DisableSecuritySetting };

    // returns true immediately if no re-prompt needed; otherwise opens modal
    bool RequestReprompt(RepromptAction action, const std::string& master_password);
    bool IsRepromptApproved(RepromptAction action);
    void ClearRepromptApproval(RepromptAction action);  // call after consuming approval
    void RenderRepromptModal();

    void RenderTrashModal(ShellState& s);
    void RenderSecurityCenterModal(ShellState& s);
    void RenderRecoveryKeyModal(ShellState& s);
    void RenderSettingsPage(ShellState& s);

    extern bool g_show_osk;
    void OskPreFrame();              // call before any widgets
    void RenderOnScreenKeyboard();   // call at end of frame

    bool IsRepromptLockedOut();
    void ResetRepromptLockout();     // call after vault lock/unlock
    void SetRepromptMasterPassword(const std::string& password);

}