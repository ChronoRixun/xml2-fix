#include "break_rule.hpp"

#include "break_rule_rules.hpp"
#include "ini.hpp"
#include "log.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>

namespace break_rule
{
	namespace
	{
		using namespace break_rule_rules;

		constexpr const char* as_before = "objects break by the game's own rule";

		// Everything below runs on the game's own thread (its object spawning and its combat).
		structure_store structures;
		attack_ring attacks;
		bool log_hits = false; // [Debug] LogBreakRule

		// What status() reports.
		std::atomic<unsigned> judged{0};
		std::atomic<unsigned> refused{0};
		std::atomic<unsigned> unknown{0};
		std::atomic<int> state{-1}; // 1 on; -1 no key or xml2; 0 the key is xml1 but nothing could be hooked

		template <typename T>
		T at(const void* base, const address offset)
		{
			T value{};
			std::memcpy(&value, static_cast<const std::uint8_t*>(base) + offset, sizeof value);
			return value;
		}

		// The game's level function, 0x44f770: `this` unused, the actor and the hit pushed, popped by the callee
		// (ret 8) - as a __fastcall with two register arguments it never reads.
		using level_function = int(__fastcall*)(void*, void*, void*, void*);
		level_function game_level = nullptr;

		// Slot 0x34 of the combat object's vtable once patched: the game's own result, unchanged, and XML1's level
		// of this attack noted on the way.
		int __fastcall melee_level(void* combat, void* edx, void* actor, void* hit)
		{
			const int authored = at<std::uint8_t>(hit, hit_level_byte);
			const int result = game_level(combat, edx, actor, hit); // also brings the actor's affecter sums up to date
			const int might = (at<std::uint8_t>(actor, actor_lift) >> 3) & 15;
			const int level = xml1_level(authored, might, at<float>(actor, actor_affecter));
			attacks.record(at<std::uint32_t>(actor, entity_handle), at<std::uint32_t>(hit, hit_attack), level);
			if (log_hits)
			{
				logger::write("break rule: attacker %08X attack %08X: authored level %d, Might %d, damageLevel %d -> XML1 level %d (the game's: %d)",
				              at<std::uint32_t>(actor, entity_handle), at<std::uint32_t>(hit, hit_attack), authored, might,
				              whole(at<float>(actor, actor_affecter)), level, result & 0xff);
			}
			return result;
		}

		// The entity reader's number getter (vtable slot 0x18): `this` in ecx, the attribute's name and where the
		// number goes pushed, popped by the callee. It writes 16 bits, and nothing when the attribute isn't there.
		using read_number = void(__fastcall*)(void*, void*, const char*, std::int16_t*);

		// From the bridge at 0x498970: the object has just read its `structure`.
		void __cdecl object_parsed(void* object, void* reader)
		{
			std::int16_t xml1 = -1;
			const auto* vtable = at<const std::uint8_t*>(reader, 0);
			at<read_number>(vtable, reader_number)(reader, nullptr, structure_attribute, &xml1);
			structures.set(at<std::uint32_t>(object, entity_handle), xml1 < 0 ? std::nullopt : std::optional<int>(xml1));
			if (log_hits && xml1 >= 0)
			{
				logger::write("break rule: object %08X: %s %d, structure byte to be %d", at<std::uint32_t>(object, entity_handle), structure_attribute, xml1,
				              engine_structure(xml1));
			}
		}

		// From the bridge at 0x49805b: the game is about to let `hit` damage `object`. Zero refuses it. An
		// integer, not a bool: the bridge tests the whole of eax.
		std::uint32_t __cdecl hit_allowed(const void* object, const void* hit)
		{
			const auto handle = at<std::uint32_t>(object, entity_handle);
			const int engine = at<std::uint8_t>(object, entity_structure);
			const auto attacker = at<std::uint32_t>(hit, hit_attacker);
			const auto attack = at<std::uint32_t>(hit, hit_attack);
			const int level = hit_level(attacks, attacker, attack, at<std::uint8_t>(hit, hit_level_byte));
			const auto xml1 = structures.get(handle);
			const auto result = decide(xml1, engine, level);
			judged.fetch_add(1, std::memory_order_relaxed);
			if (result == verdict::refused)
			{
				refused.fetch_add(1, std::memory_order_relaxed);
			}
			else if (result == verdict::no_structure)
			{
				unknown.fetch_add(1, std::memory_order_relaxed);
			}
			if (log_hits)
			{
				logger::write("break rule: object %08X (structure byte %d, %s %d) hit by %08X attack %08X, XML1 level %d (%s): %s", handle, engine,
				              structure_attribute, xml1.value_or(-1), attacker, attack, level, attacks.find(attacker, attack) ? "melee" : "the hit's own",
				              result == verdict::refused ? "refused" : result == verdict::allowed ? "allowed" : "no XML1 structure, the game's decision");
			}
			return result == verdict::refused ? 0 : 1;
		}

