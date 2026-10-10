// twofa_ops.h — vault-level TOTP 2FA
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace twofa_ops {

    struct TwoFactorData {
        std::string totp_secret_b32;
        std::vector<std::string> recovery_codes;
        int64_t last_totp_step = 0;  // highest TOTP time step accepted so far (RFC 6238 §5.2)
    };

    TwoFactorData generate_new();  // random secret + 8 recovery codes; doesn't save
    TwoFactorData load(const std::vector<uint8_t>& master_key);
    bool save(const TwoFactorData& data, const std::vector<uint8_t>& master_key);
    bool remove();

    bool verify_totp(const std::string& code, const std::vector<uint8_t>& master_key);
    bool verify_recovery(const std::string& code, const std::vector<uint8_t>& master_key);  // consumes the code on success

    bool is_enabled();
    std::string make_recovery_code();  // "XXXX-XXXX"

} // namespace twofa_ops
