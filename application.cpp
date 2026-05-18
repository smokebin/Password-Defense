
// application.cpp (Password Manager - ShellState + VaultState wired)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <algorithm>
#include <atomic>
#include <ctime>
#include <filesystem>
#include <string>
#include <thread>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>

#include "GUI.h"        // your ImGui/D3D bootstrap
#include "UI.h"         // Shell + Accordion
#include "ui_internal.h"
#include "credentials/credential.h"      // Credential struct
#include "credentials/vault_db.h"
#include "credentials/credential_ops.h"
#include "credentials/twofa_ops.h"
#include "credentials/crypto/vault_crypto.h"
#include <sodium.h>
#include "third_party/json.hpp"
#include "credentials/transfer/pwm_file.h"
#include "credentials/transfer/kdbx_export.h"
#include "tools/utility.h"
#include "tools/totp.h"
#include "tools/save.h"          // Config persistence
#include "app_internal.h"     // Shared declarations for split TUs
#include "theme_colors.h"

// serialize_creds_json / deserialize_creds_json moved to app_import_export.cpp

// Pin a buffer in physical RAM (prevent paging to disk)
static void secure_lock(void* ptr, size_t len)
{
    if (ptr && len > 0)
        VirtualLock(ptr, len);
}

// Unpin a buffer (allow paging again) — call before zeroing + freeing
static void secure_unlock(void* ptr, size_t len)
{
    if (ptr && len > 0)
        VirtualUnlock(ptr, len);
}

// Lock a std::vector<uint8_t> in RAM (call after key derivation)
static void lock_key(std::vector<uint8_t>& key)
{
    if (!key.empty())
        secure_lock(key.data(), key.size());
}

// Unlock + zero + clear a key
static void unlock_and_zero_key(std::vector<uint8_t>& key)
{
    if (!key.empty())
    {
        secure_unlock(key.data(), key.size());
        sodium_memzero(key.data(), key.size());
    }
    key.clear();
}

// Lock a std::string in RAM (call after password storage)
static void lock_string(std::string& s)
{
    if (!s.empty())
        secure_lock(s.data(), s.size());
}

// Unlock + zero + clear a string
static void unlock_and_zero_string(std::string& s)
{
    if (!s.empty())
    {
        secure_unlock(s.data(), s.size());
        sodium_memzero(s.data(), s.size());
    }
    s.clear();
}

// Securely zero all sensitive fields of a Credential before destruction
static void secure_clear_credential(Credential& c)
{
    sodium_memzero(c.password.data(), c.password.size());
    sodium_memzero(c.user.data(), c.user.size());
    sodium_memzero(c.email.data(), c.email.size());
    sodium_memzero(c.notes.data(), c.notes.size());
    sodium_memzero(c.totp_secret.data(), c.totp_secret.size());
    sodium_memzero(c.card_number.data(), c.card_number.size());
    sodium_memzero(c.card_cvv.data(), c.card_cvv.size());
    sodium_memzero(c.card_expiry.data(), c.card_expiry.size());
    sodium_memzero(c.cardholder_name.data(), c.cardholder_name.size());
    sodium_memzero(c.card_address.data(), c.card_address.size());
    sodium_memzero(c.card_city.data(), c.card_city.size());
    sodium_memzero(c.card_postal_code.data(), c.card_postal_code.size());
    sodium_memzero(c.id_number.data(), c.id_number.size());
    sodium_memzero(c.phone.data(), c.phone.size());
    sodium_memzero(c.address.data(), c.address.size());
    sodium_memzero(c.date_of_birth.data(), c.date_of_birth.size());
    for (auto& h : c.password_history)
        sodium_memzero(h.password.data(), h.password.size());
}

// Securely zero all credentials in a vector, then clear it
static void secure_clear_credentials(std::vector<Credential>& creds)
{
    for (auto& c : creds)
        secure_clear_credential(c);
    creds.clear();
}

// Securely zero all undo stack strings, then clear the stack
static void secure_clear_undo_stack(std::vector<std::string>& stack)
{
    for (auto& s : stack)
        sodium_memzero(s.data(), s.size());
    stack.clear();
}


struct VaultState
{
    bool        unlocked = false;
    bool        tofa_pending = false;  // 2FA verification pending after password success
    bool        add_pin = false;
    bool        add_fav = false;
    bool        add_open = false;
    bool        status_is_error = false;
    bool        dirty = false;    // Real dirty system (cached)

    bool add_show_password = false;

    // Edit form state
    bool        edit_open = false;
    bool        edit_pin = false;
    bool        edit_fav = false;
    bool        edit_show_password = false;
    int         edit_id = -1;  // ID of Credential being edited

    uint32_t saved_hash = 0;
    uint32_t current_hash = 0;
    uint32_t last_groups_hash = 0;  // Per-vault group rebuild tracking

    std::string vault_path;              // .db file path
    std::vector<uint8_t> master_key;     // Derived key (kept in memory while unlocked)
    std::string session_password;        // Password (kept for restore operations)
    std::string add_password_buf{};
    std::string edit_password_buf{};
    std::string status_msg;

    std::vector<std::string> undo_stack;
    std::vector<Credential> creds;
    std::unordered_map<std::string, Credential> saved_snapshot; // keyed by uuid

    Credential  add_buf{};
    Credential  edit_buf{};

    helpers::GenOptions add_gen_opt; // optional (if you want persistent toggles)
    helpers::GenOptions edit_gen_opt;

    void init_defaults_once()
    {
        static bool inited = false;
        if (inited) return;
        inited = true;

        add_gen_opt.length = 30;
        add_gen_opt.use_upper = true;
        add_gen_opt.use_lower = true;
        add_gen_opt.use_digit = true;
        add_gen_opt.use_symbol = true;
        add_gen_opt.avoid_ambiguous = false;

        edit_gen_opt.length = 30;
        edit_gen_opt.use_upper = true;
        edit_gen_opt.use_lower = true;
        edit_gen_opt.use_digit = true;
        edit_gen_opt.use_symbol = true;
        edit_gen_opt.avoid_ambiguous = false;
    }

    void set_status(const std::string& msg, bool is_err)
    {
        status_msg = msg;
        status_is_error = is_err;
    }

    void clear_status()
    {
        status_msg.clear();
        status_is_error = false;
    }

    bool is_dirty() const
    {
        return unlocked && dirty;
    }

    void push_undo()
    {
        // Store snapshot BEFORE change
        undo_stack.push_back(serialize_creds_json(creds));
        // cap (optional) — securely zero evicted entry
        if (undo_stack.size() > 128)
        {
            sodium_memzero(undo_stack.front().data(), undo_stack.front().size());
            undo_stack.erase(undo_stack.begin());
        }
    }

    void apply_snapshot_json(const std::string& json)
    {
        creds = deserialize_creds_json(json);
    }

    void clear_sensitive()
    {
        // Unlock from physical RAM, zero, and free
        unlock_and_zero_key(master_key);
        unlock_and_zero_string(session_password);

        // Zero saved snapshot Credential data before clearing
        for (auto& [uuid, c] : saved_snapshot)
            secure_clear_credential(c);
        saved_snapshot.clear();

        unlocked = false;
        tofa_pending = false;
    }

    void reset_add_form()
    {
        add_buf = Credential{};
        add_password_buf.clear();
        add_pin = false;
        add_fav = false;
        add_show_password = false;
    }

    void reset_edit_form()
    {
        edit_buf = Credential{};
        edit_password_buf.clear();
        edit_pin = false;
        edit_fav = false;
        edit_show_password = false;
        edit_id = -1;
    }

    void begin_edit(const Credential& c, const std::string& password)
    {
        edit_buf = c;
        edit_password_buf = password;
        edit_pin = c.is_pinned;
        edit_fav = c.is_favorite;
        edit_show_password = false;
        edit_id = c.id;
        edit_open = true;
    }
};

// ============================================================
// Credential Modal: Unified Add/Edit dialog
// ============================================================
struct CredentialModal
{
    enum class Mode { Closed, Add, Edit };
    Mode mode = Mode::Closed;

    // Shared form state
    Credential buf{};
    std::string password_buf{};
    std::string totp_secret_buf{};
    std::string tag_input_buf{};
    bool show_password = false;
    bool is_pinned = false;
    bool is_favorite = false;
    bool is_timed = false;
    int  duration_idx = 3;        // default: 24 hours
    int  expiry_action_idx = 0;   // default: auto-trash
    helpers::GenOptions gen_opt{ 30, true, true, true, true, false };

    CredType selected_type = CredType::Password;

    // Edit-specific
    int edit_id = -1;

    bool IsOpen() const { return mode != Mode::Closed; }
    bool IsAdd()  const { return mode == Mode::Add; }
    bool IsEdit() const { return mode == Mode::Edit; }

    void OpenAdd()
    {
        mode = Mode::Add;
        buf = Credential{};
        password_buf.clear();
        totp_secret_buf.clear();
        tag_input_buf.clear();
        show_password = false;
        is_pinned = false;
        is_favorite = false;
        is_timed = false;
        duration_idx = 3;
        expiry_action_idx = 0;
        selected_type = CredType::Password;
        edit_id = -1;
        // Keep gen_opt settings persistent
    }

    void OpenEdit(const Credential& c, const std::string& pw)
    {
        mode = Mode::Edit;
        buf = c;
        // Pre-format masked fields for display
        buf.card_number   = ui::FormatWithPattern(ui::StripNonDigits(c.card_number),   "#### #### #### ####");
        buf.card_expiry   = ui::FormatWithPattern(ui::StripNonDigits(c.card_expiry),   "## / ##");
        buf.date_of_birth = ui::FormatWithPattern(ui::StripNonDigits(c.date_of_birth), "## / ## / ####");
        buf.expiry_date   = ui::FormatWithPattern(ui::StripNonDigits(c.expiry_date),   "## / ## / ####");
        buf.phone         = ui::FormatWithPattern(ui::StripNonDigits(c.phone),         "(###) ###-####");
        password_buf = pw;
        totp_secret_buf = c.totp_secret;
        tag_input_buf.clear();
        show_password = false;
        is_pinned = c.is_pinned;
        is_favorite = c.is_favorite;
        is_timed = (c.expires_at_ms > 0);
        duration_idx = 3;  // default to 24h when editing
        expiry_action_idx = c.expiry_action;
        selected_type = c.type;
        edit_id = c.id;
    }

    void Close()
    {
        mode = Mode::Closed;
    }

    void Reset()
    {
        mode = Mode::Closed;
        buf = Credential{};
        password_buf.clear();
        totp_secret_buf.clear();
        tag_input_buf.clear();
        show_password = false;
        is_pinned = false;
        is_favorite = false;
        is_timed = false;
        duration_idx = 3;
        expiry_action_idx = 0;
        selected_type = CredType::Password;
        edit_id = -1;
    }
};

static CredentialModal g_cred_modal;



struct AutoSaveController
{
    bool enabled = true;

    double last_change_time = 0.0;
    double last_interaction_time = 0.0;
    double last_save_time = 0.0;

    bool suspended = false;
};

static AutoSaveController g_autosave;
static bool g_backups_dirty = true;
static VaultState g_vault;
static ui::ShellState g_shell;

// Forward declarations
static std::string new_db_path();
static std::vector<std::string> list_vaults_next_to_exe();

struct VaultTab
{
    std::string label;   // filename-ish label for the tab
    VaultState  vault;   // path, unlocked, creds, status
    uint32_t    tab_id = 0;  // unique ID for this tab (never changes, even if path is empty)
};

static std::vector<VaultTab> g_tabs;
static int g_active_tab = 0;
static uint32_t g_next_tab_id = 1;  // counter for unique tab IDs

static VaultTab& ActiveTab()
{
    if (g_tabs.empty())
    {
        g_tabs.push_back({});
        g_tabs[0].label = "Vault";
        g_tabs[0].vault.vault_path = new_db_path();
        g_tabs[0].tab_id = g_next_tab_id++;
        g_active_tab = 0;
    }
    g_active_tab = ImClamp(g_active_tab, 0, (int)g_tabs.size() - 1);
    return g_tabs[g_active_tab];
}

static VaultState& ActiveVault() { return ActiveTab().vault; }

// Exposed for UI layer to access master key during 2FA setup
std::vector<uint8_t> Get2FAMasterKey()
{
    return ActiveVault().master_key;
}

// Exposed for UI layer to access Credential list (Security Center modal)
const std::vector<Credential>& GetActiveVaultCreds()
{
    return ActiveVault().creds;
}

static uint32_t GetVaultKey(const VaultTab& tab)
{
    // Use vault path if available, otherwise use stable tab_id
    if (!tab.vault.vault_path.empty())
        return helpers::fnv1a_32(tab.vault.vault_path.c_str());
    return tab.tab_id;
}

static uint32_t GetActiveVaultKey()
{
    return GetVaultKey(ActiveTab());
}

// ============================================================
// Minimal WinHTTP helper for HIBP breach check
// ============================================================
#include <winhttp.h>
#pragma comment(lib, "winhttp.lib")

struct HttpResponse { bool success = false; int status_code = 0; std::string body; std::string error; };

static HttpResponse win_http_request(const char* method, const char* host, int port, bool https, const std::string& path)
{
    HttpResponse r;
    HINTERNET hSession = WinHttpOpen(L"PasswordDefense/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0);
    if (!hSession) { r.error = "WinHttpOpen failed"; return r; }

    std::wstring wHost(host, host + strlen(host));
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), (INTERNET_PORT)port, 0);
    if (!hConnect) { WinHttpCloseHandle(hSession); r.error = "WinHttpConnect failed"; return r; }

    std::wstring wPath(path.begin(), path.end());
    std::wstring wMethod(method, method + strlen(method));
    DWORD flags = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hRequest = WinHttpOpenRequest(hConnect, wMethod.c_str(), wPath.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
    if (!hRequest) { WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); r.error = "WinHttpOpenRequest failed"; return r; }

    if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hRequest, nullptr)) {
        r.error = "Request failed"; WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession); return r;
    }

    DWORD statusCode = 0, sz = sizeof(statusCode);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &statusCode, &sz, nullptr);
    r.status_code = (int)statusCode;

    DWORD bytesRead = 0;
    char buf[4096];
    while (WinHttpReadData(hRequest, buf, sizeof(buf), &bytesRead) && bytesRead > 0) {
        r.body.append(buf, bytesRead);
        bytesRead = 0;
    }

    r.success = true;
    WinHttpCloseHandle(hRequest); WinHttpCloseHandle(hConnect); WinHttpCloseHandle(hSession);
    return r;
}

// ============================================================
// HIBP Pwned Passwords breach check (k-anonymity, background thread)
// ============================================================
static void CheckBreachedPasswords(ui::ShellState& shell, const std::vector<Credential>& creds)
{
    // Collect unique passwords → Credential IDs
    std::unordered_map<std::string, std::vector<int>> pw_to_ids;
    for (const auto& c : creds) {
        if (!c.is_deleted() && c.type == CredType::Password && !c.password.empty())
            pw_to_ids[c.password].push_back(c.id);
    }

    shell.sec_breach_checking = true;
    shell.sec_breach_checked  = 0;
    shell.sec_breach_total    = (int)pw_to_ids.size();
    shell.sec_breach_error.clear();

    std::thread([&shell, pw_map = std::move(pw_to_ids)]() {
        std::unordered_set<int> exposed_ids;
        int exposed_count = 0;
        int checked = 0;
        std::string error;  // accumulate locally, write once at end

        for (const auto& [pw, ids] : pw_map) {
            // SHA-1 hash the password
            uint8_t digest[20];
            totp::sha1_digest((const uint8_t*)pw.data(), pw.size(), digest);

            // Convert to uppercase hex
            char hex[41];
            for (int i = 0; i < 20; i++)
                snprintf(hex + i * 2, 3, "%02X", digest[i]);
            hex[40] = '\0';

            std::string prefix(hex, 5);
            std::string suffix(hex + 5);

            // Query HIBP k-anonymity API
            std::string path = "/range/" + prefix;
            auto resp = win_http_request("GET", "api.pwnedpasswords.com", 443, true, path);

            if (resp.success && resp.status_code == 200) {
                // Parse: each line is "SUFFIX:COUNT\r\n"
                size_t pos = 0;
                while (pos < resp.body.size()) {
                    size_t eol = resp.body.find('\n', pos);
                    if (eol == std::string::npos) eol = resp.body.size();
                    std::string line = resp.body.substr(pos, eol - pos);
                    if (!line.empty() && line.back() == '\r') line.pop_back();

                    size_t colon = line.find(':');
                    if (colon != std::string::npos) {
                        std::string line_suffix = line.substr(0, colon);
                        if (line_suffix == suffix) {
                            for (int id : ids) exposed_ids.insert(id);
                            exposed_count += (int)ids.size();
                            break;
                        }
                    }
                    pos = eol + 1;
                }
            } else {
                error = resp.error.empty()
                    ? "HIBP API returned " + std::to_string(resp.status_code)
                    : resp.error;
                break;
            }

            checked++;
            shell.sec_breach_checked = checked;

            // Rate limit: 1.5s between requests (HIBP free tier)
            Sleep(1500);
        }

        // Write to staging fields (only touched by background thread).
        // The UI thread will move these to live fields when it sees sec_breach_done.
        shell.sec_exposed_ids_staging   = std::move(exposed_ids);
        shell.sec_exposed_count_staging = exposed_count;
        shell.sec_breach_error_staging  = std::move(error);
        shell.sec_breach_done.store(true, std::memory_order_release);
        shell.sec_breach_checking.store(false, std::memory_order_release);
    }).detach();
}

static void PushScreen(ui::ShellState& shell, ui::Screen next)
{
    if (shell.active_screen == next)
        return;
    shell.screen_stack.push_back(shell.active_screen);
    shell.active_screen = next;
}

static void PopScreen(ui::ShellState& shell)
{
    if (shell.screen_stack.empty())
        return;
    shell.active_screen = shell.screen_stack.back();
    shell.screen_stack.pop_back();
}

static void ResetScreen(ui::ShellState& shell, ui::Screen next)
{
    shell.screen_stack.clear();
    shell.active_screen = next;
}


// Generate path for new vault database next to exe
static std::string new_db_path()
{
    char exe_path_c[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path_c, MAX_PATH);

    std::filesystem::path exe_parent_path = std::filesystem::path(exe_path_c).parent_path();

    std::string prefix = "vault-";
    int count = 0;

    for (const auto& entry : std::filesystem::directory_iterator(exe_parent_path))
    {
        if (!entry.is_regular_file()) continue;
        const auto name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0 && entry.path().extension() == ".db")
            ++count;
    }

    return (exe_parent_path / ("vault-" + std::to_string(count) + ".db")).string();
}

