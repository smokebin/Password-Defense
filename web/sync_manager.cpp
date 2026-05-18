// sync_manager.cpp
// Cloud synchronization manager implementation

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #include <windows.h>
    #include <winhttp.h>
    #pragma comment(lib, "winhttp.lib")
#else
    #define CPPHTTPLIB_OPENSSL_SUPPORT
    #include "httplib.h"
#endif

#include "sync_manager.h"
#include "../credentials/vault_db.h"
#include "../credentials/crypto/vault_crypto.h"
#include "../tools/utility.h"

// nlohmann/json
#include "../third_party/json.hpp"

// libsodium for base64
#include <sodium.h>

#include <thread>
#include <sstream>
#include <ctime>

using json = nlohmann::json;

// ============================================================
// Base64 helpers (using libsodium)
// ============================================================

static std::string base64_encode(const std::vector<uint8_t>& data) {
    if (data.empty()) return "";

    size_t b64_len = sodium_base64_encoded_len(data.size(), sodium_base64_VARIANT_ORIGINAL);
    std::string out(b64_len, '\0');

    sodium_bin2base64(&out[0], b64_len, data.data(), data.size(), sodium_base64_VARIANT_ORIGINAL);

    // Remove null terminator
    while (!out.empty() && out.back() == '\0') {
        out.pop_back();
    }

    return out;
}

static std::vector<uint8_t> base64_decode(const std::string& b64) {
    if (b64.empty()) return {};

    std::vector<uint8_t> out(b64.size());
    size_t bin_len = 0;

    int rc = sodium_base642bin(
        out.data(), out.size(),
        b64.c_str(), b64.size(),
        nullptr, &bin_len, nullptr,
        sodium_base64_VARIANT_ORIGINAL
    );

    if (rc != 0) return {};

    out.resize(bin_len);
    return out;
}

// ============================================================
// ISO8601 parsing (server sends UTC strings)
// ============================================================

static int64_t iso_to_unix_ms(const std::string& iso) {
    if (iso.empty()) return 0;

    std::tm tm = {};
    int ms = 0;

    if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d.%dZ",
        &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
        &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &ms) >= 6) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
    }
    else if (sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%dZ",
        &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
        &tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
    }
    else {
        return 0;
    }

#ifdef _WIN32
    time_t sec = _mkgmtime(&tm);
#else
    time_t sec = timegm(&tm);
#endif
    if (sec == -1) return 0;

    return static_cast<int64_t>(sec) * 1000 + ms;
}

// ============================================================
// HTTP Request Helper
// ============================================================

// HttpResponse is declared in sync_manager.h

#ifdef _WIN32

static std::wstring utf8_to_wide(const std::string& str) {
    if (str.empty()) return L"";
    int size = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), nullptr, 0);
    std::wstring result(size, 0);
    MultiByteToWideChar(CP_UTF8, 0, str.c_str(), (int)str.size(), &result[0], size);
    return result;
}

HttpResponse win_http_request(
    const std::string& method,
    const std::string& host,
    int port,
    bool use_ssl,
    const std::string& path,
    const std::string& body,
    const std::string& bearer_token)
{
    HttpResponse resp;

    // Create session
    HINTERNET hSession = WinHttpOpen(
        L"PasswordManager/1.9",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);

    if (!hSession) {
        resp.error = "Failed to create WinHTTP session";
        return resp;
    }

    // Connect
    std::wstring wHost = utf8_to_wide(host);
    HINTERNET hConnect = WinHttpConnect(hSession, wHost.c_str(), (INTERNET_PORT)port, 0);

    if (!hConnect) {
        resp.error = "Failed to connect to server";
        WinHttpCloseHandle(hSession);
        return resp;
    }

    // Open request
    std::wstring wMethod = utf8_to_wide(method);
    std::wstring wPath = utf8_to_wide(path);
    DWORD flags = use_ssl ? WINHTTP_FLAG_SECURE : 0;

    HINTERNET hRequest = WinHttpOpenRequest(
        hConnect,
        wMethod.c_str(),
        wPath.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags);

    if (!hRequest) {
        resp.error = "Failed to create request";
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    // Set timeouts (15 seconds)
    WinHttpSetTimeouts(hRequest, 15000, 15000, 15000, 15000);

    // Build headers
    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!bearer_token.empty()) {
        headers += L"Authorization: Bearer " + utf8_to_wide(bearer_token) + L"\r\n";
    }

    // Send request
    BOOL bResult = WinHttpSendRequest(
        hRequest,
        headers.c_str(),
        (DWORD)-1,
        (LPVOID)(body.empty() ? nullptr : body.c_str()),
        (DWORD)body.size(),
        (DWORD)body.size(),
        0);

    if (!bResult) {
        DWORD err = GetLastError();
        resp.error = "Failed to send request (error " + std::to_string(err) + ")";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    // Receive response
    bResult = WinHttpReceiveResponse(hRequest, nullptr);
    if (!bResult) {
        DWORD err = GetLastError();
        resp.error = "Failed to receive response (error " + std::to_string(err) + ")";
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return resp;
    }

    // Get status code
    DWORD statusCode = 0;
    DWORD statusCodeSize = sizeof(statusCode);
    WinHttpQueryHeaders(
        hRequest,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX,
        &statusCode,
        &statusCodeSize,
        WINHTTP_NO_HEADER_INDEX);

    resp.status_code = (int)statusCode;

    // Read response body
    std::string responseBody;
    DWORD bytesAvailable = 0;
    DWORD bytesRead = 0;

    do {
        bytesAvailable = 0;
        WinHttpQueryDataAvailable(hRequest, &bytesAvailable);

        if (bytesAvailable > 0) {
            std::vector<char> buffer(bytesAvailable + 1, 0);
            WinHttpReadData(hRequest, buffer.data(), bytesAvailable, &bytesRead);
            responseBody.append(buffer.data(), bytesRead);
        }
    } while (bytesAvailable > 0);

    resp.body = responseBody;
    resp.success = true;

    // Cleanup
    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);

    return resp;
}

