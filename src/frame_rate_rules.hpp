#pragma once

// The frame-rate cap's decisions, kept apart from the hooks so xml2_test can check them: what
// [Display] FrameRate and VSync in xml2-fix.ini mean, the target that follows, the bytes of the
// game's own 60 fps spin that the cap switches off, how a frame's wait is split between the
// timer and a spin, the frames-per-second count the test pipe reports, and when a menu, popup
// or conversation is on screen (those run at 60 fps whatever FrameRate says).

#include "limits_rules.hpp" // guard, matches, image_base

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

	// The bytes as the patch left them: the expected ones with the replacement in place (what may be
	// put back when the in-game rows return FrameRate to the game's own cap).
	inline bool matches_patched(const code_patch& patch, const std::uint8_t* bytes)
	{
		auto patched = patch.expected;
		std::memcpy(patched.data() + patch.offset, patch.replacement.data(), patch.replacement.size());
		return std::memcmp(bytes, patched.data(), patched.size()) == 0;
	}

	// What the fix paces at when the setting changes while the game runs. Back to the game's own
	// cap (no FrameRate) the spin is put back where it can be; where it can't (spin_is_off), the
	// fix paces at the game's 60 itself until the next start, so frames never run unpaced.
	inline unsigned live_target(const cap& setting, const unsigned desktop_refresh, const bool window_vsync, const bool spin_is_off)
	{
		if (!disables_stock_cap(setting))
		{
			return spin_is_off ? stock_fps : 0;
		}
		return effective_target(setting, desktop_refresh, window_vsync);
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

	// ---- Menus, popups and conversations at 60 fps ------------------------------------------------
	//
	// Read in the retail XMen2.exe for this (Ghidra output in docs/research/decomp_fps1-8.c): the
	// menu layer runs on time, not on frames. CClient::frame hands the menu manager (CMenuMgr, the
	// singleton at [0x8aff18]) the frame's real dt every frame (0x4021c5 -> vt+0x2c, 0x5d4570:
	// +0x86048 = dt, +0x86044 += dt), and what moves in a menu reads those two:
	//   - the navigation auto-repeat (mgr vt+0x1f8, 0x5da1a0, used by every menu, popup and
	//     conversation choice): a direction fires on its press, again once it has been held 0.3 s
	//     (0x3e99999a, the default at 0x5da1f2) and every 0.1 s after that (0x6e70a4), twice as fast
	//     on a list after 1.4 s (0x68602c) - all sums of dt;
	//   - model items (0x5c4cf0: loops and one-shots advance by dt / length), their rigid animations
	//     (CRigidAnimCtrl 0x5721f0: bias + the menu time, or Alchemy's global timer), the text glow
	//     (0x5d8ae0: sin of the menu time), text fades (0x5c6030: dt), the main menu's idle timer
	//     (0x5c9640, +0x192c += dt), menu effects (0x5d6d60: the effect manager gets max(dt, 1 ms)),
	//     the Cerebro backdrop's camera path (0x447730: game time minus its start) and a held mouse
	//     button (0x61a600: 0.2 s of game time). The conversation's choices (0x45d1a0) use the same
	//     navigation; the HUD (0x59f1a0) hands its parts a time as well.
	// What does depend on the frame rate is the sampling: a direction fires on a frame where it is
	// down and wasn't the frame before (the controller's edge, 0x550b70), with no debounce, and the
	// sticks fire on crossing +-0.5 (0x682ff4 / 0x680488) with no hysteresis. At 180 fps the game
	// looks three times as often, so a d-pad's contact bounce or a stick resting near the threshold
	// is seen as a second press - the highlight skips an item. The game was only ever tuned at 60,
	// and nothing in a menu gains from more, so while one is on screen the fix paces at 60 (or at
	// FrameRate when that is lower) and goes back to FrameRate in play.
	//
	// "On screen" is the game's own test: CClient::frame skips its pause-button handling when a
	// popup is up (popup manager [0x8b13ec] vt+0x78, 0x5e9e30), a menu is up (menu manager vt+0x204,
	// 0x5d8870) or a conversation is (conversation system [0x717aac] vt+0x20, 0x458010) - 0x401ef8.
	// The fix reads the same fields those three functions read; xml2_test runs the game's functions
	// on the same memory to prove the reading equal. Two menus keep FrameRate: a movie (menu manager
	// vt+0x1b4, 0x5d8420 - the stock loop skips its 60 fps spin for movies too, 0x401fb1) and the
	// loading screen (the current menu is a CMenuLoading, vtable 0x69fa1c): it has no input, and
	// pacing it lower would only slow the load.

	constexpr unsigned menu_fps = 60;

	// Where the game keeps the three objects, and what their functions read.
	constexpr DWORD menu_manager_cell = 0x8aff18;  // CMenuMgr*, set by 0x5d88b0 (the getter 0x5d8920 makes it when null)
	constexpr DWORD popups_cell = 0x8b13ec;        // the popup dialogs, 0x5eb290 / 0x5eb300
	constexpr DWORD conversations_cell = 0x717aac; // CConversationSystem*, 0x458380 / 0x4583f0
	constexpr DWORD menu_manager_size = 0xab254;   // what 0x5d88b0 allocates
	constexpr DWORD popups_size = 0x4058;          // 0x5eb290
	constexpr DWORD conversations_size = 0x239dc;  // 0x458380

	constexpr DWORD menu_stack_count = 0x86068; // 0x5d8870: > 0,
	constexpr DWORD menu_stack_first = 0x8605c; // its first entry set,
	constexpr DWORD menu_current = 0x86090;     // and a current menu,
	constexpr DWORD menu_pending = 0x85db0;     // or the name of one about to open (a C string)
	constexpr DWORD menu_flags = 0x85cec;       // 0x5d8420: bit 5 = a movie plays
	constexpr std::uint8_t menu_movie_bit = 0x20;
	constexpr DWORD loading_menu_vtable = 0x69fa1c; // CMenuLoading (set at 0x5b81e3)

	constexpr DWORD popup_current = 0x403c; // 0x5e9e30: an index 0..2 (out of range: 0),
	constexpr DWORD popup_first = 0x18;     // popup i at +0x18 + i * 0x1560,
	constexpr DWORD popup_stride = 0x1560;
	constexpr DWORD popup_active = 0x155d; // its byte +0x155d bit 0 = shown
	constexpr int popup_count = 3;

	constexpr DWORD conversation_flags = 0x21b24; // 0x458010: bit 1 = a conversation is on screen (0x4556b0 sets it)
	constexpr std::uint8_t conversation_bit = 0x02;

	template <typename T>
	T field_of(const std::uint8_t* object, const DWORD offset)
	{
		T value{};
		std::memcpy(&value, object + offset, sizeof(value));
		return value;
	}

	// 0x5d8870, CMenuMgr vt+0x204.
	inline bool menu_up(const std::uint8_t* manager)
	{
		return field_of<std::int32_t>(manager, menu_stack_count) > 0 && field_of<std::uint32_t>(manager, menu_stack_first) != 0 &&
		       (field_of<std::uint32_t>(manager, menu_current) != 0 || manager[menu_pending] != 0);
	}

	// 0x5d8420, CMenuMgr vt+0x1b4.
	inline bool movie_playing(const std::uint8_t* manager)
	{
		return (manager[menu_flags] & menu_movie_bit) != 0;
	}

	// Whether the current menu is the loading screen: `vtable_of` reads the object's first dword (the
	// fix reads it in the game, xml2_test in its own blocks).
	template <typename ReadVtable>
	bool loading_screen(const std::uint8_t* manager, ReadVtable vtable_of)
	{
		const auto current = field_of<std::uint32_t>(manager, menu_current);
		return current != 0 && vtable_of(current) == loading_menu_vtable;
	}

	// 0x5e9e30, the popup manager's vt+0x78: the current popup's bit, popup 0's when the index is out of range.
	inline bool popup_up(const std::uint8_t* popups)
	{
		auto index = field_of<std::int32_t>(popups, popup_current);
		if (index < 0 || index >= popup_count)
		{
			index = 0;
		}
		return (popups[popup_first + static_cast<DWORD>(index) * popup_stride + popup_active] & 1) != 0;
	}

	// 0x458010, CConversationSystem vt+0x20.
	inline bool conversation_up(const std::uint8_t* conversations)
	{
		return (conversations[conversation_flags] & conversation_bit) != 0;
	}

	struct screen
	{
		bool menu = false;
		bool popup = false;
		bool conversation = false;
		bool movie = false;
		bool loading = false;
		bool operator==(const screen&) const = default;
	};

	// A menu, popup or conversation that runs at 60: not a movie, not the loading screen.
	inline bool at_menu_rate(const screen& now)
	{
		return (now.menu || now.popup || now.conversation) && !now.movie && !now.loading;
	}

	// What the fix paces at: `target` (0 = not at all) in play, 60 on such a screen - or less, when
	// FrameRate is lower.
	inline unsigned paced_fps(const unsigned target, const bool menu_rate)
	{
		if (!menu_rate)
		{
			return target;
		}
		return target && target < menu_fps ? target : menu_fps;
	}

	// Whether the screens make a difference to `target`: FrameRate above 60, or unlimited.
	inline bool menus_differ(const unsigned target)
	{
		return paced_fps(target, true) != target;
	}

	inline std::string describe_screen(const screen& now)
	{
		if (now.movie) return "a movie";
		if (now.loading) return "the loading screen";
		if (now.popup) return "a popup";
		if (now.menu) return "a menu";
		if (now.conversation) return "a conversation";
		return "play";
	}

	using limits_rules::guard;
	using limits_rules::image_base;

	// Every byte the reading relies on, from the retail build: the frame's own test, the three getters
	// and the constructors behind them (so the objects are of these classes), the vtable slots, the
	// functions whose fields are read, and CMenuLoading's vtable. All must match, or the screens aren't
	// read and every frame is paced at FrameRate, as before.
	inline constexpr std::array<guard, 23> screen_guards{{
		{0x401ef8, "e803941e008b108bc8ff527884c07565e8136a1d008b108bc8ff920402000084c07552e8d06405008b108bc8ff522084c07542",
		 "CClient::frame: no popup, no menu, no conversation - the game's own test (0x401ef8)"},
		{0x401fa8, "e873691d008b108bc8ff92b401000084c07557", "CClient::frame: its 60 fps spin skipped while a movie plays (0x401fa8)"},
		// The menu manager.
		{0x5d8920, "a118ff8a0085c0750ae882ffffffa118ff8a00c3", "the menu manager's getter reads [0x8aff18] (0x5d8920)"},
		{0x5d88c6, "6854b20a00e8f296090083c40489042485c0c744240c00000000741b8bc8e847f6ffffa318ff8a00",
		 "a 0xab254-byte CMenuMgr made by 0x5d7f30 goes to [0x8aff18] (0x5d88c6)"},
		{0x5d7f4b, "c7066c236a00", "0x5d7f30 sets the vtable 0x6a236c (0x5d7f4b)"},
		{0x6a2570, "70885d00", "CMenuMgr vt+0x204 = 0x5d8870 (0x6a2570)"},
		{0x6a2520, "20845d00", "CMenuMgr vt+0x1b4 = 0x5d8420 (0x6a2520)"},
		{0x5d8870, "8b816860080085c07e2d8b815c60080085c074238b819060080085c075168d81b05d08008d50018a084084c975f92bc285c07e03b001c332c0c3",
		 "a menu is up: stack count, first entry, current menu or a pending name (0x5d8870)"},
		{0x5d8420, "8a81ec5c0800c0e8052401c3", "a movie plays: bit 5 of +0x85cec (0x5d8420)"},
		// The popups.
		{0x5eb300, "a1ec138b0085c0750ae882ffffffa1ec138b00c3", "the popup manager's getter reads [0x8b13ec] (0x5eb300)"},
		{0x5eb2a8, "6a216a0e6858400000e82a53f7ff83c40c8904248944240485c0c744241000000000741b8bc8e8bdfdffffa3ec138b00",
		 "a 0x4058-byte popup manager made by 0x5eb090 goes to [0x8b13ec] (0x5eb2a8)"},
		{0x5eb0a8, "c7062c336a00", "0x5eb090 sets the vtable 0x6a332c (0x5eb0a8)"},
		{0x6a33a4, "309e5e00", "the popup manager's vt+0x78 = 0x5e9e30 (0x6a33a4)"},
		{0x5e9e30, "8b813c40000085c07c1883f8037d1369c0601500008d4408188a805d1500002401c38d41188a805d1500002401c3",
		 "a popup is up: popup +0x403c's (0 when out of range) byte +0x155d, bit 0 (0x5e9e30)"},
		// The conversations.
		{0x4583f0, "a1ac7a710085c0750ae882ffffffa1ac7a7100c3", "the conversation system's getter reads [0x717aac] (0x4583f0)"},
		{0x45839c, "68dc390200e83a82100083c40c8904248944240485c0c744241000000000741b8bc8e8edfaffffa3ac7a7100",
		 "a 0x239dc-byte CConversationSystem made by 0x457eb0 goes to [0x717aac] (0x45839c)"},
		{0x457ecf, "c706045e6800", "0x457eb0 sets the vtable 0x685e04 (0x457ecf)"},
		{0x685e24, "10804500", "CConversationSystem vt+0x20 = 0x458010 (0x685e24)"},
		{0x458010, "8a81241b0200d0e82401c3", "a conversation is on screen: bit 1 of +0x21b24 (0x458010)"},
		// The loading screen's class.
		{0x5b81e3, "c7061cfa6900", "a CMenuLoading gets the vtable 0x69fa1c (0x5b81e3)"},
		{0x69fa18, "78926b00", "0x69fa1c's RTTI locator is 0x6b9278 (0x69fa18)"},
		{0x6b9284, "28566e00", "whose type descriptor is 0x6e5628 (0x6b9284)"},
		{0x6e5628, "809c6a00000000002e3f4156434d656e754c6f6164696e674040", "the type .?AVCMenuLoading@@ (0x6e5628)"},
	}};

	// The first guard `image` (XMen2.exe's image base: 0x400000 in the game) doesn't match, or null.
	inline const guard* first_screen_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : screen_guards)
		{
			if (!limits_rules::matches(image + (g.va - image_base), g.hex))
			{
				return &g;
			}
		}
		return nullptr;
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
