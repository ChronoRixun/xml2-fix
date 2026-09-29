#include "pad_input.hpp"
#include "log.hpp"
#include "virtual_pad.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <mutex>
#include <string>
#include <vector>

namespace pad_input
{
	namespace
	{
		using namespace pad_input_rules;
		using kind = pad_command::kind;

		constexpr DWORD read_wait_ms = 500; // for the game to read a pad once

		std::atomic<bool> enabled{false};

		std::mutex pads_mutex;
		std::array<synthetic_pad, max_pads> pads;

		struct read_counter
		{
			std::atomic<unsigned> reads{0};      // every device of the pad
			std::atomic<unsigned> game_reads{0}; // the game's own DirectInput 8 devices of it
			std::atomic<int> game_devices{0};
		};
		std::array<read_counter, max_pads> counters;

		HANDLE read_event()
		{
			static const HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr); // auto-reset: set on every read of any pad
			return event;
		}

		// The reads a tap waits for: the game's own when it has a device of that pad, else any.
		unsigned read_count(const int index)
		{
			const auto& counter = counters[index];
			return counter.game_devices.load() > 0 ? counter.game_reads.load() : counter.reads.load();
		}

		bool wait_for_read(const int index, const unsigned before, const DWORD timeout)
		{
			const ULONGLONG deadline = GetTickCount64() + timeout;
			for (;;)
			{
				if (read_count(index) != before)
				{
					return true;
				}
				const ULONGLONG now = GetTickCount64();
				if (now >= deadline)
				{
					return false;
				}
				WaitForSingleObject(read_event(), static_cast<DWORD>(std::min<ULONGLONG>(deadline - now, 50)));
			}
		}

		std::string side_text(const pad_command& command)
		{
			return std::string(command.side ? "right" : "left") + (command.what == kind::stick ? " stick" : " trigger");
		}

		std::string what_text(const pad_command& command)
		{
			char text[96];
			switch (command.what)
			{
			case kind::stick:
				std::snprintf(text, sizeof(text), "%s %.2f %.2f", side_text(command).c_str(), command.x, command.y);
				return text;
			case kind::trigger:
				std::snprintf(text, sizeof(text), "%s %.2f", side_text(command).c_str(), command.value);
				return text;
			default:
				return buttons_text(command.buttons);
			}
		}

		// Holds `command` on its pad until `until` (a tick count).
		void apply(const pad_command& command, const ULONGLONG until)
		{
			auto& pad = pads[command.pad - 1];
			std::lock_guard lock(pads_mutex);
			switch (command.what)
			{
			case kind::stick:
				pad.set_stick(command.side, command.x, command.y, until);
				break;
			case kind::trigger:
				pad.set_trigger(command.side, command.value, until);
				break;
			default:
				pad.press(command.buttons, until);
				break;
			}
		}

		void undo(const pad_command& command)
		{
			auto& pad = pads[command.pad - 1];
			std::lock_guard lock(pads_mutex);
			switch (command.what)
			{
			case kind::stick:
				pad.set_stick(command.side, 0, 0, 0);
				break;
			case kind::trigger:
				pad.set_trigger(command.side, 0, 0);
				break;
			default:
				pad.release(command.buttons);
				break;
			}
		}

		// Why the game didn't read pad `number` while the pipe held something on it.
		std::string unread_reason(const int number)
		{
			const int virtual_pads = virtual_pad::count();
			if (virtual_pads == 0)
			{
				return "no virtual pads ([Test] VirtualPads=N gives the game N pads) and no real pad " + std::to_string(number) + " read by the game";
			}
			if (number > virtual_pads)
			{
				return "[Test] VirtualPads=" + std::to_string(virtual_pads) + ": the game has pads 1 to " + std::to_string(virtual_pads);
			}
			return "the game isn't polling its pads (still starting?)";
		}

