// credential.h — in-memory credential struct; encrypted blob in DB
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "cred_type.h"

struct Credential
{
    std::string uuid;           // UUID v4; primary key
    int         id = 0;         // sequential display ID, not stored in DB

    std::string title;
    std::string password;
    std::string user;
    std::string email;
    std::string website;
    std::string group;
    std::vector<std::string> tags;
    std::string notes;
    std::string totp_secret;          // base32; empty = no TOTP

    CredType type = CredType::Password;

    // credit card
    std::string card_number;
    std::string card_expiry;       // MM/YY
    std::string card_cvv;
    std::string card_brand;
    std::string cardholder_name;
    std::string card_address;       // billing
    std::string card_city;
    std::string card_postal_code;

    // identity
    std::string full_name;
    std::string id_type;           // Passport, Driver's License, SSN, …
    std::string id_number;
    std::string date_of_birth;
    std::string expiry_date;
    std::string country;
    std::string address;
    std::string phone;

    struct PasswordHistoryEntry {
        std::string password;
        int64_t     changed_at_ms = 0;
    };
    std::vector<PasswordHistoryEntry> password_history;  // newest first, max 5

    bool        is_favorite = false;
    bool        is_pinned = false;

    int64_t     expires_at_ms = 0;  // 0 = no expiry; <0 = expired+flagged
    int         expiry_action = 0;  // 0 = auto-trash, 1 = flag only

    // timestamps, Unix ms UTC
    int64_t     created_at_ms = 0;
    int64_t     updated_at_ms = 0;
    int64_t     deleted_at_ms = 0;  // 0 = live; nonzero = soft-deleted

    bool is_deleted() const { return deleted_at_ms != 0; }
};
