// credential_ops.cpp
// High-level Credential operations implementation

#include "credential_ops.h"
#include "vault_db.h"
#include "crypto/vault_crypto.h"
#include "../tools/utility.h"
#include "../third_party/json.hpp"

namespace cred_ops {

    // ============================================================
    // Internal helpers
    // ============================================================

    // Decrypt a database row into a Credential struct
    // Uses UUID as AAD to bind ciphertext to Credential identity
    // Sets needs_migration flag if legacy (no-AAD) fallback was used
    static Credential decrypt_row(const vault_db::CredentialRow& row, const std::vector<uint8_t>& key, bool* needs_migration = nullptr)
    {
        Credential c{};
        c.uuid = row.uuid;
        c.created_at_ms = row.created_at_ms;
        c.updated_at_ms = row.updated_at_ms;
        c.deleted_at_ms = row.deleted_at_ms;
        // Decrypt with UUID as AAD
        std::string json = enc::decrypt_credential(row.encrypted_blob, key, row.uuid);

        // If AAD decryption fails, try legacy (no AAD) for migration
        if (json.empty()) {
            json = enc::decrypt_credential_legacy(row.encrypted_blob, key);
            if (!json.empty() && needs_migration)
                *needs_migration = true;
        }

        if (json.empty()) return c;

        try {
            auto j = nlohmann::json::parse(json);
            c.title = j.value("title", "");
            c.email = j.value("email", "");
            c.user = j.value("user", "");
            c.password = j.value("password", "");
            c.website = j.value("website", "");
            c.group = j.value("group", "");
            c.notes = j.value("notes", "");
            c.totp_secret = j.value("totp_secret", "");
            c.is_favorite = j.value("is_favorite", false);
            c.is_pinned = j.value("is_pinned", false);
            c.expires_at_ms = j.value("expires_at_ms", int64_t(0));
            c.expiry_action = j.value("expiry_action", 0);

            c.type = static_cast<CredType>(j.value("type", 0));

            // Credit Card
            c.card_number     = j.value("card_number", "");
            c.card_expiry     = j.value("card_expiry", "");
            c.card_cvv        = j.value("card_cvv", "");
            c.card_brand      = j.value("card_brand", "");
            c.cardholder_name = j.value("cardholder_name", "");
            c.card_address    = j.value("card_address", "");
            c.card_city       = j.value("card_city", "");
            c.card_postal_code = j.value("card_postal_code", "");

            // Identity
            c.full_name     = j.value("full_name", "");
            c.id_type       = j.value("id_type", "");
            c.id_number     = j.value("id_number", "");
            c.date_of_birth = j.value("date_of_birth", "");
            c.expiry_date   = j.value("expiry_date", "");
            c.country       = j.value("country", "");
            c.address       = j.value("address", "");
            c.phone         = j.value("phone", "");

            // Tags (backward compat: absent in old blobs)
            for (const auto& t : j.value("tags", nlohmann::json::array())) {
                if (t.is_string() && !t.get<std::string>().empty())
                    c.tags.push_back(t.get<std::string>());
            }

            // Password history (backward compat: absent in old blobs)
            for (const auto& h : j.value("password_history", nlohmann::json::array())) {
                Credential::PasswordHistoryEntry e;
                e.password      = h.value("password", "");
                e.changed_at_ms = h.value("changed_at_ms", int64_t(0));
                c.password_history.push_back(std::move(e));
            }
        }
        catch (...) {
            // JSON parse failed, return partial Credential
        }

        // Clear sensitive data from json string
        enc::secure_zero(json);

        return c;
    }

    // Encrypt a Credential struct into a blob
    // Uses UUID as AAD to bind ciphertext to Credential identity
    static std::vector<uint8_t> encrypt_cred(const Credential& c, const std::vector<uint8_t>& key, const std::string& uuid)
    {
        nlohmann::json j = {
            {"title", c.title},
            {"email", c.email},
            {"user", c.user},
            {"password", c.password},
            {"website", c.website},
            {"group", c.group},
            {"notes", c.notes},
            {"totp_secret", c.totp_secret},
            {"is_favorite", c.is_favorite},
            {"is_pinned", c.is_pinned},
            {"expires_at_ms", c.expires_at_ms},
            {"expiry_action", c.expiry_action},
            {"type", static_cast<int>(c.type)},
            {"card_number", c.card_number},
            {"card_expiry", c.card_expiry},
            {"card_cvv", c.card_cvv},
            {"card_brand", c.card_brand},
            {"cardholder_name", c.cardholder_name},
            {"card_address", c.card_address},
            {"card_city", c.card_city},
            {"card_postal_code", c.card_postal_code},
            {"full_name", c.full_name},
            {"id_type", c.id_type},
            {"id_number", c.id_number},
            {"date_of_birth", c.date_of_birth},
            {"expiry_date", c.expiry_date},
            {"country", c.country},
            {"address", c.address},
            {"phone", c.phone},
            {"tags", nlohmann::json(c.tags)},
            {"password_history", nlohmann::json::array()}
        };

        for (const auto& h : c.password_history) {
            j["password_history"].push_back({
                {"password", h.password},
                {"changed_at_ms", h.changed_at_ms}
            });
        }

        std::string json_str = j.dump();
        auto blob = enc::encrypt_credential(json_str, key, uuid);  // UUID as AAD

        // Clear sensitive data
        enc::secure_zero(json_str);

        return blob;
    }

