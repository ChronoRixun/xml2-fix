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

		// Thunks for the personal menu's size / position getters (vtable +0x90 / +0x94): the same calls as the retail
		// getters (CALL [0x67f95c] / [0x67f9c8], the igVec2f / igVec3f constructors), with the picture window's values.
		using vec2_t = void(__thiscall*)(void*, float, float);
		using vec3_t = void(__thiscall*)(void*, float, float, float);

		void* __fastcall window_size(void*, void*, void* out)
		{
			reinterpret_cast<vec2_t>(*reinterpret_cast<void**>(vec2_constructor_import))(out, window_width, window_height);
			return out;
		}

		void* __fastcall window_position(void* self, void*, void* out)
		{
			const auto depth = static_cast<float>(*reinterpret_cast<std::int16_t*>(static_cast<std::uint8_t*>(self) + depth_offset));
			reinterpret_cast<vec3_t>(*reinterpret_cast<void**>(vec3_constructor_import))(out, window_x, depth, window_top);
			return out;
		}

		const guard* picture_mismatch_guarded(const std::uint8_t* image)
		{
			__try { return first_picture_mismatch(image); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return &picture_guards[0]; }
		}

		void install_picture(std::uint8_t* image)
		{
			const auto key = ini::text(L"Game", L"PersonalItemPicture");
			if (!key || *key == "stock" || *key == "0")
			{
				logger::write("personal item picture: off ([Game] PersonalItemPicture absent or stock); the item picture stays fullscreen");
				return;
			}
			if (*key != "window")
			{
				logger::write("personal item picture: unknown value '%s' (window or stock); no patch applied", key->c_str());
				return;
			}
			if (const auto* g = picture_mismatch_guarded(image))
			{
				logger::write("personal item picture: byte mismatch at 0x%08lX (%s); no patch applied", g->va, g->what);
				return;
			}
			const auto slots = make_slots(reinterpret_cast<DWORD>(&window_size), reinterpret_cast<DWORD>(&window_position));
			void* site = image + size_slot - image_base;
			DWORD old = 0;
			if (!VirtualProtect(site, slots.size(), PAGE_READWRITE, &old))
			{
				logger::write("personal item picture: cannot protect the personal menu vtable (error %lu); no patch applied", GetLastError());
				return;
			}
			std::memcpy(site, slots.data(), slots.size());
			DWORD ignored = 0;
			if (!VirtualProtect(site, slots.size(), old, &ignored))
				logger::write("personal item picture: could not restore page protection (error %lu)", GetLastError());
			logger::write("personal item picture: window ([Game] PersonalItemPicture=window); personal-menu image 256x128 at x 128, "
					"top 330 (the first game's picture window) via vtable slots 0x%08lX / 0x%08lX; other menus unchanged", size_slot, position_slot);
		}

		const guard* mismatch_guarded(const std::uint8_t* image)
		{
			__try { return first_mismatch(image); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return &guards[0]; }
		}
	}

	namespace
	{
		void install_text(HMODULE game);
	}

	// The text depth guards read the position getter's vtable slot, which the picture patch rewrites: text first.
	void install(HMODULE game)
	{
		install_text(game);
		if (reinterpret_cast<std::uintptr_t>(game) == image_base)
			install_picture(reinterpret_cast<std::uint8_t*>(game));
	}

	namespace
	{
	void install_text(HMODULE game)
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
}
