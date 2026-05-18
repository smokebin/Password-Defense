
//save.h
#pragma once

#include <string>

namespace cfg {
	extern std::string _path;
	extern std::string cfg_path;
	extern std::string db_name;
	extern std::string exe_path;
	extern std::string exe_full_path;

	void update_db_path();
	void init();

	// Self-destruct on exit (0=Off, 1=Exe Only, 2=Exe+Config, 3=Everything)
	int  get_self_destruct_mode();
	void set_self_destruct_mode(int mode);
	void register_self_destruct_handler();

	// Sync configuration
	std::string get_sync_server_url();
	void set_sync_server_url(const std::string& url);

	// Theme configuration (0 = dark, 1 = light)
	int get_theme();
	void set_theme(int theme);
	bool is_dark_theme();

	// Clipboard auto-clear (security)
	// Returns delay in seconds: 0=Never, 10, 20, 60
	int get_clipboard_clear_delay();
	void set_clipboard_clear_delay(int seconds);

	// Auto-lock timeout (security)
	// Returns timeout in seconds: 0=Never, 60, 300, 900 (1min, 5min, 15min)
	int get_auto_lock_timeout();
	void set_auto_lock_timeout(int seconds);

	// Master re-prompt (security)
	bool get_reprompt_reveal_password();
	void set_reprompt_reveal_password(bool enabled);
	bool get_reprompt_export();
	void set_reprompt_export(bool enabled);
	bool get_reprompt_disable_readonly();
	void set_reprompt_disable_readonly(bool enabled);
	bool get_reprompt_reveal_notes();
	void set_reprompt_reveal_notes(bool enabled);
	int  get_reprompt_lockout_count();
	void set_reprompt_lockout_count(int count);

	// Hover-expand (UX)
	bool get_hover_expand();
	void set_hover_expand(bool enabled);
	bool get_show_group_count();
	void set_show_group_count(bool enabled);
	int  get_row_gap();
	void set_row_gap(int gap);

	// View & sorting preferences
	int  get_view_mode();       // 1=Detailed, 2=Tiles, 3=ThreePane, 4=Table
	void set_view_mode(int mode);
	int  get_order_key();       // 0=Title, 1=Created, 2=Updated
	void set_order_key(int key);
	int  get_order_dir();       // 0=Asc, 1=Desc
	void set_order_dir(int dir);
	int  get_group_mode();      // 0=None, 1=Group, 2=MonthCreated, 3=MonthUpdated, 4=PinnedFavorites
	void set_group_mode(int mode);

	// Pill tab customization (filter tabs in control row)
	std::string get_pill_tab_0();  // default "Pinned"
	void set_pill_tab_0(const std::string& label);
	std::string get_pill_tab_1();  // default "Favorites"
	void set_pill_tab_1(const std::string& label);

	// Vault behavior
	bool get_autosave_enabled();
	void set_autosave_enabled(bool enabled);
	bool get_autoscroll_enabled();
	void set_autoscroll_enabled(bool enabled);
	bool get_always_on_top();
	void set_always_on_top(bool enabled);

	// System tray / startup behavior
	bool get_minimize_to_tray();
	void set_minimize_to_tray(bool enabled);
	bool get_start_on_boot();              // reads from registry (source of truth)
	void set_start_on_boot(bool enabled);  // writes to registry
	bool get_start_minimized();
	void set_start_minimized(bool enabled);
	bool get_auto_open_vault();
	void set_auto_open_vault(bool enabled);

	// Trash bin retention
	int  get_trash_retention_days();      // default 30
	void set_trash_retention_days(int days);

	// Backup settings
	bool get_auto_backup();
	void set_auto_backup(bool enabled);
	int  get_backup_keep_count();
	void set_backup_keep_count(int count);

	// Local extension server
	bool get_local_server_enabled();
	void set_local_server_enabled(bool enabled);
	int  get_local_server_port();
	void set_local_server_port(int port);

	// Local server pairings (browser extension HMAC pairing)
	std::string get_local_server_pairings_raw();   // returns JSON array string
	void set_local_server_pairings_raw(const std::string& json_array);

	// High-security KDF option (uses SENSITIVE Argon2id params — slower unlock)
	bool get_high_security_kdf();
	void set_high_security_kdf(bool enabled);

	// Password max age (days) — aging alert threshold
	int  get_password_max_age_days();      // default 90
	void set_password_max_age_days(int days);

	// Font scale
	float get_font_scale();
	void set_font_scale(float scale);

	// Privacy mode
	bool get_privacy_mode();
	void set_privacy_mode(bool enabled);

	// Custom card background colors (stored as 4 floats RGBA)
	struct ColorRGBA { float r, g, b, a; };
	ColorRGBA get_card_bg_dark();
	void set_card_bg_dark(float r, float g, float b, float a);
	ColorRGBA get_card_bg_light();
	void set_card_bg_light(float r, float g, float b, float a);

	// Extended color customization
	ColorRGBA get_card_header_bg_dark();
	void set_card_header_bg_dark(float r, float g, float b, float a);
	ColorRGBA get_card_header_bg_light();
	void set_card_header_bg_light(float r, float g, float b, float a);
	ColorRGBA get_card_body_bg_dark();
	void set_card_body_bg_dark(float r, float g, float b, float a);
	ColorRGBA get_card_body_bg_light();
	void set_card_body_bg_light(float r, float g, float b, float a);
	ColorRGBA get_soft_container_dark();
	void set_soft_container_dark(float r, float g, float b, float a);
	ColorRGBA get_soft_container_light();
	void set_soft_container_light(float r, float g, float b, float a);
	ColorRGBA get_controls_bg_dark();
	void set_controls_bg_dark(float r, float g, float b, float a);
	ColorRGBA get_controls_bg_light();
	void set_controls_bg_light(float r, float g, float b, float a);
	ColorRGBA get_window_bg_dark();
	void set_window_bg_dark(float r, float g, float b, float a);
	ColorRGBA get_window_bg_light();
	void set_window_bg_light(float r, float g, float b, float a);

	// Three-pane collapse state
	bool get_three_pane_sidebar_collapsed();
	void set_three_pane_sidebar_collapsed(bool collapsed);
	bool get_three_pane_list_collapsed();
	void set_three_pane_list_collapsed(bool collapsed);
	bool get_three_pane_sidebar_auto_collapse();
	void set_three_pane_sidebar_auto_collapse(bool enabled);
	bool get_three_pane_list_auto_collapse();
	void set_three_pane_list_auto_collapse(bool enabled);

	// Detailed header column visibility
	struct DetailedHeaderColumns {
		bool title    = true;
		bool username = true;
		bool email    = true;
		bool date     = true;
		bool index    = true;
		bool pin_fav  = true;
		bool sub_password = true;
		bool sub_card     = true;
		bool sub_identity = true;
		bool sub_note     = false;  // off by default — exposes sensitive content
	};
	DetailedHeaderColumns get_detailed_header_columns();
	void set_detailed_header_columns(const DetailedHeaderColumns& cols);
}