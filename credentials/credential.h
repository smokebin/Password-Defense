// credential.h
// Credential data structure for SQLite-based storage
#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include "cred_type.h"

struct Credential
{
    // Identity
    std::string uuid;           // PRIMARY KEY - UUID v4 for sync identification
    int         id = 0;         // Display-only sequential ID (NOT stored in DB)

    // User data (encrypted in DB)
    std::string title;
    std::string password;
    std::string user;
    std::string email;
    std::string website;
    std::string group;
    std::vector<std::string> tags;     // Multiple tags per credential
    std::string notes;
    std::string totp_secret;          // Base32-encoded TOTP secret (empty = no TOTP)

    CredType type = CredType::Password;

    // Credit Card fields
    std::string card_number;
    std::string card_expiry;       // MM/YY
    std::string card_cvv;
    std::string card_brand;
    std::string cardholder_name;
    std::string card_address;       // Street address (billing)
    std::string card_city;          // City (billing)
    std::string card_postal_code;   // Postal/ZIP code (billing)

    // Identity fields
    std::string full_name;
    std::string id_type;           // Passport, Driver's License, SSN, etc.
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
    std::vector<PasswordHistoryEntry> password_history;  // max 5, newest first

    bool        is_favorite = false;
    bool        is_pinned = false;

    // Per-Credential timer
    int64_t     expires_at_ms = 0;  // 0 = no expiry, >0 = active timer, <0 = expired+flagged
    int         expiry_action = 0;  // 0 = auto-trash, 1 = flag only

    // Timestamps (Unix milliseconds UTC)
    int64_t     created_at_ms = 0;
    int64_t     updated_at_ms = 0;
    int64_t     deleted_at_ms = 0;  // 0 = not deleted, else Unix ms when deleted

    // Helper methods
    bool is_deleted() const { return deleted_at_ms != 0; }
};
