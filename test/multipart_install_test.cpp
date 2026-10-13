// multipart::install against the retail executable's sections mapped at their own addresses (the image fixture), one
// child process per case so every install starts from retail bytes. No game runs: only the installer, the loop-length
// query (its native callees replaced by stubs) and the patched slots are inspected.
//   xml2_multipart_install_test [XMen2.exe]   (or XML2_FIX_TEST_EXE); without an executable: SKIPPED.
// Cases (from the feat/multipart review): retail; the script-table operand at 0x49FE31 zeroed (must refuse without a
// fault, write nothing, report unverified); a foreign setSegmentVisible handler (bridge required: same); a changed
// guard; the loop length draws from the generator at zero spread too.
#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>
#include "multipart.cpp"

namespace ini { bool flag(const wchar_t*, const wchar_t* key, bool fallback) { return wcscmp(key, L"Xml1Multipart") == 0 ? true : fallback; } }
namespace logger {
void write(const char* fmt, ...) { va_list a; va_start(a, fmt); std::printf("  log: "); std::vprintf(fmt, a); std::putchar('\n'); va_end(a); }
void write_once(std::string_view, const char*, ...) {}
}
namespace game_version { bool startup_refused = false; void refuse_startup() { startup_refused = true; } }

extern "C" unsigned char fixture_space[0x400000];
namespace {
int checks = 0, failures = 0;
void check(bool ok, const char* what) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", what); } }
std::uint32_t u32_at(std::uint32_t a) { std::uint32_t v; std::memcpy(&v, reinterpret_cast<const void*>(a), 4); return v; }
void put_u32(std::uint32_t a, std::uint32_t v) { std::memcpy(reinterpret_cast<void*>(a), &v, 4); }
void jump(std::uint32_t at, const void* to) {
	auto* p = reinterpret_cast<unsigned char*>(at); p[0] = 0xe9;
	const auto rel = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(to)) - at - 5; std::memcpy(p + 1, &rel, 4);
}
bool map(const char* path) {
	std::ifstream in(path, std::ios::binary); std::vector<unsigned char> raw((std::istreambuf_iterator<char>(in)), {});
	if (raw.size() < 0x1000) return false;
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data() + dos->e_lfanew);
	if (nt->OptionalHeader.ImageBase != 0x400000) return false;
	DWORD old = 0; VirtualProtect(fixture_space, sizeof fixture_space, PAGE_EXECUTE_READWRITE, &old);
	const auto* s = IMAGE_FIRST_SECTION(nt);
	for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
		const auto va = 0x400000u + s[i].VirtualAddress;
		if (va < 0x401000 || va + s[i].SizeOfRawData > 0x801000) continue;
		std::memcpy(reinterpret_cast<void*>(va), raw.data() + s[i].PointerToRawData, s[i].SizeOfRawData);
	}
	return true;
}
// the twelve install destinations and their retail values
const std::uint32_t slots[][2] = {{0x68986c, 0x48cec0}, {0x689868, 0x48d190}, {0x689870, 0x48cbc0}, {0x689878, 0x48cde0},
	{0x694c60, 0x517370}, {0x695418, 0x517370}, {0x6953f0, 0x51d610}, {0x695440, 0x51d880}, {0x695388, 0x51cad0}, {0x695468, 0x51cad0}};
bool untouched(std::uint32_t segment_handler) {
	bool ok = u32_at(0x438b10) == 0xffffc92c && u32_at(0x68b828) == segment_handler;
	for (const auto& s : slots) ok = ok && u32_at(s[0]) == s[1];
	return ok;
}
bool all_hooked() {
	bool ok = u32_at(0x438b10) != 0xffffc92c && u32_at(0x68b828) != 0x4a5bd0;
	for (const auto& s : slots) ok = ok && u32_at(s[0]) != s[1];
	return ok;
}
bool faulted = false;
void attempt() {
	__try { multipart::install(reinterpret_cast<HMODULE>(0x400000)); }
	__except (EXCEPTION_EXECUTE_HANDLER) { faulted = true; }
}
int draws = 0;
unsigned char store[0x8a18]{};
void* __cdecl stub_store() { return store; }
std::uint32_t __fastcall stub_valid(void*, void*, std::uint32_t) { return 1; }
float __cdecl stub_random(unsigned char) { ++draws; return .5f; }

