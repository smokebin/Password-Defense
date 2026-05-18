// app_internal.h
// Shared declarations for application translation units.
// Included by application.cpp (core), app_import_export.cpp, app_sharing.cpp.
#pragma once

#include "credentials/credential.h"
#include "UI.h"
#include <string>
#include <vector>
#include <cstdint>
#include <set>
#include <unordered_map>

// ============================================================
// Import/Export helpers (defined in app_import_export.cpp)
// ============================================================

std::string serialize_creds_json(const std::vector<Credential>& creds);
std::vector<Credential> deserialize_creds_json(const std::string& json_str);

std::string PickOpenFilePath_DB();
std::string PickSaveFilePath_DB();
std::string PickSaveFilePath_PWM();
std::string PickOpenFilePath_PWM();
std::string PickSaveFilePath_CSV();
std::string PickSaveFilePath_KDBX();
std::string PickOpenFilePath_CSV();

enum class CsvFormat { OwnApp, Chrome, Bitwarden, LastPass, OnePassword, KeePass, Generic, Unknown };
const char* CsvFormatName(CsvFormat f);

struct CsvImportResult {
    std::vector<Credential> creds;
    CsvFormat format = CsvFormat::Unknown;
    int skipped = 0;
    std::string error;
};

CsvImportResult parse_csv_import(const std::string& file_content);

// ============================================================
// Sharing helpers (defined in app_sharing.cpp)
// ============================================================

std::string Anonbase64_encode(const std::vector<uint8_t>& data);
std::vector<uint8_t> base64_decode(const std::string& b64);
std::string base64_url_encode(const std::vector<uint8_t>& data);
std::string serialize_credential_for_share(const Credential& c, const std::string& pw);

// ============================================================
// Display helpers (defined in app_display_helpers.cpp)
// ============================================================

namespace ui { struct AccordionItem; }

uint64_t iso_to_sortkey_yyyymmddhhmmss(const std::string& iso);
uint32_t iso_to_monthkey_yyyymm(const std::string& iso);
uint32_t unix_ms_to_monthkey(int64_t ms);
const char* month_name(int m);
std::string month_label_from_key(uint32_t yyyymm);
bool str_contains_ci(const std::string& hay, const char* needle);
bool matches_search(const Credential& c, const char* needle, int search_filter);
uint32_t compute_changed_fields(const Credential& cur, const Credential* saved);
std::vector<ui::AccordionItem> build_accordion_items(
    const std::vector<Credential>& creds,
    const std::unordered_map<std::string, Credential>& saved_snapshot,
    const char* search, int search_filter, int sort_mode,
    const std::string& pill_tab_0, const std::string& pill_tab_1,
    const std::set<std::string>& groups, const std::set<std::string>& tag_filters,
    ui::OrderKey order_key, ui::OrderDir order_dir, ui::GroupMode group_mode);
