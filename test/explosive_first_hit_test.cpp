#include "explosive_first_hit.hpp"
#include <Windows.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
// Link the production installer against isolated settings/logger providers.
namespace ini { bool requested = false; bool flag(const wchar_t*, const wchar_t*, bool) { return requested; } }
namespace logger { void write(const char*, ...) {} }
#pragma section(".fixture", read, write, execute)
__declspec(allocate(".fixture")) std::uint8_t fixture_space[0x100000]{};
namespace { void* target_hit; const std::uintptr_t tail = 0x49ab4b; int failures; }
__declspec(naked) void __stdcall run_tail(void*)
{
	__asm { push edi } __asm { push ebx } __asm { push esi }
	__asm { mov edi, [esp + 0x10] }
	__asm { mov esi, dword ptr [target_hit] }
	__asm { jmp dword ptr [tail] }
}
void check(bool ok, const char* msg) { std::printf("%s %s\n",ok ? "PASS" : "FAIL", msg); failures += !ok; }
int main()
{
	std::setbuf(stdout, nullptr);
	// The test executable reserves this address itself, before Windows maps locale data.
	const auto game = reinterpret_cast<HMODULE>(0x400000);
	auto* branch = reinterpret_cast<std::uint8_t*>(0x49ab58);
	const auto begin = reinterpret_cast<std::uintptr_t>(fixture_space);
	if (GetModuleHandleW(nullptr) != game || 0x49ab4b < begin || 0x49ab73 > begin + sizeof(fixture_space))
	{ std::puts("Test fixture section does not cover the retail code address"); return 2; }
	// Short retail compatibility signature and native epilogues, no game fixture required.
	const std::array<std::uint8_t,37> native{0x8b,0xbf,0x20,0x03,0x00,0x00,0xf6,0x87,0xab,0x00,0x00,0x00,0x08,0x74,0x0d,0x8b,0x47,0x30,0x40,0x89,0x46,0x28,0x5e,0x5b,0x5f,0xc2,0x04,0x00,0x8b,0x4f,0x30,0x89,0x4e,0x28,0x5e,0x5b,0x5f};
	auto* site=reinterpret_cast<std::uint8_t*>(0x49ab4b);
	DWORD fixture_protection;
	if (!VirtualProtect(site, 40, PAGE_EXECUTE_READWRITE, &fixture_protection)) return 2;
	auto reset=[&] { std::memcpy(site,native.data(),native.size()); site[37]=0xc2;site[38]=4;site[39]=0; };
	std::array<std::uint8_t,0x324> projectile{}; std::array<std::uint8_t,0xb0> data{}; std::array<std::uint8_t,0x64> hit{};
	*reinterpret_cast<void**>(projectile.data()+0x320)=data.data(); target_hit=hit.data();
	*reinterpret_cast<std::uint32_t*>(data.data()+0x30)=41;
	reset(); explosive_first_hit::install(game);
	check(std::memcmp(site,native.data(),native.size())==0,"absent/default key preserves every native byte");
	run_tail(projectile.data());check(*reinterpret_cast<std::uint32_t*>(hit.data()+0x28)==41,"negative control reuses contact ID without flag");
	data[0xab]=8;run_tail(projectile.data());check(*reinterpret_cast<std::uint32_t*>(hit.data()+0x28)==42,"native explicit first-victim flag gives fresh ID");
	ini::requested=true;reset();site[10]^=1;auto bad=*reinterpret_cast<std::uint16_t*>(branch);
	explosive_first_hit::install(game);check(*reinterpret_cast<std::uint16_t*>(branch)==bad,"guard mismatch leaves code unchanged");
	reset();explosive_first_hit::install(reinterpret_cast<HMODULE>(0x400001));check(branch[0]==0x74,"wrong image base refuses patch");
	reset();explosive_first_hit::install(game);
	bool only=true;for(std::size_t i=0;i<native.size();++i)if(i!=13 && i!=14 && site[i]!=native[i])only=false;
	check(only && branch[0]==0x90 && branch[1]==0x90,"production patch changes only conditional branch");
	for(auto flag:{0,8}) {
		data[0xab]=static_cast<std::uint8_t>(flag);hit.fill(0x5a);run_tail(projectile.data());
		check(*reinterpret_cast<std::uint32_t*>(hit.data()+0x28)==42,"XML1 blast ID fresh with either authored flag");
		bool unchanged=true;for(std::size_t i=0;i<hit.size();++i)if((i<0x28 || i>=0x2c) && hit[i]!=0x5a)unchanged=false;
		check(unchanged && *reinterpret_cast<std::uint32_t*>(data.data()+0x30)==41,"damage record and projectile sequence remain unchanged");
	}
	*reinterpret_cast<std::uint32_t*>(data.data()+0x30)=0xffffffffu;run_tail(projectile.data());
	check(*reinterpret_cast<std::uint32_t*>(hit.data()+0x28)==0,"native unsigned increment wraps exactly");
	DWORD unused; VirtualProtect(site, 40, fixture_protection, &unused);
	return failures ? 1:0;
}
