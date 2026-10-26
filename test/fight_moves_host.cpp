// Native union fixture: game instructions execute in our reserved image; no game starts.
#include <Windows.h>
#include "fight_moves.hpp"
#include "fight_moves_layout.hpp"
#include "gameplay_capabilities.hpp"
#include "limits.cpp" // exercise the production MoveTriggers callbacks and contribution
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <iterator>
#include <cassert>
#include <thread>

namespace {
using u32=std::uint32_t;
unsigned checks=0, constructed=0, destroyed=0;
std::string mode;
std::optional<std::string> moves, triggers;
gameplay_capabilities::record agreement;
bool unsafe=false;
std::vector<std::string> logs;
void check(bool ok,const char* what){++checks;if(!ok){std::printf("FAIL %s\n",what);std::fflush(stdout);std::abort();}}
u32 addr(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
void* ptr(u32 p){return reinterpret_cast<void*>(p);}
u32& word(void* p,u32 o=0){return *reinterpret_cast<u32*>(static_cast<unsigned char*>(p)+o);}
template<class T>void jump(u32 va,T f){*static_cast<unsigned char*>(ptr(va))=0xe9;word(ptr(va),1)=addr(reinterpret_cast<const void*>(f))-va-5;}
using unary=void*(__fastcall*)(void*,void*);
using alloc_t=bool(__fastcall*)(void*,void*,void*,void*);
std::array<u32,64> movevt{};
void* __fastcall move_ctor(void* p,void*){++constructed;std::memset(p,0,0x148);word(p)=addr(movevt.data());word(p,8)=constructed;return p;}
void __fastcall move_dtor(void*,void*,int){++destroyed;}
void* __fastcall move_key(void* p,void*){return static_cast<unsigned char*>(p)+8;}
void __fastcall parse_move(void*,void*,void*,void*,void*){}
void load(const char* exe){
    DWORD protection=0;check(VirtualProtect(ptr(0x401000),0x700000,PAGE_EXECUTE_READWRITE,&protection)!=0,"union image writable");
    std::ifstream f(exe,std::ios::binary);std::vector<unsigned char> b{std::istreambuf_iterator<char>(f),{}};
    check(!b.empty(),"retail read");
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(b.data()+word(b.data(),0x3c));
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=IMAGE_FIRST_SECTION(nt)[i];
        check(s.VirtualAddress>=0x1000&&s.VirtualAddress+std::max(s.Misc.VirtualSize,s.SizeOfRawData)<0x701000,"reserved image bounds");
        std::memset(ptr(0x400000+s.VirtualAddress),0,s.Misc.VirtualSize);
        std::memcpy(ptr(0x400000+s.VirtualAddress),b.data()+s.PointerToRawData,s.SizeOfRawData);
    }
}
void layouts(){
    using namespace fight_moves_layout;
    const auto r=layout_for(332);
    check(r.bitmap_a==0x1a960&&r.ring==0x1a98c&&r.write==0x1aec0&&r.read==0x1aec4&&r.free==0x1aec8&&r.bitmap_b==0x1aecc&&r.live==0x1aef8&&r.size==0x1aefc,"retail layout");
    const auto a=layout_for(1024);check(a.bitmap_a==0x52000&&a.words==32&&a.live==0x53110&&a.size==0x53114,"1024 layout");
    const auto b=layout_for(2048);check(b.bitmap_a==0xa4000&&b.words==64&&b.live==0xa6210&&b.size==0xa6214,"2048 layout");
    check(capacity(std::nullopt)==332&&capacity("332")==332&&capacity("333")==333&&capacity("2048")==2048,"key boundaries");
    for(const auto* v:{"331","0","-1","2049","1024x","999999999999999"})check(capacity(v)==332,"invalid key");
}
void rules(){
    using namespace gameplay_capabilities;
    record a,b;check(a.layout(1,{1,1,2,8},true)&&b.layout(1,{1,1,2,8},true),"rule layout");
    check(a.add(fight_moves_id,1,{1024,0,0,0},true),"rule 54");a.finalize();b.finalize();
    difference d;check(a.compare(b.bytes(),&d)==result::entries&&d.local_only,"rule present vs absent");
    check(b.compare(a.bytes(),&d)==result::entries&&d.peer_only,"absent vs rule present");
    record c;check(c.layout(1,{1,1,2,8},true)&&c.add(fight_moves_id,1,{2048,0,0,0},true)&&c.finalize(),"different capacity record");
    check(a.compare(c.bytes(),&d)==result::value&&(d.buckets&(std::uint64_t{1}<<bucket_of(fight_moves_id))),"capacity mismatch forward");
    check(c.compare(a.bytes(),&d)==result::value&&(d.buckets&(std::uint64_t{1}<<bucket_of(fight_moves_id))),"capacity mismatch reverse");
    check(std::string(rule_name(fight_moves_id))=="FightMoves","named mismatch");
}
}
namespace ini {
std::optional<std::string> text(const wchar_t*,const wchar_t* k){return std::wcscmp(k,L"FightMoves")==0?moves:std::wcscmp(k,L"MoveTriggers")==0?triggers:std::nullopt;}
bool flag(const wchar_t*,const wchar_t*,bool fallback){return fallback;}
}
namespace logger {
void write(const char* fmt,...){char s[2048];va_list a;va_start(a,fmt);std::vsnprintf(s,sizeof s,fmt,a);va_end(a);logs.emplace_back(s);}
void write_once(std::string_view,const char*,...){}
}
namespace game_version {
bool require_gameplay_agreement(HMODULE){return true;}
void refuse_startup(){unsafe=true;}
}
namespace gameplay_agreement {
bool register_rule(std::uint16_t id,std::uint16_t revision,std::array<u32,4> values,bool verified){return agreement.add(id,revision,values,verified);}
}
// Integration (w3/integrated): limits.cpp now also names the 080d rows (ResidentStats, MotionPool) and the W3 rows
// that limits::install / limits::status call. This host drives only MoveTriggers' contribution, never those two
// functions, so link-only stand-ins keep it building; calling one aborts.
namespace motion_pool {const descriptor& layout(){std::abort();} std::string_view counter_guard_hex(){std::abort();}}
namespace resident_stats {std::string status(){std::abort();}}
namespace model_pools {void install(HMODULE){std::abort();}}
namespace particle_scratch {void install(HMODULE){std::abort();}}
namespace effect_templates {void install(HMODULE){std::abort();} std::string status(){std::abort();}}
namespace primitives_per_effect {void install(HMODULE){std::abort();} std::string status(){std::abort();}}
namespace effect_pools {void install(HMODULE){std::abort();} std::string status(){std::abort();}}
namespace script_pools {std::string status(){std::abort();}}
namespace power_pools {void install(HMODULE){std::abort();} void register_agreement(){std::abort();} std::string status(){std::abort();}}

