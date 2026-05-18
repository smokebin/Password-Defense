// kdbx_export.h
// Write-only KDBX4 export for KeePass/KeePassXC migration
#pragma once

#include <string>
#include <vector>
#include "../credential.h"

namespace kdbx_export {

    struct ExportResult {
        bool ok = false;
        int count = 0;
        std::string error;
    };

    // Export credentials to a KDBX4 file (KeePass compatible)
    ExportResult export_kdbx(
        const std::vector<Credential>& creds,
        const std::string& password,
        const char* file_path);
}
