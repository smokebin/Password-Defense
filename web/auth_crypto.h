// auth_crypto.h
// Option C authentication: pre-hash passwords before sending to server
// Server never sees raw password - only sha256(password + email)
#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace auth {

// Compute pre-hash for server authentication
// Returns 64-char lowercase hex string: sha256(password + email)
// Email is lowercased internally to match server behavior
std::string compute_auth_hash(const std::string& password, const std::string& email);

// Convert bytes to lowercase hex string
std::string bytes_to_hex(const uint8_t* data, size_t len);
std::string bytes_to_hex(const std::vector<uint8_t>& bytes);

} // namespace auth
