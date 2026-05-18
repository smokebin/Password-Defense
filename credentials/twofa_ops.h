// twofa_ops.h
// Two-factor authentication business logic for vault-level TOTP 2FA
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace twofa_ops {

    struct TwoFactorData {
        std::string totp_secret_b32;
        std::vector<std::string> recovery_codes;
    };

    // Generate new 2FA config (random secret + 8 recovery codes). Does NOT save to DB.
    TwoFactorData generate_new();

    // Load and decrypt 2FA config from the vault DB.
    // Returns empty strings on failure.
    TwoFactorData load(const std::vector<uint8_t>& master_key);

    // Encrypt and save 2FA config to the vault DB.
    bool save(const TwoFactorData& data, const std::vector<uint8_t>& master_key);

    // Remove 2FA config from the vault DB.
    bool remove();

    // Verify a 6-digit TOTP code against the stored 2FA config.
    bool verify_totp(const std::string& code, const std::vector<uint8_t>& master_key);

    // Verify a recovery code. On success, the code is consumed (re-saved with code removed).
    bool verify_recovery(const std::string& code, const std::vector<uint8_t>& master_key);

    // Quick check if 2FA is enabled for the current vault.
    bool is_enabled();

    // Generate a single recovery code: "XXXX-XXXX"
    std::string make_recovery_code();

} // namespace twofa_ops
