#include "options_menu.hpp"
#include "display.hpp"
#include "log.hpp"
#include "options_menu_rules.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <string>

namespace options_menu
{
	namespace
	{
		using namespace options_menu_rules;

		std::uint8_t* base = nullptr; // XMen2.exe
		bool patched = false;
		constexpr char toggle_png[] = "texs\\toggle.png"; // the FSAA row's texture; the loader doesn't keep the pointer, but this lives as long as the DLL anyway

		// The game's functions. `this` travels in ecx (__thiscall); __fastcall with an unused edx
		// parameter calls them the same way: ecx, then the stack, and the callee cleans up.
		using operator_new_t = void*(__cdecl*)(std::size_t);
		using cycle_ctor_t = void*(__fastcall*)(void* self, void* edx, void* window, int id, const char* png);
		using label_ctor_t = void*(__fastcall*)(void* self, void* edx, void* window, int id);
		using set_rect_t = void(__fastcall*)(void* self, void* edx, int x, int y, int w, int h);
		using add_option_t = void(__fastcall*)(void* self, void* edx, int index, const char* text);
		using option_rect_t = void(__fastcall*)(void* self, void* edx, int x, int y, int w, int h, int style);
		using set_selection_t = void(__fastcall*)(void* self, void* edx, int index);
		using register_nav_t = void(__fastcall*)(void* window, void* edx, nav_record* record);
		using find_item_t = void*(__fastcall*)(void* window, void* edx, int id);
		using set_all_anim_t = void(__fastcall*)(void* window, void* edx, int slot);
		using plain_t = void(__cdecl*)();
		using sound_system_t = void*(__cdecl*)();
		using set_int_t = void(__fastcall*)(void* self, void* edx, int value);
		using set_dword_t = void(__fastcall*)(void* self, void* edx, DWORD value);
		using set_text_t = void(__fastcall*)(void* self, void* edx, const char* text);

		template <typename T>
		T game_function(const DWORD rva)
		{
			return reinterpret_cast<T>(base + rva);
		}

		template <typename T>
		T& field(void* object, const std::size_t offset)
		{
			return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
		}

		template <typename T>
		T method(void* object, const std::size_t vtable_offset)
		{
			return *reinterpret_cast<T*>(*static_cast<std::uint8_t**>(object) + vtable_offset);
		}

		void* current_panel()
		{
			return *reinterpret_cast<void**>(base + game::panel_pointer);
		}

		void* find_item(void* panel, const int id)
		{
			return game_function<find_item_t>(game::find_item)(panel, nullptr, id);
		}

		void set_text(void* item, const char* text)
		{
			method<set_text_t>(item, game::vtable_set_text)(item, nullptr, text);
		}

		void set_colour(void* item, const DWORD argb)
		{
			method<set_dword_t>(item, game::vtable_set_colour)(item, nullptr, argb);
		}

		void set_style(void* item, const int style)
		{
			method<set_int_t>(item, game::vtable_set_style)(item, nullptr, style);
		}

		void set_visible(void* item, const bool visible)
		{
			method<set_int_t>(item, game::vtable_set_visible)(item, nullptr, visible ? 1 : 0);
		}

		void set_rect(void* item, const int x, const int y, const int w, const int h)
		{
			game_function<set_rect_t>(game::set_rect)(item, nullptr, x, y, w, h);
		}

		void play_sound(const int which)
		{
			if (void* system = game_function<sound_system_t>(game::sound_system)())
			{
				method<set_int_t>(system, game::vtable_play_sound)(system, nullptr, which);
			}
		}

		anim_slot& slot_of(void* item, const int slot)
		{
			return *reinterpret_cast<anim_slot*>(static_cast<std::uint8_t*>(item) + game::item_anim_slots + static_cast<std::size_t>(slot) * sizeof(anim_slot));
		}

		// The panel that is open, with the rows the fix added to it.
		struct open_panel
		{
			void* panel = nullptr;
			void* bar = nullptr; // the toggle.png highlight bar (the FSAA row's callback userdata)
			panel_choices shown; // what the rows offered and showed when the panel opened
		};
		open_panel current;

