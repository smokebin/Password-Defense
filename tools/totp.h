// totp.h — RFC 6238 TOTP implementation
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace totp {

    // RFC 4648, strips whitespace/dashes/padding
    std::vector<uint8_t> base32_decode(const std::string& input);

    std::string generate_code(const std::vector<uint8_t>& secret, int64_t unix_sec, int period = 30, int digits = 6);
    std::string generate_code_now(const std::vector<uint8_t>& secret);

    int seconds_remaining(int64_t unix_sec, int period = 30);
    int seconds_remaining_now(int period = 30);

    // returns true on success; out_secret holds the base32 value
    bool parse_otpauth_uri(const std::string& uri, std::string& out_secret);

    // decodes must yield >= 10 bytes
    bool is_valid_secret(const std::string& base32);

    std::string base32_encode(const std::vector<uint8_t>& data);  // no padding
    std::string generate_random_secret();  // 20 random bytes, base32-encoded

    // checks +/- window periods to tolerate clock skew
    bool verify_code(const std::string& secret_b32, const std::string& code, int64_t unix_sec, int window = 1);
    bool verify_code_now(const std::string& secret_b32, const std::string& code);

    std::string generate_otpauth_uri(const std::string& secret_b32, const std::string& issuer, const std::string& account);

    // SHA-1 digest (20 bytes) — used by HIBP breach check
    void sha1_digest(const uint8_t* data, size_t len, uint8_t out[20]);

} // namespace totp
