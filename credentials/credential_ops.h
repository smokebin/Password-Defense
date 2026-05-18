// credential_ops.h
// High-level Credential operations (encrypt/decrypt + database)
// Works with decrypted Credential structs

#pragma once

#include "credential.h"
#include <vector>
#include <cstdint>

namespace cred_ops {

    // ============================================================
    // Load/Save operations
    // ============================================================

    // Load all credentials from SQLite (decrypted into memory)
    // Returns empty vector on failure (wrong key, db not open, etc.)
    std::vector<Credential> load_all(const std::vector<uint8_t>& master_key);

    // Load deleted (trash) credentials from SQLite (decrypted for trash UI)
    std::vector<Credential> load_deleted(const std::vector<uint8_t>& master_key);

    // ============================================================
    // CRUD operations
    // ============================================================

    // Add new Credential (generates UUID, encrypts, stores)
    // Returns the assigned UUID, or empty string on failure
    std::string add(const Credential& c, const std::vector<uint8_t>& master_key);

    // Update existing Credential by UUID
    bool update(const std::string& uuid, const Credential& c, const std::vector<uint8_t>& master_key);

    // Encrypt a credential (for save_vault_to_disk — preserves timestamps)
    std::vector<uint8_t> encrypt_cred_public(const Credential& c, const std::vector<uint8_t>& key, const std::string& uuid);

    // Delete Credential by UUID (soft delete for sync)
    bool remove(const std::string& uuid);

    // Hard delete (after sync confirmation)
    bool remove_permanent(const std::string& uuid);

    // ============================================================
    // Batch operations
    // ============================================================

    // Import multiple credentials (used during migration)
    // Generates UUIDs for credentials without them
    bool import_credentials(
        const std::vector<Credential>& creds,
        const std::vector<uint8_t>& master_key
    );

    // ============================================================
    // Utility
    // ============================================================

    // Check if vault has any local changes pending sync
    bool has_pending_changes();

    // Get count of credentials
    int count();

}
