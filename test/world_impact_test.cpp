#include <Windows.h>
#include "world_impact_bridge.hpp"
#include "gameplay_capabilities.hpp"
#include "xml1_hook_transaction.hpp"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace
{
	unsigned short observed_heaviness = 0;
	__declspec(naked) void physical_continuation()
	{
		__asm {
			mov word ptr [observed_heaviness], ax
			pop edi
			pop esi
			pop ecx
			ret 0x0c
		}
	}
	__declspec(naked) float __stdcall invoke(float, void*, int)
	{
		__asm {
			push ecx
			push esi
			push edi
			mov esi, dword ptr [esp + 0x14]
			fld dword ptr [esp + 0x10]
			jmp world_impact_bridge::bridge
		}
	}
	struct memory
	{
		std::vector<unsigned char> bytes;
		int writes = 0, fail_write = -1;
		bool read(std::uint32_t at, void* out, std::size_t n) { std::memcpy(out, bytes.data()+at, n); return true; }
		bool write(std::uint32_t at, const void* in, std::size_t n)
		{
			// Model a partial write, not just a refused first byte.
			std::memcpy(bytes.data()+at, in, n);
			return ++writes != fail_write;
		}
		bool acquire(std::uint32_t, std::uint32_t& p) { p=4; return true; }
		bool restore(std::uint32_t, std::uint32_t) { return true; }
		bool flush() { return true; }
	};
}

int main(int argc, char** argv)
{
	int failures = 0;
	auto check = [&](bool yes, const char* what) { if (!yes) { ++failures; std::printf("FAIL %s\n", what); } };
	using namespace world_impact_rules;
	check(!parse_rule("").set && parse_rule("xml2").value == rule::xml2, "default retains native impact");
	check(parse_rule(" XML1 ; enabled").value == rule::xml1 && !parse_rule("on").error.empty(), "strict opt-in");
	world_impact_bridge::resume = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&physical_continuation));
	std::array<unsigned char, 0x320> other{};
	other[0x31d] = 2;
	for (int i=0; i<100; ++i)
	{
		check(invoke(7.75f, nullptr, 1) == 0.0f, "world discards incoming FPU factor and returns zero");
		check(invoke(7.75f, other.data(), 1) == 7.75f && observed_heaviness == 2, "physical target replays displaced load and retains factor");
	}
	// Repeated calls exercise FPU depth and the native callee stack cleanup.
	gameplay_capabilities::record record;
	record.invalidate();
	check(!record.finalize() && record.bytes().empty(), "unassigned rule cannot advertise stock agreement");
	gameplay_capabilities::record finalized;
	check(finalized.finalize(), "stock record starts valid");
	finalized.invalidate();
	check(!finalized.ready() && !finalized.finalize(), "late invalidation is sticky");

	memory mem{std::vector<unsigned char>(4096, 0)};
	std::copy(original.begin(), original.end(), mem.bytes.begin()+32);
	const auto before = mem.bytes;
	xml1_hook_transaction::patch patch{32, {original.begin(),original.end()}, std::vector<unsigned char>(original.size(),0x90)};
	bool safe = false;
	mem.fail_write = 1;
	check(!xml1_hook_transaction::apply(mem,{patch},&safe) && safe && mem.bytes == before, "partial write rolls back exact original bytes");
	mem.fail_write = -1;
	check(xml1_hook_transaction::apply(mem,{patch},&safe), "verified transaction installs");
	const auto installed = mem.bytes;
	check(!xml1_hook_transaction::apply(mem,{patch},&safe) && mem.bytes == installed, "signature mismatch writes nothing");

	if (argc > 1)
	{
		std::ifstream file(argv[1],std::ios::binary);
		std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)),{});
		if (raw.size() < sizeof(IMAGE_DOS_HEADER)) { std::puts("FAIL unreadable PE fixture"); return 1; }
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
		if (dos->e_lfanew < 0 || std::size_t(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS32)>raw.size()) return 1;
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);
		if (dos->e_magic != IMAGE_DOS_SIGNATURE || nt->Signature != IMAGE_NT_SIGNATURE ||
		    nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
		    nt->OptionalHeader.SizeOfImage < 0x298000 || nt->OptionalHeader.SizeOfImage > 0x4000000 ||
		    std::size_t(dos->e_lfanew)+24+nt->FileHeader.SizeOfOptionalHeader+
		    std::size_t(nt->FileHeader.NumberOfSections)*sizeof(IMAGE_SECTION_HEADER)>raw.size()) return 1;
		std::vector<unsigned char> image((std::size_t(nt->OptionalHeader.SizeOfImage)+4095)&~std::size_t(4095));
		const auto* sections = IMAGE_FIRST_SECTION(nt);
		for (unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
		{
			const auto& s=sections[i];
			if (std::uint64_t(s.PointerToRawData)+s.SizeOfRawData>raw.size() || std::uint64_t(s.VirtualAddress)+s.SizeOfRawData>image.size()) return 1;
			std::memcpy(image.data()+s.VirtualAddress,raw.data()+s.PointerToRawData,s.SizeOfRawData);
		}
		const auto* mismatch=first_mismatch(image.data());
		if (mismatch) std::printf("guard mismatch %08X %s\n",mismatch->va,mismatch->what);
		check(!mismatch, "retail executable guards");
		for (const auto& g:guards)
		{
			image[g.va-limits_rules::image_base]^=1;
			check(first_mismatch(image.data())!=nullptr,"every dependency guard rejects corruption");
			image[g.va-limits_rules::image_base]^=1;
		}
	}
	else std::puts("SKIP retail PE guard fixture (pass executable path)");
	std::printf("world impact: %d failures\n",failures);
	return failures ? 1 : 0;
}