int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    AddVectoredExceptionHandler(1,[](EXCEPTION_POINTERS* e)->LONG{std::printf("exception %08lx at %08lx esp=%08lx return=%08lx ecx=%08lx eax=%08lx\n",e->ExceptionRecord->ExceptionCode,e->ContextRecord->Eip,e->ContextRecord->Esp,word(ptr(e->ContextRecord->Esp)),e->ContextRecord->Ecx,e->ContextRecord->Eax);for(int i=0;i<20;++i)std::printf("%08lx ",word(ptr(e->ContextRecord->Esp),i*4));std::puts("");return EXCEPTION_CONTINUE_SEARCH;});
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    check(argc>=2,"retail exe argument");mode=argc>2?argv[2]:"1024";
    layouts();rules();load(argv[1]);
    const auto before=std::vector<unsigned char>(static_cast<unsigned char*>(ptr(0x401000)),static_cast<unsigned char*>(ptr(0xa74000)));
    if(mode!="off"&&mode!="negative"&&mode!="retail")moves=mode=="stock"?"332":mode=="invalid"?"2049":mode=="333"?"333":mode=="2048"?"2048":"1024";
    if(mode=="combo"||mode=="failfast")triggers="64";
    if(mode=="exists")word(ptr(0x789e1c))=0x12345678;
    if(mode=="badguard")*static_cast<unsigned char*>(ptr(0x500de0))^=1;
    if(mode.starts_with("guard")){
        const unsigned i=std::stoul(mode.substr(5));
        const auto va=i<fight_moves_layout::sites.size()?fight_moves_layout::sites[i].va:fight_moves_layout::guards[i-fight_moves_layout::sites.size()].va;
        *static_cast<unsigned char*>(ptr(va))^=1;
    }
    patch_transaction::hooks hooks;
    if(mode=="protect")hooks.protect=[](void*,std::size_t,DWORD,DWORD*){return false;};
    if(mode=="rollback")hooks.flush=[first=true](void* p,std::size_t n)mutable{if(first){first=false;return false;}return FlushInstructionCache(GetCurrentProcess(),p,n)!=0;};
    if(mode=="retain")hooks.flush=[](void*,std::size_t){return false;};
    if(mode=="readback")hooks.after_apply=[](){*static_cast<unsigned char*>(ptr(0x500de0))^=1;};
    const auto install_before=std::vector<unsigned char>(static_cast<unsigned char*>(ptr(0x401000)),static_cast<unsigned char*>(ptr(0xa74000)));
    fight_moves::install(reinterpret_cast<HMODULE>(mode=="base"?0x401000:0x400000),&hooks);
    const bool raised=mode=="1024"||mode=="333"||mode=="2048"||mode=="combo"||mode=="failfast"||mode=="counters";
    check(fight_moves::installed()==raised,"installation outcome");
    if(!raised){
        check(std::memcmp(install_before.data(),ptr(0x401000),install_before.size())==0,"refusal/OFF writes zero bytes");
        check(fight_moves::descriptor().count==332,"no descriptor publication on refusal");
        check(unsafe==(mode=="retain"),"uncertified rollback alone refuses startup");
        if(mode!="negative"&&mode!="retail"){std::printf("PASS FightMoves %s: %u checks\n",mode.c_str(),checks);return 0;}
    }
    patch_transaction::transaction startup;
    if(triggers){check(limits::contribute(startup,reinterpret_cast<HMODULE>(0x400000),false),"MoveTriggers prepares after descriptor");check(startup.commit()==patch_transaction::outcome::committed,"MoveTriggers transaction");}
    using namespace fight_moves_layout;
    const u32 n=raised?fight_moves::descriptor().count:332;const auto layout=layout_for(n);
    std::vector<unsigned char> storage(layout.size+128,0xa5);void* pool=storage.data()+64;
    word(ptr(pool_pointer))=addr(pool);reinterpret_cast<unary>(0x4f7a40)(pool,nullptr);
    check(word(pool,layout.free)==n&&word(pool,layout.live)==0,"native pool constructor");
    if(mode=="counters"){
        word(pool,layout.live)=n+1;word(pool,layout.free)=0xffffffff;
        std::array<std::thread,8> readers;
        for(auto& reader:readers)reader=std::thread([](){for(int i=0;i<10000;++i)fight_moves::status();});
        for(auto& reader:readers)reader.join();
        check(fight_moves::counters().aliasing==80000,"concurrent status samples preserve every alias observation");
        std::printf("PASS FightMoves counters: %u checks, 80000 concurrent samples\n",checks);return 0;
    }
    movevt[0]=addr(reinterpret_cast<const void*>(&move_dtor));movevt[0xd0/4]=addr(reinterpret_cast<const void*>(&move_key));
    jump(0x500540,&move_ctor);jump(0x4f7ef0,&parse_move);
    // Native style construction and native ordered map insertion/balancing remain intact.
    std::vector<std::array<unsigned char,0xec8>> styles((n+149)/150+1);
    for(auto& style:styles)reinterpret_cast<unary>(0x500fe0)(style.data(),nullptr);
    if(mode=="failfast"){
        limits::construct_move(static_cast<unsigned char*>(pool)+n*record_size,nullptr);
        check(false,"outside-pool address must terminate");
    }
    const auto allocate=reinterpret_cast<alloc_t>(0x500de0);
    for(u32 i=0;i<n;++i){check(allocate(styles[i/150].data(),nullptr,nullptr,nullptr),"native allocation");
        check(word(pool,layout.live)==i+1&&word(pool,layout.free)==n-i-1,"native pop counts");
        if(triggers){const auto index=move_triggers_rules::slot(fight_moves::descriptor(),addr(pool)+i*record_size);check(index==i,"descriptor slot");
            check(word(static_cast<unsigned char*>(pool)+i*record_size,0xcc)==addr(limits::move_trigger_arrays+i*64),"high slot side array");}
    }
    const auto snapshot=std::vector<unsigned char>(static_cast<unsigned char*>(pool),static_cast<unsigned char*>(pool)+layout.size);
    if(mode=="negative"){
        check(allocate(styles.back().data(),nullptr,nullptr,nullptr),"retail 333rd allocation succeeds unsafely");
        check(word(pool,8)==333&&word(pool,layout.live)==333&&static_cast<int>(word(pool,layout.free))==-1,"negative control constructs over live slot zero");
        check(fight_moves::status().find("aliasing 1")!=std::string::npos,"alias detector observes retail corruption");
        std::printf("PASS negative control: retail allocation 333 overwrites live slot 0; free=-1; %u checks\n",checks);return 0;
    }
    if(raised){
        const auto empty_style=styles.back();
        check(!allocate(styles.back().data(),nullptr,nullptr,nullptr),"N+1 refused");
        check(!allocate(styles.back().data(),nullptr,nullptr,nullptr),"repeated pool refusal");
        check(std::memcmp(pool,snapshot.data(),snapshot.size())==0&&constructed==n,"N+1 touches no pool record or metadata");
        check(styles.back()==empty_style,"pool refusal leaves target style map intact");
        check(fight_moves::counters().refused==2&&fight_moves::counters().aliasing==0,"refusal counted, no alias");
        check(std::count_if(logs.begin(),logs.end(),[](auto& s){return s.find("(pool full)")!=std::string::npos;})==1,"pool refusal logs once per style");
    }
    // Free via native stride division and map removal, then allocate again at the ring tail.
    u32 firstkey=1;
    check(reinterpret_cast<bool(__fastcall*)(void*,void*,u32*)>(0x500b60)(styles[0].data(),nullptr,&firstkey),"native map removal and stride division");
    check(word(pool,layout.live)==n-1&&word(styles[0].data(),0xc60)==149,"native free counts");
    if(raised){
        const auto fullstyle=styles[1];
        check(!allocate(styles[1].data(),nullptr,nullptr,nullptr),"style 151 refused while pool has room");
        check(!allocate(styles[1].data(),nullptr,nullptr,nullptr),"repeated style map refusal");
        check(styles[1]==fullstyle&&word(pool,layout.live)==n-1,"full map intact");
        check(fight_moves::counters().map_refused==2,"map refusal counted");
        check(std::count_if(logs.begin(),logs.end(),[](auto& s){return s.find("(style map full)")!=std::string::npos;})==1,"map refusal logs once per style");
    }
    check(allocate(styles[0].data(),nullptr,nullptr,nullptr),"released slot reused");
    std::vector<u32> boundaries{31u,32u,331u,332u,n-1};
    std::sort(boundaries.begin(),boundaries.end());boundaries.erase(std::unique(boundaries.begin(),boundaries.end()),boundaries.end());
    for(const u32 index:boundaries){
        if(index>=n)continue;u32 key=index+1;
        check(reinterpret_cast<bool(__fastcall*)(void*,void*,u32*)>(0x500b60)(styles[index/150].data(),nullptr,&key),"native map removal across bitmap boundaries");
        check(allocate(styles[index/150].data(),nullptr,nullptr,nullptr),"native ring wrap reuse");
    }
    for(u32 i=0;i<n;++i)check(reinterpret_cast<int(__fastcall*)(void*,void*,int,bool)>(0x4f5970)(static_cast<unsigned char*>(pool)+layout.bitmap_a,nullptr,i,true)==static_cast<int>(i),"native findNext");
    for(u32 i=0;i<n;++i){
        auto* move=static_cast<unsigned char*>(pool)+i*record_size;u32 key=word(move,8);
        check(reinterpret_cast<void*(__fastcall*)(void*,void*,u32*)>(0x500b20)(ptr(word(move,4)),nullptr,&key)==move,"every live move remains in the native style map");
    }
    const auto was_destroyed=destroyed;reinterpret_cast<void(__fastcall*)(void*,void*)>(0x4f6260)(pool,nullptr);
    check(destroyed-was_destroyed==n,"native teardown visits all live records");
    for(u32 i=0;i<layout.words;++i)check(word(pool,layout.bitmap_a+i*4)==0,"teardown clears all bitmap words");
    check(std::all_of(storage.begin(),storage.begin()+64,[](auto c){return c==0xa5;})&&std::all_of(storage.end()-64,storage.end(),[](auto c){return c==0xa5;}),"pool canaries");
    check(fight_moves::counters().aliasing==0&&!unsafe,"no alias or unsafe rollback");
    std::printf("PASS FightMoves %s: %u checks; %u constructed, %u destroyed\n",mode.c_str(),checks,constructed,destroyed);
}
