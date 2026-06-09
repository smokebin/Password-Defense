// app_display_helpers.cpp — display/search helpers split out of application.cpp

#include "app_internal.h"
#include "UI.h"
#include "credentials/credential.h"
#include <sodium.h>
#include <algorithm>
#include <ctime>
#include <set>
#include <map>
#include <unordered_map>

uint64_t iso_to_sortkey_yyyymmddhhmmss(const std::string& iso)
{
    if (iso.size() < 19) return 0;

    auto digit = [](char c) { return (c >= '0' && c <= '9') ? (c - '0') : -1; };
    auto rd2 = [&](int i)->int { int a = digit(iso[i]); int b = digit(iso[i + 1]); return (a < 0 || b < 0) ? -1 : a * 10 + b; };
    auto rd4 = [&](int i)->int { int a = digit(iso[i]); int b = digit(iso[i + 1]); int c = digit(iso[i + 2]); int d = digit(iso[i + 3]); return (a < 0 || b < 0 || c < 0 || d < 0) ? -1 : a * 1000 + b * 100 + c * 10 + d; };

    int Y = rd4(0), M = rd2(5), D = rd2(8), h = rd2(11), m = rd2(14), s = rd2(17);
    if (Y < 0 || M < 1 || M > 12 || D < 1 || D > 31 || h < 0 || h > 23 || m < 0 || m > 59 || s < 0 || s > 59)
        return 0;

    return (uint64_t)Y * 10000000000ull +
        (uint64_t)M * 100000000ull +
        (uint64_t)D * 1000000ull +
        (uint64_t)h * 10000ull +
        (uint64_t)m * 100ull +
        (uint64_t)s;
}

uint32_t iso_to_monthkey_yyyymm(const std::string& iso)
{
    if (iso.size() < 7) return 0;
    int y = std::atoi(iso.substr(0, 4).c_str());
    int m = std::atoi(iso.substr(5, 2).c_str());
    if (y <= 0 || m < 1 || m > 12) return 0;
    return (uint32_t)(y * 100 + m);
}

uint32_t unix_ms_to_monthkey(int64_t ms)
{
    if (ms == 0) return 0;
    std::time_t sec = static_cast<std::time_t>(ms / 1000);
    std::tm tm_local;
#ifdef _WIN32
    localtime_s(&tm_local, &sec);
#else
    localtime_r(&sec, &tm_local);
#endif
    int y = tm_local.tm_year + 1900;
    int m = tm_local.tm_mon + 1;
    if (y <= 0 || m < 1 || m > 12) return 0;
    return static_cast<uint32_t>(y * 100 + m);
}

const char* month_name(int m)
{
    static const char* k[] = {
        "January","February","March","April","May","June",
        "July","August","September","October","November","December"
    };
    if (m < 1 || m > 12) return "";
    return k[m - 1];
}

std::string month_label_from_key(uint32_t yyyymm)
{
    int y = (int)(yyyymm / 100);
    int m = (int)(yyyymm % 100);
    if (y <= 0 || m < 1 || m > 12) return "Unknown date";
    return std::string(month_name(m)) + " " + std::to_string(y);
}

bool str_contains_ci(const std::string& hay, const char* needle)
{
    if (!needle || !needle[0]) return true;
    if (hay.empty()) return false;

    return ImStristr(hay.c_str(), nullptr, needle, nullptr) != nullptr; // ImStristr from imgui_internal.h via UI.h
}

bool matches_search(const Credential& c, const char* needle, int search_filter)
{
    if (!needle || !needle[0]) return true;
    switch (search_filter)
    {
    case 0: // Title: also match card_brand (cards), id_type (identity)
        return str_contains_ci(c.title, needle)
            || str_contains_ci(c.card_brand, needle)
            || str_contains_ci(c.id_type, needle);
    case 1: return str_contains_ci(c.email, needle);
    case 2: // Username: also match cardholder_name (cards), full_name (identity)
        return str_contains_ci(c.user, needle)
            || str_contains_ci(c.cardholder_name, needle)
            || str_contains_ci(c.full_name, needle);
    default: return str_contains_ci(c.title, needle);
    }
}

