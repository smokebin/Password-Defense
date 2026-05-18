// app_import_export.cpp
// File picker dialogs, CSV import parsing, credential JSON serialization.

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <string>
#include <vector>
#include "app_internal.h"
#include "third_party/json.hpp"
#include "tools/utility.h"

// ============================================================
// JSON serialization (for undo, in-memory only)
// ============================================================

std::string serialize_creds_json(const std::vector<Credential>& creds)
{
    nlohmann::json j = nlohmann::json::array();
    for (const auto& c : creds) {
        nlohmann::json hist = nlohmann::json::array();
        for (const auto& h : c.password_history) {
            hist.push_back({{"password", h.password}, {"changed_at_ms", h.changed_at_ms}});
        }
        j.push_back({
            {"id", c.id}, {"uuid", c.uuid}, {"title", c.title}, {"password", c.password},
            {"user", c.user}, {"email", c.email}, {"website", c.website}, {"group", c.group},
            {"notes", c.notes}, {"totp_secret", c.totp_secret}, {"is_favorite", c.is_favorite}, {"is_pinned", c.is_pinned},
            {"expires_at_ms", c.expires_at_ms}, {"expiry_action", c.expiry_action},
            {"type", static_cast<int>(c.type)},
            {"card_number", c.card_number}, {"card_expiry", c.card_expiry},
            {"card_cvv", c.card_cvv}, {"card_brand", c.card_brand},
            {"cardholder_name", c.cardholder_name},
            {"card_address", c.card_address},
            {"card_city", c.card_city},
            {"card_postal_code", c.card_postal_code},
            {"full_name", c.full_name}, {"id_type", c.id_type},
            {"id_number", c.id_number}, {"date_of_birth", c.date_of_birth},
            {"expiry_date", c.expiry_date}, {"country", c.country},
            {"address", c.address}, {"phone", c.phone},
            {"created_at_ms", c.created_at_ms}, {"updated_at_ms", c.updated_at_ms},
            {"password_history", hist}
        });
    }
    return j.dump();
}

std::vector<Credential> deserialize_creds_json(const std::string& json_str)
{
    std::vector<Credential> result;
    try {
        auto j = nlohmann::json::parse(json_str);
        for (const auto& x : j) {
            Credential c{};
            c.id = x.value("id", 0);
            c.uuid = x.value("uuid", "");
            c.title = x.value("title", "");
            c.password = x.value("password", "");
            c.user = x.value("user", "");
            c.email = x.value("email", "");
            c.website = x.value("website", "");
            c.group = x.value("group", "");
            c.notes = x.value("notes", "");
            c.totp_secret = x.value("totp_secret", "");
            c.is_favorite = x.value("is_favorite", false);
            c.is_pinned = x.value("is_pinned", false);
            c.expires_at_ms = x.value("expires_at_ms", int64_t(0));
            c.expiry_action = x.value("expiry_action", 0);
            c.type = static_cast<CredType>(x.value("type", 0));
            c.card_number     = x.value("card_number", "");
            c.card_expiry     = x.value("card_expiry", "");
            c.card_cvv        = x.value("card_cvv", "");
            c.card_brand      = x.value("card_brand", "");
            c.cardholder_name = x.value("cardholder_name", "");
            c.card_address    = x.value("card_address", "");
            c.card_city       = x.value("card_city", "");
            c.card_postal_code = x.value("card_postal_code", "");
            c.full_name     = x.value("full_name", "");
            c.id_type       = x.value("id_type", "");
            c.id_number     = x.value("id_number", "");
            c.date_of_birth = x.value("date_of_birth", "");
            c.expiry_date   = x.value("expiry_date", "");
            c.country       = x.value("country", "");
            c.address       = x.value("address", "");
            c.phone         = x.value("phone", "");
            c.created_at_ms = x.value("created_at_ms", int64_t(0));
            c.updated_at_ms = x.value("updated_at_ms", int64_t(0));
            for (const auto& h : x.value("password_history", nlohmann::json::array())) {
                Credential::PasswordHistoryEntry e;
                e.password      = h.value("password", "");
                e.changed_at_ms = h.value("changed_at_ms", int64_t(0));
                c.password_history.push_back(std::move(e));
            }
            result.push_back(c);
        }
    } catch (...) {}
    return result;
}

// ============================================================
// File picker dialogs
// ============================================================

std::string PickOpenFilePath_DB()
{
    char file[MAX_PATH] = { 0 };

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Vault Database (*.db)\0*.db\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn))
        return std::string(file);
    return {};
}

