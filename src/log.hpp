#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

namespace logger
{
	class rate_gate
	{
		unsigned rate_, burst_;
		double tokens_;
		std::uint64_t at_, suppressed_ = 0;
	public:
		rate_gate(unsigned rate, unsigned burst, std::uint64_t now) : rate_(rate), burst_(burst), tokens_(burst), at_(now) {}
		bool allow(std::uint64_t now)
		{
			if (now > at_)
			{
				tokens_ += double(now - at_) * rate_ / 1000.0;
				if (tokens_ > burst_) tokens_ = burst_;
				at_ = now;
			}
			if (tokens_ < 1.0) { ++suppressed_; return false; }
			tokens_ -= 1.0;
			return true;
		}
		std::uint64_t suppressed() const { return suppressed_; }
		std::uint64_t take_suppressed() { const auto n = suppressed_; suppressed_ = 0; return n; }
	};

	constexpr std::uint64_t log_size_cap = 8 * 1024 * 1024;
	constexpr bool rotation_needed(std::uint64_t current, std::uint64_t incoming, std::uint64_t cap = log_size_cap)
	{
		return current && (current >= cap || incoming > cap - current);
	}

	// Directory containing this DLL (the game folder once installed).
	const std::filesystem::path& module_dir();

	void write(const char* fmt, ...);
	void write_capped(std::string_view key, const char* fmt, ...);

	// Logs the message only the first time `key` is seen. Used for per-frame calls.
	void write_once(std::string_view key, const char* fmt, ...);
}
