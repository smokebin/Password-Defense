// credential_ops.cpp

#include "credential_ops.h"
#include "vault_db.h"
#include "crypto/vault_crypto.h"
#include "../tools/utility.h"
#include "../third_party/json.hpp"

#include <algorithm>

namespace cred_ops {

    // zero the secret fields of a decrypted credential we're about to discard
    static void scrub(Credential& c)
    {
        for (std::string* s : { &c.password, &c.user, &c.email, &c.notes, &c.totp_secret,
                                &c.card_number, &c.card_cvv, &c.card_expiry, &c.cardholder_name,
                                &c.card_address, &c.card_city, &c.card_postal_code, &c.id_number,
                                &c.phone, &c.address, &c.date_of_birth })
            enc::secure_zero(*s);
        for (auto& h : c.password_history) enc::secure_zero(h.password);
    }

    nlohmann::json to_json(const Credential& c)
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

        // Cards: also write the billing street address under the old "address"
        // key, not just "card_address". A vault file exported from here and
        // imported into an app that predates card_address (mobile, web) still
        // finds the value, and won't drop it. "address" is unused on cards so
        // there's nothing to clobber. The from_json fold below reads it back.
        // Drop this once every app knows card_address.
        if (c.type == CredType::CreditCard && !c.card_address.empty()) {
            j["address"] = c.card_address;
        }