		std::wstring ini_path()
		{
			return (logger::module_dir() / L"xml2-fix.ini").wstring();
		}

		const char* text_of(const row which, const int index)
		{
			const auto& texts = current.shown.rows[static_cast<std::size_t>(which)].texts;
			return index >= 0 && index < static_cast<int>(texts.size()) ? texts[static_cast<std::size_t>(index)].c_str() : "?";
		}

		// What the rows show now.
		std::array<int, row_count> selection_now(void* panel)
		{
			auto result = current.shown.selected();
			for (int i = 0; i < row_count; ++i)
			{
				if (void* item = find_item(panel, item_id(static_cast<row>(i))))
				{
					result[static_cast<std::size_t>(i)] = field<int>(item, game::cycle_selected);
				}
			}
			return result;
		}

		void update_status(void* panel)
		{
			if (!panel || panel != current.panel)
			{
				return;
			}
			void* status = find_item(panel, status_item_id);
			if (!status)
			{
				return;
			}
			const auto now = selection_now(panel);
			const auto shown = current.shown.selected();
			const bool mode_changed = now[static_cast<std::size_t>(row::mode)] != shown[static_cast<std::size_t>(row::mode)];
			const bool vsync_changed = now[static_cast<std::size_t>(row::vsync)] != shown[static_cast<std::size_t>(row::vsync)];
			const bool vsync_waits = vsync_changed && !display_rules::manages_window(display::current_options().window_mode);
			set_text(status, status_text(mode_changed, vsync_waits).c_str());
		}

		// ---- The highlight bar --------------------------------------------------------------------
		// The game's own highlight function (FUN_00617f10) knows only its rows (slots 2/3/4), so ours
		// runs in the bar's free slot 5, doing what the game's does: on its first frame it hides the
		// Resolution row's selection image, greys every row and whitens the target, then slides the
		// bar there in 0.05 s (at once when the bar is still off-screen, y >= 481).

		int __cdecl highlight_animation(void* bar, const float dt)
		{
			anim_slot& anim = slot_of(bar, highlight_slot);
			void* panel = current_panel();
			if (anim.elapsed == 0.0f && panel)
			{
				anim.start_x = field<int>(bar, game::item_virtual_x);
				anim.start_y = field<int>(bar, game::item_virtual_x + 4);
				if (void* selected = find_item(panel, selected_image_id))
				{
					set_visible(selected, false);
				}
				for (const int id : {resolution_label_id, fsaa_item_id})
				{
					if (void* item = find_item(panel, id))
					{
						set_colour(item, colour_idle);
					}
				}
				for (int i = 0; i < row_count; ++i)
				{
					if (void* item = find_item(panel, item_id(static_cast<row>(i))))
					{
						set_colour(item, item == anim.target_item ? colour_selected : colour_idle);
					}
				}
			}
			anim.elapsed += dt;
			float t = 1.0f;
			if (anim.duration > 0.0f && anim.start_y < 481)
			{
				t = std::min(1.0f, anim.elapsed / anim.duration);
			}
			set_rect(bar, interpolate(anim.start_x, anim.target_x, t), interpolate(anim.start_y, anim.target_y, t), field<int>(bar, game::item_virtual_x + 8),
			         field<int>(bar, game::item_virtual_x + 12));
			return t < 1.0f ? 1 : 0;
		}

		// Sends the bar to one of our rows; true when it starts moving (the hover sound plays then).
		bool highlight(void* bar, void* item, const int row_index)
		{
			int& slot = field<int>(bar, game::item_anim_current);
			if (slot == 0 || slot == 1)
			{
				return false; // the panel is sliding in or out; the game's rows don't take the highlight then either
			}
			anim_slot& anim = slot_of(bar, highlight_slot);
			const bool already_there = anim.target_item == item && (slot == highlight_slot || field<int>(bar, game::item_virtual_x + 4) == highlight_y(row_index));
			if (already_there)
			{
				return false;
			}
			anim.function = &highlight_animation;
			anim.target_x = highlight_x;
			anim.target_y = highlight_y(row_index);
			anim.delay = 0.0f;
			anim.duration = highlight_seconds;
			anim.elapsed = 0.0f;
			anim.target_item = item;
			anim.unused = 0;
			anim.slot = highlight_slot;
			field<int>(bar, game::item_anim_current + 4) = slot;
			slot = highlight_slot;
			return true;
		}

