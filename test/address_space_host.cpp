// address_space.cpp in a large-address-aware 32-bit host of its own (E12), without the game: the flag read from the
// exe header, the 2 GB guard, the status words before and after memory is taken above 2 GB, the pipe's high-address
// test (allocations then land above 2 GB) and the crash line an unhandled access violation writes to the log
// (a child copy of this program crashes on purpose, with Windows' crash dialog off). Needs 64-bit Windows (or Wine).
#include "address_space.hpp"
#include "log.hpp"

#include <Windows.h>

#include <cstdio>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
	int failures = 0;
	void check(bool ok, const std::string& what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
		failures += ok ? 0 : 1;
	}
	std::uint32_t address_of(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

	std::string read_log()
	{
		std::ifstream in(logger::module_dir() / L"address-space-test.log", std::ios::binary);
		std::stringstream s;
		s << in.rdbuf();
		return s.str();
	}
}

int main(int argc, char** argv)
{
	std::setbuf(stdout, nullptr);
	const HMODULE self = GetModuleHandleW(nullptr);
	if (argc > 1 && std::string(argv[1]) == "crash")
	{
		SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
		address_space::install(self);
		volatile int* nowhere = reinterpret_cast<volatile int*>(static_cast<std::uintptr_t>(0x00000010));
		*nowhere = 1;
		return 0;
	}
	SYSTEM_INFO system{};
	GetSystemInfo(&system);
	if (address_of(system.lpMaximumApplicationAddress) < 0x80000000u)
	{
		std::printf("SKIP this host has no address space above 2 GB (32-bit Windows, or the flag was not linked)\n");
		return 0;
	}
	DeleteFileW((logger::module_dir() / L"address-space-test.log").c_str());

	address_space::install(self);
	check(std::string(address_space::laa()) == "on", "the host's own header: laa on");
	const auto start = address_space::status();
	check(start.rfind("laa on; top 0x", 0) == 0, "status: " + start);
	MEMORY_BASIC_INFORMATION guard{};
	VirtualQuery(reinterpret_cast<void*>(static_cast<std::uintptr_t>(0x7FFF0000u)), &guard, sizeof(guard));
	check(start.find("2 GB guard on") != std::string::npos ? guard.State == MEM_RESERVE : start.find("2 GB guard unavailable") != std::string::npos,
	      "the guard block is reserved (or reported unavailable)");

	void* high = VirtualAlloc(nullptr, 64u << 20, MEM_RESERVE | MEM_COMMIT | MEM_TOP_DOWN, PAGE_READWRITE);
	check(high && address_of(high) >= 0x80000000u, "a top-down block lands above 2 GB");
	const auto with_high = address_space::status();
	check(with_high.find("above 2 GB 0 MB") == std::string::npos && with_high.find("above 2 GB") != std::string::npos, "status counts it: " + with_high);

	const auto reply = address_space::pipe("high 64");
	check(reply.rfind("ok reserved ", 0) == 0, "memory high 64: " + reply);
	void* next = VirtualAlloc(nullptr, 256u << 20, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
	char text[64];
	std::snprintf(text, sizeof(text), "%08X", address_of(next));
	check(next && address_of(next) >= 0x80000000u, std::string("after the test a bottom-up 256 MB block lands above 2 GB: 0x") + text);
	check(address_space::status().find("high-address test") != std::string::npos, "status names the test's reservation");
	check(address_space::pipe("high nope").rfind("error", 0) == 0 && address_space::pipe("what").rfind("error", 0) == 0, "bad arguments are refused");

	// the crash line, from a child that crashes on purpose
	char exe[MAX_PATH]{};
	GetModuleFileNameA(nullptr, exe, MAX_PATH);
	std::string command = std::string("\"") + exe + "\" crash";
	STARTUPINFOA si{sizeof(si)};
	PROCESS_INFORMATION pi{};
	const UINT previous = SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	const bool started = CreateProcessA(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
	SetErrorMode(previous);
	DWORD code = 0;
	if (started)
	{
		WaitForSingleObject(pi.hProcess, 30000);
		GetExitCodeProcess(pi.hProcess, &code);
		CloseHandle(pi.hThread);
		CloseHandle(pi.hProcess);
	}
	check(started && code == static_cast<DWORD>(EXCEPTION_ACCESS_VIOLATION), "the child crashed with an access violation");
	const auto log = read_log();
	const auto line = log.find("crash: exception 0xc0000005 at 0x");
	check(line != std::string::npos && log.find("writing 0x00000010", line) != std::string::npos && log.find("; laa on;", line) != std::string::npos,
	      "the crash line names the fault, the address written and the flag");
	check(log.find("address space: laa on; 2 GB guard") != std::string::npos, "the start line");

	std::printf("%s (%d failures)\n", failures ? "FAILED" : "ALL PASS", failures);
	return failures ? 1 : 0;
}
