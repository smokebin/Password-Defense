// vault_crypto.cpp
// Per-Credential encryption for SQLite storage

#include "vault_crypto.h"

#define SODIUM_STATIC
#include <sodium.h>
#pragma comment(lib, "libsodium.lib")

#include <cstring>

namespace enc {

    // Constants
    static constexpr size_t kSaltLen = crypto_pwhash_SALTBYTES;           // 16 bytes
    static constexpr size_t kKeyLen = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;   // 32 bytes
    static constexpr size_t kNonceLen = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES; // 24 bytes
    static constexpr size_t kTagLen = crypto_aead_xchacha20poly1305_ietf_ABYTES;     // 16 bytes

    // ============================================================
    // Key derivation (Argon2id)
    // ============================================================
    std::vector<uint8_t> derive_master_key(const std::string& password, const std::vector<uint8_t>& salt, bool high_security)
    {
        if (password.empty() || salt.size() != kSaltLen)
            return {};

        std::vector<uint8_t> key(kKeyLen);

        // High security: SENSITIVE params (4 iterations / 1 GB RAM)
        // Default: MODERATE params (3 iterations / 256 MB RAM)
        unsigned long long opslimit = high_security
            ? crypto_pwhash_OPSLIMIT_SENSITIVE
            : crypto_pwhash_OPSLIMIT_MODERATE;
        size_t memlimit = high_security
            ? crypto_pwhash_MEMLIMIT_SENSITIVE
            : crypto_pwhash_MEMLIMIT_MODERATE;

        int rc = crypto_pwhash(
            key.data(), kKeyLen,
            password.data(), password.size(),
            salt.data(),
            opslimit,
            memlimit,
            crypto_pwhash_ALG_ARGON2ID13
        );

        if (rc != 0) {
            sodium_memzero(key.data(), key.size());
            return {};
        }

        return key;
    }

    // ============================================================
    // Salt generation
    // ============================================================
    std::vector<uint8_t> generate_salt()
    {
        std::vector<uint8_t> salt(kSaltLen);
        randombytes_buf(salt.data(), kSaltLen);
        return salt;
    }

    // ============================================================
    // AAD-bound encryption (UUID as AAD)
    // Binds ciphertext to Credential UUID to prevent blob-swapping attacks
    // ============================================================
    std::vector<uint8_t> encrypt_credential(
        const std::string& plaintext,
        const std::vector<uint8_t>& master_key,
        const std::string& uuid)
    {
        if (master_key.size() != kKeyLen)
            return {};

        // Generate random nonce
        std::vector<uint8_t> nonce(kNonceLen);
        randombytes_buf(nonce.data(), kNonceLen);

        // Allocate output: nonce + ciphertext + tag
        const size_t ct_len = plaintext.size() + kTagLen;
        std::vector<uint8_t> blob(kNonceLen + ct_len);

        // Copy nonce to beginning
        std::memcpy(blob.data(), nonce.data(), kNonceLen);

        // Encrypt with UUID as AAD
        unsigned long long actual_ct_len = 0;
        int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
            blob.data() + kNonceLen, &actual_ct_len,
            reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
            reinterpret_cast<const unsigned char*>(uuid.data()), uuid.size(),  // UUID as AAD
            nullptr,     // nsec (not used)
            nonce.data(),
            master_key.data()
        );

        if (rc != 0 || actual_ct_len != ct_len) {
            return {};
        }