int run_case(const std::string& mode) {
	if (mode == "invalid-table") put_u32(0x49fe31, 0);
	if (mode == "bridge-changed") put_u32(0x68b828, 0x4a5bd1);
	if (mode == "guard-changed") *reinterpret_cast<unsigned char*>(0x48cec0) ^= 1;
	if (mode == "relocated-table") {   // forced_teams' move of the table and its count: still found by name
		static unsigned char table[0x121 * 16 + 64];
		std::memcpy(table, reinterpret_cast<const void*>(0x68a908), 0x121 * 16);
		put_u32(0x49fe31, static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(table)));
		put_u32(0x49fe36, 0x122);
	}
	const std::uint32_t handler_before = mode == "bridge-changed" ? 0x4a5bd1 : 0x4a5bd0;
	attempt();
	const auto c = multipart::capability();
	check(!faulted, "the installer never faults");
	check(!game_version::startup_refused, "no startup refusal (nothing was half-written)");
	if (mode == "retail") {
		check(all_hooked(), "retail: all twelve destinations hooked (bridge included)");
		check(c.verified && c.values[0] == 1 && c.values[1] == 2 && c.values[2] == 0 && c.values[3] == 0, "retail: rule 6 verified {1,2,0,0}");
	} else if (mode == "relocated-table") {
		check(u32_at(0x438b10) != 0xffffc92c && u32_at(0x68b828) == 0x4a5bd0, "relocated table: installed, the retail table left alone");
		check(c.verified && c.values[0] == 1, "relocated table: verified");
	} else if (mode == "loop-random") {
		jump(0x4078d0, reinterpret_cast<const void*>(&stub_store)); jump(0x405100, reinterpret_cast<const void*>(&stub_valid));
		jump(0x560190, reinterpret_cast<const void*>(&stub_random));
		float time = .6f, spread = 0;
		std::memcpy(store + 0x4874 + 0x34, &time, 4); std::memcpy(store + 0x4874 + 0x38, &spread, 4);
		const float zero = multipart::loop_time(1);
		check(zero > .599f && zero < .601f && draws == 1, "loop length: zero spread still draws once (XBE 0x25f30), 0.6 s");
		spread = .2f; std::memcpy(store + 0x4874 + 0x38, &spread, 4);
		const float wide = multipart::loop_time(1);
		check(wide > .699f && wide < .701f && draws == 2, "loop length: 0.6 + 0.5 x 0.2 with one more draw");
		check(multipart::loop_time(0) > .299f && draws == 2, "loop length: a stale handle is 0.3 s without a draw (0x25ee0)");
	} else {
		check(untouched(handler_before), "refused: nothing written");
		check(!c.verified && c.values[0] == 0, "refused: rule 6 unverified");
	}
	std::printf("%s: %d checks, %d failure(s)\n", mode.c_str(), checks, failures);
	return failures ? 1 : 0;
}
}

int main(int argc, char** argv) {
	std::setbuf(stdout, nullptr);
	if (argc == 3) return map(argv[1]) ? run_case(argv[2]) : 3;
	const char* exe = argc > 1 ? argv[1] : std::getenv("XML2_FIX_TEST_EXE");
	if (!exe) { std::puts("SKIPPED: no retail executable given (argument or XML2_FIX_TEST_EXE)"); return 0; }
	char self[MAX_PATH]{}; GetModuleFileNameA(nullptr, self, MAX_PATH);
	int failed = 0;
	for (const char* mode : {"retail", "relocated-table", "invalid-table", "bridge-changed", "guard-changed", "loop-random"}) {
		std::string cmd = std::string("\"") + self + "\" \"" + exe + "\" " + mode;
		STARTUPINFOA si{sizeof si}; PROCESS_INFORMATION pi{};
		if (!CreateProcessA(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi)) { ++failed; continue; }
		WaitForSingleObject(pi.hProcess, 60000); DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
		CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
		if (code != 0) { ++failed; std::printf("case %s failed (exit %lu)\n", mode, code); }
	}
	std::printf("Multipart install: %d case(s) failed\n", failed);
	return failed ? 1 : 0;
}
