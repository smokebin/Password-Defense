#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <thread>
#include <atomic>
#include <cstdint>

struct Credential;  // forward decl (credentials/credential.h)

struct PairingInfo
{
    std::string id;                     // UUID
    std::vector<uint8_t> hmac_key;      // 32 bytes raw
    std::string token;                  // current session token (empty = not logged in)
    int64_t token_expires = 0;          // unix timestamp (seconds)
    int64_t paired_at = 0;             // unix timestamp (seconds)
};

class LocalServer
{
public:
    void Start(int port = 19837);
    void Stop();
    bool IsRunning() const;
    int  GetPort() const;

    // Called by main thread when vault state changes
    void UpdateCredentials(const std::vector<Credential>& creds);
    void SetMasterKey(const std::vector<uint8_t>& key);
    void SetSalt(const std::vector<uint8_t>& salt);

    // Set by /api/creds/save when extension saves a Credential; polled by main thread
    bool HasExternalChange() const { return m_external_change.load(); }
    void ClearExternalChange() { m_external_change.store(false); }

    // Active vault tracking
    void SetActiveVaultPath(const std::string& path);

    // Vault switch signaling (HTTP thread → main thread)
    struct VaultSwitchInfo {
        std::string vault_path;
        std::vector<uint8_t> master_key;
        std::vector<uint8_t> salt;
        std::string password;  // for session_password restore
    };
    bool HasVaultSwitch() const { return m_vault_switched.load(); }
    VaultSwitchInfo ConsumeVaultSwitch();

    // Pairing management (called from UI thread)
    std::string GeneratePairingCode();   // returns 8-char code, valid 2 minutes
    void RevokePairing(const std::string& pairing_id);
    std::vector<PairingInfo> GetPairings() const;

private:
    std::thread         m_thread;
    std::atomic<bool>   m_running{false};
    std::atomic<bool>   m_external_change{false};
    std::atomic<bool>   m_vault_switched{false};
    int                 m_port = 19837;

    // Server pointer for cross-thread stop. Protected by m_svr_mutex.
    std::mutex          m_svr_mutex;
    void*               m_svr = nullptr;   // httplib::Server* (opaque to header)

    mutable std::mutex  m_data_mutex;
    std::vector<uint8_t> m_master_key;
    std::vector<uint8_t> m_salt;
    std::vector<Credential> m_credentials;  // actual Credential objects
    std::string m_active_vault_path;       // currently-open .db file path
    VaultSwitchInfo m_switch_info;         // pending vault switch (protected by m_data_mutex)

    // Pairings: pairing_id -> info
    std::unordered_map<std::string, PairingInfo> m_pairings;

    // Pending pairing challenge (short-lived)
    std::string m_pair_secret;          // the 8-char code shown to user
    int64_t m_pair_secret_expires = 0;  // unix seconds

    // Replay protection: recently seen HMAC nonces (pairing_id + timestamp + signature)
    struct ReplayEntry { std::string nonce; int64_t timestamp_ms; };
    std::vector<ReplayEntry> m_replay_log;

    // Pairing rate limiting
    int m_pair_fail_count = 0;

    void Run();
    std::string SerializeCredentials(const std::vector<Credential>& creds);
    bool VerifyMasterPassword(const std::string& password);

    // Persistence helpers
    void LoadPairings();
    void SavePairings();

    // HMAC validation — returns pairing_id on success, empty on failure (sets res status)
    struct HmacResult { bool ok; std::string pairing_id; };
    HmacResult ValidateHmac(const std::string& method, const std::string& path,
                            const std::string& body, const std::string& pairing_id_header,
                            const std::string& timestamp_header, const std::string& signature_header);

    // Token validation — returns true if valid (sets res status on failure)
    bool ValidateToken(const std::string& auth_header, const std::string& pairing_id);
};
