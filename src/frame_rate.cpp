#include "frame_rate.hpp"
#include "log.hpp"

#include <Windows.h>
#include <timeapi.h>

#include <atomic>
#include <cstring>

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
		bool spin_tried = false;    // the patch was attempted (once is enough: the bytes don't change)
		LONGLONG frequency = 0;
		LONGLONG margin = 0; // ticks the timer is trusted to within
		pacer pace;
		fps_counter counter;
		std::atomic<unsigned> fps_x10{0};
		HANDLE timer = nullptr;
		bool high_resolution = false;
		unsigned seconds = 0;

		LONGLONG now()
		{
			LARGE_INTEGER count;
			QueryPerformanceCounter(&count);
			return count.QuadPart;
		}

		// No C++ objects here: the read is guarded, in case this isn't XMen2.exe at all.
		bool bytes_match(const code_patch& patch, const std::uint8_t* at)
		{
			__try
			{
				return matches(patch, at);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		bool apply(const HMODULE game, const code_patch& patch)
		{
			auto* at = reinterpret_cast<std::uint8_t*>(game) + patch.rva;
			if (!bytes_match(patch, at))
			{
				logger::write("frame rate: XMen2.exe doesn't have the expected code for %s (not the retail build?) - left alone", patch.what);
				return false;
			}
			auto* target_bytes = at + patch.offset;
			DWORD old_protect = 0;
			if (!VirtualProtect(target_bytes, patch.replacement.size(), PAGE_EXECUTE_READWRITE, &old_protect))
			{
				logger::write("frame rate: can't unprotect %s (error %lu) - left alone", patch.what, GetLastError());
				return false;
			}
			std::memcpy(target_bytes, patch.replacement.data(), patch.replacement.size());
			VirtualProtect(target_bytes, patch.replacement.size(), old_protect, &old_protect);
			FlushInstructionCache(GetCurrentProcess(), target_bytes, patch.replacement.size());
			return true;
		}

		// Switches the game's spin off, once; false when the code isn't the retail build's.
		bool switch_spin_off()
		{
			if (!spin_tried)
			{
				spin_tried = true;
				spin_off = apply(game_module, stock_cap_patch);
			}
			return spin_off;
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

		if (!disables_stock_cap(setting))
		{
			logger::write("frame rate: the game's own 60 fps cap (no [Display] FrameRate in xml2-fix.ini)%s",
			              window_vsync ? "; VSync=1 in a window would pace at the desktop's refresh rate, but never above FrameRate" : "");
			return;
		}

		switch_spin_off();
		target = effective_target(setting, refresh, window_vsync);
		synced_window = window_vsync && target != target_fps(setting, refresh);
		if (target)
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
			logger::write("frame rate: %s, paced by the fix (%s timer + spin); the game's 60 fps spin is off", describe().c_str(),
			              high_resolution ? "high-resolution" : "1 ms");
		}
		else
		{
			logger::write("frame rate: unlimited; the game's 60 fps spin is off");
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
		target = effective_target(setting, refresh, window_vsync);
		synced_window = window_vsync && target != target_fps(setting, refresh);
		if (target)
		{
			prepare_timer();
			pace.set_interval(ticks_for_fps(target, frequency));
		}
		const char* how = "";
		if (!disables_stock_cap(setting))
		{
			how = spin_off ? " (the game's spin is already off; nothing paces)" : "";
		}
		else if (!spin_off)
		{
			how = " - but the game's 60 fps spin stays (not the retail build), so FrameRate can't raise it";
		}
		else
		{
			how = target ? ", paced by the fix from the next frame" : "; nothing paces";
		}
		logger::write("frame rate: now %s%s", describe().c_str(), how);
	}

	bool paces()
	{
		return target != 0;
	}

	void on_present()
	{
		if (!frequency)
		{
			return;
		}
		LONGLONG time = now();
		if (target)
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
			if (seconds == 2 || seconds == 30) // once early, once settled; the pipe's "status" has it live
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
		if (synced_window)
		{
			return std::to_string(target) + " fps (the desktop's refresh rate, for VSync in a window; FrameRate " + frame_rate_rules::describe(setting, refresh) + ")";
		}
		return frame_rate_rules::describe(setting, refresh);
	}
}