std::string PickSaveFilePath_DB()
{
    char file[MAX_PATH] = { 0 };

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Vault Database (*.db)\0*.db\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "db";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn))
        return std::string(file);
    return {};
}

std::string PickSaveFilePath_PWM()
{
    char file[MAX_PATH] = { 0 };

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Encrypted Export (*.pwm)\0*.pwm\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "pwm";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn))
        return std::string(file);
    return {};
}

std::string PickOpenFilePath_PWM()
{
    char file[MAX_PATH] = { 0 };

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "Encrypted Export (*.pwm)\0*.pwm\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn))
        return std::string(file);
    return {};
}

std::string PickSaveFilePath_CSV()
{
    char file[MAX_PATH] = { 0 };

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "CSV File (*.csv)\0*.csv\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "csv";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn))
        return std::string(file);
    return {};
}

std::string PickSaveFilePath_KDBX()
{
    char file[MAX_PATH] = { 0 };

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "KeePass Database (*.kdbx)\0*.kdbx\0";
    ofn.nFilterIndex = 1;
    ofn.lpstrDefExt = "kdbx";
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT | OFN_NOCHANGEDIR;

    if (GetSaveFileNameA(&ofn))
        return std::string(file);
    return {};
}

std::string PickOpenFilePath_CSV()
{
    char file[MAX_PATH] = { 0 };

    OPENFILENAMEA ofn{};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = "CSV File (*.csv)\0*.csv\0All Files\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

    if (GetOpenFileNameA(&ofn))
        return std::string(file);
    return {};
}

// ============================================================
// CSV Import: parser, format detection, mapping
// ============================================================

static std::vector<std::string> csv_split_line(const std::string& line)
{
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;

    for (size_t i = 0; i < line.size(); ++i)
    {
        char c = line[i];
        if (in_quotes)
        {
            if (c == '"')
            {
                if (i + 1 < line.size() && line[i + 1] == '"')
                {
                    field += '"';
                    ++i;
                }
                else
                {
                    in_quotes = false;
                }
            }
            else
            {
                field += c;
            }
        }
        else
        {
            if (c == '"')
            {
                in_quotes = true;
            }
            else if (c == ',')
            {
                fields.push_back(field);
                field.clear();
            }
            else
            {
                field += c;
            }
        }
    }
    fields.push_back(field);
    return fields;
}

