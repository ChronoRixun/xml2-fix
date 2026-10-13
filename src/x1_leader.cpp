#include "x1_leader.hpp"

#include "ini.hpp"
#include "log.hpp"
#include "game_version.hpp"
#include "x1_leader_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <initializer_list>
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

		// The pain gate's callback (cdecl: the bridge pushed the attack's owner).
		std::uint32_t __cdecl decide_bridge(const void* actor)
		{
			return decide(actor, names) ? 1u : 0u;
		}

		// Set by the drop roller's entry for every corpse it resolves (game thread; the roller does not
		// recurse): this roll is a listed actor's, so the count and forced-pick bridges apply the first
		// game's rule. A roll without a corpse never reaches them (its leader flag stays clear).
		address address_of(const void* pointer);
		volatile std::uint8_t x1_roll = 0;

		std::uint32_t __cdecl drop_classify(const void* corpse)
		{
			const auto path = classify(corpse, names);
			x1_roll = path == drop_path::x1 ? 1 : 0;
			return drop_target(path);
		}

		// The forced pick from drop #2 on (XBE 0x713e4-0x71444), on XML2's services: the roller's own
		// manager pair, the game's random integer, the drop manager's name lookup and descriptor.
		std::uint32_t __cdecl forced_pick(void* roller)
		{
			if (!x1_roll || !roller)
			{
				return 0;
			}
			using count_fn = int(__thiscall*)(void*);
			using limit_fn = int(__thiscall*)(void*, int);
			using random_fn = int(__cdecl*)(int, int, char);
			using manager_fn = void*(__cdecl*)();
			using lookup_fn = std::uint32_t(__thiscall*)(void*, const char*);
			using descriptor_fn = void*(__thiscall*)(void*, void*, std::uint32_t);
			const auto* vt = *static_cast<std::uintptr_t* const*>(roller);
			const int current = reinterpret_cast<count_fn>(vt[0x194 / 4])(roller);
			const int maximum = reinterpret_cast<limit_fn>(vt[0x19c / 4])(roller, -1);
			const int roll = reinterpret_cast<random_fn>(random_int_fn)(0, forced_range(current, maximum), 1);
			const char* name = *reinterpret_cast<const char* const*>(item_names_va + 4u * static_cast<address>(forced_slot(roll)));
			void* manager = reinterpret_cast<manager_fn>(drop_manager_fn)();
			if (!manager || !name)
			{
				return 0;
			}
			const auto* mvt = *static_cast<std::uintptr_t* const*>(manager);
			const auto id = reinterpret_cast<lookup_fn>(mvt[0x78 / 4])(manager, name);
			static std::uint32_t descriptor[4]; // copied out at 0x46e7ef straight away (game thread)
			return address_of(reinterpret_cast<descriptor_fn>(mvt[0x12c / 4])(manager, descriptor, id));
		}

		address address_of(const void* pointer)
		{
			return static_cast<address>(reinterpret_cast<std::uintptr_t>(pointer));
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

		// The four bridges go in one block of the fix's own, made executable once it is written.
		auto* block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 256, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!block)
		{
			logger::write("x1 leader: ERROR: no memory for the bridges (error %lu) - %s", GetLastError(), as_before);
			return;
		}
		const auto base = address_of(block);
		auto pain = bridge(0x57, address_of(reinterpret_cast<const void*>(&decide_bridge))); // push edi: the attack's owner
		const auto pain_va = base;
		const auto pain_jmp = static_cast<std::uint32_t>(pain_resume_va - (pain_va + pain.size()));
		std::memcpy(&pain[pain.size() - 4], &pain_jmp, 4);
		const auto classify_va = pain_va + static_cast<address>(pain.size());
		const auto classify = classify_bridge(address_of(reinterpret_cast<const void*>(&drop_classify)));
		const auto count_va = classify_va + static_cast<address>(classify.size());
		const auto count = count_bridge(count_va, address_of(const_cast<const std::uint8_t*>(&x1_roll)));
		const auto pick_va = count_va + static_cast<address>(count.size());
		const auto pick = pick_bridge(pick_va, address_of(reinterpret_cast<const void*>(&forced_pick)));
		std::size_t used = 0;
		for (const auto* part : std::initializer_list<const std::vector<std::uint8_t>*>{&pain, &classify, &count, &pick})
		{
			std::memcpy(block + used, part->data(), part->size());
			used += part->size();
		}
		DWORD old = 0;
		if (used > 256 || !VirtualProtect(block, 256, PAGE_EXECUTE_READ, &old))
		{
			logger::write("x1 leader: ERROR: can't make the bridges executable (error %lu) - %s", GetLastError(), as_before);
			VirtualFree(block, 0, MEM_RELEASE);
			return;
		}
		FlushInstructionCache(GetCurrentProcess(), block, used);

		// All four sites or none, through the shared transaction (retail bytes re-read, every page made
		// writable first, written, read back, rolled back on any failure).
		const auto retail_bytes = [](const address va, const std::size_t n) {
			std::vector<std::uint8_t> bytes(n);
			std::memcpy(bytes.data(), in_game(va), bytes.size());
			return bytes;
		};
		const std::vector<xml1_hook_transaction::patch> writes{
		    {pain_site_va, retail_bytes(pain_site_va, 7), jmp_bridge(pain_site_va, pain_va)},
		    {drop_site_va, retail_bytes(drop_site_va, 7), jmp_to(drop_site_va, classify_va, 7)},
		    {count_site_va, retail_bytes(count_site_va, 6), jmp_to(count_site_va, count_va, 6)},
		    {pick_site_va, retail_bytes(pick_site_va, 8), jmp_to(pick_site_va, pick_va, 8)},
		};
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (!xml1_hook_transaction::apply(memory, writes, &safe))
		{
			if (!safe)
			{
				game_version::refuse_startup();
				logger::write("x1 leader: ERROR: rollback/protection verification failed; process startup refused");
				return; // the bridges stay allocated: a site may still jump into them
			}
			logger::write("x1 leader: ERROR: the four sites could not be written and read back - %s", as_before);
			VirtualFree(block, 0, MEM_RELEASE);
			return;
		}
		applied = true;
		logger::write("x1 leader: on - %llu sidecar name(s): heroes don't flinch vs their projectiles (pain gate 0x%08lX); "
		              "their deaths follow X-Men Legends 1's drop rule (no probability roll, 4 drops, from the second on "
		              "HEALTH_ITEM / ENERGY_ITEM / XTREME_PIP; roller 0x%08lX/0x%08lX/0x%08lX); XML2's x3 variants keep "
		              "XML2's rule",
		              static_cast<unsigned long long>(names.size()), static_cast<unsigned long>(pain_site_va),
		              static_cast<unsigned long>(drop_site_va), static_cast<unsigned long>(count_site_va),
		              static_cast<unsigned long>(pick_site_va));
	}
}
