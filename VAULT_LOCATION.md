# Plan — Vault database location selector (pw-windows)

Status: **planned, not yet implemented.** Lets users keep the vault `.db` anywhere
(e.g. a Dropbox/OneDrive/Drive folder) for "bring your own sync," instead of the
current exe-directory-only model. Target app: **pw-windows** (offline, portable).
Port to pwmngr2.2.1 afterward (notes at the bottom).

## Decisions (FINAL — recommended defaults)
1. **WAL handling:** keep WAL, but **checkpoint-on-lock**. Add `vault_db::checkpoint_truncate()`
   (`PRAGMA wal_checkpoint(TRUNCATE)`) and call it on lock, minimize-to-tray, and exit so
   the synced file is always a single quiescent `.db` with no lingering `-wal`/`-shm`.
2. **"Create at…" UX:** popup menu on the existing Add button → **"New beside app"**
   (current portable default) / **"New at location…"** (Save-As picker).
3. **Conflict response:** **Reload / Overwrite / Cancel** modal on detected external change.
   `.lock` advisory sidecar = deferred follow-up (documented, not built).
4. **Path storage:** **absolute paths** first, with graceful "missing → Browse" handling.
   Exe-relative resolution = follow-up.

## Current state (for the implementer)
- Engine is path-agnostic: `load_vault_from_disk(path,pw,v)` / `create_new_vault_on_disk(path,pw,v)`.
- Locked screen is hardwired to the exe dir: `list_vaults_next_to_exe()` (~`application.cpp:617`),
  `new_db_path()` (~`:595`).
- `cfg::_path` / `db_path` in `config.json` remembers ONE vault path for auto-open
  (`tools/save.cpp`); auto-open already works with any absolute path.
- `.db` pickers already exist: `PickOpenFilePath_DB()` / `PickSaveFilePath_DB()` (`app_import_export.cpp:106,124`).
- Vault is SQLite **WAL** (`vault_db.cpp:67`), connection held open per session;
  dirty tracking via `HashCredsNow`/`saved_hash` (in-memory only).
- Native dialogs use `GetOpenFileNameA`/`GetSaveFileNameA`.

## Files touched
- `application.cpp` — locked-screen UI, load/save, `VaultState`, conflict modal.
- `tools/save.h` / `tools/save.cpp` — `recent_vaults` config list.
- `credentials/vault_db.h` / `.cpp` — `checkpoint_truncate()`.
- `app_internal.h` — ensure the two `Pick*FilePath_DB` are declared for `application.cpp`.

---

## 1. Open from… (browse)
- Add a folder button (`ICON_MDI_FOLDER_OPEN`) to the locked-screen header row beside
  Refresh/Add (~`application.cpp:2314`).
- On click → `PickOpenFilePath_DB()`; if non-empty: `v.vault_path = path`,
  `cfg::add_recent_vault(path)`, insert into `s_cached_vaults`, set `s_selected_vault_idx`.
  Existing unlock path then runs and `cfg::_path` persists it.
- Tooltip via `ui::SetTooltipPadded("Open vault from disk")`. ~15 lines, no engine change.

## 2. Create at… (save-as)
- Convert the Add button (`ICON_MDI_FILE_PLUS`, ~`:2326`) to open a small popup:
  - **New beside app** → current behavior: `new_db_path()` (exe dir) → `create_new_vault_on_disk`.
  - **New at location…** → `PickSaveFilePath_DB()` → `create_new_vault_on_disk(path,…)`.
- Both require the password field to be non-empty (existing check). After create:
  `cfg::add_recent_vault(v.vault_path)`, refresh list.

## 3. Recent / known vaults list
- Config: new `recent_vaults` JSON array in `config.json`.
  Add to `tools/save.{h,cpp}`:
  - `std::vector<std::string> get_recent_vaults();`
  - `void add_recent_vault(const std::string& path);`  // dedup (normalized), MRU, cap 10
  - `void remove_recent_vault(const std::string& path);`
- Locked screen: build the displayed list as `list_vaults_next_to_exe()` ∪ `recent_vaults`,
  deduped by normalized (lowercased, `/`→`\`) absolute path. Show external entries with
  their folder path; **missing** files greyed with an "x" → `remove_recent_vault`.
- On successful unlock/create: `cfg::add_recent_vault(v.vault_path)`.

## 4. Sync-conflict safety
**(a) Sync-friendly file — checkpoint on lock.**
- Add `bool vault_db::checkpoint_truncate();` → `exec("PRAGMA wal_checkpoint(TRUNCATE)")`.
- Call it: in the lock handler, in minimize-to-tray, and before `vault_db::close()` on exit.
  Result: the file the sync client uploads is a single consistent `.db`.

**(b) Detect external changes (another device synced over you).**
- Add to `VaultState`: `std::filesystem::file_time_type last_disk_mtime{};` and
  `uintmax_t last_disk_size = 0;`. Helper `capture_disk_stamp(v)` / `disk_stamp_changed(v)`.
- Capture the stamp right after `load_vault_from_disk` and after every `save_vault_to_disk`
  (post-checkpoint, so it reflects the app's own write).
- Before each save/autosave **and** on window focus-gain, call `disk_stamp_changed(v)`
  (compare `last_write_time` + size). If changed unexpectedly → modal:
  **"This vault changed on disk (another device?). Reload / Overwrite / Cancel."**
  - Reload → re-derive from disk (re-unlock with cached session password).
  - Overwrite → save over it (re-stamp).
  - Cancel → leave as-is, keep the warning state.
- Deferred: advisory `.lock` sidecar to warn on simultaneous open across machines.

## Config schema additions (`config.json`)
```jsonc
"recent_vaults": ["C:\\Users\\me\\Dropbox\\vault.db", "..."]
```
(No journal-mode toggle needed — decision 1 keeps WAL + checkpoints.)

## Portability caveat
Absolute paths work for one machine. Cross-machine BYO-sync differs
(`C:\Users\alice` ≠ `C:\Users\bob`) and a moved portable exe breaks drive letters.
Ship absolute + graceful-missing first; exe-relative resolution (store relative when on
the same volume) is a follow-up.

## Testing
- Create-at a folder outside the exe dir; confirm it opens, persists, and auto-opens next launch.
- Open-from a `.db` in a synced folder; confirm recent list shows it; delete the file and
  confirm it shows greyed/removable.
- Conflict: open vault, modify the `.db` externally (or via second copy), trigger save/focus →
  confirm Reload/Overwrite/Cancel behaves.
- Confirm no `-wal`/`-shm` remain after lock/exit (checkpoint works).
- Self-destruct mode 3 still only targets exe-dir `*.db` (external vaults untouched — expected).

## Port to pwmngr2.2.1 (later)
The cloud app shares `application.cpp` / `tools/save.*` / `credentials/vault_db.*` structure,
but it has its OWN cloud sync + multi-vault (`vault_slug`, `pm_sync_state`). Apply only the
**local** pieces (browse/create-at/recent-list/checkpoint) and skip conflict-vs-cloud overlap;
verify the locked-screen vault picker there (which already has cloud vaults) before splicing.
