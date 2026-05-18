// totp.h
// RFC 6238 TOTP (Time-Based One-Time Password) implementation
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace totp {

    // Decode a Base32-encoded string (RFC 4648). Strips whitespace, dashes, and padding.
    std::vector<uint8_t> base32_decode(const std::string& input);

    // Generate a TOTP code for the given secret and time.
    std::string generate_code(const std::vector<uint8_t>& secret, int64_t unix_sec, int period = 30, int digits = 6);

    // Convenience: generate code using current system time.
    std::string generate_code_now(const std::vector<uint8_t>& secret);

    // Seconds remaining in the current TOTP period.
    int seconds_remaining(int64_t unix_sec, int period = 30);
    int seconds_remaining_now(int period = 30);

    // Parse an otpauth:// URI and extract the base32 secret.
    // Returns true on success, stores the base32 secret in out_secret.
    bool parse_otpauth_uri(const std::string& uri, std::string& out_secret);

    // Validate that a base32 string decodes to a usable secret (>= 10 bytes).
    bool is_valid_secret(const std::string& base32);

    // Encode raw bytes to Base32 (RFC 4648, no padding).
    std::string base32_encode(const std::vector<uint8_t>& data);

    // Generate a random 20-byte secret, returned as Base32.
    std::string generate_random_secret();

    // Verify a TOTP code against a base32 secret, checking +/- window periods for clock skew.
    // Returns true if code matches any period in the window.
    bool verify_code(const std::string& secret_b32, const std::string& code, int64_t unix_sec, int window = 1);

    // Convenience: verify using current system time.
    bool verify_code_now(const std::string& secret_b32, const std::string& code);

    // Build an otpauth:// URI for QR code / authenticator app enrollment.
    std::string generate_otpauth_uri(const std::string& secret_b32, const std::string& issuer, const std::string& account);

    // SHA-1 digest (20 bytes) — used by HIBP breach check
    void sha1_digest(const uint8_t* data, size_t len, uint8_t out[20]);

} // namespace totp