        return j;
    }

    void from_json(const nlohmann::json& j, Credential& c)
    {
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

        // Old cards stored the billing address under "address" before
        // card_address existed. Fold it over when the new field is empty.
        // card_address wins when it's set; otherwise fall back to address.
        // Don't erase address — apps that predate card_address still read it,
        // and they'd drop card_address on save, losing it everywhere.
        if (c.type == CredType::CreditCard
            && c.card_address.empty()
            && !c.address.empty())
        {
            c.card_address = c.address;
        }

        // absent in old blobs — default to empty
        c.tags.clear();
        for (const auto& t : j.value("tags", nlohmann::json::array())) {
            if (t.is_string() && !t.get<std::string>().empty())
                c.tags.push_back(t.get<std::string>());
        }

        c.password_history.clear();
        for (const auto& h : j.value("password_history", nlohmann::json::array())) {
            Credential::PasswordHistoryEntry e;
            e.password      = h.value("password", "");
            e.changed_at_ms = h.value("changed_at_ms", int64_t(0));
            c.password_history.push_back(std::move(e));
        }
    }

    // UUID is AAD; needs_migration set if legacy no-AAD fallback succeeded.
    // ok is set true only if the blob authenticated AND its JSON parsed.
    static Credential decrypt_row(const vault_db::CredentialRow& row, const std::vector<uint8_t>& key, bool* needs_migration = nullptr, bool* ok = nullptr)
    {
        if (ok) *ok = false;
        Credential c{};
        c.uuid = row.uuid;
        c.created_at_ms = row.created_at_ms;
        c.updated_at_ms = row.updated_at_ms;
        c.deleted_at_ms = row.deleted_at_ms;
        std::string json = enc::decrypt_credential(row.encrypted_blob, key, row.uuid);

        // fallback for older blobs written without AAD
        if (json.empty()) {
            json = enc::decrypt_credential_legacy(row.encrypted_blob, key);
            if (!json.empty() && needs_migration)
                *needs_migration = true;
        }

        if (json.empty()) return c;

        try {
            auto j = nlohmann::json::parse(json);
            from_json(j, c);   // content fields; uuid/timestamps already set from the row
            if (ok) *ok = true;
        }
        catch (...) {}

        enc::secure_zero(json);

        return c;
    }

    static std::vector<uint8_t> encrypt_cred(const Credential& c, const std::vector<uint8_t>& key, const std::string& uuid)
    {
        std::string json_str = to_json(c).dump();
        auto blob = enc::encrypt_credential(json_str, key, uuid);  // UUID as AAD
        enc::secure_zero(json_str);

        return blob;
    }

    std::vector<Credential> load_all(const std::vector<uint8_t>& master_key)
    {
        std::vector<Credential> results;

        if (master_key.empty() || !vault_db::is_open())
            return results;

        auto rows = vault_db::get_all_credentials();
        std::vector<std::string> migrate_uuids;  // blobs that need re-encryption with AAD

        for (const auto& row : rows) {
            if (row.encrypted_blob.empty() || row.is_deleted()) continue;
            bool needs_migration = false;
            bool ok = false;
            Credential c = decrypt_row(row, master_key, &needs_migration, &ok);

            // include on real decrypt success, not "has some non-empty field": a valid
            // entry with only e.g. website+email used to be dropped here, and save then
            // hard-deleted it as an orphan
            if (ok) {
                if (needs_migration && !c.uuid.empty())
                    migrate_uuids.push_back(c.uuid);
                results.push_back(std::move(c));
            }
        }

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
            if (row.encrypted_blob.empty()) continue; // Skip permanent tombstones
            bool ok = false;
            Credential c = decrypt_row(row, master_key, nullptr, &ok);

            if (ok) {
                results.push_back(std::move(c));
            }
        }

        return results;
    }

    std::vector<std::string> select_hard_deletes(
        const std::vector<std::string>& live_db_uuids,
        const std::unordered_set<std::string>& memory_uuids,
        const std::unordered_set<std::string>& user_removed)
    {
        std::vector<std::string> out;
        for (const auto& uuid : live_db_uuids)
        {
            if (memory_uuids.count(uuid)) continue;     // still (or again) in the vault
            if (!user_removed.count(uuid)) continue;    // not an explicit user removal: leave it
            out.push_back(uuid);
        }
        return out;
    }

    SaveResult save_all(std::vector<Credential>& creds,
                        const std::vector<uint8_t>& master_key,
                        const std::unordered_set<std::string>& user_removed,
                        const std::unordered_set<std::string>& purged)
    {
        SaveResult res;
        if (master_key.empty() || !vault_db::is_open()) {
            res.error = "Missing vault or encryption key.";
            return res;
        }
        if (!vault_db::begin_transaction()) {
            res.error = "Save failed: couldn't start a database transaction.";
            return res;
        }

        const auto existing_rows = vault_db::get_all_credentials();
        const auto deleted_rows  = vault_db::get_deleted_credentials();  // detect undo-restored entries

        std::unordered_set<std::string> soft_deleted_uuids;
        for (const auto& row : deleted_rows) soft_deleted_uuids.insert(row.uuid);

        // live + trashed rows: a non-empty in-memory uuid in neither has no row to
        // UPDATE (an UPDATE would match 0 rows and "succeed"), so it's inserted
        std::unordered_set<std::string> db_uuids = soft_deleted_uuids;
        for (const auto& row : existing_rows) db_uuids.insert(row.uuid);

        std::unordered_set<std::string> memory_uuids;

        // UUIDs handed out during this save. A rollback discards their rows, so they
        // are cleared again; otherwise the next save would UPDATE nonexistent rows.
        std::vector<size_t> assigned_idx;
        auto fail = [&](const char* msg) {
            vault_db::rollback_transaction();
            for (size_t i : assigned_idx) creds[i].uuid.clear();
            res.ok = false;
            res.error = msg;
            return res;
        };

        for (size_t i = 0; i < creds.size(); ++i)
        {
            Credential& c = creds[i];
            const bool has_row = !c.uuid.empty() && db_uuids.count(c.uuid);

            if (!has_row)
            {
                // permanently deleted by the user this session (e.g. brought back by
                // undo after "Delete permanently"): never write it back
                if (!c.uuid.empty() && purged.count(c.uuid)) continue;

                const bool had_uuid = !c.uuid.empty();
                std::string new_uuid = add(c, master_key);
                if (new_uuid.empty())
                    return fail("Save failed: encryption or database error.");
                c.uuid = new_uuid;
                if (!had_uuid) assigned_idx.push_back(i);
                memory_uuids.insert(new_uuid);
            }
            else
            {
                if (soft_deleted_uuids.count(c.uuid) && !vault_db::restore_credential(c.uuid))
                    return fail("Save failed: encryption or database error.");  // undo brought it back

                memory_uuids.insert(c.uuid);
                auto blob = encrypt_cred(c, master_key, c.uuid);
                int64_t ts = (c.updated_at_ms != 0) ? c.updated_at_ms : helpers::now_unix_ms();
                if (blob.empty() || !vault_db::update_credential(c.uuid, blob, ts))
                    return fail("Save failed: encryption or database error.");
            }
        }

        // Hard-delete only rows the user explicitly removed this session (undo of a
        // saved add): an explicit record, not an inference. Any other live row missing
        // from memory - undecryptable, decrypts but fails to parse (newer schema,
        // malformed JSON), or added by another device - is left exactly as it is.
        {
            std::vector<std::string> live_db_uuids;
            live_db_uuids.reserve(existing_rows.size());
            for (const auto& row : existing_rows) live_db_uuids.push_back(row.uuid);

            for (const auto& uuid : select_hard_deletes(live_db_uuids, memory_uuids, user_removed))
            {
                if (!vault_db::hard_delete_credential(uuid))
                    return fail("Save failed: database error while removing entries.");
                res.hard_deleted++;
            }
        }

        // legacy vault that couldn't be verified at open (it was empty): stamp the
        // key-check now, so it matches the key this content is encrypted under
        if (!creds.empty() && vault_db::get_key_check().empty())
            vault_db::set_key_check(enc::make_key_check(master_key));

        if (!vault_db::commit_transaction())
            return fail("Save failed: couldn't commit to the database.");

        res.ok = true;
        return res;
    }

    void reconcile_after_undo(const std::vector<Credential>& before,
                              std::vector<Credential>& after,
                              std::unordered_set<std::string>& user_removed,
                              const std::unordered_set<std::string>& purged)
    {
        // never resurrect something the user permanently deleted from Trash
        if (!purged.empty())
        {
            after.erase(std::remove_if(after.begin(), after.end(),
                [&](const Credential& c) { return !c.uuid.empty() && purged.count(c.uuid); }),
                after.end());
        }

        std::unordered_set<std::string> dropped;
        for (const auto& c : before)
            if (!c.uuid.empty()) dropped.insert(c.uuid);

        for (const auto& c : after)
        {
            if (c.uuid.empty()) continue;
            dropped.erase(c.uuid);
            user_removed.erase(c.uuid);   // back in memory: no longer removed
        }
        for (const auto& uuid : dropped)
            user_removed.insert(uuid);
    }

