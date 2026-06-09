// vault_crypto.h — per-credential XChaCha20-Poly1305 encryption for SQLite storage

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace enc {

    static constexpr size_t SALT_SIZE = 16;  // crypto_pwhash_SALTBYTES

    // returns 32-byte key, or empty on failure
    // high_security uses SENSITIVE params (4 iters / 1 GB RAM)
    std::vector<uint8_t> derive_master_key(const std::string& password, const std::vector<uint8_t>& salt, bool high_security = false);

    std::vector<uint8_t> generate_salt();

    // UUID passed as AAD — prevents blob-swapping across credentials
    // blob layout: nonce (24 bytes) | ciphertext | tag (16 bytes)
    std::vector<uint8_t> encrypt_credential(
        const std::string& plaintext,
        const std::vector<uint8_t>& master_key,
        const std::string& uuid);

    // returns empty string on failure, including AAD mismatch
    std::string decrypt_credential(
        const std::vector<uint8_t>& blob,
        const std::vector<uint8_t>& master_key,
        const std::string& uuid);

    // legacy (no AAD) — migration only
    std::vector<uint8_t> encrypt_credential_legacy(
        const std::string& plaintext,
        const std::vector<uint8_t>& master_key);

    std::string decrypt_credential_legacy(
        const std::vector<uint8_t>& blob,
        const std::vector<uint8_t>& master_key);

    void secure_zero(std::vector<uint8_t>& data);
    void secure_zero(std::string& data);

    std::string bytes_to_hex(const std::vector<uint8_t>& bytes);
    std::vector<uint8_t> hex_to_bytes(const std::string& hex);

}