static std::vector<std::string> csv_split_rows(const std::string& text)
{
    std::vector<std::string> rows;
    std::string row;
    bool in_quotes = false;

    for (size_t i = 0; i < text.size(); ++i)
    {
        char c = text[i];
        if (c == '"') in_quotes = !in_quotes;

        if (!in_quotes && (c == '\n' || c == '\r'))
        {
            if (!row.empty())
                rows.push_back(row);
            row.clear();
            if (c == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
        }
        else
        {
            row += c;
        }
    }
    if (!row.empty())
        rows.push_back(row);
    return rows;
}

const char* CsvFormatName(CsvFormat f)
{
    switch (f) {
    case CsvFormat::OwnApp:      return "this app";
    case CsvFormat::Chrome:      return "Chrome";
    case CsvFormat::Bitwarden:   return "Bitwarden";
    case CsvFormat::LastPass:    return "LastPass";
    case CsvFormat::OnePassword: return "1Password";
    case CsvFormat::KeePass:     return "KeePass";
    case CsvFormat::Generic:     return "CSV";
    default:                     return "Unknown";
    }
}

static std::string str_lower(const std::string& s)
{
    std::string out = s;
    for (auto& c : out) c = (char)tolower((unsigned char)c);
    return out;
}

// Strip surrounding quotes and whitespace from a header column name
static std::string normalize_header(const std::string& s)
{
    std::string out = str_lower(s);
    // trim whitespace
    while (!out.empty() && (out.front() == ' ' || out.front() == '\t')) out.erase(out.begin());
    while (!out.empty() && (out.back() == ' ' || out.back() == '\t')) out.pop_back();
    // strip surrounding quotes
    if (out.size() >= 2 && out.front() == '"' && out.back() == '"')
        out = out.substr(1, out.size() - 2);
    return out;
}

static CsvFormat detect_csv_format(const std::vector<std::string>& header_fields)
{
    // Build a set of normalized header names
    std::vector<std::string> cols;
    for (const auto& f : header_fields)
        cols.push_back(normalize_header(f));

    auto has = [&](const std::string& name) {
        for (const auto& c : cols)
            if (c == name) return true;
        return false;
    };

    auto starts_with = [&](const std::vector<std::string>& prefix) {
        if (cols.size() < prefix.size()) return false;
        for (size_t i = 0; i < prefix.size(); ++i)
            if (cols[i] != prefix[i]) return false;
        return true;
    };

    // 1. Bitwarden: has login_uri
    if (has("login_uri")) return CsvFormat::Bitwarden;

    // 2. LastPass: has grouping and fav
    if (has("grouping") && has("fav")) return CsvFormat::LastPass;

    // 3. Own app: starts with type,title,username,password,email,url,group
    if (starts_with({"type", "title", "username", "password", "email", "url", "group"}) && cols.size() >= 25)
        return CsvFormat::OwnApp;

    // 4. KeePass: has group, title, username, password (and typically URL, Notes)
    if (has("group") && has("title") && has("username") && has("password") && has("url"))
        return CsvFormat::KeePass;

    // 5. Chrome: name,url,username,password (exactly or with note)
    if (cols.size() >= 4 && cols[0] == "name" && cols[1] == "url" && cols[2] == "username" && cols[3] == "password")
        return CsvFormat::Chrome;

    // 6. 1Password: has title and password
    if (has("title") && has("password"))
        return CsvFormat::OnePassword;

    // 7. Generic: try common column names
    if (has("password") || has("pass"))
        return CsvFormat::Generic;

    return CsvFormat::Unknown;
}

static CredType parse_cred_type(const std::string& s)
{
    std::string low = str_lower(s);
    if (low == "credit card")  return CredType::CreditCard;
    if (low == "identity")     return CredType::Identity;
    if (low == "secure note")  return CredType::SecureNote;
    return CredType::Password;
}

static bool str_to_bool(const std::string& s)
{
    std::string low = str_lower(s);
    return low == "true" || low == "1" || low == "yes";
}

CsvImportResult parse_csv_import(const std::string& file_content)
{
    CsvImportResult result;
    auto rows = csv_split_rows(file_content);

    if (rows.empty()) {
        result.error = "File is empty";
        return result;
    }

    auto header_fields = csv_split_line(rows[0]);
    result.format = detect_csv_format(header_fields);

    if (result.format == CsvFormat::Unknown) {
        result.error = "Could not detect CSV format from header row";
        return result;
    }

    // Build column index map (normalized name -> index)
    std::vector<std::string> cols;
    for (const auto& f : header_fields)
        cols.push_back(normalize_header(f));

    auto col_idx = [&](const std::string& name) -> int {
        for (size_t i = 0; i < cols.size(); ++i)
            if (cols[i] == name) return (int)i;
        return -1;
    };

    auto get_field = [](const std::vector<std::string>& fields, int idx) -> std::string {
        if (idx < 0 || idx >= (int)fields.size()) return "";
        return fields[idx];
    };

    for (size_t r = 1; r < rows.size(); ++r)
    {
        auto fields = csv_split_line(rows[r]);
        if (fields.empty()) { result.skipped++; continue; }

        // Skip rows that are all empty
        bool all_empty = true;
        for (const auto& f : fields)
            if (!f.empty()) { all_empty = false; break; }
        if (all_empty) { result.skipped++; continue; }

        Credential c;
        c.uuid.clear();

        switch (result.format)
        {
        case CsvFormat::OwnApp:
        {
            c.type           = parse_cred_type(get_field(fields, 0));
            c.title          = get_field(fields, 1);
            c.user           = get_field(fields, 2);
            c.password       = get_field(fields, 3);
            c.email          = get_field(fields, 4);
            c.website        = get_field(fields, 5);
            c.group          = get_field(fields, 6);
            c.notes          = get_field(fields, 7);
            c.is_favorite    = str_to_bool(get_field(fields, 8));
            c.is_pinned      = str_to_bool(get_field(fields, 9));
            c.cardholder_name = get_field(fields, 10);
            c.card_number    = get_field(fields, 11);
            c.card_expiry    = get_field(fields, 12);
            c.card_cvv       = get_field(fields, 13);
            c.card_brand     = get_field(fields, 14);
            c.card_address   = get_field(fields, 15);
            c.card_city      = get_field(fields, 16);
            c.card_postal_code = get_field(fields, 17);
            c.full_name      = get_field(fields, 18);
            c.id_type        = get_field(fields, 19);
            c.id_number      = get_field(fields, 20);
            c.date_of_birth  = get_field(fields, 21);
            c.expiry_date    = get_field(fields, 22);
            c.country        = get_field(fields, 23);
            c.address        = get_field(fields, 24);
            c.phone          = get_field(fields, 25);
            c.created_at_ms  = helpers::iso8601_to_unix_ms(get_field(fields, 26));
            c.updated_at_ms  = helpers::iso8601_to_unix_ms(get_field(fields, 27));
            break;
        }
        case CsvFormat::Chrome:
        {
            c.title    = get_field(fields, col_idx("name"));
            c.website  = get_field(fields, col_idx("url"));
            c.user     = get_field(fields, col_idx("username"));
            c.password = get_field(fields, col_idx("password"));
            int ni = col_idx("note");
            if (ni < 0) ni = col_idx("notes");
            c.notes    = get_field(fields, ni);
            break;
        }
        case CsvFormat::Bitwarden:
        {
            c.group       = get_field(fields, col_idx("folder"));
            c.title       = get_field(fields, col_idx("name"));
            c.notes       = get_field(fields, col_idx("notes"));
            c.website     = get_field(fields, col_idx("login_uri"));
            c.user        = get_field(fields, col_idx("login_username"));
            c.password    = get_field(fields, col_idx("login_password"));
            c.totp_secret = get_field(fields, col_idx("login_totp"));
            break;
        }
        case CsvFormat::LastPass:
        {
            c.website     = get_field(fields, col_idx("url"));
            c.user        = get_field(fields, col_idx("username"));
            c.password    = get_field(fields, col_idx("password"));
            c.title       = get_field(fields, col_idx("name"));
            c.group       = get_field(fields, col_idx("grouping"));
            c.notes       = get_field(fields, col_idx("extra"));
            c.is_favorite = str_to_bool(get_field(fields, col_idx("fav")));
            break;
        }
        case CsvFormat::OnePassword:
        {
            c.title    = get_field(fields, col_idx("title"));
            c.user     = get_field(fields, col_idx("username"));
            c.password = get_field(fields, col_idx("password"));
            c.website  = get_field(fields, col_idx("url"));
            c.notes    = get_field(fields, col_idx("notes"));
            break;
        }
        case CsvFormat::KeePass:
        {
            c.group    = get_field(fields, col_idx("group"));
            c.title    = get_field(fields, col_idx("title"));
            c.user     = get_field(fields, col_idx("username"));
            c.password = get_field(fields, col_idx("password"));
            c.website  = get_field(fields, col_idx("url"));
            c.notes    = get_field(fields, col_idx("notes"));
            break;
        }
        case CsvFormat::Generic:
        {
            // Try multiple common column name variants
            auto try_cols = [&](std::initializer_list<const char*> names) -> std::string {
                for (auto name : names) {
                    int i = col_idx(name);
                    if (i >= 0) return get_field(fields, i);
                }
                return "";
            };
            c.title    = try_cols({"title", "name", "entry"});
            c.user     = try_cols({"username", "user", "login", "login_username"});
            c.password = try_cols({"password", "pass", "login_password"});
            c.website  = try_cols({"url", "website", "uri", "login_uri"});
            c.notes    = try_cols({"notes", "note", "comment", "comments", "extra"});
            c.group    = try_cols({"group", "folder", "grouping", "category"});
            c.email    = try_cols({"email", "e-mail"});
            break;
        }
        default:
            break;
        }

        // Field length limits: prevent oversized fields from malformed CSVs
        static constexpr size_t kMaxField = 1024;       // 1 KB for most fields
        static constexpr size_t kMaxNotes = 65536;      // 64 KB for notes

        auto cap = [](std::string& s, size_t max) {
            if (s.size() > max) s.resize(max);
        };

        cap(c.title, kMaxField);
        cap(c.user, kMaxField);
        cap(c.password, kMaxField);
        cap(c.email, kMaxField);
        cap(c.website, kMaxField);
        cap(c.group, kMaxField);
        cap(c.notes, kMaxNotes);
        cap(c.totp_secret, kMaxField);
        cap(c.card_number, kMaxField);
        cap(c.card_expiry, kMaxField);
        cap(c.card_cvv, kMaxField);
        cap(c.card_brand, kMaxField);
        cap(c.cardholder_name, kMaxField);
        cap(c.card_address, kMaxField);
        cap(c.card_city, kMaxField);
        cap(c.card_postal_code, kMaxField);
        cap(c.full_name, kMaxField);
        cap(c.id_type, kMaxField);
        cap(c.id_number, kMaxField);
        cap(c.date_of_birth, kMaxField);
        cap(c.expiry_date, kMaxField);
        cap(c.country, kMaxField);
        cap(c.address, kMaxField);
        cap(c.phone, kMaxField);

        result.creds.push_back(std::move(c));
    }

    return result;
}