uint32_t compute_changed_fields(const Credential& cur, const Credential* saved)
{
    if (!saved)
        return ui::FCF_IsNew;

    uint32_t f = 0;
    if (cur.title       != saved->title)       f |= ui::FCF_Title;
    if (cur.user        != saved->user)        f |= ui::FCF_User;
    if (cur.email       != saved->email)       f |= ui::FCF_Email;
    if (cur.password    != saved->password)    f |= ui::FCF_Password;
    if (cur.website     != saved->website)     f |= ui::FCF_Website;
    if (cur.group       != saved->group)       f |= ui::FCF_Group;
    if (cur.notes       != saved->notes)       f |= ui::FCF_Notes;
    if (cur.totp_secret != saved->totp_secret) f |= ui::FCF_TotpSecret;
    if (cur.card_number != saved->card_number) f |= ui::FCF_CardNumber;
    if (cur.card_expiry != saved->card_expiry) f |= ui::FCF_CardExpiry;
    if (cur.card_cvv    != saved->card_cvv)    f |= ui::FCF_CardCvv;
    if (cur.card_brand  != saved->card_brand)  f |= ui::FCF_CardBrand;
    if (cur.cardholder_name != saved->cardholder_name) f |= ui::FCF_Cardholder;
    if (cur.card_address != saved->card_address) f |= ui::FCF_CardAddress;
    if (cur.card_city != saved->card_city) f |= ui::FCF_CardCity;
    if (cur.card_postal_code != saved->card_postal_code) f |= ui::FCF_CardPostalCode;
    if (cur.full_name   != saved->full_name)   f |= ui::FCF_FullName;
    if (cur.id_type     != saved->id_type)     f |= ui::FCF_IdType;
    if (cur.id_number   != saved->id_number)   f |= ui::FCF_IdNumber;
    if (cur.date_of_birth != saved->date_of_birth) f |= ui::FCF_DateOfBirth;
    if (cur.expiry_date != saved->expiry_date) f |= ui::FCF_ExpiryDate;
    if (cur.country     != saved->country)     f |= ui::FCF_Country;
    if (cur.address     != saved->address)     f |= ui::FCF_Address;
    if (cur.phone       != saved->phone)       f |= ui::FCF_Phone;
    if (cur.is_favorite != saved->is_favorite) f |= ui::FCF_IsFavorite;
    if (cur.is_pinned   != saved->is_pinned)   f |= ui::FCF_IsPinned;
    if (cur.expires_at_ms != saved->expires_at_ms) f |= ui::FCF_ExpiresAt;
    return f;
}