#else // !_WIN32 — use cpp-httplib with OpenSSL

HttpResponse win_http_request(
    const std::string& method,
    const std::string& host,
    int port,
    bool use_ssl,
    const std::string& path,
    const std::string& body,
    const std::string& bearer_token)
{
    HttpResponse resp;

    try {
        httplib::Headers headers = {
            {"Content-Type", "application/json"}
        };
        if (!bearer_token.empty()) {
            headers.emplace("Authorization", "Bearer " + bearer_token);
        }

        httplib::Result res;

        if (use_ssl) {
            httplib::SSLClient cli(host, port);
            cli.set_connection_timeout(15, 0);
            cli.set_read_timeout(15, 0);
            cli.set_write_timeout(15, 0);
            cli.set_follow_location(true);

            if (method == "GET")
                res = cli.Get(path, headers);
            else if (method == "POST")
                res = cli.Post(path, headers, body, "application/json");
            else if (method == "PUT")
                res = cli.Put(path, headers, body, "application/json");
            else if (method == "DELETE")
                res = cli.Delete(path, headers);
            else {
                resp.error = "Unsupported HTTP method: " + method;
                return resp;
            }
        } else {
            httplib::Client cli(host, port);
            cli.set_connection_timeout(15, 0);
            cli.set_read_timeout(15, 0);
            cli.set_write_timeout(15, 0);
            cli.set_follow_location(true);

            if (method == "GET")
                res = cli.Get(path, headers);
            else if (method == "POST")
                res = cli.Post(path, headers, body, "application/json");
            else if (method == "PUT")
                res = cli.Put(path, headers, body, "application/json");
            else if (method == "DELETE")
                res = cli.Delete(path, headers);
            else {
                resp.error = "Unsupported HTTP method: " + method;
                return resp;
            }
        }

        if (!res) {
            auto err = res.error();
            resp.error = "Connection failed (httplib error " + std::to_string(static_cast<int>(err)) + ")";
            return resp;
        }

        resp.status_code = res->status;
        resp.body = res->body;
        resp.success = true;
    }
    catch (const std::exception& e) {
        resp.error = std::string("HTTP request error: ") + e.what();
    }

    return resp;
}

#endif // _WIN32

// ============================================================
// SyncManager implementation
// ============================================================

SyncManager::SyncManager(const std::string& api_base_url) {
    set_base_url(api_base_url);
}

SyncManager::~SyncManager() {
    // Nothing special needed
}

void SyncManager::set_base_url(const std::string& url) {
    m_base_url = url;
    parse_url(url);
}

void SyncManager::parse_url(const std::string& url) {
    m_host = url;
    m_port = 443;
    m_use_ssl = true;

    // Strip scheme
    if (m_host.find("https://") == 0) {
        m_use_ssl = true;
        m_port = 443;
        m_host = m_host.substr(8);
    }
    else if (m_host.find("http://") == 0) {
        m_use_ssl = false;
        m_port = 80;
        m_host = m_host.substr(7);
    }

    // Extract port if present
    size_t colon = m_host.find(':');
    size_t slash = m_host.find('/');

    if (colon != std::string::npos && (slash == std::string::npos || colon < slash)) {
        std::string port_str = m_host.substr(colon + 1);
        if (slash != std::string::npos) {
            port_str = port_str.substr(0, slash - colon - 1);
        }
        m_port = std::stoi(port_str);
        m_host = m_host.substr(0, colon);
    }
    else if (slash != std::string::npos) {
        m_host = m_host.substr(0, slash);
    }

    // Strip trailing slash
    while (!m_host.empty() && m_host.back() == '/') {
        m_host.pop_back();
    }
}

