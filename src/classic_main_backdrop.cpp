#include "classic_main_backdrop.hpp"
#include "classic_main_backdrop_rules.hpp"
#include "ini.hpp"
#include "log.hpp"

#include <cstring>
#include <mutex>

namespace classic_main_backdrop
{
	namespace
	{
		using namespace classic_main_backdrop_rules;
		using configure_fn = void(__thiscall*)(void*, void*);
		using append_fn = void(__thiscall*)(void*, void*);
		using load_fn = void*(__thiscall*)(void*, const char*);
		using ref_fn = void(__thiscall*)(void*);
		configure_fn original = nullptr;
		load_fn original_load = nullptr;
		ref_fn retain = nullptr, release = nullptr;
		using teardown_fn = void(__cdecl*)();
		teardown_fn original_teardown = nullptr;
		std::mutex room_mutex;
		// Engine reference pins the room's objects; replaced on the next room load and dropped when the
		// main loop ends, before the game's teardown and igReleaseAlchemy.
		room_pin room;
		append_fn append = nullptr;
		address sg_base = 0;
		address entry_vtable = 0;
		unsigned applied = 0;
		constexpr std::string_view scene = "menu/main_back";
		constexpr std::array<std::string_view, 5> models{
			"menu_back.png", "cerebro_console.png", "Cerebro_Plank.png", "Cerebro_Platform.png", "subb_std3"};

		struct memory
		{
			bool read(address where, void* out, std::size_t size)
			{
				// the whole user space of a large-address-aware exe (E12: objects above 2 GB are valid); the read is guarded
				if (where < 0x10000 || size > 1024 || where > 0xFFFEFFFFu - size) return false;
				__try { std::memcpy(out, reinterpret_cast<const void*>(where), size); return true; }
				__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
			}
		};

		bool room_path(const char* path)
		{
			if (!path) return false;
			std::array<char,512> normalized{};
			std::size_t length=0;
			for (; length<normalized.size() && path[length]; ++length)
			{
				char ch=path[length]; if(ch>='A'&&ch<='Z') ch=char(ch-'A'+'a');
				normalized[length]=ch=='\\' ? '/' : ch;
			}
			if (length==normalized.size()) return false;
			const std::string_view name(normalized.data(),length);
			constexpr std::string_view target="maps/menu/main_back.igb";
			return name==target || (name.size()>target.size() && name.ends_with(target) &&
			                       name[name.size()-target.size()-1]=='/');
		}

		void* __fastcall load_hook(void* resource, void*, const char* path)
		{
			void* directory=original_load(resource,path);
			if (directory && room_path(path))
			{
				room_pin::step step;
				{
					std::lock_guard lock(room_mutex);
					step=room.capture(static_cast<address>(reinterpret_cast<std::uintptr_t>(directory)));
					if (step.retain)
					{
						retain(directory);
						logger::write("classic main backdrop: room asset directory captured (%p)",directory);
					}
				}
				if (step.release) release(reinterpret_cast<void*>(static_cast<std::uintptr_t>(step.release)));
			}
			return directory;
		}

		// In place of WinMain's first teardown call: drop the room pin while every engine object is
		// still alive, so the game's own scene release destroys the room shaders as in retail.
		void __cdecl teardown_hook()
		{
			address last=0;
			{
				std::lock_guard lock(room_mutex);
				last=room.close();
			}
			if (last)
			{
				release(reinterpret_cast<void*>(static_cast<std::uintptr_t>(last)));
				logger::write("classic main backdrop: room asset directory released at game shutdown");
			}
			original_teardown();
		}

		bool adjust(void* self)
		{
			memory m;
			const auto shader = static_cast<address>(reinterpret_cast<std::uintptr_t>(self));
			std::lock_guard lock(room_mutex);
			const auto directory=room.held;
			if (!directory || !owned_by_directory(m,directory,shader,entry_vtable)) return false;
			address table = 0, name_pointer = 0, lighting = 0;
			float reflection = 0;
			std::uint8_t transparent = 1;
			char current[32]{}, label[256]{};
			if (!word(m, shader, table) || table != sg_base + shader_vtable_rva ||
			    !m.read(zone_buffer, current, sizeof current) || !std::memchr(current, 0, sizeof current) ||
			    !word(m, shader + 8, name_pointer) || !m.read(name_pointer, label, sizeof label) ||
			    !std::memchr(label, 0, sizeof label) || !m.read(shader + 0x2c, &reflection, sizeof reflection) ||
			    !m.read(shader + 0x30, &transparent, sizeof transparent) ||
			    !in_scene(current, scene, label, models, reflection, transparent == 0) ||
			    !word(m, sg_base + disable_lighting_rva, lighting)) return false;
			list_plan lists;
			if (!plan(m, shader, lighting, lists))
			{
				logger::write_once("classic-main-layout", "classic main backdrop: unsupported shader pass layout - retail passes retained");
				return false;
			}
			// Both lists were validated before either append. The engine retains/releases the
			// attribute and grows its own list; the original reflection pass stays untouched.
			if (lists.add_begin) append(reinterpret_cast<void*>(lists.begin), reinterpret_cast<void*>(lighting));
			if (lists.add_end) append(reinterpret_cast<void*>(lists.end), reinterpret_cast<void*>(lighting));
			if (lists.add_begin || lists.add_end)
			{
				++applied;
				logger::write("classic main backdrop: diffuse pass unlit for room shader %u (reflection %.7g preserved)", applied, reflection);
			}
			return true;
		}