		// ---- The rows' callback (event 1/2 value changed, 3 focus or hover, 4 lost) --------------------

		void __cdecl row_callback(void* item, const int event, void* bar)
		{
			const auto which = row_for_item_id(field<int>(item, game::item_id));
			if (!which)
			{
				return;
			}
			switch (event)
			{
			case game::event_changed:
			{
				void* panel = current_panel();
				if (panel)
				{
					field<std::uint8_t>(panel, game::panel_dirty) = 1; // Back now asks before discarding
				}
				play_sound(game::sound_click);
				update_status(panel);
				logger::write("options: %s -> %s", row_labels[static_cast<std::size_t>(*which)], text_of(*which, field<int>(item, game::cycle_selected)));
				break;
			}
			case game::event_focus:
				if (bar && highlight(bar, item, static_cast<int>(*which)))
				{
					play_sound(game::sound_hover);
				}
				set_colour(item, colour_selected);
				break;
			case game::event_blur:
				set_colour(item, colour_idle);
				break;
			default:
				break;
			}
		}

		// ---- Hook A: the builder's last call, with ecx = the panel and the pushed slot 0 -------------------

		void build_rows(void* panel, const int slot)
		{
			current = {};
			if (slot != 0 || !panel || panel != current_panel())
			{
				logger::write("options: the panel builder finished unexpectedly (slot %d, panel %p, the game's %p) - rows not added", slot, panel, current_panel());
				return;
			}
			void* fsaa = find_item(panel, fsaa_item_id);
			void* accept = find_item(panel, accept_item_id);
			if (!fsaa || !accept)
			{
				logger::write("options: the panel has no FSAA row or Accept button (%p, %p) - rows not added", fsaa, accept);
				return;
			}
			void* bar = field<void*>(fsaa, game::item_callback + 4);

			const auto opts = display::read_ini_options();
			current.shown = choices_for(opts, display::desktop_refresh_rate());

			const auto op_new = game_function<operator_new_t>(game::operator_new);
			const auto cycle_ctor = game_function<cycle_ctor_t>(game::cycle_ctor);
			const auto add_option = game_function<add_option_t>(game::add_option);
			const auto option_rect = game_function<option_rect_t>(game::option_rect);
			const auto set_selection = game_function<set_selection_t>(game::set_selection);

			std::array<void*, row_count> items{};
			for (int i = 0; i < row_count; ++i)
			{
				const auto index = static_cast<std::size_t>(i);
				void* cycle = op_new(game::cycle_size);
				if (!cycle)
				{
					logger::write("options: out of memory building row %d - the rows so far stay, without keyboard links", i);
					return;
				}
				cycle_ctor(cycle, nullptr, panel, item_id(static_cast<row>(i)), toggle_png); // registers itself with the panel
				set_rect(cycle, row_x, row_y(i), row_w, row_h);
				set_text(cycle, row_labels[index]);
				set_style(cycle, label_style);
				const auto& offered = current.shown.rows[index];
				for (std::size_t k = 0; k < offered.texts.size() && k < cycle_max_options; ++k)
				{
					add_option(cycle, nullptr, static_cast<int>(k), offered.texts[k].c_str());
				}
				option_rect(cycle, nullptr, value_x, row_y(i), value_w, value_h, value_style);
				field<void*>(cycle, game::item_callback) = reinterpret_cast<void*>(&row_callback);
				field<void*>(cycle, game::item_callback + 4) = bar;
				set_selection(cycle, nullptr, offered.selected); // fires event 1, which the callback ignores
				std::memcpy(&slot_of(cycle, 0), &slot_of(fsaa, 0), 2 * sizeof(anim_slot)); // the panel's enter and exit slides, as every row has them
				field<int>(cycle, game::item_anim_current) = -1;
				items[index] = cycle;
			}

			if (void* status = op_new(game::label_size))
			{
				game_function<label_ctor_t>(game::label_ctor)(status, nullptr, panel, status_item_id);
				set_rect(status, status_x, status_y, status_w, status_h);
				set_text(status, "");
				set_style(status, status_style);
				set_colour(status, colour_idle);
				std::memcpy(&slot_of(status, 0), &slot_of(fsaa, 0), 2 * sizeof(anim_slot));
				field<int>(status, game::item_anim_current) = -1;
			}

			// Keyboard navigation: FSAA > our rows > Accept. The window frees the records with operator delete.
			const auto register_nav = game_function<register_nav_t>(game::register_nav);
			int registered = 0;
			for (int i = 0; i < row_count; ++i)
			{
				auto* record = static_cast<nav_record*>(op_new(sizeof(nav_record)));
				if (!record)
				{
					break;
				}
				const auto index = static_cast<std::size_t>(i);
				*record = row_record(items[index], i == 0 ? fsaa : items[index - 1], i == row_count - 1 ? accept : items[index + 1]);
				register_nav(panel, nullptr, record);
				++registered;
			}
			const int relinked = relink(field<nav_record**>(panel, game::window_nav_records), field<int>(panel, game::window_nav_count), fsaa, accept, items[0],
			                            items[row_count - 1]);

			current.panel = panel;
			current.bar = bar;
			logger::write("options: rows added to Advanced Options - Display mode %s, Frame rate %s, VSync %s, Run in background %s (%d navigation records, %d relinked%s)",
			              text_of(row::mode, current.shown.rows[0].selected), text_of(row::frame_rate, current.shown.rows[1].selected),
			              text_of(row::vsync, current.shown.rows[2].selected), text_of(row::background, current.shown.rows[3].selected), registered, relinked,
			              bar ? "" : "; no highlight bar found");
		}

