#include "local_server.h"
#include "httplib.h"
#include "../credentials/credential.h"
#include "../third_party/json.hpp"
#include "../credentials/crypto/vault_crypto.h"
#include "../credentials/credential_ops.h"
#include "../tools/save.h"
#include <sodium.h>
#include <algorithm>
#include <ctime>
#include <chrono>
#include <cctype>
#include <unordered_set>
#include <filesystem>
#include <sqlite3.h>

// ============================================================
// Helpers
// ============================================================

// --- eTLD+1 (registrable domain) extraction via public suffix list ----------

static const std::unordered_set<std::string>& getPublicSuffixes()
{
    static const std::unordered_set<std::string> s = {
        // UK
        "ac.uk","co.uk","gov.uk","org.uk","net.uk","me.uk","ltd.uk","plc.uk",
        // Australia
        "com.au","net.au","org.au","edu.au","gov.au","asn.au","id.au",
        // New Zealand
        "co.nz","net.nz","org.nz","govt.nz","ac.nz","school.nz",
        // Japan
        "co.jp","or.jp","ne.jp","ac.jp","go.jp","ed.jp","lg.jp",
        // South Korea
        "co.kr","or.kr","go.kr","ne.kr","re.kr","pe.kr","ac.kr",
        // Brazil
        "com.br","net.br","org.br","gov.br","edu.br","art.br",
        // China
        "com.cn","net.cn","org.cn","gov.cn","edu.cn","ac.cn",
        // Taiwan
        "com.tw","net.tw","org.tw","gov.tw","edu.tw",
        // Hong Kong
        "com.hk","net.hk","org.hk","gov.hk","edu.hk",
        // Singapore
        "com.sg","net.sg","org.sg","gov.sg","edu.sg",
        // India
        "co.in","net.in","org.in","gov.in","ac.in","gen.in","firm.in","ind.in",
        // Mexico
        "com.mx","net.mx","org.mx","gob.mx","edu.mx",
        // Argentina
        "com.ar","net.ar","org.ar","gov.ar","edu.ar",
        // South Africa
        "co.za","net.za","org.za","gov.za","ac.za","web.za",
        // Israel
        "co.il","net.il","org.il","gov.il","ac.il",
        // Turkey
        "com.tr","net.tr","org.tr","gov.tr","edu.tr",
        // Thailand
        "co.th","or.th","ac.th","go.th","in.th","mi.th",
        // Malaysia
        "com.my","net.my","org.my","gov.my","edu.my",
        // Philippines
        "com.ph","net.ph","org.ph","gov.ph","edu.ph",
        // Pakistan
        "com.pk","net.pk","org.pk","gov.pk","edu.pk",
        // Indonesia
        "co.id","or.id","go.id","web.id","ac.id",
        // Vietnam
        "com.vn","net.vn","org.vn","gov.vn","edu.vn",
        // Bangladesh
        "com.bd","net.bd","org.bd","gov.bd","edu.bd",
        // Ukraine
        "com.ua","net.ua","org.ua","gov.ua","edu.ua",
        // Poland
        "com.pl","net.pl","org.pl","gov.pl","edu.pl",
        // Russia
        "com.ru","net.ru","org.ru","gov.ru","edu.ru",
        // Portugal
        "com.pt","gov.pt","org.pt",
        // Spain
        "com.es","org.es","gob.es","edu.es",
        // Italy
        "com.it","gov.it",
        // France
        "com.fr","gouv.fr","asso.fr",
        // Hungary
        "co.hu",
        // Romania
        "com.ro","gov.ro","org.ro",
        // Czech
        "co.cz",
        // Austria
        "co.at","or.at",
        // Greece
        "com.gr","gov.gr","org.gr","edu.gr",
        // Nigeria
        "com.ng","net.ng","org.ng","gov.ng","edu.ng",
        // Kenya
        "co.ke","or.ke","ne.ke","go.ke","ac.ke",
        // Egypt
        "com.eg","net.eg","org.eg","gov.eg","edu.eg",
        // Saudi Arabia
        "com.sa","net.sa","org.sa","gov.sa","edu.sa",
        // UAE
        "co.ae","net.ae","org.ae","gov.ae","ac.ae",
        // Iran
        "co.ir","ac.ir","gov.ir","org.ir",
        // Colombia
        "com.co","net.co","org.co","gov.co","edu.co",
        // Chile
        "co.cl",
        // Peru
        "com.pe","net.pe","org.pe","gob.pe","edu.pe",
        // Venezuela
        "com.ve","net.ve","org.ve","gov.ve","edu.ve",

        // Platform-specific (hosting, PaaS, SaaS subdomains)
        "github.io","gitlab.io","bitbucket.io",
        "herokuapp.com",
        "azurewebsites.net","cloudapp.net","azure-api.net",
        "appspot.com","firebaseapp.com","web.app","run.app",
        "netlify.app","netlify.com",
        "vercel.app",
        "pages.dev","workers.dev","r2.dev",
        "fly.dev",
        "onrender.com",
        "railway.app",
        "surge.sh",
        "deno.dev",
        "repl.co",

        // Cloud / CDN
        "s3.amazonaws.com","elasticbeanstalk.com",
        "cloudfront.net",
        "blob.core.windows.net",
        "storage.googleapis.com",

        // Blogging / CMS
        "blogspot.com","blogspot.co.uk","blogspot.com.au",
        "wordpress.com",
        "tumblr.com",
        "typepad.com",
        "medium.com",
        "ghost.io",
        "substack.com",

        // Other
        "myshopify.com",
        "squarespace.com",
        "wixsite.com",
        "weebly.com",
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
        if (c != '.' && !std::isdigit((unsigned char)c)) return false;
    }
    return true;
}

