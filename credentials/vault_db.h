// vault_db.h — SQLite vault; one .db file per vault

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace vault_db {

    struct CredentialRow {
        std::string uuid;
        std::vector<uint8_t> encrypted_blob;

        // Unix ms UTC
        int64_t created_at_ms = 0;
        int64_t updated_at_ms = 0;
        int64_t deleted_at_ms = 0;  // 0 = live; nonzero = soft-deleted

        int64_t server_rev = 0;     // 0 = never synced
        bool is_dirty = true;

        bool is_deleted() const { return deleted_at_ms != 0; }
    };

    // creates file + tables if needed
    bool init(const std::string& db_path);

    void close();

    // collapses WAL → main file, removes -wal/-shm; call before lock/exit/sync
    bool checkpoint_truncate();

    bool exists(const std::string& db_path);
    bool is_open();
    std::string current_path();

    std::vector<CredentialRow> get_all_credentials();
    std::optional<CredentialRow> get_credential(const std::string& uuid);
    std::vector<CredentialRow> get_dirty_credentials();    // for sync push
    std::vector<CredentialRow> get_deleted_credentials();  // tombstones

    // strict INSERT; UUID collision is treated as a real error (not "already imported")
    bool insert_credential(
        const std::string& uuid,
        const std::vector<uint8_t>& encrypted_blob,
        int64_t created_at_ms,
        int64_t updated_at_ms,
        std::string* out_error = nullptr
    );

    // all three outcomes are "success" from a SQL-error standpoint
    enum class UpsertOutcome {
        Inserted,   // new row
        Updated,    // existing row replaced (incoming was newer)
        Skipped,    // existing row kept (incoming was older/equal)
    };

    // newer-wins upsert; false only on SQL errors; re-import into the source vault is a no-op
    bool upsert_credential(
        const std::string& uuid,
        const std::vector<uint8_t>& encrypted_blob,
        int64_t created_at_ms,
        int64_t updated_at_ms,
        UpsertOutcome* out_outcome = nullptr,
        std::string* out_error = nullptr
    );

    bool update_credential(
        const std::string& uuid,
        const std::vector<uint8_t>& encrypted_blob,
        int64_t updated_at_ms
    );

    bool soft_delete_credential(const std::string& uuid);
    bool restore_credential(const std::string& uuid);
    bool hard_delete_credential(const std::string& uuid);
    int  purge_old_tombstones(int64_t older_than_ms);  // only purges already-synced tombstones

    // sync state
    bool mark_synced(const std::string& uuid, int64_t server_rev);
    bool mark_dirty(const std::string& uuid);
    bool clear_all_dirty();
    bool apply_server_tombstone(const std::string& uuid, int64_t deleted_at_ms, int64_t server_rev);

    // only applies if server_rev is newer than stored
    bool upsert_from_server(
        const std::string& uuid,
        const std::vector<uint8_t>& encrypted_blob,
        int64_t created_at_ms,
        int64_t updated_at_ms,
        int64_t server_rev
    );

    // pm_sync_state key-value store
    std::string get_meta(const std::string& key);
    bool set_meta(const std::string& key, const std::string& value);

    // salt stored as hex in pm_sync_state("encryption_salt")
    std::vector<uint8_t> get_cached_salt();
    bool set_cached_salt(const std::vector<uint8_t>& salt);

    bool begin_transaction();
    bool commit_transaction();
    bool rollback_transaction();

    int  count_credentials();
    int  count_dirty();
    bool quick_integrity_check();  // PRAGMA quick_check

    // vault-level 2FA blob (encrypted with master key + fixed AAD)
    std::vector<uint8_t> get_2fa_blob();
    bool set_2fa_blob(const std::vector<uint8_t>& blob, int64_t created_ms, int64_t updated_ms);
    bool delete_2fa_blob();
    bool has_2fa();

    // master key encrypted with recovery key
    std::vector<uint8_t> get_recovery_blob();
    bool set_recovery_blob(const std::vector<uint8_t>& blob, int64_t created_ms);
    bool has_recovery_key();
    bool delete_recovery_blob();

}
