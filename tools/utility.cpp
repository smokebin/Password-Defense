
// utility.cpp
#include "utility.h"
#include "../third_party/imgui/imgui.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>

#define SODIUM_STATIC
#include <sodium.h>
#include <shellapi.h>
#pragma comment(lib, "libsodium.lib")
#pragma comment(lib, "shell32.lib")

#include <algorithm>
#include <cctype>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <thread>
#include <chrono>
#include <vector>
#include <string>
#include <sstream>
#include <ShlObj.h>
#include <unordered_set>

#include <sys/stat.h>
#include <sqlite3.h>


namespace helpers
{
    // ============================================================
    // TIME
    // ============================================================
    std::string iso_date_only(const std::string& iso)
    {
        if (iso.empty()) return {};
        auto pos = iso.find('T');
        return pos == std::string::npos ? iso : iso.substr(0, pos);
    }

    std::string format_display_date(const std::string& iso)
    {
        // Parse ISO-8601: "YYYY-MM-DDTHH:MM:SS..." (ignore timezone)
        // Output: "Dec 30, 2025 · 11:00 PM"
        if (iso.size() < 19) return iso;  // fallback to raw if malformed

        std::tm tm{};
        // Parse: YYYY-MM-DDTHH:MM:SS
        if (std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d",
            &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
            &tm.tm_hour, &tm.tm_min, &tm.tm_sec) != 6)
        {
            return iso;  // fallback
        }

        tm.tm_year -= 1900;  // tm_year is years since 1900
        tm.tm_mon -= 1;      // tm_mon is 0-11

        char buf[64];
        std::strftime(buf, sizeof(buf), "%b %d, %Y - %I:%M %p", &tm);
        return std::string(buf);
    }

    std::string now_iso8601_local()
    {
        using namespace std::chrono;
        const auto now = system_clock::now();
        const time_t tt = system_clock::to_time_t(now);

        std::tm tm{};
#if defined(_WIN32)
        localtime_s(&tm, &tt);
#else
        localtime_r(&tt, &tm);
#endif

        // Windows timezone offset (minutes west of UTC in Bias)
        TIME_ZONE_INFORMATION tzi{};
        DWORD st = GetTimeZoneInformation(&tzi);

        LONG bias = tzi.Bias;
        if (st == TIME_ZONE_ID_DAYLIGHT)       bias += tzi.DaylightBias;
        else if (st == TIME_ZONE_ID_STANDARD)  bias += tzi.StandardBias;

        // Convert bias (minutes WEST of UTC) -> offset minutes EAST of UTC
        int offset_min = -bias;
        char sign = offset_min >= 0 ? '+' : '-';
        offset_min = std::abs(offset_min);
        int off_h = offset_min / 60;
        int off_m = offset_min % 60;

        char buf[40];
        std::snprintf(buf, sizeof(buf),
            "%04d-%02d-%02dT%02d:%02d:%02d%c%02d:%02d",
            tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
            tm.tm_hour, tm.tm_min, tm.tm_sec,
            sign, off_h, off_m);

        return std::string(buf);
    }

    // ============================================================
    // UTC TIME (for sync)
    // ============================================================
    int64_t now_unix_ms()
    {
        using namespace std::chrono;
        return duration_cast<milliseconds>(
            system_clock::now().time_since_epoch()
        ).count();
    }

    std::string now_iso8601_utc()
    {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tm_utc{};
#ifdef _WIN32
        gmtime_s(&tm_utc, &time_t_now);
#else
        gmtime_r(&time_t_now, &tm_utc);
#endif

        char buf[32];
        std::snprintf(buf, sizeof(buf),
            "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
            tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
            tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec,
            (int)ms.count());

        return std::string(buf);
    }

    std::string unix_ms_to_iso8601(int64_t ms)
    {
        auto seconds = ms / 1000;
        auto remainder_ms = ms % 1000;

        std::time_t time_t_val = static_cast<std::time_t>(seconds);
        std::tm tm_utc{};
#ifdef _WIN32
        gmtime_s(&tm_utc, &time_t_val);
#else
        gmtime_r(&time_t_val, &tm_utc);
#endif

        char buf[32];
        std::snprintf(buf, sizeof(buf),
            "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
            tm_utc.tm_year + 1900, tm_utc.tm_mon + 1, tm_utc.tm_mday,
            tm_utc.tm_hour, tm_utc.tm_min, tm_utc.tm_sec,
            (int)remainder_ms);

        return std::string(buf);
    }

    int64_t iso8601_to_unix_ms(const std::string& iso)
    {
        if (iso.empty()) return 0;

        std::tm tm{};
        int ms = 0;

        // Parse "2025-01-11T23:45:30.123Z" or "2025-01-11T23:45:30Z"
        int parsed = std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d.%d",
            &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
            &tm.tm_hour, &tm.tm_min, &tm.tm_sec, &ms);

        if (parsed < 6) return 0;  // Failed to parse

        tm.tm_year -= 1900;
        tm.tm_mon -= 1;

