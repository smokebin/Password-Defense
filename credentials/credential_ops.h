// credential_ops.h — encrypt/decrypt + DB; works with plain Credential structs

#pragma once

#include "credential.h"
#include "../third_party/json_fwd.hpp"
#include <string>
#include <unordered_set>
#include <vector>
#include <cstdint>
#include <functional>

namespace cred_ops {

    // Canonical content-field (de)serialization: the single source of truth for which
    // Credential fields persist. The DB vault blob and the autolock recovery snapshot
    // both go through these, so a new field is covered everywhere at once. uuid and the
    // created/updated/deleted timestamps are NOT included (they are DB columns for the
    // vault blob); callers that store them in the JSON itself, e.g. recovery snapshots,
    // add and read them separately.
    nlohmann::json to_json(const Credential& c);
    void           from_json(const nlohmann::json& j, Credential& c);

    // empty vector on wrong key / db not open
    std::vector<Credential> load_all(const std::vector<uint8_t>& master_key);
    std::vector<Credential> load_deleted(const std::vector<uint8_t>& master_key);

    // Save-time hard-delete policy. Returns the live DB rows to hard-delete: only those
    // the user explicitly removed this session (recorded, never inferred) that are not
    // back in memory. Anything else missing from memory (failed to decrypt, failed to
    // parse, unknown schema, written by another device) is left untouched.
    std::vector<std::string> select_hard_deletes(
        const std::vector<std::string>& live_db_uuids,
        const std::unordered_set<std::string>& memory_uuids,
        const std::unordered_set<std::string>& user_removed);

    struct SaveResult {
        bool ok = false;
        int  hard_deleted = 0;
        std::string error;
    };

    // Writes the in-memory vault to the DB in ONE transaction:
    //  - entries with no row (no uuid, or a uuid with no live/trashed row) are inserted,
    //    except uuids in `purged` (permanently deleted by the user this session)
    //  - entries with a row are updated; trashed rows back in memory are restored
    //  - live rows missing from memory are hard-deleted only if in `user_removed`
    //  - a legacy vault gets its key-check stamped once it has content
    // On ANY failure (including COMMIT) the transaction is rolled back and uuids assigned
    // by this call are cleared again, so memory matches the unchanged DB and the next
    // save retries without losing or duplicating anything.
    SaveResult save_all(std::vector<Credential>& creds,
                        const std::vector<uint8_t>& master_key,
                        const std::unordered_set<std::string>& user_removed,
                        const std::unordered_set<std::string>& purged);

    // Undo bookkeeping. `before` = memory before the undo, `after` = the restored
    // snapshot (modified in place). Drops anything in `purged` from `after`, records
    // saved uuids the undo removed in `user_removed`, and un-records any that came back.
    void reconcile_after_undo(const std::vector<Credential>& before,
                              std::vector<Credential>& after,
                              std::unordered_set<std::string>& user_removed,
                              const std::unordered_set<std::string>& purged);

#ifdef PD_OFFLINE_BUILD
    // Trash retention. Hard-deletes trashed rows older than the cutoff ONLY if they
    // decrypt and parse with this key; unreadable rows are kept (count reported).
    int purge_trash_older_than(const std::vector<uint8_t>& master_key, int64_t older_than_ms,
                               int* out_kept_unreadable = nullptr);
#endif

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

    struct ImportFlowResult {
        bool ok = false;
        bool flush_failed = false;   // pending changes couldn't be saved: nothing imported
        int inserted = 0, updated = 0, skipped = 0;
        std::string error;
    };

    // The import sequence the app uses. Import writes to disk and memory is then
    // reloaded from disk, so pending in-memory changes must be saved first:
    //   1. flush()  - save pending changes. false -> abort: nothing imported, reload
    //                 NOT called, memory (with its unsaved edits) left untouched
    //   2. import_credentials(incoming)
    //   3. reload() - only after a successful import
    ImportFlowResult import_flow(const std::function<bool()>& flush,
                                 const std::vector<Credential>& incoming,
                                 const std::vector<uint8_t>& master_key,
                                 const std::function<void()>& reload);

    // Trashed rows that don't decrypt+parse with this key. Never removed by Empty
    // Trash or the retention purge, so the UI reports them persistently.
    int count_unreadable_trashed(const std::vector<uint8_t>& master_key);

    bool has_pending_changes();
    int  count();

}
