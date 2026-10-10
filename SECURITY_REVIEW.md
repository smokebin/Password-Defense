# Security review: password decryption and unlock

A review of how Password Defense encrypts, decrypts and unlocks the vault. The core crypto is solid: XChaCha20-Poly1305 per credential, Argon2id for key derivation, the UUID as AAD, and constant-time comparison for recovery codes. The problems were in the code around it.

Status below is for this branch. Nothing is committed yet.

## Fixed in this branch

| Problem | Where | What changed |
|---|---|---|
| 2FA could be turned off from an unlocked session with no password check | `ui_controls.cpp` | Turning 2FA off now asks for the master password, like the other security toggles |
| With 2FA on, all credentials were decrypted before the code was checked | `application.cpp` | Credentials load only after the second factor passes |
| A wrong password was accepted when the vault had no live entries, and entries added under it became unreadable | `application.cpp`, `credential_ops.cpp` | A key check is stored at vault creation. Wrong passwords are rejected before anything is decrypted |
| Reprompt lockout set to "Never" locked on the first typo | `ui_modals.cpp` | "Never" means no lockout |
| Auto-lock, tray lock and reprompt lockout freed the key and plaintext without wiping them | `application.cpp`, `ui_modals.cpp` | Same wipe as the explicit lock |
| TOTP codes could be reused within the ±1 window | `totp.cpp`, `twofa_ops.cpp` | The last accepted time step is stored. Anything at or before it is rejected (RFC 6238 §5.2) |
| A recovery code counted as used even if writing the change failed | `twofa_ops.cpp` | The code only counts once the write succeeds |
| Old no-AAD blobs were still decrypted on every read, and trashed entries were never upgraded | `credential_ops.cpp` | One-time upgrade on unlock covers live and trashed rows. The old format is no longer read normally |
| The recovery key skipped 2FA | `application.cpp` | Recovery now also asks for a 2FA code when 2FA is on |
| Master and export passwords could be one character | `application.cpp`, `ui_controls.cpp` | Minimum 15 characters at vault creation and on export (NIST SP 800-63B-4, single-factor) |
| README said 10 recovery codes, the code makes 8. The AAD claim didn't mention the upgrade | `README.md` | Corrected |

## Found, not changed here

- **Hand-rolled AES** in the KDBX export (`credentials/crypto/aes_cipher.*`). Table lookups indexed by secret data are a known side-channel class. I haven't measured it here. Plan is to replace it with Windows BCrypt AES-CBC and add known-answer tests.
- **Plaintext copies** in the export path. Whole-vault copies are passed by value and never wiped.
- **No attempt limit** on unlock or 2FA codes. Offline guessing against the database file is not affected by any in-app limit, so password length is the real defense.
- **Defaults:** export doesn't ask for the master password, and auto-lock is off. Left as they are; these are product choices.
- **No master password change** and **no recovery key rotation**.
- **Autosave off:** closing the tab or auto-locking skips the unsaved-changes check, so the last edits can be lost.
- **`.pwm` AAD** covers only the magic and version. Tampering is still caught by the key and tag, so this is hardening only.

## Checked and dropped

- "The export KDF is too weak": both exports are above the OWASP minimum (m=19456 KiB, t=2, p=1).
- "KDBX parallelism mismatch": the header writes `P=1`, which matches what is computed.

## Sources

- RFC 6238 §5.2 (verifier must not accept an OTP twice): https://www.rfc-editor.org/rfc/rfc6238
- NIST SP 800-63B-4 §3.1.1.2 (15 characters for single-factor passwords): https://pages.nist.gov/800-63-4/sp800-63b/authenticators/
- OWASP Password Storage Cheat Sheet (Argon2id minimum): https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html
- Microsoft `BCryptEncrypt` (for the deferred AES replacement): https://learn.microsoft.com/en-us/windows/win32/api/bcrypt/nf-bcrypt-bcryptencrypt

Not confirmed: whether KeePass accepts Argon2id. Its developer notes and user docs disagree. This matters only for the deferred KDBX work.

## Status

- Release x64 full rebuild: 0 errors. Same 16 warnings as before the change, none new.
- libsodium 1.0.22 was downloaded from download.libsodium.org into the gitignored `libs\` folder. Its signature was not checked, because no minisign tool is installed here.
- Throwaway test harness (not in the repo): 47 checks, all passing. Covers the RFC 6238 Appendix B vectors, TOTP replay and window-neighbour rejection, a failed recovery write (simulated with a SQLite trigger), the key check on an empty vault, the legacy upgrade including a trashed row, and rejection of a blob moved to another UUID.
- **Not automated:** the UI flows (2FA disable prompt, reprompt "Never", auto-lock wipe, export length check). These still need a manual check.
