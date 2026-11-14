// Execute the production regroup predicate with invented actors and native services.
// Retail bytes are used only for install guards, in this host's private image fixture.
#include "../src/solo_mode.cpp"
#include "gameplay_capabilities.hpp"
#include <cstdio>
#include <fstream>
#include <string>

namespace { bool feature_on=true; }
namespace ini { bool flag(const wchar_t*, const wchar_t* key, bool) { return std::wcscmp(key,L"SoloMode")==0 && feature_on; } }
namespace logger { void write(const char*, ...) {} }
namespace game_version { void refuse_startup() { std::abort(); } }
namespace e4_observe {
int native_exception(const char*, EXCEPTION_POINTERS*) { return EXCEPTION_CONTINUE_SEARCH; }
[[noreturn]] void fatal(const char*) { std::abort(); }
}
namespace {
using u32 = std::uint32_t;
std::array<std::array<unsigned char, 0x800>, 4> actors{};
std::array<unsigned char, 0x40> concrete{};
std::array<void*, 2> actor_vt{};
std::array<unsigned char, 0x20> nav{};
std::array<unsigned char, 8> camera{};
int queries = 0, resets = 0, valid_calls = 0, world_calls = 0;
int path_status = 2, checks = 0, failures = 0;
constexpr u32 world_floor = 0x2200, platform_floor = 0x1108, stale_floor = 0x3308;
template<class T> void put(void* p, u32 off, T value) { std::memcpy(static_cast<unsigned char*>(p)+off, &value, sizeof value); }
template<class T> void jump(u32 address, T target) {
    auto* p = reinterpret_cast<unsigned char*>(address); p[0] = 0xe9;
    put(p, 1, static_cast<u32>(reinterpret_cast<std::uintptr_t>(target))-address-5);
}
void check(bool ok, const char* label) { ++checks; failures += !ok; std::printf("%s %s\n", ok?"PASS":"FAIL", label); }
const void* __fastcall class_stub(void*, void*) { return concrete.data(); }
u32* __cdecl lookup_stub(u32* out, const char* name) { *out = name[5]>='1' && name[5]<='4' ? u32(name[5]-'0') : 1; return out; }
void* __fastcall resolve_stub(const u32* handle, void*) { return *handle>=1 && *handle<=4 ? actors[*handle-1].data() : nullptr; }
bool __fastcall valid_stub(const u32* handle, void*) { ++valid_calls; return *handle!=0 && *handle!=stale_floor; }
u32* __cdecl world_stub(u32* out) { ++world_calls; *out=world_floor; return out; }
void __fastcall reset_stub(void*, void*) { ++resets; }
void* __cdecl camera_stub() { return camera.data(); }
bool __fastcall visible_stub(void*,void*,const float*,float,float*) { return true; }
int __fastcall query_stub(void*, void*, const float*, float radius, int steps, int near_only) {
    check(radius==80 && steps==200 && near_only==1,"native path query arguments preserved"); ++queries; return path_status;
}
void load_image(const char* path) {
    constexpr u32 image_size=0x700000;
    auto* image=reinterpret_cast<void*>(0x400000); DWORD old=0;
    if(!VirtualProtect(reinterpret_cast<void*>(0x401000),image_size,PAGE_EXECUTE_READWRITE,&old)) { std::printf("Fixture protection failed: %lu\n",GetLastError()); std::exit(2); }
    std::ifstream in(path,std::ios::binary); std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(in)),{});
    if(bytes.size()<0x1000) { std::printf("Cannot read retail image: %s\n",path); std::exit(2); }
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    const auto* pe=reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data()+dos->e_lfanew);
    const auto* sections=IMAGE_FIRST_SECTION(pe);
    for(unsigned i=0;i<pe->FileHeader.NumberOfSections;++i) {
        const auto& s=sections[i];
        if(s.VirtualAddress+s.SizeOfRawData>image_size || s.PointerToRawData+s.SizeOfRawData>bytes.size()) { std::printf("Section %u exceeds image bounds: va %lx raw %lx size %lx\n",i,s.VirtualAddress,s.PointerToRawData,s.SizeOfRawData); std::exit(2); }
        std::memcpy(static_cast<unsigned char*>(image)+s.VirtualAddress,bytes.data()+s.PointerToRawData,s.SizeOfRawData);
    }
}
void setup() {
    jump(0x4a1700,&lookup_stub); jump(0x4654b0,&resolve_stub);
    jump(0x4654d0,&valid_stub); jump(0x45fe70,&world_stub);
    jump(0x4457e0,&camera_stub);
    put(camera.data(),0,u32(0x684cc4));
    put(reinterpret_cast<void*>(0x684cc4),0x178,reinterpret_cast<void*>(&visible_stub));
    actor_vt[0]=reinterpret_cast<void*>(&class_stub);
    put(reinterpret_cast<void*>(0x70b840),0,u32(0)); put(concrete.data(),0x18,u32(16));
    put(reinterpret_cast<void*>(0x689b78),0,reinterpret_cast<void*>(&query_stub));
    put(reinterpret_cast<void*>(0x689b78),4,reinterpret_cast<void*>(&reset_stub));
    put(nav.data(),0,u32(0x689b78));
    for(unsigned i=0;i<4;++i) {
        actors[i].fill(0); put(actors[i].data(),0,actor_vt.data()); put(actors[i].data(),0x1c,u32(i+1));
        put(actors[i].data(),0x27c,100.0f); put(actors[i].data(),0x3d4,0); put(actors[i].data(),0xbc,world_floor);
        put(actors[i].data(),0x3e8,nav.data());
    }
    queries=resets=valid_calls=world_calls=0; path_status=2;
    FlushInstructionCache(GetCurrentProcess(),nullptr,0);
}
bool regroup(bool manual=false, bool all=true) { return solo_mode::regroup_allowed(actors[0].data(),manual,all); }
}
int main(int argc,char** argv) {
    std::setbuf(stdout,nullptr); if(argc!=2) return 2;
    load_image(argv[1]); const auto game=reinterpret_cast<HMODULE>(0x400000);
    feature_on=false; const auto original=solo_mode::at<u32>(reinterpret_cast<void*>(0x51ee14),0);
    solo_mode::install(game);
    check(!solo_mode::installed() && solo_mode::capability().verified && solo_mode::capability().values==std::array<u32,4>{} && original==solo_mode::at<u32>(reinterpret_cast<void*>(0x51ee14),0),"disabled mode keeps code and agreement unchanged");
    feature_on=true;
    for(u32 address:{0x419f29u,0x4654d0u,0x45fe70u}) {
        auto* byte=reinterpret_cast<unsigned char*>(address); *byte^=1; solo_mode::install(game);
        check(!solo_mode::installed() && !solo_mode::capability().verified && original==solo_mode::at<u32>(reinterpret_cast<void*>(0x51ee14),0),"floor service guard mismatch refuses entire install and online agreement");
        *byte^=1;
    }
    solo_mode::install(game);
    check(solo_mode::installed(),"retail solo install guards pass"); setup();
    const auto cap=solo_mode::capability();
    check(cap.verified && cap.values==std::array<u32,4>{1,2,0,0},"installed production capability is rule 8 decisions revision 2");
    auto record=[](std::array<u32,4> values) { gameplay_capabilities::record r; r.add(gameplay_capabilities::solo_mode_id,1,values,true); r.finalize(); return r; };
    const auto current=record(cap.values), old=record({1,1,0,0});
    check(current.compare(old.bytes())==gameplay_capabilities::result::value && old.compare(current.bytes())==gameplay_capabilities::result::value,"old and new solo decisions refuse online agreement in both directions");
    check(current.compare(record(cap.values).bytes())==gameplay_capabilities::result::match,"matching new solo decisions agree");
    gameplay_capabilities::difference difference; char text[128]{};
    current.compare(old.bytes(),&difference); current.describe(difference.buckets,difference.local_only,true,text,sizeof text);
    check(std::string(text)=="8 SoloMode","mixed-version refusal names rule 8 SoloMode");
    check(regroup() && queries==3,"ordinary world floor permits reachable party");
    setup(); put(actors[0].data(),0xbc,platform_floor);
    check(!regroup() && queries==0,"leader on moving platform stays solo before navigation");
    check(!regroup(true) && queries==0,"manual regroup also refuses leader on platform");
    check(!regroup(false,false) && queries==0,"CheckSolo teleport also tests leader floor");
    setup(); put(actors[2].data(),0xbc,platform_floor);
    check(!regroup() && queries==1,"living peer on platform refuses before its path query");
    setup(); put(actors[0].data(),0xbc,stale_floor);
    check(regroup(),"stale floor handle falls through to navigation as XML1 does");
    setup(); put(actors[0].data(),0xbc,u32(0));
    check(regroup(),"no floor handle falls through to navigation as XML1 does");
    setup(); put(actors[2].data(),0xbc,platform_floor); put(actors[2].data(),0x27c,0.0f);
    check(regroup() && queries==2,"dead peer on platform is excluded");
    setup(); put(actors[2].data(),0xbc,platform_floor); put(actors[2].data(),0x3d4,1);
    check(regroup() && queries==2,"peer in another controller group is excluded");
    setup(); path_status=0; check(!regroup(),"unreachable world floor still refuses");
    setup(); put(actors[1].data(),0xbc,platform_floor);
    check(!regroup(false,false),"CheckSolo refuses first reachable peer on platform");
    setup(); put(actors[2].data(),0xbc,platform_floor);
    check(regroup(false,false) && queries==1,"CheckSolo still stops at first reachable peer");
    std::printf("%d checks, %d failures\n",checks,failures); return failures?1:0;
}