		// Down for `ms` and at least one read of the pad, then let go and read once more, so back-to-back
		// taps stay apart.
		std::string timed(const pad_command& command, const DWORD ms, const char* verb)
		{
			const int index = command.pad - 1;
			const unsigned before = read_count(index);
			apply(command, GetTickCount64() + ms + 2 * read_wait_ms); // expiry is a safety net; we let go explicitly
			Sleep(ms);
			const bool seen = wait_for_read(index, before, read_wait_ms);

			const unsigned at_release = read_count(index);
			undo(command);
			if (seen)
			{
				wait_for_read(index, at_release, read_wait_ms);
			}

			logger::write("test: pad %d %s %s %lu ms%s", command.pad, verb, what_text(command).c_str(), ms, seen ? "" : " - the game didn't read the pad meanwhile");
			return seen ? "ok" : "error the game didn't read pad " + std::to_string(command.pad) + " while it was held (" + unread_reason(command.pad) + ")";
		}
	}

	void enable()
	{
		enabled = true;
	}

	void on_read(const int index, xinput_pad::raw_state& state, const bool game)
	{
		if (!enabled.load() || index < 0 || index >= max_pads)
		{
			return;
		}
		std::vector<std::string> expired;
		{
			std::lock_guard lock(pads_mutex);
			pads[index].merge(state, GetTickCount64(), expired);
		}
		for (const auto& what : expired)
		{
			logger::write("test: pad %d %s let go - held for %lu ms without the client letting go", index + 1, what.c_str(), max_hold_ms);
		}

		auto& counter = counters[index];
		++counter.reads;
		if (game)
		{
			++counter.game_reads;
		}
		logger::write_once("pad-read:" + std::to_string(index), "test: the game reads pad %d - pipe pad commands reach it", index + 1);
		SetEvent(read_event());
	}

	void game_device(const int index, const bool created)
	{
		if (index >= 0 && index < max_pads)
		{
			counters[index].game_devices += created ? 1 : -1;
		}
	}

	std::string handle(const pad_command& command)
	{
		const int index = command.pad - 1;
		switch (command.what)
		{
		case kind::release:
		{
			std::lock_guard lock(pads_mutex);
			for (int i = 0; i < max_pads; ++i)
			{
				if (command.pad == 0 || i == index)
				{
					pads[i].release_all();
				}
			}
			logger::write(command.pad ? "test: pad %d: let go of everything" : "test: every pad: let go of everything", command.pad);
			return "ok";
		}
		case kind::up:
		{
			std::lock_guard lock(pads_mutex);
			pads[index].release(command.buttons);
			logger::write("test: pad %d up %s", command.pad, buttons_text(command.buttons).c_str());
			return "ok";
		}
		case kind::down:
		{
			const DWORD ms = command.ms ? command.ms : max_hold_ms;
			apply(command, GetTickCount64() + ms);
			logger::write("test: pad %d down %s (%lu ms at most)", command.pad, buttons_text(command.buttons).c_str(), ms);
			return "ok";
		}
		case kind::tap:
			return timed(command, command.ms ? command.ms : default_tap_ms, "tap");
		case kind::hold:
			return timed(command, command.ms, "hold");
		case kind::stick:
		case kind::trigger:
		{
			const bool centred = command.what == kind::stick ? (command.x == 0 && command.y == 0) : command.value == 0;
			if (centred || !command.ms)
			{
				apply(command, GetTickCount64() + max_hold_ms); // 0: lets go
				logger::write("test: pad %d %s%s", command.pad, what_text(command).c_str(), centred ? " (let go)" : " (until the next one, 10 s at most)");
				return "ok";
			}
			return timed(command, command.ms, "hold");
		}
		default:
			return "error not a pad command";
		}
	}

	void release_all()
	{
		std::lock_guard lock(pads_mutex);
		for (auto& pad : pads)
		{
			pad.release_all();
		}
	}

	std::string status()
	{
		std::string reads;
		for (const auto& counter : counters)
		{
			reads += (reads.empty() ? "" : "/") + std::to_string(counter.reads.load());
		}
		int held = 0;
		{
			std::lock_guard lock(pads_mutex);
			for (const auto& pad : pads)
			{
				held += pad.held();
			}
		}
		return "virtual pads " + std::to_string(virtual_pad::count()) + "; pad reads " + reads + "; pad inputs held " + std::to_string(held);
	}
}
