#include <Windows.h>
#include "harm_loop_startup.hpp"
#include "harm_loop_startup_bridge.hpp"
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
		for (const auto& g:harm_loop_startup_rules::guards) { const auto b=decode_hex(g.hex);std::memcpy(at(g.va),b.data(),b.size()); }
	}
	bool untouched() { return !harm_loop_startup_rules::first_mismatch(at(0x400000)); }

	std::uint32_t entity[0x100]{}, vtable[0x40]{};
	unsigned path=0, disables=0;
	std::uint32_t before_flags=0, captured_flags=0;
	float observed_fpu=0;
	void __fastcall disable(void*,void*) { ++disables; }
	__declspec(naked) void ordinary_continuation()
	{
		__asm { pushfd
			pop captured_flags
			mov path, 1
			ret }
	}
	__declspec(naked) void native_continuation()
	{
		__asm { mov path, 2
			ret }
	}
	std::uint32_t target=harm_loop_startup_rules::site;
	__declspec(naked) void drive()
	{
		__asm {
			pushad
			lea esi, entity
			fld1
			cmp eax, eax
			pushfd
			pop before_flags
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
		for(const auto& g:harm_loop_startup_rules::guards){
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
	using namespace harm_loop_startup_rules;
	if(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))!=0x400000 || reinterpret_cast<std::uintptr_t>(fixture_space)>site)return 2;
	DWORD old=0;if(!VirtualProtect(at(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old))return 2;
	if(argc>1)raw_guards(argv[1]);else{std::puts("FAIL retail executable argument required");return 2;}
	if(failures)return 1;
	seed();harm_loop_startup::install(GetModuleHandleW(nullptr));
	check(!harm_loop_startup::requested() && !harm_loop_startup::installed() && untouched() && rejected==0,"absent key preserves code and online agreement");
	setting="xml2";harm_loop_startup::install(GetModuleHandleW(nullptr));
	check(!harm_loop_startup::requested() && untouched() && rejected==0,"explicit native mode preserves code and agreement");
	setting="invalid";harm_loop_startup::install(GetModuleHandleW(nullptr));
	check(harm_loop_startup::requested() && !harm_loop_startup::installed() && untouched() && rejected==1,"invalid request changes no code and remains unverified");
	setting="xml1";harm_loop_startup::install(reinterpret_cast<HMODULE>(0x500000));
	check(!harm_loop_startup::installed() && untouched(),"relocated executable refused");
	for(const auto& g:guards){
		seed();*at(g.va)^=1;
		const auto before=std::vector<unsigned char>(at(site),at(site)+original.size());
		harm_loop_startup::install(GetModuleHandleW(nullptr));
		check(!harm_loop_startup::installed() && std::memcmp(at(site),before.data(),before.size())==0,"each dependency mismatch prevents writes");
	}
	seed();const std::vector<unsigned char> image_before(at(0x401000),at(0x801000));
	harm_loop_startup::install(GetModuleHandleW(nullptr));
	check(harm_loop_startup::installed() && *at(site)==0xe9 && rejected>0,"production installer succeeds with agreement blocked pending rule assignment");
	bool footprint=true;
	for(std::size_t i=0;i<image_before.size();++i)
		if(at(0x401000)[i]!=image_before[i] && !(0x401000+i>=site && 0x401000+i<resume))footprint=false;
	check(footprint,"installer changes only the ten-byte startup site");

	harm_loop_startup_bridge::resume=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&native_continuation));
	harm_loop_startup_bridge::skip=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&ordinary_continuation));
	entity[0]=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(vtable));
	vtable[0xe8/4]=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&disable));
	for(unsigned smart=0;smart<2;++smart)for(unsigned loop=0;loop<2;++loop)
	for(unsigned on=0;on<2;++on)for(unsigned off=0;off<2;++off){
		entity[0x360/4]=smart?8:0;
		reinterpret_cast<unsigned char*>(entity)[0xc5]=loop?4:0;
		entity[0x364/4]=on?0x3f800000:0;
		entity[0x368/4]=off?0x3f800000:0;
		path=disables=0;
		drive();
		const bool ordinary=!smart && loop && !on && !off;
		check(path==(ordinary?1u:2u) && disables==(ordinary?0u:1u),"only untimed ordinary start-on loops bypass PC disable and timer");
		check(observed_fpu==1.0f,"bridge preserves FPU depth and value");
		if(ordinary)check(before_flags==captured_flags,"ordinary path preserves native flags");
	}
	check(refused==0,"normal refusals do not terminate startup");
	memory mem{std::vector<unsigned char>(4096,0)};
	std::copy(original.begin(),original.end(),mem.bytes.begin()+32);const auto before=mem.bytes;
	xml1_hook_transaction::patch patch{32,{original.begin(),original.end()},std::vector<unsigned char>(original.size(),0x90)};
	bool safe=false;mem.fail_write=1;
	check(!xml1_hook_transaction::apply(mem,{patch},&safe) && safe && mem.bytes==before,"partial write rolls back exact bytes");
	mem.fail_write=-1;check(xml1_hook_transaction::apply(mem,{patch},&safe),"readback transaction succeeds");
	check(!xml1_hook_transaction::apply(mem,{patch},&safe),"already modified site rejected");
	std::printf("harm loop startup: %d checks, %d failures\n",checks,failures);
	return failures?1:0;
}