		void __fastcall finish_panel(void* panel, void* /*edx*/, const int slot)
		{
			build_rows(panel, slot);
			game_function<set_all_anim_t>(game::set_all_anim)(panel, nullptr, slot); // the enter animation, as the builder meant to
		}

		// ---- Hooks B, C, D: the close function's save, cancel and revert calls ----------------------------

		void apply_live(const ini_change& change)
		{
			switch (change.which)
			{
			case row::mode:
				logger::write("options: display mode %s applies after a restart", change.value.c_str());
				break;
			case row::frame_rate:
				if (const auto cap = frame_rate_rules::parse_frame_rate(change.value))
				{
					display::set_frame_rate(*cap);
				}
				break;
			case row::vsync:
				if (!display::set_vsync(change.value == "1"))
				{
					logger::write("options: VSync %s applies when the device is next created (a restart)", change.value == "1" ? "on" : "off");
				}
				break;
			case row::background:
				display::set_run_in_background(change.value == "1");
				break;
			}
		}

		void __cdecl on_save()
		{
			game_function<plain_t>(game::save_settings)();
			void* panel = current_panel();
			if (!panel || panel != current.panel)
			{
				logger::write("options: accepted, but the rows aren't in this panel - xml2-fix.ini untouched");
				return;
			}
			const auto changes = ini_changes(current.shown, selection_now(panel));
			if (changes.empty())
			{
				logger::write("options: accepted - no display row changed, xml2-fix.ini untouched");
				return;
			}
			const auto ini = ini_path();
			for (const auto& change : changes)
			{
				const std::wstring value(change.value.begin(), change.value.end());
				if (WritePrivateProfileStringW(L"Display", change.key, value.c_str(), ini.c_str()))
				{
					logger::write("options: [Display] %ls=%s written to xml2-fix.ini", change.key, change.value.c_str());
				}
				else
				{
					logger::write("options: ERROR: couldn't write [Display] %ls=%s to xml2-fix.ini (error %lu)", change.key, change.value.c_str(), GetLastError());
				}
				apply_live(change);
			}
		}

