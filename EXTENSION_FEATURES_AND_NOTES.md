# Password Defense — Browser Extension Reference & Feature Guide

> **Version**: 2.2.1-ext  
> **Manifest**: MV3 (Chrome / Brave / Edge / Chromium / Safari)  
> **Native Host**: `com.passworddefense.bridge` (`pw-bridge.exe`)  
> **Last Updated**: October 2026

---

## 1. Architectural Overview

Password Defense pairs an offline-first C++ desktop vault with a lightweight Manifest V3 browser extension via **OS-isolated Native Messaging**:

```
┌────────────────────────────────────────────────────────┐
│                   Web Browser                          │
│                                                        │
│  ┌─────────────────────────┐  ┌──────────────────────┐ │
│  │   Content Script        │  │   Extension Popup    │ │
│  │  - In-Field Shield Icon │  │  - Vault & Search    │ │
│  │  - Dropdown Overlay     │  │  - Category Pills    │ │
│  │  - Auto-Save Prompt     │  │  - Password Gen      │ │
│  │  - Autofill Engine      │  │  - Domain Rules      │ │
│  └───────────┬─────────────┘  └──────────┬───────────┘ │
│              │                           │             │
│              └─────────────┬─────────────┘             │
│                            │ chrome.runtime.sendMessage│
│              ┌─────────────▼─────────────┐             │
│              │    Service Worker         │             │
│              │  (background/service-worker)            │
│              └─────────────┬─────────────┘             │
└────────────────────────────┼───────────────────────────┘
                             │ Native Messaging (stdin/stdout framed JSON)
┌────────────────────────────▼───────────────────────────┐
│               Windows Desktop Environment              │
│                                                        │
│  ┌──────────────────────────────────────────────────┐  │
│  │  pw-bridge.exe (Native Messaging Host)           │  │
│  │  - Named Pipe IPC / Shared Memory Cache          │  │
│  │  - ZERO open network ports / sockets             │  │
│  └──────────────────────────┬───────────────────────┘  │
│                             │                          │
│  ┌──────────────────────────▼───────────────────────┐  │
│  │  Password Defense.exe (Main C++ Desktop App)     │  │
│  │  - Argon2id / AES-256-GCM Encrypted Vault        │  │
│  └──────────────────────────────────────────────────┘  │
└────────────────────────────────────────────────────────┘
```

### Security & Privacy Guarantees
- **Zero Local Ports**: No local HTTP servers or WebSockets (`localhost:8080`, etc.) that could be fingerprinted or attacked by arbitrary browser tabs.
- **Sandboxed In-Page Overlay**: Rendered inside an open Shadow DOM (`#pwmngr-overlay-host`) to prevent web pages from inspecting or stealing vault secrets prior to autofill.
- **Memory Hygiene**: Bridge automatically clears credential cache upon vault lock or timeout.

---

## 2. Complete Feature Inventory

### A. Autofill Engine & Field Intelligence
1. **Login Credentials**:
   - Detects username, email, and password fields across standard and single-page applications.
   - Dispatches React/Angular/Vue compatible input, change, and blur events via native prototype descriptor setters.
   - Option for **Auto-fill on Page Load** (configurable via popup footer toggle).
2. **Payment Cards (Credit / Debit)**:
   - Recognizes `autocomplete="cc-number"`, `cc-csc`, `cc-exp`, `cc-name`, and regex heuristics for PAN, CVV, and expiration.
   - Automatically handles **combined expiration** (`MM/YY`, `MM/YYYY`) as well as **split Month and Year inputs or `<select>` dropdowns**.
   - In-field dropdown displays card brand badge (`VISA`, `MC`, `AMEX`), masked number `•••• 4242`, expiration date, and 1-click **Fill**.
3. **Identities & Addresses**:
   - Recognizes `street-address`, `address-line1`, `address-line2`, `address-level2` (city), `address-level1` (state), `postal-code` (zip), `tel`, `email`, and `name`.
   - Supports combined full-name inputs as well as separate First and Last Name inputs.
   - In-field dropdown displays formatted address previews and 1-click **Fill**.
