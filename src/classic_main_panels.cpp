#include "classic_main_panels.hpp"
#include "classic_main_panels_rules.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <cstring>

namespace classic_main_panels
{
	namespace
	{
		using namespace classic_main_panels_rules;
		using update_fn = void(__thiscall*)(void*);
		using lookup_fn = void*(__thiscall*)(void*, const char*);
		update_fn original = nullptr;
		using mouse_fn = bool(__thiscall*)(void*, int, int, int, int, int);
		mouse_fn original_mouse = nullptr;
		// What the last update applied: the rows' regions and label items, for the mouse hook.
		void* applied_menu = nullptr;
		rows applied_regions{};
		std::array<void*, 8> applied_items{};
		const auto lookup = reinterpret_cast<lookup_fn>(0x5adc10);
		bool apply(void* menu)
		{
			__try
			{
				if (*static_cast<std::uint32_t*>(menu) != 0x69f134) return false;
				rows panels{}, planned{};
				std::array<void*,8> text{};
				for (int i=0; i<8; ++i)
				{
					char name[] = "button1_back"; name[6] = char('1'+i);
					const auto panel = static_cast<const char*>(lookup(menu,name));
					name[7] = 0; text[i] = lookup(menu,name);
					if (!panel || !text[i]) return false;
					std::memcpy(&panels[i],panel+0x70,sizeof(rectangle));
				}
				if (!regions(panels,planned)) return false;
				std::array<std::array<std::int16_t,2>,8> offsets{};
				for (int i=0; i<8; ++i)
				{
					auto* item=static_cast<char*>(text[i]);
					rectangle before{}; std::array<std::int16_t,2> local{};
					std::memcpy(&before,item+0x70,sizeof before);
					std::memcpy(local.data(),item+0x78,sizeof local);
					// Keep the component's world origin fixed. Text centering reads
					// these local bounds too; changing only width shifts every label.
					if (!local_offsets(before,local,planned[i],offsets[i])) return false;
				}
				for (int i=0; i<8; ++i)
				{
					auto* item=static_cast<char*>(text[i]);
					std::memcpy(item+0x70,&planned[i],sizeof(rectangle));
					std::memcpy(item+0x78,offsets[i].data(),sizeof offsets[i]);
				}
				applied_regions=planned; applied_items=text; applied_menu=menu;
				return true;
			}
			__except(EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		void __fastcall update_hook(void* menu, void*)
		{
			original(menu);
			if (!apply(menu))
			{
				applied_menu = nullptr;
				logger::write_once("classic-panel-layout", "classic main panels: unsupported item layout - retail bounds retained");
			}
		}
		// MAIN_MENU's mouse (vt+0x80): a move or press inside the focused row is that row's
		// (classic_main_panels_rules.hpp, focused_row_keeps) - not the generic handler's to refocus.
		bool focused_row_answers(void* menu, const int x, const int y, const int message)
		{
			__try
			{
				if (menu != applied_menu || *static_cast<std::uint32_t*>(menu) != 0x69f134) return false;
				const void* focused = *reinterpret_cast<void* const*>(static_cast<char*>(menu) + 0x324); // 0x5ab0c0
				int row = -1;
				for (int i = 0; i < 8; ++i) if (applied_items[i] == focused) row = i;
				if (row < 0) return false;
				for (int i = 0; i < 8; ++i) // still the regions the update applied
					if (std::memcmp(static_cast<const char*>(applied_items[i]) + 0x70, &applied_regions[i], sizeof(rectangle)) != 0) return false;
				return focused_row_keeps(applied_regions, row, x, y, static_cast<unsigned>(message));
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		bool __fastcall mouse_hook(void* menu, void*, const int x, const int y, const int buttons, const int message, const int extra)
		{
			if (focused_row_answers(menu, x, y, message)) return false; // as the native handler answers a focus it keeps
			return original_mouse(menu, x, y, buttons, message, extra);
		}
	}
	void install(HMODULE game)
	{
		if (!ini::flag(L"Game",L"ClassicMainPanelBounds",false)) return;
		auto* slot = reinterpret_cast<update_fn*>(0x69f16c);
		if (reinterpret_cast<std::uintptr_t>(game)!=0x400000 || *slot!=reinterpret_cast<update_fn>(0x5c9640))
		{
			logger::write("classic main panels: MAIN_MENU update guard failed - retail bounds retained"); return;
		}
		DWORD protection=0;
		if (!VirtualProtect(slot,sizeof(*slot),PAGE_READWRITE,&protection))
		{
			logger::write("classic main panels: hook protection failed (error %lu) - retail bounds retained",GetLastError()); return;
		}
		original=*slot; *slot=reinterpret_cast<update_fn>(&update_hook);
		DWORD ignored=0;
		if (!VirtualProtect(slot,sizeof(*slot),protection,&ignored))
			logger::write("classic main panels: hook protection restore failed (error %lu)",GetLastError());
		logger::write("classic main panels: panel-width hit regions enabled, row seams split without overlap");
		// The mouse handler, so the generic handler can't refocus a neighbour inside the focused row.
		auto* mouse_slot = reinterpret_cast<mouse_fn*>(0x69f1b4);
		if (*mouse_slot!=reinterpret_cast<mouse_fn>(0x5c9320))
		{
			logger::write("classic main panels: MAIN_MENU mouse guard failed - the focus may flip inside a row's lower third"); return;
		}
		if (!VirtualProtect(mouse_slot,sizeof(*mouse_slot),PAGE_READWRITE,&protection))
		{
			logger::write("classic main panels: mouse hook protection failed (error %lu)",GetLastError()); return;
		}
		original_mouse=*mouse_slot; *mouse_slot=reinterpret_cast<mouse_fn>(&mouse_hook);
		if (!VirtualProtect(mouse_slot,sizeof(*mouse_slot),protection,&ignored))
			logger::write("classic main panels: mouse hook protection restore failed (error %lu)",GetLastError());
		logger::write("classic main panels: a move or press inside the focused row stays that row's");
	}
}
