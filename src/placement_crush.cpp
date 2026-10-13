#include "placement_crush.hpp"

#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "placement_crush_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace placement_crush
{
	namespace
	{
		using namespace placement_crush_rules;

		constexpr const char* as_before = "objects crush each other by the game's own rule";

		std::atomic<int> state{-1}; // 1 on; -1 no key or xml2; 0 the key is xml1 (or invalid) but nothing could be hooked
		std::atomic<unsigned> refused{0};
		address resume = touch_resume;

		int settle_depth = 0; // the game's own thread only (zone loads and play)

		// The settle event functor's call (was 0x4729f0: `mov ecx,[esp+4]; mov eax,[ecx]; push 3; call [eax+0x98]; ret 8`),
		// counted. __fastcall: the functor in ecx (unused, as in the game's), the entity and the second argument on the
		// stack, popped here (ret 8); the entity's answer handed back.
		using entity_settle = int(__thiscall*)(void*, int);
		// The count goes back down however the settle ends: __finally also runs when the entity's call unwinds (a
		// C++ exception of the game's own frames or a structured one), so an unwound settle cannot leave every later
		// touch refused for the session; the unwind itself continues to the game's own handlers, as before.
		int __fastcall settle_counted(void*, void*, void* entity, void*)
		{
			const auto* vtable = *static_cast<void* const* const*>(entity);
			const auto call = reinterpret_cast<entity_settle>(vtable[entity_settle_slot / 4]);
			int answer = 0;
			++settle_depth;
			__try
			{
				answer = call(entity, settle_mode);
			}
			__finally
			{
				--settle_depth;
			}
			return answer;
		}

		// From the stub at 0x498ef0, on the game's thread: true refuses this touch (the game's own early-out).
		bool __cdecl touch_refused(const void*, const void*)
		{
			if (!refuse(rule::xml1, settle_depth))
			{
				return false;
			}
			refused.fetch_add(1, std::memory_order_relaxed);
			return true;
		}

		__declspec(naked) void touch_stub()
		{
			__asm {
				pushfd
				pushad
				push dword ptr [esp + 0x28] // the other object (the slot's argument)
				push ecx                    // this object
				call touch_refused
				add esp, 8
				test al, al
				jnz refuse_touch
				popad
				popfd
				sub esp, 0x8c
				jmp dword ptr [resume]
			refuse_touch:
				popad
				popfd
				ret 4
			}
		}

		const guard* first_mismatch_guarded()
		{
			__try
			{
				return first_mismatch(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(limits_rules::image_base)));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &guards[0];
			}
		}
	}

	void install(const HMODULE game)
	{
		const auto narrow = ini::text(L"Game", L"PlacementCrush").value_or("");
		const auto chosen = parse_rule(narrow);
		if (!chosen.error.empty())
		{
			state = 0; // asked for something: unverified in the gameplay agreement
			logger::write("placement crush: [Game] PlacementCrush=%s %s - %s", narrow.c_str(), chosen.error.c_str(), as_before);
			return;
		}
		if (!chosen.set)
		{
			return; // no key: the game's own rule
		}
		if (chosen.value == rule::xml2)
		{
			logger::write("placement crush: XML2's own ([Game] PlacementCrush=xml2) - nothing patched");
			return;
		}
		state = 0;
		if (!game || reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base)
		{
			logger::write("placement crush: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("placement crush: 0x%08lX isn't the retail code (%s) - %s", static_cast<unsigned long>(g->va), g->what, as_before);
			return;
		}
		xml1_hook_transaction::patch site{touch_site, {retail_touch.begin(), retail_touch.end()}, {0xe9, 0, 0, 0, 0, 0x90}};
		const auto rel = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&touch_stub)) - (touch_site + 5);
		std::memcpy(site.after.data() + 1, &rel, 4);
		const xml1_hook_transaction::patch slot{settle_slot, xml1_hook_transaction::pointer_bytes(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(settle_call))),
		                                        xml1_hook_transaction::pointer_bytes(reinterpret_cast<const void*>(&settle_counted))};
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, {slot, site}, &safe))
		{
			state = 1;
			logger::write("placement crush: X-Men Legends 1's ([Game] PlacementCrush=xml1): while an object is settled on the floor (every object at a zone's load, and each one spawned in play 0.5 s after it appears) "
			              "an object touching another no longer crushes it - the settle event's call (0x%08lX) counted, the touch slot (0x%08lX) hooked; "
			              "other touches the game's own rule",
			              static_cast<unsigned long>(settle_slot), static_cast<unsigned long>(touch_site));
		}
		else if (safe)
		{
			logger::write("placement crush: the patches at 0x%08lX and 0x%08lX were refused (protection or readback) - %s", static_cast<unsigned long>(settle_slot),
			              static_cast<unsigned long>(touch_site), as_before);
		}
		else
		{
			game_version::refuse_startup();
			logger::write("placement crush: rollback/protection verification failed (0x%08lX, 0x%08lX); process startup refused", static_cast<unsigned long>(settle_slot),
			              static_cast<unsigned long>(touch_site));
		}
	}

	std::string status()
	{
		const int s = state.load();
		return std::string("placement crush ") + (s == 1 ? "xml1" : s == -1 ? "xml2" : "unavailable") + "; touches refused while settling " + std::to_string(refused.load());
	}

	bool requested()
	{
		return state.load() != -1;
	}

	bool installed()
	{
		return state.load() == 1;
	}
}
