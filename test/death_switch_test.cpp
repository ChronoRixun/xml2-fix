#include "death_switch.hpp"
#include "death_switch_rules.hpp"
#include <Windows.h>
#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// The production installer and hook against isolated settings/logger/startup providers, on the retail addresses
// (no game): the test's image is based at 0x300000 and a 6 MB fixture section of its own spans 0x420860..0x72988c,
// so that memory is the test's before anything else can map there. (The installer is handed 0x400000 as the game.) Optional argument: a retail XMen2.exe, read only, to check every guard.
namespace ini { bool requested = false; bool flag(const wchar_t*, const wchar_t*, bool) { return requested; } }
namespace logger
{
	std::vector<std::string> lines;
	void write(const char* format, ...) { va_list a; va_start(a, format); char b[512]; std::vsnprintf(b, sizeof(b), format, a); va_end(a); lines.emplace_back(b); }
}
namespace game_version { int refusals = 0; void refuse_startup() { ++refusals; } }
#pragma section(".fixture", read, execute)
__declspec(allocate(".fixture")) std::uint8_t fixture_space[0x600000]{};

using namespace death_switch_rules;
namespace
{
	int failures = 0;
	void check(bool ok, const char* what) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", what); failures += !ok; }
	std::uint8_t* at(std::uint32_t va) { return reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(va)); }
	std::vector<std::uint8_t> hex(std::string_view h)
	{
		std::vector<std::uint8_t> out(limits_rules::hex_size(h));
		for (std::size_t i = 0; i < out.size(); ++i) out[i] = limits_rules::hex_byte(h, i);
		return out;
	}
	void retail_bytes()
	{
		for (const auto& g : guards) { const auto b = hex(g.hex); std::memcpy(at(g.va), b.data(), b.size()); }
	}
	bool site_is(const std::array<std::uint8_t, 5>& bytes) { return std::memcmp(at(call_site), bytes.data(), 5) == 0; }
	bool logged(const char* text) { for (const auto& l : logger::lines) if (l.find(text) != std::string::npos) return true; return false; }
	std::uint32_t u32_of(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }

	void rules()
	{
		check(call_bytes(0x42a042, 0x420860) == native_call, "native call bytes are the retail e8 19 68 ff ff");
		const auto b = call_bytes(0x42a042, 0x10001000);
		check(call_target(0x42a042, b.data()) == 0x10001000, "call target round trip");
		check(should_clear(0x1234, 0, 1, true, 1, 0x20, 10.f), "living player-controlled successor of that controller is cleared");
		check(!should_clear(0, 0, 1, true, 1, 0, 10.f) && !should_clear(7, 7, 1, true, 1, 0, 10.f), "zero or null handle (party wiped) is left alone");
		check(!should_clear(0x1234, 0, 1, false, 1, 0, 10.f), "unresolved handle is left alone");
		check(!should_clear(0x1234, 0, 1, true, 2, 0, 10.f), "another controller's hero is left alone");
		check(!should_clear(0x1234, 0, 1, true, 1, 1, 10.f), "a hero still AI-controlled is left alone");
		check(!should_clear(0x1234, 0, 1, true, 1, 0, 0.f), "a dead successor is left alone");
		check(!should_clear(0x1234, 0, -1, true, 0xffffffffu, 0, 10.f) && !should_clear(0x1234, 0, 4, true, 4, 0, 10.f), "controller outside 0..3 is left alone");
		const auto off = capability(false, false), failed = capability(true, false), on = capability(true, true);
		check(off.verified && off.values == std::array<std::uint32_t, 4>{0, 0, 0, 0}, "rule 15 absent/off: verified {0,0,0,0}");
		check(!failed.verified && failed.values == std::array<std::uint32_t, 4>{0, 0, 0, 0}, "rule 15 requested but not installed: unverified");
		check(on.verified && on.values == std::array<std::uint32_t, 4>{1, 0, 0, 0}, "rule 15 installed: verified {1,0,0,0}");
		check(revision == 1, "rule 15 revision 1");
	}

	void against_retail(const char* path)
	{
		std::ifstream file(path, std::ios::binary);
		std::vector<std::uint8_t> exe((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
		if (exe.size() < 0x70000) { std::printf("SKIP retail guards: %s not readable\n", path); return; }
		bool all = true;
		for (const auto& g : guards)
		{
			const auto b = hex(g.hex);
			// .text and .rdata are mapped at their file offsets in the retail exe (raw == RVA).
			if (std::memcmp(exe.data() + (g.va - 0x400000), b.data(), b.size()) != 0) { std::printf("  guard %08X differs\n", unsigned(g.va)); all = false; }
		}
		check(all, "every guard matches the retail XMen2.exe");
	}

	// Stubs in place of the native bodies, written after install (the guards check the retail prologues).
	void stub_successor(std::uint32_t handle)
	{
		const std::uint8_t code[] = {0x8b, 0x44, 0x24, 0x04, 0xc7, 0x00, std::uint8_t(handle), std::uint8_t(handle >> 8), std::uint8_t(handle >> 16), std::uint8_t(handle >> 24), 0xc3};
		std::memcpy(at(native_successor), code, sizeof(code)); // mov eax,[esp+4]; mov dword [eax],handle; ret
	}
	void stubs()
	{
		const std::uint8_t resolver[] = {0x8b, 0x01, 0xc3}; // the test's handles are the actors' addresses
		std::memcpy(at(resolve), resolver, sizeof(resolver));
		const std::uint8_t clear[] = {0xc7, 0x81, 0x80, 0x01, 0, 0, 0, 0, 0, 0,             // +0x180 = 0
		                              0xc7, 0x81, 0x84, 0x01, 0, 0, 0, 0, 0, 0,             // +0x184 = 0
		                              0xc7, 0x81, 0xd0, 0x01, 0, 0, 0, 0, 0x80, 0, 0xc3};   // +0x1d0 = FLT_MIN
		std::memcpy(at(clear_goal), clear, sizeof(clear));
	}
	struct actor
	{
		alignas(4) std::array<std::uint8_t, 0x900> bytes{};
		template <class T> T& f(std::uint32_t o) { return *reinterpret_cast<T*>(bytes.data() + o); }
		void walking(std::uint32_t handed)
		{
			bytes.fill(0x5a);
			f<float>(health) = 120.f; f<std::uint32_t>(controller) = handed; f<std::uint8_t>(ai_flags) = 0x20;
			f<std::uint32_t>(follow) = 0x1111; f<float>(0x180) = 3.f; f<float>(0x184) = 1.f; f<std::uint32_t>(0x1d0) = 0x44000000;
			f<std::uint32_t>(0x3e0) = 0x2222; f<std::uint8_t>(steer) = 1; f<std::uint32_t>(mouse_mode) = 3;
			f<std::uint32_t>(mouse_828) = 7; f<std::uint8_t>(stuck) = 1;
		}
	};
	using hook_t = std::uint32_t*(__cdecl*)(std::uint32_t*, int, int);
	hook_t installed_hook() { return reinterpret_cast<hook_t>(static_cast<std::uintptr_t>(call_target(call_site, at(call_site)))); }
}

int main(int argc, char** argv)
{
	std::setbuf(stdout, nullptr);
	const auto begin = reinterpret_cast<std::uintptr_t>(fixture_space);
	if (GetModuleHandleW(nullptr) != reinterpret_cast<HMODULE>(0x300000) || begin > native_successor || null_handle + 4 > begin + sizeof(fixture_space))
	{ std::printf("Test fixture (%08X..%08X) does not cover the retail addresses\n", unsigned(begin), unsigned(begin + sizeof(fixture_space))); return 2; }
	DWORD protection = 0;
	if (!VirtualProtect(fixture_space, sizeof(fixture_space), PAGE_EXECUTE_READWRITE, &protection)) { std::puts("Can't make the fixture writable"); return 2; }
	const auto game = reinterpret_cast<HMODULE>(0x400000);
	rules();
	if (argc > 1) against_retail(argv[1]);
	*reinterpret_cast<std::uint32_t*>(at(null_handle)) = 0;

	// Off (absent key): nothing written, verified off.
	retail_bytes();
	ini::requested = false;
	death_switch::install(game);
	check(site_is(native_call) && !death_switch::installed() && death_switch::capability().verified && death_switch::capability().values[0] == 0,
	      "absent key: native call kept, rule 15 verified off");
	check(logged("death switch: off"), "absent key logs off");

	// Requested, wrong image: refused, unverified.
	ini::requested = true; logger::lines.clear();
	death_switch::install(reinterpret_cast<HMODULE>(0x400001));
	check(site_is(native_call) && !death_switch::installed() && !death_switch::capability().verified, "wrong image base: nothing written, rule 15 unverified");

	// Requested, each guard mismatched in turn: one log line, nothing written, unverified.
	for (const auto& g : guards)
	{
		retail_bytes(); at(g.va)[1] ^= 0x01; logger::lines.clear();
		death_switch::install(game);
		char what[96]; std::snprintf(what, sizeof(what), "guard %08X mismatch refuses with one log line and no write", unsigned(g.va));
		check(site_is(native_call) && !death_switch::installed() && !death_switch::capability().verified && logger::lines.size() == 1 && logged("isn't retail"), what);
	}
	check(game_version::refusals == 0, "refusals never refuse startup");

	// Installed: only the call's rel32 changes, and it targets the hook.
	retail_bytes(); logger::lines.clear();
	std::array<std::uint8_t, 0x40> before{}; std::memcpy(before.data(), at(0x42a020), before.size());
	death_switch::install(game);
	bool only_rel = true;
	for (std::size_t i = 0; i < before.size(); ++i)
	{
		const auto va = 0x42a020 + i;
		if ((va < call_site + 1 || va >= call_site + 5) && at(std::uint32_t(va))[0] != before[i]) only_rel = false;
	}
	check(death_switch::installed() && at(call_site)[0] == 0xe8 && only_rel && !site_is(native_call), "install rewrites only the call's rel32");
	check(death_switch::capability().verified && death_switch::capability().values == std::array<std::uint32_t, 4>{1, 0, 0, 0}, "installed: rule 15 verified {1,0,0,0}");
	check(logged("death switch: on") && logger::lines.size() == 1, "install logs one line");

	// The hook: the native hand-over runs first with the same arguments, then the successor's move intent is cleared.
	stubs();
	const auto hook = installed_hook();
	actor a; a.walking(1);
	const auto untouched = a.bytes;
	std::uint32_t out = 0xdeadbeef;
	stub_successor(u32_of(a.bytes.data()));
	std::uint32_t* r = hook(&out, 0x1d, 1);
	check(r == &out && out == u32_of(a.bytes.data()), "native hand-over result and out handle pass through");
	check(a.f<std::uint32_t>(follow) == 0 && a.f<float>(0x180) == 0.f && a.f<float>(0x184) == 0.f && a.f<std::uint32_t>(0x1d0) == 0x00800000 &&
	      a.f<std::uint32_t>(mouse_mode) == 0 && a.f<std::uint32_t>(mouse_828) == 0 && a.f<std::uint8_t>(stuck) == 0 && a.f<std::uint8_t>(steer) == 0,
	      "successor's follow handle, move goal, steer and mouse state cleared");
	bool rest = true;
	for (std::uint32_t o = 0; o < a.bytes.size(); ++o)
	{
		const bool cleared = (o >= follow && o < follow + 4) || (o >= 0x180 && o < 0x188) || (o >= 0x1d0 && o < 0x1d4) ||
		                     (o >= mouse_mode && o < mouse_mode + 4) || (o >= mouse_828 && o < mouse_828 + 4) || o == stuck || o == steer;
		if (!cleared && a.bytes[o] != untouched[o]) rest = false;
	}
	check(rest && a.f<std::uint32_t>(0x3e0) == 0x2222, "everything else kept, including the inherited target +0x3e0");
	check(death_switch::cleared() == 1 && logged("move intent cleared"), "hand-over counted and logged");

	auto unchanged = [&](const char* what, auto prepare, std::uint32_t handle, int handed) {
		a.walking(1); prepare(a); const auto snapshot = a.bytes; out = 0;
		stub_successor(handle); hook(&out, 0x1d, handed);
		check(a.bytes == snapshot && death_switch::cleared() == 1, what);
	};
	unchanged("party wiped (null handle): nothing touched", [](actor&) {}, 0, 1);
	unchanged("successor of another controller: nothing touched", [](actor& x) { x.f<std::uint32_t>(controller) = 2; }, u32_of(a.bytes.data()), 1);
	unchanged("successor still AI-controlled: nothing touched", [](actor& x) { x.f<std::uint8_t>(ai_flags) = 0x21; }, u32_of(a.bytes.data()), 1);
	unchanged("dead successor: nothing touched", [](actor& x) { x.f<float>(health) = 0.f; }, u32_of(a.bytes.data()), 1);

	// A second install never rewrites (the INI isn't reread).
	ini::requested = false; std::array<std::uint8_t, 5> now{}; std::memcpy(now.data(), at(call_site), 5);
	death_switch::install(game);
	check(site_is(now) && death_switch::installed(), "installed once; a later install changes nothing");
	return failures ? 1 : 0;
}
