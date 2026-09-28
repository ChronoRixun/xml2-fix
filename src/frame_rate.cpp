#include "frame_rate.hpp"
#include "log.hpp"

#include <Windows.h>
#include <timeapi.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace frame_rate
{
	namespace
	{
		using namespace frame_rate_rules;

		HMODULE game_module = nullptr;
		cap setting;
		unsigned refresh = 0;
		bool synced_window = false; // VSync=1 in a window: paced at the refresh rate
		unsigned target = 0;        // fps; 0 = no pacing
		bool spin_off = false;      // the game's 60 fps spin is patched away
		bool spin_refused = false;  // the code isn't the retail build's: never patched (the bytes don't change)
		LONGLONG frequency = 0;
		LONGLONG margin = 0; // ticks the timer is trusted to within
		pacer pace;
		fps_counter counter;
		std::atomic<unsigned> fps_x10{0};
		HANDLE timer = nullptr;
		bool high_resolution = false;
		unsigned seconds = 0; // since the start or the last change: the fps is logged after the 2nd and the 30th

		// Menus, popups and conversations at 60 (frame_rate_rules.hpp).
		const guard* screen_mismatch = nullptr; // the first retail byte that isn't: the screens aren't read
		bool screens_checked = false;
		bool screens_known = false; // every guard matched
		bool screens_failed = false; // a read faulted: not read again
		unsigned paced = 0;          // what the pacer runs at now (0: not at all)
		std::atomic<bool> menu_rate{false}; // at 60 for a screen right now (the pipe's "status" reads it)
		unsigned switches_logged = 0;
		constexpr unsigned switches_to_log = 12;

		LONGLONG now()
		{
			LARGE_INTEGER count;
			QueryPerformanceCounter(&count);
			return count.QuadPart;
		}

		// No C++ objects here: the read is guarded, in case this isn't XMen2.exe at all.
		bool bytes_match(const code_patch& patch, const std::uint8_t* at, const bool patched)
		{
			__try
			{
				return patched ? matches_patched(patch, at) : matches(patch, at);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// Writes the replacement (undo = false) or puts the original bytes back (undo = true), after
		// checking the 16 bytes are exactly what that step expects.
		bool apply(const HMODULE game, const code_patch& patch, const bool undo)
		{
			auto* at = reinterpret_cast<std::uint8_t*>(game) + patch.rva;
			if (!bytes_match(patch, at, undo))
			{
				logger::write("frame rate: XMen2.exe doesn't have the %s code for %s - left alone", undo ? "patched" : "expected (retail build's)", patch.what);
				return false;
			}
			auto* target_bytes = at + patch.offset;
			const std::uint8_t* source = undo ? patch.expected.data() + patch.offset : patch.replacement.data();
			DWORD old_protect = 0;
			if (!VirtualProtect(target_bytes, patch.replacement.size(), PAGE_EXECUTE_READWRITE, &old_protect))
			{
				logger::write("frame rate: can't unprotect %s (error %lu) - left alone", patch.what, GetLastError());
				return false;
			}
			std::memcpy(target_bytes, source, patch.replacement.size());
			VirtualProtect(target_bytes, patch.replacement.size(), old_protect, &old_protect);
			FlushInstructionCache(GetCurrentProcess(), target_bytes, patch.replacement.size());
			return true;
		}

		// Switches the game's spin off; false when the code isn't the retail build's (tried once).
		bool switch_spin_off()
		{
			if (!spin_off && !spin_refused)
			{
				spin_off = apply(game_module, stock_cap_patch, false);
				spin_refused = !spin_off;
			}
			return spin_off;
		}

		// Puts the game's spin back (the rows returned FrameRate to the game's own cap). On the game's
		// thread, from the panel's Accept: the frame function wrote this frame's minimum already, the
		// next one writes 1/60 again.
		void switch_spin_on()
		{
			if (spin_off && apply(game_module, stock_cap_patch, true))
			{
				spin_off = false;
			}
		}

		void prepare_timer()
		{
			if (timer)
			{
				return;
			}
			timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
			high_resolution = timer != nullptr;
			if (!timer)
			{
				timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
				timeBeginPeriod(1); // the ordinary timer is only as fine as the scheduler's tick
			}
			margin = ticks_for_ms(high_resolution ? 0.6 : 2.0, frequency);
		}

		// ---- Which screen is up -----------------------------------------------------------------

		// No C++ objects here: the reads are guarded.
		const guard* first_screen_mismatch_guarded()
		{
			__try
			{
				return first_screen_mismatch(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(image_base)));
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return &screen_guards[0];
			}
		}

		// Once: whether this is the retail XMen2.exe whose menu, popup and conversation state the fix
		// knows how to read.
		void check_screens()
		{
			if (screens_checked)
			{
				return;
			}
			screens_checked = true;
			if (reinterpret_cast<std::uintptr_t>(game_module) != image_base)
			{
				return; // not XMen2.exe at 0x400000: screen_mismatch stays null
			}
			screen_mismatch = first_screen_mismatch_guarded();
			screens_known = screen_mismatch == nullptr;
		}

		std::uint32_t vtable_of(const std::uint32_t object)
		{
			return *reinterpret_cast<const std::uint32_t*>(static_cast<std::uintptr_t>(object));
		}

		template <typename T>
		const T* cell(const DWORD va)
		{
			return *reinterpret_cast<T* const*>(static_cast<std::uintptr_t>(va));
		}

		// What the game's own functions would answer this frame (0x5d8870, 0x5e9e30, 0x458010,
		// 0x5d8420); false if the memory couldn't be read.
		bool read_screen(screen& out)
		{
			__try
			{
				out = screen{};
				if (const auto* manager = cell<std::uint8_t>(menu_manager_cell))
				{
					out.menu = menu_up(manager);
					out.movie = movie_playing(manager);
					out.loading = loading_screen(manager, &vtable_of);
				}
				if (const auto* popups = cell<std::uint8_t>(popups_cell))
				{
					out.popup = popup_up(popups);
				}
				if (const auto* conversations = cell<std::uint8_t>(conversations_cell))
				{
					out.conversation = conversation_up(conversations);
				}
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// Whether the screens change anything: the fix paces (the game's spin is off), FrameRate is
		// above 60 or unlimited, and the state can be read.
		bool watches_screens()
		{
			return screens_known && !screens_failed && spin_off && menus_differ(target);
		}

		std::string fps_words(const unsigned fps)
		{
			return fps ? std::to_string(fps) + " fps" : std::string("unlimited");
		}

		// The setting in words, VSync in a window included.
		std::string describe_setting()
		{
			if (synced_window)
			{
				return std::to_string(target) + " fps (the desktop's refresh rate, for VSync in a window; FrameRate " + frame_rate_rules::describe(setting, refresh) + ")";
			}
			return frame_rate_rules::describe(setting, refresh);
		}

		// What the log says about the screens after a setting is applied.
		std::string screens_note()
		{
			if (!spin_off || !menus_differ(target))
			{
				return "";
			}
			if (watches_screens())
			{
				return "; menus, popups and conversations at 60 fps";
			}
			if (screens_failed)
			{
				return "; menus at " + fps_words(target) + " too - the game's menu state couldn't be read";
			}
			if (!screen_mismatch)
			{
				return "; menus at " + fps_words(target) + " too - XMen2.exe isn't loaded at 0x400000 (not the game?)";
			}
			char text[256];
			std::snprintf(text, sizeof(text), "; menus at %s too - XMen2.exe doesn't have the expected code at 0x%08lX (%s)", fps_words(target).c_str(), screen_mismatch->va,
			              screen_mismatch->what);
			return text;
		}

		// Sleeps on the timer for the bulk of the wait, then spins for the rest.
		void wait_until(const LONGLONG deadline)
		{
			for (;;)
			{
				const auto plan = plan_wait(deadline - now(), frequency, margin);
				if (!plan.spin)
				{
					return;
				}
				if (plan.timer_100ns > 0)
				{
					LARGE_INTEGER due;
					due.QuadPart = -plan.timer_100ns;
					if (timer && SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE))
					{
						WaitForSingleObject(timer, 100);
					}
					else
					{
						Sleep(1);
					}
					continue; // re-plan: what's left is the spin
				}
				while (now() < deadline)
				{
					YieldProcessor();
				}
				return;
			}
		}
	}

	void install(const HMODULE game, const cap& wanted, const unsigned desktop_refresh, const bool window_vsync)
	{
		game_module = game;
		setting = wanted;
		refresh = desktop_refresh;
		LARGE_INTEGER freq;
		QueryPerformanceFrequency(&freq);
		frequency = freq.QuadPart;
		check_screens();

		if (!disables_stock_cap(setting))
		{
			logger::write("frame rate: the game's own 60 fps cap (no [Display] FrameRate in xml2-fix.ini)%s",
			              window_vsync ? "; VSync=1 in a window would pace at the desktop's refresh rate, but never above FrameRate" : "");
			return;
		}

		switch_spin_off();
		target = effective_target(setting, refresh, window_vsync);
		synced_window = window_vsync && target != target_fps(setting, refresh);
		paced = target;
		if (target || watches_screens())
		{
			prepare_timer();
			pace.set_interval(ticks_for_fps(target, frequency));
		}

		if ((setting.what == cap::kind::refresh || window_vsync) && !desktop_refresh)
		{
			logger::write("frame rate: the desktop's refresh rate is unknown - taken as %u Hz", refresh_fallback_fps);
		}
		if (!spin_off)
		{
			logger::write("frame rate: the game's 60 fps cap stays%s", target && target < stock_fps ? "; the fix paces below it" : "; FrameRate can't raise it");
			return;
		}
		if (target)
		{
			logger::write("frame rate: %s, paced by the fix (%s timer + spin); the game's 60 fps spin is off%s", describe_setting().c_str(),
			              high_resolution ? "high-resolution" : "1 ms", screens_note().c_str());
		}
		else
		{
			logger::write("frame rate: unlimited; the game's 60 fps spin is off%s", screens_note().c_str());
		}
	}

	void retarget(const cap& wanted, const bool window_vsync)
	{
		if (!frequency)
		{
			return; // install never ran (the display fix couldn't hook the device): nothing to pace with
		}
		setting = wanted;
		if (disables_stock_cap(setting))
		{
			switch_spin_off();
		}
		else
		{
			switch_spin_on();
		}
		target = live_target(setting, refresh, window_vsync, spin_off);
		synced_window = window_vsync && disables_stock_cap(setting) && target != target_fps(setting, refresh);
		// The next frame starts a new cadence at the new rate; on_present moves it to 60 if a menu
		// is up (the change is made from one, the Advanced Options panel).
		paced = target;
		menu_rate = false;
		if (target || watches_screens())
		{
			prepare_timer();
		}
		pace.set_interval(ticks_for_fps(target, frequency));
		seconds = 0; // the fps is logged 2 and 30 seconds after the change
		const char* how = "";
		if (!disables_stock_cap(setting))
		{
			how = spin_off ? " - its spin couldn't be put back, so the fix paces at 60 until the next start" : " (its spin is back in charge; the fix doesn't pace)";
		}
		else if (!spin_off)
		{
			how = " - but the game's 60 fps spin stays (not the retail build), so FrameRate can't raise it";
		}
		else
		{
			how = target ? ", paced by the fix from the next frame" : "; nothing paces";
		}
		logger::write("frame rate: now %s%s%s", describe_setting().c_str(), how, screens_note().c_str());
	}

	bool paces()
	{
		return target != 0 || watches_screens();
	}

	void on_present()
	{
		if (!frequency)
		{
			return;
		}
		LONGLONG time = now();

		// A menu, popup or conversation: 60 fps; play: FrameRate.
		unsigned wanted = target;
		if (watches_screens())
		{
			screen shown;
			if (read_screen(shown))
			{
				const bool at_60 = at_menu_rate(shown);
				wanted = paced_fps(target, at_60);
				if (wanted != paced && switches_logged <= switches_to_log)
				{
					if (switches_logged == switches_to_log)
					{
						logger::write("frame rate: (further switches between menus and play aren't logged)");
					}
					else if (at_60)
					{
						logger::write("frame rate: %s while %s is on screen", fps_words(wanted).c_str(), describe_screen(shown).c_str());
					}
					else
					{
						logger::write("frame rate: %s again (%s)", fps_words(wanted).c_str(), describe_screen(shown).c_str());
					}
					++switches_logged;
				}
				menu_rate = at_60;
			}
			else
			{
				screens_failed = true;
				menu_rate = false;
				logger::write("frame rate: ERROR: the game's menu state couldn't be read - menus run at %s like play from now on", fps_words(target).c_str());
			}
		}
		if (wanted != paced)
		{
			paced = wanted;
			pace.set_interval(ticks_for_fps(paced, frequency)); // a new cadence from this frame
		}

		if (paced)
		{
			const LONGLONG deadline = pace.next(time);
			if (deadline > time)
			{
				wait_until(deadline);
				time = now();
			}
		}
		if (counter.count(time, frequency))
		{
			fps_x10.store(counter.fps_x10());
			++seconds;
			if (seconds == 2 || seconds == 30) // once early, once settled (after the start or a change); the pipe's "status" has it live
			{
				logger::write("frame rate: %s fps over the last second (%s)", fps_text(counter.fps_x10()).c_str(), describe().c_str());
			}
		}
	}

	unsigned measured_fps_x10()
	{
		return fps_x10.load();
	}

	std::string describe()
	{
		std::string text = describe_setting();
		if (watches_screens())
		{
			text += menu_rate ? " (60 now: a menu, popup or conversation is up)" : " (menus, popups and conversations at 60)";
		}
		return text;
	}
}
