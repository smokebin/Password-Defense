// vault_crypto.cpp
// Per-credential encryption for the vault. XChaCha20-Poly1305 with an
// Argon2id-derived key; the credential UUID is bound in as AAD.

#include "vault_crypto.h"

#define SODIUM_STATIC
#include <sodium.h>
#pragma comment(lib, "libsodium.lib")

#include <cstring>

namespace enc {

    static constexpr size_t kSaltLen = crypto_pwhash_SALTBYTES;           // 16 bytes
    static constexpr size_t kKeyLen = crypto_aead_xchacha20poly1305_ietf_KEYBYTES;   // 32 bytes
    static constexpr size_t kNonceLen = crypto_aead_xchacha20poly1305_ietf_NPUBBYTES; // 24 bytes
    static constexpr size_t kTagLen = crypto_aead_xchacha20poly1305_ietf_ABYTES;     // 16 bytes

    std::vector<uint8_t> derive_master_key(const std::string& password, const std::vector<uint8_t>& salt, bool high_security)
    {
        if (password.empty() || salt.size() != kSaltLen)
            return {};

        std::vector<uint8_t> key(kKeyLen);

        // high_security bumps Argon2 to SENSITIVE (4 passes / 1 GiB) vs the
        // default MODERATE (3 passes / 256 MiB)
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

    std::vector<uint8_t> generate_salt()
    {
        std::vector<uint8_t> salt(kSaltLen);
        randombytes_buf(salt.data(), kSaltLen);
        return salt;
    }

    // Output blob is laid out as nonce | ciphertext | tag. The UUID goes in as
    // AAD so a blob can't be lifted from one credential and replayed into
    // another (the tag won't verify against a different UUID).
    std::vector<uint8_t> encrypt_credential(
        const std::string& plaintext,
        const std::vector<uint8_t>& master_key,
        const std::string& uuid)
    {
        if (master_key.size() != kKeyLen)
            return {};

        std::vector<uint8_t> nonce(kNonceLen);
        randombytes_buf(nonce.data(), kNonceLen);

        const size_t ct_len = plaintext.size() + kTagLen;
        std::vector<uint8_t> blob(kNonceLen + ct_len);
        std::memcpy(blob.data(), nonce.data(), kNonceLen);

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

    std::string decrypt_credential(
        const std::vector<uint8_t>& blob,
        const std::vector<uint8_t>& master_key,
        const std::string& uuid)
    {
        if (master_key.size() != kKeyLen)
            return "";

        if (blob.size() < kNonceLen + kTagLen)  // too small to even hold nonce+tag
            return "";

        const uint8_t* nonce = blob.data();
        const uint8_t* ct_with_tag = blob.data() + kNonceLen;
        const size_t ct_with_tag_len = blob.size() - kNonceLen;

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

    // Pre-AAD format, kept only so old vaults can still be read/migrated.
    std::vector<uint8_t> encrypt_credential_legacy(
        const std::string& plaintext,
        const std::vector<uint8_t>& master_key)
    {
        if (master_key.size() != kKeyLen)
            return {};

        std::vector<uint8_t> nonce(kNonceLen);
        randombytes_buf(nonce.data(), kNonceLen);

        const size_t ct_len = plaintext.size() + kTagLen;
        std::vector<uint8_t> blob(kNonceLen + ct_len);
        std::memcpy(blob.data(), nonce.data(), kNonceLen);

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

    std::string decrypt_credential_legacy(
        const std::vector<uint8_t>& blob,
        const std::vector<uint8_t>& master_key)
    {
        if (master_key.size() != kKeyLen)
            return "";

        if (blob.size() < kNonceLen + kTagLen)  // too small to even hold nonce+tag
            return "";

        const uint8_t* nonce = blob.data();
        const uint8_t* ct_with_tag = blob.data() + kNonceLen;
        const size_t ct_with_tag_len = blob.size() - kNonceLen;

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

    // hex encoding/decoding
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
