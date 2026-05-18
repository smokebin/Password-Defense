// auth_crypto.cpp
// Option C authentication implementation using libsodium

#include "auth_crypto.h"
#include <sodium.h>
#include <algorithm>
#include <cctype>

namespace auth {

std::string bytes_to_hex(const uint8_t* data, size_t len) {
    std::string hex;
    hex.reserve(len * 2);

    static const char hex_chars[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        hex.push_back(hex_chars[(data[i] >> 4) & 0x0F]);
        hex.push_back(hex_chars[data[i] & 0x0F]);
    }
    return hex;
}

std::string bytes_to_hex(const std::vector<uint8_t>& bytes) {
    return bytes_to_hex(bytes.data(), bytes.size());
}

std::string compute_auth_hash(const std::string& password, const std::string& email) {
    // Lowercase the email to match server behavior
    std::string email_lower = email;
    std::transform(email_lower.begin(), email_lower.end(), email_lower.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    // Concatenate: password + email (same order as server expects)
    std::string input = password + email_lower;

    // SHA-256 hash using libsodium
    uint8_t hash[crypto_hash_sha256_BYTES];  // 32 bytes
    crypto_hash_sha256(hash,
        reinterpret_cast<const uint8_t*>(input.data()),
        input.size());

    // Return as 64-char lowercase hex string
    return bytes_to_hex(hash, sizeof(hash));
}

} // namespace auth