		void __cdecl on_cancel()
		{
			game_function<plain_t>(game::load_settings)();
			if (current.panel && current.panel == current_panel())
			{
				logger::write("options: cancelled - xml2-fix.ini untouched");
			}
		}

		void __cdecl on_revert()
		{
			game_function<plain_t>(game::load_defaults)();
			void* panel = current_panel();
			if (!panel || panel != current.panel)
			{
				return;
			}
			const auto defaults = default_selection(current.shown);
			const auto set_selection = game_function<set_selection_t>(game::set_selection);
			for (int i = 0; i < row_count; ++i)
			{
				if (void* item = find_item(panel, item_id(static_cast<row>(i))))
				{
					set_selection(item, nullptr, defaults[static_cast<std::size_t>(i)]);
				}
			}
			update_status(panel);
			logger::write("options: rows reverted to the defaults (Fullscreen, 60, VSync off, run in background on) - written only by Accept");
		}

		// ---- Patching -------------------------------------------------------------------------------------

		// No C++ objects here: the read is guarded, in case this isn't XMen2.exe at all.
		bool bytes_match(const DWORD rva, const std::uint8_t* expected, const std::size_t size)
		{
			__try
			{
				return std::memcmp(base + rva, expected, size) == 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}
	}

	void install(const HMODULE game)
	{
		base = reinterpret_cast<std::uint8_t*>(game);
		for (const auto* site : call_sites)
		{
			if (!bytes_match(site->rva, site->expected.data(), site->expected.size()))
			{
				logger::write("options: XMen2.exe doesn't have the expected code for %s (not the retail build?) - the Advanced Options panel is left as it is", site->what);
				return;
			}
		}
		for (const auto& function : game_functions)
		{
			if (!bytes_match(function.rva, function.expected.data(), function.expected.size()))
			{
				logger::write("options: XMen2.exe doesn't have the expected code for %s (not the retail build?) - the Advanced Options panel is left as it is", function.what);
				return;
			}
		}

		struct replacement
		{
			const call_site* site;
			const void* function;
		};
		const std::array<replacement, 4> replacements{{{&finish_panel_site, reinterpret_cast<const void*>(&finish_panel)},
		                                               {&save_site, reinterpret_cast<const void*>(&on_save)},
		                                               {&cancel_site, reinterpret_cast<const void*>(&on_cancel)},
		                                               {&revert_site, reinterpret_cast<const void*>(&on_revert)}}};
		std::array<DWORD, 4> protection{};
		for (std::size_t i = 0; i < replacements.size(); ++i)
		{
			if (!VirtualProtect(base + replacements[i].site->rva, 5, PAGE_EXECUTE_READWRITE, &protection[i]))
			{
				logger::write("options: can't unprotect %s (error %lu) - the Advanced Options panel is left as it is", replacements[i].site->what, GetLastError());
				for (std::size_t j = 0; j < i; ++j)
				{
					VirtualProtect(base + replacements[j].site->rva, 5, protection[j], &protection[j]);
				}
				return;
			}
		}
		for (std::size_t i = 0; i < replacements.size(); ++i)
		{
			std::uint8_t* at = base + replacements[i].site->rva;
			const std::int32_t rel = rel32(reinterpret_cast<std::uintptr_t>(at), reinterpret_cast<std::uintptr_t>(replacements[i].function));
			std::memcpy(at + 1, &rel, sizeof(rel));
			FlushInstructionCache(GetCurrentProcess(), at, 5);
			VirtualProtect(at, 5, protection[i], &protection[i]);
		}
		patched = true;
		logger::write("options: Advanced Options gets the rows Display mode, Frame rate, VSync and Run in background (four call sites patched); Accept writes the changed ones to [Display] in xml2-fix.ini");
	}

	bool installed()
	{
		return patched;
	}
}
