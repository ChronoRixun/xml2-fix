#include <Windows.h>
#include "box_harm.hpp"
#include "box_harm_bridge.hpp"
#include "ini.hpp"
#include "xml1_hook_transaction.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

extern "C" unsigned char fixture_space[];
namespace
{
	std::optional<std::string> setting;
	int rejected = 0, refused = 0, failures = 0, checks = 0;
	void check(bool condition,const char* name)
	{
		++checks;
		if (!condition) { ++failures; std::printf("FAIL %s\n",name); }
	}
	std::vector<unsigned char> decode_hex(std::string_view s)
	{
		std::vector<unsigned char> out;
		for (std::size_t i=0;i<s.size();i+=2) { unsigned n=0;std::sscanf(s.data()+i,"%2x",&n);out.push_back(static_cast<unsigned char>(n)); }
		return out;
	}
	unsigned char* at(std::uint32_t address) { return reinterpret_cast<unsigned char*>(address); }
	void seed()
	{
		for (const auto& g:box_harm_rules::guards) { const auto b=decode_hex(g.hex);std::memcpy(at(g.va),b.data(),b.size()); }
	}
	bool untouched() { return !box_harm_rules::first_mismatch(at(0x400000)); }
	float captured[3]{};
	std::uint32_t captured_ecx=0,captured_esp=0,captured_flags=0,before_flags=0;
	std::uint32_t captured_registers[6]{};
	__declspec(naked) void continuation()
	{
		__asm {
			mov captured_ecx, ecx
			mov captured_esp, esp
			pushfd
			pop captured_flags
			mov captured_registers[0], eax
			mov captured_registers[4], ebx
			mov captured_registers[8], esi
			mov captured_registers[12], edi
			mov captured_registers[16], ebp
			mov eax, dword ptr [edx]
			mov dword ptr [captured], eax
			mov eax, dword ptr [edx+4]
			mov dword ptr [captured+4], eax
			mov eax, dword ptr [edx+8]
			mov dword ptr [captured+8], eax
			ret
		}
	}
	std::uint32_t target=box_harm_rules::site;
	float observed_fpu=0;
	__declspec(naked) void drive()
	{
		__asm {
			pushad
			sub esp, 0x60
			mov dword ptr [esp+0x4c], 0x42c80000
			mov dword ptr [esp+0x50], 0x43480000
			mov dword ptr [esp+0x54], 0x43960000
			fld1
			mov eax, 0x12345678
			mov ebx, 0x23456789
			mov esi, 0x3456789a
			mov edi, 0x456789ab
			mov ebp, 0x56789abc
			cmp eax, eax
			pushfd
			pop before_flags
			call dword ptr [target]
			fstp observed_fpu
			add esp, 0x60
			popad
			ret
		}
	}
	struct memory
	{
		std::vector<unsigned char> bytes;
		int writes=0,fail_write=-1;
		bool read(std::uint32_t a,void* p,std::size_t n){std::memcpy(p,bytes.data()+a,n);return true;}
		bool write(std::uint32_t a,const void* p,std::size_t n){const bool ok=++writes!=fail_write;std::memcpy(bytes.data()+a,p,ok?n:n/2);return ok;}
		bool acquire(std::uint32_t,std::uint32_t& old){old=4;return true;}
		bool restore(std::uint32_t,std::uint32_t){return true;}
		bool flush(){return true;}
	};
	void raw_guards(const char* path)
	{
		std::ifstream f(path,std::ios::binary);
		std::vector<unsigned char> raw((std::istreambuf_iterator<char>(f)),{});
		if(raw.size()<0x1000){check(false,"readable retail fixture");return;}
		const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
		if(dos->e_lfanew<0 || std::size_t(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS32)>raw.size()){check(false,"PE header bounds");return;}
		const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);
		if(nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
		   std::size_t(dos->e_lfanew)+24+nt->FileHeader.SizeOfOptionalHeader+std::size_t(nt->FileHeader.NumberOfSections)*40>raw.size()){
			check(false,"PE section bounds");return;
		}
		for(const auto& g:box_harm_rules::guards){
			const auto bytes=decode_hex(g.hex);bool match=false;
			const auto* sections=IMAGE_FIRST_SECTION(nt);
			for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){
				const auto& s=sections[i];const auto rva=g.va-0x400000;
				if(rva<s.VirtualAddress || std::uint64_t(rva-s.VirtualAddress)+bytes.size()>s.SizeOfRawData)continue;
				const auto off=std::uint64_t(s.PointerToRawData)+rva-s.VirtualAddress;
				match=off+bytes.size()<=raw.size() && std::memcmp(raw.data()+off,bytes.data(),bytes.size())==0;
			}
			check(match,g.what);
		}
	}
}
namespace ini { std::optional<std::string> text(const wchar_t*,const wchar_t*) { return setting; } }
namespace logger { void write(const char*,...) {} }
namespace game_version { void refuse_startup() { ++refused; } }
namespace gameplay_agreement { void reject_unregistered_rule(const char*) { ++rejected; } }

