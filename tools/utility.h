// utility.h
#pragma once

// IMPORTANT: NOMINMAX must be BEFORE any Windows headers
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <Windows.h> // for WORD, TCHAR, SecureZeroMemory, clipboard APIs in helpers.cpp

#include <string>
#include <vector>
#include <cstddef>   // size_t
#include <cstdint>   // int64_t, uint32_t



namespace helpers
{
    // ============================================================
    // Files / paths
    // ============================================================
    bool directory_exists(std::string path);
    bool create_directories(const std::string& path);
    bool file_exists(std::string path);
    bool file_exists2(std::string file_name);

    std::string file_to_str(const std::string& filepath);
    void        str_to_file(const std::string& filename, const std::string& content);

    std::vector<unsigned char> file_to_vec(const std::string& path);

    bool bytes_to_file(const char* file_path, const std::vector<unsigned char>& bytes);

    // ============================================================
    // Basic conversions / misc
    // ============================================================
    std::vector<unsigned char> str_to_bytes(const std::string& str);
    std::string bytes_to_str(const std::vector<unsigned char>& bytes);

    bool        is_digit(const std::string& s);
    WORD        int_to_word(int val);

    // FNV-1a 32-bit hash (simple, fast, non-cryptographic)
    uint32_t    fnv1a_32(const char* s);

    std::string b64_encode(const std::string& input);

    std::vector<unsigned char> add_padding(const std::vector<unsigned char>& data);
    std::vector<unsigned char> remove_padding(const std::vector<unsigned char>& data);

    std::string tchar_to_utf8(TCHAR* tchar_arr);

    std::vector<char> uint_to_char(const unsigned int* data, size_t size);

    void        clear_string(std::string& s);

    // ============================================================
    // Clipboard
    // ============================================================
    void to_clipboard(const std::string& s);
    void clear_clipboard();

    // ============================================================
    // Time / formatting
    // ============================================================
    std::string iso_date_only(const std::string& iso);
    std::string now_iso8601_local();   // Legacy - local time with timezone
    std::string format_display_date(const std::string& iso);  // "Dec 30, 2025 · 11:00 PM"

    // UTC time functions (for sync)
    int64_t     now_unix_ms();                              // Current time as Unix milliseconds UTC
    std::string now_iso8601_utc();                          // Current time as "YYYY-MM-DDTHH:MM:SS.mmmZ"
    std::string unix_ms_to_iso8601(int64_t ms);             // Convert Unix ms to ISO-8601 UTC
    int64_t     iso8601_to_unix_ms(const std::string& iso); // Parse ISO-8601 UTC to Unix ms
    std::string format_display_date_ms(int64_t unix_ms);    // Format Unix ms for display

    // ============================================================
    // URL helper
    // ============================================================
    void open_website(const std::string& url_raw);

    // ============================================================
    // Password strength
    // ============================================================
    struct PwStrength
    {
        double entropy_bits = 0.0;  // length * log2(charset_size)
        int    score = 0;           // 0..4
        bool   hasLower = false;
        bool   hasUpper = false;
        bool   hasDigit = false;
        bool   hasSymbol = false;
        bool   allSame = false;     // "aaaaaaaa"
        bool   simpleSeq = false;   // "abcd", "1234", "qwerty"
        bool   shortPwd = false;    // length < 8
        bool   tooSequential = false;
        bool   repeatedPattern = false;
        bool   isCommonPwd = false;    // matches top common passwords list
        bool   isDictWord = false;     // base word is a common word/name
        int    charClasses = 0;
    };

    PwStrength   analyze_password(const std::string& pw);
    unsigned int strength_color(int score);     // IM_COL32 RGBA (implemented in helpers.cpp)
    const char* strength_label(int score);
    void         DrawStrengthMeter(const std::string& pw, float width = 200.0f);

    // ============================================================
    // Secure RNG + UUID + password generator
    // ============================================================
    bool        secure_rand_bytes(void* dst, size_t len);
    size_t      secure_rand_index(size_t n);
    std::string generate_uuid();  // UUID v4 (random)

    struct GenOptions
    {
        int  length = 30;
        bool use_upper = true;
        bool use_lower = true;
        bool use_digit = true;
        bool use_symbol = true;
        bool avoid_ambiguous = false;
    };

    std::string generate_password(const GenOptions& opt);
    double      estimate_entropy_bits(const GenOptions& opt);

    std::string GetDefaultBackupDir();
    void        SanitizeFilename(std::string& s);
    bool        CopyFileAtomic(const std::string& src, const std::string& dst);
    void        EnforceBackupRetention(const std::string& dir, size_t keep_count);
    std::vector<std::string> ListBackupsForVault(const std::string& vault_path);
    std::string              Basename(const std::string& path);
    bool GetFileInfo(const std::string& path, uint64_t& outSizeBytes, std::string& outLocalTimeStr);
    std::string FormatBytes(uint64_t bytes);

    struct BackupCreateResult
    {
        bool ok = false;
        std::string outPath;
        std::string err;
    };

    BackupCreateResult CreateBackupNow(const std::string& vault_path, size_t keep_count);
}
