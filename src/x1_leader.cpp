#include "x1_leader.hpp"

#include "ini.hpp"
#include "log.hpp"
#include "x1_leader_rules.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

namespace x1_leader
{
	namespace
	{
		using namespace x1_leader_rules;

		constexpr const char* as_before = "leaders behave like any other actor";

		// What the bridges call: the patched test with the loaded sidecar. Leaked on purpose: these
		// live as long as the process (the DLL is never unloaded before ExitProcess) and their teardown
		// order against the CRT's statics is not worth owning.
		leader_names& names_storage()
		{
			static auto* leaked = new leader_names();
			return *leaked;
		}
		leader_names& names = names_storage();
		std::optional<leader_names>& testing_override_storage()
		{
			static auto* leaked = new std::optional<leader_names>();
			return *leaked;
		}
		std::optional<leader_names>& testing_override = testing_override_storage();

		bool wanted = false, applied = false;

		std::uint8_t* game_image()
		{
			return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(image_base));
		}

		void* in_game(const address va)
		{
			return reinterpret_cast<void*>(static_cast<std::uintptr_t>(va));
		}

		const guard* first_mismatch_guarded()
		{
			__try
			{
				for (const auto& g : guards)
				{
					const auto* at = game_image() + (g.va - image_base);
					if (!limits_rules::matches(at, g.hex))
					{
						return &g;
					}
				}
				return nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &guards[0];
			}
		}

		// The callback behind both bridges (cdecl: the bridge pushed the actor).
		std::uint32_t __cdecl decide_bridge(const void* actor)
		{
			return decide(actor, names) ? 1u : 0u;
		}

		address address_of(const void* pointer)
		{
			return static_cast<address>(reinterpret_cast<std::uintptr_t>(pointer));
		}

		struct write
		{
			address va;
			std::vector<std::uint8_t> bytes;
		};

		// Every write or none: each site is made writable first, so a refusal leaves the game as it
		// was; then every write; then each site's protection goes back.
		bool patch(const std::vector<write>& writes)
		{
			std::vector<DWORD> protection(writes.size());
			for (std::size_t i = 0; i < writes.size(); ++i)
			{
				if (!VirtualProtect(in_game(writes[i].va), writes[i].bytes.size(), PAGE_EXECUTE_READWRITE, &protection[i]))
				{
					logger::write("x1 leader: ERROR: can't unprotect XMen2.exe at 0x%08lX (error %lu) - %s",
					              static_cast<unsigned long>(writes[i].va), GetLastError(), as_before);
					for (std::size_t j = i; j-- > 0;)
					{
						VirtualProtect(in_game(writes[j].va), writes[j].bytes.size(), protection[j], &protection[j]);
					}
					return false;
				}
			}
			for (const auto& w : writes)
			{
				std::memcpy(in_game(w.va), w.bytes.data(), w.bytes.size());
				FlushInstructionCache(GetCurrentProcess(), in_game(w.va), w.bytes.size());
			}
			for (std::size_t i = writes.size(); i-- > 0;)
			{
				VirtualProtect(in_game(writes[i].va), writes[i].bytes.size(), protection[i], &protection[i]);
			}
			return true;
		}

		bool load_sidecar()
		{
			if (testing_override)
			{
				names = *testing_override;
				return true;
			}
			const auto path = logger::module_dir() / L"Data" / L"x1leader.txt";
			const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
			                                FILE_ATTRIBUTE_NORMAL, nullptr);
			if (file == INVALID_HANDLE_VALUE)
			{
				return false;
			}
			LARGE_INTEGER size{};
			std::string text;
			if (GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart < 1 << 20)
			{
				text.resize(static_cast<std::size_t>(size.QuadPart));
				DWORD read = 0;
				if (!ReadFile(file, text.data(), static_cast<DWORD>(text.size()), &read, nullptr) || read != text.size())
				{
					text.clear();
				}
			}
			CloseHandle(file);
			names = leader_names::parse(text);
			return true;
		}
	}

	bool installed() { return wanted && applied; }
	bool verified() { return !wanted || applied; }

	void give_for_testing(const char* sidecar_text)
	{
		testing_override = leader_names::parse(sidecar_text ? sidecar_text : "");
	}

	void install(const HMODULE game)
	{
		if (applied)
		{
			return; // immutable successfully installed rule, never reread a changed INI
		}
		wanted = ini::flag(L"Game", L"X1Leader", false);
		if (!wanted)
		{
			logger::write("x1 leader: off ([Game] X1Leader) - %s", as_before);
			return;
		}
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("x1 leader: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("x1 leader: 0x%08lX isn't the retail code (%s) - %s", static_cast<unsigned long>(g->va), g->what, as_before);
			return;
		}
		if (!load_sidecar())
		{
			logger::write("x1 leader: Data\\x1leader.txt missing or unreadable - %s", as_before);
			return;
		}

		// Both bridges go in one block of the fix's own, made executable once it is written.
		auto pain = bridge(0x57, address_of(&decide_bridge));   // push edi: the attack's owner
		auto drop = bridge(0x56, address_of(&decide_bridge));   // push esi: the corpse
		const auto pain_size = pain.size();
		auto* block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, pain.size() + drop.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!block)
		{
			logger::write("x1 leader: ERROR: no memory for the bridges (error %lu) - %s", GetLastError(), as_before);
			return;
		}
		const auto pain_va = address_of(block);
		const auto drop_va = pain_va + pain_size;
		// The final jmp of each bridge: rel32 right after the e9 (the last 4 bytes) -> the kept branch.
		const auto jmp_at = pain_size - 4;
		const auto jmp_rel = static_cast<std::uint32_t>(pain_resume_va - (pain_va + pain_size));
		std::memcpy(&pain[static_cast<std::size_t>(jmp_at)], &jmp_rel, 4);
		const auto jmp_at2 = drop.size() - 4;
		const auto jmp_rel2 = static_cast<std::uint32_t>(drop_resume_va - (drop_va + drop.size()));
		std::memcpy(&drop[static_cast<std::size_t>(jmp_at2)], &jmp_rel2, 4);
		std::memcpy(block, pain.data(), pain.size());
		std::memcpy(block + pain_size, drop.data(), drop.size());
		DWORD old = 0;
		if (!VirtualProtect(block, pain.size() + drop.size(), PAGE_EXECUTE_READ, &old))
		{
			logger::write("x1 leader: ERROR: can't make the bridges executable (error %lu) - %s", GetLastError(), as_before);
			VirtualFree(block, 0, MEM_RELEASE);
			return;
		}
		FlushInstructionCache(GetCurrentProcess(), block, pain.size() + drop.size());

		const std::vector<write> writes{
		    {pain_site_va, call_bridge(pain_site_va, pain_va)},
		    {drop_site_va, call_bridge(drop_site_va, drop_va)},
		};
		if (!patch(writes))
		{
			VirtualFree(block, 0, MEM_RELEASE);
			return;
		}
		applied = true;
		logger::write("x1 leader: on - %llu sidecar name(s): actors in Data\\x1leader.txt lead like a native "
		              "x3-variant actor (deaths always roll drops with the forced item slots; heroes don't flinch "
		              "vs their projectiles); the pain gate (0x%08lX) and the drop roller (0x%08lX) hooked, every "
		              "native byte the rest of the game reads unchanged",
		              static_cast<unsigned long long>(names.size()), static_cast<unsigned long>(pain_site_va),
		              static_cast<unsigned long>(drop_site_va));
	}
}
