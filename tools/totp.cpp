// totp.cpp — RFC 6238 TOTP; SHA-1 (FIPS 180-4), HMAC-SHA1 (RFC 2104)

#include "totp.h"
#include <cstring>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <sodium.h>

namespace totp {

namespace {

struct SHA1_CTX {
    uint32_t state[5];
    uint64_t count;
    uint8_t  buffer[64];
};

static uint32_t rol32(uint32_t v, int n) { return (v << n) | (v >> (32 - n)); }

static void sha1_transform(uint32_t state[5], const uint8_t block[64])
{
    uint32_t w[80];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)block[i*4] << 24 | (uint32_t)block[i*4+1] << 16
              | (uint32_t)block[i*4+2] << 8 | (uint32_t)block[i*4+3];
    for (int i = 16; i < 80; i++)
        w[i] = rol32(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);

    uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | ((~b) & d);       k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                   k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else              { f = b ^ c ^ d;                   k = 0xCA62C1D6; }

        uint32_t temp = rol32(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol32(b, 30); b = a; a = temp;
    }

    state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
}

static void sha1_init(SHA1_CTX& ctx)
{
    ctx.state[0] = 0x67452301;
    ctx.state[1] = 0xEFCDAB89;
    ctx.state[2] = 0x98BADCFE;
    ctx.state[3] = 0x10325476;
    ctx.state[4] = 0xC3D2E1F0;
    ctx.count = 0;
    std::memset(ctx.buffer, 0, 64);
}

static void sha1_update(SHA1_CTX& ctx, const uint8_t* data, size_t len)
{
    size_t idx = (size_t)(ctx.count % 64);
    ctx.count += len;

    for (size_t i = 0; i < len; i++) {
        ctx.buffer[idx++] = data[i];
        if (idx == 64) {
            sha1_transform(ctx.state, ctx.buffer);
            idx = 0;
        }
    }
}

static void sha1_final(SHA1_CTX& ctx, uint8_t digest[20])
{
    uint64_t bits = ctx.count * 8;
    size_t idx = (size_t)(ctx.count % 64);

    ctx.buffer[idx++] = 0x80;
    if (idx > 56) {
        std::memset(ctx.buffer + idx, 0, 64 - idx);
        sha1_transform(ctx.state, ctx.buffer);
        idx = 0;
    }
    std::memset(ctx.buffer + idx, 0, 56 - idx);

    // Big-endian bit length
    for (int i = 0; i < 8; i++)
        ctx.buffer[56 + i] = (uint8_t)(bits >> (56 - i * 8));

    sha1_transform(ctx.state, ctx.buffer);

    for (int i = 0; i < 5; i++) {
        digest[i*4+0] = (uint8_t)(ctx.state[i] >> 24);
        digest[i*4+1] = (uint8_t)(ctx.state[i] >> 16);
        digest[i*4+2] = (uint8_t)(ctx.state[i] >> 8);
        digest[i*4+3] = (uint8_t)(ctx.state[i]);
    }
}

// HMAC-SHA1 (RFC 2104)
static void hmac_sha1(const uint8_t* key, size_t key_len,
                      const uint8_t* msg, size_t msg_len,
                      uint8_t out[20])
{
    const size_t BLOCK = 64;
    uint8_t k_pad[BLOCK];
    std::memset(k_pad, 0, BLOCK);

    if (key_len > BLOCK) {
        SHA1_CTX hctx;
        sha1_init(hctx);
        sha1_update(hctx, key, key_len);
        sha1_final(hctx, k_pad);
    } else {
        std::memcpy(k_pad, key, key_len);
    }

    uint8_t ipad[BLOCK], opad[BLOCK];
    for (size_t i = 0; i < BLOCK; i++) {
        ipad[i] = k_pad[i] ^ 0x36;
        opad[i] = k_pad[i] ^ 0x5C;
    }

    SHA1_CTX ctx;
    sha1_init(ctx);
    sha1_update(ctx, ipad, BLOCK);
    sha1_update(ctx, msg, msg_len);
    uint8_t inner[20];
    sha1_final(ctx, inner);

    sha1_init(ctx);
    sha1_update(ctx, opad, BLOCK);
    sha1_update(ctx, inner, 20);
    sha1_final(ctx, out);
}

static int base32_val(char ch)
{
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= 'a' && ch <= 'z') return ch - 'a';
    if (ch >= '2' && ch <= '7') return ch - '2' + 26;
    return -1;
}

} // anonymous namespace

// public wrapper — used by HIBP breach check
void sha1_digest(const uint8_t* data, size_t len, uint8_t out[20])
{
    SHA1_CTX ctx;
    sha1_init(ctx);
    sha1_update(ctx, data, len);
    sha1_final(ctx, out);
}