4. **Two-Factor Authentication (2FA / OTP)**:
   - Detects 2FA inputs (`autocomplete="one-time-code"`, 6-digit boxes, `otp`, `token`, `verification`).
   - Displays live code with circular countdown in overlay and popup.
   - Auto-copies live 2FA code to clipboard upon login autofill for seamless two-step logins.

---

### B. Smart Form Intelligence
1. **Auto-Save & Update Detection Prompt**:
   - Intercepts form submissions (`submit` event listeners, submit button clicks, and Enter key presses).
   - Captures entered username, password, page title, and URL.
   - Compares with existing vault entries to determine whether to prompt for **Save New** or **Update Password**.
   - Renders a floating, glassmorphic toast prompt in the top-right of the viewport with **Save**, **Update**, or **Dismiss**.
2. **Split-Step / Two-Page Login Intelligence**:
   - Tailored for Google, Microsoft, Apple, Amazon, and modern auth providers that present Username on Screen 1 and Password on Screen 2.
   - Remembers the submitted username in `sessionStorage`.
   - When the password page loads, prioritizes the exact account selected in Step 1.
3. **Equivalent Domains & Custom Match Rules**:
   - **Ecosystem Clusters**: Automatically shares logins between related services (e.g. `google.com` ↔ `youtube.com` ↔ `accounts.google.com`; `live.com` ↔ `microsoft.com` ↔ `office.com`; `apple.com` ↔ `icloud.com`).
   - **ccTLD Equivalence**: Matches across international domains (e.g., `amazon.com` ↔ `amazon.ca` ↔ `amazon.co.uk`).
   - **Custom Match Rules**: Users can add custom domain clusters directly from the popup's **Domains** tab.

---

