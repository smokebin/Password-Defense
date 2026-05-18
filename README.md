# Password Defense

A zero-knowledge, offline-first password manager built with C++ and ImGui. Your master password never leaves your device — all encryption happens locally using XChaCha20-Poly1305 and Argon2id.

## Screenshot

![Password Defense UI](screenshots/ui.png)

## Features

### Vault & Credentials
- 4 credential types: Passwords, Credit Cards, Identity Documents, Secure Notes
- Unlimited vaults with tabbed multi-vault interface
- Password history (up to 5 per credential), custom groups, pin & favorite
- Soft-delete trash (30-day recovery), credential expiry timers
- 128-level undo history

### Security & Encryption
- **XChaCha20-Poly1305** authenticated encryption (per-credential)
- **Argon2id** key derivation with optional high-security mode (4 iterations / 1 GB RAM)
- AAD-binding (per-credential UUIDs prevent ciphertext-swap attacks)
- Memory pinning, secure zeroing, master password re-prompt
- Auto-lock, clipboard auto-clear, lockout protection, self-destruct on exit

### Security Center
- Weak password detection (entropy scoring + pattern analysis)
- Reused password detection across all credentials
- Breach checking via Have I Been Pwned (k-anonymity — passwords never leave your machine)
- Password aging alerts (configurable threshold, default 90 days)
- At-a-glance dashboard with one-click navigation to fix

### Two-Factor Authentication
- TOTP support with live countdown
- QR code setup, recovery codes (10 one-time-use), recovery key

### Password Generator
- Configurable length and character sets
- Avoid ambiguous characters option
- Real-time strength meter (0–4 entropy score)

### Views & Layout
- 5 view modes: Simple Rows, Detailed Rows, Tile Grid, Three-Pane, Table
- Dark & Light themes with custom card background colors
- Hover-expand, customizable pill tabs, configurable columns
- Per-type subtitles, row gap control, collapsible three-pane panels

### Search, Filter & Sort
- Real-time search by title, email, or username
- Type filters, multi-select group filters
- Sort by title, created date, or updated date
- 6 grouping modes: None, Group, Month Created, Month Updated, Pinned & Favorites, Alphabetical

### Input Formatting
- Auto-formatted card numbers, expiry dates, phone numbers, dates of birth
- Digit-only filtering, smart cursor navigation, clean raw-digit storage

### Backup & Restore
- Automatic backups on every save
- Manual backup creation, configurable retention (default 25)
- Browse & restore from any `.lbdb` snapshot

### Import & Export
- **Export**: CSV (Bitwarden/Chrome compatible), PWM (native encrypted), KDBX (KeePass)
- **Import**: CSV (auto-detect Bitwarden, KeePass, LastPass, 1Password), PWM with conflict detection

### Cloud Sync (Pro)
- Revision-based delta sync (only changed credentials)
- Offline-first with live status indicators
- Automatic OAuth token refresh

### Browser Extension
- Local HTTP server with QR code pairing
- HMAC-SHA256 request signing, nonce-based replay protection
- Rate limiting, manage and revoke paired browsers

### Credential Sharing
- Share via encrypted, time-limited links (1h / 24h / 7d / 30d)
- Scope control: all, by group, or by type
- View limits, optional passphrase protection, QR code

### Desktop Integration
- System tray (minimize/close to tray, right-click menu)
- Start on boot, start minimized, always-on-top
- Auto-open last vault, read-only mode, favicon loading

## Platform

- Windows 10/11
- Portable single executable — no install required
- DirectX 11 hardware-accelerated UI

## License

MIT
