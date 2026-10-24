#include <Windows.h>
#include "input_worker.hpp"
#include "input_worker_rules.hpp"
#include "xml1_hook_transaction.hpp"
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

extern "C" unsigned char fixture_space[];
namespace
{
	using namespace input_worker_rules;
	unsigned checks = 0, failures = 0, startup_refusals = 0;
	std::vector<std::string> logs;
	void check(bool ok, const char* name)
	{
		++checks;
		if (!ok) { ++failures; std::printf("FAIL %s\n", name); }
	}
	unsigned char* at(std::uint32_t va) { return reinterpret_cast<unsigned char*>(va); }
	std::vector<unsigned char> bytes(std::string_view hex)
	{
		std::vector<unsigned char> out;
		for (std::size_t i = 0; i < hex.size(); i += 2)
			out.push_back(static_cast<unsigned char>((nibble(hex[i]) << 4) | nibble(hex[i+1])));
		return out;
	}
	void seed()
	{
		for (const auto& g : guards) { const auto b = bytes(g.hex); std::memcpy(at(g.va), b.data(), b.size()); }
	}
	void raw_guards(const char* file)
	{
		std::ifstream f(file, std::ios::binary);
		const std::vector<unsigned char> raw((std::istreambuf_iterator<char>(f)), {});
		if (raw.size() < sizeof(IMAGE_DOS_HEADER)) { check(false, "retail fixture readable"); return; }
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
		if (dos->e_lfanew < 0 || std::size_t(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > raw.size()) { check(false, "PE bounds"); return; }
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data() + dos->e_lfanew);
		if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.ImageBase != image_base ||
			std::size_t(dos->e_lfanew) + 24 + nt->FileHeader.SizeOfOptionalHeader + std::size_t(nt->FileHeader.NumberOfSections)*40 > raw.size())
		{ check(false, "PE sections"); return; }
		for (const auto& g : guards)
		{
			const auto b = bytes(g.hex); bool matched = false;
			const auto* sections = IMAGE_FIRST_SECTION(nt);
			for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i)
			{
				const auto& s = sections[i]; const auto rva = g.va - image_base;
				if (rva < s.VirtualAddress || std::uint64_t(rva - s.VirtualAddress) + b.size() > s.SizeOfRawData) continue;
				const auto off = std::uint64_t(s.PointerToRawData) + rva - s.VirtualAddress;
				matched = off + b.size() <= raw.size() && !std::memcmp(raw.data()+off, b.data(), b.size());
			}
			check(matched, "retail signature");
		}
	}
	using call_t = void(__cdecl*)();
	void begin() { reinterpret_cast<call_t>(entry)(); }
	void end() { reinterpret_cast<call_t>(finish)(); }
	unsigned& shared_manager() { return *reinterpret_cast<unsigned*>(manager); }
	unsigned char& shared_stopped() { return *at(stopped); }
	constexpr unsigned fixture_bytes = 0x700000;  // matches input_worker_fixture.asm; reaches past 0xa6a7e4
	constexpr unsigned synthetic_manager = 0x12345678;
	void reset_globals() { shared_manager() = synthetic_manager; shared_stopped() = 0; }

	unsigned saved_flags = 0, returned_flags = 0, saved_sp = 0, returned_sp = 0;
	unsigned got_a = 0, got_c = 0, got_d = 0, got_b = 0, got_s = 0, got_i = 0, got_p = 0;
	float got_fpu = 0;
	unsigned target = finish;
	__declspec(naked) void abi_probe()
	{
		__asm {
			pushad
			mov saved_sp, esp
			mov eax, 11
			mov ecx, 22
			mov edx, 33
			mov ebx, 44
			mov esi, 55
			mov edi, 66
			mov ebp, 77
			fld1
			cmp eax, ecx
			pushfd
			pop saved_flags
			call dword ptr [target]
			pushfd
			pop returned_flags
			mov returned_sp, esp
			mov got_a, eax
			mov got_c, ecx
			mov got_d, edx
			mov got_b, ebx
			mov got_s, esi
			mov got_i, edi
			mov got_p, ebp
			fstp got_fpu
			popad
			ret
		}
	}
	struct failing_memory
	{
		std::vector<unsigned char> data = std::vector<unsigned char>(4096, 0);
		unsigned writes = 0;
		bool read(std::uint32_t a, void* p, std::size_t n) { std::memcpy(p, data.data()+a, n); return true; }
		bool write(std::uint32_t a, const void* p, std::size_t n) { ++writes; std::memcpy(data.data()+a, p, writes==2?n/2:n); return writes!=2; }
		bool acquire(std::uint32_t, std::uint32_t& old) { old=4; return true; }
		bool restore(std::uint32_t, std::uint32_t) { return true; }
		bool flush() { return true; }
	};
}
namespace logger
{
	void write(const char* format, ...)
	{
		char b[512]; va_list args; va_start(args, format); vsnprintf(b, sizeof(b), format, args); va_end(args);
		logs.emplace_back(b);
	}
}
namespace game_version { void refuse_startup() { ++startup_refusals; } }