void SyncManager::set_timeout_seconds(int seconds) {
    m_timeout_sec = seconds;
}

// SetSkipSSLVerify removed — SSL verification must not be bypassed in production

void SyncManager::set_status_callback(StatusCallback cb) {
    m_status_callback = std::move(cb);
}

void SyncManager::set_sync_complete_callback(SyncCompleteCallback cb) {
    m_sync_complete_callback = std::move(cb);
}

void SyncManager::set_status(SyncStatus status, const std::string& msg) {
    m_status = status;

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_last_message = msg;
    }

    if (m_status_callback) {
        m_status_callback(status, msg);
    }
}

SyncStatus SyncManager::get_status() const {
    return m_status;
}

std::string SyncManager::get_last_message() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_last_message;
}

bool SyncManager::is_syncing() const {
    auto s = m_status.load();
    return s == SyncStatus::Pushing || s == SyncStatus::Pulling ||
           s == SyncStatus::Connecting || s == SyncStatus::Authenticating;
}

std::string SyncManager::extract_error_message(const std::string& json_body, const std::string& fallback) {
    try {
        auto j = json::parse(json_body);
        return j.value("message", fallback);
    }
    catch (...) {
        return fallback;
    }
}

// ============================================================
// Authentication
// ============================================================

bool SyncManager::is_logged_in() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return !m_access_token.empty();
}

std::string SyncManager::get_username() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_username;
}

void SyncManager::set_access_token(const std::string& token) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_access_token = token;
}

std::string SyncManager::get_access_token() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_access_token;
}

void SyncManager::set_refresh_token(const std::string& token) {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_refresh_token = token;
}

std::string SyncManager::get_refresh_token() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_refresh_token;
}

// ============================================================
// Token persistence
// ============================================================

void SyncManager::save_tokens_to_db() {
    std::string access, refresh, username;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        access = m_access_token;
        refresh = m_refresh_token;
        username = m_username;
    }

    // Store tokens in vault_db sync_state table
    // Note: These are already encrypted at rest by the vault's SQLite encryption
    vault_db::set_sync_state("sync_access_token", access);
    vault_db::set_sync_state("sync_refresh_token", refresh);
    vault_db::set_sync_state("sync_username", username);
}

bool SyncManager::load_tokens_from_db() {
    std::string access = vault_db::get_sync_state("sync_access_token");
    std::string refresh = vault_db::get_sync_state("sync_refresh_token");
    std::string username = vault_db::get_sync_state("sync_username");

    if (access.empty() && refresh.empty()) {
        return false;  // No stored session
    }

    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_access_token = access;
        m_refresh_token = refresh;
        m_username = username;
    }

    return true;
}

void SyncManager::clear_tokens_from_db() {
    vault_db::set_sync_state("sync_access_token", "");
    vault_db::set_sync_state("sync_refresh_token", "");
    vault_db::set_sync_state("sync_username", "");
}

// ============================================================
// Token refresh
// ============================================================

bool SyncManager::refresh_access_token() {
    std::string refresh;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        refresh = m_refresh_token;
    }

    if (refresh.empty()) {
        return false;
    }

    try {
        json payload = {
            {"refresh_token", refresh}
        };

        auto resp = win_http_request("POST", m_host, m_port, m_use_ssl,
            "/api/auth/refresh", payload.dump());

        if (!resp.success || resp.status_code != 200) {
            // Refresh failed - clear tokens
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_access_token.clear();
                m_refresh_token.clear();
            }
            clear_tokens_from_db();
            return false;
        }

        auto j = json::parse(resp.body);
        std::string new_access = j.value("access_token", "");
        std::string new_refresh = j.value("refresh_token", refresh);  // Some servers return new refresh token

        if (new_access.empty()) {
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_access_token = new_access;
            if (!new_refresh.empty()) {
                m_refresh_token = new_refresh;
            }
        }

        save_tokens_to_db();
        return true;
    }
    catch (...) {
        return false;
    }
}

// ============================================================
// Salt sync
// ============================================================

bool SyncManager::fetch_server_salt(std::vector<uint8_t>& out_salt) {
    out_salt.clear();

    auto resp = authenticated_request("GET", "/api/pm/salt");

    if (!resp.success || resp.status_code != 200) {
        return false;
    }

    try {
        auto j = json::parse(resp.body);
        if (!j.value("success", false) || !j.value("has_salt", false)) {
            return false;  // No salt on server
        }

        std::string salt_b64 = j.value("encryption_salt", "");
        if (salt_b64.empty()) {
            return false;
        }

        out_salt = base64_decode(salt_b64);
        return out_salt.size() == crypto_pwhash_SALTBYTES;  // 16 bytes
    }
    catch (...) {
        return false;
    }
}

