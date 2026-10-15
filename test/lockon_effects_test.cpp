#include <Windows.h>
#include "lockon_effects_hooks.hpp"
#include "lockon_effects_rules.hpp"
#include "mold_query_rules.hpp"
#include "gameplay_capabilities.hpp"
#include <cstdio>
#include <algorithm>
#include <fstream>

namespace {
using namespace lockon_effects_hooks;
using u32=std::uint32_t;
int checks=0,failures=0,calls=0;
void check(bool ok,const char* label){++checks;if(!ok){++failures;std::printf("FAIL %s\n",label);}}
u32 saved_sp=0,entry=0,frame_sp=0,owner=0,target=0,condition=0;
u32 out_sp=0,out_si=0,out_di=0,out_cx=0,out_ax=0;
void* seen_owner=nullptr;void* seen_target=nullptr;const void* seen_frame=nullptr;unsigned seen_phase=99;
const void* seen_query=nullptr;
void __cdecl capture(void* a,const void* f,unsigned phase,void* t){++calls;seen_owner=a;seen_frame=f;seen_phase=phase;seen_target=t;}
u32 __fastcall query(const void* p,void*){seen_query=p;return 0x12345678;}
__declspec(naked) void finish(){__asm {
    mov out_sp,esp
    mov out_si,esi
    mov out_di,edi
    mov out_cx,ecx
    mov out_ax,eax
    mov esp,saved_sp
    popad
    popfd
    ret
}}
__declspec(naked) void invoke(){__asm {
    pushfd
    pushad
    mov saved_sp,esp
    mov esp,frame_sp
    mov esi,owner
    mov edi,target
    mov ecx,055667788h
    cmp condition,0
    jmp dword ptr [entry]
}}
u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
struct memory {
    std::vector<unsigned char> data=std::vector<unsigned char>(0x300000);
    int writes=0,fail_write=-1,acquires=0,fail_acquire=-1;
    bool read(u32 a,void* p,std::size_t n){std::memcpy(p,data.data()+a-0x400000,n);return true;}
    bool write(u32 a,const void* p,std::size_t n){std::memcpy(data.data()+a-0x400000,p,n);return ++writes!=fail_write;}
    bool acquire(u32,u32& old){old=4;return ++acquires!=fail_acquire;}
    bool restore(u32,u32){return true;}
    bool flush(){return true;}
};
}
int main(int argc,char** argv) {
    using namespace lockon_effects_rules;
    check(parse("invented/scan,invented/contact,invented/marker").has_value(),"three authored paths");
    for(const auto* bad:{"", "a,b", "a,b,c,d", "../escape,b,c", "/a,b,c", "a/,b,c", "a//b,c,d", "a,b,c.xml", "a,b,c;ignored"})
        check(!parse(bad),"reject malformed contract");
    check(!parse(std::string(128,'x')+",b,c"),"bounded paths");
    struct resource_fixture {
        u32 live=0x101;unsigned loads=0;
        bool valid(u32 h){return h==live;}
        u32 load(const std::string&){++loads;return live;}
    } resources;
    handle_cache cache;const paths names{"invented/beam","invented/surface","invented/target"};
    for(int i=0;i<100000;++i)cache.resolve(0,resources,names);
    check(resources.loads==1,"repeated emissions do not increment the loader's 16-bit counter");
    resources.live=0x201;
    check(cache.resolve(0,resources,names)==0x201&&resources.loads==2,"same index with a new generation reloads after teardown");
    resources.live=0;
    check(cache.resolve(1,resources,names)==0,"failed load emits no stale handle");
    resources.live=0x301;
    check(cache.resolve(1,resources,names)==0x301&&resources.loads==4,"failed load can recover later");

    auto* stack=static_cast<unsigned char*>(VirtualAlloc(nullptr,65536,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!stack)return 2;
    frame_sp=address(stack+32768);std::array<u32,32> a{},b{};b[7]=0x4242;
    owner=address(a.data());target=address(b.data());observe=&capture;
    scan_hit=scan_exit=hit_resume=acquired_resume=locked_resume=address(&finish);query_entity=address(&query);
    entry=address(&miss_bridge);condition=1;invoke();check(calls==0&&out_sp==frame_sp,"successful trace bypasses miss callback");
    condition=0;invoke();check(calls==1&&seen_phase==0&&seen_owner==a.data()&&address(seen_frame)==frame_sp,"miss receives native actor/frame");
    check(out_si==owner&&out_di==target&&out_sp==frame_sp&&out_cx==0x55667788,"miss preserves native registers and stack");
    entry=address(&hit_bridge);invoke();check(seen_phase==1&&seen_query==reinterpret_cast<void*>(frame_sp+0x18)&&out_ax==0x12345678,"hit replays native entity lookup after observing");
    check(out_si==owner&&out_di==target&&out_sp==frame_sp,"hit stack and nonvolatile registers");
    entry=address(&acquired_bridge);invoke();check(seen_phase==2&&seen_target==b.data()&&seen_owner==a.data(),"acquisition callback uses accepted native target");
    check(out_sp==frame_sp-4&&out_ax==out_sp&&out_cx==0x4242&&*reinterpret_cast<u32*>(out_sp)==0x55667788,"acquisition replays displaced native token setup");
    auto* frame=reinterpret_cast<u32*>(frame_sp);frame[2]=0x11223344;frame[3]=0x22334455;
    owner=address(b.data())+0x2c;target=address(a.data());entry=address(&locked_bridge);invoke();
    check(seen_phase==3&&seen_owner==a.data()&&seen_target==b.data()&&address(seen_frame)==frame_sp+8,"locked callback recovers target from its native angles pointer");
    check(out_sp==frame_sp+16&&out_di==0x11223344&&out_si==0x22334455,"locked bridge replays native cleanup");
    VirtualFree(stack,0,MEM_RELEASE);

    const auto visual=plan({0x600000,0x600100,0x600200,0x600300});
    check(visual.size()==4,"visual-only opt-in preserves target query");
    const auto writes=plan({0x600000,0x600100,0x600200,0x600300},true);
    check(writes.size()==5&&writes.back().address==0x4eefb1&&writes.back().before==std::vector<unsigned char>{1}&&writes.back().after==std::vector<unsigned char>{0},"animated head query is part of the same transaction");
    gameplay_capabilities::record pending;
    pending.add(gameplay_capabilities::lockon_aim_id,1,{0,0,0,0},false);
    check(!pending.finalize()&&pending.bytes().empty(),"failed aiming rule cannot advertise stock online agreement");
    memory seed;
    for(const auto& p:writes)std::memcpy(seed.data.data()+p.address-0x400000,p.before.data(),p.before.size());
    const auto initial=seed.data;bool safe=false;
    for(int fail=1;fail<=int(writes.size());++fail){auto m=seed;m.fail_write=fail;check(!xml1_hook_transaction::apply(m,writes,&safe)&&safe&&m.data==initial,"every partial write rolls all sites back");}
    auto denied=seed;denied.fail_acquire=1;check(!xml1_hook_transaction::apply(denied,writes,&safe)&&denied.data==initial,"protection refusal writes nothing");
    auto installed=seed;check(xml1_hook_transaction::apply(installed,writes,&safe)&&safe,"all sites install together");
    for(const auto& p:writes)for(std::size_t i=0;i<p.before.size();++i){auto m=seed;m.data[p.address-0x400000+i]^=1;auto before=m.data;check(!xml1_hook_transaction::apply(m,writes,&safe)&&m.data==before,"every site byte rejects mismatch before writing");}
    mold_query_rules::entry access{0x4a95f0,"8b0185c07505","synthetic accessor"};
    auto stock=bytes(access.retail);check(mold_query_rules::entry_ok(false,stock.data(),access,0),"stock query ABI accepted");
    std::array<unsigned char,6> jump{0xe9,0,0,0,0,0x90};u32 delta=0x600000-access.va-5;std::memcpy(jump.data()+1,&delta,4);
    check(mold_query_rules::entry_ok(true,jump.data(),access,0x600000)&&!mold_query_rules::entry_ok(true,jump.data(),access,0x600010),"only published raised query target accepted");
    if(argc>1) {
        std::ifstream file(argv[1],std::ios::binary);std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)),{});
        if(raw.size()<sizeof(IMAGE_DOS_HEADER))return 2;
        const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
        if(dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<0||std::size_t(dos->e_lfanew)+sizeof(IMAGE_NT_HEADERS32)>raw.size())return 2;
        const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);
        if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC||nt->OptionalHeader.SizeOfImage>0x4000000||std::size_t(dos->e_lfanew)+24+nt->FileHeader.SizeOfOptionalHeader+nt->FileHeader.NumberOfSections*sizeof(IMAGE_SECTION_HEADER)>raw.size())return 2;
        std::size_t required=0;
        for(const auto& g:guards)required=std::max(required,std::size_t(g.va-0x400000)+g.hex.size()/2);
        for(const auto& s:sites)required=std::max(required,std::size_t(s.address-0x400000)+s.before.size()/2);
        if(nt->OptionalHeader.SizeOfImage<required)return 2;
        std::vector<unsigned char> image((std::size_t(nt->OptionalHeader.SizeOfImage)+4095)&~std::size_t(4095));const auto* sec=IMAGE_FIRST_SECTION(nt);
        for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){if(std::uint64_t(sec[i].PointerToRawData)+sec[i].SizeOfRawData>raw.size()||std::uint64_t(sec[i].VirtualAddress)+sec[i].SizeOfRawData>image.size())return 2;std::memcpy(image.data()+sec[i].VirtualAddress,raw.data()+sec[i].PointerToRawData,sec[i].SizeOfRawData);}
        for(const auto& g:guards){auto* p=image.data()+g.va-0x400000;check(limits_rules::matches(p,g.hex),g.what);for(std::size_t i=0;i<g.hex.size()/2;++i){p[i]^=1;check(!limits_rules::matches(p,g.hex),"dependency guard detects every changed byte");p[i]^=1;}}
        for(const auto& s:sites)check(limits_rules::matches(image.data()+s.address-0x400000,s.before),"retail patch site");
    }
    std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;
}
