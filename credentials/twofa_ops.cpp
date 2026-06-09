// twofa_ops.cpp

#include "twofa_ops.h"
#include "vault_db.h"
#include "crypto/vault_crypto.h"
#include "../tools/totp.h"
#include "../tools/utility.h"
#include "../third_party/json.hpp"
#include <sodium.h>
#include <algorithm>
#include <cctype>

namespace twofa_ops {

static const std::string kTwoFaAAD = "vault-2fa-config";  // fixed AAD, not a credential UUID

// A-Z minus O/I, digits 2-9 (no 0/1) — avoids ambiguous glyphs
static const char kRecoveryCharset[] = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789";
static const uint32_t kRecoveryCharsetLen = sizeof(kRecoveryCharset) - 1;

std::string make_recovery_code()
{
    std::string code;
    code.reserve(9);
    for (int i = 0; i < 8; ++i) {
        if (i == 4) code.push_back('-');
        uint32_t idx = randombytes_uniform(kRecoveryCharsetLen);
        code.push_back(kRecoveryCharset[idx]);
    }
    return code;
}

TwoFactorData generate_new()
{
    TwoFactorData data;
    data.totp_secret_b32 = totp::generate_random_secret();
    data.recovery_codes.reserve(8);
    for (int i = 0; i < 8; ++i)
        data.recovery_codes.push_back(make_recovery_code());
    return data;
}

TwoFactorData load(const std::vector<uint8_t>& master_key)
{
    TwoFactorData data;

    std::vector<uint8_t> blob = vault_db::get_2fa_blob();
    if (blob.empty()) return data;

    std::string json_str = enc::decrypt_credential(blob, master_key, kTwoFaAAD);
    if (json_str.empty()) return data;

    try {
        auto j = nlohmann::json::parse(json_str);
        data.totp_secret_b32 = j.value("totp_secret", "");
        if (j.contains("recovery_codes") && j["recovery_codes"].is_array()) {
            for (const auto& c : j["recovery_codes"])
                data.recovery_codes.push_back(c.get<std::string>());
        }
    } catch (...) {
        data = TwoFactorData{};
    }

    enc::secure_zero(json_str);
    return data;
}

bool save(const TwoFactorData& data, const std::vector<uint8_t>& master_key)
{
    nlohmann::json j;
    j["totp_secret"] = data.totp_secret_b32;
    j["recovery_codes"] = data.recovery_codes;

    std::string json_str = j.dump();
    std::vector<uint8_t> blob = enc::encrypt_credential(json_str, master_key, kTwoFaAAD);
    enc::secure_zero(json_str);

    if (blob.empty()) return false;

    int64_t now_ms = helpers::now_unix_ms();
    return vault_db::set_2fa_blob(blob, now_ms, now_ms);
}

bool remove()
{
    return vault_db::delete_2fa_blob();
}

bool verify_totp(const std::string& code, const std::vector<uint8_t>& master_key)
{
    TwoFactorData data = load(master_key);
    if (data.totp_secret_b32.empty()) return false;

    return totp::verify_code_now(data.totp_secret_b32, code);
}

static std::string normalize_recovery(const std::string& input)
{
    std::string out;
    out.reserve(input.size());
    for (char ch : input) {
        if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r') continue;
        out.push_back((char)std::toupper((unsigned char)ch));
    }
    return out;
}

bool verify_recovery(const std::string& code, const std::vector<uint8_t>& master_key)
{
    TwoFactorData data = load(master_key);
    if (data.recovery_codes.empty()) return false;

    std::string normalized = normalize_recovery(code);

    // always iterate all codes — timing side-channel otherwise
    int match_idx = -1;
    for (int i = 0; i < (int)data.recovery_codes.size(); ++i) {
        const auto& rc = data.recovery_codes[i];
        if (rc.size() == normalized.size() &&
            sodium_memcmp(rc.data(), normalized.data(), rc.size()) == 0) {
            match_idx = i;
            // no break — constant-time
        }
    }

    if (match_idx < 0) return false;

    data.recovery_codes.erase(data.recovery_codes.begin() + match_idx);
    save(data, master_key);
    return true;
}

bool is_enabled()
{
    return vault_db::has_2fa();
}

} // namespace twofa_ops
