#include "bridge/bridge_server.h"
#include "credentials/credential_ops.h"
#include "third_party/json.hpp"

#ifndef SODIUM_STATIC
#define SODIUM_STATIC
#endif
#include <sodium.h>
#include <sddl.h>
#include <algorithm>
#include <unordered_set>
#include <cctype>

namespace bridge {

// ============================================================
// Domain Matching Helpers (proven from local_server)
// ============================================================

static const std::unordered_set<std::string>& getPublicSuffixes()
{
    static const std::unordered_set<std::string> s = {
        "co.uk", "org.uk", "me.uk", "ltd.uk", "plc.uk", "net.uk",
        "com.au", "net.au", "org.au", "edu.au", "gov.au",
        "co.nz", "net.nz", "org.nz",
        "co.jp", "ne.jp", "or.jp", "ac.jp", "go.jp",
        "co.kr", "ne.kr", "re.kr",
        "co.za", "org.za", "net.za",
        "com.br", "net.br", "org.br",
        "com.mx", "org.mx", "net.mx",
        "com.sg", "edu.sg", "gov.sg",
        "com.hk", "org.hk", "edu.hk",
        "co.in", "net.in", "org.in", "gen.in",
        "qc.ca", "on.ca", "bc.ca", "ab.ca",
        "github.io", "gitlab.io", "herokuapp.com", "pages.dev",
        "netlify.app", "vercel.app", "azurewebsites.net",
        "cloudfront.net", "s3.amazonaws.com", "appspot.com",
        "firebaseapp.com", "web.app", "myshopify.com",
        "wordpress.com", "blogspot.com", "wixsite.com",
        "weebly.com"
    };
    return s;
}

static std::vector<std::string> splitLabels(const std::string& host)
{
    std::vector<std::string> labels;
    std::string cur;
    for (char c : host) {
        if (c == '.') {
            if (!cur.empty()) labels.push_back(cur);
            cur.clear();
        } else {
            cur += c;
        }
    }
    if (!cur.empty()) labels.push_back(cur);
    return labels;
}

static bool isIPAddress(const std::string& host)
{
    if (host.empty()) return false;
    for (char c : host) {
        if (c != '.' && !std::isdigit(static_cast<unsigned char>(c))) return false;
    }
    return true;
}

static std::string getRegistrableDomain(const std::string& hostname)
{
    if (isIPAddress(hostname)) return hostname;

    auto labels = splitLabels(hostname);
    if (labels.size() <= 2) return hostname;

    const auto& suffixes = getPublicSuffixes();

    if (labels.size() >= 4) {
        std::string ps3 = labels[labels.size()-3] + "." + labels[labels.size()-2] + "." + labels[labels.size()-1];
        if (suffixes.count(ps3))
            return labels[labels.size()-4] + "." + ps3;
    }

    if (labels.size() >= 3) {
        std::string ps2 = labels[labels.size()-2] + "." + labels[labels.size()-1];
        if (suffixes.count(ps2))
            return labels[labels.size()-3] + "." + ps2;
    }

    return labels[labels.size()-2] + "." + labels[labels.size()-1];
}

static std::string extractHostname(const std::string& urlOrDomain)
{
    auto schemeEnd = urlOrDomain.find("://");
    size_t start = (schemeEnd != std::string::npos) ? schemeEnd + 3 : 0;
    size_t end = urlOrDomain.find_first_of(":/", start);
    std::string host = urlOrDomain.substr(start, end == std::string::npos ? end : end - start);
    std::transform(host.begin(), host.end(), host.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return getRegistrableDomain(host);
}

// ============================================================
// BridgeServer Implementation
// ============================================================

BridgeServer& BridgeServer::GetInstance()
{
    static BridgeServer s_instance;
    return s_instance;
}

BridgeServer::BridgeServer()
{
}

BridgeServer::~BridgeServer()
{
    Stop();
}

void BridgeServer::Start()
{
    if (m_running.exchange(true)) {
        return; // Already running
    }

    m_listener_thread = std::thread(&BridgeServer::ListenerLoop, this);
}

void BridgeServer::Stop()
{
    if (!m_running.exchange(false)) {
        return;
    }

    // Wake up listener by attempting a dummy connection to the pipe
    std::wstring sid = GetCurrentUserSid();
    if (!sid.empty()) {
        std::wstring pipeName = L"\\\\.\\pipe\\PasswordDefense_" + sid;
        HANDLE hDummy = CreateFileW(pipeName.c_str(), GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (hDummy != INVALID_HANDLE_VALUE) {
            CloseHandle(hDummy);
        }
    }

    if (m_listener_thread.joinable()) {
        m_listener_thread.join();
    }

    // Close all remaining client pipes
    {
        std::lock_guard<std::mutex> lk(m_clients_mutex);
        for (HANDLE hPipe : m_active_clients) {
            CancelIoEx(hPipe, NULL);
            CloseHandle(hPipe);
        }
        m_active_clients.clear();
    }
}

std::wstring BridgeServer::GetCurrentUserSid()
{
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return L"";
    }

    DWORD len = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &len);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0) {
        CloseHandle(hToken);
        return L"";
    }

