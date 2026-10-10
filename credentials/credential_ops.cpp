// credential_ops.cpp

#include "credential_ops.h"
#include "vault_db.h"
#include "crypto/vault_crypto.h"
#include "../tools/utility.h"
#include "../third_party/json.hpp"

namespace cred_ops {

    // UUID is AAD. Pre-AAD rows are upgraded by migrate_legacy_rows(), not here.
    static Credential decrypt_row(const vault_db::CredentialRow& row, const std::vector<uint8_t>& key)
    {
        Credential c{};
        c.uuid = row.uuid;
        c.created_at_ms = row.created_at_ms;
        c.updated_at_ms = row.updated_at_ms;
        c.deleted_at_ms = row.deleted_at_ms;
        std::string json = enc::decrypt_credential(row.encrypted_blob, key, row.uuid);

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

            // absent in old blobs — use default empty array
            for (const auto& t : j.value("tags", nlohmann::json::array())) {
                if (t.is_string() && !t.get<std::string>().empty())
                    c.tags.push_back(t.get<std::string>());
            }

            // same — absent in old blobs
            for (const auto& h : j.value("password_history", nlohmann::json::array())) {
                Credential::PasswordHistoryEntry e;
                e.password      = h.value("password", "");
                e.changed_at_ms = h.value("changed_at_ms", int64_t(0));
                c.password_history.push_back(std::move(e));
            }
        }
        catch (...) {}

        enc::secure_zero(json);

        return c;
    }

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
        enc::secure_zero(json_str);

        return blob;
    }

    // Fixed plaintext, bound to its own AAD so the blob can't pass as a credential.
    static constexpr const char* kKeyCheckPlain = "vault-key-check-v1";
    static constexpr const char* kKeyCheckAAD   = "vault-key-check";
    static constexpr const char* kKeyCheckMeta  = "key_check";

    KeyCheck check_master_key(const std::vector<uint8_t>& master_key)
    {
        const std::string hex = vault_db::get_meta(kKeyCheckMeta);
        if (hex.empty())
            return KeyCheck::Missing;

        const std::vector<uint8_t> blob = enc::hex_to_bytes(hex);
        std::string plain = enc::decrypt_credential(blob, master_key, kKeyCheckAAD);
        const bool ok = (plain == kKeyCheckPlain);
        enc::secure_zero(plain);

        return ok ? KeyCheck::Match : KeyCheck::Mismatch;
    }

    bool store_master_key_check(const std::vector<uint8_t>& master_key)
    {
        auto blob = enc::encrypt_credential(kKeyCheckPlain, master_key, kKeyCheckAAD);
        if (blob.empty())
            return false;

        return vault_db::set_meta(kKeyCheckMeta, enc::bytes_to_hex(blob));
    }

    void migrate_legacy_rows(const std::vector<uint8_t>& master_key)
    {
        if (master_key.empty() || !vault_db::is_open())
            return;

        // Trashed rows are never loaded into the UI, so they have to be handled here
        // or they would keep the no-AAD format indefinitely.
        auto rows = vault_db::get_all_credentials();
        auto trashed = vault_db::get_deleted_credentials();
        rows.insert(rows.end(), trashed.begin(), trashed.end());

        for (const auto& row : rows) {
            std::string current = enc::decrypt_credential(row.encrypted_blob, master_key, row.uuid);
            const bool already_current = !current.empty();
            enc::secure_zero(current);
            if (already_current)
                continue;

            // Authenticated under this key, so it's ours. Neither format matching means
            // the row is left as-is, same as before.
            std::string json = enc::decrypt_credential_legacy(row.encrypted_blob, master_key);
            if (json.empty())
                continue;

            auto blob = enc::encrypt_credential(json, master_key, row.uuid);
            enc::secure_zero(json);
            if (!blob.empty())
                vault_db::set_credential_blob(row.uuid, blob);
        }
    }

    std::vector<Credential> load_all(const std::vector<uint8_t>& master_key)
    {
        std::vector<Credential> results;

        if (master_key.empty() || !vault_db::is_open())
            return results;

        auto rows = vault_db::get_all_credentials();

        for (const auto& row : rows) {
            Credential c = decrypt_row(row, master_key);

            if (!c.title.empty() || !c.password.empty() || !c.user.empty()
                || !c.card_number.empty() || !c.id_number.empty() || !c.notes.empty()) {
                results.push_back(std::move(c));
            }
        }

        int display_id = 1;  // display-only; identity is always uuid
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

    std::string add(const Credential& c, const std::vector<uint8_t>& master_key)
    {
        if (master_key.empty() || !vault_db::is_open())
            return "";

        std::string uuid = c.uuid.empty() ? helpers::generate_uuid() : c.uuid;
        int64_t now_ms = helpers::now_unix_ms();

        auto blob = encrypt_cred(c, master_key, uuid);
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

        auto blob = encrypt_cred(c, master_key, uuid);
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

    bool import_credentials(
        const std::vector<Credential>& creds,
        const std::vector<uint8_t>& master_key,
        int* out_inserted,
        int* out_updated,
        int* out_skipped,
        std::string* out_error)
    {
        if (out_inserted) *out_inserted = 0;
        if (out_updated)  *out_updated  = 0;
        if (out_skipped)  *out_skipped  = 0;

        if (master_key.empty() || !vault_db::is_open()) {
            if (out_error) *out_error = "vault not open or master key missing";
            return false;
        }

        vault_db::begin_transaction();

        int64_t now_ms = helpers::now_unix_ms();
        int inserted_count = 0;
        int updated_count  = 0;
        int skipped_count  = 0;

        for (const auto& c : creds) {
            std::string uuid = c.uuid.empty() ? helpers::generate_uuid() : c.uuid;

            int64_t created_ms = (c.created_at_ms != 0) ? c.created_at_ms : now_ms;
            int64_t updated_ms = (c.updated_at_ms != 0) ? c.updated_at_ms : created_ms;

            auto blob = encrypt_cred(c, master_key, uuid);
            if (blob.empty()) {
                vault_db::rollback_transaction();
                if (out_error) *out_error = "encrypt failed for one of the imported credentials";
                return false;
            }

            vault_db::UpsertOutcome outcome = vault_db::UpsertOutcome::Inserted;
            std::string sql_err;
            if (!vault_db::upsert_credential(uuid, blob, created_ms, updated_ms, &outcome, &sql_err)) {
                vault_db::rollback_transaction();
                if (out_error) *out_error = sql_err.empty() ? "upsert failed" : sql_err;
                return false;
            }
            switch (outcome) {
                case vault_db::UpsertOutcome::Inserted: inserted_count++; break;
                case vault_db::UpsertOutcome::Updated:  updated_count++;  break;
                case vault_db::UpsertOutcome::Skipped:  skipped_count++;  break;
            }
        }

        vault_db::commit_transaction();

        if (out_inserted) *out_inserted = inserted_count;
        if (out_updated)  *out_updated  = updated_count;
        if (out_skipped)  *out_skipped  = skipped_count;
        return true;
    }

    bool has_pending_changes()
    {
        return vault_db::count_dirty() > 0;
    }

    int count()
    {
        return vault_db::count_credentials();
    }

} // namespace cred_ops
