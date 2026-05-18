// pwm_file.cpp
// Encrypted .pwm export/import implementation
//
// Binary layout:
//   [0..3]   magic        "PWM\0"
//   [4..5]   version      uint16_t LE (currently 1)
//   [6..21]  salt         Argon2id salt (16 bytes)
//   [22..25] cred_count   uint32_t LE (plaintext, sanity check)
//   [26..49] nonce        XChaCha20-Poly1305 nonce (24 bytes)
//   [50..]   ciphertext   Encrypted JSON + 16-byte Poly1305 tag
//
// AAD = bytes [0..5] (magic + version)

#include "pwm_file.h"
#include "../../third_party/json.hpp"
#include "../crypto/vault_crypto.h"
#include "../../tools/utility.h"

#include <sodium.h>
#include <cstring>

namespace pwm_file {

// ---- constants ----
static const uint8_t  MAGIC[4] = { 'P', 'W', 'M', '\0' };
static constexpr uint16_t FORMAT_VERSION = 1;
static constexpr size_t HEADER_SIZE = 50;  // magic(4) + ver(2) + salt(16) + count(4) + nonce(24)
static constexpr size_t NONCE_SIZE = 24;   // crypto_aead_xchacha20poly1305_ietf_NPUBBYTES
static constexpr size_t TAG_SIZE   = 16;   // crypto_aead_xchacha20poly1305_ietf_ABYTES
static constexpr size_t AAD_SIZE   = 6;    // magic(4) + version(2)
static constexpr uint32_t MAX_CRED_COUNT = 100000;

// ---- serialization (self-contained, not reusing undo serialize) ----

static nlohmann::json cred_to_json(const Credential& c)
{
    nlohmann::json hist = nlohmann::json::array();
    for (const auto& h : c.password_history) {
        hist.push_back({{"password", h.password}, {"changed_at_ms", h.changed_at_ms}});
    }

    return {
        {"uuid",             c.uuid},
        {"title",            c.title},
        {"password",         c.password},
        {"user",             c.user},
        {"email",            c.email},
        {"website",          c.website},
        {"group",            c.group},
        {"notes",            c.notes},
        {"totp_secret",      c.totp_secret},
        {"is_favorite",      c.is_favorite},
        {"is_pinned",        c.is_pinned},
        {"type",             static_cast<int>(c.type)},
        {"card_number",      c.card_number},
        {"card_expiry",      c.card_expiry},
        {"card_cvv",         c.card_cvv},
        {"card_brand",       c.card_brand},
        {"cardholder_name",  c.cardholder_name},
        {"card_address",     c.card_address},
        {"card_city",        c.card_city},
        {"card_postal_code", c.card_postal_code},
        {"full_name",        c.full_name},
        {"id_type",          c.id_type},
        {"id_number",        c.id_number},
        {"date_of_birth",    c.date_of_birth},
        {"expiry_date",      c.expiry_date},
        {"country",          c.country},
        {"address",          c.address},
        {"phone",            c.phone},
        {"created_at_ms",    c.created_at_ms},
        {"updated_at_ms",    c.updated_at_ms},
        {"password_history", hist}
    };
}

static Credential json_to_cred(const nlohmann::json& j)
{
    Credential c{};
    c.uuid          = j.value("uuid", "");
    c.title         = j.value("title", "");
    c.password      = j.value("password", "");
    c.user          = j.value("user", "");
    c.email         = j.value("email", "");
    c.website       = j.value("website", "");
    c.group         = j.value("group", "");
    c.notes         = j.value("notes", "");
    c.totp_secret   = j.value("totp_secret", "");
    c.is_favorite   = j.value("is_favorite", false);
    c.is_pinned     = j.value("is_pinned", false);
    c.type          = static_cast<CredType>(j.value("type", 0));
    c.card_number     = j.value("card_number", "");
    c.card_expiry     = j.value("card_expiry", "");
    c.card_cvv        = j.value("card_cvv", "");
    c.card_brand      = j.value("card_brand", "");
    c.cardholder_name = j.value("cardholder_name", "");
    c.card_address    = j.value("card_address", "");
    c.card_city       = j.value("card_city", "");
    c.card_postal_code = j.value("card_postal_code", "");
    c.full_name     = j.value("full_name", "");
    c.id_type       = j.value("id_type", "");
    c.id_number     = j.value("id_number", "");
    c.date_of_birth = j.value("date_of_birth", "");
    c.expiry_date   = j.value("expiry_date", "");
    c.country       = j.value("country", "");
    c.address       = j.value("address", "");
    c.phone         = j.value("phone", "");
    c.created_at_ms = j.value("created_at_ms", int64_t(0));
    c.updated_at_ms = j.value("updated_at_ms", int64_t(0));

    for (const auto& h : j.value("password_history", nlohmann::json::array())) {
        Credential::PasswordHistoryEntry e;
        e.password      = h.value("password", "");
        e.changed_at_ms = h.value("changed_at_ms", int64_t(0));
        c.password_history.push_back(std::move(e));
    }

    return c;
}

// ---- helpers ----

static void write_u16_le(uint8_t* dst, uint16_t v)
{
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
}

static uint16_t read_u16_le(const uint8_t* src)
{
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static void write_u32_le(uint8_t* dst, uint32_t v)
{
    dst[0] = (uint8_t)(v & 0xFF);
    dst[1] = (uint8_t)((v >> 8) & 0xFF);
    dst[2] = (uint8_t)((v >> 16) & 0xFF);
    dst[3] = (uint8_t)((v >> 24) & 0xFF);
}

static uint32_t read_u32_le(const uint8_t* src)
{
    return (uint32_t)src[0]
        | ((uint32_t)src[1] << 8)
        | ((uint32_t)src[2] << 16)
        | ((uint32_t)src[3] << 24);
}

// ---- public API ----

ExportResult export_pwm(
    const std::vector<Credential>& creds,
    const std::string& export_password,
    const char* file_path)
{
    ExportResult res;

    // 1. Serialize credentials to JSON
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& c : creds)
        arr.push_back(cred_to_json(c));

    std::string plaintext = arr.dump();

    // 2. Generate salt & derive key
    auto salt = enc::generate_salt();  // 16 bytes
    auto key  = enc::derive_master_key(export_password, salt);
    if (key.empty()) {
        res.error = "Key derivation failed";
        return res;
    }

    // 3. Build header (first 50 bytes)
    std::vector<unsigned char> out(HEADER_SIZE);
    std::memcpy(out.data(), MAGIC, 4);                          // [0..3]  magic
    write_u16_le(out.data() + 4, FORMAT_VERSION);               // [4..5]  version
    std::memcpy(out.data() + 6, salt.data(), enc::SALT_SIZE);   // [6..21] salt
    write_u32_le(out.data() + 22, (uint32_t)creds.size());      // [22..25] cred_count

    // 4. Generate nonce
    uint8_t nonce[NONCE_SIZE];
    randombytes_buf(nonce, NONCE_SIZE);
    std::memcpy(out.data() + 26, nonce, NONCE_SIZE);            // [26..49] nonce

    // 5. Encrypt: XChaCha20-Poly1305 with AAD = bytes[0..5]
    size_t ct_len = plaintext.size() + TAG_SIZE;
    out.resize(HEADER_SIZE + ct_len);

    unsigned long long actual_ct_len = 0;
    int rc = crypto_aead_xchacha20poly1305_ietf_encrypt(
        out.data() + HEADER_SIZE,       // ciphertext destination
        &actual_ct_len,
        reinterpret_cast<const unsigned char*>(plaintext.data()),
        plaintext.size(),
        out.data(),                      // AAD = header[0..5]
        AAD_SIZE,
        nullptr,                         // nsec (unused)
        nonce,
        key.data());

    // Secure-zero sensitive data
    enc::secure_zero(key);
    sodium_memzero(plaintext.data(), plaintext.size());

    if (rc != 0) {
        res.error = "Encryption failed";
        return res;
    }

    out.resize(HEADER_SIZE + (size_t)actual_ct_len);

    // 6. Write to file
    if (!helpers::bytes_to_file(file_path, out)) {
        res.error = "Could not write file";
        return res;
    }

    res.ok = true;
    res.count = (int)creds.size();
    return res;
}

ImportResult import_pwm(
    const std::string& import_password,
    const std::string& file_path)
{
    ImportResult res;

    // 1. Read file
    auto data = helpers::file_to_vec(file_path);
    if (data.size() < HEADER_SIZE + TAG_SIZE) {
        res.error = "Not a valid .pwm file";
        return res;
    }

    // 2. Validate magic
    if (std::memcmp(data.data(), MAGIC, 4) != 0) {
        res.error = "Not a valid .pwm file";
        return res;
    }

    // 3. Check version
    uint16_t version = read_u16_le(data.data() + 4);
    if (version > FORMAT_VERSION) {
        res.error = "Unsupported format version";
        return res;
    }

    // 4. Extract fields
    std::vector<uint8_t> salt(data.data() + 6, data.data() + 6 + enc::SALT_SIZE);
    uint32_t cred_count = read_u32_le(data.data() + 22);
    const uint8_t* nonce = data.data() + 26;
    const uint8_t* ciphertext = data.data() + HEADER_SIZE;
    size_t ct_len = data.size() - HEADER_SIZE;

    if (cred_count > MAX_CRED_COUNT) {
        res.error = "File exceeds credential limit";
        return res;
    }

    // 5. Derive key
    auto key = enc::derive_master_key(import_password, salt);
    if (key.empty()) {
        res.error = "Key derivation failed";
        return res;
    }

    // 6. Decrypt
    std::vector<unsigned char> plaintext(ct_len - TAG_SIZE);
    unsigned long long pt_len = 0;

    int rc = crypto_aead_xchacha20poly1305_ietf_decrypt(
        plaintext.data(),
        &pt_len,
        nullptr,                         // nsec (unused)
        ciphertext,
        ct_len,
        data.data(),                     // AAD = header[0..5]
        AAD_SIZE,
        nonce,
        key.data());

    enc::secure_zero(key);

    if (rc != 0) {
        res.error = "Wrong password or corrupted file";
        return res;
    }

    // 7. Parse JSON
    std::string json_str(reinterpret_cast<const char*>(plaintext.data()), (size_t)pt_len);
    sodium_memzero(plaintext.data(), plaintext.size());

    try {
        auto arr = nlohmann::json::parse(json_str);
        sodium_memzero(json_str.data(), json_str.size());

        if (!arr.is_array()) {
            res.error = "Corrupted export data";
            return res;
        }

        res.creds.reserve(arr.size());
        for (const auto& j : arr)
            res.creds.push_back(json_to_cred(j));

        // Sanity check
        if (cred_count != 0 && res.creds.size() != (size_t)cred_count) {
            res.error = "Corrupted export data";
            res.creds.clear();
            return res;
        }

    } catch (...) {
        sodium_memzero(json_str.data(), json_str.size());
        res.error = "Corrupted export data";
        return res;
    }

    res.ok = true;
    res.count = (int)res.creds.size();
    return res;
}

} // namespace pwm_file