    std::vector<BYTE> buf(len);
    if (!GetTokenInformation(hToken, TokenUser, buf.data(), len, &len)) {
        CloseHandle(hToken);
        return L"";
    }
    CloseHandle(hToken);

    TOKEN_USER* pUser = reinterpret_cast<TOKEN_USER*>(buf.data());
    LPWSTR pSidStr = NULL;
    if (!ConvertSidToStringSidW(pUser->User.Sid, &pSidStr)) {
        return L"";
    }

    std::wstring sid(pSidStr);
    LocalFree(pSidStr);
    return sid;
}

bool BridgeServer::VerifyClientProcess(HANDLE hPipe)
{
    ULONG clientPid = 0;
    if (!GetNamedPipeClientProcessId(hPipe, &clientPid)) {
        return false;
    }

    HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, clientPid);
    if (!hProcess) {
        return false;
    }

    WCHAR imagePath[MAX_PATH] = { 0 };
    DWORD size = MAX_PATH;
    BOOL queryOk = QueryFullProcessImageNameW(hProcess, 0, imagePath, &size);
    CloseHandle(hProcess);

    if (!queryOk) {
        return false;
    }

    std::wstring pathStr(imagePath);
    size_t lastSlash = pathStr.find_last_of(L"\\/");
    std::wstring exeName = (lastSlash != std::wstring::npos) ? pathStr.substr(lastSlash + 1) : pathStr;

    for (auto& c : exeName) {
        c = static_cast<WCHAR>(towlower(c));
    }

    return (exeName == L"pw-bridge.exe");
}

void BridgeServer::ListenerLoop()
{
    std::wstring sid = GetCurrentUserSid();
    if (sid.empty()) return;
    std::wstring pipeName = L"\\\\.\\pipe\\PasswordDefense_" + sid;

    // DACL: Full Access (GA) to current user SID ONLY
    std::wstring sddl = L"D:(A;;GA;;;" + sid + L")";
    PSECURITY_DESCRIPTOR pSD = NULL;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &pSD, NULL)) {
        return;
    }

    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(sa);
    sa.lpSecurityDescriptor = pSD;
    sa.bInheritHandle = FALSE;

    while (m_running) {
        HANDLE hPipe = CreateNamedPipeW(
            pipeName.c_str(),
            PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
            PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT | PIPE_REJECT_REMOTE_CLIENTS,
            PIPE_UNLIMITED_INSTANCES,
            65536,
            65536,
            0,
            &sa
        );

        if (hPipe == INVALID_HANDLE_VALUE) {
            Sleep(200);
            continue;
        }

        OVERLAPPED ovConnect = {};
        ovConnect.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

        BOOL connected = ConnectNamedPipe(hPipe, &ovConnect);
        if (!connected) {
            DWORD err = GetLastError();
            if (err == ERROR_IO_PENDING) {
                while (m_running) {
                    DWORD waitRes = WaitForSingleObject(ovConnect.hEvent, 500);
                    if (waitRes == WAIT_OBJECT_0) {
                        connected = TRUE;
                        break;
                    }
                }
            } else if (err == ERROR_PIPE_CONNECTED) {
                connected = TRUE;
            }
        }

        CloseHandle(ovConnect.hEvent);

        if (!m_running) {
            CloseHandle(hPipe);
            break;
        }

        if (connected) {
            if (!VerifyClientProcess(hPipe)) {
                DisconnectNamedPipe(hPipe);
                CloseHandle(hPipe);
                continue;
            }

            std::thread([this, hPipe]() {
                HandleClient(hPipe);
            }).detach();
        } else {
            CloseHandle(hPipe);
        }
    }

    if (pSD) {
        LocalFree(pSD);
    }
}

