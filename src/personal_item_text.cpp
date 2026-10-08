#include "personal_item_text.hpp"
#include "personal_item_text_rules.hpp"
#include "ini.hpp"
#include "log.hpp"

namespace personal_item_text
{
	namespace
	{
		using namespace personal_item_text_rules;
		using constructor_t = void*(__fastcall*)(void*, void*);

		void* __fastcall construct(void* self, void*)
		{
			void* result = reinterpret_cast<constructor_t>(constructor)(self, nullptr);
			configure_menu(result);
			return result;
		}

		const guard* mismatch_guarded(const std::uint8_t* image)
		{
			__try { return first_mismatch(image); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return &guards[0]; }
		}
	}

	void install(HMODULE game)
	{
		if (!ini::flag(L"Game", L"PersonalItemText", true))
		{
			logger::write("personal item text: off ([Game] PersonalItemText=0); retail image depth retained");
			return;
		}
		if (reinterpret_cast<std::uintptr_t>(game) != image_base)
		{
			logger::write("personal item text: unsupported image base; retail image depth retained");
			return;
		}
		auto* image = reinterpret_cast<std::uint8_t*>(game);
		if (const auto* g = mismatch_guarded(image))
		{
			logger::write("personal item text: byte mismatch at 0x%08lX (%s); no patch applied", g->va, g->what);
			return;
		}
		std::array<std::uint8_t, 5> patch{};
		if (!make_patch(image, true, reinterpret_cast<DWORD>(&construct), patch))
		{
			logger::write("personal item text: bytes changed during initialization; no patch applied");
			return;
		}
		void* site = image + constructor_call - image_base;
		DWORD old = 0;
		if (!VirtualProtect(site, patch.size(), PAGE_EXECUTE_READWRITE, &old))
		{
			logger::write("personal item text: cannot protect constructor call (error %lu); no patch applied", GetLastError());
			return;
		}
		std::memcpy(site, patch.data(), patch.size());
		FlushInstructionCache(GetCurrentProcess(), site, patch.size());
		DWORD ignored = 0;
		if (!VirtualProtect(site, patch.size(), old, &ignored))
			logger::write("personal item text: could not restore page protection (error %lu)", GetLastError());
		logger::write("personal item text: on ([Game] PersonalItemText=1); personal-menu default image depth -10 -> 10 "
				"at constructor call 0x5b84e9; other menus and explicit depth attributes unchanged");
	}
}
