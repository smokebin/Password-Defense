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
    // read-only probe on a separate handle: true if the file has this app's vault
    // schema and a salt row. Never modifies the file.
    bool looks_like_vault(const std::string& db_path);
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
#ifdef PD_OFFLINE_BUILD
    // OFFLINE BUILD ONLY. Hard-deletes ONE soft-deleted row if it is past the cutoff,
    // regardless of is_dirty (the offline build never clears it). In a sync build this
    // would drop tombstones before upload, so the server would resurrect them on other
    // devices; there, use purge_old_tombstones. Guarded so the sync build fails to
    // compile a call. Callers decide WHICH rows (see cred_ops::purge_trash_older_than:
    // only rows that decrypt and parse, never ones we can't read).
    bool hard_delete_trashed_before(const std::string& uuid, int64_t older_than_ms);
#endif

    // sync state
    bool mark_synced(const std::string& uuid, int64_t server_rev);
    bool mark_dirty(const std::string& uuid);
    bool clear_all_dirty();
    bool clear_dirty(const std::string& uuid);
    enum class PushResolution {
        IgnoredNoOp,       // Row not found or error
        DeleteDropped,     // Local delete dropped because server edit won (cleared dirty)
        TombstoneApplied,  // Tombstone applied to old row and dirty cleared
        ForkedToNewUuid    // Active edit forked to new UUID and old row cleared/tombstoned in single tx
    };

    struct ResolveResult {
        PushResolution resolution = PushResolution::IgnoredNoOp;
        std::string forked_new_uuid;
    };

    ResolveResult resolve_push_conflict_or_tombstone(
        const std::string& old_uuid,
        bool is_tombstone_response,
        int64_t server_rev,
        const std::vector<uint8_t>& master_key = {});

    bool clone_credential_to_new_uuid(const std::string& old_uuid, const std::string& new_uuid, const std::vector<uint8_t>& master_key = {});
    bool force_apply_tombstone(const std::string& uuid, int64_t deleted_at_ms, int64_t server_rev);
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

    // master-password key-check blob, hex in pm_sync_state("key_check"); empty = legacy vault
    std::vector<uint8_t> get_key_check();
    bool set_key_check(const std::vector<uint8_t>& blob);

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