// List all .db vault files next to the exe
static std::vector<std::string> list_vaults_next_to_exe()
{
    std::vector<std::string> vaults;

    char exe_path_c[MAX_PATH];
    GetModuleFileNameA(NULL, exe_path_c, MAX_PATH);

    std::filesystem::path exe_parent_path = std::filesystem::path(exe_path_c).parent_path();

    try
    {
        for (const auto& entry : std::filesystem::directory_iterator(exe_parent_path))
        {
            if (!entry.is_regular_file()) continue;
            if (entry.path().extension() == ".db")
            {
                vaults.push_back(entry.path().string());
            }
        }
    }
    catch (...)
    {
        // Directory iteration failed - return empty
    }

    // Sort alphabetically by filename
    std::sort(vaults.begin(), vaults.end());

    return vaults;
}

static std::string BasenameNoExt(const std::string& path)
{
    size_t slash = path.find_last_of("\\/");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);

    size_t dot = name.find_last_of('.');
    if (dot != std::string::npos) name = name.substr(0, dot);
    if (name.empty()) name = "Vault";
    return name;
}

static void CreatePreOpBackup(const VaultState& v, const char* tag)
{
    if (v.vault_path.empty()) return;

    std::string dir = helpers::GetDefaultBackupDir();

    // Ensure backup directory exists
    helpers::create_directories(dir);

    std::string base = BasenameNoExt(v.vault_path);
    std::string ts = helpers::now_iso8601_local();
    helpers::SanitizeFilename(ts);

    std::string dst =
        dir + "\\" + base + "_PRE-" + tag + "_" + ts + ".lbdb";

    helpers::CopyFileAtomic(v.vault_path, dst);
    helpers::EnforceBackupRetention(dir, 20);
    g_backups_dirty = true;
}

static void SyncShellTabsFromVaultTabs()
{
    g_shell.db_labels.clear();
    g_shell.db_labels.reserve(g_tabs.size());

    for (auto& t : g_tabs)
        g_shell.db_labels.push_back(t.label.empty() ? "Vault" : t.label);

    g_shell.active_db = ImClamp(g_active_tab, 0, (int)g_shell.db_labels.size() - 1);
}

static void ApplyShellActiveTab()
{
    g_active_tab = ImClamp(g_shell.active_db, 0, (int)g_tabs.size() - 1);
}

static int FindTabByPath(const std::string& path)
{
    for (int i = 0; i < (int)g_tabs.size(); ++i)
        if (g_tabs[i].vault.vault_path == path)
            return i;
    return -1;
}

static void HandleOpenDB()
{
    std::string path = PickOpenFilePath_DB();
    if (path.empty()) return;

    int existing = FindTabByPath(path);
    if (existing != -1)
    {
        g_active_tab = existing;
        return;
    }

    VaultTab t{};
    t.vault.vault_path = path;
    t.vault.unlocked = false;
    t.label = BasenameNoExt(path);
    t.tab_id = g_next_tab_id++;

    g_tabs.push_back(std::move(t));
    g_active_tab = (int)g_tabs.size() - 1;

    // No need to clear UI state for brand new vault (no state exists yet)
}

static void HandleNewDB()
{
    std::string path = PickSaveFilePath_DB();
    if (path.empty()) return;

    int existing = FindTabByPath(path);
    if (existing != -1)
    {
        g_active_tab = existing;
        return;
    }

    VaultTab t{};
    t.vault.vault_path = path;
    t.vault.unlocked = false;
    t.label = BasenameNoExt(path);
    t.tab_id = g_next_tab_id++;

    g_tabs.push_back(std::move(t));
    g_active_tab = (int)g_tabs.size() - 1;

    // No need to clear UI state for brand new vault (no state exists yet)
}

static uint32_t HashGroupsOnly(const std::vector<Credential>& creds)
{
    uint32_t h = 2166136261u;
    for (auto& c : creds)
    {
        for (char ch : c.group) { h ^= (uint8_t)ch; h *= 16777619u; }
        h ^= 0x9E; h *= 16777619u;
    }
    return h ? h : 1u;
}

static uint32_t HashCredsNow(const std::vector<Credential>& creds)
{
    // Serialize ONLY when we explicitly decide to refresh dirty state.
    // (not every frame)
    std::string json = serialize_creds_json(creds);
    return helpers::fnv1a_32(json.c_str());
}

static void VaultRecomputeDirty(VaultState& v)
{
    v.current_hash = HashCredsNow(v.creds);
    v.dirty = (v.current_hash != v.saved_hash);
}

static void VaultMarkSaved(VaultState& v)
{
    v.saved_hash = HashCredsNow(v.creds);
    v.current_hash = v.saved_hash;
    v.dirty = false;

    // Rebuild saved snapshot for change highlighting
    v.saved_snapshot.clear();
    for (const auto& c : v.creds)
    {
        if (!c.uuid.empty())
            v.saved_snapshot[c.uuid] = c;
    }
}

static void VaultMarkChanged(VaultState& v, const char* preTagForBackup)
{
    // If we are currently clean, take the one-time "first edit" safety backup.
    if (!v.dirty && v.unlocked && !v.vault_path.empty())
        CreatePreOpBackup(v, preTagForBackup ? preTagForBackup : "EDIT");

    VaultRecomputeDirty(v);

    // Your autosave debounce uses this
    g_autosave.last_change_time = ImGui::GetTime();
}


static void rebuild_groups(ui::ShellState& s, const std::vector<Credential>& creds)
{
    std::unordered_set<std::string> uniq;
    uniq.reserve(creds.size());

    for (const auto& c : creds)
        if (!c.group.empty())
            uniq.insert(c.group);

    std::vector<std::string> out;
    out.reserve(7 + uniq.size());
    out.push_back("Filter");
    out.push_back("@Passwords");
    out.push_back("@Cards");
    out.push_back("@Identity");
    out.push_back("@Notes");
    out.push_back("---");  // separator sentinel

    // stable-ish order
    std::vector<std::string> tmp;
    tmp.reserve(uniq.size());
    for (const auto& g : uniq) tmp.push_back(g);
    std::sort(tmp.begin(), tmp.end());

    for (auto& g : tmp) out.push_back(g);

    s.groups = std::move(out);

    // Prune selected_groups to only contain groups that still exist
    std::set<std::string> pruned;
    for (const auto& g : s.selected_groups)
        if (std::find(s.groups.begin(), s.groups.end(), g) != s.groups.end())
            pruned.insert(g);
    s.selected_groups = std::move(pruned);
}

static void rebuild_tags(ui::ShellState& s, const std::vector<Credential>& creds)
{
    std::unordered_set<std::string> uniq;
    for (const auto& c : creds)
        for (const auto& t : c.tags)
            if (!t.empty()) uniq.insert(t);

    std::vector<std::string> out(uniq.begin(), uniq.end());
    std::sort(out.begin(), out.end());
    s.all_tags = std::move(out);

    // Prune selected_tags to only contain tags that still exist
    std::set<std::string> pruned;
    for (const auto& t : s.selected_tags)
        if (std::find(s.all_tags.begin(), s.all_tags.end(), t) != s.all_tags.end())
            pruned.insert(t);
    s.selected_tags = std::move(pruned);
}

// Called from UI trash modal after restoring a Credential
void ReloadVaultCredentials()
{
    VaultState& v = ActiveVault();
    if (v.master_key.empty()) return;
    v.creds = cred_ops::load_all(v.master_key);
    VaultMarkSaved(v);
    rebuild_groups(g_shell, v.creds);
    rebuild_tags(g_shell, v.creds);
    ui::ForgetVaultRowState(GetActiveVaultKey());
}

// ============================================================
// Tag editor widget with autocomplete
// ============================================================
static void render_tag_editor(float width)
{
    auto& tags = g_cred_modal.buf.tags;
    const float pillH = 20.0f;
    const float pillPad = 4.0f;
    const float pillGap = 4.0f;
    const float pillRounding = 10.0f;
    bool dark = g_shell.dark_theme;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Render existing tag pills
    float startX = ImGui::GetCursorScreenPos().x;
    float curX = startX;
    float curY = ImGui::GetCursorScreenPos().y;
    float maxX = startX + width;
    int removeIdx = -1;

    for (int i = 0; i < (int)tags.size(); i++)
    {
        const auto& t = tags[i];
        ImVec2 tSz = ImGui::CalcTextSize(t.c_str());
        ImVec2 xSz = ImGui::CalcTextSize(ICON_MDI_CLOSE);
        float pillW = pillPad * 2 + tSz.x + 4.0f + xSz.x + 2.0f;

        // Wrap to next line if needed
        if (curX + pillW > maxX && curX > startX)
        {
            curX = startX;
            curY += pillH + 2.0f;
        }

        ImVec2 pMin(curX, curY);
        ImVec2 pMax(curX + pillW, curY + pillH);

        // Pill background
        ImU32 pillBg = dark ? IM_COL32(255, 255, 255, 20) : IM_COL32(0, 0, 0, 15);
        dl->AddRectFilled(pMin, pMax, pillBg, pillRounding);

        // Tag text
        float textY = curY + (pillH - tSz.y) * 0.5f;
        dl->AddText(ImVec2(curX + pillPad, textY), ImGui::GetColorU32(ImGuiCol_Text), t.c_str());

        // X button
        float xX = curX + pillPad + tSz.x + 4.0f;
        dl->AddText(ImVec2(xX, textY), ImGui::GetColorU32(ImGuiCol_TextDisabled), ICON_MDI_CLOSE);

        // Invisible button for the whole pill to remove
        ImGui::SetCursorScreenPos(pMin);
        char btnId[32];
        snprintf(btnId, sizeof(btnId), "##tagpill_%d", i);
        ImGui::InvisibleButton(btnId, ImVec2(pillW, pillH));
        if (ImGui::IsItemClicked()) removeIdx = i;
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove tag");

        curX += pillW + pillGap;
    }

    if (removeIdx >= 0)
        tags.erase(tags.begin() + removeIdx);

    // Move cursor to after pills
    if (!tags.empty())
    {
        curY += pillH + 4.0f;
        ImGui::SetCursorScreenPos(ImVec2(startX, curY));
    }

    // Tag input — type and press Enter, or pick from dropdown button
    {
        // Available tags (exclude already-added)
        std::vector<std::string> availTags;
        for (const auto& t : g_shell.all_tags)
        {
            bool already = false;
            for (const auto& existing : tags)
                if (existing == t) { already = true; break; }
            if (!already)
                availTags.push_back(t);
        }

        static std::string s_tag_input;
        ImGui::SetNextItemWidth(width);
        bool picked = ui::SearchableCombo("Add tag...##modal_tags", s_tag_input, availTags, &g_shell.sb_tag_counts);

        bool addTag = picked
                    || (ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Enter));

        if (addTag && !s_tag_input.empty())
        {
            std::string trimmed = s_tag_input;
            while (!trimmed.empty() && trimmed.front() == ' ') trimmed.erase(trimmed.begin());
            while (!trimmed.empty() && trimmed.back() == ' ') trimmed.pop_back();
            if (!trimmed.empty())
            {
                bool exists = false;
                for (const auto& t : tags)
                    if (t == trimmed) { exists = true; break; }
                if (!exists)
                    tags.push_back(trimmed);
            }
            s_tag_input.clear();
        }
    }
}

// Group input with searchable combo dropdown
static void render_group_input(float width)
{
    auto& group = g_cred_modal.buf.group;

    // Build filtered group list (exclude system entries)
    std::vector<std::string> userGroups;
    for (const auto& g : g_shell.groups)
    {
        if (g.empty() || g[0] == '@' || g == "---" || g == "Filter" || g == "All") continue;
        userGroups.push_back(g);
    }

    ImGui::SetNextItemWidth(width);
    ui::SearchableCombo("Group##modal_group", group, userGroups, &g_shell.sb_group_counts);
}