        return blob;
    }

    // ============================================================
    // AAD-bound decryption (UUID as AAD)
    // ============================================================
    std::string decrypt_credential(
        const std::vector<uint8_t>& blob,
        const std::vector<uint8_t>& master_key,
        const std::string& uuid)
    {
        if (master_key.size() != kKeyLen)
            return "";

        // Minimum size: nonce + tag
        if (blob.size() < kNonceLen + kTagLen)
            return "";

        const uint8_t* nonce = blob.data();
        const uint8_t* ct_with_tag = blob.data() + kNonceLen;
        const size_t ct_with_tag_len = blob.size() - kNonceLen;

        // Allocate plaintext buffer
        const size_t pt_len = ct_with_tag_len - kTagLen;
        std::vector<uint8_t> pt(pt_len);

        unsigned long long actual_pt_len = 0;
        int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
            pt.data(), &actual_pt_len,
            nullptr,  // nsec (not used)
            ct_with_tag, ct_with_tag_len,
            reinterpret_cast<const unsigned char*>(uuid.data()), uuid.size(),  // UUID as AAD
            nonce,
            master_key.data()
        );

        if (rc != 0 || actual_pt_len != pt_len) {
            sodium_memzero(pt.data(), pt.size());
            return "";
        }

        std::string result(reinterpret_cast<char*>(pt.data()), pt_len);
        sodium_memzero(pt.data(), pt.size());

        return result;
    }

    // ============================================================
    // Legacy encryption (no AAD) - for migration only
    // ============================================================
    std::vector<uint8_t> encrypt_credential_legacy(
        const std::string& plaintext,
        const std::vector<uint8_t>& master_key)
    {
        if (master_key.size() != kKeyLen)
            return {};

        // Generate random nonce
        std::vector<uint8_t> nonce(kNonceLen);
        randombytes_buf(nonce.data(), kNonceLen);

        // Allocate output: nonce + ciphertext + tag
        const size_t ct_len = plaintext.size() + kTagLen;
        std::vector<uint8_t> blob(kNonceLen + ct_len);

        // Copy nonce to beginning
        std::memcpy(blob.data(), nonce.data(), kNonceLen);

        // Encrypt without AAD
        unsigned long long actual_ct_len = 0;
        int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
            blob.data() + kNonceLen, &actual_ct_len,
            reinterpret_cast<const unsigned char*>(plaintext.data()), plaintext.size(),
            nullptr, 0,  // No AAD
            nullptr,     // nsec (not used)
            nonce.data(),
            master_key.data()
        );

        if (rc != 0 || actual_ct_len != ct_len) {
            return {};
        }

        return blob;
    }

    // ============================================================
    // Legacy decryption (no AAD) - for migration only
    // ============================================================
    std::string decrypt_credential_legacy(
        const std::vector<uint8_t>& blob,
        const std::vector<uint8_t>& master_key)
    {
        if (master_key.size() != kKeyLen)
            return "";

        // Minimum size: nonce + tag
        if (blob.size() < kNonceLen + kTagLen)
            return "";

        const uint8_t* nonce = blob.data();
        const uint8_t* ct_with_tag = blob.data() + kNonceLen;
        const size_t ct_with_tag_len = blob.size() - kNonceLen;

        // Allocate plaintext buffer
        const size_t pt_len = ct_with_tag_len - kTagLen;
        std::vector<uint8_t> pt(pt_len);

        unsigned long long actual_pt_len = 0;
        int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
            pt.data(), &actual_pt_len,
            nullptr,  // nsec (not used)
            ct_with_tag, ct_with_tag_len,
            nullptr, 0,  // No AAD
            nonce,
            master_key.data()
        );

        if (rc != 0 || actual_pt_len != pt_len) {
            sodium_memzero(pt.data(), pt.size());
            return "";
        }

        std::string result(reinterpret_cast<char*>(pt.data()), pt_len);
        sodium_memzero(pt.data(), pt.size());

        return result;
    }

    // ============================================================
    // Secure memory clearing
    // ============================================================
    void secure_zero(std::vector<uint8_t>& data)
    {
        if (!data.empty())
            sodium_memzero(data.data(), data.size());
        data.clear();
    }

    void secure_zero(std::string& data)
    {
        if (!data.empty())
            sodium_memzero(&data[0], data.size());
        data.clear();
    }

    // ============================================================
    // Hex encoding/decoding
    // ============================================================
    std::string bytes_to_hex(const std::vector<uint8_t>& bytes)
    {
        static const char* hex_chars = "0123456789abcdef";
        std::string result;
        result.reserve(bytes.size() * 2);

        for (uint8_t b : bytes) {
            result.push_back(hex_chars[(b >> 4) & 0x0F]);
            result.push_back(hex_chars[b & 0x0F]);
        }

        return result;
    }

    std::vector<uint8_t> hex_to_bytes(const std::string& hex)
    {
        if (hex.size() % 2 != 0)
            return {};

        auto hex_val = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };

        std::vector<uint8_t> result;
        result.reserve(hex.size() / 2);

        for (size_t i = 0; i < hex.size(); i += 2) {
            int hi = hex_val(hex[i]);
            int lo = hex_val(hex[i + 1]);
            if (hi < 0 || lo < 0)
                return {};
            result.push_back(static_cast<uint8_t>((hi << 4) | lo));
        }

        return result;
    }

} // namespace enc
