#pragma once

// The frame-rate cap's decisions, kept apart from the hooks so xml2_test can check them: what
// [Display] FrameRate and VSync in xml2-fix.ini mean, the target that follows, the bytes of the
// game's own 60 fps spin that the cap switches off, how a frame's wait is split between the
// timer and a spin, and the frames-per-second count the test pipe reports.

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>

namespace frame_rate_rules
{
	// [Display] FrameRate. Absent: the game's own 60 fps cap, untouched.
	struct cap
	{
		enum class kind
		{
			stock,     // no key: the game's busy-spin at 60 fps stays
			unlimited, // 0 / off: the spin is switched off, nothing paces
			fixed,     // n: the fix paces at n fps
			refresh    // refresh: the fix paces at the desktop's refresh rate
		};

		kind what = kind::stock;
		unsigned fps = 0; // fixed
		bool operator==(const cap&) const = default;
	};

	constexpr unsigned min_fps = 10;
	constexpr unsigned max_fps = 1000;
	constexpr unsigned stock_fps = 60;           // what the game's own spin allows
	constexpr unsigned refresh_fallback_fps = 60; // "refresh" when the desktop's rate is unknown

	inline std::string lower(const std::string_view text)
	{
		std::string result(text);
		std::ranges::transform(result, result.begin(), [](const char c) { return static_cast<char>(std::tolower(static_cast<unsigned char>(c))); });
		return result;
	}