std::vector<ui::AccordionItem> build_accordion_items(
    const std::vector<Credential>& creds,
    const std::unordered_map<std::string, Credential>& saved_snapshot,
    const char* search,
    int search_filter,
    int sort_mode,
    const std::string& pill_tab_0,
    const std::string& pill_tab_1,
    const std::set<std::string>& groups,
    const std::set<std::string>& tag_filters,
    ui::OrderKey order_key,
    ui::OrderDir order_dir,
    ui::GroupMode group_mode)
{
    std::vector<const Credential*> list;
    list.reserve(creds.size());

    // @-prefixed entries in `groups` are type filters, not group name filters
    std::set<CredType> type_filters;
    std::set<std::string> group_filters;
    for (const auto& g : groups) {
        if (g == "@Passwords")      type_filters.insert(CredType::Password);
        else if (g == "@Cards")    type_filters.insert(CredType::CreditCard);
        else if (g == "@Identity") type_filters.insert(CredType::Identity);
        else if (g == "@Notes")    type_filters.insert(CredType::SecureNote);
        else if (g != "---")           group_filters.insert(g);
    }

    std::string active_pill;
    if (sort_mode == 0) active_pill = pill_tab_0;
    else if (sort_mode == 1) active_pill = pill_tab_1;

    for (const auto& c : creds)
    {
        if (!type_filters.empty() && type_filters.find(c.type) == type_filters.end()) continue;
        if (!group_filters.empty() && group_filters.find(c.group) == group_filters.end()) continue;

        // tag filter is OR: cred needs at least one matching tag
        if (!tag_filters.empty()) {
            bool has_any = false;
            for (const auto& t : c.tags)
                if (tag_filters.count(t)) { has_any = true; break; }
            if (!has_any) continue;
        }

        if (!active_pill.empty())
        {
            if (active_pill == "Pinned") { if (!c.is_pinned) continue; }
            else if (active_pill == "Favorites") { if (!c.is_favorite) continue; }
            else { if (c.group != active_pill) continue; }
        }

        if (!matches_search(c, search, search_filter)) continue;

        list.push_back(&c);
    }

    auto created_key = [](const Credential* c) -> uint64_t {
        return c ? static_cast<uint64_t>(c->created_at_ms) : 0;
        };
    auto updated_key = [&](const Credential* c) -> uint64_t {
        if (!c) return 0;
        if (c->updated_at_ms != 0)
            return static_cast<uint64_t>(c->updated_at_ms);
        return created_key(c);
        };

    auto compare_items = [&](const Credential* a, const Credential* b) -> bool
        {
            if (order_key == ui::OrderKey::Title)
            {
                if (sort_mode == 2)
                {
                    if (a->is_pinned != b->is_pinned)      return a->is_pinned > b->is_pinned;
                    if (a->is_favorite != b->is_favorite) return a->is_favorite > b->is_favorite;
                }

                if (a->title != b->title)
                {
                    if (order_dir == ui::OrderDir::Asc)  return a->title < b->title;
                    else                                 return a->title > b->title;
                }

                const uint64_t ka = created_key(a);
                const uint64_t kb = created_key(b);
                if (ka != kb) return ka > kb;
                return a->id < b->id;
            }

            const uint64_t ka = (order_key == ui::OrderKey::Created) ? created_key(a) : updated_key(a);
            const uint64_t kb = (order_key == ui::OrderKey::Created) ? created_key(b) : updated_key(b);

            if (ka != kb)
            {
                if (order_dir == ui::OrderDir::Asc)  return ka < kb;
                else                                 return ka > kb;
            }

            if (a->title != b->title) return a->title < b->title;
            return a->id < b->id;
        };

    auto sort_bucket = [&](std::vector<const Credential*>& bucket)
        {
            std::sort(bucket.begin(), bucket.end(), compare_items);
        };

    std::vector<ui::AccordionItem> out;
    out.reserve(list.size() + 32);

    const bool showCount = ui::GetShowGroupCount();
    auto push_header = [&](const std::string& label, int count = 0)
        {
            ui::AccordionItem hdr{};
            hdr.is_header = true;
            hdr.id = 0;
            if (showCount && count > 0)
                hdr.header_label = label + "  (" + std::to_string(count) + ")";
            else
                hdr.header_label = label;
            out.push_back(std::move(hdr));
        };

    auto push_item = [&](const Credential* c)
        {
            if (!c) return;
            ui::AccordionItem it{};
            it.id = c->id;
            it.uuid = c->uuid;
            it.title = c->title;
            it.user = c->user;
            it.email = c->email;
            it.website = c->website;
            it.group = c->group;
            it.notes = c->notes;
            it.tags = c->tags;
            it.totp_secret = c->totp_secret;
            it.type = c->type;
            it.card_number = c->card_number;
            it.card_expiry = c->card_expiry;
            it.card_cvv = c->card_cvv;
            it.card_brand = c->card_brand;
            it.cardholder_name = c->cardholder_name;
            it.card_address = c->card_address;
            it.card_city = c->card_city;
            it.card_postal_code = c->card_postal_code;
            it.full_name = c->full_name;
            it.id_type = c->id_type;
            it.id_number = c->id_number;
            it.date_of_birth = c->date_of_birth;
            it.expiry_date = c->expiry_date;
            it.country = c->country;
            it.address = c->address;
            it.phone = c->phone;
            it.is_pinned = c->is_pinned;
            it.is_favorite = c->is_favorite;
            it.expires_at_ms = c->expires_at_ms;
            it.expiry_action = c->expiry_action;
            it.created_at_ms = c->created_at_ms;
            it.updated_at_ms = c->updated_at_ms;
            it.password_history.reserve(c->password_history.size());
            for (const auto& h : c->password_history) {
                it.password_history.push_back({h.password, h.changed_at_ms});
            }

            const Credential* saved_ptr = nullptr;
            if (!c->uuid.empty()) {
                auto sit = saved_snapshot.find(c->uuid);
                if (sit != saved_snapshot.end())
                    saved_ptr = &sit->second;
            }
            it.changed_fields = compute_changed_fields(*c, saved_ptr);
            it.is_changed = (it.changed_fields != 0);

            out.push_back(std::move(it));
        };

    if (group_mode == ui::GroupMode::PinnedFavorites)
    {
        std::vector<const Credential*> pinned;
        std::vector<const Credential*> favorites;
        std::vector<const Credential*> rest;
        pinned.reserve(list.size());
        favorites.reserve(list.size());
        rest.reserve(list.size());

        for (const Credential* c : list)
        {
            if (!c) continue;
            if (c->is_pinned) pinned.push_back(c);
            else if (c->is_favorite) favorites.push_back(c);
            else rest.push_back(c);
        }

        if (!pinned.empty())
        {
            sort_bucket(pinned);
            push_header("Pinned", (int)pinned.size());
            for (const Credential* c : pinned) push_item(c);
        }

        if (!favorites.empty())
        {
            sort_bucket(favorites);
            push_header("Favorites", (int)favorites.size());
            for (const Credential* c : favorites) push_item(c);
        }

        if (!rest.empty())
        {
            sort_bucket(rest);
            push_header("All", (int)rest.size());
            for (const Credential* c : rest) push_item(c);
        }
    }
    else if (group_mode == ui::GroupMode::Group)
    {
        std::vector<const Credential*> pinned;
        std::vector<const Credential*> favorites;
        std::unordered_map<std::string, std::vector<const Credential*>> buckets;
        buckets.reserve(list.size());

        for (const Credential* c : list)
        {
            if (!c) continue;
            if (c->is_pinned)        pinned.push_back(c);
            else if (c->is_favorite) favorites.push_back(c);
            else {
                std::string key = c->group.empty() ? "Ungrouped" : c->group;
                buckets[key].push_back(c);
            }
        }

        if (!pinned.empty())
        {
            sort_bucket(pinned);
            push_header("Pinned", (int)pinned.size());
            for (const Credential* c : pinned) push_item(c);
        }

        if (!favorites.empty())
        {
            sort_bucket(favorites);
            push_header("Favorites", (int)favorites.size());
            for (const Credential* c : favorites) push_item(c);
        }

        std::vector<std::string> keys;
        keys.reserve(buckets.size());
        for (const auto& kv : buckets) keys.push_back(kv.first);

        std::sort(keys.begin(), keys.end(), [](const std::string& a, const std::string& b)
            {
                if (a == "Ungrouped") return false;
                if (b == "Ungrouped") return true;
                return a < b;
            });

        for (const auto& key : keys)
        {
            auto& bucket = buckets[key];
            sort_bucket(bucket);
            push_header(key, (int)bucket.size());
            for (const Credential* c : bucket) push_item(c);
        }
    }
    else if (group_mode == ui::GroupMode::Alphabetical)
    {
        std::vector<const Credential*> pinned;
        std::vector<const Credential*> favorites;
        std::map<char, std::vector<const Credential*>> buckets;
        std::vector<const Credential*> other;

        for (const Credential* c : list)
        {
            if (!c) continue;
            if (c->is_pinned)        pinned.push_back(c);
            else if (c->is_favorite) favorites.push_back(c);
            else if (!c->title.empty() && std::isalpha((unsigned char)c->title[0]))
                buckets[(char)std::toupper((unsigned char)c->title[0])].push_back(c);
            else
                other.push_back(c);
        }

        if (!pinned.empty())
        {
            sort_bucket(pinned);
            push_header("Pinned", (int)pinned.size());
            for (const Credential* c : pinned) push_item(c);
        }

        if (!favorites.empty())
        {
            sort_bucket(favorites);
            push_header("Favorites", (int)favorites.size());
            for (const Credential* c : favorites) push_item(c);
        }

        for (auto& kv : buckets)
        {
            sort_bucket(kv.second);
            char hdr[2] = { kv.first, '\0' };
            push_header(hdr, (int)kv.second.size());
            for (const Credential* c : kv.second) push_item(c);
        }

        if (!other.empty())
        {
            sort_bucket(other);
            push_header("#", (int)other.size());
            for (const Credential* c : other) push_item(c);
        }
    }
    else if (group_mode == ui::GroupMode::MonthCreated || group_mode == ui::GroupMode::MonthUpdated)
    {
        std::vector<const Credential*> pinned;
        std::vector<const Credential*> favorites;
        std::unordered_map<uint32_t, std::vector<const Credential*>> buckets;
        buckets.reserve(list.size());

        for (const Credential* c : list)
        {
            if (!c) continue;
            if (c->is_pinned)        pinned.push_back(c);
            else if (c->is_favorite) favorites.push_back(c);
            else {
                int64_t src_ms = (group_mode == ui::GroupMode::MonthCreated) ? c->created_at_ms : c->updated_at_ms;
                uint32_t mk = unix_ms_to_monthkey(src_ms);
                buckets[mk].push_back(c);
            }
        }

        if (!pinned.empty())
        {
            sort_bucket(pinned);
            push_header("Pinned", (int)pinned.size());
            for (const Credential* c : pinned) push_item(c);
        }

        if (!favorites.empty())
        {
            sort_bucket(favorites);
            push_header("Favorites", (int)favorites.size());
            for (const Credential* c : favorites) push_item(c);
        }

        std::vector<uint32_t> keys;
        keys.reserve(buckets.size());
        for (const auto& kv : buckets) keys.push_back(kv.first);

        const bool desc = (order_dir == ui::OrderDir::Desc);
        std::sort(keys.begin(), keys.end(), [&](uint32_t a, uint32_t b)
            {
                if (a == 0) return false;
                if (b == 0) return true;
                return desc ? (a > b) : (a < b);
            });

        for (uint32_t key : keys)
        {
            auto& bucket = buckets[key];
            sort_bucket(bucket);
            push_header((key == 0) ? "Unknown date" : month_label_from_key(key), (int)bucket.size());
            for (const Credential* c : bucket) push_item(c);
        }
    }
    else
    {
        sort_bucket(list);
        for (const Credential* c : list) push_item(c);
    }

    return out;
}
