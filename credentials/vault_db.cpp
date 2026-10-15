// vault_db.cpp

#include "vault_db.h"
#include "crypto/vault_crypto.h"
#include "../tools/utility.h"

#include <sqlite3.h>
#define SODIUM_STATIC
#include <sodium.h>
// sqlite3.c compiled directly into the project

#include <cstring>

namespace vault_db {

    static sqlite3* g_db = nullptr;
    static std::string g_db_path;

    static bool exec(const char* sql)
    {
        if (!g_db) return false;
        char* err = nullptr;
        int rc = sqlite3_exec(g_db, sql, nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            if (err) sqlite3_free(err);
            return false;
        }
        return true;
    }

    // SQLITE_TRANSIENT copies the data before returning, safe for C++ temporaries
    static void bind_text(sqlite3_stmt* stmt, int idx, const std::string& val) {
        sqlite3_bind_text(stmt, idx, val.c_str(), static_cast<int>(val.size()), SQLITE_TRANSIENT);
    }

    static void bind_blob(sqlite3_stmt* stmt, int idx, const std::vector<uint8_t>& val) {
        static const uint8_t dummy = 0;
        const void* ptr = val.empty() ? static_cast<const void*>(&dummy) : static_cast<const void*>(val.data());
        sqlite3_bind_blob(stmt, idx, ptr, static_cast<int>(val.size()), SQLITE_TRANSIENT);
    }

    static void bind_int64(sqlite3_stmt* stmt, int idx, int64_t val) {
        sqlite3_bind_int64(stmt, idx, val);
    }

    bool init(const std::string& db_path)
    {
        if (g_db) close();

        int rc = sqlite3_open(db_path.c_str(), &g_db);
        if (rc != SQLITE_OK) {
            g_db = nullptr;
            return false;
        }

        g_db_path = db_path;

        exec("PRAGMA journal_mode=WAL");
        exec("PRAGMA synchronous=FULL");
        exec("PRAGMA foreign_keys=ON");
        exec("PRAGMA secure_delete=ON");


        const char* schema = R"(
            CREATE TABLE IF NOT EXISTS pm_credentials (
                uuid TEXT PRIMARY KEY,
                encrypted_blob BLOB NOT NULL,

                -- Timestamps (Unix milliseconds UTC)
                created_at_ms INTEGER NOT NULL,
                updated_at_ms INTEGER NOT NULL,
                deleted_at_ms INTEGER DEFAULT NULL,

                -- Sync state
                server_rev INTEGER DEFAULT 0,
                is_dirty INTEGER DEFAULT 1
            );

            CREATE INDEX IF NOT EXISTS idx_cred_dirty
                ON pm_credentials(is_dirty) WHERE is_dirty = 1;
            CREATE INDEX IF NOT EXISTS idx_cred_deleted
                ON pm_credentials(deleted_at_ms) WHERE deleted_at_ms IS NOT NULL;
            CREATE INDEX IF NOT EXISTS idx_cred_server_rev
                ON pm_credentials(server_rev);

            CREATE TABLE IF NOT EXISTS pm_sync_state (
                key TEXT PRIMARY KEY,
                value TEXT NOT NULL
            );

            CREATE TABLE IF NOT EXISTS pm_vault_2fa (
                id INTEGER PRIMARY KEY CHECK(id=1),
                encrypted_blob BLOB NOT NULL,
                created_at_ms INTEGER NOT NULL,
                updated_at_ms INTEGER NOT NULL
            );

            CREATE TABLE IF NOT EXISTS pm_vault_recovery (
                id INTEGER PRIMARY KEY CHECK(id=1),
                encrypted_blob BLOB NOT NULL,
                created_at_ms INTEGER NOT NULL
            );
        )";