		std::uint8_t* game_image()
		{
			return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(image_base));
		}

		void* in_game(const address va)
		{
			return reinterpret_cast<void*>(static_cast<std::uintptr_t>(va));
		}

		address address_of(const void* pointer)
		{
			return static_cast<address>(reinterpret_cast<std::uintptr_t>(pointer));
		}

		const guard* first_mismatch_guarded()
		{
			__try
			{
				return first_mismatch(game_image());
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &guards[0];
			}
		}

		struct write
		{
			address va;
			std::vector<std::uint8_t> bytes;
		};

		// Every write or none: each site is made writable first, so a refusal leaves the game as it was; then every
		// write; then each site's protection goes back.
		bool patch(const std::vector<write>& writes)
		{
			std::vector<DWORD> protection(writes.size());
			for (std::size_t i = 0; i < writes.size(); ++i)
			{
				if (!VirtualProtect(in_game(writes[i].va), writes[i].bytes.size(), PAGE_EXECUTE_READWRITE, &protection[i]))
				{
					logger::write("break rule: ERROR: can't unprotect XMen2.exe at 0x%08lX (error %lu) - %s", static_cast<unsigned long>(writes[i].va), GetLastError(),
					              as_before);
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
	}

	void install(const HMODULE game)
	{
		const auto narrow = ini::text(L"Game", L"BreakRule").value_or("");
		const auto chosen = parse_rule(narrow);
		if (!chosen.error.empty())
		{
			logger::write("break rule: [Game] BreakRule=%s %s - %s", narrow.c_str(), chosen.error.c_str(), as_before);
			return;
		}
		if (!chosen.set)
		{
			return; // no key: the game's own rule
		}
		if (chosen.value == rule::xml2)
		{
			logger::write("break rule: XML2's own ([Game] BreakRule=xml2) - nothing patched");
			return;
		}

		state = 0;
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("break rule: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("break rule: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, as_before);
			return;
		}

		// Both bridges go in one block of the fix's own, made executable once it is written.
		const auto parse = parse_bridge(address_of(&object_parsed), parse_resume_va);
		const auto gate = gate_bridge(address_of(&hit_allowed), gate_refused_va, gate_allowed_va);
		auto* block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, parse.size() + gate.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!block)
		{
			logger::write("break rule: ERROR: no memory for the bridges (error %lu) - %s", GetLastError(), as_before);
			return;
		}
		std::memcpy(block, parse.data(), parse.size());
		std::memcpy(block + parse.size(), gate.data(), gate.size());
		DWORD old = 0;
		if (!VirtualProtect(block, parse.size() + gate.size(), PAGE_EXECUTE_READ, &old))
		{
			logger::write("break rule: ERROR: can't make the bridges executable (error %lu) - %s", GetLastError(), as_before);
			VirtualFree(block, 0, MEM_RELEASE);
			return;
		}
		FlushInstructionCache(GetCurrentProcess(), block, parse.size() + gate.size());

		game_level = reinterpret_cast<level_function>(static_cast<std::uintptr_t>(level_function_va));
		log_hits = ini::flag(L"Debug", L"LogBreakRule", false);
		const address level = address_of(&melee_level);
		std::vector<std::uint8_t> slot(4);
		std::memcpy(slot.data(), &level, 4);
		const std::vector<write> writes{
			{level_slot_va, slot},
			{parse_hook_va, jump(parse_hook_va, address_of(block), limits_rules::hex_size(parse_displaced))},
			{gate_hook_va, jump(gate_hook_va, address_of(block + parse.size()), limits_rules::hex_size(gate_displaced))},
		};
		if (!patch(writes))
		{
			VirtualFree(block, 0, MEM_RELEASE);
			return;
		}
		state = 1;
		logger::write("break rule: X-Men Legends 1's ([Game] BreakRule=xml1): an object whose definition carries %s breaks only to a hit of at least that level "
		              "on XML1's scale (a melee hit: the attack's authored level + 0/3/6/8 for Might 0-3 + the damageLevel affecter, at most 9; any other hit: "
		              "its own level) - the objects' gate (0x%08lX), their structure read (0x%08lX) and the melee level slot (0x%08lX) hooked; the level and "
		              "structure bytes the rest of the game reads are unchanged%s",
		              structure_attribute, static_cast<unsigned long>(gate_hook_va), static_cast<unsigned long>(parse_hook_va),
		              static_cast<unsigned long>(level_slot_va), log_hits ? "; every hit is logged ([Debug] LogBreakRule=1)" : "");
	}

	std::string status()
	{
		const int s = state.load();
		return std::string("break rule ") + (s == 1 ? "xml1" : s == -1 ? "xml2" : "unavailable") + "; object hits judged " + std::to_string(judged.load()) + "; refused " +
		       std::to_string(refused.load()) + "; without xml1structure " + std::to_string(unknown.load());
	}
}
