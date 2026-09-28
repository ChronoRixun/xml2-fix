#pragma once

// The decisions behind the display fix, kept apart from the hooks so xml2_test can check them:
// which resolution the game should believe it runs at, what the Video options list offers, how
// the Direct3D present parameters change for each mode, and where the window goes.

#include "d3d8_min.hpp"

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

	struct options
	{
		mode window_mode = mode::stock;
		int width = 0; // forced resolution; 0 = the desktop size (borderless) or the game's own setting
		int height = 0;
		bool topmost = false;
		bool run_in_background = true;
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
	// picked; when there are too many, the smallest go.
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
	// device. `notes` receives what changed and why.
	inline d3d8::present_parameters rewrite_present(const d3d8::present_parameters& requested, const options& opts, const d3d8::display_mode& desktop,
	                                                const std::function<bool(DWORD, DWORD)>& multisample_ok,
	                                                const std::function<bool(DWORD)>& back_buffer_format_ok, std::string& notes)
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
			return pp;
		}

		if (!manages_window(opts.window_mode))
		{
			return pp;
		}

		pp.windowed = TRUE;
		pp.fullscreen_refresh_rate = 0;                                   // must be 0 for a windowed device
		pp.fullscreen_presentation_interval = d3d8::present_interval_default; // the only value Direct3D 8 allows windowed
		if (pp.swap_effect == d3d8::swap_flip)
		{
			pp.swap_effect = d3d8::swap_discard;
			notes += "swap effect: discard; ";
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
