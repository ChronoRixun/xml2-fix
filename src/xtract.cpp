#include "xtract.hpp"

#include "ini.hpp"
#include "log.hpp"
#include "xtract_rules.hpp"
#include "xtraction_shop_rules.hpp"

#include <cstdint>
#include <cstring>

namespace xtract
{
	namespace
	{
		using namespace xtract_rules;

		constexpr const char* as_before = "an Xtraction Point's menu offers the game's Xtract choice";
		bool shop_installed = false;
		bool shop_permission = false;
		using getter_t = void*(__cdecl*)();
		using text_t = const char*(__fastcall*)(void*, void*, int);
		using option_t = void(__fastcall*)(void*, void*, const char*, const char*, const char*, int, int, int);

		void* __cdecl shop_dialog()
		{
			void* dialog = reinterpret_cast<getter_t>(xtraction_shop_rules::dialog_getter)();
			if (xtraction_shop_rules::should_add(shop_installed, shop_permission))
			{
				__try
				{
					void* texts = reinterpret_cast<getter_t>(xtraction_shop_rules::text_getter)();
					if (dialog && texts && *static_cast<DWORD*>(texts) == xtraction_shop_rules::text_vtable)
					{
						const auto lookup = reinterpret_cast<text_t>((*static_cast<DWORD**>(texts))[2]);
						const char* label = lookup(texts, nullptr, xtraction_shop_rules::label_id);
						if (label && *label)
						{
							const auto add = reinterpret_cast<option_t>((*static_cast<DWORD**>(dialog))[7]);
							add(dialog, nullptr, label, "shopMenu('equip')", nullptr, 1, 1, 0);
							logger::write("xtraction shop: equipment-shop option added (point permission set)");
						}
					}
				}
				__except (EXCEPTION_EXECUTE_HANDLER)
				{
					logger::write("xtraction shop: native option call faulted - menu kept");
				}
			}
			return dialog; // preserve the native getter's result for vt+0x68 (finalize)
		}

		void install_shop(const HMODULE game)
		{
			if (!ini::flag(L"Game", L"XtractionShop", false)) return;
			if (reinterpret_cast<std::uintptr_t>(game) != image_base) return;
			for (const auto& g : xtraction_shop_rules::guards)
			{
				if (!limits_rules::matches(reinterpret_cast<const std::uint8_t*>(g.va), g.hex))
				{
					logger::write("xtraction shop: guard mismatch at 0x%08lX - no option hook", g.va);
					return;
				}
			}
			auto* target = reinterpret_cast<std::uint8_t*>(xtraction_shop_rules::finalize_call);
			DWORD old_protect = 0;
			if (!VirtualProtect(target, 5, PAGE_EXECUTE_READWRITE, &old_protect)) return;
			const auto delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&shop_dialog) -
			                                            (xtraction_shop_rules::finalize_call + 5));
			std::memcpy(target + 1, &delta, sizeof(delta)); // guarded E8, change only its rel32 destination
			VirtualProtect(target, 5, old_protect, &old_protect);
			FlushInstructionCache(GetCurrentProcess(), target, 5);
			shop_installed = true;
			logger::write("xtraction shop: guarded finalize getter wrapped ([Game] XtractionShop=1)");
		}

		const limits_rules::guard* first_xtract_mismatch()
		{
			__try
			{
				return first_mismatch(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(image_base)));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &guards[0];
			}
		}
	}

	void install(const HMODULE game)
	{
		install_shop(game);
		if (ini::flag(L"Game", L"Xtract", true))
		{
			return; // no key, or 1: the game's own menu
		}
		if (!game || reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("xtract: XMen2.exe isn't loaded at 0x400000 (not the game?) - %s", as_before);
			return;
		}
		if (const auto* g = first_xtract_mismatch())
		{
			logger::write("xtract: 0x%08lX isn't the retail code (%s) - %s", g->va, g->what, as_before);
			return;
		}
		auto* target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(choice_block));
		DWORD old_protect = 0;
		if (!VirtualProtect(target, patched_jump.size(), PAGE_EXECUTE_READWRITE, &old_protect))
		{
			logger::write("xtract: ERROR: can't unprotect XMen2.exe's code at 0x%08lX (error %lu) - %s", static_cast<unsigned long>(choice_block), GetLastError(), as_before);
			return;
		}
		apply(reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(image_base)));
		VirtualProtect(target, patched_jump.size(), old_protect, &old_protect);
		FlushInstructionCache(GetCurrentProcess(), target, patched_jump.size());
		logger::write("xtract: no Xtract choice ([Game] Xtract=0): an Xtraction Point's menu offers its title, Change Team and Save only "
		              "(extractionPoint's Xtract block at 0x%08lX jumps over the choice to 0x%08lX)",
		              static_cast<unsigned long>(choice_block), static_cast<unsigned long>(next_choice));
	}

	bool allow_shop(const bool allowed)
	{
		const bool previous = shop_permission;
		shop_permission = allowed;
		return previous;
	}
}
