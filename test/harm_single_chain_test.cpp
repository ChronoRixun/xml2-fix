#include <Windows.h>
#include "harm_single_chain.hpp"
#include "harm_single_chain_bridge.hpp"
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
		for (const auto& g:harm_single_chain_rules::guards) { const auto b=decode_hex(g.hex);std::memcpy(at(g.va),b.data(),b.size()); }
	}
	bool untouched() { return !harm_single_chain_rules::first_mismatch(at(0x400000)); }

	std::uint32_t entity[0x100]{}, target=harm_single_chain_rules::tail_site;
	std::uint32_t captured_eax=0, captured_edx=0;
	float observed_fpu=0;
	__declspec(naked) void tail_continuation()
	{
		__asm { mov byte ptr [esi+0xc4], dl
			mov captured_eax, eax
			mov captured_edx, edx
			ret }
	}
	__declspec(naked) void request_continuation()
	{
		__asm { mov captured_eax, eax
			mov captured_edx, edx
			ret }
	}
	__declspec(naked) void drive()
	{
		__asm {
			pushad
			lea esi, entity
			mov eax, 0x12345678
			mov edx, 0x3456789a
			fld1
			call dword ptr [target]
			fstp observed_fpu
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
		for(const auto& g:harm_single_chain_rules::guards){
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
	using namespace harm_single_chain_rules;
	constexpr auto site=tail_site, resume=tail_resume;
	const auto& original=tail_original;
	if(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))!=0x400000 || reinterpret_cast<std::uintptr_t>(fixture_space)>site)return 2;
	DWORD old=0;if(!VirtualProtect(at(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old))return 2;
	if(argc>1)raw_guards(argv[1]);else{std::puts("FAIL retail executable argument required");return 2;}
	if(failures)return 1;
	seed();harm_single_chain::install(GetModuleHandleW(nullptr));
	check(!harm_single_chain::requested() && !harm_single_chain::installed() && untouched() && rejected==0,"absent key preserves code and online agreement");
	setting="off";harm_single_chain::install(GetModuleHandleW(nullptr));
	check(!harm_single_chain::requested() && untouched() && rejected==0,"explicit native mode preserves code and agreement");
	setting="invalid";harm_single_chain::install(GetModuleHandleW(nullptr));
	check(harm_single_chain::requested() && !harm_single_chain::installed() && untouched() && rejected==1,"invalid request changes no code and remains unverified");
	setting="single";harm_single_chain::install(reinterpret_cast<HMODULE>(0x500000));
	check(!harm_single_chain::installed() && untouched(),"relocated executable refused");
	for(const auto& g:guards){
		seed();*at(g.va)^=1;
		const auto before=std::vector<unsigned char>(at(site),at(site)+original.size());
		harm_single_chain::install(GetModuleHandleW(nullptr));
		check(!harm_single_chain::installed() && std::memcmp(at(site),before.data(),before.size())==0,"each dependency mismatch prevents writes");
	}
	seed();const std::vector<unsigned char> image_before(at(0x401000),at(0x801000));
	harm_single_chain::install(GetModuleHandleW(nullptr));
	check(harm_single_chain::installed() && *at(site)==0xe9 && rejected>0,"production installer succeeds with agreement blocked pending rule assignment");
	bool footprint=true;
	for(std::size_t i=0;i<image_before.size();++i)
		if(at(0x401000)[i]!=image_before[i] && !(0x401000+i>=site && 0x401000+i<resume) && !(0x401000+i>=request_site && 0x401000+i<request_resume))footprint=false;
	check(footprint,"installer changes only the two guarded pending-clear sites");

	harm_single_chain_bridge::tail_resume=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&tail_continuation));
	harm_single_chain_bridge::request_resume=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&request_continuation));
	for(unsigned smart=0;smart<2;++smart)for(unsigned box=0;box<2;++box)
	for(unsigned touch=0;touch<2;++touch)for(unsigned loop=0;loop<2;++loop)
	for(unsigned toggle=0;toggle<2;++toggle)for(unsigned repeat=0;repeat<2;++repeat){
		entity[0]=0x684224;
		entity[1]=0x100000|(touch?0x80000:0);
		entity[0x360/4]=(smart?8:0)|(box?2:0);
		reinterpret_cast<unsigned char*>(entity)[0xc5]=(loop?4:0)|(toggle?8:0);
		entity[0xa4/4]=repeat?0x3ecccccd:0;
		const bool keep=!smart && box && touch && loop && !toggle && repeat;
		for(auto entry_site:{tail_site,request_site})for(unsigned pending:{7u,0x87u}){
			reinterpret_cast<unsigned char*>(entity)[0xc4]=static_cast<unsigned char>(pending);
			target=entry_site;drive();
			check(reinterpret_cast<unsigned char*>(entity)[0xc4]==(keep?pending:7),"only qualifying loops retain pending; unrelated flags survive and no pending bit is invented");
			check(observed_fpu==1,"FPU depth and value preserved");
			if(entry_site==request_site)check(captured_eax==0x12345678 && captured_edx==0x3456789a,"request return registers preserved");
			else check(captured_eax==(0x12345600|entity[0x360/4]) && (captured_edx&0xffffff00)==0x34567800,"tail native loaded values and upper register bytes preserved");
		}
	}
	entity[0]=0x684224;entity[1]=0x180000;entity[0x360/4]=2;
	reinterpret_cast<unsigned char*>(entity)[0xc5]=4;
	for(unsigned bits:{0u,0x80000000u,0xbecccccd,0x7f800000u,0x7fc00000u}){
		entity[0xa4/4]=bits;
		for(auto entry_site:{tail_site,request_site}){
			reinterpret_cast<unsigned char*>(entity)[0xc4]=0x87;target=entry_site;drive();
			check(reinterpret_cast<unsigned char*>(entity)[0xc4]==7,"zero, negative and nonfinite intervals retain native clearing");
		}
	}
	entity[0]=0x12340000;entity[0xa4/4]=0x3ecccccd;
	for(auto entry_site:{tail_site,request_site}){
		reinterpret_cast<unsigned char*>(entity)[0xc4]=0x87;target=entry_site;drive();
		check(reinterpret_cast<unsigned char*>(entity)[0xc4]==7,"different entity class retains native clearing");
	}
	entity[0]=0x684224;
	entity[0x360/4]=3;
	for(auto entry_site:{tail_site,request_site}){
		reinterpret_cast<unsigned char*>(entity)[0xc4]=0x87;target=entry_site;drive();
		check(reinterpret_cast<unsigned char*>(entity)[0xc4]==7,"mixed radius and box harm retains native clearing");
	}
	entity[0x360/4]=2;
	for(unsigned timer:{0x364u,0x368u}){
		entity[timer/4]=0x3f800000;
		for(auto entry_site:{tail_site,request_site}){
			reinterpret_cast<unsigned char*>(entity)[0xc4]=0x87;target=entry_site;drive();
			check(reinterpret_cast<unsigned char*>(entity)[0xc4]==7,"authored on/off cycles retain native clearing");
		}
		entity[timer/4]=0;
	}
	check(refused==0,"normal refusals do not terminate startup");

	memory mem{std::vector<unsigned char>(4096,0)};
	std::copy(tail_original.begin(),tail_original.end(),mem.bytes.begin()+32);
	std::copy(request_original.begin(),request_original.end(),mem.bytes.begin()+96);
	const auto before=mem.bytes;
	std::vector<xml1_hook_transaction::patch> patches{
		{32,{tail_original.begin(),tail_original.end()},std::vector<unsigned char>(tail_original.size(),0x90)},
		{96,{request_original.begin(),request_original.end()},std::vector<unsigned char>(request_original.size(),0x90)}};
	bool safe=false;mem.fail_write=2;
	check(!xml1_hook_transaction::apply(mem,patches,&safe) && safe && mem.bytes==before,"second partial write restores both sites exactly");
	mem.fail_write=-1;check(xml1_hook_transaction::apply(mem,patches,&safe),"two-site readback transaction succeeds");
	check(!xml1_hook_transaction::apply(mem,patches,&safe),"already modified sites rejected");
	std::printf("harm single chain: %d checks, %d failures\n",checks,failures);
	return failures?1:0;
}
