#include "log.hpp"
#include "online_security.hpp"

#include <Windows.h>

#include <cstdarg>
#include <cstdio>
#include <map>
#include <mutex>
#include <string>
#include <unordered_set>
#include <vector>

namespace logger
{
	namespace
	{
		std::mutex mutex;
		std::FILE* file = nullptr;
		std::uint64_t current_bytes = 0;
		std::unordered_set<std::string> seen_keys;
		std::map<std::string, rate_gate> capped;

		void write_literal(const std::string& text)
		{
			const auto path = module_dir() / FIX_LOG_FILE;
			if (!file)
			{
				file = _wfopen(path.c_str(), L"w");
				current_bytes = 0;
				if (!file) return;
			}
			SYSTEMTIME time{};
			GetLocalTime(&time);
			char stamp[32];
			std::snprintf(stamp, sizeof(stamp), "[%02u:%02u:%02u.%03u] ", time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
			std::string line = stamp + online_security::sanitize_log_text(text) + "\n";
			if (rotation_needed(current_bytes, line.size()))
			{
				std::fclose(file);
				file = nullptr;
				auto previous = path;
				previous += ".old";
				MoveFileExW(path.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
				file = _wfopen(path.c_str(), L"w");
				current_bytes = 0;
				if (!file) return;
			}
			const auto written = std::fwrite(line.data(), 1, line.size(), file);
			current_bytes += written;
			std::fflush(file);
		}

		void write_line(const char* fmt, va_list args)
		{
			va_list measure;
			va_copy(measure, args);
			const int needed = _vscprintf(fmt, measure);
			va_end(measure);
			if (needed < 0) return;
			std::vector<char> text(static_cast<std::size_t>(needed) + 1);
			std::vsnprintf(text.data(), text.size(), fmt, args);
			write_literal(std::string(text.data(), static_cast<std::size_t>(needed)));
		}
	}

	const std::filesystem::path& module_dir()
	{
		static const std::filesystem::path dir = []
		{
			HMODULE self = nullptr;
			GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
			                   reinterpret_cast<LPCWSTR>(&module_dir), &self);

			std::wstring buffer(MAX_PATH, L'\0');
			const auto length = GetModuleFileNameW(self, buffer.data(), static_cast<DWORD>(buffer.size()));
			buffer.resize(length);
			return std::filesystem::path(buffer).parent_path();
		}();
		return dir;
	}

	void write(const char* fmt, ...)
	{
		std::lock_guard lock(mutex);
		va_list args;
		va_start(args, fmt);
		write_line(fmt, args);
		va_end(args);
	}

	void write_capped(const std::string_view key, const char* fmt, ...)
	{
		std::lock_guard lock(mutex);
		const auto now = GetTickCount64();
		auto gate = capped.try_emplace(std::string(key), 20, 20, now).first;
		if (!gate->second.allow(now)) return;
		if (const auto suppressed = gate->second.take_suppressed())
			write_literal("log: " + std::string(key) + " suppressed " + std::to_string(suppressed) + " messages");
		va_list args;
		va_start(args, fmt);
		write_line(fmt, args);
		va_end(args);
	}

	void write_once(const std::string_view key, const char* fmt, ...)
	{
		std::lock_guard lock(mutex);
		if (!seen_keys.emplace(key).second)
		{
			return;
		}

		va_list args;
		va_start(args, fmt);
		write_line(fmt, args);
		va_end(args);
	}
}
