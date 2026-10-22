#pragma once

#include <string>

namespace bridge {

// Registers the Native Messaging Host manifests and HKCU registry keys
// for Chrome, Edge, Brave, and Firefox.
// If bridgeExePath is empty, it uses the pw-bridge.exe located alongside the running executable.
bool RegisterNativeMessagingHosts(const std::wstring& bridgeExePath = L"");

// Removes the registry keys registered for Native Messaging hosts.
bool UnregisterNativeMessagingHosts();

// Returns true if the Native Messaging Host is currently registered in HKCU for at least Chrome or Edge.
bool IsNativeMessagingRegistered();

} // namespace bridge
