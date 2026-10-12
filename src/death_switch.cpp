#include "death_switch.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include <cstdint>
#include <vector>

namespace death_switch
{
	namespace
	{
		using u32 = std::uint32_t;
		using namespace death_switch_rules;
		bool was_requested = false, applied = false;
		volatile unsigned long count = 0;

		template <class T> T& field(void* actor, const u32 offset) { return *reinterpret_cast<T*>(static_cast<unsigned char*>(actor) + offset); }
		using successor_t = u32*(__cdecl*)(u32*, int, int);
		using resolve_t = void*(__fastcall*)(const u32*, void*);
		using clear_t = void(__fastcall*)(void*, void*);

		// Replaces the CALL 0x420860 at 0x42a042 (same cdecl ABI; the caller reads *out and cleans the stack).
		u32* __cdecl successor(u32* out, const int team, const int handed)
		{
			u32* const result = reinterpret_cast<successor_t>(native_successor)(out, team, handed);
			const u32 handle = out ? *out : 0;
			const u32 none = *reinterpret_cast<const u32*>(null_handle);
			void* const actor = handle && handle != none ? reinterpret_cast<resolve_t>(resolve)(&handle, nullptr) : nullptr;
			if (actor && should_clear(handle, none, handed, true, field<u32>(actor, controller), field<std::uint8_t>(actor, ai_flags), field<float>(actor, health)))
			{
				// 0x42bd60 without its target clear (+0x3e0 and the target-marker call): the move intent only.
				field<u32>(actor, follow) = 0;
				reinterpret_cast<clear_t>(clear_goal)(actor, nullptr);
				field<u32>(actor, mouse_mode) = 0;
				field<u32>(actor, mouse_828) = 0;
				field<std::uint8_t>(actor, stuck) = 0;
				field<std::uint8_t>(actor, steer) = 0;
				count = count + 1;
				logger::write("death switch: controller %d handed to %08X; its AI move intent cleared", handed, handle);
			}
			return result;
		}

		const limits_rules::guard* first_mismatch(xml1_hook_transaction::memory& memory)
		{
			for (const auto& g : guards)
			{
				std::vector<std::uint8_t> bytes(limits_rules::hex_size(g.hex));
				if (!memory.read(g.va, bytes.data(), bytes.size()) || !limits_rules::matches(bytes.data(), g.hex)) return &g;
			}
			return nullptr;
		}
	}

	bool requested() { return was_requested; }
	bool installed() { return applied; }
	registration capability() { return death_switch_rules::capability(was_requested, applied); }
	unsigned long cleared() { return count; }

	void install(const HMODULE game)
	{
		if (applied) return; // installed once; never reread a changed INI
		was_requested = ini::flag(L"Game", L"DeathSwitchClear", false);
		if (!was_requested)
		{
			logger::write("death switch: off - native hand-over (the next hero finishes its AI walk)");
			return;
		}
		if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base)
		{
			logger::write("death switch: image isn't retail - nothing patched; online agreement refused");
			return;
		}
		xml1_hook_transaction::memory memory;
		if (const auto* g = first_mismatch(memory))
		{
			logger::write("death switch: %08X (%s) isn't retail - nothing patched; online agreement refused", unsigned(g->va), g->what);
			return;
		}
		const auto hook = call_bytes(call_site, static_cast<u32>(reinterpret_cast<std::uintptr_t>(&successor)));
		const std::vector<xml1_hook_transaction::patch> writes{
			{call_site, {native_call.begin(), native_call.end()}, {hook.begin(), hook.end()}}};
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			applied = true;
			logger::write("death switch: on - the hero handed a dead hero's controller stops its AI walk (0x42A042)");
		}
		else if (safe)
			logger::write("death switch: install refused at 0x42A042 - native hand-over retained; online agreement refused");
		else
		{
			game_version::refuse_startup();
			logger::write("death switch: rollback at 0x42A042 failed; process startup refused");
		}
	}
}