		void __fastcall configure_hook(void* self, void*, void* capabilities)
		{
			original(self, capabilities);
			adjust(self);
		}

		void jump(std::uint8_t* buffer, address from, address target)
		{
			buffer[0] = 0xe9;
			const auto relative = target - from - 5;
			std::memcpy(buffer + 1, &relative, sizeof relative);
		}
	}

	void install(HMODULE game)
	{
		if (!ini::flag(L"Game", L"ClassicMainBackdrop", false)) return;
		if (reinterpret_cast<std::uintptr_t>(game) != 0x400000)
		{
			logger::write("classic main backdrop: non-retail executable base - unavailable");
			return;
		}
		const auto sg = GetModuleHandleW(L"libIGSg.dll");
		const auto core = GetModuleHandleW(L"libIGCore.dll");
		sg_base = static_cast<address>(reinterpret_cast<std::uintptr_t>(sg));
		const auto core_base = static_cast<address>(reinterpret_cast<std::uintptr_t>(core));
		entry_vtable=core_base+0x7e6b0;
		const auto load_site=reinterpret_cast<std::uint8_t*>(GetProcAddress(core,"?load@igResource@Core@Gap@@QAEPAVigDirectory@23@QBD@Z"));
		retain=reinterpret_cast<ref_fn>(GetProcAddress(core,"?ref@igObject@Core@Gap@@QBEXXZ"));
		release=reinterpret_cast<ref_fn>(GetProcAddress(core,"?release@igObject@Core@Gap@@QBEXXZ"));
		const auto site = reinterpret_cast<std::uint8_t*>(GetProcAddress(sg,
			"?configureScale@igDx8EnvironmentMapShader@Sg@Gap@@IAEXPAVigCapabilityManager@Gfx@3@@Z"));
		append = reinterpret_cast<append_fn>(GetProcAddress(core, "?append@igObjectList@Core@Gap@@QAEXPAVigObject@23@@Z"));
		std::array<std::uint8_t, prologue.size()> bytes{};
		memory m;
		constexpr std::array<std::uint8_t,11> load_prologue{0x53,0x8b,0x5c,0x24,0x08,0x56,0x57,0x8d,0x44,0x24,0x10};
		std::array<std::uint8_t,11> load_bytes{};
		std::array<std::uint8_t,4> ref_bytes{},release_bytes{};
		std::array<std::uint8_t,loop_exit_bytes.size()> exit_bytes{};
		if (reinterpret_cast<std::uintptr_t>(load_site)!=core_base+0x4ca90 ||
		    reinterpret_cast<std::uintptr_t>(retain)!=core_base+0x1be0 ||
		    reinterpret_cast<std::uintptr_t>(release)!=core_base+0x1b30 ||
		    reinterpret_cast<std::uintptr_t>(GetProcAddress(core,"??_7igObjectDirEntry@Core@Gap@@6B@"))!=entry_vtable ||
		    !m.read(core_base+0x4ca90,load_bytes.data(),load_bytes.size()) || load_bytes!=load_prologue ||
		    !m.read(core_base+0x1be0,ref_bytes.data(),4) || ref_bytes!=std::array<std::uint8_t,4>{0xff,0x41,0x04,0xc3} ||
		    !m.read(core_base+0x1b30,release_bytes.data(),4) || release_bytes!=std::array<std::uint8_t,4>{0x8b,0x51,0x04,0x4a} ||
		    !m.read(loop_exit_site,exit_bytes.data(),exit_bytes.size()) || exit_bytes!=loop_exit_bytes)
		{
			logger::write("classic main backdrop: room ownership export/prologue guard failed - retail passes retained"); return;
		}
		// The pin's release at shutdown goes in before anything can be pinned: without it the room
		// would outlive igReleaseAlchemy and crash libIGSg at exit, so a failure here keeps retail passes.
		{
			auto* const call_site=reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(loop_exit_call));
			DWORD exit_old=0;
			if (!VirtualProtect(call_site,5,PAGE_EXECUTE_READWRITE,&exit_old))
			{
				logger::write("classic main backdrop: shutdown hook protection failed (error %lu) - retail passes retained",GetLastError()); return;
			}
			original_teardown=reinterpret_cast<teardown_fn>(static_cast<std::uintptr_t>(loop_exit_target));
			const auto call=call_to(loop_exit_call,static_cast<address>(reinterpret_cast<std::uintptr_t>(&teardown_hook)));
			std::memcpy(call_site,call.data(),call.size());
			DWORD exit_ignored=0;
			if (!VirtualProtect(call_site,5,exit_old,&exit_ignored))
				logger::write("classic main backdrop: shutdown hook protection restore failed (error %lu)",GetLastError());
			FlushInstructionCache(GetCurrentProcess(),call_site,5);
		}
		if (!sg || !core || reinterpret_cast<std::uintptr_t>(site) != sg_base + configure_rva ||
		    reinterpret_cast<std::uintptr_t>(append) != core_base + append_rva ||
		    !m.read(sg_base + configure_rva, bytes.data(), bytes.size()) || bytes != prologue)
		{
			logger::write("classic main backdrop: engine export/prologue guard failed - unavailable, retail passes retained");
			return;
		}
		// Pin the returned room asset's own directory. A fresh membership scan on
		// every configure rejects matching labels from any other loaded asset.
		auto* load_bridge=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,10,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
		if (!load_bridge)
		{
			logger::write("classic main backdrop: ownership bridge allocation failed (error %lu) - retail passes retained",GetLastError()); return;
		}
		std::memcpy(load_bridge,load_site,5);
		const auto load_address=static_cast<address>(reinterpret_cast<std::uintptr_t>(load_bridge));
		jump(load_bridge+5,load_address+5,core_base+0x4ca95);
		DWORD load_old=0;
		if (!VirtualProtect(load_bridge,10,PAGE_EXECUTE_READ,&load_old))
		{
			logger::write("classic main backdrop: ownership bridge protection failed (error %lu) - retail passes retained",GetLastError());
			VirtualFree(load_bridge,0,MEM_RELEASE); return;
		}
		if (!VirtualProtect(load_site,5,PAGE_EXECUTE_READWRITE,&load_old))
		{
			logger::write("classic main backdrop: ownership hook protection failed (error %lu) - retail passes retained",GetLastError());
			VirtualFree(load_bridge,0,MEM_RELEASE); return;
		}
		original_load=reinterpret_cast<load_fn>(load_bridge);
		std::array<std::uint8_t,5> load_patch{};
		jump(load_patch.data(),core_base+0x4ca90,static_cast<address>(reinterpret_cast<std::uintptr_t>(&load_hook)));
		std::memcpy(load_site,load_patch.data(),5);
		DWORD load_ignored=0;
		if (!VirtualProtect(load_site,5,load_old,&load_ignored))
			logger::write("classic main backdrop: ownership hook protection restore failed (error %lu)",GetLastError());
		FlushInstructionCache(GetCurrentProcess(),load_bridge,10);
		FlushInstructionCache(GetCurrentProcess(),load_site,5);
		auto* bridge = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, displaced + 5, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!bridge)
		{
			logger::write("classic main backdrop: bridge allocation failed (error %lu) - retail passes retained",GetLastError()); return;
		}
		std::memcpy(bridge, site, displaced);
		const auto bridge_address = static_cast<address>(reinterpret_cast<std::uintptr_t>(bridge));
		jump(bridge + displaced, bridge_address + displaced, sg_base + configure_rva + displaced);
		DWORD old = 0;
		if (!VirtualProtect(bridge, displaced + 5, PAGE_EXECUTE_READ, &old))
		{
			logger::write("classic main backdrop: bridge protection failed (error %lu) - retail passes retained",GetLastError());
			VirtualFree(bridge, 0, MEM_RELEASE);
			return;
		}
		std::array<std::uint8_t, displaced> patch{};
		patch.fill(0x90);
		jump(patch.data(), sg_base + configure_rva, static_cast<address>(reinterpret_cast<std::uintptr_t>(&configure_hook)));
		if (!VirtualProtect(site, displaced, PAGE_EXECUTE_READWRITE, &old))
		{
			logger::write("classic main backdrop: hook protection failed (error %lu) - retail passes retained",GetLastError());
			VirtualFree(bridge, 0, MEM_RELEASE);
			return;
		}
		original = reinterpret_cast<configure_fn>(bridge);
		std::memcpy(site, patch.data(), patch.size());
		DWORD ignored = 0;
		if (!VirtualProtect(site, displaced, old, &ignored))
			logger::write("classic main backdrop: hook protection restore failed (error %lu)",GetLastError());
		FlushInstructionCache(GetCurrentProcess(), bridge, displaced + 5);
		FlushInstructionCache(GetCurrentProcess(), site, displaced);
		logger::write("classic main backdrop: guarded asset-owned room-only diffuse-pass hook installed (room released at shutdown)");
	}
}