// ============================================================
// Credential Modal Rendering
// ============================================================
static void render_credential_modal(VaultState& v)
{
    if (!g_cred_modal.IsOpen()) return;

    static constexpr int64_t timer_dur_ms[] = {
        30 * time_ms::MINUTE,       //  30 min
        time_ms::HOUR,              //   1 hour
        6 * time_ms::HOUR,          //   6 hours
        time_ms::DAY,               //   1 day
        time_ms::WEEK,              //   7 days
        30 * time_ms::DAY,          //  30 days
        90 * time_ms::DAY,          //  90 days
    };

    // Theme-aware modal colors
    const bool dark = g_shell.dark_theme;
    const ImU32 popupBg = dark
        ? theme::ModalBg.dark
        : theme::ModalBg.light;
    const ImU32 dimBg = colors::DimOverlayLight;  // Same for both themes

    // Styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(24, 20));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, popupBg);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, dimBg);

    // Fixed width, auto height — wider to accommodate two-column layout
    const float modalW = 560.0f;
    ImGui::SetNextWindowSizeConstraints(ImVec2(modalW, 0), ImVec2(modalW, FLT_MAX));

    // Re-center on every frame so the modal follows window resizes
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    bool modal_open = true;
    if (ImGui::BeginPopupModal("Add/Edit###cred_modal", &modal_open, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
    {

        // Keyboard shortcuts
        bool escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
        bool submit_shortcut = ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_Enter);

        // Form fields
        const float fieldW = ImGui::GetContentRegionAvail().x;

        // Header with close button
        ImGui::PushFont(render::FontLarge);
        ImGui::TextUnformatted(g_cred_modal.IsAdd() ? "Add Credential" : "Edit Credential");
        ImGui::PopFont();
        {
            // Close text
            ImVec2 closeSz = ImGui::CalcTextSize(ICON_MDI_CLOSE);
            ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - closeSz.x);
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
            ImGui::TextUnformatted(ICON_MDI_CLOSE);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(0) || escape_pressed)
            {
                g_cred_modal.Close();
                ImGui::CloseCurrentPopup();
            }

            //ImGui::Dummy(ImVec2(0, 6));
        }
        ImGui::Dummy(ImVec2(0, 8));

        // Type selector (Add mode: interactive buttons, Edit mode: read-only label)
        {
            const CredType ct = g_cred_modal.selected_type;
            if (g_cred_modal.IsAdd())
            {
                struct TypeBtn { CredType t; const char* icon; const char* label; };
                TypeBtn btns[] = {
                    { CredType::Password,   ICON_MDI_KEY,         "Password"    },
                    { CredType::CreditCard, ICON_MDI_CREDIT_CARD, "Card"        },
                    { CredType::Identity,   ICON_MDI_CARD_ACCOUNT_DETAILS,     "Identity"    },
                    { CredType::SecureNote, ICON_MDI_NOTE_TEXT, "Note"        },
                };

                const int numTabs = 4;
                const float tabH = 30.0f;
                const float pillR = 7.0f;
                const float cardR = 5.0f;
                const float pillPad = 2.0f;
                bool dark = g_shell.dark_theme;

                // Pill background
                ImU32 pillBg = dark ? IM_COL32(40, 40, 44, 255) : IM_COL32(232, 232, 236, 255);
                ImU32 cardBg = dark ? IM_COL32(60, 60, 66, 255) : IM_COL32(255, 255, 255, 255);
                ImU32 cardShadow = dark ? IM_COL32(0, 0, 0, 70) : IM_COL32(0, 0, 0, 35);

                ImDrawList* dl = ImGui::GetWindowDrawList();
                ImVec2 pillPos = ImGui::GetCursorScreenPos();
                float pillW = fieldW;

                // Draw pill background
                dl->AddRectFilled(pillPos, ImVec2(pillPos.x + pillW, pillPos.y + tabH), pillBg, pillR);

                // Segment width
                float segW = pillW / numTabs;

                // Find active index
                int activeIdx = 0;
                for (int i = 0; i < numTabs; i++)
                    if (ct == btns[i].t) { activeIdx = i; break; }

                // Draw active card (raised, with shadow)
                {
                    float cardX = pillPos.x + activeIdx * segW + pillPad;
                    float cardY = pillPos.y + pillPad;
                    float cardW = segW - pillPad * 2.0f;
                    float cardH = tabH - pillPad * 2.0f;
                    ImVec2 cMin(cardX, cardY);
                    ImVec2 cMax(cardX + cardW, cardY + cardH);

                    // Shadow
                    dl->AddRectFilled(
                        ImVec2(cMin.x + 0.5f, cMin.y + 1.0f),
                        ImVec2(cMax.x + 0.5f, cMax.y + 1.0f),
                        cardShadow, cardR);
                    // Card fill
                    dl->AddRectFilled(cMin, cMax, cardBg, cardR);
                }

                // Hit test + text for each segment
                for (int i = 0; i < numTabs; i++)
                {
                    bool active = (ct == btns[i].t);
                    float segX = pillPos.x + i * segW;

                    char bid[32]; snprintf(bid, sizeof(bid), "##type_%d", i);
                    ImGui::SetCursorScreenPos(ImVec2(segX, pillPos.y));
                    ImGui::PushID(bid);
                    ImGui::InvisibleButton("##tb", ImVec2(segW, tabH));
                    bool hovered = ImGui::IsItemHovered();
                    if (ImGui::IsItemClicked())
                        g_cred_modal.selected_type = btns[i].t;
                    ImGui::PopID();

                    // Hover highlight on inactive
                    if (hovered && !active)
                    {
                        ImU32 hovCol = dark ? IM_COL32(255, 255, 255, 10) : IM_COL32(0, 0, 0, 8);
                        dl->AddRectFilled(
                            ImVec2(segX + pillPad, pillPos.y + pillPad),
                            ImVec2(segX + segW - pillPad, pillPos.y + tabH - pillPad),
                            hovCol, cardR);
                    }

                    // Text
                    char lbl[48]; snprintf(lbl, sizeof(lbl), "%s %s", btns[i].icon, btns[i].label);
                    ImVec2 ts = ImGui::CalcTextSize(lbl);
                    ImVec2 tp(segX + (segW - ts.x) * 0.5f, pillPos.y + (tabH - ts.y) * 0.5f);
                    ImU32 tcol = active
                        ? ImGui::GetColorU32(ImGuiCol_Text)
                        : ImGui::GetColorU32(ImGuiCol_TextDisabled);
                    dl->AddText(tp, tcol, lbl);
                }

                ImGui::SetCursorScreenPos(ImVec2(pillPos.x, pillPos.y + tabH));
                ImGui::Dummy(ImVec2(0, 4));
            }
            else
            {
                ImGui::TextDisabled("%s", CredTypeLabel(ct));
                ImGui::Dummy(ImVec2(0, 2));
            }
        }

        const CredType ct = g_cred_modal.selected_type;

        // Two-column layout helpers
        const float colGap = 12.0f;
        const float halfW = (fieldW - colGap) * 0.5f;

        // Title (all types) — full width
        ImGui::SetNextItemWidth(fieldW);
        ui::InputTextString("Title##modal_title", &g_cred_modal.buf.title);
        ImGui::Spacing();

        // Type-specific fields — two columns where possible
        if (ct == CredType::Password)
        {
            // Row 1: Email | User
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextString("Email##modal_email", &g_cred_modal.buf.email);
            ImGui::SameLine(0, colGap);
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextString("User##modal_user", &g_cred_modal.buf.user);
            ImGui::Spacing();

            // Row 2: Password (full width — has icon buttons)
            ui::PasswordFieldRow("##modal_pw", g_cred_modal.password_buf,
                g_cred_modal.show_password, g_cred_modal.gen_opt, fieldW);
            ImGui::Spacing();

            // Row 3: Website (full width)
            ImGui::SetNextItemWidth(fieldW);
            ui::InputTextString("Website##modal_website", &g_cred_modal.buf.website);
            ImGui::Spacing();

            // Row 4: Group | Tags
            render_group_input(halfW);
            ImGui::SameLine(0, colGap);
            render_tag_editor(halfW);
            ImGui::Spacing();
        }
        else if (ct == CredType::CreditCard)
        {
            // Row 1: Cardholder | Brand
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextString("Cardholder Name##modal_cardholder", &g_cred_modal.buf.cardholder_name);
            ImGui::SameLine(0, colGap);
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextString("Brand##modal_brand", &g_cred_modal.buf.card_brand);
            ImGui::Spacing();

            // Row 2: Card Number | Street Address
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextFormatted("Card Number##modal_cardnum", &g_cred_modal.buf.card_number, "#### #### #### ####");
            ImGui::SameLine(0, colGap);
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextString("Street Address##modal_card_addr", &g_cred_modal.buf.card_address);
            ImGui::Spacing();

            // Row 3: Expiry | CVV
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextFormatted("Expiry (MM/YY)##modal_expiry", &g_cred_modal.buf.card_expiry, "## / ##");
            ImGui::SameLine(0, colGap);
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextString("CVV##modal_cvv", &g_cred_modal.buf.card_cvv);
            ImGui::Spacing();

            // Row 4: City | Postal Code
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextString("City##modal_card_city", &g_cred_modal.buf.card_city);
            ImGui::SameLine(0, colGap);
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextString("Postal Code##modal_card_postal", &g_cred_modal.buf.card_postal_code);
            ImGui::Spacing();

            // Row 6: Group | Tags
            render_group_input(halfW);
            ImGui::SameLine(0, colGap);
            render_tag_editor(halfW);
            ImGui::Spacing();
        }
        else if (ct == CredType::Identity)
        {
            // Row 1: Full Name | Country
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextString("Full Name##modal_fullname", &g_cred_modal.buf.full_name);
            ImGui::SameLine(0, colGap);
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextString("Country##modal_country", &g_cred_modal.buf.country);
            ImGui::Spacing();

            // Row 2: ID Type | ID Number
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextString("ID Type##modal_idtype", &g_cred_modal.buf.id_type);
            ImGui::SameLine(0, colGap);
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextString("ID Number##modal_idnum", &g_cred_modal.buf.id_number);
            ImGui::Spacing();

            // Row 3: Date of Birth | Expiry
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextFormatted("Date of Birth##modal_dob", &g_cred_modal.buf.date_of_birth, "## / ## / ####");
            ImGui::SameLine(0, colGap);
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextFormatted("Expiry##modal_idexpiry", &g_cred_modal.buf.expiry_date, "## / ## / ####");
            ImGui::Spacing();

            // Row 4: Address | Phone
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextString("Address##modal_address", &g_cred_modal.buf.address);
            ImGui::SameLine(0, colGap);
            ImGui::SetNextItemWidth(halfW);
            ui::InputTextFormatted("Phone##modal_phone", &g_cred_modal.buf.phone, "(###) ###-####");
            ImGui::Spacing();

            // Row 5: Group | Tags
            render_group_input(halfW);
            ImGui::SameLine(0, colGap);
            render_tag_editor(halfW);
            ImGui::Spacing();
        }
        else
        {
            // SecureNote: Group | Tags
            render_group_input(halfW);
            ImGui::SameLine(0, colGap);
            render_tag_editor(halfW);
            ImGui::Spacing();
        }

        // Notes (all types, full width — larger for SecureNote)
        ImGui::TextDisabled("Notes");
        float notesH = (ct == CredType::SecureNote) ? 180.0f : 60.0f;
        ui::InputTextMultilineString("##modal_notes", &g_cred_modal.buf.notes,
            ImVec2(fieldW, notesH), 0);

        ImGui::Spacing();

        // TOTP Secret field (Password type only)
        if (ct == CredType::Password)
        {
            //ImGui::TextDisabled(ICON_MDI_CLOCK " TOTP Secret");
            ImGui::SetNextItemWidth(fieldW - 110.0f);
            ui::InputTextString(ICON_MDI_CLOCK " TOTP Secret", &g_cred_modal.totp_secret_buf);

            // Auto-parse otpauth:// URIs on paste
            if (!g_cred_modal.totp_secret_buf.empty() &&
                g_cred_modal.totp_secret_buf.size() > 15 &&
                g_cred_modal.totp_secret_buf.find("otpauth://") != std::string::npos)
            {
                std::string parsed;
                if (totp::parse_otpauth_uri(g_cred_modal.totp_secret_buf, parsed))
                    g_cred_modal.totp_secret_buf = parsed;
            }

            // Live preview of current code
            if (!g_cred_modal.totp_secret_buf.empty())
            {
                auto bytes = totp::base32_decode(g_cred_modal.totp_secret_buf);
                if (bytes.size() >= 10) {
                    std::string code = totp::generate_code_now(bytes);
                    int secs = totp::seconds_remaining_now();
                    ImGui::SameLine();
                    ImGui::TextColored(colors::Green, "%s (%ds)", code.c_str(), secs);
                } else {
                    ImGui::SameLine();
                    ImGui::TextColored(colors::Red, "Invalid");
                }
            }
        }

        // Password age indicator (edit mode, Password type, when flagged as aging)
        if (g_cred_modal.IsEdit() && ct == CredType::Password &&
            g_shell.sec_aging_ids.count(g_cred_modal.edit_id))
        {
            int64_t pw_set_at = g_cred_modal.buf.created_at_ms;
            if (!g_cred_modal.buf.password_history.empty() &&
                g_cred_modal.buf.password_history[0].changed_at_ms > 0)
                pw_set_at = g_cred_modal.buf.password_history[0].changed_at_ms;

            if (pw_set_at > 0) {
                int64_t age_days = (helpers::now_unix_ms() - pw_set_at) / (24LL * 3600 * 1000);
                char age_label[128];
                snprintf(age_label, sizeof(age_label),
                    ICON_MDI_CLOCK_ALERT " Password unchanged for %lld day%s",
                    (long long)age_days, age_days == 1 ? "" : "s");
                ImGui::Dummy(ImVec2(0, 2));
                ImGui::PushStyleColor(ImGuiCol_Text, colors::StatusAging);
                ImGui::TextUnformatted(age_label);
                ImGui::PopStyleColor();
            }
        }

        ImGui::Dummy(ImVec2(0, 6));
        ui::CheckboxBg("Pinned", &g_cred_modal.is_pinned);
        ImGui::SameLine(0, 16);
        ui::CheckboxBg("Favorite", &g_cred_modal.is_favorite);
        ImGui::SameLine(0, 16);
        ui::CheckboxBg("Timed", &g_cred_modal.is_timed);

        if (g_cred_modal.is_timed)
        {
            static const char* timer_duration_labels[] = { "30 min", "1 hour", "6 hours", "24 hours", "7 days", "30 days", "90 days" };
            static const char* expiry_action_labels[]  = { "Auto-trash", "Flag only" };

            ImGui::Dummy(ImVec2(0, 2));
            ImGui::TextDisabled("Expires after");
            ImGui::SameLine(0, 8);
            ui::AnimatedComboDot("##timer_dur",
                timer_duration_labels[g_cred_modal.duration_idx],
                timer_duration_labels, IM_ARRAYSIZE(timer_duration_labels),
                &g_cred_modal.duration_idx, 100.0f, 28.0f, true);
            ImGui::SameLine(0, 16);
            ImGui::TextDisabled("then");
            ImGui::SameLine(0, 8);
            ui::AnimatedComboDot("##timer_act",
                expiry_action_labels[g_cred_modal.expiry_action_idx],
                expiry_action_labels, IM_ARRAYSIZE(expiry_action_labels),
                &g_cred_modal.expiry_action_idx, 110.0f, 28.0f, true);
        }

        // Validation (per type)
        bool can_submit = false;
        switch (ct)
        {
        case CredType::Password:
            can_submit = (!g_cred_modal.buf.title.empty() ||
                          !g_cred_modal.buf.email.empty() ||
                          !g_cred_modal.buf.user.empty()) &&
                         !g_cred_modal.password_buf.empty();
            break;
        case CredType::CreditCard:
            can_submit = !g_cred_modal.buf.title.empty() &&
                         !g_cred_modal.buf.card_number.empty();
            break;
        case CredType::Identity:
            can_submit = !g_cred_modal.buf.title.empty() &&
                         !g_cred_modal.buf.id_number.empty();
            break;
        case CredType::SecureNote:
            can_submit = !g_cred_modal.buf.title.empty();
            break;
        }

        // Submit button - same line as checkboxes, right flushed
        const float btnW = 80.0f;
        const float btnH = 32.0f;
        ImGui::SameLine(ImGui::GetWindowContentRegionMax().x - btnW);

        ImGui::BeginDisabled(!can_submit);
        const char* submitLabel = g_cred_modal.IsAdd() ? ICON_MDI_PLUS " Add" : ICON_MDI_CONTENT_SAVE " Save";
        bool submit_clicked = ui::StyledButtonLight("##modal_submit", submitLabel, ImVec2(btnW, btnH));

        if ((submit_clicked || submit_shortcut) && can_submit)
        {
            v.push_undo();

            if (g_cred_modal.IsAdd())
            {
                Credential c = g_cred_modal.buf;
                // Strip formatting from masked fields
                c.card_number   = ui::StripNonDigits(c.card_number);
                c.card_expiry   = ui::StripNonDigits(c.card_expiry);
                c.date_of_birth = ui::StripNonDigits(c.date_of_birth);
                c.expiry_date   = ui::StripNonDigits(c.expiry_date);
                c.phone         = ui::StripNonDigits(c.phone);
                c.type = g_cred_modal.selected_type;
                c.password = g_cred_modal.password_buf;
                c.totp_secret = g_cred_modal.totp_secret_buf;
                c.is_pinned = g_cred_modal.is_pinned;
                c.is_favorite = g_cred_modal.is_favorite;

                // Per-Credential timer
                {
                    if (g_cred_modal.is_timed) {
                        int64_t now = helpers::now_unix_ms();
                        c.expires_at_ms = now + timer_dur_ms[g_cred_modal.duration_idx];
                        c.expiry_action = g_cred_modal.expiry_action_idx;
                    } else {
                        c.expires_at_ms = 0;
                        c.expiry_action = 0;
                    }
                }

                // Assign next available ID
                int max_id = 0;
                for (const auto& cr : v.creds)
                    if (cr.id > max_id) max_id = cr.id;
                c.id = max_id + 1;

                const int64_t now_ms = helpers::now_unix_ms();
                if (c.created_at_ms == 0)
                    c.created_at_ms = now_ms;
                c.updated_at_ms = now_ms;

                v.creds.push_back(c);
                v.set_status("Added.", false);
                ui::ShowToast("Credential added", ui::ToastType::Success);
            }
            else
            {
                for (auto& c : v.creds)
                {
                    if (c.id == g_cred_modal.edit_id)
                    {
                        // Record password history if password actually changed
                        if (!c.password.empty() && c.password != g_cred_modal.password_buf) {
                            Credential::PasswordHistoryEntry entry;
                            entry.password = c.password;
                            entry.changed_at_ms = helpers::now_unix_ms();
                            c.password_history.insert(c.password_history.begin(), std::move(entry));
                            if (c.password_history.size() > 5)
                                c.password_history.resize(5);
                        }

                        c.title = g_cred_modal.buf.title;
                        c.email = g_cred_modal.buf.email;
                        c.user = g_cred_modal.buf.user;
                        c.password = g_cred_modal.password_buf;
                        c.website = g_cred_modal.buf.website;
                        c.group = g_cred_modal.buf.group;
                        c.notes = g_cred_modal.buf.notes;
                        c.tags = g_cred_modal.buf.tags;
                        c.totp_secret = g_cred_modal.totp_secret_buf;
                        c.is_pinned = g_cred_modal.is_pinned;
                        c.is_favorite = g_cred_modal.is_favorite;
                        // Per-Credential timer
                        if (g_cred_modal.is_timed) {
                            c.expires_at_ms = helpers::now_unix_ms() + timer_dur_ms[g_cred_modal.duration_idx];
                            c.expiry_action = g_cred_modal.expiry_action_idx;
                        } else {
                            c.expires_at_ms = 0;
                            c.expiry_action = 0;
                        }
                        // Type is immutable after creation (c.type unchanged)
                        c.card_number     = ui::StripNonDigits(g_cred_modal.buf.card_number);
                        c.card_expiry     = ui::StripNonDigits(g_cred_modal.buf.card_expiry);
                        c.card_cvv        = g_cred_modal.buf.card_cvv;
                        c.card_brand      = g_cred_modal.buf.card_brand;
                        c.cardholder_name = g_cred_modal.buf.cardholder_name;
                        c.card_address    = g_cred_modal.buf.card_address;
                        c.card_city       = g_cred_modal.buf.card_city;
                        c.card_postal_code = g_cred_modal.buf.card_postal_code;
                        c.full_name     = g_cred_modal.buf.full_name;
                        c.id_type       = g_cred_modal.buf.id_type;
                        c.id_number     = g_cred_modal.buf.id_number;
                        c.date_of_birth = ui::StripNonDigits(g_cred_modal.buf.date_of_birth);
                        c.expiry_date   = ui::StripNonDigits(g_cred_modal.buf.expiry_date);
                        c.country       = g_cred_modal.buf.country;
                        c.address       = g_cred_modal.buf.address;
                        c.phone         = ui::StripNonDigits(g_cred_modal.buf.phone);
                        c.updated_at_ms = helpers::now_unix_ms();
                        break;
                    }
                }
                v.set_status("Saved.", false);
            }

            VaultMarkChanged(v, "EDIT");
            g_autosave.last_change_time = ImGui::GetTime();
            rebuild_groups(g_shell, v.creds); rebuild_tags(g_shell, v.creds);

            g_cred_modal.Close();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();

        if (ImGui::IsItemHovered() && can_submit)
            ImGui::SetTooltip("Ctrl+Enter");

        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);

    if (!modal_open)
        g_cred_modal.Close();
}


static const char* GetPasswordForRow(int id)
{
    auto& v = ActiveVault();
    for (auto& c : v.creds)
        if (c.id == id)
            return (c.type == CredType::Password) ? c.password.c_str() : "";
    return "";
}

// Forward declaration for auto-lock timer (defined with TickAutoLock)
namespace {
    double g_last_activity_time = 0.0;
    ImVec2 g_last_mouse_pos = ImVec2(0, 0);
}

static bool load_vault_from_disk(const std::string& path, const std::string& password, VaultState& v)
{
    v.clear_status();

    if (path.empty() || password.empty())
    {
        v.set_status("Path + password required.", true);
        return false;
    }

    // Close any previously open vault
    vault_db::close();

    // Open the SQLite database
    if (!vault_db::init(path))
    {
        v.set_status("Failed to open vault database.", true);
        return false;
    }

    // Integrity check — warn but allow access (user may want to salvage data)
    if (!vault_db::quick_integrity_check())
        v.set_status("Warning: database may be corrupted. Consider restoring from backup.", true);

    // Get cached salt
    std::vector<uint8_t> salt = vault_db::get_cached_salt();
    if (salt.empty())
    {
        vault_db::close();
        v.set_status("Vault has no encryption salt (corrupt or wrong format).", true);
        return false;
    }

    // Check stored KDF level (defaults to moderate for legacy vaults)
    bool high_sec = (vault_db::get_meta("kdf_level") == "sensitive");

    // Derive master key from password + salt
    std::vector<uint8_t> master_key = enc::derive_master_key(password, salt, high_sec);
    if (master_key.empty())
    {
        vault_db::close();
        v.set_status("Failed to derive encryption key.", true);
        return false;
    }

    // Load all credentials
    std::vector<Credential> creds = cred_ops::load_all(master_key);

    // If we have credentials in DB but couldn't decrypt any, password is likely wrong
    if (creds.empty() && vault_db::count_credentials() > 0)
    {
        enc::secure_zero(master_key);
        vault_db::close();
        v.set_status("Failed to decrypt credentials (wrong password?).", true);
        return false;
    }

    v.vault_path = path;
    v.master_key = std::move(master_key);
    lock_key(v.master_key);            // pin in physical RAM
    v.session_password = password;     // Keep for restore operations
    lock_string(v.session_password);   // pin in physical RAM
    v.creds = std::move(creds);

    // Auto-purge old trash items
    {
        int days = cfg::get_trash_retention_days();
        if (days > 0) {
            int64_t cutoff = helpers::now_unix_ms() - (int64_t)days * time_ms::DAY;
            vault_db::purge_old_tombstones(cutoff);
        }
    }

    // Check if 2FA is enabled — if so, defer full unlock
    if (twofa_ops::is_enabled())
    {
        v.tofa_pending = true;
        v.set_status("Enter your two-factor authentication code.", false);
        return false;
    }

    VaultMarkSaved(v);

    secure_clear_undo_stack(v.undo_stack);
    v.unlocked = true;

    // Set up re-prompt security
    ui::SetRepromptMasterPassword(password);
    ui::ResetRepromptLockout();

    g_autosave.last_change_time = ImGui::GetTime();
    g_autosave.last_save_time = ImGui::GetTime();
    g_last_activity_time = ImGui::GetTime(); // Reset auto-lock timer

    v.set_status("Vault unlocked.", false);
    return true;
}

