#include "online_tick.hpp"
#include "frame_rate_rules.hpp"
#include "ini.hpp"
#include "log.hpp"

#include <cstring>

namespace online_tick
{
	namespace
	{
		using namespace frame_rate_rules;
		tick_setting setting;
		unsigned in_effect = stock_online_fps;

		bool read_matches(const std::uint8_t* at, const bool patched)
		{
			__try
			{
				return patched ? matches_patched(online_tick_patch, at) : matches(online_tick_patch, at);
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return false;
			}
		}

		// Writes the online 1/60 after checking the 16 bytes, reads it back, and puts the original back if the
		// read-back differs.
		bool patch(const HMODULE game)
		{
			auto* at = reinterpret_cast<std::uint8_t*>(game) + online_tick_patch.rva;
			if (!read_matches(at, false))
			{
				logger::write("online tick rate: XMen2.exe doesn't have the retail build's code for %s - online stays at 30 fps", online_tick_patch.what);
				return false;
			}
			auto* field = at + online_tick_patch.offset;
			DWORD old_protect = 0;
			if (!VirtualProtect(field, online_tick_patch.replacement.size(), PAGE_EXECUTE_READWRITE, &old_protect))
			{
				logger::write("online tick rate: can't unprotect %s (error %lu) - online stays at 30 fps", online_tick_patch.what, GetLastError());
				return false;
			}
			std::memcpy(field, online_tick_patch.replacement.data(), online_tick_patch.replacement.size());
			const bool ok = read_matches(at, true);
			if (!ok)
			{
				std::memcpy(field, online_tick_patch.expected.data() + online_tick_patch.offset, online_tick_patch.replacement.size());
			}
			VirtualProtect(field, online_tick_patch.replacement.size(), old_protect, &old_protect);
			FlushInstructionCache(GetCurrentProcess(), field, online_tick_patch.replacement.size());
			if (!ok)
			{
				logger::write("online tick rate: the patch didn't read back - put back, online stays at 30 fps");
			}
			return ok;
		}
	}

	void install(const HMODULE game)
	{
		setting = parse_tick_rate(ini::text(L"Online", L"TickRate"));
		if (!setting.requested)
		{
			return; // the game's own 30, no agreement entry
		}
		if (!setting.valid)
		{
			logger::write("online tick rate: [Online] TickRate is not 60 or 30 - online runs at the game's 30 fps (gameplay agreement: TickRate 30)");
		}
		const bool patched = setting.valid && setting.fps == 60 && patch(game);
		in_effect = effective_tick_rate(setting, patched);
		if (in_effect == 60)
		{
			logger::write("online tick rate: online sessions run at 60 fps (the game's online 1/30 s frame is 1/60 s); every PC in a session must have TickRate=60 (gameplay agreement rule 29)");
		}
		else if (setting.valid)
		{
			logger::write("online tick rate: online sessions run at %s30 fps; every PC in a session must run at 30 (gameplay agreement rule 29)", setting.fps == 60 ? "the fallback " : "");
		}
	}

	bool requested() { return setting.requested; }
	unsigned rate() { return in_effect; }
	std::array<std::uint32_t, 4> agreement_values() { return tick_rate_values(in_effect); }
	std::size_t frames_for(const std::size_t count_at_30) { return frame_rate_rules::frames_for(count_at_30, in_effect); }
}
