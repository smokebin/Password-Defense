#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <sddl.h>
#include <io.h>
#include <fcntl.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
#include <thread>
#include <atomic>

// Obtains the current user's SID string (e.g. S-1-5-21-...)
static std::wstring GetCurrentUserSid()
{
    HANDLE hToken = NULL;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &hToken)) {
        return L"";
    }

    DWORD len = 0;
    GetTokenInformation(hToken, TokenUser, NULL, 0, &len);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || len == 0) {
        CloseHandle(hToken);
        return L"";
    }

    std::vector<BYTE> buf(len);
    if (!GetTokenInformation(hToken, TokenUser, buf.data(), len, &len)) {
        CloseHandle(hToken);
        return L"";
    }
    CloseHandle(hToken);

    TOKEN_USER* pUser = reinterpret_cast<TOKEN_USER*>(buf.data());
    LPWSTR pSidStr = NULL;
    if (!ConvertSidToStringSidW(pUser->User.Sid, &pSidStr)) {
        return L"";
    }

    std::wstring sid(pSidStr);
    LocalFree(pSidStr);
    return sid;
}

// Writes a Native Messaging framed JSON error to stdout
static void SendStdioError(const std::string& err)
{
    std::string json = "{\"ok\":false,\"error\":\"" + err + "\"}";
    uint32_t len = static_cast<uint32_t>(json.size());
    fwrite(&len, sizeof(len), 1, stdout);
    fwrite(json.data(), 1, len, stdout);
    fflush(stdout);
}

int main(int argc, char* argv[])
{
    // Native Messaging requires binary mode on stdin and stdout
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);

    std::wstring sid = GetCurrentUserSid();
    if (sid.empty()) {
        SendStdioError("failed_to_get_user_sid");
        return 1;
    }

    std::wstring pipeName = L"\\\\.\\pipe\\PasswordDefense_" + sid;

    // Connect to named pipe with retry on ERROR_PIPE_BUSY
    HANDLE hPipe = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 3; ++attempt) {
        hPipe = CreateFileW(
            pipeName.c_str(),
            GENERIC_READ | GENERIC_WRITE,
            0,
            NULL,
            OPEN_EXISTING,
            FILE_FLAG_OVERLAPPED,
            NULL
        );

        if (hPipe != INVALID_HANDLE_VALUE) {
            break;
        }

        DWORD err = GetLastError();
        if (err == ERROR_PIPE_BUSY) {
            WaitNamedPipeW(pipeName.c_str(), 1000);
        } else {
            break;
        }
    }

    if (hPipe == INVALID_HANDLE_VALUE) {
        SendStdioError("app_not_running");
        return 0; // Clean exit so browser gets the message
    }

    // Configure pipe to message read mode
    DWORD pipeMode = PIPE_READMODE_MESSAGE;
    SetNamedPipeHandleState(hPipe, &pipeMode, NULL, NULL);

    std::atomic<bool> running{ true };

    // Thread 1: Pipe -> Stdout (Desktop -> Browser)
    std::thread pipeToStdoutThread([&]() {
        std::vector<char> chunk(65536);
        std::string fullMsg;
        OVERLAPPED ovRead = {};
        ovRead.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

        while (running) {
            DWORD bytesRead = 0;
            ResetEvent(ovRead.hEvent);
            BOOL ok = ReadFile(hPipe, chunk.data(), static_cast<DWORD>(chunk.size()), &bytesRead, &ovRead);
            if (!ok) {
                DWORD err = GetLastError();
                if (err == ERROR_IO_PENDING) {
                    DWORD waitRes = WaitForSingleObject(ovRead.hEvent, 500);
                    if (waitRes == WAIT_TIMEOUT) {
                        continue;
                    }
                    ok = GetOverlappedResult(hPipe, &ovRead, &bytesRead, FALSE);
                    err = GetLastError();
                }
                if (!ok) {
                    if (err == ERROR_MORE_DATA) {
                        if (bytesRead > 0) fullMsg.append(chunk.data(), bytesRead);
                        continue; // Keep appending chunks until message ends
                    }
                    // Pipe closed or broken
                    break;
                }
            }

            if (bytesRead > 0) {
                fullMsg.append(chunk.data(), bytesRead);
            }

            // Message complete: write length-prefixed JSON to stdout
            uint32_t len = static_cast<uint32_t>(fullMsg.size());
            if (len > 0) {
                fwrite(&len, sizeof(len), 1, stdout);
                fwrite(fullMsg.data(), 1, len, stdout);
                fflush(stdout);
                fullMsg.clear();
            }
        }

        if (ovRead.hEvent) CloseHandle(ovRead.hEvent);
        running = false;
    });

    // Thread 2 (Main Thread): Stdin -> Pipe (Browser -> Desktop)
    OVERLAPPED ovWrite = {};
    ovWrite.hEvent = CreateEventW(NULL, TRUE, FALSE, NULL);

    while (running) {
        uint32_t len = 0;
        size_t n = fread(&len, sizeof(len), 1, stdin);
        if (n != 1) {
            // Stdin closed (browser closed port or navigated away)
            break;
        }

        if (len == 0 || len > 1024 * 1024) {
            // Exceeds 1MB limit or invalid
            break;
        }

        std::vector<char> msg(len);
        size_t readCount = fread(msg.data(), 1, len, stdin);
        if (readCount != len) {
            break;
        }

        // Forward to named pipe
        DWORD written = 0;
        ResetEvent(ovWrite.hEvent);
        BOOL ok = WriteFile(hPipe, msg.data(), static_cast<DWORD>(len), &written, &ovWrite);
        if (!ok) {
            if (GetLastError() == ERROR_IO_PENDING) {
                ok = GetOverlappedResult(hPipe, &ovWrite, &written, TRUE);
            }
        }
        if (!ok) {
            break;
        }
    }

    running = false;
    if (ovWrite.hEvent) CloseHandle(ovWrite.hEvent);

    // Cancel pending I/O and close pipe to unblock pipeToStdoutThread
    CancelIoEx(hPipe, NULL);
    CloseHandle(hPipe);

    if (pipeToStdoutThread.joinable()) {
        pipeToStdoutThread.join();
    }

    return 0;
}
