// vault_db.h
// SQLite-backed vault storage with per-Credential encryption
// One .db file per vault (replaces .lbdb)

#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace vault_db {

    // ============================================================
    // Credential row (encrypted blob storage)
    // ============================================================
    struct CredentialRow {
        std::string uuid;
        std::vector<uint8_t> encrypted_blob;

        // Timestamps (Unix milliseconds UTC)
        int64_t created_at_ms = 0;
        int64_t updated_at_ms = 0;
        int64_t deleted_at_ms = 0;  // 0 = not deleted, else Unix ms when deleted

        // Sync state
        int64_t server_rev = 0;     // Server revision number (0 = never synced)
        bool is_dirty = true;

        // Helper methods
        bool is_deleted() const { return deleted_at_ms != 0; }
    };

    // ============================================================
    // Database lifecycle
    // ============================================================

    // Initialize/open database (creates file + tables if needed)
    // Returns true on success
    bool init(const std::string& db_path);

    // Close database connection
    void close();

    // Check if database file exists and is valid SQLite
    bool exists(const std::string& db_path);

    // Check if a database is currently open
    bool is_open();

    // Get path of currently open database
    std::string current_path();

    // ============================================================
    // Credential CRUD operations
    // ============================================================

    // Get all credentials (non-deleted)
    std::vector<CredentialRow> get_all_credentials();

    // Get single Credential by UUID
    std::optional<CredentialRow> get_credential(const std::string& uuid);

    // Get only dirty credentials (for sync push)
    std::vector<CredentialRow> get_dirty_credentials();

    // Get deleted credentials (tombstones, for sync deletion)
    std::vector<CredentialRow> get_deleted_credentials();

    // Insert new Credential
    bool insert_credential(
        const std::string& uuid,
        const std::vector<uint8_t>& encrypted_blob,
        int64_t created_at_ms,
        int64_t updated_at_ms
    );

    // Update Credential blob + mark dirty
    bool update_credential(
        const std::string& uuid,
        const std::vector<uint8_t>& encrypted_blob,
        int64_t updated_at_ms
    );

    // Soft delete (set deleted_at_ms, mark dirty for sync)
    bool soft_delete_credential(const std::string& uuid);

    // Restore from trash (clear deleted_at_ms, mark dirty for sync)
    bool restore_credential(const std::string& uuid);

    // Hard delete (after sync confirms deletion, or purge old tombstones)
    bool hard_delete_credential(const std::string& uuid);

    // Purge old synced tombstones (older than TTL)
    int purge_old_tombstones(int64_t older_than_ms);

    // ============================================================
    // Sync state management
    // ============================================================

    // Mark Credential as synced (clear dirty flag, set server revision)
    bool mark_synced(const std::string& uuid, int64_t server_rev);

    // Mark Credential as dirty (local change)
    bool mark_dirty(const std::string& uuid);

    // Clear all dirty flags (after full sync)
    bool clear_all_dirty();

    // Apply server tombstone (mark as deleted from server)
    bool apply_server_tombstone(const std::string& uuid, int64_t deleted_at_ms, int64_t server_rev);

    // Upsert from server (stores encrypted blob directly, only if server_rev is newer)
    bool upsert_from_server(
        const std::string& uuid,
        const std::vector<uint8_t>& encrypted_blob,
        int64_t created_at_ms,
        int64_t updated_at_ms,
        int64_t server_rev
    );

    // ============================================================
    // Key-value sync state storage
    // ============================================================

    std::string get_sync_state(const std::string& key);
    bool set_sync_state(const std::string& key, const std::string& value);

    // Server revision tracking
    int64_t get_last_server_rev();
    bool set_last_server_rev(int64_t rev);

    // Salt caching
    std::vector<uint8_t> get_cached_salt();
    bool set_cached_salt(const std::vector<uint8_t>& salt);

    // Cloud vault slug (identifies which server-side vault this .db maps to)
    std::string get_vault_slug();
    bool set_vault_slug(const std::string& slug);

    // Read vault_slug from a .db file without changing the active connection
    // Opens a temporary read-only connection, reads pm_sync_state, closes it
    std::string read_vault_slug_from_file(const std::string& db_path);

    // ============================================================
    // Transaction helpers
    // ============================================================

    bool begin_transaction();
    bool commit_transaction();
    bool rollback_transaction();

    // ============================================================
    // Utility
    // ============================================================

    // Count total credentials (non-deleted)
    int count_credentials();

    // Count dirty credentials
    int count_dirty();

    // Run PRAGMA quick_check — returns true if DB is intact
    bool quick_integrity_check();

    // ============================================================
    // 2FA configuration (vault-level)
    // ============================================================

    // Get encrypted 2FA config blob (empty if not set)
    std::vector<uint8_t> get_2fa_blob();

    // Upsert encrypted 2FA config blob
    bool set_2fa_blob(const std::vector<uint8_t>& blob, int64_t created_ms, int64_t updated_ms);

    // Delete 2FA config
    bool delete_2fa_blob();

    // Quick check if 2FA is configured
    bool has_2fa();

    // ============================================================
    // Recovery key (vault-level)
    // ============================================================

    // Get encrypted recovery blob (master key encrypted with recovery key)
    std::vector<uint8_t> get_recovery_blob();

    // Store encrypted recovery blob
    bool set_recovery_blob(const std::vector<uint8_t>& blob, int64_t created_ms);

    // Quick check if recovery key is configured
    bool has_recovery_key();

    // Delete recovery blob
    bool delete_recovery_blob();

}