int main(int argc,char** argv)
{
	using namespace box_harm_rules;
	if(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))!=0x400000 || reinterpret_cast<std::uintptr_t>(fixture_space)>site)return 2;
	DWORD old=0;if(!VirtualProtect(at(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old))return 2;
	if(argc>1)raw_guards(argv[1]);else{std::puts("FAIL retail executable argument required");return 2;}
	if(failures)return 1;
	seed();box_harm::install(GetModuleHandleW(nullptr));
	check(!box_harm::requested() && !box_harm::installed() && untouched() && rejected==0,"absent key preserves code and online agreement");
	setting="xml2";box_harm::install(GetModuleHandleW(nullptr));
	check(!box_harm::requested() && untouched() && rejected==0,"explicit native mode preserves code and agreement");
	setting="invalid";box_harm::install(GetModuleHandleW(nullptr));
	check(box_harm::requested() && !box_harm::installed() && untouched() && rejected==1,"invalid request changes no code and blocks unregistered agreement");
	setting="xml1";box_harm::install(reinterpret_cast<HMODULE>(0x500000));
	check(!box_harm::installed() && untouched(),"relocated executable refused");
	for(const auto& g:guards){
		seed();*at(g.va)^=1;
		const auto before=std::vector<unsigned char>(at(site),at(site)+original.size());
		box_harm::install(GetModuleHandleW(nullptr));
		check(!box_harm::installed() && std::memcmp(at(site),before.data(),before.size())==0,"each dependency mismatch prevents writes");
	}
	seed();const std::vector<unsigned char> image_before(at(0x401000),at(0x801000));
	box_harm::install(GetModuleHandleW(nullptr));
	check(box_harm::installed() && *at(site)==0xe9 && rejected==7,"production installer opts in and rejects unassigned multiplayer rule");
	bool footprint=true;
	for(std::size_t i=0;i<image_before.size();++i)
		if(at(0x401000)[i]!=image_before[i] && !(0x401000+i>=site && 0x401000+i<resume))footprint=false;
	check(footprint,"installer changes only the six-byte box-origin site, including no radius-path change");
	box_harm_bridge::resume=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&continuation));
	for(int i=0;i<100;++i){
		drive();
		check(captured[0]==0 && captured[1]==0 && captured[2]==0,"nonzero center replaced with zero origin");
		check(captured_ecx==captured_esp && captured_flags==before_flags && observed_fpu==1.0f,"native stack destination, flags and FPU preserved");
		check(captured_registers[0]==0x12345678 && captured_registers[1]==0x23456789 && captured_registers[2]==0x3456789a &&
		      captured_registers[3]==0x456789ab && captured_registers[4]==0x56789abc,"unrelated registers preserved");
	}
	check(refused==0,"normal refusals do not terminate startup");
	memory mem{std::vector<unsigned char>(4096,0)};
	std::copy(original.begin(),original.end(),mem.bytes.begin()+32);const auto before=mem.bytes;
	xml1_hook_transaction::patch patch{32,{original.begin(),original.end()},std::vector<unsigned char>(original.size(),0x90)};
	bool safe=false;mem.fail_write=1;
	check(!xml1_hook_transaction::apply(mem,{patch},&safe) && safe && mem.bytes==before,"partial write rolls back exact bytes");
	mem.fail_write=-1;check(xml1_hook_transaction::apply(mem,{patch},&safe),"readback transaction succeeds");
	check(!xml1_hook_transaction::apply(mem,{patch},&safe),"already modified site rejected");
	std::printf("box harm: %d checks, %d failures\n",checks,failures);
	return failures?1:0;
}