static void complete_2fa_unlock(const std::string& code, VaultState& v)
{
    if (!v.tofa_pending) return;

    // Try TOTP first
    bool ok = twofa_ops::verify_totp(code, v.master_key);

    // If TOTP fails, try recovery code
    if (!ok)
        ok = twofa_ops::verify_recovery(code, v.master_key);

    if (!ok)
    {
        v.set_status("Invalid code. Try again.", true);
        return;
    }

    // Success — complete the unlock
    VaultMarkSaved(v);

    secure_clear_undo_stack(v.undo_stack);
    v.unlocked = true;
    v.tofa_pending = false;

    ui::SetRepromptMasterPassword(v.session_password);
    ui::ResetRepromptLockout();

    g_autosave.last_change_time = ImGui::GetTime();
    g_autosave.last_save_time = ImGui::GetTime();
    g_last_activity_time = ImGui::GetTime();

    v.set_status("Vault unlocked.", false);
}

static bool create_new_vault_on_disk(const std::string& path, const std::string& password, VaultState& v)
{
    v.clear_status();

    if (path.empty() || password.empty())
    {
        v.set_status("Path + password required.", true);
        return false;
    }

    // Close any previously open vault
    vault_db::close();

    // Create new SQLite database
    if (!vault_db::init(path))
    {
        v.set_status("Failed to create vault database.", true);
        return false;
    }

    // Generate new salt
    std::vector<uint8_t> salt = enc::generate_salt();

    if (!vault_db::set_cached_salt(salt))
    {
        vault_db::close();
        v.set_status("Failed to store encryption salt.", true);
        return false;
    }

    // Derive master key from password + salt
    bool high_sec = cfg::get_high_security_kdf();
    std::vector<uint8_t> master_key = enc::derive_master_key(password, salt, high_sec);
    if (master_key.empty())
    {
        vault_db::close();
        v.set_status("Failed to derive encryption key.", true);
        return false;
    }

    // Store KDF level in vault for future reference
    vault_db::set_meta("kdf_level", high_sec ? "sensitive" : "moderate");

    v.vault_path = path;
    v.master_key = std::move(master_key);
    lock_key(v.master_key);            // pin in physical RAM
    v.session_password = password;     // Keep for restore operations
    lock_string(v.session_password);   // pin in physical RAM
    v.creds.clear();

    VaultMarkSaved(v);

    secure_clear_undo_stack(v.undo_stack);
    v.unlocked = true;

    // Set up re-prompt security
    ui::SetRepromptMasterPassword(password);
    ui::ResetRepromptLockout();

    g_autosave.last_change_time = ImGui::GetTime();
    g_autosave.last_save_time = ImGui::GetTime();
    g_last_activity_time = ImGui::GetTime(); // Reset auto-lock timer

    // Generate recovery key
    {
        // 32 random bytes
        std::vector<uint8_t> recovery_raw(32);
        randombytes_buf(recovery_raw.data(), 32);

        // Derive encryption key from recovery bytes via BLAKE2b
        std::vector<uint8_t> rec_enc_key(32);
        crypto_generichash(rec_enc_key.data(), 32,
                           recovery_raw.data(), 32, nullptr, 0);

        // Encrypt master key (hex-encoded) with recovery encryption key
        std::string mk_hex = enc::bytes_to_hex(v.master_key);
        auto blob = enc::encrypt_credential(mk_hex, rec_enc_key, "vault-recovery");
        enc::secure_zero(mk_hex);
        enc::secure_zero(rec_enc_key);

        // Store encrypted blob
        vault_db::set_recovery_blob(blob, helpers::now_unix_ms());

        // Format recovery key for display: xxxx-xxxx-... (16 groups of 4 hex)
        std::string hex = enc::bytes_to_hex(recovery_raw);
        enc::secure_zero(recovery_raw);
        std::string display;
        for (size_t i = 0; i < hex.size(); i++) {
            if (i > 0 && i % 4 == 0) display += '-';
            display += hex[i];
        }
        enc::secure_zero(hex);

        // Signal UI to show modal
        g_shell.recovery_key_display = std::move(display);
        g_shell.recovery_key_modal_open = true;
    }

    v.set_status("New vault created.", false);
    return true;
}

static bool save_vault_to_disk(VaultState& v)
{
    v.clear_status();

    if (!v.unlocked)
    {
        v.set_status("Vault is locked.", true);
        return false;
    }

    if (v.vault_path.empty() || v.master_key.empty())
    {
        v.set_status("Missing vault path or encryption key.", true);
        return false;
    }

    // Ensure the database is open
    if (!vault_db::is_open())
    {
        if (!vault_db::init(v.vault_path))
        {
            v.set_status("Failed to open vault database.", true);
            return false;
        }
    }

    // Sync in-memory credentials to SQLite
    // Strategy: Clear and re-import all credentials
    // This handles adds, updates, and deletes uniformly

    vault_db::begin_transaction();

    // Get all existing non-deleted UUIDs from database
    auto existing_rows = vault_db::get_all_credentials();
    std::unordered_set<std::string> existing_uuids;
    for (const auto& row : existing_rows)
        existing_uuids.insert(row.uuid);

    // Collect soft-deleted UUIDs (for undo recovery detection)
    auto deleted_rows = vault_db::get_deleted_credentials();
    std::unordered_set<std::string> soft_deleted_uuids;
    for (const auto& row : deleted_rows)
        soft_deleted_uuids.insert(row.uuid);

    // Track which UUIDs are still in memory
    std::unordered_set<std::string> memory_uuids;

    // Update or insert each in-memory Credential
    bool save_ok = true;
    for (auto& c : v.creds)
    {
        if (c.uuid.empty())
        {
            // New Credential - add it
            std::string new_uuid = cred_ops::add(c, v.master_key);
            if (!new_uuid.empty())
            {
                c.uuid = new_uuid;
                memory_uuids.insert(new_uuid);
            }
            else
            {
                save_ok = false;
                break;
            }
        }
        else
        {
            // If Credential was soft-deleted but restored via undo, restore in DB
            if (soft_deleted_uuids.count(c.uuid))
                vault_db::restore_credential(c.uuid);

            // Existing Credential - re-encrypt and store (preserve timestamps)
            memory_uuids.insert(c.uuid);
            {
                auto blob = cred_ops::encrypt_cred_public(c, v.master_key, c.uuid);
                int64_t ts = (c.updated_at_ms != 0) ? c.updated_at_ms : helpers::now_unix_ms();
                if (blob.empty() || !vault_db::update_credential(c.uuid, blob, ts))
                {
                    save_ok = false;
                    break;
                }
            }
        }
    }

    if (!save_ok)
    {
        vault_db::rollback_transaction();
        v.set_status("Save failed: encryption or database error.", true);
        ui::ShowToast("Save failed", ui::ToastType::Error);
        return false;
    }

    // Hard delete only true orphans (not in memory AND not soft-deleted)
    for (const auto& uuid : existing_uuids)
    {
        if (memory_uuids.find(uuid) == memory_uuids.end())
        {
            vault_db::hard_delete_credential(uuid);
        }
    }

    vault_db::commit_transaction();

    VaultMarkSaved(v);
    v.set_status("Saved.", false);
    ui::ShowToast("Vault saved", ui::ToastType::Success);
    return true;
}

static void TickAutosave()
{
    VaultState& v = ActiveVault();
    if (!g_shell.autosave_enabled) return;
    if (g_autosave.suspended) return;
    if (!v.unlocked) return;
    if (!v.is_dirty()) return;

    const double now = ImGui::GetTime();

    // Debounce (~1.2s)
    if (now - g_autosave.last_change_time >= 1.2)
    {
        // Preserve user status messages if they exist (especially errors)
        const bool had_status = !v.status_msg.empty();
        const bool was_error = v.status_is_error;
        const std::string saved_status = v.status_msg;

        save_vault_to_disk(v);
        g_autosave.last_save_time = now;

        // Restore user status if it was non-empty or an error
        if (had_status && (was_error || !saved_status.empty()))
        {
            v.status_msg = saved_status;
            v.status_is_error = was_error;
        }
    }
}

// ============================================================
// Auto-lock: lock vault after inactivity
// ============================================================
static void TickAutoLock()
{
    VaultState& v = ActiveVault();
    if (!v.unlocked) return;

    const int timeout = cfg::get_auto_lock_timeout();
    if (timeout == 0) return; // "Never" - disabled

    const double now = ImGui::GetTime();
    const ImGuiIO& io = ImGui::GetIO();

    // Detect user activity: mouse movement, mouse clicks, or key presses
    bool has_activity = false;

    // Check mouse movement (with small threshold to avoid jitter)
    ImVec2 mouse_delta = ImVec2(io.MousePos.x - g_last_mouse_pos.x, io.MousePos.y - g_last_mouse_pos.y);
    if (fabsf(mouse_delta.x) > 2.0f || fabsf(mouse_delta.y) > 2.0f)
        has_activity = true;
    g_last_mouse_pos = io.MousePos;

    // Check mouse clicks
    for (int i = 0; i < IM_ARRAYSIZE(io.MouseDown); ++i)
        if (io.MouseDown[i]) has_activity = true;

    // Check keyboard input via input queue
    if (io.InputQueueCharacters.Size > 0)
        has_activity = true;

    // Check any key press
    for (int i = ImGuiKey_NamedKey_BEGIN; i < ImGuiKey_NamedKey_END; ++i)
        if (ImGui::IsKeyDown((ImGuiKey)i)) has_activity = true;

    // Reset timer on activity
    if (has_activity)
    {
        g_last_activity_time = now;
        return;
    }

    // Initialize timer if not set
    if (g_last_activity_time == 0.0)
    {
        g_last_activity_time = now;
        return;
    }

    // Check if timeout exceeded
    if (now - g_last_activity_time >= (double)timeout)
    {
        // Lock the vault
        g_shell.footer_close_anyway = true;
        g_last_activity_time = now; // Reset to avoid repeated triggers
        ui::ShowToast("Vault locked due to inactivity", ui::ToastType::Info);
    }
}

// ============================================================
// Credential expiry: auto-trash or flag expired timed credentials
// ============================================================
static void TickCredentialExpiry()
{
    VaultState& v = ActiveVault();
    if (!v.unlocked) return;

    // Throttle: check once per second
    static double s_last_expiry_check = 0.0;
    const double now_sec = ImGui::GetTime();
    if (now_sec - s_last_expiry_check < 1.0) return;
    s_last_expiry_check = now_sec;

    const int64_t now_ms = helpers::now_unix_ms();
    bool changed = false;

    for (auto& c : v.creds) {
        if (c.expires_at_ms <= 0) continue;           // no timer or already flagged
        if (c.is_deleted()) continue;                  // already in trash
        if (now_ms < c.expires_at_ms) continue;        // not yet expired

        if (!changed) v.push_undo();                   // one undo snapshot per tick
        changed = true;

        if (c.expiry_action == 0) {
            // Auto-trash
            c.deleted_at_ms = now_ms;
            c.updated_at_ms = now_ms;
            char toast[256];
            snprintf(toast, sizeof(toast), "Timed credential trashed: %s", c.title.c_str());
            ui::ShowToast(toast, ui::ToastType::Info);
        } else {
            // Flag only: negate expires_at_ms to mark as expired
            c.expires_at_ms = -c.expires_at_ms;
            c.updated_at_ms = now_ms;
            char toast[256];
            snprintf(toast, sizeof(toast), "Credential expired: %s", c.title.c_str());
            ui::ShowToast(toast, ui::ToastType::Error);
        }
    }

    if (changed) {
        VaultMarkChanged(v, "EXPIRY");
        g_autosave.last_change_time = ImGui::GetTime();
    }
}

static void do_undo(VaultState& v)
{
    v.clear_status();

    if (!v.unlocked)
    {
        v.set_status("Vault is locked.", true);
        return;
    }

    if (v.undo_stack.empty())
    {
        v.set_status("Nothing to undo.", true);
        return;
    }

    const std::string snap = v.undo_stack.back();
    v.undo_stack.pop_back();

    v.apply_snapshot_json(snap);

    // Important: reset per-row UI state for THIS vault after big changes
    ui::ForgetVaultRowState(GetActiveVaultKey());

    g_autosave.last_change_time = ImGui::GetTime();

    VaultRecomputeDirty(v);

    v.set_status("Undo applied.", false);
}

