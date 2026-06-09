// kdbx_export.h — write-only KDBX4 export (KeePass/KeePassXC migration)
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

    ExportResult export_kdbx(
        const std::vector<Credential>& creds,
        const std::string& password,
        const char* file_path);
}
