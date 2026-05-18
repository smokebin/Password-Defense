// app_sharing.cpp
// Anonymous sharing helpers: base64, credential serialization.

#include <string>
#include <vector>
#include <cstdint>
#include <sodium.h>
#include "app_internal.h"
#include "third_party/json.hpp"

// ============================================================
// Base64 helpers (using libsodium)
// ============================================================

// Base64 encode (standard, using libsodium)
std::string Anonbase64_encode(const std::vector<uint8_t>& data) {
    if (data.empty()) return "";
    size_t b64_len = sodium_base64_encoded_len(data.size(), sodium_base64_VARIANT_ORIGINAL);
    std::string out(b64_len, '\0');
    sodium_bin2base64(&out[0], b64_len, data.data(), data.size(), sodium_base64_VARIANT_ORIGINAL);
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

std::vector<uint8_t> base64_decode(const std::string& b64) {
    if (b64.empty()) return {};
    std::vector<uint8_t> out(b64.size());
    size_t bin_len = 0;
    int rc = sodium_base642bin(
        out.data(), out.size(),
        b64.c_str(), b64.size(),
        nullptr, &bin_len, nullptr,
        sodium_base64_VARIANT_ORIGINAL);
    if (rc != 0) return {};
    out.resize(bin_len);
    return out;
}

// Base64url encode (no padding, using libsodium)
std::string base64_url_encode(const std::vector<uint8_t>& data) {
    if (data.empty()) return "";
    size_t b64_len = sodium_base64_encoded_len(data.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    std::string out(b64_len, '\0');
    sodium_bin2base64(&out[0], b64_len, data.data(), data.size(), sodium_base64_VARIANT_URLSAFE_NO_PADDING);
    while (!out.empty() && out.back() == '\0') out.pop_back();
    return out;
}

// ============================================================
// Credential serialization for sharing
// ============================================================

// Serialize Credential for sharing (user-facing fields only, no UUIDs/sync state)
std::string serialize_credential_for_share(const Credential& c, const std::string& pw) {
    nlohmann::json j;
    j["title"] = c.title;
    j["type"] = static_cast<int>(c.type);

    switch (c.type) {
    case CredType::Password:
        j["user"] = c.user;
        j["email"] = c.email;
        j["password"] = pw;
        j["website"] = c.website;
        j["totp_secret"] = c.totp_secret;
        j["notes"] = c.notes;
        break;
    case CredType::CreditCard:
        j["cardholder_name"] = c.cardholder_name;
        j["card_number"] = c.card_number;
        j["card_expiry"] = c.card_expiry;
        j["card_cvv"] = c.card_cvv;
        j["card_brand"] = c.card_brand;
        j["card_address"] = c.card_address;
        j["card_city"] = c.card_city;
        j["card_postal_code"] = c.card_postal_code;
        j["notes"] = c.notes;
        break;
    case CredType::Identity:
        j["full_name"] = c.full_name;
        j["id_type"] = c.id_type;
        j["id_number"] = c.id_number;
        j["date_of_birth"] = c.date_of_birth;
        j["expiry_date"] = c.expiry_date;
        j["country"] = c.country;
        j["address"] = c.address;
        j["phone"] = c.phone;
        j["notes"] = c.notes;
        break;
    case CredType::SecureNote:
        j["notes"] = c.notes;
        break;
    }
    return j.dump();
}