static void render_locked_screen()
{
    VaultState& v = ActiveVault();

    // ---- 2FA pending screen ----
    if (v.tofa_pending)
    {
        static std::string s_tofa_code;

        render::BeginCenteredColumn("##tofa_col", 400.0f);
        ImGui::Dummy(ImVec2(0, 80));
        render::BeginSoftContainer(20.0f, 10.0f);

        ImGui::Dummy(ImVec2(0, 8));
        ImGui::PushFont(render::FontLarge);
        ImGui::TextUnformatted("Two-Factor Authentication");
        ImGui::PopFont();

        ImGui::Dummy(ImVec2(0, 4));
        ImGui::TextWrapped("Enter the 6-digit code from your authenticator app, or a recovery code.");
        ImGui::Dummy(ImVec2(0, 8));

        ImGui::SetNextItemWidth(-1);
        bool enter = ui::InputTextString(
            "##tofa_input", &s_tofa_code,
            ImGuiInputTextFlags_EnterReturnsTrue
        );

        ImGui::Dummy(ImVec2(0, 6));

        bool canVerify = !s_tofa_code.empty();
        float btnW = (ImGui::GetContentRegionAvail().x - ImGui::GetStyle().ItemSpacing.x) * 0.5f;

        if (!canVerify) ImGui::BeginDisabled();
        bool verify = ui::StyledButton("##tofa_verify", "Verify", ImVec2(btnW, 34.0f));
        if (!canVerify) ImGui::EndDisabled();

        ImGui::SameLine();

        if (ui::StyledButton("##tofa_cancel", "Cancel", ImVec2(btnW, 34.0f)))
        {
            s_tofa_code.clear();
            vault_db::close();
            v.clear_sensitive();
            secure_clear_credentials(v.creds);
            secure_clear_undo_stack(v.undo_stack);
            v.vault_path.clear();
        }

        if ((enter && canVerify) || verify)
        {
            complete_2fa_unlock(s_tofa_code, v);
            if (v.unlocked)
            {
                s_tofa_code.clear();
                rebuild_groups(g_shell, v.creds); rebuild_tags(g_shell, v.creds);
                ui::ForgetVaultRowState(GetActiveVaultKey());
            }
        }

        // Status
        if (!v.status_msg.empty())
        {
            ImGui::Dummy(ImVec2(0, 4));
            if (v.status_is_error) ImGui::TextColored(colors::Red, "%s", v.status_msg.c_str());
            else                   ImGui::TextColored(colors::Green, "%s", v.status_msg.c_str());
        }

        ImGui::Dummy(ImVec2(0, 8));
        render::EndSoftContainer();
        render::EndCenteredColumn();
        return;
    }

    // per-tab buffers
    static std::vector<std::string> s_pw;
    static std::vector<std::string> s_cached_vaults;
    static int s_selected_vault_idx = -1;
    static std::string s_search_filter;


    if ((int)s_pw.size() < (int)g_tabs.size())
        s_pw.resize(g_tabs.size());

    const int ti = ImClamp(g_active_tab, 0, (int)g_tabs.size() - 1);

    // Narrow, centered "picker" column
    render::BeginCenteredColumn("##locked_col", 450.0f);

    // Vertical centering offset (push content down a bit)
    ImGui::Dummy(ImVec2(0, 50));

    // Soft container: lifted surface with subtle shadow
    render::BeginSoftContainer(20.0f, 10.0f);

    // Cache vault list
    if (s_cached_vaults.empty())
        s_cached_vaults = list_vaults_next_to_exe();


    // Auto-select last opened vault (one-shot on first render)
    {
        static bool s_auto_open_done = false;
        if (!s_auto_open_done)
        {
            s_auto_open_done = true;
            if (g_shell.auto_open_vault
                && cfg::_path != "NONE" && !cfg::_path.empty()
                && helpers::file_exists(cfg::_path))
            {
                v.vault_path = cfg::_path;
                for (int i = 0; i < (int)s_cached_vaults.size(); i++)
                {
                    if (s_cached_vaults[i] == cfg::_path)
                    {
                        s_selected_vault_idx = i;
                        break;
                    }
                }
            }
        }
    }

    // ============================================================
    // HEADER: Title on its own line, then Search + icons row
    // ============================================================
    ImGui::Dummy(ImVec2(0, 8));
    {
        char headerBuf[128];
        int total_vaults = (int)s_cached_vaults.size();
        snprintf(headerBuf, sizeof(headerBuf), "Vaults (%d)", total_vaults);
        ImGui::PushFont(render::FontRegular);
        ImGui::TextUnformatted(headerBuf);
        ImGui::PopFont();

        // Search + Add + Refresh row
        const float icon = 34.0f; // match input field height
        const float gap = ImGui::GetStyle().ItemSpacing.x;
        const float reservedRight = (icon * 2.0f) + (gap * 2.0f);

        float avail = ImGui::GetContentRegionAvail().x;
        float searchW = ImMax(140.0f, avail - reservedRight);

        ImGui::SetNextItemWidth(searchW);
        ui::InputTextString("Search next to exe", &s_search_filter);
        ImGui::SameLine();

        // Align buttons with the field box (skip floating label area)
        float labelAreaH = ImGui::GetFontSize() * 0.82f + 2.0f;
        float btnY = ImGui::GetCursorPosY() + labelAreaH;
        ImGui::SetCursorPosY(btnY);

        // Refresh list
        if (ui::IconButtonSquare("refresh_list", ICON_MDI_REFRESH, icon, true))
        {
            s_cached_vaults = list_vaults_next_to_exe();
            s_selected_vault_idx = -1;
        }
        if (ImGui::IsItemHovered()) ui::SetTooltipPadded("Refresh vault list");

        ImGui::SameLine();
        ImGui::SetCursorPosY(btnY);

        // Add vault (create new)
        if (ui::IconButtonSquare("add_tolist", ICON_MDI_FILE_PLUS, icon, true))
        {
            if (s_pw[ti].empty())
            {
                v.set_status("Enter a password to create a new vault.", true);
            }
            else
            {
                // Local vault creation
                v.vault_path = new_db_path();
                create_new_vault_on_disk(v.vault_path, s_pw[ti], v);

                if (v.unlocked)
                {
                    sodium_memzero(s_pw[ti].data(), s_pw[ti].size());
                    s_pw[ti].clear();

                    rebuild_groups(g_shell, v.creds); rebuild_tags(g_shell, v.creds);
                    ui::ForgetVaultRowState(GetActiveVaultKey());
                }

                s_cached_vaults = list_vaults_next_to_exe();
                s_selected_vault_idx = -1;
            }
        }
        if (ImGui::IsItemHovered()) ui::SetTooltipPadded("Create new vault");
    }

    ImGui::Dummy(ImVec2(0, 4)); // tighter than 6/8

    // ============================================================
    // FILTER VAULTS (case-insensitive)
    // ============================================================
    std::vector<int> filtered_indices;
    filtered_indices.reserve(s_cached_vaults.size());

    for (int i = 0; i < (int)s_cached_vaults.size(); i++)
    {
        std::string filename = helpers::Basename(s_cached_vaults[i]);

        if (s_search_filter.empty() ||
            ImStristr(filename.c_str(), nullptr, s_search_filter.c_str(), nullptr))
        {
            filtered_indices.push_back(i);
        }
    }

    // ============================================================
    // COMPACT HEIGHT RULES
    // ============================================================
    const float footerH = 68.0f;     // taller to fit outlined input + label area
    const float statusH = (!v.status_msg.empty()) ? 20.0f : 0.0f;

    // List should NOT expand forever — cap it so screen feels "picker"
    float listAvail = ImGui::GetContentRegionAvail().y - footerH - statusH - 8.0f;
    float listH = ImClamp(listAvail, 160.0f, 215.0f); // <-- compact cap

    // Smaller rows = denser list
    const float rowH = 32.0f;

    // ============================================================
    // VAULT LIST (compact)
    // ============================================================
    ImGui::BeginChild("##vault_list", ImVec2(-1, listH));

    if (filtered_indices.empty())
    {
        float h = ImGui::GetContentRegionAvail().y;
        ImGui::Dummy(ImVec2(0, h * 0.35f));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetColorU32(ImGuiCol_TextDisabled));

        const char* emptyMsg = s_cached_vaults.empty()
            ? "No vaults found next to the exe."
            : "No vaults match search.";

        float msgW = ImGui::CalcTextSize(emptyMsg).x;
        ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - msgW) * 0.5f);
        ImGui::TextUnformatted(emptyMsg);

        if (s_cached_vaults.empty())
        {
            ImGui::Dummy(ImVec2(0, 4));
            const char* hint = "Enter a password and click " ICON_MDI_FILE_PLUS " to create one";
            float hintW = ImGui::CalcTextSize(hint).x;
            ImGui::SetCursorPosX((ImGui::GetContentRegionAvail().x - hintW) * 0.5f);
            ImGui::PushFont(render::FontSmall);
            ImGui::TextUnformatted(hint);
            ImGui::PopFont();
        }

        ImGui::PopStyleColor();
    }
    else
    {
        for (int i = 0; i < (int)filtered_indices.size(); i++)
        {
            int vaultIdx = filtered_indices[i];
            const std::string& vaultPath = s_cached_vaults[vaultIdx];
            std::string filename = helpers::Basename(vaultPath);

            bool is_selected = (s_selected_vault_idx == vaultIdx);

            std::string label = filename;
            // If collisions possible:
            // label += "##"; label += std::to_string(vaultIdx);

            if (ImGui::Selectable2(label.c_str(), is_selected, 0, ImVec2(0, rowH)))
            {
                s_selected_vault_idx = vaultIdx;
                v.vault_path = vaultPath;
            }

            if (ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
                ui::SetTooltipPadded("%s", vaultPath.c_str());

            // Double-click unlock (even if not selected yet)
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
            {
                s_selected_vault_idx = vaultIdx;
                v.vault_path = vaultPath;

                if (!s_pw[ti].empty())
                {
                    load_vault_from_disk(v.vault_path, s_pw[ti], v);
                    if (v.unlocked)
                    {
                        // Clear password from input buffer
                        sodium_memzero(s_pw[ti].data(), s_pw[ti].size());
                        s_pw[ti].clear();

                        // Remember last opened vault
                        cfg::_path = v.vault_path;
                        cfg::update_db_path();

                        rebuild_groups(g_shell, v.creds); rebuild_tags(g_shell, v.creds);
                        ui::ForgetVaultRowState(GetActiveVaultKey());
                    }
                }
                else
                {
                    v.set_status("Enter password to unlock.", true);
                }
            }
        }
    }

    ImGui::EndChild();

    ImGui::Dummy(ImVec2(0, 6)); // tight spacing between list and footer

    // ============================================================
    // FOOTER: Password + Unlock (compact + attached)
    // ============================================================
    {
        bool hasVault = !v.vault_path.empty();
        bool hasPw = !s_pw[ti].empty();
        bool canUnlock = hasVault && hasPw;

        static bool s_show_pw = false;

        const float icon = 34.0f; // match input field height
        const float gap = ImGui::GetStyle().ItemSpacing.x;

        // Keep input tight inside column (reserve space for eye + unlock icons)
        float avail = ImGui::GetContentRegionAvail().x;
        float pwW = ImMax(140.0f, avail - (icon * 2.0f) - (gap * 2.0f));

        ImGui::SetNextItemWidth(pwW);
        bool enterPressed;
        if (s_show_pw)
        {
            enterPressed = ui::InputTextString(
                "Vault Password##vault_pw",
                &s_pw[ti],
                ImGuiInputTextFlags_EnterReturnsTrue
            );
        }
        else
        {
            enterPressed = ui::InputTextPasswordReveal(
                "Vault Password##vault_pw",
                &s_pw[ti],
                ImGuiInputTextFlags_EnterReturnsTrue
            );
        }

        ImGui::SameLine();

        // Align buttons with the field box (skip floating label area)
        float labelAreaH = ImGui::GetFontSize() * 0.82f + 2.0f;
        float btnY = ImGui::GetCursorPosY() + labelAreaH;
        ImGui::SetCursorPosY(btnY);

        if (ui::IconButtonSquare("show_pw", s_show_pw ? ICON_MDI_EYE_OFF : ICON_MDI_EYE, icon, true))
            s_show_pw = !s_show_pw;

        ImGui::SameLine();
        ImGui::SetCursorPosY(btnY);

        if (!canUnlock) ImGui::BeginDisabled();
        bool clickUnlock = ui::IconButtonSquare("unlock_vault", ICON_MDI_LOCK_OPEN, icon, true);
        if (!canUnlock) ImGui::EndDisabled();

        if ((enterPressed && canUnlock) || clickUnlock)
        {
            if (!v.vault_path.empty())
            {
                load_vault_from_disk(v.vault_path, s_pw[ti], v);
                if (v.unlocked)
                {
                    // Clear password from input buffer (no longer needed)
                    sodium_memzero(s_pw[ti].data(), s_pw[ti].size());
                    s_pw[ti].clear();

                    // Remember last opened vault
                    cfg::_path = v.vault_path;
                    cfg::update_db_path();

                    rebuild_groups(g_shell, v.creds); rebuild_tags(g_shell, v.creds);
                    ui::ForgetVaultRowState(GetActiveVaultKey());
                }
            }
            else
            {
                v.set_status("Please select a vault first.", true);
            }
        }

        if (!hasVault && ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
            ui::SetTooltipPadded("Select a vault first.");
        if (hasVault && !hasPw && ImGui::IsItemHovered(ImGuiHoveredFlags_Stationary))
            ImGui::SetTooltip("Enter your password.");
    }

    // ============================================================
    // Recovery key unlock option
    // ============================================================
    {
        static bool s_show_recovery = false;
        static std::string s_recovery_input;

        // Check if vault has a recovery key
        bool vault_has_recovery = false;
        if (!v.vault_path.empty()) {
            if (vault_db::is_open())
                vault_has_recovery = vault_db::has_recovery_key();
            else if (vault_db::exists(v.vault_path) && vault_db::init(v.vault_path)) {
                vault_has_recovery = vault_db::has_recovery_key();
                vault_db::close();
            }
        }

        if (vault_has_recovery)
        {
            ImGui::Dummy(ImVec2(0, 4));

            {
                const char* linkLabel = s_show_recovery ? "Use password instead" : "Use Recovery Key";
                const char* chevron = " " ICON_MDI_CHEVRON_DOWN;
                ImVec2 textSz = ImGui::CalcTextSize(linkLabel);
                ImVec2 chevSz = ImGui::CalcTextSize(chevron);
                ImVec2 totalSz(textSz.x + chevSz.x, textSz.y);

                ImVec2 pos = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton("##recovery_link", totalSz);
                bool clicked = ImGui::IsItemClicked();
                bool hovered = ImGui::IsItemHovered();

                ImU32 col = ImGui::GetColorU32(hovered ? ImGuiCol_Text : ImGuiCol_TextDisabled);
                ImDrawList* dl = ImGui::GetWindowDrawList();
                dl->AddText(pos, col, linkLabel);
                dl->AddText(ImVec2(pos.x + textSz.x, pos.y), col, chevron);

                if (hovered)
                {
                    float y = pos.y + textSz.y + 1.0f;
                    dl->AddLine(ImVec2(pos.x, y), ImVec2(pos.x + textSz.x, y), col, 1.0f);
                }

                if (clicked)
                {
                    s_show_recovery = !s_show_recovery;
                    s_recovery_input.clear();
                }
            }

            if (s_show_recovery)
            {
                ImGui::Dummy(ImVec2(0, 4));
                float btnW = 75.0f;
                float gap = ImGui::GetStyle().ItemSpacing.x;
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - btnW - gap);
                bool enterRec = ui::InputTextString(
                    "Recovery Key##rec_input", &s_recovery_input,
                    ImGuiInputTextFlags_EnterReturnsTrue);

                ImGui::SameLine();
                // Align button with the field box (skip floating label area)
                float labelAreaH = ImGui::GetFontSize() * 0.82f + 2.0f;
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + labelAreaH);

                bool canRecover = !s_recovery_input.empty() && !v.vault_path.empty();
                if (!canRecover) ImGui::BeginDisabled();
                bool clickRecover = ui::StyledButton("##recover_btn", "Recover", ImVec2(btnW, 34.0f));
                if (!canRecover) ImGui::EndDisabled();

                if ((enterRec && canRecover) || clickRecover)
                {
                    // Parse input: strip dashes/spaces, hex-decode
                    std::string clean;
                    for (char ch : s_recovery_input)
                        if (ch != '-' && ch != ' ') clean += ch;

                    std::vector<uint8_t> recovery_raw = enc::hex_to_bytes(clean);
                    enc::secure_zero(clean);

                    if (recovery_raw.size() == 32)
                    {
                        // Open DB if needed
                        if (!vault_db::is_open())
                            vault_db::init(v.vault_path);

                        // Derive encryption key
                        std::vector<uint8_t> rec_enc_key(32);
                        crypto_generichash(rec_enc_key.data(), 32,
                                           recovery_raw.data(), 32, nullptr, 0);
                        enc::secure_zero(recovery_raw);

                        // Decrypt recovery blob
                        auto blob = vault_db::get_recovery_blob();
                        std::string mk_hex = enc::decrypt_credential(blob, rec_enc_key, "vault-recovery");
                        enc::secure_zero(rec_enc_key);

                        if (!mk_hex.empty())
                        {
                            std::vector<uint8_t> master_key = enc::hex_to_bytes(mk_hex);
                            enc::secure_zero(mk_hex);

                            // Load credentials with recovered master key
                            auto creds = cred_ops::load_all(master_key);

                            v.master_key = std::move(master_key);
                            lock_key(v.master_key);            // pin in physical RAM
                            v.session_password.clear(); // no password known
                            v.creds = std::move(creds);
                            v.tofa_pending = false; // recovery bypasses 2FA

                            // Auto-purge trash
                            int days = cfg::get_trash_retention_days();
                            if (days > 0) {
                                int64_t cutoff = helpers::now_unix_ms() - (int64_t)days * time_ms::DAY;
                                vault_db::purge_old_tombstones(cutoff);
                            }

                            VaultMarkSaved(v);
                            secure_clear_undo_stack(v.undo_stack);
                            v.unlocked = true;

                            ui::SetRepromptMasterPassword("");
                            ui::ResetRepromptLockout();

                            g_autosave.last_change_time = ImGui::GetTime();
                            g_autosave.last_save_time = ImGui::GetTime();
                            g_last_activity_time = ImGui::GetTime();

                            rebuild_groups(g_shell, v.creds); rebuild_tags(g_shell, v.creds);
                            ui::ForgetVaultRowState(GetActiveVaultKey());

                            s_recovery_input.clear();
                            s_show_recovery = false;
                            v.set_status("Vault recovered.", false);
                            ui::ShowToast("Vault unlocked via recovery key", ui::ToastType::Success);
                        }
                        else
                        {
                            v.set_status("Invalid recovery key.", true);
                        }
                    }
                    else
                    {
                        enc::secure_zero(recovery_raw);
                        v.set_status("Invalid recovery key format.", true);
                    }
                }
            }
        }
    }

    // ============================================================
    // Status message (tight)
    // ============================================================
    if (!v.status_msg.empty())
    {
        ImGui::Dummy(ImVec2(0, 4));
        if (v.status_is_error) ImGui::TextColored(colors::Red, "%s", v.status_msg.c_str());
        else                   ImGui::TextColored(colors::Green, "%s", v.status_msg.c_str());
    }

    ImGui::Dummy(ImVec2(0, 8));

    // Close soft container
    render::EndSoftContainer();

    render::EndCenteredColumn();
}

