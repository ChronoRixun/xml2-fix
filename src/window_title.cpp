#include "window_title.hpp"

#include "iat_hook.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "window_title_rules.hpp"

#include <Windows.h>

#include <string>

namespace window_title
{
	namespace
	{
		using namespace window_title_rules;

		using create_window_ex_a_t = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
		using set_window_text_a_t = BOOL(WINAPI*)(HWND, LPCSTR);

		create_window_ex_a_t real_create_window_ex_a = nullptr;
		set_window_text_a_t real_set_window_text_a = nullptr;
		std::string title; // set once, before the hooks are in place

		bool engine_window(const HWND handle)
		{
			char name[64]{};
			return handle && GetClassNameA(handle, name, static_cast<int>(sizeof(name))) > 0 && is_engine_class(name);
		}

		HWND WINAPI hooked_create_window_ex_a(const DWORD ex_style, const LPCSTR class_name, const LPCSTR game_title, const DWORD style, const int x, const int y,
		                                      const int width, const int height, const HWND parent, const HMENU menu, const HINSTANCE instance, const LPVOID param)
		{
			const bool ours = class_name && !IS_INTRESOURCE(class_name) && is_engine_class(class_name);
			if (ours)
			{
				logger::write_once("title:create", "title: the game's window opens as \"%s\" ([Game] WindowTitle; the game asked for \"%s\")", title.c_str(),
				                   game_title ? game_title : "");
			}
			return real_create_window_ex_a(ex_style, class_name, ours ? title.c_str() : game_title, style, x, y, width, height, parent, menu, instance, param);
		}

		BOOL WINAPI hooked_set_window_text_a(const HWND handle, const LPCSTR text)
		{
			if (engine_window(handle))
			{
				logger::write_once("title:set", "title: the engine retitles its window (\"%s\") - \"%s\" kept ([Game] WindowTitle)", text ? text : "", title.c_str());
				return real_set_window_text_a(handle, title.c_str());
			}
			return real_set_window_text_a(handle, text);
		}
	}

	void install()
	{
		const auto chosen = parse_title(ini::text(L"Game", L"WindowTitle"));
		if (!chosen.error.empty())
		{
			logger::write("title: [Game] WindowTitle %s - the window keeps the game's title", chosen.error.c_str());
			return;
		}
		if (chosen.title.empty())
		{
			return; // no key: the game's own title
		}
		title = chosen.title;

		const HMODULE engine_display = GetModuleHandleA("libIGDisplay.dll");
		real_create_window_ex_a = reinterpret_cast<create_window_ex_a_t>(
			iat_hook::hook(engine_display, "USER32.dll", "CreateWindowExA", 0, reinterpret_cast<void*>(&hooked_create_window_ex_a)));
		real_set_window_text_a = reinterpret_cast<set_window_text_a_t>(
			iat_hook::hook(engine_display, "USER32.dll", "SetWindowTextA", 0, reinterpret_cast<void*>(&hooked_set_window_text_a)));
		if (!real_create_window_ex_a)
		{
			logger::write("title: libIGDisplay.dll doesn't create the game's window with CreateWindowExA (not the game?) - the window keeps the game's title");
			return;
		}
		logger::write("title: the game's window is called \"%s\" ([Game] WindowTitle) - libIGDisplay.dll's CreateWindowExA%s hooked for its window only",
		              title.c_str(), real_set_window_text_a ? " and SetWindowTextA" : "");
	}
}