std::vector<uint8_t> base32_decode(const std::string& input)
{
    std::vector<uint8_t> out;
    int bits = 0;
    int value = 0;

    for (char ch : input) {
        if (ch == ' ' || ch == '-' || ch == '=' || ch == '\t' || ch == '\n' || ch == '\r')
            continue;
        int v = base32_val(ch);
        if (v < 0) continue;
        value = (value << 5) | v;
        bits += 5;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((uint8_t)((value >> bits) & 0xFF));
        }
    }
    return out;
}

std::string generate_code(const std::vector<uint8_t>& secret, int64_t unix_sec, int period, int digits)
{
    // T = floor(unix_sec / period), per RFC 6238
    uint64_t T = (uint64_t)(unix_sec / period);

    uint8_t msg[8];  // T as big-endian 8 bytes
    for (int i = 7; i >= 0; i--) {
        msg[i] = (uint8_t)(T & 0xFF);
        T >>= 8;
    }

    uint8_t hash[20];
    hmac_sha1(secret.data(), secret.size(), msg, 8, hash);

    // dynamic truncation — RFC 4226 §5.4
    int offset = hash[19] & 0x0F;
    uint32_t code = ((uint32_t)(hash[offset] & 0x7F) << 24)
                  | ((uint32_t)hash[offset+1] << 16)
                  | ((uint32_t)hash[offset+2] << 8)
                  | ((uint32_t)hash[offset+3]);

    uint32_t mod = 1;
    for (int i = 0; i < digits; i++) mod *= 10;
    code %= mod;

    std::string result = std::to_string(code);
    while ((int)result.size() < digits)
        result.insert(result.begin(), '0');

    return result;
}

std::string generate_code_now(const std::vector<uint8_t>& secret)
{
    return generate_code(secret, (int64_t)std::time(nullptr));
}

int seconds_remaining(int64_t unix_sec, int period)
{
    return period - (int)(unix_sec % period);
}

int seconds_remaining_now(int period)
{
    return seconds_remaining((int64_t)std::time(nullptr), period);
}

bool parse_otpauth_uri(const std::string& uri, std::string& out_secret)
{
    const std::string prefix = "otpauth://totp/";
    if (uri.size() < prefix.size())
        return false;

    std::string lower_uri = uri.substr(0, prefix.size());
    for (auto& ch : lower_uri) ch = (char)std::tolower((unsigned char)ch);
    if (lower_uri != prefix)
        return false;

    std::string lower_full = uri;
    for (auto& ch : lower_full) ch = (char)std::tolower((unsigned char)ch);

    size_t pos = lower_full.find("secret=");
    if (pos == std::string::npos)
        return false;

    pos += 7;
    size_t end = uri.find('&', pos);
    std::string secret = (end != std::string::npos) ? uri.substr(pos, end - pos) : uri.substr(pos);

    while (!secret.empty() && std::isspace((unsigned char)secret.back()))
        secret.pop_back();

    if (secret.empty())
        return false;

    for (auto& ch : secret) ch = (char)std::toupper((unsigned char)ch);

    out_secret = secret;
    return true;
}

bool is_valid_secret(const std::string& base32)
{
    auto bytes = base32_decode(base32);
    return bytes.size() >= 10;
}

std::string base32_encode(const std::vector<uint8_t>& data)
{
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    std::string out;
    int bits = 0;
    int value = 0;
    for (uint8_t b : data) {
        value = (value << 8) | b;
        bits += 8;
        while (bits >= 5) {
            bits -= 5;
            out.push_back(alphabet[(value >> bits) & 0x1F]);
        }
    }
    if (bits > 0) {
        out.push_back(alphabet[(value << (5 - bits)) & 0x1F]);
    }
    return out;
}

std::string generate_random_secret()
{
    std::vector<uint8_t> buf(20);
    randombytes_buf(buf.data(), buf.size());
    return base32_encode(buf);
}

bool verify_code(const std::string& secret_b32, const std::string& code, int64_t unix_sec, int window)
{
    auto secret = base32_decode(secret_b32);
    if (secret.size() < 10) return false;

    for (int i = -window; i <= window; ++i) {
        std::string expected = generate_code(secret, unix_sec + i * 30);
        // constant-time compare to prevent timing side-channel
        if (expected.size() == code.size() &&
            sodium_memcmp(expected.data(), code.data(), expected.size()) == 0)
            return true;
    }
    return false;
}

bool verify_code_now(const std::string& secret_b32, const std::string& code)
{
    return verify_code(secret_b32, code, (int64_t)std::time(nullptr));
}

std::string generate_otpauth_uri(const std::string& secret_b32, const std::string& issuer, const std::string& account)
{
    std::string uri = "otpauth://totp/";
    uri += issuer;
    uri += ':';
    uri += account;
    uri += "?secret=";
    uri += secret_b32;
    uri += "&issuer=";
    uri += issuer;
    uri += "&algorithm=SHA1&digits=6&period=30";
    return uri;
}

} // namespace totp
