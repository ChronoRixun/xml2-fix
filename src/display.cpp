#include "display.hpp"
#include "d3d8_min.hpp"
#include "display_rules.hpp"
#include "iat_hook.hpp"
#include "log.hpp"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace display
{
	namespace
	{
		using namespace display_rules;

		constexpr const char* window_class = "igWin32WindowClass";
		constexpr size_t game_mode_slots = 20; // XMen2.exe's resolution string table (12 bytes each, 0x6e9800)

		options opts;
		std::recursive_mutex mutex; // hooks re-enter through window messages

		d3d8::display_mode desktop{}; // the desktop at start; the display mode never changes in our modes

		// The engine's Direct3D 8 and device.
		void* d3d = nullptr;
		UINT adapter = 0;
		DWORD device_type = d3d8::device_type_hal;
		d3d8::present_parameters applied{};
		bool have_applied = false;
		bool stock_fallback = false; // our parameters were refused and the engine's own are in use
		bool geometry_hooked = false;
		bool emulate_pause = false; // RunInBackground=0: Present reports the device lost while unfocused
		bool paused = false;
		void (*frame_hook)(void*) = nullptr;     // the test pipe's screenshots, before every Present
		const char* no_multisampling = nullptr;  // why the device is made without multisampling, if it is
		HWND window = nullptr;
		size client_size; // the client area the window should have (the back buffer size)

		std::vector<d3d8::display_mode> modes;
		bool modes_built = false;

		d3d8::direct3d_create8_t real_direct3d_create8 = nullptr;
		d3d8::get_adapter_mode_count_t real_get_adapter_mode_count = nullptr;
		d3d8::enum_adapter_modes_t real_enum_adapter_modes = nullptr;
		d3d8::create_device_t real_create_device = nullptr;
		d3d8::test_cooperative_level_t real_test_cooperative_level = nullptr;
		d3d8::reset_t real_reset = nullptr;
		d3d8::present_t real_present = nullptr;

		using create_window_ex_a_t = HWND(WINAPI*)(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HWND, HMENU, HINSTANCE, LPVOID);
		using set_window_long_a_t = LONG(WINAPI*)(HWND, int, LONG);
		using set_window_pos_t = BOOL(WINAPI*)(HWND, HWND, int, int, int, int, UINT);
		using move_window_t = BOOL(WINAPI*)(HWND, int, int, int, int, BOOL);
		using show_window_t = BOOL(WINAPI*)(HWND, int);
		using reg_query_value_ex_a_t = LSTATUS(WINAPI*)(HKEY, LPCSTR, LPDWORD, LPDWORD, LPBYTE, LPDWORD);

		create_window_ex_a_t real_create_window_ex_a = nullptr;
		set_window_long_a_t real_set_window_long_a = nullptr;
		set_window_pos_t real_set_window_pos = nullptr;
		move_window_t real_move_window = nullptr;
		show_window_t real_show_window = nullptr;
		reg_query_value_ex_a_t real_reg_query_value_ex_a = nullptr;

		// Replaces one COM vtable slot in place (the vtable is shared by every object of the class),
		// keeping the original. Does nothing if it is already ours.
		template <typename T>
		void hook_slot(void* object, const int slot, T replacement, T& original)
		{
			auto** vtable = *static_cast<void***>(object);
			if (vtable[slot] == reinterpret_cast<void*>(replacement))
			{
				return;
			}
			DWORD old_protect = 0;
			VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &old_protect);
			original = reinterpret_cast<T>(vtable[slot]);
			vtable[slot] = reinterpret_cast<void*>(replacement);
			VirtualProtect(&vtable[slot], sizeof(void*), old_protect, &old_protect);
		}

		std::string describe(const d3d8::present_parameters& pp)
		{
			char text[200];
			std::snprintf(text, sizeof(text), "%ux%u format %lu x%u msaa %lu swap %lu %s depth %s/%lu flags %lX refresh %u interval %X",
			              pp.back_buffer_width, pp.back_buffer_height, pp.back_buffer_format, pp.back_buffer_count, pp.multi_sample_type,
			              pp.swap_effect, pp.windowed ? "windowed" : "fullscreen", pp.enable_auto_depth_stencil ? "on" : "off",
			              pp.auto_depth_stencil_format, pp.flags, pp.fullscreen_refresh_rate, pp.fullscreen_presentation_interval);
			return text;
		}

		std::string describe(const RECT& rect)
		{
			char text[80];
			std::snprintf(text, sizeof(text), "(%ld,%ld) %ldx%ld", rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top);
			return text;
		}

		bool manages_geometry()
		{
			return manages_window(opts.window_mode) && !stock_fallback && geometry_hooked;
		}

		// The desktop as the game starts, in physical pixels (EnumDisplaySettings isn't DPI-virtualised).
		d3d8::display_mode current_desktop()
		{
			DEVMODEW mode{};
			mode.dmSize = sizeof(mode);
			d3d8::display_mode result{0, 0, 0, d3d8::format_x8r8g8b8};
			if (EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &mode))
			{
				result.width = mode.dmPelsWidth;
				result.height = mode.dmPelsHeight;
				result.refresh_rate = mode.dmDisplayFrequency;
			}
			return result;
		}

		size desktop_size()
		{
			return {desktop.width, desktop.height};
		}

		// Windowed and borderless windows need real pixels, not DPI-scaled ones, or the back buffer
		// gets stretched. Must run before the game's window exists.
		void make_dpi_aware()
		{
			static bool done = false;
			if (done)
			{
				return;
			}
			done = true;

			const HMODULE user32 = GetModuleHandleW(L"user32.dll");
			using set_context_t = BOOL(WINAPI*)(HANDLE);
			if (const auto set_context = reinterpret_cast<set_context_t>(GetProcAddress(user32, "SetProcessDpiAwarenessContext")))
			{
				const auto per_monitor_aware_v2 = reinterpret_cast<HANDLE>(static_cast<INT_PTR>(-4)); // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2
				if (set_context(per_monitor_aware_v2))
				{
					logger::write("display: process is per-monitor DPI aware");
					return;
				}
			}
			using set_aware_t = BOOL(WINAPI*)();
			if (const auto set_aware = reinterpret_cast<set_aware_t>(GetProcAddress(user32, "SetProcessDPIAware")))
			{
				logger::write("display: SetProcessDPIAware -> %s", set_aware() ? "ok" : "failed (a window already exists?)");
			}
		}

		RECT monitor_rect(const HMONITOR monitor, const bool work_area)
		{
			MONITORINFO info{};
			info.cbSize = sizeof(info);
			if (!monitor || !GetMonitorInfoW(monitor, &info))
			{
				const RECT whole{0, 0, static_cast<LONG>(desktop.width), static_cast<LONG>(desktop.height)};
				return whole;
			}
			return work_area ? info.rcWork : info.rcMonitor;
		}

		placement placement_for(const HMONITOR monitor, const DWORD requested_style, const DWORD requested_ex_style)
		{
			return place_window(opts, monitor_rect(monitor, false), monitor_rect(monitor, true), client_size, requested_style, requested_ex_style);
		}

		bool game_in_foreground()
		{
			DWORD process = 0;
			if (const HWND foreground = GetForegroundWindow())
			{
				GetWindowThreadProcessId(foreground, &process);
			}
			return process == GetCurrentProcessId();
		}

		// ---- Direct3D 8 -------------------------------------------------------------------------

		bool multisample_supported(void* self, const DWORD format, const DWORD type)
		{
			const auto check = d3d8::method<d3d8::check_device_multi_sample_type_t>(self, d3d8::d3d_slot::check_device_multi_sample_type);
			return SUCCEEDED(check(self, adapter, device_type, format, TRUE, type));
		}

		bool back_buffer_format_supported(void* self, const DWORD format)
		{
			const auto check = d3d8::method<d3d8::check_device_type_t>(self, d3d8::d3d_slot::check_device_type);
			return SUCCEEDED(check(self, adapter, device_type, desktop.format, format, TRUE));
		}

		d3d8::present_parameters rewrite(void* self, const d3d8::present_parameters& requested, std::string& notes)
		{
			auto pp = rewrite_present(requested, opts, desktop,
			                          [&](const DWORD format, const DWORD type) { return multisample_supported(self, format, type); },
			                          [&](const DWORD format) { return back_buffer_format_supported(self, format); }, notes);
			if (no_multisampling && pp.multi_sample_type != d3d8::multisample_none)
			{
				pp.multi_sample_type = d3d8::multisample_none;
				notes += std::string("multisampling: off (") + no_multisampling + "); ";
			}
			return pp;
		}

		void remember_applied(const d3d8::present_parameters& pp)
		{
			applied = pp;
			have_applied = true;
			if (pp.back_buffer_width && pp.back_buffer_height)
			{
				client_size = {pp.back_buffer_width, pp.back_buffer_height};
			}
			if (pp.device_window)
			{
				window = pp.device_window;
			}
		}

		// What Direct3D filled in (sizes and formats it chose), back to the engine's copy. Its
		// Windowed flag stays as the engine set it: the engine keeps explicit back buffer sizes that way.
		void copy_back(d3d8::present_parameters& engine, const d3d8::present_parameters& used)
		{
			engine.back_buffer_width = used.back_buffer_width;
			engine.back_buffer_height = used.back_buffer_height;
			engine.back_buffer_format = used.back_buffer_format;
			engine.back_buffer_count = used.back_buffer_count;
		}

		// Creates or resets the device with `wanted`; if Direct3D refuses, once more without
		// multisampling, then with the engine's own `requested` (exclusive fullscreen), so the game
		// still starts. `wanted` ends up as what was used.
		template <typename Call>
		HRESULT apply_with_fallbacks(Call call, d3d8::present_parameters& wanted, const d3d8::present_parameters& requested, const char* what)
		{
			HRESULT result = call(wanted);
			if (FAILED(result) && wanted.multi_sample_type != d3d8::multisample_none && manages_window(opts.window_mode))
			{
				logger::write("display: %s failed (%08lX) - trying without multisampling", what, result);
				auto plain = wanted;
				plain.multi_sample_type = d3d8::multisample_none;
				result = call(plain);
				if (SUCCEEDED(result))
				{
					wanted = plain;
				}
			}
			if (FAILED(result) && std::memcmp(&wanted, &requested, sizeof(wanted)) != 0)
			{
				logger::write("display: ERROR: %s failed (%08lX) - trying the engine's own parameters", what, result);
				wanted = requested;
				result = call(wanted);
				if (SUCCEEDED(result))
				{
					stock_fallback = true;
					logger::write("display: running with the engine's own display settings (exclusive fullscreen) from here on");
				}
			}
			return result;
		}

		HRESULT STDMETHODCALLTYPE hooked_test_cooperative_level(void* self)
		{
			if (!opts.run_in_background && !game_in_foreground())
			{
				return d3d8::err_device_lost;
			}
			return real_test_cooperative_level(self);
		}

		HRESULT STDMETHODCALLTYPE hooked_present(void* self, const RECT* source, const RECT* destination, const HWND override, const void* dirty)
		{
			if (frame_hook)
			{
				frame_hook(self); // the back buffer holds the finished frame
			}
			if (emulate_pause)
			{
				const bool foreground = game_in_foreground();
				if (foreground == paused)
				{
					paused = !foreground;
					logger::write("display: %s (RunInBackground=0)", paused ? "window lost the focus - pausing like the stock game" : "window has the focus again - resuming");
				}
				if (paused)
				{
					return d3d8::err_device_lost; // the engine stops drawing until TestCooperativeLevel says otherwise
				}
			}
			return real_present(self, source, destination, override, dirty);
		}

		HRESULT STDMETHODCALLTYPE hooked_reset(void* self, d3d8::present_parameters* pp)
		{
			if (!pp || stock_fallback)
			{
				return real_reset(self, pp);
			}

			std::lock_guard lock(mutex);
			const auto requested = *pp;
			std::string notes;
			auto wanted = d3d ? rewrite(d3d, requested, notes) : requested;

			// Restoring a minimised window makes the engine reset with the same parameters; nothing to do then.
			if (manages_window(opts.window_mode) && have_applied && std::memcmp(&wanted, &applied, sizeof(wanted)) == 0 &&
			    real_test_cooperative_level && real_test_cooperative_level(self) == S_OK)
			{
				logger::write("display: reset with unchanged parameters skipped");
				return S_OK;
			}

			logger::write("display: reset requested %s", describe(requested).c_str());
			if (std::memcmp(&wanted, &requested, sizeof(wanted)) != 0)
			{
				logger::write("display: reset applied   %s%s%s", describe(wanted).c_str(), notes.empty() ? "" : " - ", notes.c_str());
			}
			const HRESULT result = apply_with_fallbacks([&](d3d8::present_parameters& pp_to_use) { return real_reset(self, &pp_to_use); }, wanted, requested, "reset");
			if (SUCCEEDED(result))
			{
				remember_applied(wanted);
				copy_back(*pp, wanted);
				logger::write("display: reset ok");
			}
			else
			{
				logger::write("display: ERROR: reset failed (%08lX)", result);
			}
			return result;
		}

		HRESULT STDMETHODCALLTYPE hooked_create_device(void* self, const UINT which, const DWORD type, const HWND focus, const DWORD behaviour,
		                                               d3d8::present_parameters* pp, void** out)
		{
			if (!pp || !out)
			{
				return real_create_device(self, which, type, focus, behaviour, pp, out);
			}

			std::lock_guard lock(mutex);
			adapter = which;
			device_type = type;
			const auto requested = *pp;
			std::string notes;
			auto wanted = stock_fallback ? requested : rewrite(self, requested, notes);

			logger::write("display: device requested %s (adapter %u type %lu behaviour %lX)", describe(requested).c_str(), which, type, behaviour);
			if (std::memcmp(&wanted, &requested, sizeof(wanted)) != 0)
			{
				logger::write("display: device applied   %s%s%s", describe(wanted).c_str(), notes.empty() ? "" : " - ", notes.c_str());
			}

			const HRESULT result = apply_with_fallbacks(
				[&](d3d8::present_parameters& pp_to_use) { return real_create_device(self, which, type, focus, behaviour, &pp_to_use, out); },
				wanted, requested, "device creation");
			if (FAILED(result) || !*out)
			{
				logger::write("display: ERROR: device creation failed (%08lX)", result);
				return result;
			}

			remember_applied(wanted);
			if (!window)
			{
				window = focus;
			}
			copy_back(*pp, wanted);

			void* device = *out;
			hook_slot(device, d3d8::device_slot::reset, &hooked_reset, real_reset);
			if (const auto current = d3d8::method<d3d8::test_cooperative_level_t>(device, d3d8::device_slot::test_cooperative_level);
			    current != &hooked_test_cooperative_level)
			{
				real_test_cooperative_level = current; // a second device shares the vtable, possibly already hooked
			}
			emulate_pause = !opts.run_in_background && manages_window(opts.window_mode) && !stock_fallback;
			if (emulate_pause)
			{
				hook_slot(device, d3d8::device_slot::test_cooperative_level, &hooked_test_cooperative_level, real_test_cooperative_level);
			}
			if (emulate_pause || frame_hook)
			{
				hook_slot(device, d3d8::device_slot::present, &hooked_present, real_present);
			}
			logger::write("display: device created - %s", describe(wanted).c_str());
			return result;
		}

		void build_modes(void* self)
		{
			std::vector<d3d8::display_mode> adapter_modes;
			const UINT count = real_get_adapter_mode_count(self, adapter);
			for (UINT i = 0; i < count; ++i)
			{
				d3d8::display_mode mode{};
				if (SUCCEEDED(real_enum_adapter_modes(self, adapter, i, &mode)))
				{
					adapter_modes.push_back(mode);
				}
			}

			modes = curate_modes(adapter_modes, desktop_size(), desktop.refresh_rate, resolution_override(opts, desktop_size()), game_mode_slots);
			modes_built = true;

			std::string list;
			for (const auto& mode : modes)
			{
				list += (list.empty() ? "" : " ") + std::to_string(mode.width) + "x" + std::to_string(mode.height);
			}
			logger::write("display: video options list: %zu of %u adapter modes: %s", modes.size(), count, list.c_str());
		}

		UINT STDMETHODCALLTYPE hooked_get_adapter_mode_count(void* self, const UINT which)
		{
			if (which != adapter)
			{
				return real_get_adapter_mode_count(self, which);
			}
			std::lock_guard lock(mutex);
			if (!modes_built)
			{
				build_modes(self);
			}
			return static_cast<UINT>(modes.size());
		}

		HRESULT STDMETHODCALLTYPE hooked_enum_adapter_modes(void* self, const UINT which, const UINT index, d3d8::display_mode* out)
		{
			if (which != adapter || !out)
			{
				return real_enum_adapter_modes(self, which, index, out);
			}
			std::lock_guard lock(mutex);
			if (!modes_built)
			{
				build_modes(self);
			}
			if (index >= modes.size())
			{
				return d3d8::err_invalid_call;
			}
			*out = modes[index];
			return S_OK;
		}

		void* WINAPI hooked_direct3d_create8(const UINT sdk)
		{
			void* result = real_direct3d_create8(sdk);
			if (!result)
			{
				logger::write("display: Direct3DCreate8(%u) returned nothing", sdk);
				return result;
			}

			std::lock_guard lock(mutex);
			d3d = result;
			modes_built = false;
			hook_slot(result, d3d8::d3d_slot::create_device, &hooked_create_device, real_create_device);
			if (opts.window_mode != mode::stock) // the Video options list stays the game's own in stock mode
			{
				hook_slot(result, d3d8::d3d_slot::get_adapter_mode_count, &hooked_get_adapter_mode_count, real_get_adapter_mode_count);
				hook_slot(result, d3d8::d3d_slot::enum_adapter_modes, &hooked_enum_adapter_modes, real_enum_adapter_modes);
			}

			d3d8::display_mode current{};
			const auto get_display_mode = d3d8::method<d3d8::get_adapter_display_mode_t>(result, d3d8::d3d_slot::get_adapter_display_mode);
			if (SUCCEEDED(get_display_mode(result, adapter, &current)) && current.format)
			{
				desktop.format = current.format;
			}
			logger::write("display: hooked the engine's Direct3D 8 (desktop format %lu)", desktop.format);
			return result;
		}

		// ---- The window ---------------------------------------------------------------------------

		bool ours(const HWND handle)
		{
			return handle && handle == window;
		}

		HWND WINAPI hooked_create_window_ex_a(const DWORD ex_style, const LPCSTR class_name, const LPCSTR title, const DWORD style, const int x, const int y,
		                                      const int width, const int height, const HWND parent, const HMENU menu, const HINSTANCE instance, const LPVOID param)
		{
			if (!class_name || IS_INTRESOURCE(class_name) || std::strcmp(class_name, window_class) != 0 || !manages_geometry())
			{
				return real_create_window_ex_a(ex_style, class_name, title, style, x, y, width, height, parent, menu, instance, param);
			}

			make_dpi_aware();
			std::lock_guard lock(mutex);

			// The engine asks for a popup of the game's resolution at 0,0; take the client size out of what it passed.
			RECT frame{0, 0, 0, 0};
			AdjustWindowRectEx(&frame, style, menu != nullptr, ex_style);
			const LONG client_width = width - (frame.right - frame.left);
			const LONG client_height = height - (frame.bottom - frame.top);
			if (client_width > 0 && client_height > 0)
			{
				client_size = {static_cast<UINT>(client_width), static_cast<UINT>(client_height)};
			}

			const POINT at{x == CW_USEDEFAULT ? 0 : x, y == CW_USEDEFAULT ? 0 : y};
			const auto place = placement_for(MonitorFromPoint(at, MONITOR_DEFAULTTOPRIMARY), style, ex_style);
			logger::write("display: engine window: style %08lX ex %08lX at (%d,%d) %dx%d", style, ex_style, x, y, width, height);
			const HWND result = real_create_window_ex_a(place.ex_style, class_name, title, place.style, place.rect.left, place.rect.top,
			                                            place.rect.right - place.rect.left, place.rect.bottom - place.rect.top, parent, menu, instance, param);
			if (result)
			{
				window = result;
				logger::write("display: %s window: style %08lX ex %08lX %s, client %ux%u", name(opts.window_mode), place.style, place.ex_style,
				              describe(place.rect).c_str(), client_size.width, client_size.height);
			}
			else
			{
				logger::write("display: ERROR: CreateWindowExA failed (error %lu)", GetLastError());
			}
			return result;
		}

		LONG WINAPI hooked_set_window_long_a(const HWND handle, const int index, const LONG value)
		{
			if (ours(handle) && index == GWL_STYLE && manages_geometry())
			{
				const LONG current = GetWindowLongA(handle, GWL_STYLE);
				logger::write("display: engine set window style %08lX - kept %08lX", value, current);
				return current;
			}
			return real_set_window_long_a(handle, index, value);
		}

		// The engine's own placement calls (open, resolution changes) become ours.
		BOOL reposition(const HWND handle, const char* what, const RECT& asked, UINT flags, HWND insert_after)
		{
			std::lock_guard lock(mutex);
			const auto place = placement_for(MonitorFromWindow(handle, MONITOR_DEFAULTTOPRIMARY), static_cast<DWORD>(GetWindowLongA(handle, GWL_STYLE)),
			                                 static_cast<DWORD>(GetWindowLongA(handle, GWL_EXSTYLE)));
			flags &= ~static_cast<UINT>(SWP_NOMOVE | SWP_NOSIZE);
			if (opts.topmost)
			{
				insert_after = HWND_TOPMOST;
				flags &= ~static_cast<UINT>(SWP_NOZORDER);
			}
			logger::write("display: engine %s %s -> %s", what, describe(asked).c_str(), describe(place.rect).c_str());
			return real_set_window_pos(handle, insert_after, place.rect.left, place.rect.top, place.rect.right - place.rect.left,
			                           place.rect.bottom - place.rect.top, flags);
		}

		BOOL WINAPI hooked_set_window_pos(const HWND handle, const HWND insert_after, const int x, const int y, const int width, const int height, const UINT flags)
		{
			if (!ours(handle) || !manages_geometry())
			{
				return real_set_window_pos(handle, insert_after, x, y, width, height, flags);
			}
			return reposition(handle, "SetWindowPos", {x, y, x + width, y + height}, flags, insert_after);
		}

		BOOL WINAPI hooked_move_window(const HWND handle, const int x, const int y, const int width, const int height, const BOOL repaint)
		{
			if (!ours(handle) || !manages_geometry())
			{
				return real_move_window(handle, x, y, width, height, repaint);
			}
			return reposition(handle, "MoveWindow", {x, y, x + width, y + height}, SWP_NOZORDER | SWP_NOACTIVATE, nullptr);
		}

		BOOL WINAPI hooked_show_window(const HWND handle, int command)
		{
			if (ours(handle) && manages_geometry() && command == SW_SHOWDEFAULT)
			{
				command = SW_SHOW; // not whatever the launcher's STARTUPINFO says
			}
			return real_show_window(handle, command);
		}

		// ---- The game's registry settings --------------------------------------------------------

		LSTATUS answer(const LPDWORD type, const LPBYTE data, const LPDWORD size, const DWORD kind, const void* value, const DWORD length)
		{
			if (!size)
			{
				return ERROR_INVALID_PARAMETER;
			}
			if (type)
			{
				*type = kind;
			}
			if (!data)
			{
				*size = length;
				return ERROR_SUCCESS;
			}
			if (*size < length)
			{
				*size = length;
				return ERROR_MORE_DATA;
			}
			std::memcpy(data, value, length);
			*size = length;
			return ERROR_SUCCESS;
		}

		LSTATUS WINAPI hooked_reg_query_value_ex_a(const HKEY key, const LPCSTR name, const LPDWORD reserved, const LPDWORD type, const LPBYTE data, const LPDWORD size)
		{
			if (name && !IS_INTRESOURCE(name) && !stock_fallback)
			{
				// Settings\Display\Resolution: what the game sizes its HUD and aspect ratio by.
				if (std::strcmp(name, "Resolution") == 0)
				{
					if (const auto forced = resolution_override(opts, desktop_size()))
					{
						const auto text = std::to_string(forced->width) + "x" + std::to_string(forced->height);
						logger::write_once("display:resolution", "display: the game asked for its resolution setting -> %s", text.c_str());
						return answer(type, data, size, REG_SZ, text.c_str(), static_cast<DWORD>(text.size() + 1));
					}
				}
				// Settings\RestartOldRez: set when the previous run didn't exit cleanly after a resolution
				// change, and the game then drops to 640x480. There is no mode switch to guard in a window.
				if (std::strcmp(name, "RestartOldRez") == 0 && manages_window(opts.window_mode))
				{
					const DWORD zero = 0;
					logger::write_once("display:restartoldrez", "display: the game's 640x480 crash fallback is switched off");
					return answer(type, data, size, REG_DWORD, &zero, sizeof(zero));
				}
			}
			return real_reg_query_value_ex_a(key, name, reserved, type, data, size);
		}

		// ---- Setup ----------------------------------------------------------------------------------

		options read_options()
		{
			const auto ini = (logger::module_dir() / L"xml2-fix.ini").wstring();
			wchar_t mode[64]{};
			GetPrivateProfileStringW(L"Display", L"Mode", L"", mode, static_cast<DWORD>(std::size(mode)), ini.c_str());
			std::string narrow;
			for (const wchar_t c : std::wstring(mode))
			{
				narrow += static_cast<char>(c);
			}

			options result;
			result.window_mode = parse_mode(narrow);
			result.width = static_cast<int>(GetPrivateProfileIntW(L"Display", L"Width", 0, ini.c_str()));
			result.height = static_cast<int>(GetPrivateProfileIntW(L"Display", L"Height", 0, ini.c_str()));
			result.topmost = GetPrivateProfileIntW(L"Display", L"Topmost", 0, ini.c_str()) != 0;
			result.run_in_background = GetPrivateProfileIntW(L"Display", L"RunInBackground", 1, ini.c_str()) != 0;
			if (!narrow.empty() && result.window_mode == mode::stock)
			{
				logger::write("display: unknown Mode '%s' in xml2-fix.ini (fullscreen, borderless or windowed) - left as the game has it", narrow.c_str());
			}
			return result;
		}

		template <typename T>
		bool hook_import(const HMODULE module, const char* dll, const char* function, T replacement, T& original)
		{
			original = reinterpret_cast<T>(iat_hook::hook(module, dll, function, 0, reinterpret_cast<void*>(replacement)));
			return original != nullptr;
		}
	}

	void set_frame_hook(void (*hook)(void* device))
	{
		frame_hook = hook;
	}

	void disable_multisampling(const char* why)
	{
		no_multisampling = why;
	}

	void install(const HMODULE game)
	{
		opts = read_options();
		if (opts.window_mode == mode::stock && !frame_hook)
		{
			logger::write("display: as the game has it (no [Display] Mode in xml2-fix.ini)");
			return;
		}

		desktop = current_desktop();
		if (opts.window_mode == mode::stock)
		{
			logger::write("display: as the game has it (no [Display] Mode in xml2-fix.ini); the Direct3D device is hooked for the test pipe's screenshots");
		}
		else
		{
			logger::write("display: mode %s, size %s, topmost %d, run in background %d; desktop %ux%u @ %u Hz", name(opts.window_mode),
			              opts.width > 0 && opts.height > 0 ? (std::to_string(opts.width) + "x" + std::to_string(opts.height)).c_str()
			              : opts.window_mode == mode::borderless ? "desktop" : "the game's setting",
			              opts.topmost, opts.run_in_background, desktop.width, desktop.height, desktop.refresh_rate);
		}

		// The engine's Direct3D 8: device creation, resets and the mode list.
		const HMODULE gfx = GetModuleHandleA("libIGGfx.dll");
		if (!hook_import(gfx, "d3d8.dll", "Direct3DCreate8", &hooked_direct3d_create8, real_direct3d_create8))
		{
			logger::write("display: libIGGfx.dll doesn't import Direct3DCreate8 - display left as the game has it%s",
			              frame_hook ? " (and no frames for the test pipe)" : "");
			opts.window_mode = mode::stock;
			return;
		}
		if (opts.window_mode == mode::stock)
		{
			return; // only the device hooks, for the frame hook
		}

		if (manages_window(opts.window_mode))
		{
			// The engine's window: created, styled and placed by igWin32Window through these.
			const HMODULE engine_display = GetModuleHandleA("libIGDisplay.dll");
			geometry_hooked = hook_import(engine_display, "USER32.dll", "CreateWindowExA", &hooked_create_window_ex_a, real_create_window_ex_a) &&
			                  hook_import(engine_display, "USER32.dll", "SetWindowLongA", &hooked_set_window_long_a, real_set_window_long_a) &&
			                  hook_import(engine_display, "USER32.dll", "SetWindowPos", &hooked_set_window_pos, real_set_window_pos) &&
			                  hook_import(engine_display, "USER32.dll", "MoveWindow", &hooked_move_window, real_move_window) &&
			                  hook_import(engine_display, "USER32.dll", "ShowWindow", &hooked_show_window, real_show_window);
			if (!geometry_hooked)
			{
				logger::write("display: libIGDisplay.dll doesn't create the window the way this build expects - keeping exclusive fullscreen");
				opts.window_mode = mode::fullscreen;
			}
		}

		// The game's settings: the resolution it believes it runs at, and its crash fallback.
		if (manages_window(opts.window_mode) || (opts.width > 0 && opts.height > 0))
		{
			if (!hook_import(game, "ADVAPI32.dll", "RegQueryValueExA", &hooked_reg_query_value_ex_a, real_reg_query_value_ex_a))
			{
				logger::write("display: the game doesn't read its settings through RegQueryValueExA - resolution left to its registry setting");
			}
		}
	}
}
