

//save.cpp
#include "save.h"

#include <fstream>
#include <cstdlib>
#include <Windows.h>
#include <string>
#include <vector>
#include <filesystem>

#include "../third_party/json.hpp"

#include "utility.h"

namespace cfg {
	std::string _path = "";
	std::string cfg_path = "";
	std::string db_name = "";
	std::string exe_path = "";
	std::string exe_full_path = "";

	void init()
	{
		char exe_path_c[MAX_PATH];
		GetModuleFileNameA(NULL, exe_path_c, MAX_PATH);
		std::filesystem::path exe_path_full = std::string(exe_path_c);
		std::filesystem::path exe_parent_path = exe_path_full.parent_path();
		exe_path = exe_parent_path.string();
		exe_full_path = exe_path_full.string();
		cfg_path = exe_parent_path.string() + "\\config.json";

		if (!helpers::file_exists(cfg_path)) //no cfg file
		{
			nlohmann::json j;

			if (!helpers::file_exists(_path)) //no db file
			{
				j = {
				   {"db_path", "NONE"},
				   {"wnd_border", "1.000000,1.000000,1.000000"}
				};
				_path = "NONE";
			}
			else {
				j = {
					{"db_path", _path},
					{"wnd_border", "1.000000,1.000000,1.000000"}
				};
			}

			_path = j["db_path"];
			helpers::str_to_file(cfg_path, j.dump(5));
		}
		else { //cfg file exists, read default db path from file
			nlohmann::json j;
			try {
				j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			}
			catch (nlohmann::json::parse_error& e) {
				MessageBox(0, L"nlohmann::json::parse error", L"save.cpp", 0);

				Sleep(3000);
				return;
			}
			_path = j["db_path"];
			if (!helpers::file_exists(_path)) //if the default path doesnt point to a db
			{
				j["db_path"] = "NONE";
				_path = "NONE";
			}

			helpers::str_to_file(cfg_path, j.dump(5));
		}
	}

	void update_db_path()
	{
		nlohmann::json j;
		try {
			j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
		}
		catch (...) {
			// Config file is corrupted — recreate from defaults
			j = nlohmann::json::object();
			j["db_path"] = _path.empty() ? "NONE" : _path;
		}

		if (!helpers::file_exists(_path))
		{
			j["db_path"] = "NONE";
			_path = "NONE";
		}
		else {
			j["db_path"] = _path;
		}

		helpers::str_to_file(cfg_path, j.dump(5));
	}

	static std::string norm_vault_key(const std::string& p)
	{
		std::string s = p;
		for (char& c : s) {
			if (c == '/') c = '\\';
			if (c >= 'A' && c <= 'Z') c = char(c + 32);
		}
		return s;
	}