    // ============================================================
    // Load operations
    // ============================================================

    std::vector<Credential> load_all(const std::vector<uint8_t>& master_key)
    {
        std::vector<Credential> results;

        if (master_key.empty() || !vault_db::is_open())
            return results;

        auto rows = vault_db::get_all_credentials();
        std::vector<std::string> migrate_uuids;  // legacy no-AAD blobs to re-encrypt

        for (const auto& row : rows) {
            bool needs_migration = false;
            Credential c = decrypt_row(row, master_key, &needs_migration);

            // Only include if decryption succeeded (has some content)
            if (!c.title.empty() || !c.password.empty() || !c.user.empty()
                || !c.card_number.empty() || !c.id_number.empty() || !c.notes.empty()) {
                if (needs_migration && !c.uuid.empty())
                    migrate_uuids.push_back(c.uuid);
                results.push_back(std::move(c));
            }
        }

        // Force-migrate legacy no-AAD blobs: re-encrypt with UUID as AAD
        if (!migrate_uuids.empty()) {
            for (const auto& uuid : migrate_uuids) {
                for (const auto& c : results) {
                    if (c.uuid == uuid) {
                        update(uuid, c, master_key);
                        break;
                    }
                }
            }
        }

        // Assign sequential IDs for UI display (NOT for identity - use UUID)
        int display_id = 1;
        for (auto& c : results) {
            c.id = display_id++;
        }

        return results;
    }

    std::vector<Credential> load_deleted(const std::vector<uint8_t>& master_key)
    {
        std::vector<Credential> results;

        if (master_key.empty() || !vault_db::is_open())
            return results;

        auto rows = vault_db::get_deleted_credentials();

        for (const auto& row : rows) {
            Credential c = decrypt_row(row, master_key);

            if (!c.title.empty() || !c.password.empty() || !c.user.empty()
                || !c.card_number.empty() || !c.id_number.empty() || !c.notes.empty()) {
                results.push_back(std::move(c));
            }
        }

        return results;
    }

    // ============================================================
    // CRUD operations
    // ============================================================

    std::string add(const Credential& c, const std::vector<uint8_t>& master_key)
    {
        if (master_key.empty() || !vault_db::is_open())
            return "";

        std::string uuid = c.uuid.empty() ? helpers::generate_uuid() : c.uuid;
        int64_t now_ms = helpers::now_unix_ms();

        auto blob = encrypt_cred(c, master_key, uuid);  // Encrypt with UUID as AAD
        if (blob.empty())
            return "";

        if (!vault_db::insert_credential(uuid, blob, now_ms, now_ms))
            return "";

        return uuid;
    }

    std::vector<uint8_t> encrypt_cred_public(const Credential& c, const std::vector<uint8_t>& key, const std::string& uuid)
    {
        return encrypt_cred(c, key, uuid);
    }

    bool update(const std::string& uuid, const Credential& c, const std::vector<uint8_t>& master_key)
    {
        if (uuid.empty() || master_key.empty() || !vault_db::is_open())
            return false;

        int64_t now_ms = helpers::now_unix_ms();

        auto blob = encrypt_cred(c, master_key, uuid);  // Encrypt with UUID as AAD
        if (blob.empty())
            return false;

        return vault_db::update_credential(uuid, blob, now_ms);
    }

    bool remove(const std::string& uuid)
    {
        if (uuid.empty() || !vault_db::is_open())
            return false;

        return vault_db::soft_delete_credential(uuid);
    }

    bool remove_permanent(const std::string& uuid)
    {
        if (uuid.empty() || !vault_db::is_open())
            return false;

        return vault_db::hard_delete_credential(uuid);
    }

    // ============================================================
    // Batch operations
    // ============================================================

    bool import_credentials(
        const std::vector<Credential>& creds,
        const std::vector<uint8_t>& master_key)
    {
        if (master_key.empty() || !vault_db::is_open())
            return false;

        vault_db::begin_transaction();

        int64_t now_ms = helpers::now_unix_ms();

        for (const auto& c : creds) {
            std::string uuid = c.uuid.empty() ? helpers::generate_uuid() : c.uuid;

            // Use existing timestamps or generate new ones
            int64_t created_ms = (c.created_at_ms != 0) ? c.created_at_ms : now_ms;
            int64_t updated_ms = (c.updated_at_ms != 0) ? c.updated_at_ms : created_ms;

            auto blob = encrypt_cred(c, master_key, uuid);  // Encrypt with UUID as AAD
            if (blob.empty()) {
                vault_db::rollback_transaction();
                return false;
            }

            if (!vault_db::insert_credential(uuid, blob, created_ms, updated_ms)) {
                vault_db::rollback_transaction();
                return false;
            }
        }

        vault_db::commit_transaction();
        return true;
    }

    // ============================================================
    // Utility
    // ============================================================

    bool has_pending_changes()
    {
        return vault_db::count_dirty() > 0;
    }

    int count()
    {
        return vault_db::count_credentials();
    }

} // namespace cred_ops