// Returns the registrable domain (eTLD+1) for a hostname.
// login.github.com  → github.com
// app.example.co.uk → example.co.uk
// mysite.github.io  → mysite.github.io  (github.io is a public suffix)
static std::string getRegistrableDomain(const std::string& hostname)
{
    if (isIPAddress(hostname)) return hostname;

    auto labels = splitLabels(hostname);
    if (labels.size() <= 2) return hostname;

    const auto& suffixes = getPublicSuffixes();

    // Check 3-part public suffix (e.g. s3.amazonaws.com)
    if (labels.size() >= 4) {
        std::string ps3 = labels[labels.size()-3] + "." + labels[labels.size()-2] + "." + labels[labels.size()-1];
        if (suffixes.count(ps3))
            return labels[labels.size()-4] + "." + ps3;
    }

    // Check 2-part public suffix (e.g. co.uk, github.io)
    if (labels.size() >= 3) {
        std::string ps2 = labels[labels.size()-2] + "." + labels[labels.size()-1];
        if (suffixes.count(ps2))
            return labels[labels.size()-3] + "." + ps2;
    }

    // Default: single-part TLD, registrable domain = last 2 labels
    return labels[labels.size()-2] + "." + labels[labels.size()-1];
}

// Extract eTLD+1 from a URL or bare domain.
// https://login.github.com/foo → github.com
// www.example.co.uk:8080       → example.co.uk
static std::string extractHostname(const std::string& urlOrDomain)
{
    auto schemeEnd = urlOrDomain.find("://");
    size_t start = (schemeEnd != std::string::npos) ? schemeEnd + 3 : 0;
    size_t end = urlOrDomain.find_first_of(":/", start);
    std::string host = urlOrDomain.substr(start, end == std::string::npos ? end : end - start);
    std::transform(host.begin(), host.end(), host.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return getRegistrableDomain(host);
}

static int64_t nowUnixSeconds()
{
    return (int64_t)std::time(nullptr);
}

static std::string generateUUID()
{
    unsigned char buf[16];
    randombytes_buf(buf, sizeof(buf));
    buf[6] = (buf[6] & 0x0f) | 0x40; // version 4
    buf[8] = (buf[8] & 0x3f) | 0x80; // variant 1
    char hex[37];
    snprintf(hex, sizeof(hex),
             "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             buf[0], buf[1], buf[2], buf[3],
             buf[4], buf[5], buf[6], buf[7],
             buf[8], buf[9], buf[10], buf[11],
             buf[12], buf[13], buf[14], buf[15]);
    return std::string(hex);
}

static std::string bytesToHex(const uint8_t* data, size_t len)
{
    std::string out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        char buf[3];
        snprintf(buf, sizeof(buf), "%02x", data[i]);
        out += buf;
    }
    return out;
}