	std::vector<std::string> get_recent_vaults()
	{
		std::vector<std::string> out;
		if (cfg_path.empty()) return out;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			if (j.contains("recent_vaults") && j["recent_vaults"].is_array())
				for (const auto& e : j["recent_vaults"])
					if (e.is_string()) out.push_back(e.get<std::string>());
		}
		catch (...) {}
		return out;
	}

	void add_recent_vault(const std::string& path)
	{
		if (cfg_path.empty() || path.empty() || path == "NONE") return;

		const std::string key = norm_vault_key(path);
		std::vector<std::string> list;
		list.push_back(path);
		for (const auto& e : get_recent_vaults())
			if (norm_vault_key(e) != key) list.push_back(e);
		if (list.size() > 10) list.resize(10);

		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["recent_vaults"] = list;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	void remove_recent_vault(const std::string& path)
	{
		if (cfg_path.empty()) return;

		const std::string key = norm_vault_key(path);
		std::vector<std::string> kept;
		for (const auto& e : get_recent_vaults())
			if (norm_vault_key(e) != key) kept.push_back(e);

		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["recent_vaults"] = kept;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	int get_theme()
	{
		if (cfg_path.empty()) return 0; // default dark

		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("theme", 0);
		}
		catch (...) {
			return 0;
		}
	}

	void set_theme(int theme)
	{
		if (cfg_path.empty()) return;

		nlohmann::json j;
		try {
			j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
		}
		catch (...) {
			j = nlohmann::json::object();
		}

		j["theme"] = theme;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool is_dark_theme()
	{
		return get_theme() == 0;
	}

	int get_clipboard_clear_delay()
	{
		if (cfg_path.empty()) return 20; // default 20 seconds

		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			// migrate old boolean field to new delay field
			if (j.contains("clipboard_clear_enabled") && !j.contains("clipboard_clear_delay")) {
				bool old_enabled = j.value("clipboard_clear_enabled", true);
				return old_enabled ? 20 : 0;
			}
			return j.value("clipboard_clear_delay", 20);
		}
		catch (...) {
			return 20;
		}
	}

	void set_clipboard_clear_delay(int seconds)
	{
		if (cfg_path.empty()) return;

		nlohmann::json j;
		try {
			j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
		}
		catch (...) {
			j = nlohmann::json::object();
		}

		j["clipboard_clear_delay"] = seconds;
		j.erase("clipboard_clear_enabled"); // remove old key
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	int get_auto_lock_timeout()
	{
		if (cfg_path.empty()) return 0; // default Never

		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("auto_lock_timeout", 0);
		}
		catch (...) {
			return 0;
		}
	}

	void set_auto_lock_timeout(int seconds)
	{
		if (cfg_path.empty()) return;

		nlohmann::json j;
		try {
			j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
		}
		catch (...) {
			j = nlohmann::json::object();
		}

		j["auto_lock_timeout"] = seconds;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_reprompt_reveal_password()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("reprompt_reveal_password", false);
		}
		catch (...) { return false; }
	}

	void set_reprompt_reveal_password(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["reprompt_reveal_password"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_reprompt_export()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("reprompt_export", false);
		}
		catch (...) { return false; }
	}

	void set_reprompt_export(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["reprompt_export"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_reprompt_disable_readonly()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("reprompt_disable_readonly", false);
		}
		catch (...) { return false; }
	}

	void set_reprompt_disable_readonly(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["reprompt_disable_readonly"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_reprompt_reveal_notes()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("reprompt_reveal_notes", false);
		}
		catch (...) { return false; }
	}

	void set_reprompt_reveal_notes(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["reprompt_reveal_notes"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	int get_reprompt_lockout_count()
	{
		if (cfg_path.empty()) return 3;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("reprompt_lockout_count", 3);
		}
		catch (...) { return 3; }
	}

	void set_reprompt_lockout_count(int count)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["reprompt_lockout_count"] = count;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_hover_expand()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("hover_expand", false);
		}
		catch (...) { return false; }
	}

	void set_hover_expand(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["hover_expand"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_show_group_count()
	{
		if (cfg_path.empty()) return true;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("show_group_count", true);
		}
		catch (...) { return true; }
	}

	void set_show_group_count(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["show_group_count"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	int get_row_gap()
	{
		if (cfg_path.empty()) return 6;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("row_gap", 6);
		}
		catch (...) { return 6; }
	}

	void set_row_gap(int gap)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["row_gap"] = gap;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	int get_view_mode()
	{
		if (cfg_path.empty()) return 0;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("view_mode", 3);
		}
		catch (...) { return 0; }
	}

	void set_view_mode(int mode)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["view_mode"] = mode;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	int get_order_key()
	{
		if (cfg_path.empty()) return 0;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("order_key", 0);
		}
		catch (...) { return 0; }
	}

	void set_order_key(int key)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["order_key"] = key;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	int get_order_dir()
	{
		if (cfg_path.empty()) return 0;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("order_dir", 0);
		}
		catch (...) { return 0; }
	}

	void set_order_dir(int dir)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["order_dir"] = dir;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	int get_group_mode()
	{
		if (cfg_path.empty()) return 0;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("group_mode", 0);
		}
		catch (...) { return 0; }
	}

	void set_group_mode(int mode)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["group_mode"] = mode;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	std::string get_pill_tab_0()
	{
		if (cfg_path.empty()) return "Pinned";
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("pill_tab_0", std::string("Pinned"));
		}
		catch (...) { return "Pinned"; }
	}

	void set_pill_tab_0(const std::string& label)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["pill_tab_0"] = label;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	std::string get_pill_tab_1()
	{
		if (cfg_path.empty()) return "Favorites";
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("pill_tab_1", std::string("Favorites"));
		}
		catch (...) { return "Favorites"; }
	}

	void set_pill_tab_1(const std::string& label)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["pill_tab_1"] = label;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_online_favicons()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("online_favicons", false);
		}
		catch (...) { return false; }
	}

	void set_online_favicons(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["online_favicons"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_online_breach_check()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("online_breach_check", false);
		}
		catch (...) { return false; }
	}

	void set_online_breach_check(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["online_breach_check"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_autosave_enabled()
	{
		if (cfg_path.empty()) return true;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("autosave_enabled", true);
		}
		catch (...) { return true; }
	}

	void set_autosave_enabled(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["autosave_enabled"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_autoscroll_enabled()
	{
		if (cfg_path.empty()) return true;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("autoscroll_enabled", true);
		}
		catch (...) { return true; }
	}

	void set_autoscroll_enabled(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["autoscroll_enabled"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_always_on_top()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("always_on_top", false);
		}
		catch (...) { return false; }
	}

	void set_always_on_top(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["always_on_top"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_minimize_to_tray()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("minimize_to_tray", false);
		}
		catch (...) { return false; }
	}

	void set_minimize_to_tray(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["minimize_to_tray"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_start_on_boot()
	{
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
			0, KEY_READ, &hKey) != ERROR_SUCCESS)
			return false;

		DWORD type = 0, size = 0;
		LSTATUS st = RegQueryValueExW(hKey, L"PasswordDefense", nullptr, &type, nullptr, &size);
		RegCloseKey(hKey);
		return (st == ERROR_SUCCESS && type == REG_SZ && size > 0);
	}

	void set_start_on_boot(bool enabled)
	{
		HKEY hKey = nullptr;
		if (RegOpenKeyExW(HKEY_CURRENT_USER,
			L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
			0, KEY_SET_VALUE, &hKey) != ERROR_SUCCESS)
			return;

		if (enabled)
		{
			wchar_t exePath[MAX_PATH];
			GetModuleFileNameW(NULL, exePath, MAX_PATH);
			DWORD len = (DWORD)((wcslen(exePath) + 1) * sizeof(wchar_t));
			RegSetValueExW(hKey, L"PasswordDefense", 0, REG_SZ, (const BYTE*)exePath, len);
		}
		else
		{
			RegDeleteValueW(hKey, L"PasswordDefense");
		}
		RegCloseKey(hKey);
	}

	bool get_start_minimized()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("start_minimized", false);
		}
		catch (...) { return false; }
	}

	void set_start_minimized(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["start_minimized"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_auto_open_vault()
	{
		if (cfg_path.empty()) return true;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("auto_open_vault", true);
		}
		catch (...) { return true; }
	}

	void set_auto_open_vault(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["auto_open_vault"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	int get_trash_retention_days()
	{
		if (cfg_path.empty()) return 30;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("trash_retention_days", 30);
		}
		catch (...) { return 30; }
	}

	void set_trash_retention_days(int days)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["trash_retention_days"] = days;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_auto_backup()
	{
		if (cfg_path.empty()) return true;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("auto_backup", true);
		}
		catch (...) { return true; }
	}

	void set_auto_backup(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["auto_backup"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	int get_backup_keep_count()
	{
		if (cfg_path.empty()) return 25;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("backup_keep_count", 25);
		}
		catch (...) { return 25; }
	}

	void set_backup_keep_count(int count)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["backup_keep_count"] = count;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	DetailedHeaderColumns get_detailed_header_columns()
	{
		DetailedHeaderColumns cols;
		if (cfg_path.empty()) return cols;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			if (j.contains("detailed_header_columns") && j["detailed_header_columns"].is_object()) {
				auto& o = j["detailed_header_columns"];
				cols.title    = o.value("title",    true);
				cols.username = o.value("username", true);
				cols.email    = o.value("email",    true);
				cols.date     = o.value("date",     true);
				cols.index    = o.value("index",    true);
				cols.pin_fav  = o.value("pin_fav",  true);
				cols.sub_password = o.value("sub_password", true);
				cols.sub_card     = o.value("sub_card",     true);
				cols.sub_identity = o.value("sub_identity", true);
				cols.sub_note     = o.value("sub_note",     false);
			}
		}
		catch (...) {}
		return cols;
	}

	bool get_three_pane_sidebar_collapsed()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("three_pane_sidebar_collapsed", false);
		}
		catch (...) { return false; }
	}

	void set_three_pane_sidebar_collapsed(bool collapsed)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["three_pane_sidebar_collapsed"] = collapsed;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_three_pane_list_collapsed()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("three_pane_list_collapsed", false);
		}
		catch (...) { return false; }
	}

	void set_three_pane_list_collapsed(bool collapsed)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["three_pane_list_collapsed"] = collapsed;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_three_pane_sidebar_auto_collapse()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("three_pane_sidebar_auto_collapse", false);
		}
		catch (...) { return false; }
	}

	void set_three_pane_sidebar_auto_collapse(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["three_pane_sidebar_auto_collapse"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_three_pane_list_auto_collapse()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("three_pane_list_auto_collapse", false);
		}
		catch (...) { return false; }
	}

	void set_three_pane_list_auto_collapse(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["three_pane_list_auto_collapse"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	void set_detailed_header_columns(const DetailedHeaderColumns& cols)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["detailed_header_columns"] = {
			{"title",    cols.title},
			{"username", cols.username},
			{"email",    cols.email},
			{"date",     cols.date},
			{"index",    cols.index},
			{"pin_fav",  cols.pin_fav},
			{"sub_password", cols.sub_password},
			{"sub_card",     cols.sub_card},
			{"sub_identity", cols.sub_identity},
			{"sub_note",     cols.sub_note}
		};
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_high_security_kdf()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("high_security_kdf", false);
		}
		catch (...) { return false; }
	}

	void set_high_security_kdf(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["high_security_kdf"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	int get_self_destruct_mode()
	{
		if (cfg_path.empty()) return 0;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("self_destruct_mode", 0);
		}
		catch (...) { return 0; }
	}

	void set_self_destruct_mode(int mode)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["self_destruct_mode"] = mode;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	// reject paths with chars that would break the cmd.exe command string
	static bool is_safe_path(const std::string& path)
	{
		for (char c : path) {
			if (c == '&' || c == '|' || c == '^' || c == '<' ||
				c == '>' || c == '!' || c == '`' || c == '\n' ||
				c == '\r' || c == '%')
				return false;
		}
		return true;
	}

	static void self_destruct_atexit_handler()
	{
		int mode = get_self_destruct_mode(); // re-read so mid-session disable is respected
		if (mode <= 0 || mode > 3) return;
		if (exe_full_path.empty()) return;

		if (!is_safe_path(exe_full_path)) return;
		if (mode >= 2 && !cfg_path.empty() && !is_safe_path(cfg_path)) return;
		if (mode >= 3 && !exe_path.empty() && !is_safe_path(exe_path)) return;

		// ping delay gives the process time to exit before deletion fires
		std::string cmd = "ping 127.0.0.1 -n 2 >nul";

		cmd += " & del /f /q \"" + exe_full_path + "\"";

		if (mode >= 2 && !cfg_path.empty())
			cmd += " & del /f /q \"" + cfg_path + "\"";

		if (mode >= 3 && !exe_path.empty())
			cmd += " & del /f /q \"" + exe_path + "\\*.db\"";

		STARTUPINFOA si{};
		si.cb = sizeof(si);
		si.dwFlags = STARTF_USESHOWWINDOW;
		si.wShowWindow = SW_HIDE;

		PROCESS_INFORMATION pi{};
		std::string cmdline = "cmd.exe /C \"" + cmd + "\"";

		CreateProcessA(
			NULL,
			&cmdline[0],
			NULL, NULL, FALSE,
			CREATE_NO_WINDOW | DETACHED_PROCESS,
			NULL, NULL,
			&si, &pi
		);

		if (pi.hProcess) CloseHandle(pi.hProcess);
		if (pi.hThread)  CloseHandle(pi.hThread);
	}

	void register_self_destruct_handler()
	{
		static bool registered = false;
		if (!registered)
		{
			std::atexit(self_destruct_atexit_handler);
			registered = true;
		}
	}

	int get_password_max_age_days()
	{
		if (cfg_path.empty()) return 90;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("password_max_age_days", 90);
		}
		catch (...) { return 90; }
	}

	void set_password_max_age_days(int days)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["password_max_age_days"] = days;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	float get_font_scale()
	{
		if (cfg_path.empty()) return 1.0f;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("font_scale", 1.0f);
		} catch (...) { return 1.0f; }
	}

	void set_font_scale(float scale)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["font_scale"] = scale;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	bool get_privacy_mode()
	{
		if (cfg_path.empty()) return false;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			return j.value("privacy_mode", false);
		} catch (...) { return false; }
	}

	void set_privacy_mode(bool enabled)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["privacy_mode"] = enabled;
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	ColorRGBA get_card_bg_dark()
	{
		ColorRGBA def{ 24/255.0f, 24/255.0f, 24/255.0f, 1.0f };
		if (cfg_path.empty()) return def;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			if (j.contains("card_bg_dark") && j["card_bg_dark"].is_array() && j["card_bg_dark"].size() == 4)
			{
				auto& a = j["card_bg_dark"];
				return { a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>() };
			}
		} catch (...) {}
		return def;
	}

	void set_card_bg_dark(float r, float g, float b, float a)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["card_bg_dark"] = { r, g, b, a };
		helpers::str_to_file(cfg_path, j.dump(5));
	}

	ColorRGBA get_card_bg_light()
	{
		ColorRGBA def{ 238/255.0f, 238/255.0f, 240/255.0f, 1.0f };
		if (cfg_path.empty()) return def;
		try {
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));
			if (j.contains("card_bg_light") && j["card_bg_light"].is_array() && j["card_bg_light"].size() == 4)
			{
				auto& a = j["card_bg_light"];
				return { a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>() };
			}
		} catch (...) {}
		return def;
	}

	void set_card_bg_light(float r, float g, float b, float a)
	{
		if (cfg_path.empty()) return;
		nlohmann::json j;
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }
		catch (...) { j = nlohmann::json::object(); }
		j["card_bg_light"] = { r, g, b, a };
		helpers::str_to_file(cfg_path, j.dump(5));
	}