void BridgeServer::HandleClient(HANDLE hPipe)
{
    {
        std::lock_guard<std::mutex> lk(m_clients_mutex);
        m_active_clients.push_back(hPipe);
    }

    std::vector<char> buffer(65536);
    std::string fullMessage;

    while (m_running) {
        DWORD bytesRead = 0;
        BOOL ok = ReadFile(hPipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, NULL);
        if (bytesRead > 0) {
            fullMessage.append(buffer.data(), bytesRead);
        }
        if (!ok) {
            DWORD err = GetLastError();
            if (err == ERROR_MORE_DATA) {
                continue;
            }
            break; // Disconnected or closed
        }

        if (!fullMessage.empty()) {
            std::string response = ProcessMessage(fullMessage);
            DWORD written = 0;
            BOOL writeOk = WriteFile(hPipe, response.data(), static_cast<DWORD>(response.size()), &written, NULL);
            if (!writeOk) break;
            fullMessage.clear();
        }
    }

    {
        std::lock_guard<std::mutex> lk(m_clients_mutex);
        m_active_clients.erase(
            std::remove(m_active_clients.begin(), m_active_clients.end(), hPipe),
            m_active_clients.end());
    }

    FlushFileBuffers(hPipe);
    DisconnectNamedPipe(hPipe);
    CloseHandle(hPipe);
}

void BridgeServer::BroadcastEvent(const std::string& eventType)
{
    nlohmann::json evt;
    evt["event"] = eventType;
    std::string payload = evt.dump();

    std::lock_guard<std::mutex> lk(m_clients_mutex);
    for (HANDLE hPipe : m_active_clients) {
        DWORD written = 0;
        WriteFile(hPipe, payload.data(), static_cast<DWORD>(payload.size()), &written, NULL);
    }
}

void BridgeServer::OnVaultUnlocked(const std::vector<Credential>& creds, const std::string& vaultPath)
{
    {
        std::lock_guard<std::mutex> lk(m_cache_mutex);
        m_is_unlocked = true;
        m_active_vault_path = vaultPath;
        m_cached_creds = creds;
    }

    BroadcastEvent("vault_unlocked");
}

void BridgeServer::OnVaultLocked()
{
    {
        std::lock_guard<std::mutex> lk(m_cache_mutex);
        m_is_unlocked = false;
        m_active_vault_path.clear();

        // Secure zeroization invariant:
        for (auto& c : m_cached_creds) {
            if (!c.password.empty()) sodium_memzero((void*)c.password.data(), c.password.capacity());
            if (!c.user.empty()) sodium_memzero((void*)c.user.data(), c.user.capacity());
            if (!c.email.empty()) sodium_memzero((void*)c.email.data(), c.email.capacity());
            if (!c.website.empty()) sodium_memzero((void*)c.website.data(), c.website.capacity());
            if (!c.notes.empty()) sodium_memzero((void*)c.notes.data(), c.notes.capacity());
            if (!c.totp_secret.empty()) sodium_memzero((void*)c.totp_secret.data(), c.totp_secret.capacity());
        }
        m_cached_creds.clear();
        m_cached_creds.shrink_to_fit();
    }

    BroadcastEvent("vault_locked");
}

void BridgeServer::OnVaultModified(const std::vector<Credential>& creds)
{
    {
        std::lock_guard<std::mutex> lk(m_cache_mutex);
        if (m_is_unlocked) {
            m_cached_creds = creds;
        }
    }

    BroadcastEvent("vault_modified");
}

bool BridgeServer::HasPendingWrites() const
{
    std::lock_guard<std::mutex> lk(const_cast<std::mutex&>(m_queue_mutex));
    return !m_pending_writes.empty();
}