#ifdef _WIN32
        std::time_t time = _mkgmtime(&tm);
#else
        std::time_t time = timegm(&tm);
#endif

        if (time == -1) return 0;

        return static_cast<int64_t>(time) * 1000 + ms;
    }

    std::string format_display_date_ms(int64_t unix_ms)
    {
        if (unix_ms <= 0) return "";

        // Convert to local time for display
        std::time_t seconds = static_cast<std::time_t>(unix_ms / 1000);
        std::tm tm_local{};
#ifdef _WIN32
        localtime_s(&tm_local, &seconds);
#else
        localtime_r(&seconds, &tm_local);
#endif

        char buf[64];
        std::strftime(buf, sizeof(buf), "%b %d, %Y - %I:%M %p", &tm_local);
        return std::string(buf);
    }

    // ============================================================
    // URL / SHELL
    // ============================================================
    void open_website(const std::string& url_raw)
    {
        if (url_raw.empty())
            return;

        std::string url = url_raw;
        if (url.find("://") == std::string::npos)
            url = "https://" + url;

        ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }

    // ============================================================
    // PASSWORD STRENGTH METER (existing)
    // ============================================================

    // ---- internal helpers
    static inline bool _is_symbol(unsigned char c) { return !std::isalnum(c); }

    static inline size_t _charset_size_of(const std::string& s, PwStrength& out)
    {
        bool lower = false, upper = false, digit = false, symbol = false;
        for (unsigned char c : s)
        {
            if (std::islower(c)) lower = true;
            else if (std::isupper(c)) upper = true;
            else if (std::isdigit(c)) digit = true;
            else if (_is_symbol(c))   symbol = true;
        }
        out.hasLower = lower; out.hasUpper = upper; out.hasDigit = digit; out.hasSymbol = symbol;

        size_t N = 0;
        if (lower)  N += 26;
        if (upper)  N += 26;
        if (digit)  N += 10;
        if (symbol) N += 33; // approx printable symbols set
        return (N == 0) ? 1 : N;
    }

    static inline bool _all_same_char(const std::string& s)
    {
        if (s.size() <= 1) return false;
        for (size_t i = 1; i < s.size(); ++i) if (s[i] != s[0]) return false;
        return true;
    }

    // Simple sequence detector: >=4 ascending/descending ascii or common bad words
    static inline bool _has_simple_sequence(const std::string& s)
    {
        if (s.size() < 4) return false;
        int runAsc = 1, runDesc = 1;
        for (size_t i = 1; i < s.size(); ++i)
        {
            if ((int)s[i] == (int)s[i - 1] + 1) { runAsc++;  runDesc = 1; }
            else if ((int)s[i] == (int)s[i - 1] - 1) { runDesc++; runAsc = 1; }
            else { runAsc = runDesc = 1; }

            if (runAsc >= 4 || runDesc >= 4) return true;
        }

        std::string low = s;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        const char* bads[] = { "password", "qwerty", "letmein", "admin", "welcome", "iloveyou" };
        for (auto* b : bads) if (low.find(b) != std::string::npos) return true;
        return false;
    }

    // ---- Common passwords & dictionary words for strength detection ----
    static const std::unordered_set<std::string>& _common_passwords()
    {
        static const std::unordered_set<std::string> s = {
            // Top common passwords
            "123456","password","12345678","qwerty","123456789","12345","1234",
            "111111","1234567","dragon","123123","baseball","abc123","football",
            "monkey","letmein","shadow","master","666666","qwertyuiop",
            "123321","mustang","1234567890","michael","654321","superman",
            "1qaz2wsx","7777777","121212","000000","qazwsx","123qwe","killer",
            "trustno1","jordan","jennifer","zxcvbnm","asdfgh","hunter","buster",
            "soccer","harley","batman","andrew","tigger","sunshine","iloveyou",
            "2000","charlie","robert","thomas","hockey","ranger","daniel",
            "starwars","klaster","112233","george","computer","michelle",
            "jessica","pepper","1111","zxcvbn","555555","11111111","131313",
            "freedom","777777","pass","maggie","159753","aaaaaa","ginger",
            "princess","joshua","cheese","amanda","summer","love","ashley",
            "nicole","chelsea","biteme","matthew","access","yankees","987654321",
            "dallas","austin","thunder","taylor","matrix","william","corvette",
            "hello","martin","heather","secret","merlin","diamond","1234qwer",
            "gfhjkm","hammer","silver","222222","88888888","anthony","justin",
            "test","bailey","q1w2e3r4t5","patrick","internet","scooter",
            "orange","golfer","cookie","richard","samantha","banana","abcdef",
            "letmein1","1q2w3e4r","welcome","welcome1","p@ssw0rd","passw0rd",
            "password1","password123","admin","admin123","root","toor","login",
            "abc123456","qwerty123","iloveu","trustno1","changeme",
            // Common with trailing digits
            "password1","password12","password2","qwerty1","qwerty12",
            "dragon1","monkey1","shadow1","master1","jordan23",
        };
        return s;
    }

    static const std::unordered_set<std::string>& _common_words()
    {
        static const std::unordered_set<std::string> s = {
            // Common names
            "james","john","robert","michael","david","richard","joseph","thomas",
            "charles","christopher","daniel","matthew","anthony","mark","donald",
            "steven","paul","andrew","joshua","kenneth","kevin","brian","george",
            "timothy","ronald","edward","jason","jeffrey","ryan","jacob","gary",
            "nicholas","eric","jonathan","stephen","larry","justin","scott","brandon",
            "benjamin","samuel","raymond","gregory","frank","alexander","patrick",
            "jack","dennis","jerry","tyler","aaron","jose","nathan","henry","peter",
            "adam","douglas","zachary","walter","kyle","harold","carl","jeremy",
            "roger","keith","gerald","sean","austin","albert","arthur","lawrence",
            "terry","jesse","dylan","bryan","joe","jordan","billy","bruce","gabriel",
            "mary","patricia","jennifer","linda","barbara","elizabeth","susan",
            "jessica","sarah","karen","lisa","nancy","betty","margaret","sandra",
            "ashley","dorothy","kimberly","emily","donna","michelle","carol",
            "amanda","melissa","deborah","stephanie","rebecca","sharon","laura",
            "cynthia","kathleen","amy","angela","shirley","anna","brenda","pamela",
            "emma","nicole","helen","samantha","katherine","christine","debra",
            "rachel","carolyn","janet","catherine","maria","heather","diane","ruth",
            "julie","olivia","joyce","virginia","victoria","kelly","lauren","christina",
            "joan","evelyn","judith","megan","andrea","cheryl","hannah","jacqueline",
            "martha","gloria","teresa","ann","sara","madison","frances","kathryn",
            "janice","jean","abigail","alice","judy","sophia","grace","denise",
            "amber","doris","marilyn","danielle","beverly","isabella","theresa",
            "diana","natalie","brittany","charlotte","marie","kayla","alexis","lori",
            "maggie","charlie","bailey","buster","ginger","princess","cookie",
            "shadow","tiger","buddy","ranger","harley","lucky","sammy","max",
            "rocky","tucker","bear","molly","oscar","winston","casey","bentley",
            // Common words
            "password","dragon","master","monkey","letmein","football","baseball",
            "soccer","hockey","basketball","tennis","cricket","batman","superman",
            "spider","pokemon","naruto","gaming","gamer","player","winner","loser",
            "hello","welcome","goodbye","secret","private","access","control",
            "freedom","liberty","justice","power","energy","thunder","lightning",
            "silver","golden","diamond","crystal","shadow","phantom","ghost",
            "ninja","samurai","warrior","knight","prince","queen","king",
            "summer","winter","spring","autumn","sunshine","rainbow","starlight",
            "heaven","angel","devil","demon","killer","hunter","sniper",
            "hacker","cyber","matrix","system","network","server","computer",
            "laptop","mobile","apple","google","amazon","facebook","twitter",
            "orange","banana","cherry","mango","strawberry","blueberry",
            "chocolate","vanilla","coffee","cookie","butter","cheese","pepper",
            "mustard","ketchup","pizza","burger","taco","sushi",
            "love","lover","forever","trust","faith","hope","dream",
        };
        return s;
    }

    // Strip trailing digits/symbols to extract base word: "maggie21" -> "maggie"
    static std::string _extract_base_word(const std::string& pw)
    {
        std::string low = pw;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        // Strip trailing non-alpha
        size_t end = low.size();
        while (end > 0 && !std::isalpha((unsigned char)low[end - 1])) --end;
        // Strip leading non-alpha
        size_t start = 0;
        while (start < end && !std::isalpha((unsigned char)low[start])) ++start;
        if (end - start < 3) return ""; // too short to be a word
        return low.substr(start, end - start);
    }

    static bool _is_common_password(const std::string& pw)
    {
        std::string low = pw;
        std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        return _common_passwords().count(low) > 0;
    }

    static bool _is_dictionary_based(const std::string& pw)
    {
        std::string base = _extract_base_word(pw);
        if (base.empty()) return false;
        // Check the base word itself
        if (_common_words().count(base) > 0) return true;
        // Also check common passwords for base word (e.g. "dragon" from "dragon99!")
        if (_common_passwords().count(base) > 0) return true;
        return false;
    }

    PwStrength analyze_password(const std::string& pw)
    {
        PwStrength r;
        const int L = (int)pw.size();
        if (L == 0) { r.score = 0; r.entropy_bits = 0.0; r.shortPwd = true; return r; }

        size_t N = _charset_size_of(pw, r);
        r.entropy_bits = L * std::log2((double)N);

        r.allSame = _all_same_char(pw);
        r.simpleSeq = _has_simple_sequence(pw);
        r.shortPwd = (L < 8);
        r.isCommonPwd = _is_common_password(pw);
        r.isDictWord = !r.isCommonPwd && _is_dictionary_based(pw); // don't double-flag

        int base;
        if (r.entropy_bits < 28)       base = 0;
        else if (r.entropy_bits < 36)  base = 1;
        else if (r.entropy_bits < 60)  base = 2;
        else if (r.entropy_bits < 80)  base = 3;
        else                           base = 4;

        if (r.shortPwd)    base = std::max(0, base - 1);
        if (r.allSame)     base = std::max(0, base - 2);
        if (r.simpleSeq)   base = std::max(0, base - 1);
        if (r.isCommonPwd) base = std::max(0, base - 3);
        if (r.isDictWord)  base = std::max(0, base - 2);

        int classes = (int)r.hasLower + (int)r.hasUpper + (int)r.hasDigit + (int)r.hasSymbol;
        if (classes >= 3 && base < 4 && !r.shortPwd && !r.isCommonPwd && !r.isDictWord)
            base = std::min(4, base + 1);

        r.score = std::clamp(base, 0, 4);
        return r;
    }

    unsigned int strength_color(int score)
    {
        switch (score)
        {
        case 0: return IM_COL32(220, 60, 60, 255);   // Very Weak
        case 1: return IM_COL32(240, 140, 60, 255);  // Weak
        case 2: return IM_COL32(240, 210, 70, 255);  // Fair
        case 3: return IM_COL32(80, 180, 90, 255);   // Strong
        default:return IM_COL32(60, 200, 200, 255);  // Excellent
        }
    }

    const char* strength_label(int score)
    {
        switch (score)
        {
        case 0: return "VERY WEAK";
        case 1: return "WEAK";
        case 2: return "FAIR";
        case 3: return "STRONG";
        default:return "EXCELLENT";
        }
    }

    void DrawStrengthMeter(const std::string& pw, float width)
    {
        PwStrength s = analyze_password(pw);
        float frac = s.score / 4.0f;

        ImVec2 barSize(width, 8.0f);
        ImU32  col = strength_color(s.score);

        ImGui::Text("%s ", strength_label(s.score));

        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + barSize.x, p0.y + barSize.y);

        dl->AddRectFilled(p0, p1, IM_COL32(50, 50, 50, 130), 4.0f); // background
        ImVec2 pFill = ImVec2(p0.x + barSize.x * frac, p1.y);
        dl->AddRectFilled(p0, pFill, col, 4.0f);                    // foreground

        ImGui::Dummy(barSize); // advance cursor

        if (s.shortPwd)  ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "use at least 8+ characters");
        if (s.allSame)   ImGui::TextColored(ImVec4(1, 0.5f, 0.5f, 1), "don't repeat the same character");
        if (s.simpleSeq) ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "avoid simple sequences like 1234 or abcd");
    }

    // ============================================================
    // SECURE RNG (using libsodium)
    // ============================================================
    bool secure_rand_bytes(void* dst, size_t len)
    {
        if (!dst || len == 0) return false;
        randombytes_buf(dst, len);
        return true;
    }

    size_t secure_rand_index(size_t n)
    {
        if (n == 0) return 0;
        return (size_t)randombytes_uniform((uint32_t)n);
    }

    // ============================================================
    // UUID GENERATOR (v4 random)
    // ============================================================
    std::string generate_uuid()
    {
        unsigned char bytes[16];
        randombytes_buf(bytes, 16);

        // Set version 4 (random UUID)
        bytes[6] = (bytes[6] & 0x0F) | 0x40;
        // Set variant (RFC 4122)
        bytes[8] = (bytes[8] & 0x3F) | 0x80;

        char buf[37];
        snprintf(buf, sizeof(buf),
            "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
            bytes[0], bytes[1], bytes[2], bytes[3],
            bytes[4], bytes[5],
            bytes[6], bytes[7],
            bytes[8], bytes[9],
            bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);

        return std::string(buf);
    }

    // ============================================================
    // PASSWORD GENERATOR (moved from application.cpp)
    // ============================================================
    static inline std::string build_charset(const GenOptions& opt,
        std::string& upper, std::string& lower,
        std::string& digit, std::string& symbol)
    {
        upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
        lower = "abcdefghijklmnopqrstuvwxyz";
        digit = "0123456789";
        symbol = "~!@#$%^&*()_+-={}[]|:;\"'<>,.?/";

        if (opt.avoid_ambiguous)
        {
            auto strip = [](std::string& s, const char* bad) {
                s.erase(std::remove_if(s.begin(), s.end(),
                    [&](char c) { return std::strchr(bad, c) != nullptr; }),
                    s.end());
                };
            strip(upper, "OI");
            strip(lower, "ol");
            strip(digit, "01");
        }

        std::string charset;
        if (opt.use_upper)  charset += upper;
        if (opt.use_lower)  charset += lower;
        if (opt.use_digit)  charset += digit;
        if (opt.use_symbol) charset += symbol;

        return charset;
    }

    std::string generate_password(const GenOptions& opt)
    {
        std::string upper, lower, digit, symbol;
        std::string all = build_charset(opt, upper, lower, digit, symbol);

        // If user disables everything, nothing to generate.
        if (all.empty() || opt.length <= 0)
            return {};

        std::string out;
        out.reserve((size_t)opt.length);

        auto push_one = [&](const std::string& set) {
            if (!set.empty())
                out.push_back(set[secure_rand_index(set.size())]);
            };

        // Ensure at least one from each enabled class (so "Generate" respects toggles)
        if (opt.use_upper)  push_one(upper);
        if (opt.use_lower)  push_one(lower);
        if (opt.use_digit)  push_one(digit);
        if (opt.use_symbol) push_one(symbol);

        while ((int)out.size() < opt.length)
            out.push_back(all[secure_rand_index(all.size())]);

        // Fisher�Yates shuffle (secure index)
        for (size_t i = out.size(); i > 1; --i)
        {
            size_t j = secure_rand_index(i);
            std::swap(out[i - 1], out[j]);
        }

        // If length is shorter than enabled-class count, we overshot.
        if ((int)out.size() > opt.length)
            out.resize((size_t)opt.length);

        return out;
    }

    double estimate_entropy_bits(const GenOptions& opt)
    {
        std::string u, l, d, s;
        std::string all = build_charset(opt, u, l, d, s);
        if (all.empty() || opt.length <= 0) return 0.0;

        const double N = (double)all.size();
        return (double)opt.length * std::log2(N);
    }

    // ============================================================
    // FILE / STRING / MISC (existing)
    // ============================================================
    bool directory_exists(std::string path)
    {
        return (std::filesystem::exists(path) && std::filesystem::is_directory(path));
    }

    bool create_directories(const std::string& path)
    {
        if (path.empty()) return false;

        std::error_code ec;
        bool result = std::filesystem::create_directories(path, ec);

        // create_directories returns false if directory already exists (which is fine)
        // or if it failed to create. Check error code to distinguish.
        if (ec)
            return false;  // actual error occurred

        return true;  // success (created or already exists)
    }

    bool file_exists(std::string path)
    {
        if (std::filesystem::is_directory(path)) return false;
        return std::filesystem::exists(path);
    }

    bool file_exists2(std::string file_name)
    {
        struct stat buffer;
        return (stat(file_name.c_str(), &buffer) == 0);
    }

    std::vector<unsigned char> str_to_bytes(const std::string& str)
    {
        return std::vector<unsigned char>(str.begin(), str.end());
    }

    std::string bytes_to_str(const std::vector<unsigned char>& bytes)
    {
        return std::string(bytes.begin(), bytes.end());
    }

    bool is_digit(const std::string& s)
    {
        for (char c : s) if (!isdigit((unsigned char)c)) return false;
        return true;
    }

    WORD int_to_word(int val)
    {
        if (val >= 0 && val <= USHRT_MAX)
            return static_cast<WORD>(val);
        return (WORD)65000;
    }

    uint32_t fnv1a_32(const char* s)
    {
        uint32_t h = 2166136261u;
        for (; s && *s; ++s)
        {
            h ^= (uint8_t)(*s);
            h *= 16777619u;
        }
        return h ? h : 1u;
    }

    std::string file_to_str(const std::string& filepath)
    {
        std::ifstream ifs(filepath);
        return std::string((std::istreambuf_iterator<char>(ifs)),
            std::istreambuf_iterator<char>());
    }

    void str_to_file(const std::string& filename, const std::string& content)
    {
        // Write to temp file first, then atomic rename to prevent truncation on crash
        std::string tmp_path = filename + ".tmp";

        {
            std::fstream file(tmp_path, std::ios::out | std::ios::trunc);
            if (!file.is_open())
                return;

            file << content;
            file.flush();
            if (!file.good()) {
                file.close();
                std::filesystem::remove(tmp_path);
                return;
            }
            file.close();
        }

        try {
            if (std::filesystem::exists(filename))
                std::filesystem::remove(filename);
            std::filesystem::rename(tmp_path, filename);
        } catch (...) {
            std::filesystem::remove(tmp_path);
        }
    }

    std::string b64_encode(const std::string& input)
    {
        static const char* b64_chars = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string output;
        int val = 0, valb = -6;
        for (unsigned char c : input)
        {
            val = (val << 8) + c;
            valb += 8;
            while (valb >= 0)
            {
                output.push_back(b64_chars[(val >> valb) & 0x3F]);
                valb -= 6;
            }
        }
        if (valb > -6) output.push_back(b64_chars[((val << 8) >> (valb + 8)) & 0x3F]);
        while (output.size() % 4) output.push_back('=');
        return output;
    }

    std::vector<unsigned char> add_padding(const std::vector<unsigned char>& data)
    {
        size_t padding_size = 16 - (data.size() % 16);
        std::vector<unsigned char> padded_data(data);
        padded_data.resize(data.size() + padding_size);
        std::fill(padded_data.end() - padding_size, padded_data.end(), static_cast<unsigned char>(padding_size));
        return padded_data;
    }

    std::vector<unsigned char> remove_padding(const std::vector<unsigned char>& data)
    {
        if (data.empty())
            throw std::runtime_error("Data is empty.");

        unsigned char padding_size = data.back();
        if (padding_size > data.size())
            return std::vector<unsigned char>{{' '}};

        return std::vector<unsigned char>(data.begin(), data.end() - padding_size);
    }

    std::string tchar_to_utf8(TCHAR* tchar_arr)
    {
        std::vector<char> buffer;
        int size = WideCharToMultiByte(CP_UTF8, 0, tchar_arr, -1, NULL, 0, NULL, NULL);
        if (size > 0)
        {
            buffer.resize(size);
            WideCharToMultiByte(CP_UTF8, 0, tchar_arr, -1, &buffer[0], (int)buffer.size(), NULL, NULL);
        }
        return buffer.empty() ? std::string() : std::string(&buffer[0]);
    }

    void to_clipboard(const std::string& s)
    {
        if (!OpenClipboard(NULL)) return;
        if (!EmptyClipboard()) { CloseClipboard(); return; }

        HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, s.size() + 1);
        if (!hg) { CloseClipboard(); return; }

        void* p = GlobalLock(hg);
        if (!p) { GlobalFree(hg); CloseClipboard(); return; }

        std::memcpy(p, s.c_str(), s.size() + 1);
        GlobalUnlock(hg);

        SetClipboardData(CF_TEXT, hg);

        // Exclude from Windows clipboard history (Win10 1809+)
        // Format "ExcludeClipboardContentFromMonitorProcessing" signals clipboard
        // managers and Windows Cloud Clipboard to skip this content.
        static UINT s_excludeFormat = RegisterClipboardFormatW(L"ExcludeClipboardContentFromMonitorProcessing");
        if (s_excludeFormat != 0) {
            HGLOBAL hgExclude = GlobalAlloc(GMEM_MOVEABLE, 1);
            if (hgExclude) {
                void* pe = GlobalLock(hgExclude);
                if (pe) {
                    *(char*)pe = 0;
                    GlobalUnlock(hgExclude);
                    SetClipboardData(s_excludeFormat, hgExclude);
                } else {
                    GlobalFree(hgExclude);
                }
            }
        }

        CloseClipboard();
    }

    std::vector<unsigned char> file_to_vec(const std::string& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
            return {};

        file.seekg(0, std::ios::end);
        std::streamsize size = file.tellg();
        file.seekg(0, std::ios::beg);

        std::vector<unsigned char> data((size_t)size);
        file.read(reinterpret_cast<char*>(data.data()), size);
        return data;
    }

    bool bytes_to_file(const char* file_path, const std::vector<unsigned char>& bytes)
    {
        // Write to temp file first, then atomic rename to prevent truncated files on crash
        std::string tmp_path = std::string(file_path) + ".tmp";

        {
            std::ofstream file(tmp_path, std::ios::binary | std::ios::trunc);
            if (!file) return false;

            file.write(reinterpret_cast<const char*>(bytes.data()), (std::streamsize)bytes.size());
            file.flush();

            if (!file.good()) {
                file.close();
                std::filesystem::remove(tmp_path);
                return false;
            }
        }

        try {
            if (std::filesystem::exists(file_path))
                std::filesystem::remove(file_path);
            std::filesystem::rename(tmp_path, file_path);
            return true;
        } catch (...) {
            std::filesystem::remove(tmp_path);
            return false;
        }
    }

    void clear_clipboard()
    {
        std::this_thread::sleep_for(std::chrono::seconds(10));
        if (OpenClipboard(NULL))
        {
            EmptyClipboard();
            CloseClipboard();
        }
    }

    std::vector<char> uint_to_char(const unsigned int* data, size_t size)
    {
        std::vector<char> char_data(size);
        for (size_t i = 0; i < size; ++i)
            char_data[i] = static_cast<char>(data[i]);
        return char_data;
    }

    void clear_string(std::string& s)
    {
        const char* const ptr = s.data();
        SecureZeroMemory((void*)ptr, s.size());
    }

    std::string GetDefaultBackupDir()
    {
        char docs[MAX_PATH];
        if (SHGetFolderPathA(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, docs) != S_OK)
            return ".\\Backups";

        std::string dir = std::string(docs) + "\\PasswordManager\\Backups";

        std::filesystem::create_directories(dir);
        return dir;
    }

    void SanitizeFilename(std::string& s)
    {
        while (!s.empty() && (s.back() == '.' || s.back() == ' '))
            s.pop_back();
        if (s.empty()) s = "file";

        for (char& c : s)
        {
            if (c == ':' || c == '/' || c == '\\' || c == '*' ||
                c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
            {
                c = '-';
            }
        }
    }

    bool CopyFileAtomic(const std::string& src, const std::string& dst)
    {
        try
        {
            if (!std::filesystem::exists(src))
                return false;

            std::filesystem::path tmp = dst + ".tmp";

            std::filesystem::copy_file(src, tmp,
                std::filesystem::copy_options::overwrite_existing);

            // ensure dst doesn't exist so rename succeeds on Windows
            if (std::filesystem::exists(dst))
                std::filesystem::remove(dst);

            std::filesystem::rename(tmp, dst);
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    void EnforceBackupRetention(const std::string& dir, size_t keep_count)
    {
        if (keep_count == 0) return;
        if (!std::filesystem::exists(dir)) return;

        try
        {
            struct Entry
            {
                std::filesystem::path path;
                std::filesystem::file_time_type time;
            };

            std::vector<Entry> files;

            for (const auto& e : std::filesystem::directory_iterator(dir))
            {
                if (!e.is_regular_file()) continue;
                if (e.path().extension() != ".lbdb") continue;

                files.push_back({ e.path(), e.last_write_time() });
            }

            if (files.size() <= keep_count)
                return;

            std::sort(files.begin(), files.end(),
                [](const Entry& a, const Entry& b)
                {
                    return a.time > b.time; // newest first
                });

            for (size_t i = keep_count; i < files.size(); ++i)
            {
                std::filesystem::remove(files[i].path);
            }
        }
        catch (...)
        {
            // Never throw; backups must not block app
        }
    }

    std::string helpers::Basename(const std::string& path)
    {
        size_t slash = path.find_last_of("\\/");
        return (slash == std::string::npos) ? path : path.substr(slash + 1);
    }

    std::vector<std::string> helpers::ListBackupsForVault(const std::string& vault_path)
    {
        std::vector<std::string> out;
        if (vault_path.empty()) return out;

        const std::string dir = helpers::GetDefaultBackupDir();

        // match backups that start with vault basename (no extension)
        std::string base = helpers::Basename(vault_path);
        size_t dot = base.find_last_of('.');
        if (dot != std::string::npos) base = base.substr(0, dot);

        try
        {
            struct Entry { std::string path; std::filesystem::file_time_type t; };
            std::vector<Entry> entries;

            for (auto& e : std::filesystem::directory_iterator(dir))
            {
                if (!e.is_regular_file()) continue;
                if (e.path().extension() != ".lbdb") continue;

                // file name begins with "<VaultName>_"
                const std::string fn = e.path().filename().string();
                if (fn.rfind(base + "_", 0) != 0) continue;

                entries.push_back({ e.path().string(), e.last_write_time() });
            }

            std::sort(entries.begin(), entries.end(),
                [](const Entry& a, const Entry& b) { return a.t > b.t; });

            out.reserve(entries.size());
            for (auto& it : entries) out.push_back(it.path);
        }
        catch (...) {}

        return out;
    }

    static std::string _format_local_time_from_filetime(const FILETIME& ftUtc)
    {
        FILETIME ftLocal{};
        SYSTEMTIME stUtc{}, stLocal{};
        FileTimeToSystemTime(&ftUtc, &stUtc);
        SystemTimeToTzSpecificLocalTime(nullptr, &stUtc, &stLocal);

        char buf[64];
        std::snprintf(buf, sizeof(buf),
            "%04d-%02d-%02d %02d:%02d",
            stLocal.wYear, stLocal.wMonth, stLocal.wDay,
            stLocal.wHour, stLocal.wMinute);

        return std::string(buf);
    }

    bool GetFileInfo(const std::string& path, uint64_t& outSizeBytes, std::string& outLocalTimeStr)
    {
        outSizeBytes = 0;
        outLocalTimeStr.clear();
        if (path.empty()) return false;

        WIN32_FILE_ATTRIBUTE_DATA fad{};
        if (!GetFileAttributesExA(path.c_str(), GetFileExInfoStandard, &fad))
            return false;

        ULARGE_INTEGER sz{};
        sz.LowPart = fad.nFileSizeLow;
        sz.HighPart = fad.nFileSizeHigh;
        outSizeBytes = (uint64_t)sz.QuadPart;

        outLocalTimeStr = _format_local_time_from_filetime(fad.ftLastWriteTime);
        return true;
    }

    std::string FormatBytes(uint64_t bytes)
    {
        const double b = (double)bytes;
        char buf[64];

        if (bytes < 1024ull)
            std::snprintf(buf, sizeof(buf), "%.0f B", b);
        else if (bytes < 1024ull * 1024ull)
            std::snprintf(buf, sizeof(buf), "%.1f KB", b / 1024.0);
        else if (bytes < 1024ull * 1024ull * 1024ull)
            std::snprintf(buf, sizeof(buf), "%.1f MB", b / (1024.0 * 1024.0));
        else
            std::snprintf(buf, sizeof(buf), "%.1f GB", b / (1024.0 * 1024.0 * 1024.0));

        return std::string(buf);
    }


    static std::string _timestamp_compact_local()
    {
        std::string iso = now_iso8601_local();     // "YYYY-MM-DDTHH:MM:SS-08:00"
        if (iso.size() >= 19) iso = iso.substr(0, 19); // keep seconds
        for (char& c : iso)
        {
            if (c == 'T') c = '_';
            else if (c == ':') c = '-';
        }
        return iso; // "YYYY-MM-DD_HH-MM-SS"
    }

    // Verify a SQLite database file passes PRAGMA quick_check
    static bool verify_sqlite_integrity(const std::string& path)
    {
        sqlite3* db = nullptr;
        int rc = sqlite3_open_v2(path.c_str(), &db, SQLITE_OPEN_READONLY, nullptr);
        if (rc != SQLITE_OK || !db) {
            if (db) sqlite3_close(db);
            return false;
        }

        bool ok = false;
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, "PRAGMA quick_check", -1, &stmt, nullptr) == SQLITE_OK) {
            if (sqlite3_step(stmt) == SQLITE_ROW) {
                const char* result = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                ok = (result && std::strcmp(result, "ok") == 0);
            }
            sqlite3_finalize(stmt);
        }

        sqlite3_close(db);
        return ok;
    }

    BackupCreateResult CreateBackupNow(const std::string& vault_path, size_t keep_count)
    {
        BackupCreateResult r;

        if (vault_path.empty()) { r.err = "No vault file selected."; return r; }
        if (!std::filesystem::exists(vault_path)) { r.err = "Vault file does not exist."; return r; }

        const std::string dir = GetDefaultBackupDir();

        // prefix must match ListBackupsForVault(vault_path)
        std::string base = Basename(vault_path);
        if (auto dot = base.find_last_of('.'); dot != std::string::npos)
            base = base.substr(0, dot);
        SanitizeFilename(base);

        const std::string name = base + "_" + _timestamp_compact_local() + ".lbdb";
        const std::string dst = (std::filesystem::path(dir) / name).string();

        if (!CopyFileAtomic(vault_path, dst))
        {
            r.err = "Failed to create backup (copy error).";
            return r;
        }

        // Verify backup integrity
        if (!verify_sqlite_integrity(dst))
        {
            std::filesystem::remove(dst);
            r.err = "Backup verification failed (corrupt copy). Backup deleted.";
            return r;
        }

        EnforceBackupRetention(dir, keep_count);

        r.ok = true;
        r.outPath = dst;
        return r;
    }

}