bool SyncManager::upload_salt(const std::vector<uint8_t>& salt) {
    std::string unused;
    return upload_salt_with_error(salt, unused);
}

bool SyncManager::upload_salt_with_error(const std::vector<uint8_t>& salt, std::string& out_error) {
    out_error.clear();

    if (salt.size() != crypto_pwhash_SALTBYTES) {  // 16 bytes for Argon2id
        out_error = "Invalid salt size";
        return false;
    }

    try {
        json payload = {
            {"encryption_salt", base64_encode(salt)}
        };

        auto resp = authenticated_request("PUT", "/api/pm/salt", payload.dump());

        if (!resp.success) {
            out_error = "Request failed";
            return false;
        }

        if (resp.status_code != 200) {
            char buf[64];
            snprintf(buf, sizeof(buf), "HTTP %d", resp.status_code);
            out_error = buf;
            return false;
        }

        auto j = json::parse(resp.body);
        if (!j.value("success", false)) {
            out_error = j.value("message", "Unknown error");
            return false;
        }

        return true;
    }
    catch (const std::exception& e) {
        out_error = e.what();
        return false;
    }
    catch (...) {
        out_error = "Unknown exception";
        return false;
    }
}

// ============================================================
// Authenticated request with auto-refresh
// ============================================================

HttpResponse SyncManager::make_auth_request(
    const std::string& method,
    const std::string& path,
    const std::string& body)
{
    return authenticated_request(method, path, body);
}

HttpResponse SyncManager::authenticated_request(
    const std::string& method,
    const std::string& path,
    const std::string& body)
{
    std::string token;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        token = m_access_token;
    }

    // First attempt
    auto resp = win_http_request(method, m_host, m_port, m_use_ssl, path, body, token);

    // If 401, try refreshing token and retry once
    if (resp.success && resp.status_code == 401) {
        if (refresh_access_token()) {
            // Get new token and retry
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                token = m_access_token;
            }
            resp = win_http_request(method, m_host, m_port, m_use_ssl, path, body, token);
        }
    }

    return resp;
}

void SyncManager::logout() {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_access_token.clear();
        m_refresh_token.clear();
        m_username.clear();
    }
    clear_tokens_from_db();
    set_status(SyncStatus::Idle, "Logged out");
}

AuthResult SyncManager::login(const std::string& email, const std::string& auth_hash) {
    AuthResult result;
    set_status(SyncStatus::Authenticating, "Logging in...");

    try {
        json payload = {
            {"username", email},
            {"password_hash", auth_hash},
            {"native_client", true}
        };

        auto resp = win_http_request("POST", m_host, m_port, m_use_ssl,
            "/api/auth/login", payload.dump());

        if (!resp.success) {
            result.message = resp.error.empty() ? "Connection failed" : resp.error;
            set_status(SyncStatus::Offline, result.message);
            return result;
        }

        if (resp.status_code == 200) {
            auto j = json::parse(resp.body);
            if (j.value("success", false)) {
                result.success = true;
                result.access_token = j.value("access_token", "");
                result.username = j.value("username", email);
                result.message = "Login successful";

                std::string refresh = j.value("refresh_token", "");

                // Check for encryption salt from server
                std::string salt_b64 = j.value("encryption_salt", "");
                if (!salt_b64.empty()) {
                    result.encryption_salt = base64_decode(salt_b64);
                    result.has_server_salt = !result.encryption_salt.empty();
                }

                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_access_token = result.access_token;
                    m_refresh_token = refresh;
                    m_username = result.username;
                }

                // Persist tokens for session restoration
                save_tokens_to_db();

                set_status(SyncStatus::Idle, result.message);
                return result;
            }
        }

        result.message = extract_error_message(resp.body, "Login failed (HTTP " + std::to_string(resp.status_code) + ")");
        set_status(SyncStatus::Error, result.message);
    }
    catch (const std::exception& e) {
        result.message = std::string("Login error: ") + e.what();
        set_status(SyncStatus::Error, result.message);
    }

    return result;
}

AuthResult SyncManager::register_user(const std::string& username, const std::string& email, const std::string& auth_hash) {
    AuthResult result;
    set_status(SyncStatus::Authenticating, "Registering...");

    try {
        json payload = {
            {"username", username},
            {"email", email},
            {"password_hash", auth_hash}
        };

        auto resp = win_http_request("POST", m_host, m_port, m_use_ssl,
            "/api/auth/register", payload.dump());

        if (!resp.success) {
            result.message = resp.error.empty() ? "Connection failed" : resp.error;
            set_status(SyncStatus::Offline, result.message);
            return result;
        }

        if (resp.status_code == 200 || resp.status_code == 201) {
            auto j = json::parse(resp.body);
            if (j.value("success", false)) {
                result.success = true;
                result.username = username;
                result.message = "Registration successful";
                set_status(SyncStatus::Idle, result.message);
                return result;
            }
        }

        result.message = extract_error_message(resp.body, "Registration failed (HTTP " + std::to_string(resp.status_code) + ")");
        set_status(SyncStatus::Error, result.message);
    }
    catch (const std::exception& e) {
        result.message = std::string("Registration error: ") + e.what();
        set_status(SyncStatus::Error, result.message);
    }

    return result;
}

