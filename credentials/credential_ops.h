// credential_ops.h — encrypt/decrypt + DB; works with plain Credential structs

#pragma once

#include "credential.h"
#include <vector>
#include <cstdint>

namespace cred_ops {

    // empty vector on wrong key / db not open
    std::vector<Credential> load_all(const std::vector<uint8_t>& master_key);
    std::vector<Credential> load_deleted(const std::vector<uint8_t>& master_key);

    // returns assigned UUID, or "" on failure
    std::string add(const Credential& c, const std::vector<uint8_t>& master_key);
    bool update(const std::string& uuid, const Credential& c, const std::vector<uint8_t>& master_key);

    // exposed for save_vault_to_disk (preserves caller-supplied timestamps)
    std::vector<uint8_t> encrypt_cred_public(const Credential& c, const std::vector<uint8_t>& key, const std::string& uuid);

    bool remove(const std::string& uuid);           // soft delete
    bool remove_permanent(const std::string& uuid); // hard delete after sync confirmation

    // Preserves uuid if set (CSV imports get fresh UUIDs; .pwmngr round-trips keep theirs).
    // newer-wins upsert under the hood; counts split by outcome for the import toast.
    bool import_credentials(
        const std::vector<Credential>& creds,
        const std::vector<uint8_t>& master_key,
        int* out_inserted = nullptr,
        int* out_updated  = nullptr,
        int* out_skipped  = nullptr,
        std::string* out_error = nullptr
    );

    bool has_pending_changes();
    int  count();

}
