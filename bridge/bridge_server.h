#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <vector>
#include <queue>
#include <mutex>
#include <future>
#include <thread>
#include <atomic>
#include <functional>
#include <memory>
#include "credentials/credential.h"

namespace bridge {

struct SaveResult {
    bool ok = false;
    std::string uuid;
    std::string error;
};

struct PendingCommand {
    uint64_t id = 0;
    std::string action; // "save_new", "update"
    std::string hostname;
    std::string title;
    std::string username;
    std::string password;
    std::string existing_id;
    std::promise<SaveResult> completion_promise;
};

class BridgeServer {
public:
    static BridgeServer& GetInstance();

    // Lifecycle
    void Start();
    void Stop();
    bool IsRunning() const { return m_running.load(); }

    // UI Thread Notification Hooks (called from application.cpp)
    void OnVaultUnlocked(const std::vector<Credential>& creds, const std::string& vaultPath);
    void OnVaultLocked();
    void OnVaultModified(const std::vector<Credential>& creds);

    // Command Queue Drain (called from UI thread in main loop / DisplayProgram)
    bool HasPendingWrites() const;
    void DrainPendingWrites(
        std::vector<Credential>& uiCreds,
        const std::vector<uint8_t>& masterKey,
        std::function<void()> onStateChanged);

private:
    BridgeServer();
    ~BridgeServer();

    BridgeServer(const BridgeServer&) = delete;
    BridgeServer& operator=(const BridgeServer&) = delete;

    std::atomic<bool> m_running{ false };
    std::thread m_listener_thread;

    // Mutex protecting snapshot cache and status
    mutable std::mutex m_cache_mutex;
    bool m_is_unlocked = false;
    std::string m_active_vault_path;
    std::vector<Credential> m_cached_creds;

    // Mutex protecting connected client pipe handles (for push broadcasts)
    std::mutex m_clients_mutex;
    std::vector<HANDLE> m_active_clients;

    // Mutex and queue for pending write commands (Workers -> UI thread)
    std::mutex m_queue_mutex;
    std::vector<std::shared_ptr<PendingCommand>> m_pending_writes;
    std::atomic<uint64_t> m_next_cmd_id{ 1 };

    void ListenerLoop();
    void HandleClient(HANDLE hPipe);
    void BroadcastEvent(const std::string& eventType);

    // Request handlers (running on worker thread)
    std::string ProcessMessage(const std::string& jsonRequest);
    std::string HandleStatus();
    std::string HandleMatch(const std::string& url);
    std::string HandleSearch(const std::string& query);
    std::string HandleCheck(const std::string& hostname, const std::string& username, const std::string& password);
    std::string HandleSave(const std::string& action, const std::string& hostname, const std::string& title,
                           const std::string& username, const std::string& password, const std::string& existing_id);

    static std::wstring GetCurrentUserSid();
    static bool VerifyClientProcess(HANDLE hPipe);
};

} // namespace bridge