// ============================================================
// Synchronization
// ============================================================

void SyncManager::start_background_sync() {
    if (is_syncing()) {
        return;  // Already syncing
    }

    std::thread([this]() {
        SyncResult result = perform_full_sync();

        if (m_sync_complete_callback) {
            m_sync_complete_callback(result);
        }
    }).detach();
}

SyncResult SyncManager::perform_full_sync() {
    SyncResult result;

    if (!is_logged_in()) {
        result.message = "Not logged in";
        set_status(SyncStatus::Error, result.message);
        return result;
    }

    // Step 1: Push local changes
    int pushed = 0;
    if (!push_local_changes(pushed)) {
        result.message = get_last_message();
        return result;
    }
    result.pushed = pushed;

    // Step 2: Pull remote changes
    int pulled = 0;
    int64_t new_rev = 0;
    if (!pull_remote_changes(pulled, new_rev)) {
        result.message = get_last_message();
        return result;
    }
    result.pulled = pulled;
    result.new_server_rev = new_rev;

    // Success
    result.success = true;
    result.message = "Sync complete";
    set_status(SyncStatus::Success, result.message);

    // Reset to idle after delay
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));
    if (m_status == SyncStatus::Success) {
        set_status(SyncStatus::Idle, "Up to date");
    }

    return result;
}

bool SyncManager::push_local_changes(int& out_pushed) {
    out_pushed = 0;
    set_status(SyncStatus::Pushing, "Uploading changes...");

    std::string vault_slug = vault_db::get_vault_slug();

    auto dirty_rows = vault_db::get_dirty_credentials();
    if (dirty_rows.empty()) {
        return true;  // Nothing to push
    }

    try {
        // Build payload
        json items = json::array();
        for (const auto& row : dirty_rows) {
            json item;
            item["uuid"] = row.uuid;

            if (!vault_slug.empty())
                item["vault_slug"] = vault_slug;

            if (row.is_deleted()) {
                item["deleted_at"] = helpers::unix_ms_to_iso8601(row.deleted_at_ms);
                item["encrypted_blob"] = nullptr;
            }
            else {
                item["encrypted_blob"] = base64_encode(row.encrypted_blob);
                item["deleted_at"] = nullptr;
            }

            items.push_back(item);
        }

        json payload = {{"items", items}};

        // Use AuthenticatedRequest for auto-refresh on 401
        auto resp = authenticated_request("POST", "/api/pm/creds/push", payload.dump());

        if (!resp.success) {
            set_status(SyncStatus::Offline, "Connection failed during push");
            return false;
        }

        if (resp.status_code == 401) {
            set_status(SyncStatus::Error, "Session expired - please sign in again");
            return false;
        }

        if (resp.status_code != 200) {
            set_status(SyncStatus::Error, extract_error_message(resp.body, "Push failed (HTTP " + std::to_string(resp.status_code) + ")"));
            return false;
        }

        // Process response
        auto j = json::parse(resp.body);
        if (!j.value("success", false)) {
            set_status(SyncStatus::Error, j.value("message", "Push rejected by server"));
            return false;
        }

        // Mark synced in DB
        auto ack_items = j.value("items", json::array());
        for (const auto& ack : ack_items) {
            std::string uuid = ack.value("uuid", "");
            int64_t server_rev = ack.value("server_rev", int64_t(0));

            if (!uuid.empty() && server_rev > 0) {
                vault_db::mark_synced(uuid, server_rev);
                out_pushed++;
            }
        }

        return true;
    }
    catch (const std::exception& e) {
        set_status(SyncStatus::Error, std::string("Push error: ") + e.what());
        return false;
    }
}