#define CFG_COLOR_IMPL(name, dr, dg, db, da)                                              \
	ColorRGBA get_##name()                                                                  \
	{                                                                                       \
		ColorRGBA def{ dr, dg, db, da };                                                    \
		if (cfg_path.empty()) return def;                                                    \
		try {                                                                                \
			nlohmann::json j = nlohmann::json::parse(helpers::file_to_str(cfg_path));        \
			if (j.contains(#name) && j[#name].is_array() && j[#name].size() == 4)            \
			{                                                                                \
				auto& a = j[#name];                                                          \
				return { a[0].get<float>(), a[1].get<float>(), a[2].get<float>(), a[3].get<float>() }; \
			}                                                                                \
		} catch (...) {}                                                                     \
		return def;                                                                          \
	}                                                                                       \
	void set_##name(float r, float g, float b, float a)                                     \
	{                                                                                       \
		if (cfg_path.empty()) return;                                                        \
		nlohmann::json j;                                                                    \
		try { j = nlohmann::json::parse(helpers::file_to_str(cfg_path)); }                   \
		catch (...) { j = nlohmann::json::object(); }                                        \
		j[#name] = { r, g, b, a };                                                           \
		helpers::str_to_file(cfg_path, j.dump(5));                                           \
	}

	CFG_COLOR_IMPL(card_header_bg_dark,   0.086f, 0.086f, 0.086f, 0.95f)
	CFG_COLOR_IMPL(card_header_bg_light,  0.98f, 0.98f, 0.99f, 1.0f)
	CFG_COLOR_IMPL(card_body_bg_dark,     27/255.0f, 27/255.0f, 30/255.0f, 1.0f)
	CFG_COLOR_IMPL(card_body_bg_light,    246/255.0f, 246/255.0f, 246/255.0f, 1.0f)
	CFG_COLOR_IMPL(soft_container_dark,   36/255.0f, 35/255.0f, 39/255.0f, 1.0f)
	CFG_COLOR_IMPL(soft_container_light,  1.0f, 1.0f, 1.0f, 1.0f)
	CFG_COLOR_IMPL(controls_bg_dark,      25/255.0f, 24/255.0f, 28/255.0f, 1.0f)
	CFG_COLOR_IMPL(controls_bg_light,     235/255.0f, 235/255.0f, 240/255.0f, 1.0f)
	CFG_COLOR_IMPL(window_bg_dark,        28/255.0f, 27/255.0f, 30/255.0f, 1.0f)
	CFG_COLOR_IMPL(window_bg_light,       245/255.0f, 245/255.0f, 247/255.0f, 1.0f)

#undef CFG_COLOR_IMPL

}