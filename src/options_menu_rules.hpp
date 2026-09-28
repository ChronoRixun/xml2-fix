#pragma once

// The in-game Advanced Options rows' decisions, kept apart from the hooks so xml2_test can check
// them: which rows there are and what each offers, how a choice maps to and from the [Display]
// keys of xml2-fix.ini, where the rows sit in the panel's 640x480 space, how the panel's keyboard
// navigation is relinked around them, the status line, and the bytes of the four call sites in
// XMen2.exe the menu patches (all read from the retail build's disassembly; see
// docs/in-game-options-plan.md).

#include "display_rules.hpp"
#include "frame_rate_rules.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace options_menu_rules
{
	// ---- Rows ------------------------------------------------------------------------------------

	enum class row
	{
		mode,       // Display mode: Fullscreen / Borderless / Windowed ([Display] Mode)
		frame_rate, // Frame rate: 30 ... 240 / Refresh / Unlimited ([Display] FrameRate)
		vsync,      // VSync: Off / On ([Display] VSync)
		background  // Run in background: Off / On ([Display] RunInBackground)
	};
	constexpr int row_count = 4;

	inline constexpr std::array<const char*, row_count> row_labels{"Display mode", "Frame rate", "VSync", "Run in background"};
	inline constexpr std::array<const wchar_t*, row_count> row_keys{L"Mode", L"FrameRate", L"VSync", L"RunInBackground"};

	// Item ids. The game's panel uses 1, 6-9, 0xa-0xf and 0x12-0x18; it finds items by id
	// (FUN_00621d60), so ours must not collide.
	constexpr int first_item_id = 0x40;
	constexpr int status_item_id = 0x4f;
	constexpr int fsaa_item_id = 6;        // the FSAA cycle, the template for our rows and the row above ours
	constexpr int accept_item_id = 1;      // the Accept button, the row below ours
	constexpr int resolution_label_id = 0x15;
	constexpr int selected_image_id = 0x12;

	inline int item_id(const row r)
	{
		return first_item_id + static_cast<int>(r);
	}

	inline std::optional<row> row_for_item_id(const int id)
	{
		if (id < first_item_id || id >= first_item_id + row_count)
		{
			return std::nullopt;
		}
		return static_cast<row>(id - first_item_id);
	}

	// ---- Geometry (the panel's 640x480 virtual space, as the builder's setRect calls) --------------
	// The FSAA row is label (33,146,196,12) style 3, value (177,146,40,18) style 5, and the highlight
	// bar (toggle.png, 196x38) sits at (30, 125) for it: 21 above the row. The left pane background
	// (bgleft.png) spans y 77..353; the Accept button starts at y 335.

	constexpr int row_x = 33;
	constexpr int row_w = 196;
	constexpr int row_h = 12;
	constexpr int first_row_y = 170; // 170, 194, 218, 242: below FSAA (146), above Accept (335)
	constexpr int row_pitch = 24;
	constexpr int value_x = 120; // wider than FSAA's 40 px for "Fullscreen" / "Unlimited", ending at 217 like it
	constexpr int value_w = 97;
	constexpr int value_h = 18;
	constexpr int status_x = 33;
	constexpr int status_y = 272;
	constexpr int status_w = 196;
	constexpr int status_h = 24;
	constexpr int highlight_x = 30;
	constexpr int highlight_dy = -21;
	constexpr int accept_y = 335;
	constexpr int fsaa_y = 146;

	// Text styles: style % 3 is the alignment (0 left, 1 centre, 2 right), style / 3 the font (0 the
	// small one, 1 and 2 the large one), read from the panel's drawText (0x617910) and the builder's
	// use of 0 / 2 / 3 / 4 / 5.
	constexpr int label_style = 3;  // large, left: like the FSAA label
	constexpr int value_style = 5;  // large, right: like the FSAA value
	constexpr int status_style = 0; // small, left: like the key-binding help text

	constexpr DWORD colour_selected = 0xffffffff;
	constexpr DWORD colour_idle = 0xffcccccc;

	// The highlight bar's animation slot. An item has 7 (0x2c bytes each from +0x78); the bar uses
	// 0/1 for the panel's enter/exit slide and 2/3/4 for the game's rows, 5 and 6 are free.
	constexpr int highlight_slot = 5;
	constexpr float highlight_seconds = 0.05f; // the game's own highlight takes 0.05 s

	inline int row_y(const int index)
	{
		return first_row_y + index * row_pitch;
	}

	inline int highlight_y(const int index)
	{
		return row_y(index) + highlight_dy;
	}

	// ---- The choices each row offers, and what they mean for the ini ------------------------------

	struct choices
	{
		std::vector<std::string> texts;
		int selected = 0;
	};

	inline constexpr std::array<const char*, 3> mode_texts{"Fullscreen", "Borderless", "Windowed"};
	inline constexpr std::array<const char*, 3> mode_values{"fullscreen", "borderless", "windowed"};
	inline constexpr std::array<const char*, 2> off_on_texts{"Off", "On"};

	// No Mode in the ini (the game's own exclusive fullscreen) shows as Fullscreen.
	inline choices mode_choices(const display_rules::mode current)
	{
		choices result{{mode_texts.begin(), mode_texts.end()}, 0};
		switch (current)
		{
		case display_rules::mode::borderless: result.selected = 1; break;
		case display_rules::mode::windowed: result.selected = 2; break;
		default: break;
		}
		return result;
	}

	inline std::string mode_value(const int index)
	{
		return index >= 0 && index < static_cast<int>(mode_values.size()) ? mode_values[static_cast<size_t>(index)] : mode_values[0];
	}

	constexpr const char* refresh_text = "Refresh";
	constexpr const char* unlimited_text = "Unlimited";
	inline constexpr std::array<unsigned, 7> frame_rate_presets{30, 60, 120, 144, 165, 180, 240};
	inline constexpr std::array<unsigned, 5> frame_rate_expendable{165, 144, 240, 120, 30}; // dropped first when the desktop's rate and the ini's value need room
	constexpr size_t cycle_max_options = 10;                                                  // a BXIGCycle holds ten strings

	// The presets, the desktop's refresh rate and the ini's own value (so what the launcher wrote is
	// always shown), sorted, then Refresh and Unlimited - ten at most. No FrameRate in the ini (the
	// game's own 60 fps cap) shows as 60.
	inline choices frame_rate_choices(const frame_rate_rules::cap& setting, const unsigned desktop_refresh)
	{
		std::vector<unsigned> numbers(frame_rate_presets.begin(), frame_rate_presets.end());
		const auto add = [&](const unsigned fps)
		{
			if (fps && std::ranges::find(numbers, fps) == numbers.end())
			{
				numbers.push_back(fps);
			}
		};
		add(desktop_refresh);
		const unsigned wanted = setting.what == frame_rate_rules::cap::kind::fixed ? setting.fps : 0;
		add(wanted);
		for (const unsigned drop : frame_rate_expendable)
		{
			if (numbers.size() + 2 <= cycle_max_options)
			{
				break;
			}
			if (drop != desktop_refresh && drop != wanted)
			{
				std::erase(numbers, drop);
			}
		}
		std::ranges::sort(numbers);

		choices result;
		for (const unsigned fps : numbers)
		{
			result.texts.push_back(std::to_string(fps));
		}
		result.texts.emplace_back(refresh_text);
		result.texts.emplace_back(unlimited_text);

		std::string shown;
		switch (setting.what)
		{
		case frame_rate_rules::cap::kind::unlimited: shown = unlimited_text; break;
		case frame_rate_rules::cap::kind::refresh: shown = refresh_text; break;
		case frame_rate_rules::cap::kind::fixed: shown = std::to_string(setting.fps); break;
		default: shown = std::to_string(frame_rate_rules::stock_fps); break;
		}
		const auto at = std::ranges::find(result.texts, shown);
		result.selected = at == result.texts.end() ? 0 : static_cast<int>(at - result.texts.begin());
		return result;
	}

	// The ini value for a frame-rate choice: "refresh", "0" (unlimited) or the number.
	inline std::string frame_rate_value(const std::string_view text)
	{
		if (text == refresh_text) return "refresh";
		if (text == unlimited_text) return "0";
		return std::string(text);
	}

	inline choices vsync_choices(const std::optional<bool> current)
	{
		return {{off_on_texts.begin(), off_on_texts.end()}, current.value_or(false) ? 1 : 0};
	}

	inline choices background_choices(const bool run_in_background)
	{
		return {{off_on_texts.begin(), off_on_texts.end()}, run_in_background ? 1 : 0};
	}

	// What every row shows when the panel opens, from the ini as it is then.
	struct panel_choices
	{
		std::array<choices, row_count> rows;

		std::array<int, row_count> selected() const
		{
			std::array<int, row_count> result{};
			for (int i = 0; i < row_count; ++i)
			{
				result[static_cast<size_t>(i)] = rows[static_cast<size_t>(i)].selected;
			}
			return result;
		}
	};

	inline panel_choices choices_for(const display_rules::options& opts, const unsigned desktop_refresh)
	{
		return {{mode_choices(opts.window_mode), frame_rate_choices(opts.frame_rate, desktop_refresh), vsync_choices(opts.vsync), background_choices(opts.run_in_background)}};
	}

	// The stock defaults ("Revert to default"): the game's own fullscreen, 60 fps, no vsync, keep running.
	inline std::array<int, row_count> default_selection(const panel_choices& shown)
	{
		const auto& frame_texts = shown.rows[static_cast<size_t>(row::frame_rate)].texts;
		const auto sixty = std::ranges::find(frame_texts, std::to_string(frame_rate_rules::stock_fps));
		return {0, sixty == frame_texts.end() ? 0 : static_cast<int>(sixty - frame_texts.begin()), 0, 1};
	}

	// One key to write.
	struct ini_change
	{
		row which;
		const wchar_t* key;
		std::string value;
		bool operator==(const ini_change&) const = default;
	};

	// The keys whose row the user changed (Accept writes only these, so the rest of the file and the
	// launcher's other settings stay as they are).
	inline std::vector<ini_change> ini_changes(const panel_choices& shown, const std::array<int, row_count>& now)
	{
		std::vector<ini_change> changes;
		for (int i = 0; i < row_count; ++i)
		{
			const auto index = static_cast<size_t>(i);
			const auto& offered = shown.rows[index];
			const int chosen = now[index];
			if (chosen == offered.selected || chosen < 0 || chosen >= static_cast<int>(offered.texts.size()))
			{
				continue;
			}
			std::string value;
			switch (static_cast<row>(i))
			{
			case row::mode: value = mode_value(chosen); break;
			case row::frame_rate: value = frame_rate_value(offered.texts[static_cast<size_t>(chosen)]); break;
			default: value = chosen ? "1" : "0"; break;
			}
			changes.push_back({static_cast<row>(i), row_keys[index], std::move(value)});
		}
		return changes;
	}

	// The line under the rows. The display mode always waits for a restart (owner decision 2);
	// VSync does in fullscreen (the presentation interval is set when the device is created), while
	// in a window it is a pacing change that applies at once.
	inline std::string status_text(const bool mode_changed, const bool vsync_waits_for_restart)
	{
		if (mode_changed && vsync_waits_for_restart) return "Display mode, VSync apply after restart";
		if (mode_changed) return "Display mode applies after restart";
		if (vsync_waits_for_restart) return "VSync applies after restart";
		return "";
	}

	// ---- The panel's navigation records --------------------------------------------------------------
	// BXIGWindow keeps an array of pointers to these (window+0x18, count window+0x28); on WM_KEYUP it
	// moves the keyboard selection along them (FUN_00621ea0). A neighbour equal to self, or a set
	// keep flag, forwards the key to the item instead of moving (left/right cycle the value).

	struct nav_record
	{
		void* self;
		void* left;
		void* right;
		void* up;
		void* down;
		std::uint8_t keep_left_right;
		std::uint8_t keep_up_down;
		std::uint8_t padding[2];
	};
	static_assert(sizeof(nav_record) == 0x18);

	// One of our rows: left/right stay on the row and cycle it (as the FSAA record does), up/down move.
	inline nav_record row_record(void* self, void* up, void* down)
	{
		return {self, self, self, up, down, 1, 0, {0, 0}};
	}

	// The game's own records around ours: FSAA's down was Accept and Accept's up was FSAA; now they
	// are our first and last rows. Returns how many records were changed (2 when both were found).
	inline int relink(nav_record* const* records, const int count, const void* fsaa, const void* accept, void* first_row, void* last_row)
	{
		int changed = 0;
		for (int i = 0; i < count; ++i)
		{
			nav_record* record = records[i];
			if (!record)
			{
				continue;
			}
			if (record->self == fsaa)
			{
				record->down = first_row;
				++changed;
			}
			else if (record->self == accept)
			{
				record->up = last_row;
				++changed;
			}
		}
		return changed;
	}

	// ---- An item's animation slot (0x2c bytes; FUN_00620090 copies eleven dwords) ------------------------

	using anim_function = int(__cdecl*)(void* item, float dt); // returns non-zero while running

	struct anim_slot
	{
		anim_function function;
		int start_x;
		int start_y;
		int target_x;
		int target_y;
		float delay;
		float duration;
		float elapsed;
		void* target_item; // ours: the row the highlight moves to (unused by the game's animations)
		int unused;
		int slot;
	};
	static_assert(sizeof(anim_slot) == 0x2c);

	// Where the highlight bar is after `t` of the way (0..1) from its start to a row, as the game's
	// own highlight (FUN_00617f10) moves it: linear, truncated to whole virtual pixels.
	inline int interpolate(const int from, const int to, const float t)
	{
		return from + static_cast<int>(static_cast<float>(to - from) * t);
	}

	// ---- XMen2.exe: the call sites the menu patches, and the functions it calls ---------------------------

	// A `call rel32` in the game, with the 16 bytes from it as the retail build has them.
	struct call_site
	{
		const char* what;
		DWORD rva;                             // of the E8 byte, from the module base
		std::array<std::uint8_t, 16> expected; // the call and what follows it
		DWORD target_rva;                      // where the retail build's call goes
	};

	// A  0x61f356  call FUN_006222b0  the builder (FUN_0061dc10) starts the enter animation: ecx = the panel, one pushed 0
	inline constexpr call_site finish_panel_site{"the panel builder's final call (0x61f356)", 0x21f356,
	                                             {0xE8, 0x55, 0x2F, 0x00, 0x00, 0x8B, 0x8C, 0x24, 0xA8, 0x00, 0x00, 0x00, 0x5F, 0x5E, 0x5D, 0xB0}, 0x2222b0};
	// B  0x61f8a4  call FUN_00619440  the close function (FUN_0061f380) saves the settings to the registry (Accept)
	inline constexpr call_site save_site{"the panel's save call (0x61f8a4)", 0x21f8a4,
	                                     {0xE8, 0x97, 0x9B, 0xFF, 0xFF, 0xE8, 0x22, 0x26, 0xF3, 0xFF, 0x8B, 0x10, 0x8B, 0xC8, 0xFF, 0x52}, 0x219440};
	// C  0x61f8be  call FUN_00619770  the close function reloads the settings (Cancel)
	inline constexpr call_site cancel_site{"the panel's cancel call (0x61f8be)", 0x21f8be,
	                                       {0xE8, 0xAD, 0x9E, 0xFF, 0xFF, 0x8B, 0x15, 0x34, 0xAD, 0xA6, 0x00, 0x33, 0xC0, 0x3B, 0xD5, 0xB9}, 0x219770};
	// D  0x61f667  call FUN_006196c0  the revert popup's "yes" loads the defaults
	inline constexpr call_site revert_site{"the panel's revert call (0x61f667)", 0x21f667,
	                                       {0xE8, 0x54, 0xA0, 0xFF, 0xFF, 0x8B, 0x0D, 0x34, 0xAD, 0xA6, 0x00, 0x6A, 0x0E, 0xE8, 0xE7, 0x26}, 0x2196c0};
	inline constexpr std::array<const call_site*, 4> call_sites{&finish_panel_site, &save_site, &cancel_site, &revert_site};

	inline bool matches(const call_site& site, const std::uint8_t* bytes)
	{
		return std::memcmp(bytes, site.expected.data(), site.expected.size()) == 0;
	}

	// The target the expected bytes call (an offline check that the table is consistent).
	inline DWORD decoded_target(const call_site& site)
	{
		std::int32_t rel = 0;
		std::memcpy(&rel, site.expected.data() + 1, sizeof(rel));
		return static_cast<DWORD>(static_cast<std::int32_t>(site.rva) + 5 + rel);
	}

	// The rel32 for a call at `site_address` to `target_address`.
	inline std::int32_t rel32(const std::uintptr_t site_address, const std::uintptr_t target_address)
	{
		return static_cast<std::int32_t>(static_cast<std::intptr_t>(target_address) - static_cast<std::intptr_t>(site_address + 5));
	}

	// The game functions the rows are built with: RVAs and the first 16 bytes of each, checked
	// before anything is patched so an unknown build gets its panel left alone.
	struct game_function
	{
		const char* what;
		DWORD rva;
		std::array<std::uint8_t, 16> expected;
	};

	namespace game
	{
		constexpr DWORD operator_new = 0x271fc2;     // cdecl void*(size_t)
		constexpr DWORD cycle_ctor = 0x223e10;       // thiscall BXIGCycle*(this, window, id, png)
		constexpr DWORD label_ctor = 0x2229a0;       // thiscall BXIGLabel*(this, window, id)
		constexpr DWORD set_rect = 0x21fe20;         // thiscall (this, x, y, w, h) virtual 640x480
		constexpr DWORD add_option = 0x2216a0;       // thiscall (this, index < 10, text) copies the text
		constexpr DWORD option_rect = 0x221750;      // thiscall (this, x, y, w, h, style)
		constexpr DWORD set_selection = 0x221730;    // thiscall (this, index): sets +0x1e0, fires callback event 1
		constexpr DWORD register_nav = 0x221e00;     // thiscall (window, nav_record*): the window frees it with operator delete
		constexpr DWORD find_item = 0x221d60;        // thiscall BXIGItem*(window, id)
		constexpr DWORD set_all_anim = 0x2222b0;     // thiscall (window, slot): the original at site A
		constexpr DWORD save_settings = 0x219440;    // cdecl (): the original at site B
		constexpr DWORD load_settings = 0x219770;    // cdecl (): the original at site C
		constexpr DWORD load_defaults = 0x2196c0;    // cdecl (): the original at site D
		constexpr DWORD sound_system = 0x1d8920;     // cdecl object*(); vtable +0xe8 = thiscall play(this, int): 6 click, 0 hover
		constexpr DWORD panel_pointer = 0x66ad34;    // XML2IGConfig* DAT_00a6ad34: the open panel, 0 when closed
		constexpr DWORD toggle_png_string = 0x2a5350; // "texs\\toggle.png" in .rdata (ours is passed instead; same text)

		constexpr std::size_t cycle_size = 0x208;
		constexpr std::size_t label_size = 0x1b0;
		constexpr std::size_t item_anim_slots = 0x78;   // 7 x anim_slot from here
		constexpr std::size_t item_virtual_x = 0x14;    // then y, w, h
		constexpr std::size_t item_callback = 0x68;     // then userdata
		constexpr std::size_t item_id = 0x5c;
		constexpr std::size_t item_anim_current = 0x70; // -1 = none; +0x74 the previous slot
		constexpr std::size_t cycle_selected = 0x1e0;
		constexpr std::size_t window_nav_records = 0x18; // nav_record** ; count at +0x28
		constexpr std::size_t window_nav_count = 0x28;
		constexpr std::size_t panel_dirty = 0x285c8;
		constexpr std::size_t vtable_set_visible = 0x14; // (this, bool)
		constexpr std::size_t vtable_set_text = 0x2c;    // (this, const char*) copies
		constexpr std::size_t vtable_set_colour = 0x30;  // (this, ARGB)
		constexpr std::size_t vtable_set_style = 0x34;   // (this, int)
		constexpr std::size_t vtable_play_sound = 0xe8;  // sound system: (this, int)
		constexpr int sound_click = 6;
		constexpr int sound_hover = 0;
		constexpr int event_changed = 2; // BXIGCycle fires 1 then 2 on a change; set_selection fires 1 only
		constexpr int event_focus = 3;
		constexpr int event_blur = 4;
	}

	inline constexpr std::array<game_function, 9> game_functions{{
		{"operator new (0x671fc2)", game::operator_new, {0x56, 0x8B, 0x74, 0x24, 0x08, 0xEB, 0x10, 0x56, 0xE8, 0x99, 0x08, 0x00, 0x00, 0x85, 0xC0, 0x59}},
		{"BXIGCycle constructor (0x623e10)", game::cycle_ctor, {0x6A, 0xFF, 0x68, 0xB8, 0x99, 0x67, 0x00, 0x64, 0xA1, 0x00, 0x00, 0x00, 0x00, 0x50, 0x64, 0x89}},
		{"BXIGLabel constructor (0x6229a0)", game::label_ctor, {0x8B, 0x44, 0x24, 0x08, 0x56, 0x8B, 0xF1, 0x8B, 0x4C, 0x24, 0x08, 0x50, 0x51, 0x8B, 0xCE, 0xE8}},
		{"BXIGItem::setRect (0x61fe20)", game::set_rect, {0x8B, 0x44, 0x24, 0x04, 0x8B, 0x54, 0x24, 0x08, 0x89, 0x41, 0x14, 0x8B, 0x44, 0x24, 0x0C, 0x89}},
		{"BXIGCycle::addOption (0x6216a0)", game::add_option, {0x53, 0x8B, 0x5C, 0x24, 0x08, 0x83, 0xFB, 0x09, 0x57, 0x8B, 0xF9, 0x7F, 0x6F, 0x8B, 0x84, 0x9F}},
		{"BXIGCycle::setOptionRect (0x621750)", game::option_rect, {0x8B, 0x44, 0x24, 0x10, 0xDB, 0x44, 0x24, 0x08, 0x53, 0x8B, 0x5C, 0x24, 0x08, 0x55, 0x8B, 0x6C}},
		{"BXIGCycle::setSelection (0x621730)", game::set_selection, {0x8B, 0x44, 0x24, 0x04, 0x8B, 0x11, 0x89, 0x81, 0xE0, 0x01, 0x00, 0x00, 0xC7, 0x44, 0x24, 0x04}},
		{"BXIGWindow::registerNav (0x621e00)", game::register_nav, {0x56, 0x8B, 0xF1, 0x8B, 0x46, 0x30, 0x39, 0x46, 0x28, 0x75, 0x40, 0x83, 0xC0, 0x64, 0x89, 0x46}},
		{"BXIGWindow::findItem (0x621d60)", game::find_item, {0x8B, 0x51, 0x24, 0x53, 0x56, 0x33, 0xC0, 0x85, 0xD2, 0x57, 0x7E, 0x18, 0x8B, 0x71, 0x14, 0x8B}},
	}};

	inline bool matches(const game_function& function, const std::uint8_t* bytes)
	{
		return std::memcmp(bytes, function.expected.data(), function.expected.size()) == 0;
	}
}