bool SyncManager::pull_remote_changes(int& out_pulled, int64_t& out_new_rev) {
    out_pulled = 0;
    out_new_rev = 0;
    set_status(SyncStatus::Pulling, "Downloading changes...");

    int64_t last_rev = vault_db::get_last_server_rev();
    std::string vault_slug = vault_db::get_vault_slug();

    try {
        std::string path = "/api/pm/creds/since?rev=" + std::to_string(last_rev);
        if (!vault_slug.empty())
            path += "&vault=" + vault_slug;

        // Use AuthenticatedRequest for auto-refresh on 401
        auto resp = authenticated_request("GET", path);

        if (!resp.success) {
            set_status(SyncStatus::Offline, "Connection failed during pull");
            return false;
        }

        if (resp.status_code == 401) {
            set_status(SyncStatus::Error, "Session expired - please sign in again");
            return false;
        }

        if (resp.status_code != 200) {
            set_status(SyncStatus::Error, extract_error_message(resp.body, "Pull failed (HTTP " + std::to_string(resp.status_code) + ")"));
            return false;
        }

        auto j = json::parse(resp.body);
        if (!j.value("success", false)) {
            set_status(SyncStatus::Error, j.value("message", "Pull rejected by server"));
            return false;
        }

        auto items = j.value("items", json::array());
        int64_t max_rev = last_rev;

        if (items.empty()) {
            out_new_rev = last_rev;
            return true;  // Nothing new
        }

        // Apply in transaction
        vault_db::begin_transaction();

        for (const auto& item : items) {
            std::string uuid = item.value("uuid", "");
            int64_t server_rev = item.value("server_rev", int64_t(0));

            if (uuid.empty()) continue;

            if (server_rev > max_rev) {
                max_rev = server_rev;
            }

            // Check for deletion
            auto deleted_at_val = item.find("deleted_at");
            bool is_tombstone = (deleted_at_val != item.end() && !deleted_at_val->is_null());

            if (is_tombstone) {
                int64_t deleted_at_ms = iso_to_unix_ms(item.value("deleted_at", ""));
                vault_db::apply_server_tombstone(uuid, deleted_at_ms, server_rev);
            }
            else {
                std::string b64_blob = item.value("encrypted_blob", "");
                auto blob = base64_decode(b64_blob);

                int64_t updated_at_ms = iso_to_unix_ms(item.value("updated_at", ""));
                if (updated_at_ms == 0) {
                    updated_at_ms = helpers::now_unix_ms();
                }

                vault_db::upsert_from_server(uuid, blob, updated_at_ms, updated_at_ms, server_rev);
            }

            out_pulled++;
        }

        vault_db::commit_transaction();

        // Save checkpoint
        vault_db::set_last_server_rev(max_rev);
        out_new_rev = max_rev;

        return true;
    }
    catch (const std::exception& e) {
        vault_db::rollback_transaction();
        set_status(SyncStatus::Error, std::string("Pull error: ") + e.what());
        return false;
    }
}

// ============================================================
// Base64url (no padding) helper
// ============================================================