int main(int argc, char** argv)
{
	if (argc != 2 || reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr)) != image_base ||
		reinterpret_cast<std::uintptr_t>(fixture_space) > entry) return 2;
	// The retail globals (manager 0xa6a7e4, stopped 0xa6a7e1) live inside the fixture image itself, so the loader
	// reserved them with the image; nothing is allocated at a fixed address here.
	DWORD protection = 0;
	MEMORY_BASIC_INFORMATION globals {};
	if (!VirtualProtect(at(0x401000), fixture_bytes, PAGE_EXECUTE_READWRITE, &protection) ||
		!VirtualQuery(at(manager), &globals, sizeof(globals)) || globals.State != MEM_COMMIT ||
		reinterpret_cast<std::uintptr_t>(globals.AllocationBase) != image_base ||
		!VirtualQuery(at(stopped), &globals, sizeof(globals)) || globals.State != MEM_COMMIT ||
		reinterpret_cast<std::uintptr_t>(globals.AllocationBase) != image_base) return 2;
	raw_guards(argv[1]); if (failures) return 1;
	seed(); input_worker::install(reinterpret_cast<HMODULE>(0x500000));
	check(!first_mismatch(at(image_base)), "foreign image untouched");
	for (const auto& g : guards)
		for (std::size_t i = 0; i < g.hex.size()/2; ++i)
		{
			seed(); at(g.va)[i] ^= 1;
			const std::vector<unsigned char> before(at(entry), at(finish_next));
			const auto log_count = logs.size();
			input_worker::install(GetModuleHandleW(nullptr));
			check(!std::memcmp(at(entry), before.data(), before.size()), "each corrupted guard byte refuses both writes");
			check(logs.size() == log_count+1 && logs.back().find("retail guard refused") != std::string::npos, "one useful refusal log");
		}
	seed(); const std::vector<unsigned char> before(at(0x401000), at(0x801000));
	input_worker::install(GetModuleHandleW(nullptr));
	check(*at(entry) == 0xe9 && *at(finish) == 0xe9, "both production bridges installed");
	bool footprint = true;
	for (std::size_t i = 0; i < before.size(); ++i)
		if (at(0x401000)[i] != before[i] && !(0x401000+i >= entry && 0x401000+i < entry_next) &&
			!(0x401000+i >= finish && 0x401000+i < finish_next)) footprint = false;
	check(footprint, "only the two guarded sites changed");
	const auto log_count = logs.size(); input_worker::install(GetModuleHandleW(nullptr));
	check(logs.size() == log_count, "repeat installation is harmless");
	// Synthetic continuations exercise the installed bridges, not copied game code.
	const unsigned char unwind[] {0x8b,0xe5,0x5d,0xc3};
	std::memcpy(at(entry_next), unwind, sizeof(unwind)); *at(finish_next) = 0xc3;
	FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
	reset_globals(); begin(); begin(); end();
	check(shared_manager() == synthetic_manager && shared_stopped() == 0, "first exit retains manager and withholds completion");
	end(); check(shared_manager() == 0 && shared_stopped() == 1, "last exit clears manager then acknowledges completion");
	reset_globals(); begin(); begin(); begin(); end(); end();
	check(shared_manager() == synthetic_manager && shared_stopped() == 0, "three overlapping workers retain lifetime");
	end(); check(shared_manager() == 0 && shared_stopped() == 1, "three-worker final completion");
	for (unsigned repeat = 0; repeat < 100; ++repeat)
	{
		reset_globals(); begin(); end();
		check(shared_manager() == 0 && shared_stopped() == 1, "single worker/reinitialization completes");
	}
	// Explicitly stage a returning reader against a peer's exit on distinct threads.
	reset_globals(); std::atomic<unsigned> phase{0}; bool reader_valid = false;
	std::thread reader([&] { begin(); phase.store(1); while (phase.load()!=2) SwitchToThread();
		reader_valid = shared_manager()==synthetic_manager && !shared_stopped(); end(); });
	while (phase.load()!=1) SwitchToThread(); begin(); end(); phase.store(2); reader.join();
	check(reader_valid && shared_manager()==0 && shared_stopped()==1, "staged peer exit cannot invalidate suspended reader");
	reset_globals(); begin(); abi_probe();
	check(got_a==11 && got_c==22 && got_d==33 && got_b==44 && got_s==55 && got_i==66 && got_p==77, "exit bridge preserves all registers including cookie ECX");
	check(saved_flags==returned_flags && saved_sp==returned_sp && got_fpu==1.0f, "exit bridge preserves flags stack and FPU");
	target=entry; reset_globals(); abi_probe(); end();
	check(got_a==11 && got_c==22 && got_d==33 && got_b==44 && got_s==55 && got_i==66 && got_p==77 && saved_sp==returned_sp && got_fpu==1.0f, "entry bridge preserves arguments registers stack and FPU");
	failing_memory memory; const auto original=memory.data; bool safe=false;
	check(!xml1_hook_transaction::apply(memory, {{32,{0,0,0,0,0,0},{1,1,1,1,1,1}}, {128,{0,0,0,0,0,0},{2,2,2,2,2,2}}}, &safe) &&
		safe && memory.data==original, "second-site partial failure rolls back both destinations");
	check(startup_refusals==0, "ordinary refusals leave stock startup available");
	std::printf("input worker: %u checks, %u failures\n", checks, failures);
	return failures ? 1 : 0;
}
