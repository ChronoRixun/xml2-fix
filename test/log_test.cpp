#include <Windows.h>
#include "log.hpp"

#include <cstdio>
#include <filesystem>
#include <string>

namespace
{
	int failures = 0, passes = 0;
	void check(bool ok, const char* what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
		ok ? ++passes : ++failures;
	}
}

int main()
{
	logger::rate_gate gate(20, 2, 0);
	check(gate.allow(0) && gate.allow(0) && !gate.allow(0) && !gate.allow(25),
	      "rate gate: the burst is capped and insufficient refill keeps rejecting");
	check(gate.suppressed() == 2 && gate.allow(50) && gate.take_suppressed() == 2 && gate.take_suppressed() == 0,
	      "rate gate: accepted traffic after suppression emits one exact suppressed count");
	constexpr std::uint64_t cap = 8 * 1024 * 1024;
	check(!logger::rotation_needed(0, cap) && !logger::rotation_needed(cap - 10, 10) && logger::rotation_needed(cap - 10, 11) &&
	      logger::rotation_needed(cap, 1) && !logger::rotation_needed(0, cap + 1),
	      "rotation: exact fits stay, boundary-plus-one rotates, an empty current file is never rotated");

	const auto path = logger::module_dir() / FIX_LOG_FILE;
	auto previous = path;
	previous += ".old";
	std::filesystem::remove(path);
	std::filesystem::remove(previous);
	const std::string chunk(1024 * 1024, 'x');
	for (int i = 0; i < 8; ++i) logger::write("%s", chunk.c_str());
	const bool rotated = std::filesystem::exists(previous) && std::filesystem::exists(path);
	check(rotated && std::filesystem::file_size(previous) >= 7 * cap / 8 && std::filesystem::file_size(path) < cap / 4,
	      "rotation: crossing 8 MiB keeps one full-ish previous generation and a small current log");
	for (int i = 0; i < 25; ++i) logger::write_capped("log-test-capped", "capped line");
	Sleep(60);
	logger::write_capped("log-test-capped", "after refill");
	logger::write("broker text %s", "bad\r\nline\x01!");
	bool suppressed_summary = false, sanitized = false;
	{
		const auto size = static_cast<size_t>(std::filesystem::file_size(path));
		std::string data(size, '\0');
		if (FILE* in = _wfopen(path.c_str(), L"rb"))
		{
			const auto bytes = std::fread(data.data(), 1, size, in);
			data.resize(bytes);
			fclose(in);
		}
		suppressed_summary = data.find("suppressed 5") != std::string::npos;
		sanitized = data.find("broker text badline!") != std::string::npos && data.find("broker text bad\r\nline") == std::string::npos;
	}
	check(suppressed_summary, "capped writes: a later accepted line reports the suppressed count");
	check(sanitized, "log output: remote-controlled CR/LF/control bytes cannot inject log lines");
	std::printf("%d PASS / %d FAIL\n", passes, failures);
	return failures ? 1 : 0;
}
