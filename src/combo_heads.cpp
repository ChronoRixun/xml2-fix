#include "combo_heads.hpp"

#include "combo_heads_rules.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace combo_heads
{
	namespace
	{
		using namespace combo_heads_rules;
		std::atomic<int> state{-1}; // 1 guarded; 0 off by the ini; -2 unavailable
		std::atomic<unsigned> hidden{0};
		show_fn original = reinterpret_cast<show_fn>(static_cast<std::uintptr_t>(retail_show));

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

		address igb_live = igb_pool_live;           // model_pools moves them when [Engine] ModelPools raises the pools
		address shaderfx_live = shaderfx_pool_live;
		int igb_cap = 400, shaderfx_cap = 432;

		long read_live(const address at)
		{
			__try
			{
				return *reinterpret_cast<const volatile long*>(static_cast<std::uintptr_t>(at));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return -1;
			}
		}

		bool hide(std::uint8_t* widget)
		{
			using hide_fn = void(__fastcall*)(void*, void*, int);
			__try
			{
				const auto* table = *reinterpret_cast<const std::uintptr_t* const*>(widget);
				reinterpret_cast<hide_fn>(table[hide_slot_offset / 4])(widget, nullptr, 0);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}
	}

	void __fastcall show(void* hud, void* unused, const void* hero_a, const void* hero_b, const int type, const int amount, const char* text)
	{
		original(hud, unused, hero_a, hero_b, type, amount, text);
		if (!hud)
		{
			return;
		}
		auto* widget = static_cast<std::uint8_t*>(hud) + widget_offset;
		const unsigned missing = missing_heads(widget);
		if (!missing)
		{
			return;
		}
		const bool ok = hide(widget);
		const unsigned n = ++hidden;
		if (n <= 5 || n % 100 == 0)
		{
			logger::write("combo popup: %s missing (model pools: CModelIGB %ld/%d, CModelShaderFx %ld/%d) - popup %s, the combo itself counts (%u so far)",
			              missing == 3 ? "both heroes' heads" : missing == 1 ? "the first hero's head" : "the second hero's head",
			              read_live(igb_live), igb_cap, read_live(shaderfx_live), shaderfx_cap, ok ? "hidden" : "NOT hidden (its hide faulted)", n);
		}
	}

	void set_pool_live_addresses(const std::uint32_t igb, const std::uint32_t shader_fx, const int igb_capacity, const int shader_fx_capacity)
	{
		igb_live = igb;
		shaderfx_live = shader_fx;
		igb_cap = igb_capacity;
		shaderfx_cap = shader_fx_capacity;
	}

	void set_original_for_test(const show_fn f)
	{
		original = f;
	}

	unsigned hidden_count()
	{
		return hidden.load();
	}

	void install(const HMODULE game)
	{
		if (!ini::flag(L"Game", L"ComboPopupGuard", true))
		{
			state = 0;
			logger::write("combo popup: guard off ([Game] ComboPopupGuard=0) - a popup missing a head crashes as in the game");
			return;
		}
		state = -2;
		if (!game || reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base)
		{
			logger::write("combo popup: XMen2.exe isn't loaded at 0x400000 (not the game?) - guard not installed");
			return;
		}
		if (const guard* g = first_mismatch_guarded())
		{
			logger::write("combo popup: 0x%08lX isn't the retail code (%s) - guard not installed", static_cast<unsigned long>(g->va), g->what);
			return;
		}
		const auto target = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&show));
		std::vector<unsigned char> after(4);
		std::memcpy(after.data(), &target, 4);
		const std::vector<xml1_hook_transaction::patch> writes{
			{hud_show_slot, {0xf0, 0x96, 0x59, 0x00}, after},
		};
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			state = 1;
			logger::write("combo popup: guard on - HUD vt+0xb4 (0x%08lX) hides a combo popup that lacks a hero's head instead of drawing through null (0x5a67fe/0x5a6845)",
			              static_cast<unsigned long>(hud_show_slot));
		}
		else if (safe)
		{
			logger::write("combo popup: the patch was refused (protection or readback) - guard not installed");
		}
		else
		{
			game_version::refuse_startup();
			logger::write("combo popup: rollback/protection verification failed; process startup refused");
		}
	}

	std::string status()
	{
		const int s = state.load();
		if (s == 1)
		{
			return "combo popup guard on (" + std::to_string(hidden.load()) + " hidden)";
		}
		return s == 0 ? "combo popup guard off" : s == -1 ? "combo popup guard not installed" : "combo popup guard unavailable";
	}
}