void BridgeServer::DrainPendingWrites(
    std::vector<Credential>& uiCreds,
    const std::vector<uint8_t>& masterKey,
    std::function<void()> onStateChanged)
{
    std::vector<std::shared_ptr<PendingCommand>> commands;
    {
        std::lock_guard<std::mutex> lk(m_queue_mutex);
        if (m_pending_writes.empty()) return;
        commands.swap(m_pending_writes);
    }

    if (masterKey.empty()) {
        for (auto& cmd : commands) {
            SaveResult res;
            res.ok = false;
            res.error = "Vault locked";
            cmd->completion_promise.set_value(res);
        }
        return;
    }

    bool stateChanged = false;

    for (auto& cmd : commands) {
        SaveResult res;
        if (cmd->action == "save_new") {
            Credential c;
            c.title = cmd->title.empty() ? cmd->hostname : cmd->title;
            c.user = cmd->username;
            c.password = cmd->password;
            c.website = cmd->hostname;
            c.type = CredType::Password;

            int max_id = 0;
            for (const auto& cr : uiCreds)
                if (cr.id > max_id) max_id = cr.id;
            c.id = max_id + 1;

            const int64_t now_ms = static_cast<int64_t>(std::time(nullptr)) * 1000;
            c.created_at_ms = now_ms;
            c.updated_at_ms = now_ms;

            unsigned char b[16];
            randombytes_buf(b, sizeof(b));
            b[6] = (b[6] & 0x0f) | 0x40; // v4
            b[8] = (b[8] & 0x3f) | 0x80;
            char hex[37];
            snprintf(hex, sizeof(hex),
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                b[0], b[1], b[2], b[3], b[4], b[5], b[6], b[7],
                b[8], b[9], b[10], b[11], b[12], b[13], b[14], b[15]);
            c.uuid = hex;

            uiCreds.push_back(c);
            res.ok = true;
            res.uuid = c.uuid;
            stateChanged = true;
        }
        else if (cmd->action == "update" && !cmd->existing_id.empty()) {
            bool found = false;
            for (auto& c : uiCreds) {
                if (c.uuid == cmd->existing_id) {
                    c.password = cmd->password;
                    c.updated_at_ms = static_cast<int64_t>(std::time(nullptr)) * 1000;
                    res.ok = true;
                    res.uuid = c.uuid;
                    stateChanged = true;
                    found = true;
                    break;
                }
            }
            if (!found) {
                res.ok = false;
                res.error = "Credential not found";
            }
        }
        else {
            res.ok = false;
            res.error = "Invalid action";
        }

        cmd->completion_promise.set_value(res);
    }

    if (stateChanged) {
        {
            std::lock_guard<std::mutex> lk(m_cache_mutex);
            m_cached_creds = uiCreds;
        }
        if (onStateChanged) {
            onStateChanged();
        }
        BroadcastEvent("vault_modified");
    }
}

std::string BridgeServer::ProcessMessage(const std::string& jsonRequest)
{
    nlohmann::json j;
    try {
        j = nlohmann::json::parse(jsonRequest);
    } catch (...) {
        return R"({"ok":false,"error":"Invalid JSON"})";
    }

    std::string action = j.value("action", "");
    if (action == "status") {
        return HandleStatus();
    } else if (action == "match") {
        return HandleMatch(j.value("url", ""));
    } else if (action == "search") {
        return HandleSearch(j.value("q", ""));
    } else if (action == "check") {
        return HandleCheck(j.value("hostname", ""), j.value("username", ""), j.value("password", ""));
    } else if (action == "save") {
        return HandleSave(
            j.value("save_action", j.value("action_type", "save_new")),
            j.value("hostname", ""),
            j.value("title", ""),
            j.value("username", ""),
            j.value("password", ""),
            j.value("existing_id", ""));
    } else {
        nlohmann::json err;
        err["ok"] = false;
        err["error"] = "unknown_action";
        return err.dump();
    }
}

std::string BridgeServer::HandleStatus()
{
    std::lock_guard<std::mutex> lk(m_cache_mutex);
    nlohmann::json resp;
    resp["ok"] = true;
    resp["unlocked"] = m_is_unlocked;
    resp["credCount"] = m_is_unlocked ? m_cached_creds.size() : 0;
    resp["activeVault"] = m_active_vault_path;
    return resp.dump();
}

std::string BridgeServer::HandleMatch(const std::string& url)
{
    std::string tabHost = extractHostname(url);
    nlohmann::json arr = nlohmann::json::array();

    {
        std::lock_guard<std::mutex> lk(m_cache_mutex);
        if (!m_is_unlocked) {
            nlohmann::json err;
            err["ok"] = false;
            err["error"] = "Vault locked";
            return err.dump();
        }

        for (const auto& c : m_cached_creds) {
            if (c.is_deleted()) continue;
            if (c.website.empty()) continue;
            std::string credHost = extractHostname(c.website);
            if (credHost == tabHost) {
                arr.push_back({
                    {"uuid",        c.uuid},
                    {"title",       c.title},
                    {"user",        c.user},
                    {"email",       c.email},
                    {"password",    c.password},
                    {"website",     c.website},
                    {"group",       c.group},
                    {"notes",       c.notes},
                    {"totp_secret", c.totp_secret},
                    {"is_favorite", c.is_favorite},
                    {"is_pinned",   c.is_pinned},
                    {"type",        static_cast<int>(c.type)}
                });
            }
        }
    }

    nlohmann::json resp;
    resp["ok"] = true;
    resp["credentials"] = arr;
    return resp.dump();
}

