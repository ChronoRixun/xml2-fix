#include "display.hpp"
#include "d3d8_min.hpp"
#include "display_rules.hpp"
#include "frame_rate.hpp"
#include "iat_hook.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "options_menu.hpp"
#include "resolution_list.hpp"
#include "resolution_rules.hpp"

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
		size_t mode_slots = resolution_rules::stock_slots; // the resolution table's slots: the game's 20, or the fix's 64 (resolution_list.hpp)

		options opts;
		std::recursive_mutex mutex; // hooks re-enter through window messages

		d3d8::display_mode desktop{}; // the desktop at start; the display mode never changes in our modes

		// The engine's Direct3D 8 and device.
		void* d3d = nullptr;
		void* device = nullptr; // the engine's IDirect3DDevice8, once created
		bool present_hooked = false;
		UINT adapter = 0;
		DWORD device_type = d3d8::device_type_hal;
		d3d8::present_parameters applied{};
		bool have_applied = false;
		bool stock_fallback = false; // our parameters were refused and the engine's own are in use
		bool geometry_hooked = false;
		bool paused = false; // RunInBackground=0: Present reports the device lost while unfocused
		void (*frame_hook)(void*) = nullptr;     // the test pipe's screenshots, before every Present
		const char* no_multisampling = nullptr;  // why the device is made without multisampling, if it is
		const char* no_activate = nullptr;       // why the window is shown without taking the focus, if it is
		HWND window = nullptr;
		size client_size; // the client area the window should have (the back buffer size)

		std::vector<d3d8::display_mode> modes;
		constexpr UINT no_adapter = ~0u;
		UINT modes_adapter = no_adapter; // the adapter `modes` was built for

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
		using get_cursor_pos_t = BOOL(WINAPI*)(LPPOINT);
		using set_cursor_pos_t = BOOL(WINAPI*)(int, int);
		using clip_cursor_t = BOOL(WINAPI*)(const RECT*);

		get_cursor_pos_t real_get_cursor_pos = nullptr;
		set_cursor_pos_t real_set_cursor_pos = nullptr;
		clip_cursor_t real_clip_cursor = nullptr;

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

		// RunInBackground=0 in a window of ours: the stock pause on losing the focus, emulated. Read
		// every frame, since the in-game rows change RunInBackground while the game runs.
		bool pauses_when_unfocused()
		{
			return !opts.run_in_background && manages_window(opts.window_mode) && !stock_fallback;
		}

		bool window_vsync()
		{
			return opts.vsync.value_or(false) && manages_window(opts.window_mode);
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

		bool presentation_interval_supported(void* self, const UINT interval)
		{
			d3d8::caps8 caps{};
			const auto get = d3d8::method<d3d8::get_device_caps_t>(self, d3d8::d3d_slot::get_device_caps);
			return SUCCEEDED(get(self, adapter, device_type, &caps)) && (caps.presentation_intervals & interval) != 0;
		}

		d3d8::present_parameters rewrite(void* self, const d3d8::present_parameters& requested, std::string& notes)
		{
			auto pp = rewrite_present(requested, opts, desktop,
			                          [&](const DWORD format, const DWORD type) { return multisample_supported(self, format, type); },
			                          [&](const DWORD format) { return back_buffer_format_supported(self, format); },
			                          [&](const UINT interval) { return presentation_interval_supported(self, interval); }, notes);
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
		// still starts. `wanted` ends up as what was used. A reset that fails because the device is
		// lost is not a refusal of our parameters: the engine (igDxVisualContext::getLastError)
		// retries it every 200 ms once TestCooperativeLevel allows, so that error goes straight back.
		template <typename Call>
		HRESULT apply_with_fallbacks(Call call, d3d8::present_parameters& wanted, const d3d8::present_parameters& requested, const char* what,
		                             const bool engine_retries)
		{
			HRESULT result = call(wanted);
			if (result == d3d8::err_device_lost && engine_retries)
			{
				logger::write("display: %s: the device is lost right now - left to the engine's retry", what);
				return result;
			}
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
			if (pauses_when_unfocused() && !game_in_foreground())
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
			if (pauses_when_unfocused())
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
			else if (paused)
			{
				paused = false;
				logger::write("display: resuming - RunInBackground is on now");
			}
			const HRESULT result = real_present(self, source, destination, override, dirty);
			if (SUCCEEDED(result))
			{
				frame_rate::on_present(); // paces the frame (FrameRate) and counts it (the pipe's "status")
			}
			return result;
		}

		// Present is hooked only when something needs it: the pause of RunInBackground=0 in a window,
		// the test pipe's screenshots, or the frame limiter - at device creation, or later when the
		// in-game rows start the limiter. Called with the mutex held, on the game's thread.
		void hook_present(const char* why)
		{
			if (present_hooked || !device)
			{
				return;
			}
			hook_slot(device, d3d8::device_slot::present, &hooked_present, real_present);
			present_hooked = true;
			logger::write("display: Present hooked (%s)", why);
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
			const HRESULT result = apply_with_fallbacks([&](d3d8::present_parameters& pp_to_use) { return real_reset(self, &pp_to_use); }, wanted, requested, "reset", true);
			if (SUCCEEDED(result))
			{
				remember_applied(wanted);
				copy_back(*pp, wanted);
				logger::write("display: reset ok");
			}
			else if (result != d3d8::err_device_lost)
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
				wanted, requested, "device creation", false);
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

			device = *out;
			hook_slot(device, d3d8::device_slot::reset, &hooked_reset, real_reset);
			if (const auto current = d3d8::method<d3d8::test_cooperative_level_t>(device, d3d8::device_slot::test_cooperative_level);
			    current != &hooked_test_cooperative_level)
			{
				real_test_cooperative_level = current; // a second device shares the vtable, possibly already hooked
			}
			// The pause on losing the focus can be switched on from the in-game rows at any time, so a
			// window of ours gets both hooks whatever RunInBackground says now. Otherwise Present waits
			// until something needs it (hook_present; the rows' frame rate, for one).
			const bool can_pause = manages_window(opts.window_mode) && !stock_fallback;
			if (can_pause)
			{
				hook_slot(device, d3d8::device_slot::test_cooperative_level, &hooked_test_cooperative_level, real_test_cooperative_level);
			}
			present_hooked = present_hooked && d3d8::method<void*>(device, d3d8::device_slot::present) == reinterpret_cast<void*>(&hooked_present);
			if (can_pause || frame_hook || frame_rate::paces())
			{
				hook_present(can_pause ? "the window's pause without the focus" : frame_hook ? "the test pipe's screenshots" : "the frame limiter");
			}
			logger::write("display: device created - %s", describe(wanted).c_str());
			return result;
		}

		// The list for `which`: the game's list builder (FUN_00619ac0) asks adapter 0 whatever
		// adapter the device is on, and must get a list that fits its table either way.
		void build_modes(void* self, const UINT which)
		{
			std::vector<d3d8::display_mode> adapter_modes;
			const UINT count = real_get_adapter_mode_count(self, which);
			for (UINT i = 0; i < count; ++i)
			{
				d3d8::display_mode mode{};
				if (SUCCEEDED(real_enum_adapter_modes(self, which, i, &mode)))
				{
					adapter_modes.push_back(mode);
				}
			}

			modes = resolution_rules::video_list(adapter_modes, desktop_size(), desktop.refresh_rate, resolution_override(opts, desktop_size()), opts.window_mode,
			                                     opts.resolutions, mode_slots);
			modes_adapter = which;

			std::string list;
			for (const auto& mode : modes)
			{
				list += (list.empty() ? "" : " ") + resolution_rules::text_of(mode.width, mode.height);
			}
			logger::write("display: video options list for adapter %u: %zu sizes from %u adapter modes (%zu-slot table, ResolutionList %s): %s", which, modes.size(), count,
			              mode_slots, name(opts.resolutions), list.c_str());
		}

		UINT STDMETHODCALLTYPE hooked_get_adapter_mode_count(void* self, const UINT which)
		{
			std::lock_guard lock(mutex);
			if (modes_adapter != which)
			{
				build_modes(self, which);
			}
			return static_cast<UINT>(modes.size());
		}

		HRESULT STDMETHODCALLTYPE hooked_enum_adapter_modes(void* self, const UINT which, const UINT index, d3d8::display_mode* out)
		{
			if (!out)
			{
				return real_enum_adapter_modes(self, which, index, out);
			}
			std::lock_guard lock(mutex);
			if (modes_adapter != which)
			{
				build_modes(self, which);
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
			modes_adapter = no_adapter;
			hook_slot(result, d3d8::d3d_slot::create_device, &hooked_create_device, real_create_device);
			// The Video options list: the game writes it into a fixed table with no bounds check
			// (resolution_list.hpp), so the fix's list is kept within that table's slots.
			if (fix_builds_mode_list(opts))
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

		// The engine's window procedure, behind ours (see filtered_window_procedure).
		WNDPROC engine_window_procedure = nullptr;

		// Mouse messages reach a window under the cursor whether or not it has the focus, and the
		// game hovers and clicks its menus on their coordinates (the window callback 0x5faa20 ->
		// 0x5f9eb0, or the Advanced Options panel's hit-test 0x621ea0), with no focus check. While
		// another window has the focus they are dropped, so the owner's mouse passing over a game
		// running in the background (the test harness) neither hovers nor clicks anything.
		LRESULT CALLBACK filtered_window_procedure(const HWND handle, const UINT message, const WPARAM wparam, const LPARAM lparam)
		{
			if (is_pointer_message(message) && !game_in_foreground())
			{
				static bool logged = false;
				if (!logged)
				{
					logged = true;
					logger::write("display: another window has the focus - mouse messages to the game's window are dropped until it has it again");
				}
				return 0;
			}
			return engine_window_procedure ? CallWindowProcA(engine_window_procedure, handle, message, wparam, lparam) : DefWindowProcA(handle, message, wparam, lparam);
		}

		// Puts filtered_window_procedure in front of the engine's (an ANSI window: igWin32Window
		// registers its class with the A functions and never replaces the procedure itself).
		void filter_mouse_messages(const HWND handle)
		{
			engine_window_procedure = reinterpret_cast<WNDPROC>(GetWindowLongPtrA(handle, GWLP_WNDPROC));
			if (!engine_window_procedure)
			{
				logger::write("display: couldn't read the game's window procedure (error %lu) - no mouse filter; its menus may react to the mouse while unfocused", GetLastError());
				return;
			}
			if (!SetWindowLongPtrA(handle, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&filtered_window_procedure)))
			{
				logger::write("display: couldn't put the mouse filter on the game's window (error %lu) - its menus may react to the mouse while unfocused", GetLastError());
				return;
			}
			logger::write("display: the game's window ignores the mouse while another window has the focus");
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
				filter_mouse_messages(result);
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
			// The engine sizes a frameless popup, so what it asks for is the client area it wants;
			// until the window's creation or the device has told us the size, that is the size.
			if ((!client_size.width || !client_size.height) && !(flags & SWP_NOSIZE) && asked.right > asked.left && asked.bottom > asked.top)
			{
				client_size = {static_cast<UINT>(asked.right - asked.left), static_cast<UINT>(asked.bottom - asked.top)};
			}
			const auto place = placement_for(MonitorFromWindow(handle, MONITOR_DEFAULTTOPRIMARY), static_cast<DWORD>(GetWindowLongA(handle, GWL_STYLE)),
			                                 static_cast<DWORD>(GetWindowLongA(handle, GWL_EXSTYLE)));
			flags &= ~static_cast<UINT>(SWP_NOMOVE | SWP_NOSIZE);
			if (opts.topmost)
			{
				insert_after = HWND_TOPMOST;
				flags &= ~static_cast<UINT>(SWP_NOZORDER);
			}
			if (no_activate)
			{
				// The engine shows its window with SWP_SHOWWINDOW, which activates it; the test harness
				// keeps it behind the owner's window instead.
				flags |= SWP_NOACTIVATE;
				if (!opts.topmost)
				{
					insert_after = HWND_BOTTOM;
					flags &= ~static_cast<UINT>(SWP_NOZORDER);
				}
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
			if (ours(handle) && manages_geometry())
			{
				if (command == SW_SHOWDEFAULT)
				{
					command = SW_SHOW; // not whatever the launcher's STARTUPINFO says
				}
				if (no_activate && (command == SW_SHOW || command == SW_SHOWNORMAL))
				{
					// The test harness: the window appears behind whatever the owner is using.
					static bool logged = false;
					if (!logged)
					{
						logged = true;
						logger::write("display: window shown without taking the focus (%s)", no_activate);
					}
					command = command == SW_SHOW ? SW_SHOWNA : SW_SHOWNOACTIVATE;
				}
			}
			return real_show_window(handle, command);
		}

		// ---- The cursor while another window has the focus ---------------------------------------
		// The game reads the Windows cursor for its menus (GetCursorPos + ScreenToClient: hovering a
		// button selects it) and moves and clips it (SetCursorPos, ClipCursor). In a window that keeps
		// running in the background, that would let the owner's mouse, used in another program, pick
		// menu items, and let the game move or trap that mouse. While the game doesn't have the focus it
		// sees the cursor parked outside its window and can't move or clip it. (The menus also hover
		// on mouse messages; filtered_window_procedure drops those meanwhile.)

		void note_cursor_parked()
		{
			static bool logged = false;
			if (!logged)
			{
				logged = true;
				logger::write("display: another window has the focus - the game sees the cursor outside its window and can't move or clip it");
			}
		}

		BOOL WINAPI hooked_get_cursor_pos(const LPPOINT point)
		{
			const BOOL result = real_get_cursor_pos(point);
			if (result && point && window && !game_in_foreground())
			{
				RECT rect{};
				if (GetWindowRect(window, &rect))
				{
					point->x = rect.left - 256;
					point->y = rect.top - 256;
					note_cursor_parked();
				}
			}
			return result;
		}

		BOOL WINAPI hooked_set_cursor_pos(const int x, const int y)
		{
			if (!game_in_foreground())
			{
				note_cursor_parked();
				return TRUE;
			}
			return real_set_cursor_pos(x, y);
		}

		BOOL WINAPI hooked_clip_cursor(const RECT* rect)
		{
			if (rect && !game_in_foreground())
			{
				note_cursor_parked();
				return TRUE;
			}
			return real_clip_cursor(rect);
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

		// [Display] key by the fix's one rule (ini_rules.hpp): up to a ';', trimmed; "" when it isn't set.
		std::string read_text(const wchar_t* key)
		{
			return ini::text(L"Display", key).value_or("");
		}

		options read_options()
		{
			const auto mode_text = read_text(L"Mode");

			options result;
			result.window_mode = parse_mode(mode_text);
			result.width = ini::number(L"Display", L"Width", 0);
			result.height = ini::number(L"Display", L"Height", 0);
			result.topmost = ini::flag(L"Display", L"Topmost", false);
			result.run_in_background = ini::flag(L"Display", L"RunInBackground", true);
			result.in_game_options = ini::flag(L"Display", L"InGameOptions", true);
			if (!mode_text.empty() && result.window_mode == mode::stock)
			{
				logger::write("display: unknown Mode '%s' in xml2-fix.ini (fullscreen, borderless or windowed) - left as the game has it", mode_text.c_str());
			}

			const auto list_text = read_text(L"ResolutionList");
			if (const auto parsed = parse_resolution_list(list_text))
			{
				result.resolutions = *parsed;
			}
			else
			{
				logger::write("display: unknown ResolutionList '%s' in xml2-fix.ini (all or game) - taken as absent", list_text.c_str());
			}

			const auto frame_rate_text = read_text(L"FrameRate");
			if (const auto parsed = frame_rate_rules::parse_frame_rate(frame_rate_text))
			{
				result.frame_rate = *parsed;
			}
			else
			{
				logger::write("display: unknown FrameRate '%s' in xml2-fix.ini (%u-%u, refresh or 0) - the game's own 60 fps cap stays", frame_rate_text.c_str(),
				              frame_rate_rules::min_fps, frame_rate_rules::max_fps);
			}

			const auto vsync_text = read_text(L"VSync");
			switch (frame_rate_rules::parse_vsync(vsync_text))
			{
			case frame_rate_rules::vsync::on: result.vsync = true; break;
			case frame_rate_rules::vsync::off: result.vsync = false; break;
			case frame_rate_rules::vsync::invalid:
				logger::write("display: unknown VSync '%s' in xml2-fix.ini (1 or 0) - left as the engine has it", vsync_text.c_str());
				break;
			default: break;
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

	void show_without_focus(const char* why)
	{
		no_activate = why;
	}

	display_rules::options read_ini_options()
	{
		return read_options();
	}

	const display_rules::options& current_options()
	{
		return opts;
	}

	unsigned desktop_refresh_rate()
	{
		return desktop.refresh_rate;
	}

	void set_frame_rate(const frame_rate_rules::cap& setting)
	{
		std::lock_guard lock(mutex);
		opts.frame_rate = setting;
		frame_rate::retarget(setting, window_vsync());
		if (frame_rate::paces())
		{
			hook_present("the frame limiter, from the in-game rows");
		}
	}

	bool set_vsync(const std::optional<bool> on)
	{
		std::lock_guard lock(mutex);
		opts.vsync = on;
		if (manages_window(opts.window_mode) && !stock_fallback)
		{
			logger::write("display: VSync %s in a window - %s", on.value_or(false) ? "on" : "off",
			              on.value_or(false) ? "frames paced at the desktop's refresh rate from now on" : "frames run free again, up to FrameRate");
			frame_rate::retarget(opts.frame_rate, on.value_or(false));
			if (frame_rate::paces())
			{
				hook_present("the frame limiter, from the in-game rows");
			}
			return true;
		}
		return false; // rewrite_present applies it when the device is next created or reset
	}

	void set_run_in_background(const bool on)
	{
		std::lock_guard lock(mutex);
		opts.run_in_background = on;
		logger::write("display: run in background %s%s", on ? "on" : "off",
		              manages_window(opts.window_mode) ? "" : " (matters in the borderless and windowed modes; the game's own fullscreen always pauses without the focus)");
	}

	void install(const HMODULE game)
	{
		opts = read_options();
		const bool caps_frames = frame_rate_rules::disables_stock_cap(opts.frame_rate);

		desktop = current_desktop();
		if (opts.in_game_options)
		{
			options_menu::install(game); // the rows in Advanced Options; checks the game's code first
		}
		else
		{
			logger::write("options: no rows in Advanced Options ([Display] InGameOptions=0)");
		}
		const bool list_hooked = fix_builds_mode_list(opts);
		if (opts.window_mode == mode::stock)
		{
			// Without a Mode the engine's Direct3D is hooked only for what asks for it; the rows need
			// the device to start the frame limiter live, but every hook passes the engine's calls
			// through unchanged until a value is set (Present itself waits for that: hook_present).
			std::string why;
			for (const auto& [wanted, reason] : {std::pair{frame_hook != nullptr, "the test pipe's screenshots"}, std::pair{caps_frames, "the frame rate cap"},
			                                     std::pair{opts.vsync.has_value(), "VSync"}, std::pair{options_menu::installed(), "the in-game options"},
			                                     std::pair{list_hooked, "the Video options list (ResolutionList)"}})
			{
				if (wanted)
				{
					why += (why.empty() ? "" : ", ") + std::string(reason);
				}
			}
			if (why.empty())
			{
				logger::write("display: as the game has it (no [Display] Mode, FrameRate, VSync or ResolutionList in xml2-fix.ini, no in-game rows) - nothing hooked");
				return;
			}
			logger::write("display: as the game has it (no [Display] Mode in xml2-fix.ini); the Direct3D device is hooked for %s", why.c_str());
		}
		else
		{
			logger::write("display: mode %s, size %s, topmost %d, run in background %d; desktop %ux%u @ %u Hz", name(opts.window_mode),
			              opts.width > 0 && opts.height > 0 ? (std::to_string(opts.width) + "x" + std::to_string(opts.height)).c_str()
			              : opts.window_mode == mode::borderless ? "desktop" : "the game's setting",
			              opts.topmost, opts.run_in_background, desktop.width, desktop.height, desktop.refresh_rate);
		}
		if (opts.vsync.has_value())
		{
			if (manages_window(opts.window_mode))
			{
				logger::write("display: VSync %s in a window ([Display] VSync): Direct3D 8 can't sync a windowed present usefully (its copy_vsync runs at ~32 fps under the "
				              "desktop compositor), so %s",
				              *opts.vsync ? "on" : "off",
				              *opts.vsync ? "the frame limiter paces at the desktop's refresh rate instead; the compositor shows the frames without tearing"
				                          : "frames run free, up to FrameRate or the game's own 60 fps cap");
			}
			else
			{
				logger::write("display: VSync %s ([Display] VSync) - presentation interval %s when the engine creates or resets its device", *opts.vsync ? "on" : "off",
				              *opts.vsync ? "one" : "immediate");
			}
		}

		// The engine's Direct3D 8: device creation, resets and the mode list.
		const HMODULE gfx = GetModuleHandleA("libIGGfx.dll");
		if (!hook_import(gfx, "d3d8.dll", "Direct3DCreate8", &hooked_direct3d_create8, real_direct3d_create8))
		{
			logger::write("display: libIGGfx.dll doesn't import Direct3DCreate8 - display left as the game has it%s%s%s",
			              frame_hook ? " (and no frames for the test pipe)" : "", caps_frames ? " (and no frame rate cap: the game's own 60 fps stays)" : "",
			              list_hooked ? " (and the Video options list and its table stay the game's own)" : "");
			opts.window_mode = mode::stock;
			return;
		}

		// The resolution table is only ever relocated behind the mode-list hooks, which keep the list
		// within its slots (the game's writer has no bounds check): they are certain from here on.
		if (list_hooked)
		{
			mode_slots = ::resolution_list::install(game, opts.resolutions); // the namespace, not display_rules::resolution_list
		}

		// The frame limiter paces frames from the Present hook, so it needs the device hooked above.
		frame_rate::install(game, opts.frame_rate, desktop.refresh_rate, window_vsync());

		if (opts.window_mode == mode::stock)
		{
			return; // only the Direct3D hooks: the mode list, the frame hook, the frame limiter and VSync
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
			else
			{
				// The cursor, read and moved by both the game and the engine (see hooked_get_cursor_pos).
				// The real functions are known before any import points at the hooks, so a call can never
				// find them unset.
				const HMODULE user32 = GetModuleHandleW(L"user32.dll");
				real_get_cursor_pos = reinterpret_cast<get_cursor_pos_t>(GetProcAddress(user32, "GetCursorPos"));
				real_set_cursor_pos = reinterpret_cast<set_cursor_pos_t>(GetProcAddress(user32, "SetCursorPos"));
				real_clip_cursor = reinterpret_cast<clip_cursor_t>(GetProcAddress(user32, "ClipCursor"));
				if (real_get_cursor_pos && real_set_cursor_pos && real_clip_cursor)
				{
					int hooked = 0;
					for (const HMODULE module : {game, engine_display})
					{
						get_cursor_pos_t get = nullptr;
						set_cursor_pos_t set = nullptr;
						clip_cursor_t clip = nullptr;
						hooked += hook_import(module, "USER32.dll", "GetCursorPos", &hooked_get_cursor_pos, get);
						hooked += hook_import(module, "USER32.dll", "SetCursorPos", &hooked_set_cursor_pos, set);
						hooked += hook_import(module, "USER32.dll", "ClipCursor", &hooked_clip_cursor, clip);
					}
					logger::write("display: %d cursor imports hooked (the game can't use the cursor while another window has the focus)", hooked);
				}
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
