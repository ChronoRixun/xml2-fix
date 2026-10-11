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
				return true;
			}
			__except(EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		void __fastcall update_hook(void* menu, void*)
		{
			original(menu);
			if (!apply(menu)) logger::write_once("classic-panel-layout", "classic main panels: unsupported item layout - retail bounds retained");
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
	}
}
