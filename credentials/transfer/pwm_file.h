// pwm_file.h — encrypted .pwm export/import (vault backup and transfer)
#pragma once

#include <string>
#include <vector>
#include "../credential.h"

namespace pwm_file {

    struct ExportResult {
        bool ok = false;
        int count = 0;
        std::string error;
    };

    struct ImportResult {
        bool ok = false;
        int count = 0;
        std::string error;
        std::vector<Credential> creds;
    };

    ExportResult export_pwm(
        const std::vector<Credential>& creds,
        const std::string& export_password,
        const char* file_path);

    ImportResult import_pwm(
        const std::string& import_password,
        const std::string& file_path);
}