	inline bool all_digits(const std::string_view text)
	{
		return !text.empty() && std::ranges::all_of(text, [](const char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
	}

	// "" -> stock; "0", "off", "unlimited" -> unlimited; "refresh" -> refresh; "10".."1000" -> that
	// many fps; anything else -> nothing (the caller logs it and keeps stock).
	inline std::optional<cap> parse_frame_rate(const std::string_view text)
	{
		const auto value = lower(text);
		if (value.empty())
		{
			return cap{};
		}
		if (value == "0" || value == "off" || value == "unlimited")
		{
			return cap{cap::kind::unlimited, 0};
		}
		if (value == "refresh")
		{
			return cap{cap::kind::refresh, 0};
		}
		if (!all_digits(value) || value.size() > 4)
		{
			return std::nullopt;
		}
		const unsigned long fps = std::strtoul(value.c_str(), nullptr, 10);
		if (fps < min_fps || fps > max_fps)
		{
			return std::nullopt;
		}
		return cap{cap::kind::fixed, static_cast<unsigned>(fps)};
	}

	// [Display] VSync. Absent: the engine's own presentation interval.
	enum class vsync
	{
		untouched,
		off,
		on,
		invalid
	};

	inline vsync parse_vsync(const std::string_view text)
	{
		const auto value = lower(text);
		if (value.empty()) return vsync::untouched;
		if (value == "1" || value == "on" || value == "true" || value == "yes") return vsync::on;
		if (value == "0" || value == "off" || value == "false" || value == "no") return vsync::off;
		return vsync::invalid;
	}

	// The frames per second the fix paces at; 0 = it doesn't (stock, or unlimited).
	inline unsigned target_fps(const cap& setting, const unsigned desktop_refresh)
	{
		switch (setting.what)
		{
		case cap::kind::fixed: return setting.fps;
		case cap::kind::refresh: return desktop_refresh ? desktop_refresh : refresh_fallback_fps;
		default: return 0;
		}
	}

	// Whether the game's own 60 fps spin has to go: for any setting at all, since the spin would
	// cap everything at 60 and, below 60, pace worse than the fix does.
	inline bool disables_stock_cap(const cap& setting)
	{
		return setting.what != cap::kind::stock;
	}

	// What the fix paces at once VSync is taken into account. In a window (borderless/windowed)
	// Direct3D 8 has no usable vsync (measured: the default interval never waits, COPY_VSYNC runs
	// at ~32 fps under the desktop compositor), so VSync=1 there means frames paced to the
	// desktop's refresh rate; the compositor shows them without tearing. It never raises the rate
	// above FrameRate's, and with FrameRate absent the game's own 60 fps spin stays in charge.
	inline unsigned effective_target(const cap& setting, const unsigned desktop_refresh, const bool window_vsync)
	{
		if (!disables_stock_cap(setting))
		{
			return 0;
		}
		unsigned target = target_fps(setting, desktop_refresh);
		if (window_vsync)
		{
			const unsigned refresh = desktop_refresh ? desktop_refresh : refresh_fallback_fps;
			target = target ? std::min(target, refresh) : refresh;
		}
		return target;
	}

	inline std::string describe(const cap& setting, const unsigned desktop_refresh)
	{
		switch (setting.what)
		{
		case cap::kind::unlimited: return "unlimited";
		case cap::kind::fixed: return std::to_string(setting.fps) + " fps";
		case cap::kind::refresh: return "the desktop's refresh rate (" + std::to_string(target_fps(setting, desktop_refresh)) + " fps)";
		default: return "the game's own 60 fps cap";
		}
	}

	// ---- The game's own limiter --------------------------------------------------------------------

	// A few bytes of XMen2.exe to check, and the ones among them to replace.
	struct code_patch
	{
		const char* what;
		DWORD rva;                             // of the checked bytes, from the module base
		std::array<std::uint8_t, 16> expected; // what the retail build has there
		std::size_t offset;                    // where, within them, the replacement goes
		std::array<std::uint8_t, 4> replacement;
	};

	// CClient::frame (0x401d70) rewrites its minimum frame time at the top of every frame - 1/30 s
	// in one mode, 1/60 s otherwise - and, at the end of the frame, spins until that much has
	// passed since the last one. A minimum of 0.0 ends the spin at once; the 1/30 case is left as
	// it is.
	//   0x401dab  C7 46 18 89 88 08 3D   mov dword ptr [esi+0x18], 0x3d088889   (1/30)
	//   0x401db2  EB 07                  jmp 0x401dbb
	//   0x401db4  C7 46 18 89 88 88 3C   mov dword ptr [esi+0x18], 0x3c888889   (1/60)
	inline constexpr code_patch stock_cap_patch{
		"the game's 60 fps spin (CClient::frame, 0x401db4)", 0x1dab,
		{0xC7, 0x46, 0x18, 0x89, 0x88, 0x08, 0x3D, 0xEB, 0x07, 0xC7, 0x46, 0x18, 0x89, 0x88, 0x88, 0x3C}, 12, {0x00, 0x00, 0x00, 0x00}};

	inline bool matches(const code_patch& patch, const std::uint8_t* bytes)
	{
		return std::memcmp(bytes, patch.expected.data(), patch.expected.size()) == 0;
	}

	// ---- Pacing -----------------------------------------------------------------------------------

	// When each frame may end, in ticks of a clock: one interval after the previous deadline, so a
	// frame that finished early doesn't shift the cadence, and one that overshot its deadline by
	// less than an interval (a late timer wake) is followed by a shorter one that puts the cadence
	// back. A frame that arrives after its next deadline has already passed ends at once and the
	// cadence restarts from there (no burst of catch-up frames).
	class pacer
	{
	public:
		void set_interval(const LONGLONG ticks)
		{
			interval_ = ticks;
			started_ = false;
		}

		LONGLONG interval() const { return interval_; }

		// The moment the frame that is ending now may end; `now` when it needn't wait.
		LONGLONG next(const LONGLONG now)
		{
			if (!interval_)
			{
				return now;
			}
			LONGLONG target = started_ ? deadline_ + interval_ : now;
			if (target < now)
			{
				target = now;
			}
			deadline_ = target;
			started_ = true;
			return target;
		}

	private:
		LONGLONG interval_ = 0;
		LONGLONG deadline_ = 0;
		bool started_ = false;
	};

	// How to wait `remaining` ticks: a timer for all but the last `margin` ticks (the timer wakes
	// up to that much early or late), then a spin on the clock. Under twice the margin, only the spin.
	struct wait_plan
	{
		LONGLONG timer_100ns = 0; // 0: no timer
		bool spin = false;
	};

	inline wait_plan plan_wait(const LONGLONG remaining, const LONGLONG frequency, const LONGLONG margin)
	{
		wait_plan plan;
		if (remaining <= 0 || frequency <= 0)
		{
			return plan;
		}
		plan.spin = true;
		if (remaining > 2 * margin)
		{
			plan.timer_100ns = (remaining - margin) * 10'000'000 / frequency;
		}
		return plan;
	}

	inline LONGLONG ticks_for_fps(const unsigned fps, const LONGLONG frequency)
	{
		return fps ? frequency / fps : 0;
	}

	inline LONGLONG ticks_for_ms(const double ms, const LONGLONG frequency)
	{
		return static_cast<LONGLONG>(ms * static_cast<double>(frequency) / 1000.0);
	}

	// ---- Counting ---------------------------------------------------------------------------------

	// Frames per second over the last full second, in tenths.
	class fps_counter
	{
	public:
		// Counts a frame at `now`; true when a second has passed and fps_x10() is new.
		bool count(const LONGLONG now, const LONGLONG frequency)
		{
			if (!started_)
			{
				start_ = now; // the first frame opens the window; the frames after it are counted
				started_ = true;
				return false;
			}
			++frames_;
			const LONGLONG elapsed = now - start_;
			if (elapsed < frequency || frequency <= 0)
			{
				return false;
			}
			fps_x10_ = static_cast<unsigned>(frames_ * 10 * frequency / elapsed);
			start_ = now;
			frames_ = 0;
			return true;
		}

		unsigned fps_x10() const { return fps_x10_; }

	private:
		LONGLONG start_ = 0;
		LONGLONG frames_ = 0;
		unsigned fps_x10_ = 0;
		bool started_ = false;
	};

	inline std::string fps_text(const unsigned fps_x10)
	{
		return std::to_string(fps_x10 / 10) + "." + std::to_string(fps_x10 % 10);
	}
}
