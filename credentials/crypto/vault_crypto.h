// vault_crypto.h
// Per-Credential encryption for SQLite storage
// Uses XChaCha20-Poly1305 with pre-derived master key

#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace enc {

    // Salt size for Argon2id (crypto_pwhash_SALTBYTES = 16 bytes)
    static constexpr size_t SALT_SIZE = 16;

    // Derive master key from password + salt using Argon2id
    // Returns 32-byte key, or empty vector on failure
    // high_security=true uses SENSITIVE params (4 iterations / 1 GB RAM — slower but stronger)
    std::vector<uint8_t> derive_master_key(const std::string& password, const std::vector<uint8_t>& salt, bool high_security = false);

    // Generate a random salt (16 bytes for Argon2id)
    std::vector<uint8_t> generate_salt();

    // ============================================================
    // AAD-bound encryption (preferred)
    // Binds ciphertext to Credential UUID to prevent blob-swapping attacks
    // ============================================================

    // Encrypt plaintext with master key and UUID as AAD
    // Returns: nonce (24 bytes) + ciphertext + tag (16 bytes)
    // Returns empty on failure
    std::vector<uint8_t> encrypt_credential(
        const std::string& plaintext,
        const std::vector<uint8_t>& master_key,
        const std::string& uuid);

    // Decrypt blob (nonce + ciphertext + tag) with master key and UUID as AAD
    // Returns plaintext, or empty string on failure (includes AAD mismatch)
    std::string decrypt_credential(
        const std::vector<uint8_t>& blob,
        const std::vector<uint8_t>& master_key,
        const std::string& uuid);

    // ============================================================
    // Legacy functions (no AAD) - for migration only
    // ============================================================

    // Encrypt plaintext with master key (no AAD)
    std::vector<uint8_t> encrypt_credential_legacy(
        const std::string& plaintext,
        const std::vector<uint8_t>& master_key);

    // Decrypt blob with master key (no AAD)
    std::string decrypt_credential_legacy(
        const std::vector<uint8_t>& blob,
        const std::vector<uint8_t>& master_key);

    // ============================================================
    // Utilities
    // ============================================================

    // Securely zero memory
    void secure_zero(std::vector<uint8_t>& data);
    void secure_zero(std::string& data);

    // Hex encoding/decoding for salt storage
    std::string bytes_to_hex(const std::vector<uint8_t>& bytes);
    std::vector<uint8_t> hex_to_bytes(const std::string& hex);

}