#ifdef PD_OFFLINE_BUILD
    int purge_trash_older_than(const std::vector<uint8_t>& master_key, int64_t older_than_ms,
                               int* out_kept_unreadable)
    {
        if (out_kept_unreadable) *out_kept_unreadable = 0;
        if (master_key.empty() || !vault_db::is_open()) return 0;

        int purged = 0, kept = 0;
        if (!vault_db::begin_transaction()) return 0;

        for (const auto& row : vault_db::get_deleted_credentials())
        {
            if (row.deleted_at_ms == 0 || row.deleted_at_ms >= older_than_ms) continue;

            // only rows we can actually read (decrypt + parse). A trashed row we can't
            // read may be the only copy of data written under another key or a newer
            // schema; age alone is never a reason to destroy it.
            bool ok = false;
            Credential c = decrypt_row(row, master_key, nullptr, &ok);
            scrub(c);
            if (!ok) { kept++; continue; }

            if (!vault_db::hard_delete_trashed_before(row.uuid, older_than_ms)) {
                vault_db::rollback_transaction();
                return 0;
            }
            purged++;
        }

        if (!vault_db::commit_transaction()) {
            vault_db::rollback_transaction();
            return 0;
        }
        if (out_kept_unreadable) *out_kept_unreadable = kept;
        return purged;
    }
#endif

    std::string add(const Credential& c, const std::vector<uint8_t>& master_key)
    {
        if (master_key.empty() || !vault_db::is_open())
            return "";

        std::string uuid = c.uuid.empty() ? helpers::generate_uuid() : c.uuid;
        int64_t created_ms = (c.created_at_ms != 0) ? c.created_at_ms : helpers::now_unix_ms();
        int64_t updated_ms = (c.updated_at_ms != 0) ? c.updated_at_ms : helpers::now_unix_ms();

        auto blob = encrypt_cred(c, master_key, uuid);
        if (blob.empty())
            return "";

        if (!vault_db::insert_credential(uuid, blob, created_ms, updated_ms))
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

        int64_t now_ms = (c.updated_at_ms != 0) ? c.updated_at_ms : helpers::now_unix_ms();

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

        if (!vault_db::commit_transaction()) {
            vault_db::rollback_transaction();
            if (out_error) *out_error = "commit failed";
            return false;
        }

        if (out_inserted) *out_inserted = inserted_count;
        if (out_updated)  *out_updated  = updated_count;
        if (out_skipped)  *out_skipped  = skipped_count;
        return true;
    }

    ImportFlowResult import_flow(const std::function<bool()>& flush,
                                 const std::vector<Credential>& incoming,
                                 const std::vector<uint8_t>& master_key,
                                 const std::function<void()>& reload)
    {
        ImportFlowResult res;
        if (!flush || !flush()) {
            // the reload would replace memory with disk and drop the unsaved edits
            res.flush_failed = true;
            res.error = "Couldn't save your pending changes, so nothing was imported. "
                        "Resolve the save problem first.";
            return res;
        }
        std::string err;
        if (!import_credentials(incoming, master_key, &res.inserted, &res.updated, &res.skipped, &err)) {
            res.error = err.empty() ? std::string("Database error during import")
                                    : ("Database error during import: " + err);
            return res;   // rolled back; memory already matches disk (just flushed)
        }
        if (reload) reload();
        res.ok = true;
        return res;
    }

    int count_unreadable_trashed(const std::vector<uint8_t>& master_key)
    {
        if (master_key.empty() || !vault_db::is_open()) return 0;
        int n = 0;
        for (const auto& row : vault_db::get_deleted_credentials())
        {
            if (row.encrypted_blob.empty()) continue; // permanent tombstone with wiped blob is not unreadable
            bool ok = false;
            Credential c = decrypt_row(row, master_key, nullptr, &ok);
            scrub(c);
            if (!ok) n++;
        }
        return n;
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
