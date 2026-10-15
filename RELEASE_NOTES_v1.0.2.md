# Password Defense v1.0.2

A robustness and data-safety release. No new surface features — this hardens the
save, delete, import, recovery, and self-destruct paths, and makes the whole app
correct for non-English account names and folder paths.

### Fixed
- **Non-ASCII paths and names now work everywhere.** Vaults, backups, config, and
  file dialogs handle accented, CJK, and emoji characters in account names and in
  folder paths (e.g. `C:\Users\José\...`) without mojibake. A legacy config written
  in the old system code page is migrated to UTF-8 once instead of silently resetting
  all your settings.
- **A wrong master password is now rejected even on an empty vault** (previously an
  empty vault would unlock with any password).
- **Crash- and lock-safety.** Auto-lock and exit saves are written in a single
  transaction that rolls back cleanly on any failure, and no longer hand the vault
  file to a background thread — closing a source of potential corruption.
- **Emergency recovery keeps everything.** If a save fails, the recovery snapshot now
  preserves every field — card details, identity fields, password history, tags,
  favorites — not just the basics, and a trashed entry stays trashed when recovered.
- **Full self-destruct now erases recovery snapshots too**, including when your vault
  lives outside the app's own folders.

### Improved
- **App data moved to `%LOCALAPPDATA%\PasswordDefense`** (out of Documents, which
  OneDrive commonly redirects into the cloud). Existing files are migrated once.
- **Per-vault backup retention** — rotating one vault's backups never prunes another's.
- Unreadable trashed rows (e.g. from a different key) are reported and never deleted
  by Empty Trash or retention cleanup.

---
Offline. No account, no server, no telemetry. Your vault is a single encrypted file
(XChaCha20-Poly1305 + Argon2id). Windows-only; not independently audited — stated plainly.