        return exec(schema);
    }

    void close()
    {
        if (g_db) {
            sqlite3_close(g_db);
            g_db = nullptr;
            g_db_path.clear();
        }
    }

    bool checkpoint_truncate()
    {
        if (!g_db) return false;
        return exec("PRAGMA wal_checkpoint(TRUNCATE)");
    }

    bool exists(const std::string& db_path)
    {
        sqlite3* test_db = nullptr;
        int rc = sqlite3_open_v2(db_path.c_str(), &test_db, SQLITE_OPEN_READONLY, nullptr);
        if (rc == SQLITE_OK && test_db) {
            sqlite3_close(test_db);
            return true;
        }
        return false;
    }

    bool looks_like_vault(const std::string& db_path)
    {
        sqlite3* db = nullptr;
        if (sqlite3_open_v2(db_path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK) {
            if (db) sqlite3_close(db);
            return false;
        }

        bool ok = false;
        sqlite3_stmt* st = nullptr;
        const char* sql =
            "SELECT 1 FROM pm_sync_state WHERE key = 'encryption_salt' "
            "AND EXISTS (SELECT 1 FROM sqlite_master WHERE type = 'table' AND name = 'pm_credentials')";
        if (sqlite3_prepare_v2(db, sql, -1, &st, nullptr) == SQLITE_OK) {
            ok = (sqlite3_step(st) == SQLITE_ROW);
            sqlite3_finalize(st);
        }
        sqlite3_close(db);
        return ok;
    }

    bool is_open()
    {
        return g_db != nullptr;
    }

    std::string current_path()
    {
        return g_db_path;
    }

    // column order: uuid, encrypted_blob, created_at_ms, updated_at_ms, deleted_at_ms, server_rev, is_dirty
    static CredentialRow row_from_stmt(sqlite3_stmt* stmt)
    {
        CredentialRow row;

        const char* uuid_text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        row.uuid = uuid_text ? uuid_text : "";

        const void* blob = sqlite3_column_blob(stmt, 1);
        int blob_size = sqlite3_column_bytes(stmt, 1);
        if (blob && blob_size > 0) {
            row.encrypted_blob.assign(
                static_cast<const uint8_t*>(blob),
                static_cast<const uint8_t*>(blob) + blob_size
            );
        }

        row.created_at_ms = sqlite3_column_int64(stmt, 2);
        row.updated_at_ms = sqlite3_column_int64(stmt, 3);

        // NULL → 0 (not deleted)
        if (sqlite3_column_type(stmt, 4) == SQLITE_NULL) {
            row.deleted_at_ms = 0;
        } else {
            row.deleted_at_ms = sqlite3_column_int64(stmt, 4);
        }

        row.server_rev = sqlite3_column_int64(stmt, 5);
        row.is_dirty = sqlite3_column_int(stmt, 6) != 0;

        return row;
    }

    std::vector<CredentialRow> get_all_credentials()
    {
        std::vector<CredentialRow> results;
        if (!g_db) return results;

        const char* sql = R"(
            SELECT uuid, encrypted_blob, created_at_ms, updated_at_ms, deleted_at_ms, server_rev, is_dirty
            FROM pm_credentials
            WHERE deleted_at_ms IS NULL
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return results;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            results.push_back(row_from_stmt(stmt));
        }

        sqlite3_finalize(stmt);
        return results;
    }

    std::optional<CredentialRow> get_credential(const std::string& uuid)
    {
        if (!g_db) return std::nullopt;

        const char* sql = R"(
            SELECT uuid, encrypted_blob, created_at_ms, updated_at_ms, deleted_at_ms, server_rev, is_dirty
            FROM pm_credentials WHERE uuid = ?
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return std::nullopt;
        }

        bind_text(stmt, 1, uuid);

        std::optional<CredentialRow> result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            result = row_from_stmt(stmt);
        }

        sqlite3_finalize(stmt);
        return result;
    }

    std::vector<CredentialRow> get_dirty_credentials()
    {
        std::vector<CredentialRow> results;
        if (!g_db) return results;

        const char* sql = R"(
            SELECT uuid, encrypted_blob, created_at_ms, updated_at_ms, deleted_at_ms, server_rev, is_dirty
            FROM pm_credentials WHERE is_dirty = 1
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return results;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            results.push_back(row_from_stmt(stmt));
        }

        sqlite3_finalize(stmt);
        return results;
    }

    std::vector<CredentialRow> get_deleted_credentials()
    {
        std::vector<CredentialRow> results;
        if (!g_db) return results;

        const char* sql = R"(
            SELECT uuid, encrypted_blob, created_at_ms, updated_at_ms, deleted_at_ms, server_rev, is_dirty
            FROM pm_credentials WHERE deleted_at_ms IS NOT NULL
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return results;
        }

        while (sqlite3_step(stmt) == SQLITE_ROW) {
            results.push_back(row_from_stmt(stmt));
        }

        sqlite3_finalize(stmt);
        return results;
    }

    bool insert_credential(
        const std::string& uuid,
        const std::vector<uint8_t>& encrypted_blob,
        int64_t created_at_ms,
        int64_t updated_at_ms,
        std::string* out_error)
    {
        if (!g_db) {
            if (out_error) *out_error = "database not open";
            return false;
        }

        const char* sql = R"(
            INSERT INTO pm_credentials (uuid, encrypted_blob, created_at_ms, updated_at_ms, is_dirty)
            VALUES (?, ?, ?, ?, 1)
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            if (out_error) *out_error = std::string("insert prepare failed: ") + sqlite3_errmsg(g_db);
            return false;
        }

        bind_text(stmt, 1, uuid);
        bind_blob(stmt, 2, encrypted_blob);
        bind_int64(stmt, 3, created_at_ms);
        bind_int64(stmt, 4, updated_at_ms);

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            if (out_error) {
                int extended = sqlite3_extended_errcode(g_db);
                if (rc == SQLITE_CONSTRAINT) {
                    *out_error = std::string("uuid collision: ") + uuid + " (code " + std::to_string(extended) + ")";
                } else {
                    *out_error = std::string("insert step failed: ") + sqlite3_errmsg(g_db) +
                                 " (code " + std::to_string(extended) + ")";
                }
            }
            sqlite3_finalize(stmt);
            return false;
        }

        sqlite3_finalize(stmt);
        return true;
    }

    bool upsert_credential(
        const std::string& uuid,
        const std::vector<uint8_t>& encrypted_blob,
        int64_t created_at_ms,
        int64_t updated_at_ms,
        UpsertOutcome* out_outcome,
        std::string* out_error)
    {
        if (!g_db) {
            if (out_error) *out_error = "database not open";
            return false;
        }

        // pre-check needed: sqlite3_changes alone can't distinguish Inserted from Updated
        bool existed = false;
        {
            sqlite3_stmt* check = nullptr;
            const char* check_sql = "SELECT 1 FROM pm_credentials WHERE uuid = ?";
            int prep = sqlite3_prepare_v2(g_db, check_sql, -1, &check, nullptr);
            if (prep != SQLITE_OK) {
                if (out_error) *out_error = std::string("upsert pre-check prepare failed: ") + sqlite3_errmsg(g_db);
                return false;
            }
            bind_text(check, 1, uuid);
            int rc = sqlite3_step(check);
            sqlite3_finalize(check);
            if (rc == SQLITE_ROW)        existed = true;
            else if (rc == SQLITE_DONE)  existed = false;
            else {
                if (out_error) *out_error = std::string("upsert pre-check step failed: ") + sqlite3_errmsg(g_db);
                return false;
            }
        }

        // is_dirty=1 on update; offline build doesn't consume it, but keeping this
        // identical to the cloud variant avoids schema drift
        const char* sql = R"(
            INSERT INTO pm_credentials (uuid, encrypted_blob, created_at_ms, updated_at_ms, is_dirty)
            VALUES (?, ?, ?, ?, 1)
            ON CONFLICT(uuid) DO UPDATE SET
                encrypted_blob = excluded.encrypted_blob,
                updated_at_ms  = excluded.updated_at_ms,
                is_dirty       = 1
            WHERE pm_credentials.updated_at_ms < excluded.updated_at_ms
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            if (out_error) *out_error = std::string("upsert prepare failed: ") + sqlite3_errmsg(g_db);
            return false;
        }

        bind_text(stmt, 1, uuid);
        bind_blob(stmt, 2, encrypted_blob);
        bind_int64(stmt, 3, created_at_ms);
        bind_int64(stmt, 4, updated_at_ms);

        int rc = sqlite3_step(stmt);
        if (rc != SQLITE_DONE) {
            if (out_error) {
                int extended = sqlite3_extended_errcode(g_db);
                *out_error = std::string("upsert step failed: ") + sqlite3_errmsg(g_db) +
                             " (code " + std::to_string(extended) + ")";
            }
            sqlite3_finalize(stmt);
            return false;
        }

        int changes = sqlite3_changes(g_db);
        sqlite3_finalize(stmt);

        if (out_outcome) {
            if (!existed)         *out_outcome = UpsertOutcome::Inserted;
            else if (changes > 0) *out_outcome = UpsertOutcome::Updated;
            else                  *out_outcome = UpsertOutcome::Skipped;
        }
        return true;
    }

    bool update_credential(
        const std::string& uuid,
        const std::vector<uint8_t>& encrypted_blob,
        int64_t updated_at_ms)
    {
        if (!g_db) return false;

        const char* sql = R"(
            UPDATE pm_credentials
            SET encrypted_blob = ?, updated_at_ms = ?, is_dirty = 1
            WHERE uuid = ?
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        bind_blob(stmt, 1, encrypted_blob);
        bind_int64(stmt, 2, updated_at_ms);
        bind_text(stmt, 3, uuid);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool soft_delete_credential(const std::string& uuid)
    {
        if (!g_db) return false;

        int64_t now_ms = helpers::now_unix_ms();

        const char* sql = R"(
            UPDATE pm_credentials
            SET deleted_at_ms = ?, updated_at_ms = ?, is_dirty = 1
            WHERE uuid = ?
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        bind_int64(stmt, 1, now_ms);
        bind_int64(stmt, 2, now_ms);
        bind_text(stmt, 3, uuid);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool restore_credential(const std::string& uuid)
    {
        if (!g_db) return false;

        int64_t now_ms = helpers::now_unix_ms();

        const char* sql = R"(
            UPDATE pm_credentials
            SET deleted_at_ms = NULL, updated_at_ms = ?, is_dirty = 1
            WHERE uuid = ?
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        bind_int64(stmt, 1, now_ms);
        bind_text(stmt, 2, uuid);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool hard_delete_credential(const std::string& uuid)
    {
        if (!g_db) return false;

        const char* sql = "DELETE FROM pm_credentials WHERE uuid = ?";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        bind_text(stmt, 1, uuid);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    int purge_old_tombstones(int64_t older_than_ms)
    {
        if (!g_db) return 0;

        const char* sql = R"(
            DELETE FROM pm_credentials
            WHERE deleted_at_ms IS NOT NULL
              AND is_dirty = 0
              AND deleted_at_ms < ?
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return 0;
        }

        bind_int64(stmt, 1, older_than_ms);

        int deleted = 0;
        if (sqlite3_step(stmt) == SQLITE_DONE) {
            deleted = sqlite3_changes(g_db);
        }
        sqlite3_finalize(stmt);
        return deleted;
    }

#ifdef PD_OFFLINE_BUILD
    bool hard_delete_trashed_before(const std::string& uuid, int64_t older_than_ms)
    {
        if (!g_db) return false;

        // no is_dirty filter: offline soft-deletes are always dirty, so the sync-aware
        // purge above would never match. Row must still be trashed and past the cutoff.
        const char* sql = R"(
            DELETE FROM pm_credentials
            WHERE uuid = ?
              AND deleted_at_ms IS NOT NULL
              AND deleted_at_ms < ?
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        bind_text(stmt, 1, uuid);
        bind_int64(stmt, 2, older_than_ms);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }
#endif // PD_OFFLINE_BUILD

    bool mark_synced(const std::string& uuid, int64_t server_rev)
    {
        if (!g_db) return false;

        const char* sql = R"(
            UPDATE pm_credentials
            SET is_dirty = 0, server_rev = ?
            WHERE uuid = ?
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        bind_int64(stmt, 1, server_rev);
        bind_text(stmt, 2, uuid);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool mark_dirty(const std::string& uuid)
    {
        if (!g_db) return false;

        const char* sql = "UPDATE pm_credentials SET is_dirty = 1 WHERE uuid = ?";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        bind_text(stmt, 1, uuid);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool clear_all_dirty()
    {
        return exec("UPDATE pm_credentials SET is_dirty = 0");
    }
    bool clear_dirty(const std::string& uuid)
    {
        if (!g_db) return false;
        const char* sql = "UPDATE pm_credentials SET is_dirty = 0 WHERE uuid = ?";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        bind_text(stmt, 1, uuid);
        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }

    ResolveResult resolve_push_conflict_or_tombstone(
        const std::string& old_uuid,
        bool is_tombstone_response,
        int64_t server_rev,
        const std::vector<uint8_t>& master_key)
    {
        ResolveResult result;
        result.resolution = PushResolution::IgnoredNoOp;
        if (!g_db) return result;

        auto row = get_credential(old_uuid);
        if (!row.has_value()) return result;

        bool is_permanent_delete = row->encrypted_blob.empty();
        bool is_trashed = (row->deleted_at_ms > 0);
        bool is_deletion = is_permanent_delete || is_trashed;

        if (is_deletion) {
            if (is_tombstone_response) {
                // Permanent delete wins: apply permanent tombstone locally
                int64_t del_ms = (row->deleted_at_ms > 0) ? row->deleted_at_ms : helpers::now_unix_ms();
                force_apply_tombstone(old_uuid, del_ms, server_rev);
                result.resolution = PushResolution::TombstoneApplied;
            } else {
                // Conflict with server edit: server edit wins, drop local delete
                clear_dirty(old_uuid);
                result.resolution = PushResolution::DeleteDropped;
            }
            return result;
        }

        // Active edit: decrypt with old_uuid AAD and re-encrypt with new_uuid AAD
        std::string new_uuid = helpers::generate_uuid();
        std::vector<uint8_t> new_blob = row->encrypted_blob;
        if (!master_key.empty() && !new_blob.empty()) {
            std::string plain = enc::decrypt_credential(new_blob, master_key, old_uuid);
            if (plain.empty()) {
                plain = enc::decrypt_credential_legacy(new_blob, master_key);
            }
            if (!plain.empty()) {
                new_blob = enc::encrypt_credential(plain, master_key, new_uuid);
            } else {
                // Decryption failure: do not create corrupt fork
                if (is_tombstone_response) {
                    force_apply_tombstone(old_uuid, helpers::now_unix_ms(), server_rev);
                    result.resolution = PushResolution::TombstoneApplied;
                } else {
                    clear_dirty(old_uuid);
                    result.resolution = PushResolution::DeleteDropped;
                }
                return result;
            }
        }

        // Execute fork and old row update in a SINGLE SQLite transaction
        char* err_msg = nullptr;
        if (sqlite3_exec(g_db, "BEGIN IMMEDIATE;", nullptr, nullptr, &err_msg) != SQLITE_OK) {
            if (err_msg) sqlite3_free(err_msg);
            return result;
        }

        bool tx_ok = false;
        do {
            int64_t now_ms = helpers::now_unix_ms();

            // 1. Insert forked row with new_uuid (is_dirty = 1, server_rev = 0)
            sqlite3_stmt* stmt_ins = nullptr;
            const char* sql_ins = "INSERT INTO pm_credentials (uuid, encrypted_blob, created_at_ms, updated_at_ms, deleted_at_ms, server_rev, is_dirty) "
                                  "VALUES (?, ?, ?, ?, NULL, 0, 1);";
            if (sqlite3_prepare_v2(g_db, sql_ins, -1, &stmt_ins, nullptr) != SQLITE_OK) break;
            sqlite3_bind_text(stmt_ins, 1, new_uuid.c_str(), -1, SQLITE_TRANSIENT);
            bind_blob(stmt_ins, 2, new_blob);
            sqlite3_bind_int64(stmt_ins, 3, row->created_at_ms);
            sqlite3_bind_int64(stmt_ins, 4, now_ms);
            int rc_ins = sqlite3_step(stmt_ins);
            sqlite3_finalize(stmt_ins);
            if (rc_ins != SQLITE_DONE) break;

            // 2. Update old_uuid row: clear dirty (and apply tombstone if tombstoned response)
            sqlite3_stmt* stmt_upd = nullptr;
            if (is_tombstone_response) {
                const char* sql_upd = "UPDATE pm_credentials SET is_dirty = 0, encrypted_blob = X'', deleted_at_ms = ?, server_rev = ? WHERE uuid = ?;";
                if (sqlite3_prepare_v2(g_db, sql_upd, -1, &stmt_upd, nullptr) != SQLITE_OK) break;
                sqlite3_bind_int64(stmt_upd, 1, now_ms);
                sqlite3_bind_int64(stmt_upd, 2, server_rev);
                sqlite3_bind_text(stmt_upd, 3, old_uuid.c_str(), -1, SQLITE_TRANSIENT);
            } else {
                const char* sql_upd = "UPDATE pm_credentials SET is_dirty = 0 WHERE uuid = ?;";
                if (sqlite3_prepare_v2(g_db, sql_upd, -1, &stmt_upd, nullptr) != SQLITE_OK) break;
                sqlite3_bind_text(stmt_upd, 1, old_uuid.c_str(), -1, SQLITE_TRANSIENT);
            }
            int rc_upd = sqlite3_step(stmt_upd);
            sqlite3_finalize(stmt_upd);
            if (rc_upd != SQLITE_DONE) break;

            tx_ok = true;
        } while (false);

        if (tx_ok) {
            sqlite3_exec(g_db, "COMMIT;", nullptr, nullptr, nullptr);
            result.resolution = PushResolution::ForkedToNewUuid;
            result.forked_new_uuid = new_uuid;
        } else {
            sqlite3_exec(g_db, "ROLLBACK;", nullptr, nullptr, nullptr);
        }

        return result;
    }

    bool clone_credential_to_new_uuid(
        const std::string& old_uuid,
        const std::string& new_uuid,
        const std::vector<uint8_t>& master_key)
    {
        if (!g_db) return false;
        auto row = get_credential(old_uuid);
        if (!row.has_value()) return false;

        std::vector<uint8_t> new_blob = row->encrypted_blob;
        if (!master_key.empty() && !new_blob.empty()) {
            std::string plain = enc::decrypt_credential(new_blob, master_key, old_uuid);
            if (plain.empty()) {
                plain = enc::decrypt_credential_legacy(new_blob, master_key);
            }
            if (!plain.empty()) {
                new_blob = enc::encrypt_credential(plain, master_key, new_uuid);
            }
        }

        int64_t now_ms = helpers::now_unix_ms();
        return insert_credential(new_uuid, new_blob, row->created_at_ms, now_ms);
    }

    bool force_apply_tombstone(const std::string& uuid, int64_t deleted_at_ms, int64_t server_rev)
    {
        if (!g_db) return false;
        const char* sql = R"(
            UPDATE pm_credentials
            SET deleted_at_ms = ?, server_rev = ?, encrypted_blob = X'', is_dirty = 0
            WHERE uuid = ?
        )";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        bind_int64(stmt, 1, deleted_at_ms);
        bind_int64(stmt, 2, server_rev);
        bind_text(stmt, 3, uuid);
        bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
        sqlite3_finalize(stmt);
        return ok;
    }


    bool apply_server_tombstone(const std::string& uuid, int64_t deleted_at_ms, int64_t server_rev)
    {
        if (!g_db) return false;

        const char* sql = R"(
            UPDATE pm_credentials
            SET deleted_at_ms = ?, server_rev = ?, encrypted_blob = X'', is_dirty = 0
            WHERE uuid = ? AND is_dirty = 0 AND server_rev < ?
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        bind_int64(stmt, 1, deleted_at_ms);
        bind_int64(stmt, 2, server_rev);
        bind_text(stmt, 3, uuid);
        bind_int64(stmt, 4, server_rev);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool upsert_from_server(
        const std::string& uuid,
        const std::vector<uint8_t>& encrypted_blob,
        int64_t created_at_ms,
        int64_t updated_at_ms,
        int64_t server_rev)
    {
        if (!g_db) return false;

        const char* sql = R"(
            INSERT INTO pm_credentials (uuid, encrypted_blob, created_at_ms, updated_at_ms, server_rev, is_dirty)
            VALUES (?, ?, ?, ?, ?, 0)
            ON CONFLICT(uuid) DO UPDATE SET
                encrypted_blob = excluded.encrypted_blob,
                updated_at_ms = excluded.updated_at_ms,
                server_rev = excluded.server_rev,
                is_dirty = 0
            WHERE server_rev < excluded.server_rev
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        bind_text(stmt, 1, uuid);
        bind_blob(stmt, 2, encrypted_blob);
        bind_int64(stmt, 3, created_at_ms);
        bind_int64(stmt, 4, updated_at_ms);
        bind_int64(stmt, 5, server_rev);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    std::string get_meta(const std::string& key)
    {
        std::string result;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, "SELECT value FROM pm_sync_state WHERE key = ?", -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(stmt) == SQLITE_ROW)
                result = (const char*)sqlite3_column_text(stmt, 0);
        }
        sqlite3_finalize(stmt);
        return result;
    }

    bool set_meta(const std::string& key, const std::string& value)
    {
        sqlite3_stmt* stmt = nullptr;
        bool ok = false;
        if (sqlite3_prepare_v2(g_db,
            "INSERT OR REPLACE INTO pm_sync_state(key, value) VALUES(?, ?)",
            -1, &stmt, nullptr) == SQLITE_OK)
        {
            sqlite3_bind_text(stmt, 1, key.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, value.c_str(), -1, SQLITE_TRANSIENT);
            ok = (sqlite3_step(stmt) == SQLITE_DONE);
        }
        sqlite3_finalize(stmt);
        return ok;
    }

    std::vector<uint8_t> get_cached_salt()
    {
        std::string hex = get_meta("encryption_salt");
        if (hex.empty()) return {};
        std::vector<uint8_t> salt(hex.size() / 2);
        sodium_hex2bin(salt.data(), salt.size(), hex.c_str(), hex.size(), nullptr, nullptr, nullptr);
        return salt;
    }

    bool set_cached_salt(const std::vector<uint8_t>& salt)
    {
        std::string hex(salt.size() * 2 + 1, '\0');
        sodium_bin2hex(hex.data(), hex.size(), salt.data(), salt.size());
        hex.resize(salt.size() * 2);
        return set_meta("encryption_salt", hex);
    }

    std::vector<uint8_t> get_key_check()
    {
        if (!g_db) return {};
        std::string hex = get_meta("key_check");
        if (hex.empty()) return {};
        return enc::hex_to_bytes(hex);
    }

    bool set_key_check(const std::vector<uint8_t>& blob)
    {
        if (!g_db || blob.empty()) return false;
        return set_meta("key_check", enc::bytes_to_hex(blob));
    }

    bool begin_transaction()
    {
        return exec("BEGIN TRANSACTION");
    }

    bool commit_transaction()
    {
        return exec("COMMIT");
    }

    bool rollback_transaction()
    {
        return exec("ROLLBACK");
    }

    int count_credentials()
    {
        if (!g_db) return 0;

        const char* sql = "SELECT COUNT(*) FROM pm_credentials WHERE deleted_at_ms IS NULL";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return 0;
        }

        int count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }

        sqlite3_finalize(stmt);
        return count;
    }

    int count_dirty()
    {
        if (!g_db) return 0;

        const char* sql = "SELECT COUNT(*) FROM pm_credentials WHERE is_dirty = 1";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return 0;
        }

        int count = 0;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            count = sqlite3_column_int(stmt, 0);
        }

        sqlite3_finalize(stmt);
        return count;
    }

    std::vector<uint8_t> get_2fa_blob()
    {
        std::vector<uint8_t> result;
        if (!g_db) return result;

        const char* sql = "SELECT encrypted_blob FROM pm_vault_2fa WHERE id = 1";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return result;

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const void* blob = sqlite3_column_blob(stmt, 0);
            int blob_size = sqlite3_column_bytes(stmt, 0);
            if (blob && blob_size > 0) {
                result.assign(
                    static_cast<const uint8_t*>(blob),
                    static_cast<const uint8_t*>(blob) + blob_size
                );
            }
        }

        sqlite3_finalize(stmt);
        return result;
    }

    bool set_2fa_blob(const std::vector<uint8_t>& blob, int64_t created_ms, int64_t updated_ms)
    {
        if (!g_db) return false;

        const char* sql = R"(
            INSERT INTO pm_vault_2fa (id, encrypted_blob, created_at_ms, updated_at_ms)
            VALUES (1, ?, ?, ?)
            ON CONFLICT(id) DO UPDATE SET
                encrypted_blob = excluded.encrypted_blob,
                updated_at_ms = excluded.updated_at_ms
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        bind_blob(stmt, 1, blob);
        bind_int64(stmt, 2, created_ms);
        bind_int64(stmt, 3, updated_ms);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool delete_2fa_blob()
    {
        return exec("DELETE FROM pm_vault_2fa WHERE id = 1");
    }

    bool has_2fa()
    {
        if (!g_db) return false;

        const char* sql = "SELECT COUNT(*) FROM pm_vault_2fa WHERE id = 1";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW)
            found = sqlite3_column_int(stmt, 0) > 0;

        sqlite3_finalize(stmt);
        return found;
    }

    std::vector<uint8_t> get_recovery_blob()
    {
        std::vector<uint8_t> result;
        if (!g_db) return result;

        const char* sql = "SELECT encrypted_blob FROM pm_vault_recovery WHERE id = 1";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return result;

        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const void* blob = sqlite3_column_blob(stmt, 0);
            int blob_size = sqlite3_column_bytes(stmt, 0);
            if (blob && blob_size > 0) {
                result.assign(
                    static_cast<const uint8_t*>(blob),
                    static_cast<const uint8_t*>(blob) + blob_size
                );
            }
        }

        sqlite3_finalize(stmt);
        return result;
    }

    bool set_recovery_blob(const std::vector<uint8_t>& blob, int64_t created_ms)
    {
        if (!g_db) return false;

        const char* sql = R"(
            INSERT INTO pm_vault_recovery (id, encrypted_blob, created_at_ms)
            VALUES (1, ?, ?)
            ON CONFLICT(id) DO UPDATE SET
                encrypted_blob = excluded.encrypted_blob,
                created_at_ms = excluded.created_at_ms
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        bind_blob(stmt, 1, blob);
        bind_int64(stmt, 2, created_ms);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    bool has_recovery_key()
    {
        if (!g_db) return false;

        const char* sql = "SELECT COUNT(*) FROM pm_vault_recovery WHERE id = 1";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        bool found = false;
        if (sqlite3_step(stmt) == SQLITE_ROW)
            found = sqlite3_column_int(stmt, 0) > 0;

        sqlite3_finalize(stmt);
        return found;
    }

    bool delete_recovery_blob()
    {
        return exec("DELETE FROM pm_vault_recovery WHERE id = 1");
    }

    bool quick_integrity_check()
    {
        if (!g_db) return false;

        const char* sql = "PRAGMA quick_check";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK)
            return false;

        bool ok = false;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            ok = (result && std::strcmp(result, "ok") == 0);
        }

        sqlite3_finalize(stmt);
        return ok;
    }

} // namespace vault_db
