// vault_db.cpp
// SQLite-backed vault storage implementation

#include "vault_db.h"
#include "crypto/vault_crypto.h"
#include "../tools/utility.h"

#include <sqlite3.h>
// sqlite3.c compiled directly into project (no lib linking needed)

#include <cstring>

namespace vault_db {

    // ============================================================
    // Static state
    // ============================================================
    static sqlite3* g_db = nullptr;
    static std::string g_db_path;

    // ============================================================
    // Internal helpers
    // ============================================================
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

    // Safe binding helpers (always use SQLITE_TRANSIENT for C++ strings/vectors)
    static void bind_text(sqlite3_stmt* stmt, int idx, const std::string& val) {
        sqlite3_bind_text(stmt, idx, val.c_str(), static_cast<int>(val.size()), SQLITE_TRANSIENT);
    }

    static void bind_blob(sqlite3_stmt* stmt, int idx, const std::vector<uint8_t>& val) {
        sqlite3_bind_blob(stmt, idx, val.data(), static_cast<int>(val.size()), SQLITE_TRANSIENT);
    }

    static void bind_int64(sqlite3_stmt* stmt, int idx, int64_t val) {
        sqlite3_bind_int64(stmt, idx, val);
    }

    // ============================================================
    // Database lifecycle
    // ============================================================
    bool init(const std::string& db_path)
    {
        if (g_db) close();

        int rc = sqlite3_open(db_path.c_str(), &g_db);
        if (rc != SQLITE_OK) {
            g_db = nullptr;
            return false;
        }

        g_db_path = db_path;

        // Enable WAL mode for better concurrency
        exec("PRAGMA journal_mode=WAL");
        exec("PRAGMA synchronous=FULL");
        exec("PRAGMA foreign_keys=ON");
        exec("PRAGMA secure_delete=ON");

        // Create tables with per-Credential sync schema
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

    bool is_open()
    {
        return g_db != nullptr;
    }

    std::string current_path()
    {
        return g_db_path;
    }

    // ============================================================
    // Credential operations
    // ============================================================

    // Helper to populate CredentialRow from SQLite statement
    // Expected column order: uuid, encrypted_blob, created_at_ms, updated_at_ms, deleted_at_ms, server_rev, is_dirty
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

        // deleted_at_ms: NULL becomes 0
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

        // Get all dirty credentials (including tombstones for sync push)
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

        // Get tombstones (deleted_at_ms IS NOT NULL)
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
        int64_t updated_at_ms)
    {
        if (!g_db) return false;

        const char* sql = R"(
            INSERT INTO pm_credentials (uuid, encrypted_blob, created_at_ms, updated_at_ms, is_dirty)
            VALUES (?, ?, ?, ?, 1)
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        bind_text(stmt, 1, uuid);
        bind_blob(stmt, 2, encrypted_blob);
        bind_int64(stmt, 3, created_at_ms);
        bind_int64(stmt, 4, updated_at_ms);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
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

        // Only purge tombstones that:
        // 1. Are marked deleted (deleted_at_ms IS NOT NULL)
        // 2. Have been synced (is_dirty = 0)
        // 3. Are older than TTL
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

    // ============================================================
    // Sync state management
    // ============================================================
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

    bool apply_server_tombstone(const std::string& uuid, int64_t deleted_at_ms, int64_t server_rev)
    {
        if (!g_db) return false;

        const char* sql = R"(
            UPDATE pm_credentials
            SET deleted_at_ms = ?, server_rev = ?, is_dirty = 0
            WHERE uuid = ?
        )";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        bind_int64(stmt, 1, deleted_at_ms);
        bind_int64(stmt, 2, server_rev);
        bind_text(stmt, 3, uuid);

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

        // Insert or update, but only if server_rev is newer
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

    // ============================================================
    // Key-value sync state
    // ============================================================
    std::string get_sync_state(const std::string& key)
    {
        if (!g_db) return "";

        const char* sql = "SELECT value FROM pm_sync_state WHERE key = ?";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return "";
        }

        bind_text(stmt, 1, key);

        std::string result;
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (val) result = val;
        }

        sqlite3_finalize(stmt);
        return result;
    }

    bool set_sync_state(const std::string& key, const std::string& value)
    {
        if (!g_db) return false;

        const char* sql = "INSERT OR REPLACE INTO pm_sync_state (key, value) VALUES (?, ?)";

        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(g_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            return false;
        }

        bind_text(stmt, 1, key);
        bind_text(stmt, 2, value);

        bool ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        return ok;
    }

    int64_t get_last_server_rev()
    {
        std::string val = get_sync_state("last_server_rev");
        if (val.empty()) return 0;
        try {
            return std::stoll(val);
        } catch (...) {
            return 0;
        }
    }

    bool set_last_server_rev(int64_t rev)
    {
        return set_sync_state("last_server_rev", std::to_string(rev));
    }

    std::vector<uint8_t> get_cached_salt()
    {
        std::string hex = get_sync_state("encryption_salt");
        if (hex.empty()) return {};
        return enc::hex_to_bytes(hex);
    }

    bool set_cached_salt(const std::vector<uint8_t>& salt)
    {
        return set_sync_state("encryption_salt", enc::bytes_to_hex(salt));
    }

    std::string get_vault_slug()
    {
        return get_sync_state("vault_slug");
    }

    bool set_vault_slug(const std::string& slug)
    {
        return set_sync_state("vault_slug", slug);
    }

    std::string read_vault_slug_from_file(const std::string& db_path)
    {
        sqlite3* temp_db = nullptr;
        int rc = sqlite3_open_v2(db_path.c_str(), &temp_db, SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK || !temp_db) {
            if (temp_db) sqlite3_close(temp_db);
            return "";
        }

        std::string slug;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "SELECT value FROM pm_sync_state WHERE key = 'vault_slug'";
        if (sqlite3_prepare_v2(temp_db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                if (val) slug = val;
            }
            sqlite3_finalize(stmt);
        }

        sqlite3_close(temp_db);
        return slug;
    }

    // ============================================================
    // Transaction helpers
    // ============================================================
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

    // ============================================================
    // Utility
    // ============================================================
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

    // ============================================================
    // 2FA configuration
    // ============================================================
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

    // ============================================================
    // Recovery key
    // ============================================================
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

    // ============================================================
    // Integrity checking
    // ============================================================
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
