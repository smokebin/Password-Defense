# Password Defense v1.0.1

Polish release focused on the self-destructing-credentials feature and a few import/UX fixes.

### Fixed
- **Self-destructing credentials now land in the recoverable Trash.** Previously an auto-expired credential was dropped on save instead of being soft-deleted — it's now recoverable (and undo-able) just like a hand-deleted entry.
- **Expired credentials disappear from the list instantly** when their timer runs out.
- **CSV import** now opens reliably from Settings (fixed a modal that could flicker without appearing).

### Improved
- **Live M:SS countdown** on timed credentials under one hour — you can watch them count down to zero.
- **Toast notifications** recolored to match the app palette, with proper padding, plus a distinct look for self-destruct confirmations.
- **New 1-minute timer option** for ephemeral credentials.

---
Offline. No account, no server, no telemetry. Your vault is a single encrypted file (XChaCha20-Poly1305 + Argon2id). Windows-only; not independently audited — stated plainly.
