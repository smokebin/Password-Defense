Privacy Policy

Password Defense
Operated by Stacked Software
Last Updated: March 2, 2026

1. Introduction

Password Defense is a privacy-focused password management application designed around a zero-knowledge architecture. Encryption and decryption of vault data occur exclusively on the user’s device using keys derived from the user’s master password.

Stacked Software does not have access to user master passwords or unencrypted vault contents.

This Privacy Policy explains what information we collect, how it is processed, and the limited third-party services involved in operating Password Defense.

2. Scope of This Policy

This Privacy Policy applies to:

The Password Defense desktop application

The Password Defense browser extension

The Password Defense website

Optional cloud synchronization services (if enabled)

Use of Password Defense in fully offline mode does not require account creation and does not transmit vault data to our servers.

3. Information We Collect
3.1 Account Information (Cloud Sync Users Only)

If a user elects to enable cloud synchronization, the following data is collected:

Email address — used for authentication and account-related communication.

Username — display identifier.

Authentication verifier — a one-way cryptographic derivation used to validate login credentials. This value cannot be reversed to obtain the master password.

Encryption salt — a random cryptographic salt stored server-side to enable consistent key derivation across devices.

Users operating Password Defense in offline mode do not provide this information.

3.2 Encrypted Vault Data

When cloud synchronization is enabled:

Vault data is encrypted client-side using XChaCha20-Poly1305.

Encryption keys are derived locally using Argon2id.

Only ciphertext is transmitted to and stored on our servers.

Because encryption keys are derived exclusively from the user’s master password and are never transmitted, Stacked Software is cryptographically incapable of decrypting stored vault data.

3.3 Synchronization Metadata

To support conflict resolution and version management, we store:

Vault revision identifiers

Vault identifiers (slugs and names)

Timestamps of synchronization events

This metadata does not include unencrypted credential data.

3.4 Local Device Storage

The desktop application stores locally:

Encrypted vault database files

Application preferences

Vault path references

Authentication tokens (encrypted within the vault database)

Local files remain under user control and are not accessed by Stacked Software.

3.5 Information We Do Not Collect

We do not collect:

Master passwords

Unencrypted credentials

Browsing history

Keystroke data

Device fingerprints

Advertising identifiers

Behavioral analytics or telemetry data

4. Encryption and Security Architecture
4.1 Cryptographic Design

Authenticated encryption: XChaCha20-Poly1305

Key derivation: Argon2id with random per-vault salt

Cryptographic library: libsodium

All sensitive vault data is encrypted before leaving the user’s device.

4.2 Transport Security

All communications between client applications and api.passworddefense.net occur over TLS (HTTPS).

4.3 Server Storage Model

Servers store:

Encrypted vault ciphertext

Account identifiers

Limited synchronization metadata

In the event of a server breach, exposed data would consist only of encrypted vault blobs and account identifiers. Without the user’s master password, decryption is computationally infeasible.

5. Third-Party Services
5.1 Stripe (Payment Processing)

We use Stripe, Inc. to process subscription payments.

Payment card data is transmitted directly to Stripe.

Stacked Software does not store full payment card numbers.

Stripe’s data practices are governed by Stripe’s Privacy Policy: https://stripe.com/privacy

5.2 Have I Been Pwned (Breach Checking)

Password Defense integrates with the Have I Been Pwned Pwned Passwords API.

Only the first five characters of a SHA-1 password hash prefix are transmitted.

Full hashes are compared locally.

No account identifiers are transmitted.

HIBP’s privacy policy: https://haveibeenpwned.com/Privacy

5.3 No Advertising or Analytics SDKs

We do not integrate third-party analytics SDKs, advertising frameworks, or behavioral tracking systems within the desktop or browser extension applications.

6. Cookies

The desktop application and browser extension do not use cookies.

If cloud login or a web dashboard is used, essential session cookies may be used strictly for authentication. No tracking or advertising cookies are deployed.

7. Data Retention
Data Type	Retention Policy
Account information	Retained until user deletion
Encrypted vault data	Retained until deletion of vault or account
Sync metadata	Retained while account is active
Anonymous share bundles	Deleted automatically upon expiry
Payment records	Retained by Stripe per their policies

Upon account deletion, server-side account data and encrypted vault data are removed from active systems and scheduled for removal from backup systems in accordance with standard retention cycles.

8. Anonymous Vault Sharing

Shared vault bundles:

Are encrypted client-side

May be protected with an optional passphrase

Expire automatically

Are not indexed or associated with user identities

Expired bundles are permanently deleted from active storage.

9. Browser Extension

The browser extension communicates exclusively with a locally running desktop instance via localhost.

It does not:

Transmit browsing history externally

Inject tracking scripts

Send credential data to third-party services

10. Data Security Practices

Password Defense implements:

TLS for all server communications

In-memory sensitive data wiping using SecureZeroMemory

Automatic session locking after inactivity

Encrypted storage of authentication tokens

11. User Rights

Users may:

Export vault data in CSV, encrypted proprietary format, or KDBX

Delete their account and associated server data

Operate the application entirely offline

12. Children’s Privacy

Password Defense is not directed toward children under the age of 13. We do not knowingly collect personal information from children under 13.

13. Governing Law

This Privacy Policy is governed by the laws of the State of [Insert State], without regard to conflict of law principles.

14. Changes to This Policy

We may update this Privacy Policy periodically. The updated version will be posted on our website with a revised “Last Updated” date.

Continued use of the services after changes become effective constitutes acceptance of the revised policy.

15. Contact Information

Questions regarding this Privacy Policy may be directed to:

Email: support@passworddefense.net

Operator: Stacked Software

16. Transparency and Legal Requests

Stacked Software is committed to protecting user privacy within the limits of applicable law.

16.1 Zero-Knowledge Architecture

Password Defense is designed using a client-side encryption model in which encryption keys are derived exclusively from the user’s master password and are never transmitted to or stored by Stacked Software.

As a result, we do not possess:

User master passwords

Encryption keys

The ability to decrypt vault contents

Mechanisms to bypass client-side encryption

Accordingly, we are cryptographically incapable of providing unencrypted vault data.

16.2 Legal Requests

If Stacked Software receives a valid legal request (such as a subpoena, court order, or other compulsory process), we may disclose only the limited information we store, as described in Section 3 of this Privacy Policy, to the extent required by law.

Such information may include:

Account email address

Username

Encrypted vault data (ciphertext only)

Synchronization metadata

We do not have access to unencrypted vault contents and therefore cannot disclose them.

Where legally permitted, we may notify affected users of such requests.

16.3 Warrant Statement

As of March 2, 2026, Stacked Software has:

Received zero National Security Letters

Received zero classified government requests for user data

Received zero warrants seeking decrypted vault contents

This statement will be updated if its accuracy changes.