static std::vector<uint8_t> hexToBytes(const std::string& hex)
{
    std::vector<uint8_t> out;
    out.reserve(hex.size() / 2);
    for (size_t i = 0; i + 1 < hex.size(); i += 2) {
        uint8_t byte = 0;
        for (int j = 0; j < 2; j++) {
            byte <<= 4;
            char c = hex[i + j];
            if (c >= '0' && c <= '9') byte |= (c - '0');
            else if (c >= 'a' && c <= 'f') byte |= (c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') byte |= (c - 'A' + 10);
        }
        out.push_back(byte);
    }
    return out;
}

static bool caseInsensitiveContains(const std::string& haystack, const std::string& needle)
{
    if (needle.empty()) return true;
    if (haystack.size() < needle.size()) return false;
    auto it = std::search(haystack.begin(), haystack.end(),
                          needle.begin(), needle.end(),
                          [](char a, char b) {
                              return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
                          });
    return it != haystack.end();
}

// ============================================================
// Credential serialization
// ============================================================

std::string LocalServer::SerializeCredentials(const std::vector<Credential>& creds)
{
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& c : creds)
    {
        if (c.is_deleted()) continue;

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
    return arr.dump();
}

// ============================================================
// Master password verification (Argon2id -- same params as vault unlock)
// ============================================================

bool LocalServer::VerifyMasterPassword(const std::string& password)
{
    std::vector<uint8_t> salt;
    std::vector<uint8_t> expected_key;

    {
        std::lock_guard<std::mutex> lk(m_data_mutex);
        if (m_master_key.empty() || m_salt.empty()) return false;
        salt = m_salt;
        expected_key = m_master_key;
    }

    std::vector<uint8_t> derived = enc::derive_master_key(password, salt);
    if (derived.empty()) return false;

    bool match = (sodium_memcmp(derived.data(), expected_key.data(), derived.size()) == 0);
    enc::secure_zero(derived);
    return match;
}

// ============================================================
// HMAC validation
// ============================================================

LocalServer::HmacResult LocalServer::ValidateHmac(
    const std::string& method, const std::string& path,
    const std::string& body, const std::string& pairing_id_header,
    const std::string& timestamp_header, const std::string& signature_header)
{
    HmacResult result{false, ""};

    if (pairing_id_header.empty() || timestamp_header.empty() || signature_header.empty())
        return result;

    std::lock_guard<std::mutex> lk(m_data_mutex);

    auto it = m_pairings.find(pairing_id_header);
    if (it == m_pairings.end())
        return result;

    // Check timestamp (±30 seconds)
    int64_t ts = 0;
    try { ts = std::stoll(timestamp_header); }
    catch (...) { return result; }

    int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    int64_t diff = (now_ms > ts) ? (now_ms - ts) : (ts - now_ms);
    if (diff > 30000) // 30 seconds in ms
        return result;

    // Reconstruct message: timestamp + "\n" + METHOD + "\n" + path + "\n" + body
    std::string message = timestamp_header + "\n" + method + "\n" + path + "\n" + body;

    // Compute HMAC-SHA-256
    const auto& key = it->second.hmac_key;
    unsigned char mac[crypto_auth_hmacsha256_BYTES];
    crypto_auth_hmacsha256_state state;
    crypto_auth_hmacsha256_init(&state, key.data(), key.size());
    crypto_auth_hmacsha256_update(&state, (const unsigned char*)message.data(), message.size());
    crypto_auth_hmacsha256_final(&state, mac);

    // Compare with provided signature
    std::vector<uint8_t> provided_sig = hexToBytes(signature_header);
    if (provided_sig.size() != crypto_auth_hmacsha256_BYTES)
        return result;

    if (sodium_memcmp(mac, provided_sig.data(), crypto_auth_hmacsha256_BYTES) != 0)
        return result;

    // Replay protection: reject duplicate (pairing_id + timestamp + signature) within window
    std::string nonce = pairing_id_header + ":" + timestamp_header + ":" + signature_header;

    // Prune expired entries (older than 30s)
    m_replay_log.erase(
        std::remove_if(m_replay_log.begin(), m_replay_log.end(),
            [now_ms](const ReplayEntry& e) { return (now_ms - e.timestamp_ms) > 30000; }),
        m_replay_log.end());

    // Check for duplicate
    for (const auto& e : m_replay_log)
    {
        if (e.nonce == nonce)
            return result;  // replay detected — reject
    }

    // Record this nonce
    m_replay_log.push_back({nonce, now_ms});

    result.ok = true;
    result.pairing_id = pairing_id_header;
    return result;
}

// ============================================================
// Token validation
// ============================================================

bool LocalServer::ValidateToken(const std::string& auth_header, const std::string& pairing_id)
{
    const std::string bearer_prefix = "Bearer ";
    if (auth_header.size() <= bearer_prefix.size() ||
        auth_header.substr(0, bearer_prefix.size()) != bearer_prefix)
        return false;

    std::string provided_token = auth_header.substr(bearer_prefix.size());

    std::lock_guard<std::mutex> lk(m_data_mutex);

    auto it = m_pairings.find(pairing_id);
    if (it == m_pairings.end())
        return false;

    const auto& p = it->second;
    if (p.token.empty())
        return false;

    if (provided_token.size() != p.token.size() ||
        sodium_memcmp(provided_token.data(), p.token.data(), p.token.size()) != 0)
        return false;

    if (p.token_expires <= nowUnixSeconds())
        return false;

    return true;
}

// ============================================================
// Pairing persistence
// ============================================================

void LocalServer::LoadPairings()
{
    std::string raw = cfg::get_local_server_pairings_raw();
    try {
        auto arr = nlohmann::json::parse(raw);
        if (!arr.is_array()) return;

        std::lock_guard<std::mutex> lk(m_data_mutex);
        m_pairings.clear();
        for (const auto& item : arr) {
            PairingInfo p;
            p.id = item.value("id", "");
            if (p.id.empty()) continue;
            std::string key_hex = item.value("hmac_key", "");
            p.hmac_key = hexToBytes(key_hex);
            p.paired_at = item.value("paired_at", (int64_t)0);
            p.token = item.value("token", "");
            p.token_expires = item.value("token_expires", (int64_t)0);
            m_pairings[p.id] = std::move(p);
        }
    }
    catch (...) {}
}

void LocalServer::SavePairings()
{
    nlohmann::json arr = nlohmann::json::array();
    {
        std::lock_guard<std::mutex> lk(m_data_mutex);
        for (const auto& [id, p] : m_pairings) {
            arr.push_back({
                {"id", p.id},
                {"hmac_key", bytesToHex(p.hmac_key.data(), p.hmac_key.size())},
                {"paired_at", p.paired_at},
                {"token", p.token},
                {"token_expires", p.token_expires}
            });
        }
    }
    cfg::set_local_server_pairings_raw(arr.dump());
}

// ============================================================
// Update helpers (called from main thread)
// ============================================================

void LocalServer::SetMasterKey(const std::vector<uint8_t>& key)
{
    std::lock_guard<std::mutex> lk(m_data_mutex);
    m_master_key = key;
}

void LocalServer::SetSalt(const std::vector<uint8_t>& salt)
{
    std::lock_guard<std::mutex> lk(m_data_mutex);
    m_salt = salt;
}

void LocalServer::UpdateCredentials(const std::vector<Credential>& creds)
{
    std::lock_guard<std::mutex> lk(m_data_mutex);
    m_credentials = creds;
}

void LocalServer::SetActiveVaultPath(const std::string& path)
{
    std::lock_guard<std::mutex> lk(m_data_mutex);
    m_active_vault_path = path;
}

LocalServer::VaultSwitchInfo LocalServer::ConsumeVaultSwitch()
{
    std::lock_guard<std::mutex> lk(m_data_mutex);
    m_vault_switched.store(false);
    return std::move(m_switch_info);
}

// ============================================================
// Pairing management (called from UI thread)
// ============================================================

std::string LocalServer::GeneratePairingCode()
{
    static const char charset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"; // no ambiguous chars
    char code[9] = {};
    unsigned char buf[8];
    randombytes_buf(buf, sizeof(buf));
    for (int i = 0; i < 8; i++)
        code[i] = charset[buf[i] % (sizeof(charset) - 1)];

    std::lock_guard<std::mutex> lk(m_data_mutex);
    m_pair_secret = code;
    m_pair_secret_expires = nowUnixSeconds() + 120; // 2 minutes
    m_pair_fail_count = 0;  // reset rate limiter on new code
    return std::string(code);
}

void LocalServer::RevokePairing(const std::string& pairing_id)
{
    {
        std::lock_guard<std::mutex> lk(m_data_mutex);
        m_pairings.erase(pairing_id);
    }
    SavePairings();
}

std::vector<PairingInfo> LocalServer::GetPairings() const
{
    std::lock_guard<std::mutex> lk(m_data_mutex);
    std::vector<PairingInfo> out;
    out.reserve(m_pairings.size());
    for (const auto& [id, p] : m_pairings)
        out.push_back(p);
    return out;
}

// ============================================================
// HTTP server thread
// ============================================================

void LocalServer::Run()
{
    httplib::Server svr;

    // Hardening: request size limit (1 MB — no legitimate request should exceed this)
    svr.set_payload_max_length(1 * 1024 * 1024);

    // Hardening: read timeout (kills slow-loris attacks)
    svr.set_read_timeout(5, 0);

    // Hardening: limit persistent connections
    svr.set_keep_alive_max_count(10);

    // Store server pointer so Stop() can call svr.stop()
    {
        std::lock_guard<std::mutex> lk(m_svr_mutex);
        m_svr = &svr;
    }

    // Hardening: explicit CORS deny — block all cross-origin requests
    svr.set_pre_routing_handler([](const httplib::Request& req, httplib::Response& res) -> httplib::Server::HandlerResponse {
        if (req.method == "OPTIONS") {
            res.status = 403;
            res.set_content(R"({"ok":false,"error":"CORS denied"})", "application/json");
            return httplib::Server::HandlerResponse::Handled;
        }
        // Set restrictive CORS headers on all responses
        res.set_header("Access-Control-Allow-Origin", "null");
        return httplib::Server::HandlerResponse::Unhandled;
    });

    // ---- GET /api/status (no auth required) ----
    svr.Get("/api/status", [&](const httplib::Request&, httplib::Response& res) {
        std::string vault_name;
        {
            std::lock_guard<std::mutex> lk(m_data_mutex);
            if (!m_active_vault_path.empty()) {
                auto stem = std::filesystem::path(m_active_vault_path).stem().string();
                vault_name = stem;
            }
        }
        nlohmann::json resp;
        resp["ok"] = true;
        resp["unlocked"] = true;
        if (!vault_name.empty()) resp["vault_name"] = vault_name;
        res.set_content(resp.dump(), "application/json");
    });

    // ---- POST /api/pair (no HMAC required — bootstrapping step) ----
    svr.Post("/api/pair", [&](const httplib::Request& req, httplib::Response& res) {
        nlohmann::json body;
        try { body = nlohmann::json::parse(req.body); }
        catch (...) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Invalid JSON"})", "application/json");
            return;
        }

        std::string secret = body.value("pairing_secret", "");
        if (secret.empty()) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Missing pairing_secret"})", "application/json");
            return;
        }

        std::string pairing_id;
        std::string key_hex;

        {
            std::lock_guard<std::mutex> lk(m_data_mutex);

            // Rate limiting: after 5 failures, invalidate the code
            if (m_pair_fail_count >= 5) {
                m_pair_secret.clear();
                m_pair_secret_expires = 0;
                res.status = 429;
                res.set_content(R"({"ok":false,"error":"Too many failed attempts. Generate a new code."})", "application/json");
                return;
            }

            if (m_pair_secret.empty() || m_pair_secret_expires <= nowUnixSeconds()) {
                res.status = 403;
                res.set_content(R"({"ok":false,"error":"No pending pairing challenge or expired"})", "application/json");
                return;
            }

            // Constant-time compare
            if (secret.size() != m_pair_secret.size() ||
                sodium_memcmp(secret.data(), m_pair_secret.data(), m_pair_secret.size()) != 0)
            {
                m_pair_fail_count++;
                res.status = 403;
                res.set_content(R"({"ok":false,"error":"Invalid pairing code"})", "application/json");
                return;
            }

            // Generate pairing
            PairingInfo p;
            p.id = generateUUID();
            p.hmac_key.resize(32);
            randombytes_buf(p.hmac_key.data(), 32);
            p.paired_at = nowUnixSeconds();

            pairing_id = p.id;
            key_hex = bytesToHex(p.hmac_key.data(), p.hmac_key.size());

            m_pairings[p.id] = std::move(p);
            m_pair_secret.clear();
            m_pair_secret_expires = 0;
        }

        SavePairings();

        nlohmann::json resp;
        resp["ok"] = true;
        resp["pairing_id"] = pairing_id;
        resp["hmac_key"] = key_hex;
        res.set_content(resp.dump(), "application/json");
    });

    // ---- POST /api/auth/login ----
    svr.Post("/api/auth/login", [&](const httplib::Request& req, httplib::Response& res) {
        // Validate HMAC
        auto hmac = ValidateHmac(
            req.method, req.path, req.body,
            req.get_header_value("X-Pairing-Id"),
            req.get_header_value("X-Timestamp"),
            req.get_header_value("X-Signature"));

        if (!hmac.ok) {
            res.status = 403;
            res.set_content(R"({"ok":false,"error":"HMAC validation failed"})", "application/json");
            return;
        }

        nlohmann::json body;
        try { body = nlohmann::json::parse(req.body); }
        catch (...) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Invalid JSON"})", "application/json");
            return;
        }

        std::string master_pw = body.value("master_password", "");
        if (master_pw.empty()) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Missing master_password"})", "application/json");
            return;
        }

        if (!VerifyMasterPassword(master_pw)) {
            res.status = 401;
            res.set_content(R"({"ok":false,"error":"Wrong master password"})", "application/json");
            return;
        }

        // Generate random 32-byte session token
        unsigned char token_raw[32];
        randombytes_buf(token_raw, sizeof(token_raw));
        char token_hex[65];
        sodium_bin2hex(token_hex, sizeof(token_hex), token_raw, sizeof(token_raw));

        int cred_count = 0;
        {
            std::lock_guard<std::mutex> lk(m_data_mutex);
            auto it = m_pairings.find(hmac.pairing_id);
            if (it != m_pairings.end()) {
                it->second.token = std::string(token_hex);
                it->second.token_expires = nowUnixSeconds() + 4 * 3600; // 4 hours
            }
            for (const auto& c : m_credentials)
                if (!c.is_deleted()) cred_count++;
        }
        SavePairings();  // persist token for server restart survival

        nlohmann::json resp;
        resp["ok"] = true;
        resp["token"] = std::string(token_hex);
        resp["credCount"] = cred_count;
        res.set_content(resp.dump(), "application/json");
    });

    // ---- GET /api/creds/match?url=<encoded> ----
    svr.Get("/api/creds/match", [&](const httplib::Request& req, httplib::Response& res) {
        // Validate HMAC
        auto hmac = ValidateHmac(
            req.method, req.path, "",
            req.get_header_value("X-Pairing-Id"),
            req.get_header_value("X-Timestamp"),
            req.get_header_value("X-Signature"));

        if (!hmac.ok) {
            res.status = 403;
            res.set_content(R"({"ok":false,"error":"HMAC validation failed"})", "application/json");
            return;
        }

        if (!ValidateToken(req.get_header_value("Authorization"), hmac.pairing_id)) {
            // Determine if expired vs invalid
            {
                std::lock_guard<std::mutex> lk(m_data_mutex);
                auto it = m_pairings.find(hmac.pairing_id);
                if (it != m_pairings.end() && !it->second.token.empty() &&
                    it->second.token_expires <= nowUnixSeconds()) {
                    res.status = 401;
                    res.set_content(R"({"ok":false,"error":"Token expired"})", "application/json");
                    return;
                }
            }
            res.status = 401;
            res.set_content(R"({"ok":false,"error":"Invalid token"})", "application/json");
            return;
        }

        std::string url = req.get_param_value("url");
        std::string tabHost = extractHostname(url);

        nlohmann::json arr = nlohmann::json::array();
        {
            std::lock_guard<std::mutex> lk(m_data_mutex);
            for (const auto& c : m_credentials) {
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
        res.set_content(resp.dump(), "application/json");
    });

    // ---- GET /api/creds/search?q=<encoded> ----
    svr.Get("/api/creds/search", [&](const httplib::Request& req, httplib::Response& res) {
        auto hmac = ValidateHmac(
            req.method, req.path, "",
            req.get_header_value("X-Pairing-Id"),
            req.get_header_value("X-Timestamp"),
            req.get_header_value("X-Signature"));

        if (!hmac.ok) {
            res.status = 403;
            res.set_content(R"({"ok":false,"error":"HMAC validation failed"})", "application/json");
            return;
        }

        if (!ValidateToken(req.get_header_value("Authorization"), hmac.pairing_id)) {
            {
                std::lock_guard<std::mutex> lk(m_data_mutex);
                auto it = m_pairings.find(hmac.pairing_id);
                if (it != m_pairings.end() && !it->second.token.empty() &&
                    it->second.token_expires <= nowUnixSeconds()) {
                    res.status = 401;
                    res.set_content(R"({"ok":false,"error":"Token expired"})", "application/json");
                    return;
                }
            }
            res.status = 401;
            res.set_content(R"({"ok":false,"error":"Invalid token"})", "application/json");
            return;
        }

        std::string query = req.get_param_value("q");

        nlohmann::json arr = nlohmann::json::array();
        {
            std::lock_guard<std::mutex> lk(m_data_mutex);
            for (const auto& c : m_credentials) {
                if (c.is_deleted()) continue;
                if (caseInsensitiveContains(c.title, query) ||
                    caseInsensitiveContains(c.user, query) ||
                    caseInsensitiveContains(c.email, query) ||
                    caseInsensitiveContains(c.website, query))
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
                }
            }
        }

        nlohmann::json resp;
        resp["ok"] = true;
        resp["credentials"] = arr;
        res.set_content(resp.dump(), "application/json");
    });

    // ---- GET /api/creds/count ----
    svr.Get("/api/creds/count", [&](const httplib::Request& req, httplib::Response& res) {
        auto hmac = ValidateHmac(
            req.method, req.path, "",
            req.get_header_value("X-Pairing-Id"),
            req.get_header_value("X-Timestamp"),
            req.get_header_value("X-Signature"));

        if (!hmac.ok) {
            res.status = 403;
            res.set_content(R"({"ok":false,"error":"HMAC validation failed"})", "application/json");
            return;
        }

        if (!ValidateToken(req.get_header_value("Authorization"), hmac.pairing_id)) {
            {
                std::lock_guard<std::mutex> lk(m_data_mutex);
                auto it = m_pairings.find(hmac.pairing_id);
                if (it != m_pairings.end() && !it->second.token.empty() &&
                    it->second.token_expires <= nowUnixSeconds()) {
                    res.status = 401;
                    res.set_content(R"({"ok":false,"error":"Token expired"})", "application/json");
                    return;
                }
            }
            res.status = 401;
            res.set_content(R"({"ok":false,"error":"Invalid token"})", "application/json");
            return;
        }

        int count = 0;
        {
            std::lock_guard<std::mutex> lk(m_data_mutex);
            for (const auto& c : m_credentials)
                if (!c.is_deleted()) count++;
        }

        nlohmann::json resp;
        resp["ok"] = true;
        resp["count"] = count;
        res.set_content(resp.dump(), "application/json");
    });

    // ---- POST /api/creds/check — check if Credential already exists ----
    svr.Post("/api/creds/check", [&](const httplib::Request& req, httplib::Response& res) {
        auto hmac = ValidateHmac(
            req.method, req.path, req.body,
            req.get_header_value("X-Pairing-Id"),
            req.get_header_value("X-Timestamp"),
            req.get_header_value("X-Signature"));

        if (!hmac.ok) {
            res.status = 403;
            res.set_content(R"({"ok":false,"error":"HMAC validation failed"})", "application/json");
            return;
        }

        if (!ValidateToken(req.get_header_value("Authorization"), hmac.pairing_id)) {
            {
                std::lock_guard<std::mutex> lk(m_data_mutex);
                auto it = m_pairings.find(hmac.pairing_id);
                if (it != m_pairings.end() && !it->second.token.empty() &&
                    it->second.token_expires <= nowUnixSeconds()) {
                    res.status = 401;
                    res.set_content(R"({"ok":false,"error":"Token expired"})", "application/json");
                    return;
                }
            }
            res.status = 401;
            res.set_content(R"({"ok":false,"error":"Invalid token"})", "application/json");
            return;
        }

        nlohmann::json body;
        try { body = nlohmann::json::parse(req.body); }
        catch (...) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Invalid JSON"})", "application/json");
            return;
        }

        std::string hostname = body.value("hostname", "");
        std::string username = body.value("username", "");
        std::string password = body.value("password", "");

        if (hostname.empty() || username.empty() || password.empty()) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Missing required fields"})", "application/json");
            return;
        }

        std::string submittedDomain = getRegistrableDomain(hostname);

        std::string action = "save_new";
        std::string existing_id;
        std::string existing_title;

        {
            std::lock_guard<std::mutex> lk(m_data_mutex);
            for (const auto& c : m_credentials) {
                if (c.is_deleted()) continue;
                if (c.website.empty()) continue;

                std::string credDomain = extractHostname(c.website);
                if (credDomain != submittedDomain) continue;

                // Same domain — check username match
                std::string credUser = c.user.empty() ? c.email : c.user;
                if (credUser != username) continue;

                // Same domain + same username — check password
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
        res.set_content(resp.dump(), "application/json");
    });

    // ---- POST /api/creds/save — save new or update existing Credential ----
    svr.Post("/api/creds/save", [&](const httplib::Request& req, httplib::Response& res) {
        auto hmac = ValidateHmac(
            req.method, req.path, req.body,
            req.get_header_value("X-Pairing-Id"),
            req.get_header_value("X-Timestamp"),
            req.get_header_value("X-Signature"));

        if (!hmac.ok) {
            res.status = 403;
            res.set_content(R"({"ok":false,"error":"HMAC validation failed"})", "application/json");
            return;
        }

        if (!ValidateToken(req.get_header_value("Authorization"), hmac.pairing_id)) {
            {
                std::lock_guard<std::mutex> lk(m_data_mutex);
                auto it = m_pairings.find(hmac.pairing_id);
                if (it != m_pairings.end() && !it->second.token.empty() &&
                    it->second.token_expires <= nowUnixSeconds()) {
                    res.status = 401;
                    res.set_content(R"({"ok":false,"error":"Token expired"})", "application/json");
                    return;
                }
            }
            res.status = 401;
            res.set_content(R"({"ok":false,"error":"Invalid token"})", "application/json");
            return;
        }

        nlohmann::json body;
        try { body = nlohmann::json::parse(req.body); }
        catch (...) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Invalid JSON"})", "application/json");
            return;
        }

        std::string action = body.value("action", "");
        std::string hostname = body.value("hostname", "");
        std::string title = body.value("title", "");
        std::string username = body.value("username", "");
        std::string password = body.value("password", "");
        std::string existing_id = body.value("existing_id", "");

        if (action.empty() || hostname.empty() || username.empty() || password.empty()) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Missing required fields"})", "application/json");
            return;
        }

        std::vector<uint8_t> master_key;
        {
            std::lock_guard<std::mutex> lk(m_data_mutex);
            master_key = m_master_key;
        }

        if (master_key.empty()) {
            res.status = 503;
            res.set_content(R"({"ok":false,"error":"Vault locked"})", "application/json");
            return;
        }

        bool success = false;

        if (action == "save_new") {
            Credential c;
            c.title = title.empty() ? hostname : title;
            c.user = username;
            c.password = password;
            c.website = hostname;
            c.type = CredType::Password;

            std::string uuid = cred_ops::add(c, master_key);
            success = !uuid.empty();
        }
        else if (action == "update" && !existing_id.empty()) {
            // Load the existing Credential, update its password
            auto all = cred_ops::load_all(master_key);
            for (auto& c : all) {
                if (c.uuid == existing_id) {
                    c.password = password;
                    c.is_dirty = true;
                    success = cred_ops::update(c.uuid, c, master_key);
                    break;
                }
            }
        }
        else {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Invalid action"})", "application/json");
            return;
        }

        if (success) {
            m_external_change.store(true);
        }

        nlohmann::json resp;
        resp["ok"] = true;
        resp["success"] = success;
        res.set_content(resp.dump(), "application/json");
    });

    // ---- GET /api/vault/list — list available vaults ----
    svr.Get("/api/vault/list", [&](const httplib::Request& req, httplib::Response& res) {
        auto hmac = ValidateHmac(
            req.method, req.path, "",
            req.get_header_value("X-Pairing-Id"),
            req.get_header_value("X-Timestamp"),
            req.get_header_value("X-Signature"));

        if (!hmac.ok) {
            res.status = 403;
            res.set_content(R"({"ok":false,"error":"HMAC validation failed"})", "application/json");
            return;
        }

        if (!ValidateToken(req.get_header_value("Authorization"), hmac.pairing_id)) {
            res.status = 401;
            res.set_content(R"({"ok":false,"error":"Invalid token"})", "application/json");
            return;
        }

        // List .db files in exe directory
        std::vector<std::pair<std::string, std::string>> vaults; // {path, name}
        try {
            char exe_path_c[MAX_PATH];
            GetModuleFileNameA(NULL, exe_path_c, MAX_PATH);
            auto exe_dir = std::filesystem::path(exe_path_c).parent_path();

            for (const auto& entry : std::filesystem::directory_iterator(exe_dir)) {
                if (!entry.is_regular_file()) continue;
                if (entry.path().extension() == ".db") {
                    std::string p = entry.path().string();
                    std::string name = entry.path().stem().string();
                    vaults.push_back({p, name});
                }
            }
            std::sort(vaults.begin(), vaults.end());
        } catch (...) {}

        std::string active_path;
        {
            std::lock_guard<std::mutex> lk(m_data_mutex);
            active_path = m_active_vault_path;
        }

        nlohmann::json arr = nlohmann::json::array();
        for (const auto& [p, n] : vaults) {
            arr.push_back({{"path", p}, {"name", n}});
        }

        nlohmann::json resp;
        resp["ok"] = true;
        resp["vaults"] = arr;
        resp["active_path"] = active_path;
        res.set_content(resp.dump(), "application/json");
    });

    // ---- POST /api/vault/switch — switch to a different vault ----
    svr.Post("/api/vault/switch", [&](const httplib::Request& req, httplib::Response& res) {
        auto hmac = ValidateHmac(
            req.method, req.path, req.body,
            req.get_header_value("X-Pairing-Id"),
            req.get_header_value("X-Timestamp"),
            req.get_header_value("X-Signature"));

        if (!hmac.ok) {
            res.status = 403;
            res.set_content(R"({"ok":false,"error":"HMAC validation failed"})", "application/json");
            return;
        }

        if (!ValidateToken(req.get_header_value("Authorization"), hmac.pairing_id)) {
            res.status = 401;
            res.set_content(R"({"ok":false,"error":"Invalid token"})", "application/json");
            return;
        }

        nlohmann::json body;
        try { body = nlohmann::json::parse(req.body); }
        catch (...) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Invalid JSON"})", "application/json");
            return;
        }

        std::string vault_path = body.value("vault_path", "");
        std::string master_password = body.value("master_password", "");

        if (vault_path.empty() || master_password.empty()) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Missing vault_path or master_password"})", "application/json");
            return;
        }

        // Validate vault_path exists and has .db extension
        std::filesystem::path vp(vault_path);
        if (vp.extension() != ".db" || !std::filesystem::exists(vp)) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Invalid vault path"})", "application/json");
            return;
        }

        // Path traversal protection: vault must be in the exe directory
        try {
            char exe_path_c[MAX_PATH];
            GetModuleFileNameA(NULL, exe_path_c, MAX_PATH);
            auto exe_dir = std::filesystem::canonical(std::filesystem::path(exe_path_c).parent_path());
            auto vault_parent = std::filesystem::canonical(vp.parent_path());
            if (vault_parent != exe_dir) {
                res.status = 403;
                res.set_content(R"({"ok":false,"error":"Vault must be in application directory"})", "application/json");
                return;
            }
        } catch (...) {
            res.status = 400;
            res.set_content(R"({"ok":false,"error":"Path validation failed"})", "application/json");
            return;
        }

        // Open temporary sqlite3 connection to the target vault
        sqlite3* temp_db = nullptr;
        if (sqlite3_open_v2(vault_path.c_str(), &temp_db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            if (temp_db) sqlite3_close(temp_db);
            res.status = 500;
            res.set_content(R"({"ok":false,"error":"Failed to open vault"})", "application/json");
            return;
        }

        // Read salt from pm_sync_state
        std::vector<uint8_t> salt;
        {
            sqlite3_stmt* stmt = nullptr;
            const char* sql = "SELECT value FROM pm_sync_state WHERE key = 'encryption_salt'";
            if (sqlite3_prepare_v2(temp_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* hex = (const char*)sqlite3_column_text(stmt, 0);
                    if (hex) salt = hexToBytes(std::string(hex));
                }
                sqlite3_finalize(stmt);
            }
        }

        if (salt.empty()) {
            sqlite3_close(temp_db);
            res.status = 500;
            res.set_content(R"({"ok":false,"error":"Could not read vault salt"})", "application/json");
            return;
        }

        // Derive key (Argon2id — blocks ~1-2s)
        std::vector<uint8_t> derived_key = enc::derive_master_key(master_password, salt);
        if (derived_key.empty()) {
            sqlite3_close(temp_db);
            res.status = 500;
            res.set_content(R"({"ok":false,"error":"Key derivation failed"})", "application/json");
            return;
        }

        // Verify password by decrypting one Credential
        bool password_ok = false;
        {
            sqlite3_stmt* stmt = nullptr;
            const char* sql = "SELECT uuid, encrypted_blob FROM pm_credentials WHERE deleted_at_ms IS NULL OR deleted_at_ms = 0 LIMIT 1";
            if (sqlite3_prepare_v2(temp_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW) {
                    const char* uuid_raw = (const char*)sqlite3_column_text(stmt, 0);
                    const void* blob_data = sqlite3_column_blob(stmt, 1);
                    int blob_size = sqlite3_column_bytes(stmt, 1);

                    if (uuid_raw && blob_data && blob_size > 0) {
                        std::string uuid(uuid_raw);
                        std::vector<uint8_t> blob((const uint8_t*)blob_data, (const uint8_t*)blob_data + blob_size);
                        try {
                            std::string decrypted = enc::decrypt_credential(blob, derived_key, uuid);
                            password_ok = !decrypted.empty();
                        } catch (...) {
                            password_ok = false;
                        }
                    }
                } else {
                    // No credentials in vault — password is OK (nothing to verify against)
                    password_ok = true;
                }
                sqlite3_finalize(stmt);
            }
        }

        if (!password_ok) {
            enc::secure_zero(derived_key);
            sqlite3_close(temp_db);
            res.status = 401;
            res.set_content(R"({"ok":false,"error":"Wrong password"})", "application/json");
            return;
        }

        // Count non-deleted credentials
        int cred_count = 0;
        {
            sqlite3_stmt* stmt = nullptr;
            const char* sql = "SELECT COUNT(*) FROM pm_credentials WHERE deleted_at_ms IS NULL OR deleted_at_ms = 0";
            if (sqlite3_prepare_v2(temp_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
                if (sqlite3_step(stmt) == SQLITE_ROW)
                    cred_count = sqlite3_column_int(stmt, 0);
                sqlite3_finalize(stmt);
            }
        }

        sqlite3_close(temp_db);

        // Signal main thread to switch vault
        std::string vault_name = vp.stem().string();
        {
            std::lock_guard<std::mutex> lk(m_data_mutex);
            m_switch_info.vault_path = vault_path;
            m_switch_info.master_key = derived_key;
            m_switch_info.salt = salt;
            m_switch_info.password = master_password;
            m_vault_switched.store(true);
        }

        nlohmann::json resp;
        resp["ok"] = true;
        resp["credCount"] = cred_count;
        resp["vault_name"] = vault_name;
        res.set_content(resp.dump(), "application/json");
    });

    // Listen only on localhost (blocking)
    svr.listen("127.0.0.1", m_port);

    // Cleanup after listen returns
    {
        std::lock_guard<std::mutex> lk(m_svr_mutex);
        m_svr = nullptr;
    }
    m_running = false;
}

// ============================================================
// Start / Stop / IsRunning / GetPort
// ============================================================

void LocalServer::Start(int port)
{
    if (m_running) return;

    m_port = port;
    m_running = true;

    LoadPairings();

    m_thread = std::thread([this]() { Run(); });
}

void LocalServer::Stop()
{
    if (!m_running) return;

    // Tell httplib to stop accepting
    {
        std::lock_guard<std::mutex> lk(m_svr_mutex);
        if (m_svr)
            static_cast<httplib::Server*>(m_svr)->stop();
    }

    if (m_thread.joinable())
        m_thread.join();

    // Clear sensitive state
    std::lock_guard<std::mutex> lk(m_data_mutex);
    for (auto& [id, p] : m_pairings) {
        p.token.clear();
        p.token_expires = 0;
    }
    m_credentials.clear();
    enc::secure_zero(m_master_key);
    m_salt.clear();
    m_pair_secret.clear();
    m_pair_secret_expires = 0;
    m_active_vault_path.clear();
    m_vault_switched.store(false);
}

bool LocalServer::IsRunning() const
{
    return m_running;
}

int LocalServer::GetPort() const
{
    return m_port;
}