static void render_settings_modal()
{
    const float SETTINGS_MODAL_WIDTH = 660.0f;
    const float SETTINGS_MODAL_HEIGHT = 550.0f;
    const ImVec2 SETTINGS_MODAL_PADDING = ImVec2(20.0f, 20.0f);

    // Center and set size constraints
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(SETTINGS_MODAL_WIDTH, SETTINGS_MODAL_HEIGHT), ImGuiCond_Always);

    // Theme-aware modal colors
    const bool dark = g_shell.dark_theme;
    const ImU32 popupBg = dark ? theme::ModalBg.dark : theme::ModalBg.light;
    const ImU32 dimBg = colors::DimOverlayLight;

    // Styling
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, SETTINGS_MODAL_PADDING);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8, 6));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, popupBg);
    ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, dimBg);

    if (ImGui::BeginPopupModal("Settings###settings_modal", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
    {
        ui::RenderSettingsPage(g_shell);
        ImGui::EndPopup();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(3);
}

// application.cpp
static void render_unlocked_screen()
{
    // Ensure at least one tab
    ActiveTab();

    // Sync shell top tabs from vault tabs (labels + active index)
    SyncShellTabsFromVaultTabs();

    // --- Center window once ---
    RECT screen_rect;
    GetWindowRect(GetDesktopWindow(), &screen_rect);

    const float x = float(screen_rect.right - WINDOW_WIDTH) * 0.5f;
    const float y = float(screen_rect.bottom - WINDOW_HEIGHT) * 0.5f;

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(WINDOW_WIDTH, WINDOW_HEIGHT), ImGuiCond_Always);

    // ============================================================
    // Begin Shell
    // ============================================================
    ui::BeginShell(g_shell, "Password Manager");

    // Store previous active tab BEFORE syncing
    const int prev_active_tab = g_active_tab;

    // If user switched top DB tab in the shell, update active tab index
    ApplyShellActiveTab();

    // If we switched DB, reset close confirm state IMMEDIATELY
    if (g_active_tab != prev_active_tab)
    {
        g_shell.footer_close_confirming = false;
        g_shell.footer_close_anyway = false;
        g_shell.footer_close_cancel = false;
    }

    // Now bind the correct active vault after tab switching
    VaultState& v = ActiveVault();
    const uint32_t activeVaultKey = GetActiveVaultKey();

    // Security center stats (compute on Settings and Unlocked screens)
    if ((g_shell.active_screen == ui::Screen::Settings ||
         g_shell.active_screen == ui::Screen::Unlocked) && v.unlocked)
    {
        // Reused: password frequency map
        std::unordered_map<std::string, int> pw_freq;
        for (const auto& c : v.creds) {
            if (!c.is_deleted() && c.type == CredType::Password && !c.password.empty())
                pw_freq[c.password]++;
        }
        int reused = 0;
        g_shell.sec_reused_ids.clear();
        for (const auto& c : v.creds) {
            if (!c.is_deleted() && c.type == CredType::Password && !c.password.empty() && pw_freq[c.password] > 1) {
                reused++;
                g_shell.sec_reused_ids.insert(c.id);
            }
        }

        // Weak: score <= 1
        int weak = 0;
        g_shell.sec_weak_ids.clear();
        for (const auto& c : v.creds) {
            if (!c.is_deleted() && c.type == CredType::Password && !c.password.empty())
                if (helpers::analyze_password(c.password).score <= 1) {
                    weak++;
                    g_shell.sec_weak_ids.insert(c.id);
                }
        }

        g_shell.sec_reused_count  = reused;
        g_shell.sec_weak_count    = weak;

        // Aging: password not changed in more than N days
        int aging = 0;
        g_shell.sec_aging_ids.clear();
        {
            int64_t now_ms = helpers::now_unix_ms();
            int64_t max_age_ms = (int64_t)g_shell.password_max_age_days * 24 * 3600 * 1000LL;
            for (const auto& c : v.creds) {
                if (!c.is_deleted() && c.type == CredType::Password && !c.password.empty()) {
                    int64_t pw_set_at = c.created_at_ms;
                    if (!c.password_history.empty() && c.password_history[0].changed_at_ms > 0)
                        pw_set_at = c.password_history[0].changed_at_ms;

                    if (pw_set_at > 0 && (now_ms - pw_set_at) > max_age_ms) {
                        aging++;
                        g_shell.sec_aging_ids.insert(c.id);
                    }
                }
            }
        }
        g_shell.sec_aging_count = aging;

        // Consume breach check results from background thread (acquire pairs with release in thread)
        if (g_shell.sec_breach_done.load(std::memory_order_acquire)) {
            g_shell.sec_exposed_ids   = std::move(g_shell.sec_exposed_ids_staging);
            g_shell.sec_exposed_count = g_shell.sec_exposed_count_staging;
            g_shell.sec_breach_error  = std::move(g_shell.sec_breach_error_staging);
            g_shell.sec_breach_done.store(false, std::memory_order_relaxed);
        }

        // Breach check: triggered by user "Check Now" button.
        // Offline-first: only contacts api.pwnedpasswords.com if the user opted in.
        if (g_shell.sec_breach_trigger && !g_shell.sec_breach_checking) {
            g_shell.sec_breach_trigger = false;
            if (cfg::get_online_breach_check())
                CheckBreachedPasswords(g_shell, v.creds);
            else
                g_shell.sec_breach_error = "Online breach check is disabled. Enable it in Settings > Security > Network & Privacy.";
        }
    }

    // Screen transition logic:
    // - Lock/unlock transitions based on vault state
    if (!v.unlocked &&
        g_shell.active_screen != ui::Screen::Locked)
    {
        g_shell.settings_modal_open = false;
        ResetScreen(g_shell, ui::Screen::Locked);
    }
    else if (v.unlocked && g_shell.active_screen == ui::Screen::Locked)
    {
        ResetScreen(g_shell, ui::Screen::Unlocked);
    }

    // ============================================================
    // FIXED HEADER: DrawListControlsRow (not scrollable)
    // ============================================================
    if (g_shell.active_screen != ui::Screen::Locked &&
        g_shell.active_screen != ui::Screen::Settings)
    {
        ui::BeginShellHeader();
        ui::DrawListControlsRow(g_shell, activeVaultKey);
        ui::EndShellHeader();
        //ImGui::Dummy(ImVec2(0, 4));
    }

    // ============================================================
    // SCROLLABLE LIST: Add panel + accordion list
    // (Skip if settings page is open)
    // ============================================================

    const bool render_scroll = (g_shell.active_screen != ui::Screen::Settings);
    if (render_scroll)
        ui::BeginShellScroll();

    // ============================================================
    // Footer status + dirty
    // ============================================================
    g_shell.dirty = v.is_dirty();
    g_shell.can_undo = !v.undo_stack.empty();

    g_shell.footer_status_text = v.status_msg.empty()
        ? (v.unlocked ? "Vault unlocked." : "Locked — enter password.")
        : v.status_msg;

    g_shell.footer_status_is_error = v.status_is_error;

    auto stamp_saved_status = [&]()
        {
            // Optional: show local time without seconds if you want
            // simplest: reuse now_iso8601_local() and trim
            std::string t = helpers::now_iso8601_local();
            // "YYYY-MM-DDTHH:MM:SS-08:00" -> "YYYY-MM-DD HH:MM"
            if (t.size() >= 16) {
                t[10] = ' ';
                t = t.substr(0, 16);
            }
            v.set_status("Saved • " + t, false);
        };

    // ============================================================
    // Ctrl+S saves active vault (BLOCKED in read-only)
    // ============================================================
    if (!g_shell.read_only && ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_S, false))
    {
        save_vault_to_disk(v);
        g_autosave.last_save_time = ImGui::GetTime();
        stamp_saved_status();
    }

    // ============================================================
    // Footer backups list for active vault
    // ============================================================
    static std::string s_last_backup_vault;

    auto rebuild_footer_backups = [&]()
        {
            g_shell.footer_backups.clear();
            g_shell.footer_backup_paths.clear();
            g_shell.footer_backup_meta.clear();

            const auto paths = helpers::ListBackupsForVault(v.vault_path);
            g_shell.footer_backup_paths = paths;

            const int n = (int)g_shell.footer_backup_paths.size();
            g_shell.footer_backups.reserve(n);
            g_shell.footer_backup_meta.resize(n);

            std::string vaultBase = helpers::Basename(v.vault_path);
            if (auto dot = vaultBase.find_last_of('.'); dot != std::string::npos)
                vaultBase = vaultBase.substr(0, dot);

            const std::string stripPrefix = vaultBase + "_";

            for (int i = 0; i < n; ++i)
            {
                const std::string fn = helpers::Basename(g_shell.footer_backup_paths[i]);

                std::string label = fn;
                if (label.rfind(stripPrefix, 0) == 0)
                    label = label.substr(stripPrefix.size());

                g_shell.footer_backups.push_back(label);

                uint64_t bytes = 0;
                std::string t;
                if (!helpers::GetFileInfo(g_shell.footer_backup_paths[i], bytes, t))
                {
                    bytes = 0;
                    t.clear();
                }

                g_shell.footer_backup_meta[i].sizeBytes = bytes;
                g_shell.footer_backup_meta[i].localTime = t;
            }

            if (n <= 0) g_shell.footer_backup_index = 0;
            else
            {
                if (g_shell.footer_backup_index < 0 || g_shell.footer_backup_index >= n)
                    g_shell.footer_backup_index = 0;

                if (g_shell.backup_select_newest_on_refresh)
                    g_shell.footer_backup_index = 0;
            }
        };

    auto update_last_backup_label = [&]()
        {
            g_shell.last_backup_label.clear();
            if (g_shell.footer_backup_paths.empty() || g_shell.footer_backup_meta.empty())
                return;

            // newest is index 0 (your ListBackupsForVault sorts newest first)
            const auto& m = g_shell.footer_backup_meta[0];
            if (m.localTime.empty())
                return;

            g_shell.last_backup_label = "Last backup: " + m.localTime + " \u2022 " + helpers::FormatBytes(m.sizeBytes);
        };

    if (g_shell.footer_refresh_backups_clicked || g_backups_dirty || s_last_backup_vault != v.vault_path)
    {
        rebuild_footer_backups();
        update_last_backup_label();

        s_last_backup_vault = v.vault_path;
        g_shell.footer_refresh_backups_clicked = false;
        g_backups_dirty = false;
    }

    // ============================================================
    // Apply read-only requested by UI popup
    // ============================================================
    if (g_shell.footer_set_read_only)
    {
        g_shell.footer_set_read_only = false;
        g_shell.read_only = g_shell.footer_set_read_only_value;

        g_shell.footer_status_text = g_shell.read_only ? "Read-only enabled" : "Read-only disabled";
        g_shell.footer_status_is_error = false;

    }

    // ============================================================
    // Create backup now
    // ============================================================
    if (g_shell.footer_create_backup_clicked)
    {
        g_shell.footer_create_backup_clicked = false;

        auto res = helpers::CreateBackupNow(v.vault_path, (size_t)g_shell.backup_keep_count);
        if (res.ok)
        {
            g_shell.backup_select_newest_on_refresh = true;

            v.set_status("Backup created.", false);
            g_backups_dirty = true;
        }
        else
        {
            v.set_status(res.err.empty() ? "Backup failed." : res.err.c_str(), true);
        }
    }

    // ============================================================
    // Security lockout - force vault lock on too many failed re-prompts
    // ============================================================
    if (ui::IsRepromptLockedOut() && v.unlocked)
    {
        g_shell.footer_close_anyway = true; // Trigger vault lock
    }

    // ============================================================
    // Browse for backup file
    // ============================================================
    if (g_shell.footer_browse_backup_clicked)
    {
        g_shell.footer_browse_backup_clicked = false;
        char file[MAX_PATH] = { 0 };
        OPENFILENAMEA ofn{};
        ofn.lStructSize  = sizeof(ofn);
        ofn.hwndOwner    = nullptr;
        ofn.lpstrFile    = file;
        ofn.nMaxFile     = MAX_PATH;
        ofn.lpstrFilter  = "Vault Backup (*.lbdb)\0*.lbdb\0All Files\0*.*\0";
        ofn.nFilterIndex = 1;
        ofn.Flags        = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;
        if (GetOpenFileNameA(&ofn))
            g_shell.footer_browse_backup_path = std::string(file);
    }

    // ============================================================
    // Block restore/save intents in read-only (belt + suspenders)
    // ============================================================
    if (g_shell.footer_restore_clicked && g_shell.read_only)
    {
        g_shell.footer_restore_clicked = false;
        v.set_status("Blocked: Read-only mode.", true);
    }

    if (g_shell.footer_save_clicked && g_shell.read_only)
    {
        g_shell.footer_save_clicked = false;
        v.set_status("Blocked: Read-only mode.", true);
    }

    // ============================================================
    // If read-only, nuke mutating intents so nothing slips through
    // ============================================================
    if (g_shell.read_only)
    {
        g_shell.add_clicked = false;
        g_shell.undo_clicked = false;
        g_shell.bulk_delete_clicked = false;
        g_shell.bulk_pin_clicked = false;
        g_shell.bulk_unpin_clicked = false;
        g_shell.bulk_fav_clicked = false;
        g_shell.bulk_unfav_clicked = false;
        g_shell.bulk_set_group_clicked = false;
    }

    // ============================================================
    // Groups rebuild (per-vault tracking)
    // ============================================================
    const uint32_t gh = HashGroupsOnly(v.creds);
    if (gh != v.last_groups_hash)
    {
        rebuild_groups(g_shell, v.creds); rebuild_tags(g_shell, v.creds);
        v.last_groups_hash = gh;
    }

    // ============================================================
    // Add / Undo
    // ============================================================
    if (g_shell.add_clicked && !g_shell.read_only && g_shell.active_screen == ui::Screen::Unlocked)
    {
        g_cred_modal.OpenAdd();
        ImGui::OpenPopup("Add/Edit###cred_modal");
    }

    if (g_shell.undo_clicked)
        do_undo(v);

    if (g_shell.settings_clicked)
    {
        g_shell.settings_modal_open = true;
        g_shell.settings_clicked = false;
    }




    // ============================================================
    // CSV Export
    // ============================================================
    if (g_shell.export_csv_clicked)
    {
        g_shell.export_csv_clicked = false;
        std::string path = PickSaveFilePath_CSV();
        if (!path.empty())
        {
            // Build CSV: quote every field, escape internal quotes by doubling
            auto csv_quote = [](const std::string& s) -> std::string {
                std::string out = "\"";
                for (char c : s) {
                    if (c == '"') out += "\"\"";
                    else out += c;
                }
                out += '"';
                return out;
            };

            std::string csv;
            csv += "Type,Title,Username,Password,Email,URL,Group,Notes,Favorite,Pinned,"
                   "CardholderName,CardNumber,CardExpiry,CardCVV,CardBrand,"
                   "CardAddress,CardCity,CardPostalCode,"
                   "FullName,IDType,IDNumber,DateOfBirth,ExpiryDate,Country,Address,Phone,"
                   "Created,Modified,ExpiresAt\n";

            int count = 0;
            for (const auto& c : v.creds) {
                if (c.is_deleted()) continue;
                csv += csv_quote(CredTypeLabel(c.type)) + ','
                     + csv_quote(c.title) + ','
                     + csv_quote(c.user) + ','
                     + csv_quote(c.password) + ','
                     + csv_quote(c.email) + ','
                     + csv_quote(c.website) + ','
                     + csv_quote(c.group) + ','
                     + csv_quote(c.notes) + ','
                     + (c.is_favorite ? "True" : "False") + ','
                     + (c.is_pinned ? "True" : "False") + ','
                     + csv_quote(c.cardholder_name) + ','
                     + csv_quote(c.card_number) + ','
                     + csv_quote(c.card_expiry) + ','
                     + csv_quote(c.card_cvv) + ','
                     + csv_quote(c.card_brand) + ','
                     + csv_quote(c.card_address) + ','
                     + csv_quote(c.card_city) + ','
                     + csv_quote(c.card_postal_code) + ','
                     + csv_quote(c.full_name) + ','
                     + csv_quote(c.id_type) + ','
                     + csv_quote(c.id_number) + ','
                     + csv_quote(c.date_of_birth) + ','
                     + csv_quote(c.expiry_date) + ','
                     + csv_quote(c.country) + ','
                     + csv_quote(c.address) + ','
                     + csv_quote(c.phone) + ','
                     + csv_quote(helpers::unix_ms_to_iso8601(c.created_at_ms)) + ','
                     + csv_quote(helpers::unix_ms_to_iso8601(c.updated_at_ms)) + ','
                     + (c.expires_at_ms > 0 ? csv_quote(helpers::unix_ms_to_iso8601(c.expires_at_ms)) : "") + '\n';
                count++;
            }

            helpers::str_to_file(path, csv);
            sodium_memzero(csv.data(), csv.size());

            char msg[128];
            snprintf(msg, sizeof(msg), "Exported %d credentials to CSV", count);
            ui::ShowToast(msg, ui::ToastType::Success);
        }
    }

    // ============================================================
    // PWM Export / Import
    // ============================================================
    {
        static bool    s_pwm_export_modal = false;
        static char    s_pwm_export_pw1[256] = {};
        static char    s_pwm_export_pw2[256] = {};
        static std::string s_pwm_export_error;

        static bool    s_pwm_import_modal = false;
        static char    s_pwm_import_pw[256] = {};
        static std::string s_pwm_import_path;
        static std::string s_pwm_import_error;

        // --- Export intent ---
        if (g_shell.export_pwm_clicked)
        {
            g_shell.export_pwm_clicked = false;
            s_pwm_export_modal = true;
            sodium_memzero(s_pwm_export_pw1, sizeof(s_pwm_export_pw1));
            sodium_memzero(s_pwm_export_pw2, sizeof(s_pwm_export_pw2));
            s_pwm_export_error.clear();
            ImGui::OpenPopup("Export PWM###pwm_export_modal");
        }

        // --- Export modal ---
        if (s_pwm_export_modal)
        {
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

            const bool dark = g_shell.dark_theme;
            ImU32 dimBg = colors::DimOverlay;
            ImU32 popupBg = dark ? theme::ModalBg.dark : theme::ModalBg.light;
            ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, dimBg);
            ImGui::PushStyleColor(ImGuiCol_PopupBg, popupBg);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);

            const float modalW = 380.0f;
            ImGui::SetNextWindowSizeConstraints(ImVec2(modalW, 0), ImVec2(modalW, FLT_MAX));

            bool modal_open = true;
            if (ImGui::BeginPopupModal("Export PWM###pwm_export_modal", &modal_open,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
            {
                bool escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
                bool submit_shortcut = ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_Enter);

                ImGui::PushFont(render::FontLarge);
                ImGui::TextUnformatted(ICON_MDI_LOCK "  Export Encrypted Backup");
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0, 12));

                const float fieldW = ImGui::GetContentRegionAvail().x;

                ImGui::TextDisabled("Export Password");
                ImGui::SetNextItemWidth(fieldW);
                ImGui::InputText("##pwm_export_pw1", s_pwm_export_pw1, sizeof(s_pwm_export_pw1), ImGuiInputTextFlags_Password);

                ImGui::Dummy(ImVec2(0, 4));

                ImGui::TextDisabled("Confirm Password");
                ImGui::SetNextItemWidth(fieldW);
                ImGui::InputText("##pwm_export_pw2", s_pwm_export_pw2, sizeof(s_pwm_export_pw2), ImGuiInputTextFlags_Password);

                // Error message
                if (!s_pwm_export_error.empty()) {
                    ImGui::Dummy(ImVec2(0, 8));
                    ImGui::PushStyleColor(ImGuiCol_Text, colors::StatusWeak);
                    ImGui::TextWrapped("%s", s_pwm_export_error.c_str());
                    ImGui::PopStyleColor();
                }

                ImGui::Dummy(ImVec2(0, 12));

                // Buttons
                const float btnW = 100.0f;
                const float btnH = 32.0f;
                float totalBtnW = btnW * 2 + 8.0f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + fieldW - totalBtnW);

                if (ImGui::Button("Cancel", ImVec2(btnW, btnH)) || escape_pressed)
                {
                    sodium_memzero(s_pwm_export_pw1, sizeof(s_pwm_export_pw1));
                    sodium_memzero(s_pwm_export_pw2, sizeof(s_pwm_export_pw2));
                    s_pwm_export_error.clear();
                    s_pwm_export_modal = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine(0, 8.0f);

                bool can_submit = s_pwm_export_pw1[0] != '\0';
                bool pw_match = (strcmp(s_pwm_export_pw1, s_pwm_export_pw2) == 0);

                if (!can_submit) ImGui::BeginDisabled();
                bool do_export = ImGui::Button("Export", ImVec2(btnW, btnH)) || (submit_shortcut && can_submit);
                if (!can_submit) ImGui::EndDisabled();

                if (do_export && can_submit)
                {
                    if (!pw_match) {
                        s_pwm_export_error = "Passwords do not match.";
                    } else {
                        std::string path = PickSaveFilePath_PWM();
                        if (!path.empty())
                        {
                            auto result = pwm_file::export_pwm(v.creds, std::string(s_pwm_export_pw1), path.c_str());
                            sodium_memzero(s_pwm_export_pw1, sizeof(s_pwm_export_pw1));
                            sodium_memzero(s_pwm_export_pw2, sizeof(s_pwm_export_pw2));

                            if (result.ok) {
                                char msg[128];
                                snprintf(msg, sizeof(msg), "Exported %d credentials", result.count);
                                ui::ShowToast(msg, ui::ToastType::Success);
                                s_pwm_export_error.clear();
                                s_pwm_export_modal = false;
                                ImGui::CloseCurrentPopup();
                            } else {
                                s_pwm_export_error = "Export failed: " + result.error;
                            }
                        }
                    }
                }

                ImGui::EndPopup();
            }

            if (!modal_open)
            {
                sodium_memzero(s_pwm_export_pw1, sizeof(s_pwm_export_pw1));
                sodium_memzero(s_pwm_export_pw2, sizeof(s_pwm_export_pw2));
                s_pwm_export_error.clear();
                s_pwm_export_modal = false;
            }

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }

        // --- Import intent ---
        if (g_shell.import_pwm_clicked)
        {
            g_shell.import_pwm_clicked = false;
            s_pwm_import_path = PickOpenFilePath_PWM();
            if (!s_pwm_import_path.empty())
            {
                s_pwm_import_modal = true;
                sodium_memzero(s_pwm_import_pw, sizeof(s_pwm_import_pw));
                s_pwm_import_error.clear();
                ImGui::OpenPopup("Import PWM###pwm_import_modal");
            }
        }

        // --- Import modal ---
        if (s_pwm_import_modal)
        {
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

            const bool dark = g_shell.dark_theme;
            ImU32 dimBg = colors::DimOverlay;
            ImU32 popBg = dark ? theme::ModalBg.dark : theme::ModalBg.light;
            ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, dimBg);
            ImGui::PushStyleColor(ImGuiCol_PopupBg, popBg);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);

            const float modalW = 380.0f;
            ImGui::SetNextWindowSizeConstraints(ImVec2(modalW, 0), ImVec2(modalW, FLT_MAX));

            bool modal_open = true;
            if (ImGui::BeginPopupModal("Import PWM###pwm_import_modal", &modal_open,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
            {
                bool escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
                bool submit_shortcut = ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_Enter);

                ImGui::PushFont(render::FontLarge);
                ImGui::TextUnformatted(ICON_MDI_FILE_IMPORT "  Import Encrypted Backup");
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0, 12));

                const float fieldW = ImGui::GetContentRegionAvail().x;

                ImGui::TextDisabled("Export Password");
                ImGui::SetNextItemWidth(fieldW);
                ImGui::InputText("##pwm_import_pw", s_pwm_import_pw, sizeof(s_pwm_import_pw), ImGuiInputTextFlags_Password);

                // Error message
                if (!s_pwm_import_error.empty()) {
                    ImGui::Dummy(ImVec2(0, 8));
                    ImGui::PushStyleColor(ImGuiCol_Text, colors::StatusWeak);
                    ImGui::TextWrapped("%s", s_pwm_import_error.c_str());
                    ImGui::PopStyleColor();
                }

                ImGui::Dummy(ImVec2(0, 12));

                // Buttons
                const float btnW = 100.0f;
                const float btnH = 32.0f;
                float totalBtnW = btnW * 2 + 8.0f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + fieldW - totalBtnW);

                if (ImGui::Button("Cancel", ImVec2(btnW, btnH)) || escape_pressed)
                {
                    sodium_memzero(s_pwm_import_pw, sizeof(s_pwm_import_pw));
                    s_pwm_import_error.clear();
                    s_pwm_import_path.clear();
                    s_pwm_import_modal = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine(0, 8.0f);

                bool can_submit = s_pwm_import_pw[0] != '\0';

                if (!can_submit) ImGui::BeginDisabled();
                bool do_import = ImGui::Button("Import", ImVec2(btnW, btnH)) || (submit_shortcut && can_submit);
                if (!can_submit) ImGui::EndDisabled();

                if (do_import && can_submit)
                {
                    auto result = pwm_file::import_pwm(std::string(s_pwm_import_pw), s_pwm_import_path);
                    sodium_memzero(s_pwm_import_pw, sizeof(s_pwm_import_pw));

                    if (result.ok)
                    {
                        // Clear UUIDs so import_credentials generates fresh ones
                        for (auto& c : result.creds)
                            c.uuid.clear();

                        if (cred_ops::import_credentials(result.creds, v.master_key))
                        {
                            v.creds = cred_ops::load_all(v.master_key);
                            rebuild_groups(g_shell, v.creds); rebuild_tags(g_shell, v.creds);
                            ui::ForgetVaultRowState(GetActiveVaultKey());
                            VaultMarkChanged(v, "IMPORT");

                            char msg[128];
                            snprintf(msg, sizeof(msg), "Imported %d credentials", result.count);
                            ui::ShowToast(msg, ui::ToastType::Success);

                            s_pwm_import_error.clear();
                            s_pwm_import_path.clear();
                            s_pwm_import_modal = false;
                            ImGui::CloseCurrentPopup();
                        }
                        else
                        {
                            s_pwm_import_error = "Database error during import";
                        }
                    }
                    else
                    {
                        s_pwm_import_error = result.error;
                    }
                }

                ImGui::EndPopup();
            }

            if (!modal_open)
            {
                sodium_memzero(s_pwm_import_pw, sizeof(s_pwm_import_pw));
                s_pwm_import_error.clear();
                s_pwm_import_path.clear();
                s_pwm_import_modal = false;
            }

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }
    }

    // ============================================================
    // KDBX Export
    // ============================================================
    {
        static bool    s_kdbx_export_modal = false;
        static char    s_kdbx_export_pw1[256] = {};
        static char    s_kdbx_export_pw2[256] = {};
        static std::string s_kdbx_export_error;

        // --- Export intent ---
        if (g_shell.export_kdbx_clicked)
        {
            g_shell.export_kdbx_clicked = false;
            s_kdbx_export_modal = true;
            sodium_memzero(s_kdbx_export_pw1, sizeof(s_kdbx_export_pw1));
            sodium_memzero(s_kdbx_export_pw2, sizeof(s_kdbx_export_pw2));
            s_kdbx_export_error.clear();
            ImGui::OpenPopup("Export KDBX###kdbx_export_modal");
        }

        // --- Export modal ---
        if (s_kdbx_export_modal)
        {
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

            const bool dk = g_shell.dark_theme;
            ImU32 dimBg = colors::DimOverlay;
            ImU32 popBg = dk ? theme::ModalBg.dark : theme::ModalBg.light;
            ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, dimBg);
            ImGui::PushStyleColor(ImGuiCol_PopupBg, popBg);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);

            const float modalW = 380.0f;
            ImGui::SetNextWindowSizeConstraints(ImVec2(modalW, 0), ImVec2(modalW, FLT_MAX));

            bool modal_open = true;
            if (ImGui::BeginPopupModal("Export KDBX###kdbx_export_modal", &modal_open,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
            {
                bool escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
                bool submit_shortcut = ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_Enter);

                ImGui::PushFont(render::FontLarge);
                ImGui::TextUnformatted(ICON_MDI_FILE_EXPORT "  Export KeePass Database");
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0, 12));

                const float fieldW = ImGui::GetContentRegionAvail().x;

                ImGui::TextDisabled("Export Password");
                ImGui::SetNextItemWidth(fieldW);
                ImGui::InputText("##kdbx_export_pw1", s_kdbx_export_pw1, sizeof(s_kdbx_export_pw1), ImGuiInputTextFlags_Password);

                ImGui::Dummy(ImVec2(0, 4));

                ImGui::TextDisabled("Confirm Password");
                ImGui::SetNextItemWidth(fieldW);
                ImGui::InputText("##kdbx_export_pw2", s_kdbx_export_pw2, sizeof(s_kdbx_export_pw2), ImGuiInputTextFlags_Password);

                // Error message
                if (!s_kdbx_export_error.empty()) {
                    ImGui::Dummy(ImVec2(0, 8));
                    ImGui::PushStyleColor(ImGuiCol_Text, colors::StatusWeak);
                    ImGui::TextWrapped("%s", s_kdbx_export_error.c_str());
                    ImGui::PopStyleColor();
                }

                ImGui::Dummy(ImVec2(0, 12));

                // Buttons
                const float btnW = 100.0f;
                const float btnH = 32.0f;
                float totalBtnW = btnW * 2 + 8.0f;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + fieldW - totalBtnW);

                if (ImGui::Button("Cancel", ImVec2(btnW, btnH)) || escape_pressed)
                {
                    sodium_memzero(s_kdbx_export_pw1, sizeof(s_kdbx_export_pw1));
                    sodium_memzero(s_kdbx_export_pw2, sizeof(s_kdbx_export_pw2));
                    s_kdbx_export_error.clear();
                    s_kdbx_export_modal = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine(0, 8.0f);

                bool can_submit = s_kdbx_export_pw1[0] != '\0';
                bool pw_match = (strcmp(s_kdbx_export_pw1, s_kdbx_export_pw2) == 0);

                if (!can_submit) ImGui::BeginDisabled();
                bool do_export = ImGui::Button("Export", ImVec2(btnW, btnH)) || (submit_shortcut && can_submit);
                if (!can_submit) ImGui::EndDisabled();

                if (do_export && can_submit)
                {
                    if (!pw_match) {
                        s_kdbx_export_error = "Passwords do not match.";
                    } else {
                        std::string path = PickSaveFilePath_KDBX();
                        if (!path.empty())
                        {
                            auto result = kdbx_export::export_kdbx(v.creds, std::string(s_kdbx_export_pw1), path.c_str());
                            sodium_memzero(s_kdbx_export_pw1, sizeof(s_kdbx_export_pw1));
                            sodium_memzero(s_kdbx_export_pw2, sizeof(s_kdbx_export_pw2));

                            if (result.ok) {
                                char msg[128];
                                snprintf(msg, sizeof(msg), "Exported %d credentials to KDBX", result.count);
                                ui::ShowToast(msg, ui::ToastType::Success);
                                s_kdbx_export_error.clear();
                                s_kdbx_export_modal = false;
                                ImGui::CloseCurrentPopup();
                            } else {
                                s_kdbx_export_error = "Export failed: " + result.error;
                            }
                        }
                    }
                }

                ImGui::EndPopup();
            }

            if (!modal_open)
            {
                sodium_memzero(s_kdbx_export_pw1, sizeof(s_kdbx_export_pw1));
                sodium_memzero(s_kdbx_export_pw2, sizeof(s_kdbx_export_pw2));
                s_kdbx_export_error.clear();
                s_kdbx_export_modal = false;
            }

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }
    }

    // ============================================================
    // CSV Import
    // ============================================================
    {
        static bool        s_csv_import_modal = false;
        static CsvImportResult s_csv_result;
        static std::string s_csv_import_error;

        // --- Import intent ---
        if (g_shell.import_csv_clicked)
        {
            g_shell.import_csv_clicked = false;
            std::string path = PickOpenFilePath_CSV();
            if (!path.empty())
            {
                std::string content = helpers::file_to_str(path);
                if (content.empty())
                {
                    s_csv_import_error = "Could not read file";
                    s_csv_result = {};
                }
                else
                {
                    s_csv_result = parse_csv_import(content);
                    sodium_memzero(content.data(), content.size());
                    s_csv_import_error = s_csv_result.error;
                }
                s_csv_import_modal = true;
                ImGui::OpenPopup("Import CSV###csv_import_modal");
            }
        }

        // --- Import modal ---
        if (s_csv_import_modal)
        {
            ImVec2 center = ImGui::GetMainViewport()->GetCenter();
            ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

            const bool dk = g_shell.dark_theme;
            ImU32 dimBg = colors::DimOverlay;
            ImU32 popBg = dk ? theme::ModalBg.dark : theme::ModalBg.light;
            ImGui::PushStyleColor(ImGuiCol_ModalWindowDimBg, dimBg);
            ImGui::PushStyleColor(ImGuiCol_PopupBg, popBg);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(20, 16));
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f);

            const float modalW = 400.0f;
            ImGui::SetNextWindowSizeConstraints(ImVec2(modalW, 0), ImVec2(modalW, FLT_MAX));

            bool modal_open = true;
            if (ImGui::BeginPopupModal("Import CSV###csv_import_modal", &modal_open,
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove))
            {
                bool escape_pressed = ImGui::IsKeyPressed(ImGuiKey_Escape);
                bool submit_shortcut = ImGui::IsKeyDown(ImGuiMod_Ctrl) && ImGui::IsKeyPressed(ImGuiKey_Enter);

                ImGui::PushFont(render::FontLarge);
                ImGui::TextUnformatted(ICON_MDI_FILE_DELIMITED "  Import CSV");
                ImGui::PopFont();
                ImGui::Dummy(ImVec2(0, 12));

                bool has_error = !s_csv_import_error.empty();
                bool has_creds = !s_csv_result.creds.empty();

                if (has_error)
                {
                    ImGui::PushStyleColor(ImGuiCol_Text, colors::StatusWeak);
                    ImGui::TextWrapped("%s", s_csv_import_error.c_str());
                    ImGui::PopStyleColor();
                }
                else if (has_creds)
                {
                    ImGui::Text("Detected format: %s", CsvFormatName(s_csv_result.format));
                    ImGui::Dummy(ImVec2(0, 4));
                    ImGui::Text("Import %d credential%s?",
                        (int)s_csv_result.creds.size(),
                        s_csv_result.creds.size() == 1 ? "" : "s");
                    if (s_csv_result.skipped > 0)
                    {
                        ImGui::Dummy(ImVec2(0, 4));
                        ImGui::TextDisabled("(%d empty row%s skipped)",
                            s_csv_result.skipped,
                            s_csv_result.skipped == 1 ? "" : "s");
                    }
                }

                ImGui::Dummy(ImVec2(0, 12));

                // Buttons
                const float btnW = 100.0f;
                const float btnH = 32.0f;
                float totalBtnW = btnW * 2 + 8.0f;
                float fieldW = ImGui::GetContentRegionAvail().x;
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + fieldW - totalBtnW);

                if (ImGui::Button("Cancel", ImVec2(btnW, btnH)) || escape_pressed)
                {
                    // Clear parsed Credential passwords
                    for (auto& c : s_csv_result.creds)
                        sodium_memzero(c.password.data(), c.password.size());
                    s_csv_result = {};
                    s_csv_import_error.clear();
                    s_csv_import_modal = false;
                    ImGui::CloseCurrentPopup();
                }

                ImGui::SameLine(0, 8.0f);

                if (!has_creds || has_error) ImGui::BeginDisabled();
                bool do_import = ImGui::Button("Import", ImVec2(btnW, btnH)) || (submit_shortcut && has_creds && !has_error);
                if (!has_creds || has_error) ImGui::EndDisabled();

                if (do_import && has_creds && !has_error)
                {
                    // Clear UUIDs so import_credentials generates fresh ones
                    for (auto& c : s_csv_result.creds)
                        c.uuid.clear();

                    if (cred_ops::import_credentials(s_csv_result.creds, v.master_key))
                    {
                        v.creds = cred_ops::load_all(v.master_key);
                        rebuild_groups(g_shell, v.creds); rebuild_tags(g_shell, v.creds);
                        ui::ForgetVaultRowState(GetActiveVaultKey());
                        VaultMarkChanged(v, "IMPORT");

                        char msg[128];
                        snprintf(msg, sizeof(msg), "Imported %d credentials from %s",
                            (int)s_csv_result.creds.size(), CsvFormatName(s_csv_result.format));
                        ui::ShowToast(msg, ui::ToastType::Success);

                        // Clear passwords
                        for (auto& c : s_csv_result.creds)
                            sodium_memzero(c.password.data(), c.password.size());

                        s_csv_result = {};
                        s_csv_import_error.clear();
                        s_csv_import_modal = false;
                        ImGui::CloseCurrentPopup();
                    }
                    else
                    {
                        s_csv_import_error = "Database error during import";
                    }
                }

                ImGui::EndPopup();
            }

            if (!modal_open)
            {
                for (auto& c : s_csv_result.creds)
                    sodium_memzero(c.password.data(), c.password.size());
                s_csv_result = {};
                s_csv_import_error.clear();
                s_csv_import_modal = false;
            }

            ImGui::PopStyleVar(2);
            ImGui::PopStyleColor(2);
        }
    }

    // Render Credential modal (Add/Edit)
    render_credential_modal(v);

    // Render re-prompt modal (security)
    ui::RenderRepromptModal();

    // Render trash bin modal
    ui::RenderTrashModal(g_shell);

    // Render security center modal
    ui::RenderSecurityCenterModal(g_shell);

    // Render recovery key modal (shown once at vault creation)
    ui::RenderRecoveryKeyModal(g_shell);

    // Render settings modal
    ui::RenderSettingsPage(g_shell);

    // Render on-screen keyboard
    ui::RenderOnScreenKeyboard();

    // If locked, render locked and bail
    if (g_shell.active_screen == ui::Screen::Locked)
    {
        render_locked_screen();
        if (render_scroll)
            ui::EndShellScroll();
        ui::EndShell();
        return;
    }

    ui::AccordionListResult r{};
    r.delete_id = -1;
    r.edit_commit_id = -1;
    const bool render_list = (g_shell.active_screen == ui::Screen::Unlocked);

    if (render_list)
    {
        if (v.creds.empty())
        {
            // ============================================================
            // Empty vault onboarding message
            // ============================================================
            ImVec2 avail   = ImGui::GetContentRegionAvail();
            float  startY  = ImGui::GetCursorPosY();

            const char* icon      = ICON_MDI_SHIELD_HALF_FULL;
            const char* primary   = "Your vault is empty";
            const char* secondary = "Click  " ICON_MDI_ACCOUNT_PLUS "  in the top right to add your first credential";

            // Measure each line with its font
            ImGui::PushFont(render::FontLarge);
            ImVec2 iconSz = ImGui::CalcTextSize(icon);
            ImGui::PopFont();

            ImGui::PushFont(render::FontRegular);
            ImVec2 primarySz = ImGui::CalcTextSize(primary);
            ImGui::PopFont();

            ImGui::PushFont(render::FontSmall);
            ImVec2 secondarySz = ImGui::CalcTextSize(secondary);
            ImGui::PopFont();

            float spacing = 6.0f;
            float totalH  = iconSz.y + spacing + primarySz.y + spacing + secondarySz.y;
            float yOff    = startY + (avail.y - totalH) * 0.4f; // slightly above center

            // Icon
            ImGui::SetCursorPos(ImVec2((avail.x - iconSz.x) * 0.5f, yOff));
            ImGui::PushFont(render::FontLarge);
            ImGui::TextDisabled("%s", icon);
            ImGui::PopFont();

            // Primary text
            yOff += iconSz.y + spacing;
            ImGui::SetCursorPos(ImVec2((avail.x - primarySz.x) * 0.5f, yOff));
            ImGui::PushFont(render::FontRegular);
            ImGui::TextDisabled("%s", primary);
            ImGui::PopFont();

            // Secondary text
            yOff += primarySz.y + spacing;
            ImGui::SetCursorPos(ImVec2((avail.x - secondarySz.x) * 0.5f, yOff));
            ImGui::PushFont(render::FontSmall);
            ImGui::TextDisabled("%s", secondary);
            ImGui::PopFont();
        }
        else
        {
            // ============================================================
            // Build list items
            // ============================================================
            const char* search = ui::GetSearchText();
            const int searchFilter = ui::GetSearchFilter();

            std::vector<ui::AccordionItem> items =
                build_accordion_items(
                    v.creds,
                    v.saved_snapshot,
                    search,
                    searchFilter,
                    g_shell.sort_mode,
                    g_shell.pill_tab_0,
                    g_shell.pill_tab_1,
                    g_shell.selected_groups,
                    g_shell.selected_tags,
                    g_shell.order_key,
                    g_shell.order_dir,
                    g_shell.group_mode
                );

            // Populate sidebar badge counts from UNFILTERED credentials
            {
                int ca = 0, cp = 0, cf = 0, cPw = 0, cCd = 0, cId = 0, cNt = 0;
                std::unordered_map<std::string, int> gc;
                std::unordered_map<std::string, int> tc;
                std::unordered_map<std::string, std::vector<ui::ShellState::SidebarCredItem>> gi;
                for (const auto& c : v.creds)
                {
                    if (c.is_deleted()) continue;
                    ca++;
                    if (c.is_pinned)   cp++;
                    if (c.is_favorite) cf++;
                    switch (c.type) {
                        case CredType::Password:   cPw++; break;
                        case CredType::CreditCard: cCd++; break;
                        case CredType::Identity:   cId++; break;
                        case CredType::SecureNote: cNt++; break;
                    }
                    if (!c.group.empty())
                    {
                        gc[c.group]++;
                        gi[c.group].push_back({c.id, c.title, c.website, c.type, c.uuid});
                    }
                    for (const auto& t : c.tags)
                        if (!t.empty()) tc[t]++;
                }
                g_shell.sb_count_all       = ca;
                g_shell.sb_count_pinned    = cp;
                g_shell.sb_count_favorites = cf;
                g_shell.sb_count_passwords = cPw;
                g_shell.sb_count_cards     = cCd;
                g_shell.sb_count_identity  = cId;
                g_shell.sb_count_notes     = cNt;
                g_shell.sb_group_counts    = std::move(gc);
                g_shell.sb_group_items     = std::move(gi);
                g_shell.sb_tag_counts      = std::move(tc);
            }

            r = ui::RenderAccordionList(
                items,
                activeVaultKey,
                &GetPasswordForRow,
                g_shell.read_only,
                g_shell.view_mode
            );

            if (g_shell.select_all_clicked)
            {
                for (const auto& it : items)
                    if (!it.is_header)
                        ui::SelectRow(activeVaultKey, it.id);
                g_shell.select_all_clicked = false;
            }
        }
    }


    // ============================================================
    // HARD READ-ONLY GATE for commits coming back from UI
    // (this is the part you were missing)
    // ============================================================
    if (g_shell.read_only && render_list)
    {
        // If UI tried to commit anything, ignore it and notify.
        if (r.delete_id != -1 || r.edit_commit_id != -1 || r.edit_open_id != -1 ||
            r.toggle_pin_id != -1 || r.toggle_fav_id != -1)
        {
            v.set_status("Blocked: Read-only mode.", true);
            r.delete_id = -1;
            r.edit_commit_id = -1;
            r.edit_open_id = -1;
            r.toggle_pin_id = -1;
            r.toggle_fav_id = -1;
        }
    }

    // ============================================================
    // Security Center: feed selected Credential into edit pipeline
    // ============================================================
    if (g_shell.sec_center_edit_id != -1)
    {
        r.edit_open_id = g_shell.sec_center_edit_id;
        g_shell.sec_center_edit_id = -1;
    }

    // ============================================================
    // Handle edit request -> open modal
    // ============================================================
    if (render_list && r.edit_open_id != -1)
    {
        for (const auto& c : v.creds)
        {
            if (c.id == r.edit_open_id)
            {
                g_cred_modal.OpenEdit(c, c.password);
                ImGui::OpenPopup("Add/Edit###cred_modal");
                break;
            }
        }
    }


    // ============================================================
    // BULK INTENTS
    // ============================================================
    auto ApplyBulkToSelected = [&](auto&& fn)
        {
            std::vector<uint64_t> keys;
            ui::GetSelectedRowKeys(activeVaultKey, keys);
            if (keys.empty()) return false;

            v.push_undo();

            std::unordered_set<int> ids;
            ids.reserve(keys.size());
            for (uint64_t k : keys)
                ids.insert((int)(k & 0xFFFFFFFFu));

            for (auto& c : v.creds)
                if (ids.count(c.id))
                    fn(c);

            rebuild_groups(g_shell, v.creds); rebuild_tags(g_shell, v.creds);

            VaultMarkChanged(v, "BULKEDIT");
            g_autosave.last_change_time = ImGui::GetTime();
            return true;
        };

    if (g_shell.clear_selection_clicked && render_list)
    {
        ui::ClearSelectionForVault(activeVaultKey);
        g_shell.clear_selection_clicked = false;
    }

    if (g_shell.bulk_delete_clicked && render_list)
    {
        std::vector<uint64_t> keys;
        ui::GetSelectedRowKeys(activeVaultKey, keys);

        if (!keys.empty())
        {
            std::unordered_set<int> ids;
            ids.reserve(keys.size());
            for (uint64_t k : keys)
                ids.insert((int)(k & 0xFFFFFFFFu));

            v.push_undo();
            CreatePreOpBackup(v, "BULKDELETE");

            // Soft-delete each in DB before removing from memory
            for (const auto& c : v.creds) {
                if (ids.count(c.id) > 0 && !c.uuid.empty())
                    cred_ops::remove(c.uuid);
            }

            const size_t before = v.creds.size();
            v.creds.erase(std::remove_if(v.creds.begin(), v.creds.end(),
                [&](const Credential& c) { return ids.count(c.id) > 0; }),
                v.creds.end());

            ui::ClearSelectionForVault(activeVaultKey);
            ui::ForgetVaultRowState(activeVaultKey);

            rebuild_groups(g_shell, v.creds); rebuild_tags(g_shell, v.creds);

            VaultMarkChanged(v, "BULKDELETE");
            g_autosave.last_change_time = ImGui::GetTime();

            const size_t removed = before - v.creds.size();
            v.set_status(("Moved " + std::to_string(removed) + " item(s) to trash.").c_str(), false);
            ui::ShowToast(("Moved " + std::to_string(removed) + " to trash").c_str(), ui::ToastType::Success);
        }

        g_shell.bulk_delete_clicked = false;
    }

    if (g_shell.bulk_pin_clicked && render_list)
    {
        if (ApplyBulkToSelected([](Credential& c) { c.is_pinned = true; }))
            v.set_status("Pinned selected.", false);
        g_shell.bulk_pin_clicked = false;
    }

    if (g_shell.bulk_unpin_clicked && render_list)
    {
        if (ApplyBulkToSelected([](Credential& c) { c.is_pinned = false; }))
            v.set_status("Unpinned selected.", false);
        g_shell.bulk_unpin_clicked = false;
    }

    if (g_shell.bulk_fav_clicked && render_list)
    {
        if (ApplyBulkToSelected([](Credential& c) { c.is_favorite = true; }))
            v.set_status("Favorited selected.", false);
        g_shell.bulk_fav_clicked = false;
    }

    if (g_shell.bulk_unfav_clicked && render_list)
    {
        if (ApplyBulkToSelected([](Credential& c) { c.is_favorite = false; }))
            v.set_status("Unfavorited selected.", false);
        g_shell.bulk_unfav_clicked = false;
    }

    if (g_shell.bulk_set_group_clicked && render_list)
    {
        std::string target = "Filter";
        if (g_shell.bulk_group_index >= 0 && g_shell.bulk_group_index < (int)g_shell.groups.size())
            target = g_shell.groups[g_shell.bulk_group_index];

        // Reject non-group entries (Filter, @-prefixed types, separator)
        if (target == "Filter" || (!target.empty() && target[0] == '@') || target == "---")
        {
            v.set_status("Pick a real group.", true);
        }
        else
        {
            if (ApplyBulkToSelected([&](Credential& c) { c.group = target; }))
                v.set_status("Updated group for selected.", false);
        }

        g_shell.bulk_set_group_clicked = false;
    }

    // Bulk tag add
    if (!g_shell.bulk_tag_to_add.empty() && render_list)
    {
        const std::string tag = g_shell.bulk_tag_to_add;
        if (ApplyBulkToSelected([&](Credential& c) {
            // Add tag if not already present
            bool exists = false;
            for (const auto& t : c.tags)
                if (t == tag) { exists = true; break; }
            if (!exists)
                c.tags.push_back(tag);
        }))
            v.set_status("Added tag to selected.", false);
        g_shell.bulk_tag_to_add.clear();
    }

    // Bulk tag remove
    if (!g_shell.bulk_tag_to_remove.empty() && render_list)
    {
        const std::string tag = g_shell.bulk_tag_to_remove;
        if (ApplyBulkToSelected([&](Credential& c) {
            c.tags.erase(std::remove(c.tags.begin(), c.tags.end(), tag), c.tags.end());
        }))
            v.set_status("Removed tag from selected.", false);
        g_shell.bulk_tag_to_remove.clear();
    }

    // Drag-drop: credential → group
    if (g_shell.drag_drop_cred_id >= 0 && !g_shell.drag_drop_target_group.empty())
    {
        v.push_undo();
        for (auto& c : v.creds)
        {
            if (c.id == g_shell.drag_drop_cred_id)
            {
                c.group = g_shell.drag_drop_target_group;
                c.updated_at_ms = helpers::now_unix_ms();
                cred_ops::update(c.uuid, c, v.master_key);
                break;
            }
        }
        rebuild_groups(g_shell, v.creds); rebuild_tags(g_shell, v.creds);
        VaultMarkChanged(v, "DRAG_GROUP");
        g_autosave.last_change_time = ImGui::GetTime();
        g_shell.drag_drop_cred_id = -1;
        g_shell.drag_drop_target_group.clear();
    }

    // ============================================================
    // Per-row delete/edit commits (now safe due to read-only gate)
    // ============================================================
    if (render_list && r.delete_id != -1)
    {
        CreatePreOpBackup(v, "DELETE");
        v.push_undo();

        // Soft-delete in DB before removing from memory
        for (const auto& c : v.creds) {
            if (c.id == r.delete_id && !c.uuid.empty()) {
                cred_ops::remove(c.uuid);
                break;
            }
        }

        // Remove Credential from UI
        v.creds.erase(
            std::remove_if(v.creds.begin(), v.creds.end(),
                [&](const Credential& c) { return c.id == r.delete_id; }),
            v.creds.end());

        VaultMarkChanged(v, "DELETE");

        ui::ForgetRowState(activeVaultKey, r.delete_id);

        g_autosave.last_change_time = ImGui::GetTime();

        rebuild_groups(g_shell, v.creds); rebuild_tags(g_shell, v.creds);
        v.set_status("Moved to trash.", false);
        ui::ShowToast("Moved to trash", ui::ToastType::Success);
    }

    if (render_list && r.toggle_pin_id != -1)
    {
        for (auto& c : v.creds)
        {
            if (c.id == r.toggle_pin_id)
            {
                v.push_undo();
                c.is_pinned = !c.is_pinned;
                c.updated_at_ms = helpers::now_unix_ms();
                VaultMarkChanged(v, "PIN");
                g_autosave.last_change_time = ImGui::GetTime();
                break;
            }
        }
    }

    if (render_list && r.toggle_fav_id != -1)
    {
        for (auto& c : v.creds)
        {
            if (c.id == r.toggle_fav_id)
            {
                v.push_undo();
                c.is_favorite = !c.is_favorite;
                c.updated_at_ms = helpers::now_unix_ms();
                VaultMarkChanged(v, "FAV");
                g_autosave.last_change_time = ImGui::GetTime();
                break;
            }
        }
    }

    if (render_list && r.edit_commit_id != -1)
    {
        v.push_undo();

        // Update Credential by ID
        for (auto& c : v.creds)
        {
            if (c.id == r.edit_commit_id)
            {
                c.title = r.edited.title;
                c.user = r.edited.user;
                c.email = r.edited.email;
                c.website = r.edited.website;
                c.group = r.edited.group;
                c.notes = r.edited.notes;
                c.tags = r.edited.tags;
                c.is_pinned = r.edited.is_pinned;
                c.is_favorite = r.edited.is_favorite;
                c.expires_at_ms = r.edited.expires_at_ms;
                c.expiry_action = r.edited.expiry_action;
                c.password = r.edited_password;
                // Type-specific fields
                c.card_number     = r.edited.card_number;
                c.card_expiry     = r.edited.card_expiry;
                c.card_cvv        = r.edited.card_cvv;
                c.card_brand      = r.edited.card_brand;
                c.cardholder_name = r.edited.cardholder_name;
                c.card_address    = r.edited.card_address;
                c.card_city       = r.edited.card_city;
                c.card_postal_code = r.edited.card_postal_code;
                c.full_name     = r.edited.full_name;
                c.id_type       = r.edited.id_type;
                c.id_number     = r.edited.id_number;
                c.date_of_birth = r.edited.date_of_birth;
                c.expiry_date   = r.edited.expiry_date;
                c.country       = r.edited.country;
                c.address       = r.edited.address;
                c.phone         = r.edited.phone;
                c.updated_at_ms = helpers::now_unix_ms();
                break;
            }
        }

        VaultMarkChanged(v, "EDIT");
        g_autosave.last_change_time = ImGui::GetTime();

        rebuild_groups(g_shell, v.creds); rebuild_tags(g_shell, v.creds);
        v.set_status("Saved changes (in memory). Ctrl+S to write to disk.", false);
    }


    // ============================================================
    // Autosave tick + Auto-lock tick + end shell
    // ============================================================
    TickAutosave();
    TickAutoLock();
    TickCredentialExpiry();

    if (render_scroll)
        ui::EndShellScroll();
    ui::EndShell();

    // Style editor (floating window)
    ui::RenderAppStyleEditor(g_shell);

    // ============================================================
    // Back navigation
    // ============================================================
    if (g_shell.back_clicked)
    {
        g_shell.back_clicked = false;
        PopScreen(g_shell);
    }

    // ============================================================
    // Handle footer Open/New intents
     // ============================================================
    if (g_shell.footer_open_db_clicked) HandleOpenDB();
    if (g_shell.footer_new_db_clicked)  HandleNewDB();

    if (g_shell.open_db_clicked) HandleOpenDB();
    if (g_shell.new_db_clicked)  HandleNewDB();

    // ============================================================
    // Footer Save (guarded)
    // ============================================================
    if (g_shell.footer_save_clicked && !g_shell.read_only)
    {
        save_vault_to_disk(ActiveVault());
        g_autosave.last_save_time = ImGui::GetTime();
        stamp_saved_status();
    }
    g_shell.footer_save_clicked = false; // consume


    // ============================================================
    // Always on Top toggle
    // ============================================================
    if (g_shell.always_on_top_changed)
    {
        g_shell.always_on_top_changed = false;
        ::SetWindowPos(render::hwnd,
            g_shell.always_on_top ? HWND_TOPMOST : HWND_NOTOPMOST,
            0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }

    // ============================================================
    // System tray events
    // ============================================================
    // Sync minimize_to_tray flag for WndProc
    render::SetMinimizeToTray(g_shell.minimize_to_tray);

    if (render::ConsumeTrayLock())
    {
        // Trigger vault lock (same path as close-anyway)
        g_shell.footer_close_anyway = true;
    }

    if (render::ConsumeTrayQuit())
    {
        render::RemoveTrayIcon();
        ::PostQuitMessage(0);
    }

    // ============================================================
    // Go to locked screen (from settings "Create New Vault")
    // ============================================================
    if (g_shell.goto_locked_clicked)
    {
        g_shell.goto_locked_clicked = false;

        VaultState& vv = ActiveVault();

        // Close current database
        vault_db::close();

        // Clear sensitive data (zero before free)
        vv.clear_sensitive();
        secure_clear_credentials(vv.creds);
        secure_clear_undo_stack(vv.undo_stack);
        vv.vault_path.clear();

        // Clear re-prompt security state
        ui::SetRepromptMasterPassword("");
        ui::ResetRepromptLockout();

        // Reset to locked screen
        ResetScreen(g_shell, ui::Screen::Locked);
    }

    // ============================================================
    // Close clicked / confirm flow
    // ============================================================
    if (g_shell.footer_close_clicked)
    {
        if (ActiveVault().is_dirty())
            g_shell.footer_close_confirming = true;
        else
            g_shell.footer_close_anyway = true;

        g_shell.footer_close_clicked = false; // consume
    }

    if (g_shell.footer_close_anyway)
    {
        g_shell.footer_close_anyway = false;

        // Close all open modals
        g_shell.settings_modal_open = false;
        g_shell.trash_modal_open = false;
        g_shell.sec_center_open = false;
        g_cred_modal.Close();
        ImGui::ClosePopupsOverWindow(ImGui::GetCurrentWindow(), true);

        VaultState& vv = ActiveVault();
        uint32_t vk = GetActiveVaultKey();

        ui::ForgetVaultRowState(vk);

        // Clear re-prompt security state
        ui::SetRepromptMasterPassword("");
        ui::ResetRepromptLockout();

        g_tabs.erase(g_tabs.begin() + g_active_tab);
        if (g_tabs.empty())
            g_tabs.push_back({ "Vault", VaultState{} });

        g_active_tab = ImClamp(g_active_tab, 0, (int)g_tabs.size() - 1);

        SyncShellTabsFromVaultTabs();
        g_shell.active_db = g_active_tab;
    }

    // ============================================================
    // Restore selected backup
    // ============================================================
    if (g_shell.footer_restore_clicked)
    {
        g_shell.footer_restore_clicked = false;

        VaultState& vv = ActiveVault();

        if (vv.is_dirty())
        {
            vv.set_status("Restore blocked: you have unsaved changes. Save or Undo first.", true);
            return;
        }

        const std::string& backup_path = g_shell.footer_browse_backup_path;
        if (!backup_path.empty())
        {

            CreatePreOpBackup(vv, "RESTORE");

            if (!helpers::CopyFileAtomic(backup_path, vv.vault_path))
            {
                vv.set_status("Restore failed (copy error).", true);
            }
            else
            {
                if (!load_vault_from_disk(vv.vault_path, vv.session_password, vv))
                {
                    vv.set_status("Restore applied, but reload failed (password mismatch?).", true);
                }
                else
                {
                    // Integrity check on restored vault
                    if (!vault_db::quick_integrity_check())
                        vv.set_status("Warning: restored database may be corrupted.", true);

                    ui::ForgetVaultRowState(GetActiveVaultKey());
                    rebuild_groups(g_shell, vv.creds); rebuild_tags(g_shell, vv.creds);

                    vv.set_status("Restored backup.", false);
                    g_autosave.last_change_time = ImGui::GetTime();
                    g_autosave.last_save_time = ImGui::GetTime();
                }
            }
        }
    }
}