### C. Password & Passphrase Generator
Accessible via the **Generator** tab in the popup or in-field shortcut (`Ctrl+Shift+G` / `Cmd+Shift+G`):
1. **Random Password Mode**:
   - Length slider (8 to 64 characters) with quick preset pills (16, 20, 24, 32).
   - Toggles: Uppercase (A-Z), Lowercase (a-z), Digits (0-9), Symbols (!@#$%^&*), Avoid Ambiguous (`l, 1, I, O, 0`).
   - Real-time entropy strength meter (bits) and rating ("Weak", "Good", "Strong").
2. **Memorable Passphrase Mode**:
   - Clean word count slider (3 to 8 words) from a curated 250+ memorable wordlist.
   - Configurable separator (`-`, `.`, `_`, space).
   - Options for Capitalization and appending numbers.
3. **Quick Actions**:
   - 1-click **Copy to Clipboard**.
   - 1-click **Fill Page** into the active webpage.

---

### D. Popup Interface & Vault Management
1. **Navigation Tabs**:
   - **Vault**: Search, browse site matches, payment cards, and identities.
   - **Generator**: Comprehensive password and passphrase creation tool.
   - **Domains**: Configure equivalent domains, ecosystem rules, and ccTLD matching.
2. **Category Filter Pills**:
   - `[All]`: Unified view with site matches, payment cards, and identities.
   - `[Logins]`: Dedicated view of matched logins or full search results.
   - `[Cards]`: Dedicated view of payment cards with 1-click Fill, copy number, and copy CVV.
   - `[Identities]`: Dedicated view of saved addresses with 1-click Fill, copy address, and copy name.
3. **Vault Controls**:
   - Instant debounced search bar.
   - Quick **New** button to add credentials without opening the desktop app.
   - Auto-lock timeout selector (5 min, 15 min, 30 min, 1 hour, Never).
   - Auto-fill on load checkbox.

---

## 3. Extension File Map & Key Roles

```
web extensions/pwmngr-ext/
├── manifest.json                  # MV3 manifest defining permissions & content scripts
├── background/
│   └── service-worker.js          # Background service worker, native IPC & tab messaging
├── content/
│   ├── content.js                 # In-page scanner, overlay UI, form filler & save prompt
│   └── content.css                # Scoped styles for Shadow DOM overlay & save dialog
├── lib/
│   ├── api.js                     # Bridge API client abstraction (PwmngrAPI)
│   ├── crypto.js                  # Cryptographic helpers
│   ├── equivalent_domains.js      # Domain matching & cluster evaluation engine
│   ├── store.js                   # Extension local storage wrapper
│   └── totp.js                    # RFC 6238 TOTP generator (HMAC-SHA1 & Base32)
├── popup/
│   ├── popup.html                 # Main popup DOM (Vault, Generator, Domains, Editor)
│   ├── popup.css                  # Dark glassmorphic design system styles
│   └── popup.js                   # Popup UI controller & state management
└── safari/                        # Xcode Safari Web Extension target project
```

---

## 4. Native IPC Protocol Reference

All messages between the browser extension and `pw-bridge.exe` use 4-byte little-endian length prefix framing:

| Action | Request Payload | Response Payload | Description |
|---|---|---|---|
| `status` | `{}` | `{"connected": true, "unlocked": bool, "credCount": int}` | Heartbeat and lock state query |
| `match` | `{"url": "https://..."}` | `{"status": "ok", "matches": [...]}` | Query logins matching the given URL |
| `search` | `{"q": "query"}` | `{"status": "ok", "results": [...]}` | Vault-wide credential search |
| `get_cards` | `{}` | `{"ok": true, "cards": [...]}` | Query all stored payment cards |
| `get_identities` | `{}` | `{"ok": true, "identities": [...]}` | Query all stored identities/addresses |
| `save` | `{"save_action": "save_new"\|"update", "website": "...", ...}` | `{"ok": true}` | Save new login or update existing password |
| `get_timeout` | `{}` | `{"timeout": int}` | Query desktop auto-lock timeout (minutes) |
| `set_timeout` | `{"minutes": int}` | `{"ok": true}` | Set desktop auto-lock timeout |

---

## 5. Helpful Notes for Next Session

### A. Testing & Reloading
- **Extension Reload**: Visit `brave://extensions` (or `chrome://extensions`), find **Password Defense**, and click the 🔄 reload icon.
- **Inspecting Content Script**: Right-click anywhere on a webpage → **Inspect** → **Console**. Content logs are prefixed with `[Password Defense]`.
- **Inspecting Popup**: Right-click the extension icon in the toolbar → **Inspect Popup**.
- **Inspecting Background Worker**: In `brave://extensions`, click **service worker** on the extension card.

### B. Building & Deploying Native C++ Components
If desktop or bridge C++ code is modified:
```powershell
# 1. Build Desktop + Bridge Solutions (MSBuild)
& "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe" "A:\prjcts\pwmngr\pw-windows-cloud\pwmngr2.2.1.sln" /p:Configuration=Release /p:Platform=x64 /t:Build -verbosity:minimal

# 2. Deploy Executables to Installation Directory
Copy-Item "A:\prjcts\pwmngr\pw-windows-cloud\x64\Release\Password Defense.exe" "A:\apps\Password Defense\Password Defense.exe" -Force
Copy-Item "A:\prjcts\pwmngr\pw-windows-cloud\x64\Release\pw-bridge.exe" "A:\apps\Password Defense\pw-bridge.exe" -Force
```

### C. Future Roadmap / Next Candidate Features
1. **Passkey / WebAuthn Support**: Native FIDO2 / Passkey registration and assertion via native messaging.
2. **Credit Card Save Prompt**: Automatically prompt to save new credit cards when submitting checkout forms.
3. **Cardholder Address Linking**: Automatically link a billing address from an Identity to a Payment Card.
4. **Context Menu Deep Actions**: Right-click menu shortcuts to insert generated passwords, copy 2FA, or open specific vault entries.
