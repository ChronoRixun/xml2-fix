#pragma once

// The decisions behind the display fix, kept apart from the hooks so xml2_test can check them:
// which resolution the game should believe it runs at, what the Video options list offers, how
// the Direct3D present parameters change for each mode and for VSync, and where the window goes.

#include "d3d8_min.hpp"
#include "frame_rate_rules.hpp"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace display_rules
{
	enum class mode
	{
		stock,      // no [Display] Mode: the game's own exclusive fullscreen, nothing hooked
		fullscreen, // exclusive fullscreen, but the desktop resolution is offered and, at that
		            // resolution, the desktop's refresh rate is kept
		borderless, // a popup window covering the monitor; Direct3D runs windowed
		windowed    // a normal, centred window with a caption; Direct3D runs windowed
	};

	// [Display] ResolutionList: where the Video options list lives (resolution_rules.hpp).
	enum class resolution_list
	{
		stock, // no key: as before the 64-slot table existed - in the fix's own modes (Mode set) the list is
		       // the fix's, trimmed to the game's 20 slots; in the game's own mode it is the game's, untouched
		game,  // the game's own 20-slot table, the fix's list trimmed to it in every mode (so it can't overflow)
		all    // the table is replaced by one of 64 slots, the fix's list in every mode
	};

	struct options
	{
		mode window_mode = mode::stock;
		int width = 0; // forced resolution; 0 = the desktop size (borderless) or the game's own setting
		int height = 0;
		bool topmost = false;
		bool run_in_background = true;
		frame_rate_rules::cap frame_rate; // [Display] FrameRate; stock = the game's own 60 fps cap
		std::optional<bool> vsync;        // [Display] VSync; nothing = the engine's own presentation interval
		bool in_game_options = true;      // [Display] InGameOptions: the rows in the Advanced Options panel
		resolution_list resolutions = resolution_list::stock; // [Display] ResolutionList
	};

	struct size
	{
		UINT width = 0;
		UINT height = 0;
		bool operator==(const size&) const = default;
	};

	inline mode parse_mode(std::string_view text)
	{
		std::string lower(text);
		std::ranges::transform(lower, lower.begin(), [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
		if (lower == "fullscreen") return mode::fullscreen;
		if (lower == "borderless") return mode::borderless;
		if (lower == "windowed") return mode::windowed;
		return mode::stock;
	}

	inline const char* name(const mode m)
	{
		switch (m)
		{
		case mode::fullscreen: return "fullscreen";
		case mode::borderless: return "borderless";
		case mode::windowed: return "windowed";
		default: return "stock";
		}
	}

	// Direct3D runs windowed and the window is ours to place.
	inline bool manages_window(const mode m)
	{
		return m == mode::borderless || m == mode::windowed;
	}

	// The mouse's client-area messages (WM_MOUSEMOVE 0x200 .. WM_MOUSEHWHEEL 0x20e: moves, buttons,
	// wheels). Windows sends them to the window under the cursor whether or not it has the focus
	// (the wheel too, with "scroll inactive windows" on), and the game's menus hover and click on
	// their lParam; a window of ours drops them while another window has the focus.
	inline bool is_pointer_message(const UINT message)
	{
		return message >= 0x200 && message <= 0x20e;
	}

	// ---- The cursor in the engine's screen space ------------------------------------------------
	//
	// The engine turns a mouse message's client-area point into menu coordinates against its own
	// idea of the screen size, never the window's: XMen2.exe 0x5f9eb0 (from the window callback
	// 0x5faa20) computes x = left + px * width / [0xa09ffc] (0x5fa041..0x5fa057) and
	// y = height - py * height / [0xa0a000] (0x5fa05e..0x5fa074), where [0xa09ffc] / [0xa0a000] are
	// the resolution it was set up with (0x5fad54 / 0x5fad5c, from Settings\Display\Resolution) and
	// width x height its 2D view (512 x 384 at 4:3, 682.7 x 384 at 16:9: the camera 0xa0a138's
	// vt+0x20 / +0x28, left = -(width - 512) / 2 its vt+0x2c). The Advanced Options panel (0x621d07)
	// and the 3D cursor (0x5f5cbf, GetCursorPos + ScreenToClient) scale by the same two numbers.
	// Direct3D stretches the back buffer over whatever client area the window has, so when the client
	// area isn't that size - a window Windows shrank on a DPI change (1280x720 dragged from a 125%
	// monitor to a 100% one becomes 1023x575), a borderless window over a monitor bigger than a forced
	// Width x Height - every hover lands on the wrong item, more so towards the bottom and right, and
	// the last rows can't be reached at all. The fix converts the point to the engine's screen pixel
	// it is shown at before the engine sees it.

	// Mouse messages whose lParam is a client-area point. WM_MOUSEWHEEL / WM_MOUSEHWHEEL carry a
	// screen point (the engine converts that with ScreenToClient, hooked separately).
	inline bool has_client_point(const UINT message)
	{
		return is_pointer_message(message) && message != 0x20a && message != 0x20e;
	}

	// One coordinate from a client area `client` pixels long to a screen `screen` pixels long:
	// pixel centre to pixel centre, floor((2v + 1) * screen / (2 * client)), so the client's first
	// and last pixels are the screen's first and last, and points outside the window (a drag with
	// the mouse captured) scale on the same line. Equal sizes, or a size unknown (0), leave it.
	inline LONG scale_coordinate(const LONG value, const UINT client, const UINT screen)
	{
		if (!client || !screen || client == screen)
		{
			return value;
		}
		const long long numerator = (2LL * value + 1) * screen;
		const long long denominator = 2LL * client;
		long long result = numerator / denominator;
		if (numerator % denominator != 0 && numerator < 0)
		{
			--result; // floor, not truncation, for points left of / above the window
		}
		return static_cast<LONG>(result);
	}

	inline POINT client_to_screen_space(const POINT point, const size& client, const size& screen)
	{
		return {scale_coordinate(point.x, client.width, screen.width), scale_coordinate(point.y, client.height, screen.height)};
	}

	// A mouse message's lParam (two signed 16-bit coordinates) converted the same way; the result
	// stays within 16 bits.
	inline LPARAM scale_point_lparam(const LPARAM lparam, const size& client, const size& screen)
	{
		const POINT in{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
		const POINT out = client_to_screen_space(in, client, screen);
		const auto clamp16 = [](const LONG v) { return static_cast<WORD>(static_cast<short>(std::clamp<LONG>(v, -32768, 32767))); };
		return static_cast<LPARAM>(MAKELONG(clamp16(out.x), clamp16(out.y)));
	}

	// ---- The windowed window's size across DPI changes --------------------------------------------
	//
	// Dragged to a monitor of another scale, a per-monitor-aware window is resized by Windows to keep
	// its apparent size (1280x720 from 125% to 100% became 1023x575); the game's window keeps its client
	// area instead, in pixels: the size it had before the change, whatever set it - the forced Width x
	// Height, the engine's own placement, or a resize from outside (the engine then resets its back
	// buffer to it). The render resolution is not that size, so it is tracked apart: every WM_SIZE at
	// the DPI the window was last settled at records the client size; one at another DPI is the frame
	// changing mid-transition and is ignored. The DPI handlers then ask for the recorded size.
	struct kept_client
	{
		size client;          // the client area to keep, in pixels
		UINT dpi = 0;         // the DPI it was recorded at (0: not yet settled)
		bool pending = false; // a DPI change came while maximized or minimized: apply `client` at the next restore
	};

	// The window was created (or settled after a DPI change) with `client` at `dpi`.
	inline void settle_client(kept_client& kept, const size& client, const UINT dpi)
	{
		if (client.width && client.height)
		{
			kept.client = client;
		}
		kept.dpi = dpi;
	}

	// A WM_SIZE (restored) of `client` while the window's DPI is `dpi`; `in_dpi_change`: inside the
	// fix's own DPI handling. Returns whether it was recorded.
	inline bool note_client_resize(kept_client& kept, const size& client, const UINT dpi, const bool in_dpi_change)
	{
		if (in_dpi_change || kept.pending || !client.width || !client.height || (kept.dpi && dpi != kept.dpi))
		{
			return false;
		}
		kept.client = client;
		kept.dpi = dpi;
		return true;
	}

	// A DPI change while the window is maximized or minimized: its size is Windows' (the suggested
	// rectangle, or none), the kept client waits for the restore. Returns whether the fix sizes the
	// window now.
	inline bool sizes_on_dpi_change(kept_client& kept, const bool maximized_or_minimized)
	{
		if (maximized_or_minimized)
		{
			kept.pending = true;
			return false;
		}
		return true;
	}

	// A restored WM_SIZE: after a DPI change that came while maximized or minimized, the client area to
	// apply at the window's DPI now `dpi` (then settled there); nothing otherwise.
	inline std::optional<size> client_after_restore(kept_client& kept, const UINT dpi)
	{
		if (!kept.pending)
		{
			return std::nullopt;
		}
		kept.pending = false;
		kept.dpi = dpi;
		if (!kept.client.width || !kept.client.height)
		{
			return std::nullopt;
		}
		return kept.client;
	}

	// The client area to ask for at a DPI change: the kept one, or `fallback` (the back buffer's) when
	// nothing was recorded.
	inline size client_for_dpi_change(const kept_client& kept, const size& fallback)
	{
		return kept.client.width && kept.client.height ? kept.client : fallback;
	}

	// The engine's own mapping (0x5f9eb0) from its screen space to its 2D view, for the tests: the
	// view is 384 tall and `aspect` * 384 wide, centred on the 512-wide 4:3 view. Integer steps
	// as the engine does them (imul / idiv, then the float add and ftol).
	struct menu_point
	{
		int x, y;
	};
	inline menu_point engine_menu_point(const POINT screen_point, const size& screen, const float aspect)
	{
		const float view_width = aspect * 384.0f;
		const int width = static_cast<int>(view_width);
		const int height = 384;
		const float left = -(view_width - 512.0f) * 0.5f;
		const int x = static_cast<int>(left + static_cast<float>(screen_point.x * width / static_cast<int>(screen.width)));
		const int y = height - screen_point.y * height / static_cast<int>(screen.height);
		return {x, y};
	}

	// "" (absent) -> stock; "all" -> all; "game" -> game; anything else -> nothing (the caller logs it).
	inline std::optional<resolution_list> parse_resolution_list(std::string_view text)
	{
		std::string lower(text);
		std::ranges::transform(lower, lower.begin(), [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
		if (lower.empty()) return resolution_list::stock;
		if (lower == "all") return resolution_list::all;
		if (lower == "game") return resolution_list::game;
		return std::nullopt;
	}

	inline const char* name(const resolution_list list)
	{
		switch (list)
		{
		case resolution_list::game: return "game";
		case resolution_list::all: return "all";
		default: return "stock";
		}
	}

	// Whether the Video options list is the fix's (IDirect3D8::GetAdapterModeCount/EnumAdapterModes
	// hooked): always in the fix's own modes, as before; in the game's own mode only when
	// ResolutionList asks for it, so with no [Display] keys the game's list is its own.
	inline bool fix_builds_mode_list(const options& opts)
	{
		return opts.window_mode != mode::stock || opts.resolutions != resolution_list::stock;
	}

	// The resolution the game should believe it runs at (its Settings\Display\Resolution), when
	// the mode changes it: a forced Width x Height, or the desktop size in borderless mode.
	inline std::optional<size> resolution_override(const options& opts, const size& desktop)
	{
		if (opts.window_mode == mode::stock)
		{
			return std::nullopt;
		}
		if (opts.width > 0 && opts.height > 0)
		{
			return size{static_cast<UINT>(opts.width), static_cast<UINT>(opts.height)};
		}
		if (opts.window_mode == mode::borderless && desktop.width && desktop.height)
		{
			return desktop;
		}
		return std::nullopt;
	}

	// The Video options list. The game lists every adapter mode of at least 640x480, one entry per
	// width x height, sorted ascending, into a table of `cap` fixed slots. This does the same from
	// the adapter's modes and adds the desktop size and the forced resolution, so they can be
	// picked; when there are too many, the smallest go. (resolution_rules::build_list adds the
	// sizes each display mode can offer beyond the adapter's, and sets `cap` from the table in use.)
	inline std::vector<d3d8::display_mode> curate_modes(const std::vector<d3d8::display_mode>& adapter_modes, const size& desktop,
	                                                    const UINT refresh_rate, const std::optional<size>& extra, const size_t cap)
	{
		std::vector<d3d8::display_mode> list;
		const auto add = [&](const UINT width, const UINT height)
		{
			if (width < 640 || height < 480)
			{
				return;
			}
			for (const auto& entry : list)
			{
				if (entry.width == width && entry.height == height)
				{
					return;
				}
			}
			list.push_back({width, height, refresh_rate, d3d8::format_x8r8g8b8});
		};

		for (const auto& entry : adapter_modes)
		{
			add(entry.width, entry.height);
		}
		add(desktop.width, desktop.height);
		if (extra)
		{
			add(extra->width, extra->height);
		}

		std::ranges::sort(list, [](const d3d8::display_mode& a, const d3d8::display_mode& b)
		{
			return a.width != b.width ? a.width < b.width : a.height < b.height;
		});
		if (list.size() > cap)
		{
			list.erase(list.begin(), list.begin() + static_cast<std::ptrdiff_t>(list.size() - cap));
		}
		return list;
	}

	// The present parameters to use in place of the engine's. `multisample_ok(format, type)` and
	// `back_buffer_format_ok(format)` answer whether the adapter supports these for a windowed
	// device, `presentation_interval_ok(interval)` whether it offers that interval fullscreen.
	// `notes` receives what changed and why.
	//
	// VSync: a fullscreen device syncs through its presentation interval (the engine's default is
	// IMMEDIATE, i.e. off, when the driver allows it; ONE is on). A windowed Direct3D 8 device
	// can't set an interval - DEFAULT is the only value it accepts, and a present with it never
	// waits for the vertical blank - and its one synchronised presentation, the COPY_VSYNC swap
	// effect, runs at about 32 fps under the desktop compositor (both measured by xml2_test on
	// Windows 11; see docs/in-game-options-plan.md). So in a window VSync=1 is not a swap effect but
	// frames paced at the desktop's refresh rate (frame_rate_rules::effective_target), which the
	// compositor shows without tearing; VSync=0 only undoes an engine-side windowedVSync.
	inline d3d8::present_parameters rewrite_present(const d3d8::present_parameters& requested, const options& opts, const d3d8::display_mode& desktop,
	                                                const std::function<bool(DWORD, DWORD)>& multisample_ok,
	                                                const std::function<bool(DWORD)>& back_buffer_format_ok,
	                                                const std::function<bool(UINT)>& presentation_interval_ok, std::string& notes)
	{
		auto pp = requested;
		notes.clear();

		if (opts.window_mode == mode::fullscreen)
		{
			// At the desktop's own resolution, keep its refresh rate too: Direct3D's default is
			// often 60 Hz, which is a mode switch of its own.
			if (!pp.windowed && pp.fullscreen_refresh_rate == 0 && desktop.refresh_rate &&
			    pp.back_buffer_width == desktop.width && pp.back_buffer_height == desktop.height)
			{
				pp.fullscreen_refresh_rate = desktop.refresh_rate;
				notes += "refresh rate: the desktop's; ";
			}
		}
		else if (manages_window(opts.window_mode))
		{
			pp.windowed = TRUE;
			pp.fullscreen_refresh_rate = 0;                                   // must be 0 for a windowed device
			pp.fullscreen_presentation_interval = d3d8::present_interval_default; // the only value Direct3D 8 allows windowed
			if (pp.swap_effect == d3d8::swap_flip)
			{
				pp.swap_effect = d3d8::swap_discard;
				notes += "swap effect: discard; ";
			}
			if (opts.vsync.has_value())
			{
				if (*opts.vsync)
				{
					notes += "vsync: on in a window = frames paced at the desktop's refresh rate (Direct3D 8's copy_vsync would run at ~32 fps); ";
				}
				else if (pp.swap_effect == d3d8::swap_copy_vsync)
				{
					pp.swap_effect = d3d8::swap_discard;
					notes += "vsync: off (discard swap effect instead of the engine's copy_vsync); ";
				}
			}
			if (pp.swap_effect == d3d8::swap_copy_vsync || pp.swap_effect == d3d8::swap_copy)
			{
				pp.back_buffer_count = 1; // the copy swap effects allow no more
			}
			if (pp.back_buffer_format != desktop.format && !back_buffer_format_ok(pp.back_buffer_format))
			{
				notes += "back buffer format: the desktop's (" + std::to_string(pp.back_buffer_format) + " isn't supported windowed); ";
				pp.back_buffer_format = desktop.format;
			}
			if (pp.multi_sample_type != d3d8::multisample_none)
			{
				if (pp.swap_effect != d3d8::swap_discard)
				{
					notes += "multisampling: off (needs the discard swap effect); ";
					pp.multi_sample_type = d3d8::multisample_none;
				}
				else if (!multisample_ok(pp.back_buffer_format, pp.multi_sample_type))
				{
					notes += "multisampling: off (" + std::to_string(pp.multi_sample_type) + " samples aren't supported windowed); ";
					pp.multi_sample_type = d3d8::multisample_none;
				}
			}
		}

		// VSync on a fullscreen device (the stock mode's and the fullscreen mode's).
		if (opts.vsync.has_value() && !pp.windowed)
		{
			const UINT wanted = *opts.vsync ? d3d8::present_interval_one : d3d8::present_interval_immediate;
			if (pp.fullscreen_presentation_interval != wanted)
			{
				if (presentation_interval_ok(wanted))
				{
					pp.fullscreen_presentation_interval = wanted;
					notes += *opts.vsync ? "vsync: on (presentation interval one); " : "vsync: off (presentation interval immediate); ";
				}
				else
				{
					notes += std::string("vsync: the adapter doesn't offer presentation interval ") + (*opts.vsync ? "one" : "immediate") + " - left as the engine has it; ";
				}
			}
		}
		return pp;
	}

	struct placement
	{
		DWORD style;
		DWORD ex_style;
		RECT rect; // window rectangle, in screen coordinates
	};

	// Where the game window goes for a client area of `client` pixels: over the whole monitor
	// (borderless) or centred on the work area with a caption but no resize frame (windowed).
	// `requested_style` / `requested_ex_style` are what the engine asked for.
	inline placement place_window(const options& opts, const RECT& monitor, const RECT& work_area, const size& client,
	                              const DWORD requested_style, const DWORD requested_ex_style)
	{
		placement result{requested_style, requested_ex_style & ~static_cast<DWORD>(WS_EX_TOPMOST), {}};
		if (opts.topmost)
		{
			result.ex_style |= WS_EX_TOPMOST;
		}
		const DWORD keep = requested_style & (WS_VISIBLE | WS_DISABLED);

		if (opts.window_mode == mode::borderless)
		{
			result.style = keep | WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
			result.rect = monitor;
			return result;
		}
		if (opts.window_mode == mode::windowed)
		{
			result.style = keep | WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
			RECT frame{0, 0, static_cast<LONG>(client.width), static_cast<LONG>(client.height)};
			AdjustWindowRectEx(&frame, result.style, FALSE, result.ex_style);
			const LONG width = frame.right - frame.left;
			const LONG height = frame.bottom - frame.top;
			// Centred; a window taller or wider than the work area keeps its caption on screen.
			const LONG x = std::max(work_area.left, work_area.left + (work_area.right - work_area.left - width) / 2);
			const LONG y = std::max(work_area.top, work_area.top + (work_area.bottom - work_area.top - height) / 2);
			result.rect = {x, y, x + width, y + height};
			return result;
		}

		result.rect = {0, 0, static_cast<LONG>(client.width), static_cast<LONG>(client.height)};
		return result;
	}
}