static std::string base64_url_encode(const std::vector<uint8_t>& data) {
    if (data.empty()) return "";
    size_t b64_len = sodium_base64_encoded_len(data.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    std::string out(b64_len, '\0');
    sodium_bin2base64(&out[0], b64_len, data.data(), data.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

// ============================================================
// Serialize Credential for sharing (no UUIDs/sync state)
// ============================================================

static json serialize_cred_for_share(const Credential& c) {
    json j;
    j["title"] = c.title;
    j["type"] = static_cast<int>(c.type);

    switch (c.type) {
    case CredType::Password:
        j["user"] = c.user;
        j["email"] = c.email;
        j["password"] = c.password;
        j["website"] = c.website;
        j["totp_secret"] = c.totp_secret;
        j["notes"] = c.notes;
        break;
    case CredType::CreditCard:
        j["cardholder_name"] = c.cardholder_name;
        j["card_number"] = c.card_number;
        j["card_expiry"] = c.card_expiry;
        j["card_cvv"] = c.card_cvv;
        j["card_brand"] = c.card_brand;
        j["card_address"] = c.card_address;
        j["card_city"] = c.card_city;
        j["card_postal_code"] = c.card_postal_code;
        j["notes"] = c.notes;
        break;
    case CredType::Identity:
        j["full_name"] = c.full_name;
        j["id_type"] = c.id_type;
        j["id_number"] = c.id_number;
        j["date_of_birth"] = c.date_of_birth;
        j["expiry_date"] = c.expiry_date;
        j["country"] = c.country;
        j["address"] = c.address;
        j["phone"] = c.phone;
        j["notes"] = c.notes;
        break;
    case CredType::SecureNote:
        j["notes"] = c.notes;
        break;
    }
    return j;
}

// ============================================================
// Anonymous Vault Share (bundle)
// ============================================================

SyncManager::ShareResult SyncManager::share_vault_anon(
    const std::vector<Credential>& creds,
    const std::string& bundle_name,
    int expiry_seconds,
    int max_views,
    const std::string& passphrase)
{
    ShareResult result;

    try {
        // 1. Build bundle JSON
        json bundle;
        bundle["bundle"] = true;
        bundle["name"] = bundle_name;
        bundle["created_at"] = helpers::now_iso8601_utc();

        json cred_arr = json::array();
        for (const auto& c : creds) {
            cred_arr.push_back(serialize_cred_for_share(c));
        }
        bundle["credentials"] = cred_arr;

        std::string plaintext = bundle.dump();

        // 2. Generate random 32-byte data key
        std::vector<uint8_t> data_key(32);
        randombytes_buf(data_key.data(), data_key.size());

        std::string url_key_fragment;
        std::vector<uint8_t> blob;

        if (!passphrase.empty()) {
            // ---- Passphrase-protected flow ----

            // 3a. Generate argon salt
            std::vector<uint8_t> argon_salt(crypto_pwhash_SALTBYTES);
            randombytes_buf(argon_salt.data(), argon_salt.size());

            // 3b. Derive wrap_key from passphrase
            std::vector<uint8_t> wrap_key(32);
            if (crypto_pwhash(
                    wrap_key.data(), wrap_key.size(),
                    passphrase.c_str(), passphrase.size(),
                    argon_salt.data(),
                    crypto_pwhash_OPSLIMIT_MODERATE,
                    crypto_pwhash_MEMLIMIT_MODERATE,
                    crypto_pwhash_ALG_ARGON2ID13) != 0) {
                result.error = "Key derivation failed";
                return result;
            }

            // 3c. Encrypt data_key with wrap_key (XChaCha20-Poly1305, no AAD)
            std::vector<uint8_t> wrap_nonce(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
            randombytes_buf(wrap_nonce.data(), wrap_nonce.size());

            std::vector<uint8_t> wrapped_key(data_key.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
            unsigned long long wrapped_len = 0;
            crypto_aead_xchacha20poly1305_ietf_encrypt(
                wrapped_key.data(), &wrapped_len,
                data_key.data(), data_key.size(),
                nullptr, 0,  // no AAD
                nullptr,
                wrap_nonce.data(),
                wrap_key.data());
            wrapped_key.resize(wrapped_len);

            // 3d. Encrypt plaintext with data_key (XChaCha20-Poly1305, no AAD)
            std::vector<uint8_t> data_nonce(crypto_aead_xchacha20poly1305_ietf_NPUBBYTES);
            randombytes_buf(data_nonce.data(), data_nonce.size());

            std::vector<uint8_t> ciphertext(plaintext.size() + crypto_aead_xchacha20poly1305_ietf_ABYTES);
            unsigned long long ct_len = 0;
            crypto_aead_xchacha20poly1305_ietf_encrypt(
                ciphertext.data(), &ct_len,
                reinterpret_cast<const uint8_t*>(plaintext.data()), plaintext.size(),
                nullptr, 0,  // no AAD
                nullptr,
                data_nonce.data(),
                data_key.data());
            ciphertext.resize(ct_len);

            // 3e. Assemble: [0x02][argon_salt(16)][wrap_nonce(24)][wrapped_key(48)][data_nonce(24)][ciphertext]
            blob.reserve(1 + argon_salt.size() + wrap_nonce.size() + wrapped_key.size() + data_nonce.size() + ciphertext.size());
            blob.push_back(0x02);
            blob.insert(blob.end(), argon_salt.begin(), argon_salt.end());
            blob.insert(blob.end(), wrap_nonce.begin(), wrap_nonce.end());
            blob.insert(blob.end(), wrapped_key.begin(), wrapped_key.end());
            blob.insert(blob.end(), data_nonce.begin(), data_nonce.end());
            blob.insert(blob.end(), ciphertext.begin(), ciphertext.end());

            // No key in URL fragment
            url_key_fragment = "";

            // Wipe sensitive keys
            sodium_memzero(wrap_key.data(), wrap_key.size());
        }
        else {
            // ---- No passphrase flow (existing format) ----
            blob = enc::encrypt_credential_legacy(plaintext, data_key);
            url_key_fragment = base64_url_encode(data_key);
        }

        // Wipe data_key
        sodium_memzero(data_key.data(), data_key.size());

        // 4. Base64-encode blob for upload
        std::string encrypted_data = base64_encode(blob);

        // 5. Build request payload
        json payload;
        payload["encrypted_data"] = encrypted_data;
        payload["max_views"] = max_views;

        if (expiry_seconds > 0)
            payload["expires_in"] = expiry_seconds;
        else
            payload["expires_in"] = nullptr;

        // 6. Upload via authenticated request
        auto resp = authenticated_request("POST", "/api/pm/share/anon", payload.dump());
        if (!resp.success || resp.status_code != 201) {
            auto j = json::parse(resp.body, nullptr, false);
            std::string msg = "Upload failed";
            if (j.is_object() && j.contains("message"))
                msg = j["message"].get<std::string>();
            result.error = msg;
            return result;
        }

        // 7. Parse response token and construct URL
        auto j = json::parse(resp.body);
        std::string token = j["token"].get<std::string>();

        result.url = "https://passworddefense.net/s/" + token;
        if (!url_key_fragment.empty())
            result.url += "#" + url_key_fragment;

        result.success = true;
    }
    catch (const std::exception& e) {
        result.error = std::string("Error: ") + e.what();
    }
    catch (...) {
        result.error = "Unknown error creating share link";
    }

    return result;
}

// ============================================================
// Cloud Multi-Vault Management
// ============================================================

SyncManager::CloudVaultResult SyncManager::list_cloud_vaults()
{
    CloudVaultResult result;

    try {
        auto resp = authenticated_request("GET", "/api/pm/vault");

        if (!resp.success) {
            result.message = resp.error.empty() ? "Connection failed" : resp.error;
            return result;
        }

        if (resp.status_code != 200) {
            result.message = extract_error_message(resp.body,
                "Failed to list vaults (HTTP " + std::to_string(resp.status_code) + ")");
            return result;
        }

        auto j = json::parse(resp.body);
        auto vaults_arr = j.value("vaults", json::array());

        for (const auto& vj : vaults_arr) {
            CloudVaultInfo info;
            info.vault_slug = vj.value("vault_slug", "");
            info.vault_name = vj.value("vault_name", info.vault_slug);
            info.encryption_salt_b64 = vj.value("encryption_salt", "");
            info.current_version = vj.value("current_version", int64_t(0));
            result.vaults.push_back(std::move(info));
        }

        result.success = true;
    }
    catch (const std::exception& e) {
        result.message = std::string("Error: ") + e.what();
    }
    catch (...) {
        result.message = "Unknown error listing cloud vaults";
    }

    return result;
}

SyncManager::CloudVaultResult SyncManager::create_cloud_vault(
    const std::string& slug,
    const std::string& name,
    const std::string& salt_b64)
{
    CloudVaultResult result;

    try {
        json payload = {
            {"vault_slug", slug},
            {"vault_name", name},
            {"encryption_salt", salt_b64}
        };

        auto resp = authenticated_request("POST", "/api/pm/vault", payload.dump());

        if (!resp.success) {
            result.message = resp.error.empty() ? "Connection failed" : resp.error;
            return result;
        }

        if (resp.status_code == 409) {
            result.message = "A vault with that slug already exists.";
            return result;
        }

        if (resp.status_code != 201 && resp.status_code != 200) {
            result.message = extract_error_message(resp.body,
                "Failed to create vault (HTTP " + std::to_string(resp.status_code) + ")");
            return result;
        }

        auto j = json::parse(resp.body);
        result.created_slug = j.value("vault_slug", slug);
        result.success = true;
    }
    catch (const std::exception& e) {
        result.message = std::string("Error: ") + e.what();
    }
    catch (...) {
        result.message = "Unknown error creating cloud vault";
    }

    return result;
}

SyncManager::CloudVaultResult SyncManager::delete_cloud_vault(const std::string& slug)
{
    CloudVaultResult result;

    try {
        json payload = {
            {"confirm_slug", slug}
        };

        auto resp = authenticated_request("DELETE", "/api/pm/vault/" + slug, payload.dump());

        if (!resp.success) {
            result.message = resp.error.empty() ? "Connection failed" : resp.error;
            return result;
        }

        if (resp.status_code != 200 && resp.status_code != 204) {
            result.message = extract_error_message(resp.body,
                "Failed to delete vault (HTTP " + std::to_string(resp.status_code) + ")");
            return result;
        }

        result.success = true;
    }
    catch (const std::exception& e) {
        result.message = std::string("Error: ") + e.what();
    }
    catch (...) {
        result.message = "Unknown error deleting cloud vault";
    }

    return result;
}

// ============================================================
// Share status check
// ============================================================

SyncManager::ShareStatus SyncManager::get_share_status(const std::string& token) {
    ShareStatus out;
    try {
        auto resp = authenticated_request("GET", "/api/pm/share/anon/" + token + "/status", "");
        // 200 = active, 410 = expired/exhausted (both return data)
        if (!resp.success || (resp.status_code != 200 && resp.status_code != 410)) {
            return out; // valid stays false
        }

        auto j = json::parse(resp.body, nullptr, false);
        if (!j.is_object() || !j.value("success", false)) return out;

        out.valid           = true;
        out.view_count      = j.value("view_count", 0);
        out.max_views       = j.value("max_views", 0);  // null → 0 (unlimited)
        out.expired         = j.value("expired", false);
        out.views_exhausted = j.value("views_exhausted", false);

        std::string created_str = j.value("created_at", "");
        std::string expires_str = j.value("expires_at", "");
        out.created_at_ms = iso_to_unix_ms(created_str);
        out.expires_at_ms = iso_to_unix_ms(expires_str);
    }
    catch (...) {
        // leave valid = false
    }
    return out;
}