void render::DisplayProgram()
{
    // One-time initialization
    static bool s_first_frame = true;
    if (s_first_frame)
    {
        s_first_frame = false;
        g_shell.dark_theme = cfg::is_dark_theme();

        // Load custom card colors
        {
            auto cd = cfg::get_card_bg_dark();
            g_shell.card_bg_dark = ImVec4(cd.r, cd.g, cd.b, cd.a);
            auto cl = cfg::get_card_bg_light();
            g_shell.card_bg_light = ImVec4(cl.r, cl.g, cl.b, cl.a);
        }

        // Load extended color customization
        {
            auto load_color = [](ImVec4& dst, cfg::ColorRGBA c) {
                dst = ImVec4(c.r, c.g, c.b, c.a);
            };
            load_color(g_shell.card_header_bg_dark,  cfg::get_card_header_bg_dark());
            load_color(g_shell.card_header_bg_light, cfg::get_card_header_bg_light());
            load_color(g_shell.card_body_bg_dark,    cfg::get_card_body_bg_dark());
            load_color(g_shell.card_body_bg_light,   cfg::get_card_body_bg_light());
            load_color(g_shell.soft_container_dark,   cfg::get_soft_container_dark());
            load_color(g_shell.soft_container_light,  cfg::get_soft_container_light());
            load_color(g_shell.controls_bg_dark,      cfg::get_controls_bg_dark());
            load_color(g_shell.controls_bg_light,     cfg::get_controls_bg_light());
            load_color(g_shell.window_bg_dark,        cfg::get_window_bg_dark());
            load_color(g_shell.window_bg_light,       cfg::get_window_bg_light());
        }

        // Load font scale and privacy mode
        g_shell.font_scale = cfg::get_font_scale();
        ImGui::GetIO().FontGlobalScale = g_shell.font_scale;
        g_shell.privacy_mode = cfg::get_privacy_mode();

        // Load password aging threshold
        g_shell.password_max_age_days = cfg::get_password_max_age_days();
    }

    ui::OskPreFrame(); // Intercept mouse clicks over OSK before widgets see them
    ui::Initialize();
    ActiveTab(); // ensure tab exists

    // Always draw shell; inside it, draw locked or unlocked for ActiveVault()
    render_unlocked_screen(); // (this function now handles locked internally)
}
