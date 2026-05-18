// sync_manager.h
// Cloud synchronization manager for password manager
// Implements push-then-pull sync with per-Credential granularity

#pragma once

#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <mutex>
#include <cstdint>

#include "../credentials/credential.h"  // Credential struct

// Sync status for UI feedback
enum class SyncStatus {
    Idle,
    Connecting,
    Authenticating,
    Pushing,
    Pulling,
    Success,
    Error,
    Offline
};

// Auth result
struct AuthResult {
    bool success = false;
    std::string message;
    std::string access_token;
    std::string username;
    std::vector<uint8_t> encryption_salt;  // Server's salt (if available)
    bool has_server_salt = false;
};

// Sync result
struct SyncResult {
    bool success = false;
    std::string message;
    int pushed = 0;
    int pulled = 0;
    int64_t new_server_rev = 0;
};

// Free-standing HTTP helper (WinHTTP on Windows, cpp-httplib elsewhere)
struct HttpResponse {
    int status_code = 0;
    std::string body;
    bool success = false;
    std::string error;
};

HttpResponse win_http_request(
    const std::string& method,
    const std::string& host,
    int port,
    bool use_ssl,
    const std::string& path,
    const std::string& body = "",
    const std::string& bearer_token = "");

class SyncManager {
public:
    // Callback types
    using StatusCallback = std::function<void(SyncStatus, const std::string&)>;
    using SyncCompleteCallback = std::function<void(const SyncResult&)>;

    explicit SyncManager(const std::string& api_base_url);
    ~SyncManager();

    // Configuration
    void set_base_url(const std::string& url);
    void set_timeout_seconds(int seconds);
    // SetSkipSSLVerify removed — SSL verification must not be bypassed in production

    // Callbacks (optional)
    void set_status_callback(StatusCallback cb);
    void set_sync_complete_callback(SyncCompleteCallback cb);

    // Authentication
    AuthResult login(const std::string& email, const std::string& auth_hash);
    AuthResult register_user(const std::string& username, const std::string& email, const std::string& auth_hash);
    void logout();
    bool is_logged_in() const;
    std::string get_username() const;

    // Token management
    void set_access_token(const std::string& token);
    std::string get_access_token() const;
    void set_refresh_token(const std::string& token);
    std::string get_refresh_token() const;

    // Token persistence (uses vault_db sync_state)
    void save_tokens_to_db();
    bool load_tokens_from_db();
    void clear_tokens_from_db();

    // Token refresh
    bool refresh_access_token();  // Returns true if refresh succeeded

    // Salt sync (for cross-device encryption)
    bool fetch_server_salt(std::vector<uint8_t>& out_salt);  // Returns true if server has salt
    bool upload_salt(const std::vector<uint8_t>& salt);     // Upload local salt to server
    bool upload_salt_with_error(const std::vector<uint8_t>& salt, std::string& out_error);  // With error details

    // Generic authenticated request (for external callers like share)
    ::HttpResponse make_auth_request(
        const std::string& method,
        const std::string& path,
        const std::string& body = ""
    );

    // Share status (for tracking active shares)
    struct ShareStatus {
        bool    valid = false;       // true if API returned data
        int     view_count = 0;
        int     max_views = 0;       // 0 = unlimited
        bool    expired = false;
        bool    views_exhausted = false;
        int64_t expires_at_ms = 0;   // 0 = never
        int64_t created_at_ms = 0;
    };

    ShareStatus get_share_status(const std::string& token);

    // Anonymous vault sharing (bundle)
    struct ShareResult {
        bool success = false;
        std::string url;
        std::string error;
    };

    ShareResult share_vault_anon(
        const std::vector<Credential>& creds,
        const std::string& bundle_name,
        int expiry_seconds,
        int max_views,
        const std::string& passphrase  // empty = no passphrase
    );

    // Cloud multi-vault management
    struct CloudVaultInfo {
        std::string vault_slug;
        std::string vault_name;
        std::string encryption_salt_b64;  // base64, 16 bytes
        int64_t     current_version = 0;
    };

    struct CloudVaultResult {
        bool success = false;
        std::string message;
        std::vector<CloudVaultInfo> vaults;
        std::string created_slug;
    };

    CloudVaultResult list_cloud_vaults();
    CloudVaultResult create_cloud_vault(const std::string& slug, const std::string& name, const std::string& salt_b64);
    CloudVaultResult delete_cloud_vault(const std::string& slug);

    // Synchronization
    SyncResult perform_full_sync();           // Blocking: push then pull
    void start_background_sync();             // Non-blocking: spawns thread

    // Status
    SyncStatus get_status() const;
    std::string get_last_message() const;
    bool is_syncing() const;

private:
    // URL components
    std::string m_base_url;
    std::string m_host;
    int m_port = 443;
    bool m_use_ssl = true;

    // Auth state
    std::string m_access_token;
    std::string m_refresh_token;
    std::string m_username;

    // Config
    int m_timeout_sec = 15;
    // m_skip_ssl_verify removed — SSL verification must not be bypassed

    // Status
    std::atomic<SyncStatus> m_status{SyncStatus::Idle};
    mutable std::mutex m_mutex;
    std::string m_last_message;

    // Callbacks
    StatusCallback m_status_callback;
    SyncCompleteCallback m_sync_complete_callback;

    // Internal
    void parse_url(const std::string& url);
    void set_status(SyncStatus status, const std::string& msg = "");
    std::string extract_error_message(const std::string& json_body, const std::string& fallback);

    // Authenticated request with auto-refresh on 401
    ::HttpResponse authenticated_request(
        const std::string& method,
        const std::string& path,
        const std::string& body = ""
    );

    bool push_local_changes(int& out_pushed);
    bool pull_remote_changes(int& out_pulled, int64_t& out_new_rev);
};