std::string BridgeServer::HandleSearch(const std::string& query)
{
    std::string q = query;
    std::transform(q.begin(), q.end(), q.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    nlohmann::json arr = nlohmann::json::array();

    {
        std::lock_guard<std::mutex> lk(m_cache_mutex);
        if (!m_is_unlocked) {
            nlohmann::json err;
            err["ok"] = false;
            err["error"] = "Vault locked";
            return err.dump();
        }

        auto toLower = [](std::string s) {
            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return s;
        };

        for (const auto& c : m_cached_creds) {
            if (c.is_deleted()) continue;

            if (q.empty() ||
                toLower(c.title).find(q) != std::string::npos ||
                toLower(c.user).find(q) != std::string::npos ||
                toLower(c.email).find(q) != std::string::npos ||
                toLower(c.website).find(q) != std::string::npos ||
                toLower(c.notes).find(q) != std::string::npos)
            {
                arr.push_back({
                    {"uuid",        c.uuid},
                    {"title",       c.title},
                    {"user",        c.user},
                    {"email",       c.email},
                    {"password",    c.password},
                    {"website",     c.website},
                    {"group",       c.group},
                    {"notes",       c.notes},
                    {"totp_secret", c.totp_secret},
                    {"is_favorite", c.is_favorite},
                    {"is_pinned",   c.is_pinned},
                    {"type",        static_cast<int>(c.type)}
                });
                if (arr.size() >= 50) break;
            }
        }
    }

    nlohmann::json resp;
    resp["ok"] = true;
    resp["credentials"] = arr;
    return resp.dump();
}

std::string BridgeServer::HandleCheck(const std::string& hostname, const std::string& username, const std::string& password)
{
    if (hostname.empty() || username.empty() || password.empty()) {
        nlohmann::json err;
        err["ok"] = false;
        err["error"] = "Missing required fields";
        return err.dump();
    }

    std::string submittedDomain = getRegistrableDomain(hostname);
    std::string action = "save_new";
    std::string existing_id;
    std::string existing_title;

    {
        std::lock_guard<std::mutex> lk(m_cache_mutex);
        if (!m_is_unlocked) {
            nlohmann::json err;
            err["ok"] = false;
            err["error"] = "Vault locked";
            return err.dump();
        }

        for (const auto& c : m_cached_creds) {
            if (c.is_deleted()) continue;
            if (c.website.empty()) continue;

            std::string credDomain = extractHostname(c.website);
            if (credDomain != submittedDomain) continue;

            std::string credUser = c.user.empty() ? c.email : c.user;
            if (credUser != username) continue;

            if (c.password == password) {
                action = "already_exists";
                break;
            } else {
                action = "update";
                existing_id = c.uuid;
                existing_title = c.title;
                break;
            }
        }
    }

    nlohmann::json resp;
    resp["ok"] = true;
    resp["action"] = action;
    resp["existing_id"] = existing_id.empty() ? nlohmann::json(nullptr) : nlohmann::json(existing_id);
    resp["existing_title"] = existing_title.empty() ? nlohmann::json(nullptr) : nlohmann::json(existing_title);
    return resp.dump();
}

std::string BridgeServer::HandleSave(const std::string& action, const std::string& hostname, const std::string& title,
                                     const std::string& username, const std::string& password, const std::string& existing_id)
{
    {
        std::lock_guard<std::mutex> lk(m_cache_mutex);
        if (!m_is_unlocked) {
            nlohmann::json err;
            err["ok"] = false;
            err["error"] = "Vault locked";
            return err.dump();
        }
    }

    auto cmd = std::make_shared<PendingCommand>();
    cmd->id = m_next_cmd_id.fetch_add(1);
    cmd->action = action;
    cmd->hostname = hostname;
    cmd->title = title;
    cmd->username = username;
    cmd->password = password;
    cmd->existing_id = existing_id;

    auto fut = cmd->completion_promise.get_future();

    {
        std::lock_guard<std::mutex> lk(m_queue_mutex);
        m_pending_writes.push_back(cmd);
    }

    // Wait up to 5 seconds for UI thread to drain
    if (fut.wait_for(std::chrono::seconds(5)) == std::future_status::timeout) {
        nlohmann::json timeoutErr;
        timeoutErr["ok"] = false;
        timeoutErr["error"] = "Timeout waiting for UI thread";
        return timeoutErr.dump();
    }

    SaveResult res = fut.get();
    nlohmann::json resp;
    resp["ok"] = res.ok;
    if (res.ok) {
        resp["uuid"] = res.uuid;
    } else {
        resp["error"] = res.error;
    }
    return resp.dump();
}

} // namespace bridge
