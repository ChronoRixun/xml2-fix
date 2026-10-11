#include "classic_main_backdrop.hpp"
#include "classic_main_backdrop_rules.hpp"
#include "ini.hpp"
#include "log.hpp"

#include <cstring>

namespace classic_main_backdrop
{
	namespace
	{
		using namespace classic_main_backdrop_rules;
		using configure_fn = void(__thiscall*)(void*, void*);
		using append_fn = void(__thiscall*)(void*, void*);
		configure_fn original = nullptr;
		append_fn append = nullptr;
		address sg_base = 0;
		unsigned applied = 0;
		constexpr std::string_view scene = "menu/main_back";
		constexpr std::array<std::string_view, 5> models{
			"menu_back.png", "cerebro_console.png", "Cerebro_Plank.png", "Cerebro_Platform.png", "subb_std3"};

		struct memory
		{
			bool read(address where, void* out, std::size_t size)
			{
				if (where < 0x10000 || size > 1024 || where > 0x7fffffff - size) return false;
				__try { std::memcpy(out, reinterpret_cast<const void*>(where), size); return true; }
				__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
			}
		};

		bool adjust(void* self)
		{
			memory m;
			const auto shader = static_cast<address>(reinterpret_cast<std::uintptr_t>(self));
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
		const auto site = reinterpret_cast<std::uint8_t*>(GetProcAddress(sg,
			"?configureScale@igDx8EnvironmentMapShader@Sg@Gap@@IAEXPAVigCapabilityManager@Gfx@3@@Z"));
		append = reinterpret_cast<append_fn>(GetProcAddress(core, "?append@igObjectList@Core@Gap@@QAEXPAVigObject@23@@Z"));
		std::array<std::uint8_t, prologue.size()> bytes{};
		memory m;
		if (!sg || !core || reinterpret_cast<std::uintptr_t>(site) != sg_base + configure_rva ||
		    reinterpret_cast<std::uintptr_t>(append) != core_base + append_rva ||
		    !m.read(sg_base + configure_rva, bytes.data(), bytes.size()) || bytes != prologue)
		{
			logger::write("classic main backdrop: engine export/prologue guard failed - unavailable, retail passes retained");
			return;
		}
		auto* bridge = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, displaced + 5, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!bridge) return;
		std::memcpy(bridge, site, displaced);
		const auto bridge_address = static_cast<address>(reinterpret_cast<std::uintptr_t>(bridge));
		jump(bridge + displaced, bridge_address + displaced, sg_base + configure_rva + displaced);
		DWORD old = 0;
		if (!VirtualProtect(bridge, displaced + 5, PAGE_EXECUTE_READ, &old))
		{
			VirtualFree(bridge, 0, MEM_RELEASE);
			return;
		}
		std::array<std::uint8_t, displaced> patch{};
		patch.fill(0x90);
		jump(patch.data(), sg_base + configure_rva, static_cast<address>(reinterpret_cast<std::uintptr_t>(&configure_hook)));
		if (!VirtualProtect(site, displaced, PAGE_EXECUTE_READWRITE, &old))
		{
			VirtualFree(bridge, 0, MEM_RELEASE);
			return;
		}
		original = reinterpret_cast<configure_fn>(bridge);
		std::memcpy(site, patch.data(), patch.size());
		DWORD ignored = 0;
		VirtualProtect(site, displaced, old, &ignored);
		FlushInstructionCache(GetCurrentProcess(), bridge, displaced + 5);
		FlushInstructionCache(GetCurrentProcess(), site, displaced);
		logger::write("classic main backdrop: guarded room-only diffuse-pass hook installed");
	}
}
