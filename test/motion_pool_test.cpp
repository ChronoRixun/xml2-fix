// TM2-TM8: execute retail routines in our reserved image with invented services.
// The game's entry point and renderer never run.
#include <Windows.h>
#include "motion_pool.hpp"
#include "motion_pool_sites.hpp"
#include "patch_transaction.hpp"
#include "gameplay_capabilities.hpp"
#include <array>
#include <optional>
#include <vector>
#include <string>
#include <fstream>
#include <iterator>
#include <algorithm>
#include <memory>
#include <cstdio>
#include <cstdarg>
#include <xmmintrin.h>
namespace {
using u32=std::uint32_t;
unsigned checks=0,owner_capacity=127;
bool required=false,unsafe=false;
std::optional<std::string> option;
gameplay_capabilities::record agreement;
std::vector<std::string> messages;
std::vector<unsigned char> retail;
void check(bool ok,const char* why){++checks;if(!ok){std::printf("FAIL %s\n",why);std::fflush(stdout);std::abort();}}
void* ptr(u32 a){return reinterpret_cast<void*>(a);}
u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
template<class T>T get(const void* p,u32 o=0){T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+o,sizeof v);return v;}
template<class T>void put(void* p,u32 o,T v){std::memcpy(static_cast<unsigned char*>(p)+o,&v,sizeof v);}
template<class T>void jump(u32 at,T fn){put<unsigned char>(ptr(at),0,0xe9);put<u32>(ptr(at),1,address(reinterpret_cast<const void*>(fn))-at-5);}
void load(){const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(retail.data()+get<u32>(retail.data(),0x3c));
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=IMAGE_FIRST_SECTION(nt)[i];
        if(s.VirtualAddress>=0x1000&&s.VirtualAddress+s.Misc.VirtualSize<=0x401000){
            std::memset(ptr(0x400000+s.VirtualAddress),0,s.Misc.VirtualSize);
            std::memcpy(ptr(0x400000+s.VirtualAddress),retail.data()+s.PointerToRawData,s.SizeOfRawData);}}
    put<unsigned char>(ptr(0x56b2c9),0,127); // already published ActorSlots in this owned fixture
}
struct allocation {std::vector<unsigned char> bytes;unsigned size;};
std::vector<std::unique_ptr<allocation>> allocations;
std::vector<unsigned> requested_sizes;
void* __cdecl game_alloc(unsigned size,int pool,int tag){
    check(pool==14&&tag==57,"game allocator arguments preserved");
    auto a=std::make_unique<allocation>();a->size=size;a->bytes.assign(size+64,0xa5);
    auto* out=a->bytes.data()+32;allocations.push_back(std::move(a));requested_sizes.push_back(size);return out;}
void __cdecl game_free(void*,int pool){check(pool==14,"game pool free preserved");}
int __cdecl at_exit(void*){return 0;}
std::array<float,3> zero{};
std::array<void*,400> clips{};
std::array<unsigned char,32> list{};
std::array<void*,32> file_vt{};void* file=file_vt.data();void* file_ptr=&file;
void* __fastcall clip_list(void*,void*){return list.data();}
void __fastcall file_service(void*,void*){}
void* __fastcall vector_init(void* p,void*,int,int,int){std::memset(p,0,12);return p;}
void services(){
    jump(0x5605e0,&game_alloc);jump(0x5606a0,&game_free);jump(0x401d50,&at_exit);
    put<u32>(ptr(0x67fa54),0,address(zero.data()));
    put<u32>(ptr(0x67fea8),0,address(reinterpret_cast<void*>(&clip_list)));
    put<u32>(ptr(0x67fa1c),0,address(reinterpret_cast<void*>(&vector_init)));
    put<unsigned char>(ptr(0x7b05e0),0,1);std::memset(ptr(0x7b05c0),0,32);
    file_vt[0x58/4]=reinterpret_cast<void*>(&file_service);
    clips.fill(&file);put(list.data(),0x10,clips.data());
}
using ctor_t=void*(__fastcall*)(void*,void*);
using alloc_t=u32(__fastcall*)(void*,void*);
using lookup_t=void*(__fastcall*)(void*,void*,u32);
using owner_t=void(__fastcall*)(void*,void*,void*,int);
auto construct=reinterpret_cast<ctor_t>(0x56a330);
auto claim=reinterpret_cast<alloc_t>(0x56a3e0);
auto lookup=reinterpret_cast<lookup_t>(0x56a1a0);
auto destroy=reinterpret_cast<void(__fastcall*)(void*,void*)>(0x56a4d0);
auto append=reinterpret_cast<owner_t>(0x56a620);
void* pool(){return ptr(get<u32>(ptr(0x7b05bc)));}
void* create(){return reinterpret_cast<void*(__cdecl*)()>(0x56a480)();}
void close_pool(){reinterpret_cast<void(__cdecl*)()>(0x56a450)();}
void canaries(){for(const auto& a:allocations)for(unsigned i=0;i<32;++i)
    check(a->bytes[i]==0xa5&&a->bytes[a->size+32+i]==0xa5,"pool allocation canaries intact");}
void layout_check(void* p,unsigned n){
    const unsigned words=(n+31)/32,constructed=n*32,ring=constructed+words*4;
    const unsigned enqueue=ring+(n+1)*4,active=enqueue+12,live=active+words*4,generations=live+4;
    unsigned shift=0,mask=0;for(unsigned v=n-1;v;v>>=1){++shift;mask=mask*2+1;}
    check(get<u32>(p,enqueue)==0&&get<u32>(p,enqueue+4)==0&&get<u32>(p,enqueue+8)==n,"TM2 queue fields");
    check(get<u32>(p,live)==0&&get<u32>(p,generations+n*4)==mask&&get<u32>(p,generations+n*4+4)==shift,"TM2 live/mask/shift");
    for(unsigned i=0;i<words;++i)check(get<u32>(p,constructed+i*4)==0&&get<u32>(p,active+i*4)==0,"TM4 both constructor bitmap clears cover every dword");
    for(unsigned i=0;i<n;++i)check(get<u32>(p,ring+i*4)==i&&get<u32>(p,generations+i*4)==((1u<<shift)|i),"TM2 ring and generation initialization");
}
// Observe the production constructor counter-reset hook in its native ESI/EBX
// context. Stop immediately at its continuation; no engine constructors run.
u32 reset_target=0x56b6b9;
__declspec(naked) void __fastcall reset_owner(void*,void*){
    __asm {
        push esi
        push ebx
        mov esi,ecx
        xor ebx,ebx
        call reset_target
        pop ebx
        pop esi
        ret
    }
}
void make_owner(void* p){std::memset(p,0x69,0x2e0);put(p,8,&file);put<u32>(p,0x2d0,0x12345678);put<u32>(p,0x2d4,0x87654321);put<int>(p,0x2d8,-1);put<u32>(p,0x2dc,0x32456789);reset_owner(p,nullptr);}
void owner_call(void* p,int i){u32 before=0,after=0,sp_before=0,sp_after=0;
    __asm {
        mov eax,fs:[0]
        mov before,eax
        mov sp_before,esp
    }
    append(p,nullptr,&file_ptr,i);
    __asm {
        mov eax,fs:[0]
        mov after,eax
        mov sp_after,esp
    }
    check(before==after&&sp_before==sp_after,"TM5 owner append/refusal preserves FS and stack");}
unsigned owner_logs(){return static_cast<unsigned>(std::count_if(messages.begin(),messages.end(),[](const auto& x){return x.find("R3: animation list too long")!=std::string::npos;}));}
}
namespace ini {std::optional<std::string> text(const wchar_t* s,const wchar_t* k){return std::wcscmp(s,L"Limits")==0&&std::wcscmp(k,L"MotionPool")==0?option:std::nullopt;}}
namespace logger {void write(const char* format,...){char b[1024]{};va_list a;va_start(a,format);std::vsnprintf(b,sizeof b,format,a);va_end(a);messages.emplace_back(b);}}
namespace limits {unsigned actor_capacity(){return owner_capacity;}}
namespace game_version {bool require_gameplay_agreement(HMODULE){required=true;return true;}void refuse_startup(){unsafe=true;}}
namespace gameplay_agreement {bool register_rule(std::uint16_t id,std::uint16_t rev,std::array<u32,4> value,bool verified){return agreement.add(id,rev,value,verified);}}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);check(argc==2||argc==3,"retail fixture argument");
    std::ifstream f(argv[1],std::ios::binary);retail.assign(std::istreambuf_iterator<char>(f),{});check(!retail.empty(),"retail read");
    DWORD old=0;check(VirtualProtect(ptr(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old)!=0,"owned image writable");load();
    const std::vector<unsigned char> before(static_cast<unsigned char*>(ptr(0x401000)),static_cast<unsigned char*>(ptr(0x801000)));
    const auto& stock_layout=motion_pool::layout();
    const motion_pool::descriptor stock_expected{500,0x3e80,0x3ec0,0x4694,0x4698,0x469c,0x46a0,0x46e0,0x46e4,0x4eb4,0x4eb8,0x4ebc};
    check(std::memcmp(&stock_layout,&stock_expected,sizeof stock_expected)==0,"TM2 every stock descriptor field");
    gameplay_capabilities::record stock;check(stock.finalize(),"stock digest");
    for(auto key:{std::optional<std::string>{},std::optional<std::string>{"500"}}){option=key;patch_transaction::transaction tx;motion_pool::require_agreement(GetModuleHandleW(nullptr));motion_pool::contribute(tx,GetModuleHandleW(nullptr));check(!tx.contributions(),"TM3 OFF contributes nothing");}
    check(!required&&agreement.finalize()&&std::equal(stock.bytes().begin(),stock.bytes().end(),agreement.bytes().begin()),"TM3 OFF digest and exclusion unchanged");
    check(std::memcmp(before.data(),ptr(0x401000),before.size())==0,"TM3 OFF image unchanged");
    for(const auto key:{"0","22529","512",""}){option=key;agreement={};patch_transaction::transaction tx;motion_pool::contribute(tx,GetModuleHandleW(nullptr));check(!tx.contributions()&&!agreement.finalize(),"TM7 unsupported key unverified");}
    option="22528";owner_capacity=129;agreement={};{patch_transaction::transaction tx;motion_pool::contribute(tx,GetModuleHandleW(nullptr));check(!tx.contributions()&&!agreement.finalize(),"TM7 owner bound rejects 129");}owner_capacity=127;
    put<u32>(ptr(0x7b05bc),0,1234);agreement={};{patch_transaction::transaction tx;motion_pool::contribute(tx,GetModuleHandleW(nullptr));check(!tx.contributions()&&!agreement.finalize(),"TM7 existing pool refused");}put<u32>(ptr(0x7b05bc),0,0);
    for(const auto& s:motion_pool_sites::sites)for(unsigned i=0;i<s.hex.size()/2;++i){auto* p=static_cast<unsigned char*>(ptr(s.va+i));*p^=1;agreement={};patch_transaction::transaction tx;motion_pool::contribute(tx,GetModuleHandleW(nullptr));
        check(tx.commit()==patch_transaction::outcome::refused_guard&&!agreement.finalize(),"TM7 every retail byte guarded");*p^=1;check(std::memcmp(before.data(),ptr(0x401000),before.size())==0,"guard refusal writes zero bytes");}
    agreement={};{patch_transaction::transaction tx;motion_pool::contribute(tx,GetModuleHandleW(nullptr));patch_transaction::contribution c;c.owner="conflict";c.guards.push_back({0x56a125,patch_transaction::bytes_of("b910000000"),"conflict"});c.writes.push_back({0x56a125,{0x90,0x90,0x90,0x90,0x90},"conflict"});tx.add(std::move(c));check(tx.commit()==patch_transaction::outcome::refused_conflict&&!agreement.finalize(),"TM7 overlapping write refused");}
    agreement={};{patch_transaction::transaction tx;motion_pool::contribute(tx,GetModuleHandleW(nullptr));patch_transaction::hooks h;
        h.after_apply=[]{*static_cast<unsigned char*>(ptr(0x56a126))^=1;};
        if(argc==3)h.after_rollback=[]{*static_cast<unsigned char*>(ptr(0x56a126))^=1;};
        const auto result=tx.commit(&h);
        if(argc==3){check(result==patch_transaction::outcome::rollback_failed&&unsafe&&!agreement.finalize(),"uncertified rollback closes startup");std::puts("PASS MotionPool uncertified rollback retains DLL caves and refuses startup");return 0;}
        check(result==patch_transaction::outcome::rolled_back&&!agreement.finalize(),"TM7 certified whole-row rollback");
        check(std::memcmp(before.data(),ptr(0x401000),before.size())==0,"TM7 rollback restores every image byte");}
    // TM2 executes the unchanged retail constructor before any R3 writes.
    services();std::vector<unsigned char> small(0x4ebc+64,0xa5);construct(small.data()+32,nullptr);layout_check(small.data()+32,500);load();
    agreement={};unsigned short cw=0,cw_after=0;__asm fnstcw cw
    const auto mxcsr=_mm_getcsr();{patch_transaction::transaction tx;motion_pool::contribute(tx,GetModuleHandleW(nullptr));check(tx.commit()==patch_transaction::outcome::committed,"TM4 R3 production transaction commits");}
    check(motion_pool::layout().capacity==22528&&motion_pool::layout().size==0xdd61c&&motion_pool::layout().live==0xc7610,"published descriptor");
    const auto& emitted=motion_pool::layout();
    const motion_pool::descriptor raised_expected{22528,0xb0000,0xb0b00,0xc6b04,0xc6b08,0xc6b0c,0xc6b10,0xc7610,0xc7614,0xdd614,0xdd618,0xdd61c};
    check(std::memcmp(&emitted,&raised_expected,sizeof raised_expected)==0,"TM2 every emitted descriptor field");
    const auto counter=patch_transaction::bytes_of(motion_pool::counter_guard_hex());
    check(counter.size()==10&&std::memcmp(counter.data(),ptr(0x56a3e3),10)==0,"status consumer guard uses both installed operands");
    check(agreement.finalize()&&agreement.rule(1).id==51&&agreement.rule(1).revision==1&&agreement.rule(1).values==std::array<u32,4>{22528,0,0,0},"TM8 verified rule 51");
    for(bool direction:{false,true}){const auto& a=direction?agreement:stock;const auto& b=direction?stock:agreement;gameplay_capabilities::difference d;char why[256]{};
        check(a.compare(b.bytes(),&d)!=gameplay_capabilities::result::match,"TM8 mismatch refused both ways");a.describe(d.buckets,d.local_only,true,why,sizeof why);check(std::strstr(why,"51 MotionPool")!=nullptr,"TM8 mismatch named");}
    services();put<unsigned char>(ptr(0x56b6bf),0,0xc3);
    void* p=create();check(requested_sizes.back()==0xdd61c,"TM4 getter allocation uses new size");layout_check(p,22528);
    std::vector<u32> handles;for(unsigned i=0;i<22528;++i){auto h=claim(p,nullptr);handles.push_back(h);check(h!=0&&lookup(p,nullptr,h)==static_cast<unsigned char*>(p)+i*32,"TM4 native allocation and masked lookup");}
    check(claim(p,nullptr)==0,"TM4 native overflow returns zero");check(lookup(p,nullptr,0)==ptr(0x7b05c0),"TM4 zero fallback retained");
    std::array<unsigned char,0x2e0> one{};put(one.data(),0x2cc,1);put(one.data(),12,handles[37]);destroy(one.data(),nullptr);
    check(lookup(p,nullptr,handles[37])==static_cast<unsigned char*>(p)+37*32,"TM4 released handle still masks (no stale rejection)");
    auto recycled=claim(p,nullptr);check((recycled&0x7fff)==37&&recycled!=handles[37],"TM4 native FIFO recycle and generation");
    check(lookup(p,nullptr,handles[37])==lookup(p,nullptr,recycled),"TM4 stale handle masks to recycled record");
    reinterpret_cast<void(__fastcall*)(void*,void*)>(0x56a0b0)(p,nullptr);
    for(unsigned i=0;i<704;++i)check(get<u32>(p,0xb0000+i*4)==0,"TM4 destructor clears all 704 constructed words");close_pool();
    // TM5: real owner routine, synthetic 400-entry clip list, exact stack/SEH.
    std::array<std::array<unsigned char,0x2e0>,127> owners{};unsigned total=0,refused=0;
    for(unsigned k=0;k<6;++k){const unsigned n=std::array<unsigned,6>{0,1,175,176,177,400}[k];auto* o=owners[k].data();make_owner(o);const auto logs=owner_logs();
        for(unsigned i=0;i<n;++i)owner_call(o,i);
        total+=(std::min)(n,176u);refused+=n>176?n-176:0;
        check(get<u32>(o,0x2cc)==(std::min)(n,176u),"TM5 list append count capped at 176");
        check(get<u32>(o,0x2d0)==0x12345678&&get<u32>(o,0x2d4)==0x87654321&&get<int>(o,0x2d8)==-1&&get<u32>(o,0x2dc)==0x32456789,"TM5 owner tail canaries intact");
        check(owner_logs()-logs==(n>176?1u:0u),"TM5 one refusal log per owner");
        check(!pool()?total==0:get<u32>(pool(),0xc7610)==total,"TM5 pool live equals owner counts");
        for(unsigned i=0;i<(std::min)(n,176u);++i)owner_call(o,i);
        check(!pool()?total==0:get<u32>(pool(),0xc7610)==total,"TM5 second pass uses existing path without allocation");}
    // TM6: every reader matches the native record in range, falls back beyond count.
    auto* o=owners[3].data();auto* record=lookup(pool(),nullptr,get<u32>(o,12));
    put<float>(record,0,1.25f);put<float>(record,4,2.5f);put<float>(record,8,3.75f);put<float>(record,12,4.5f);put<float>(record,16,5.25f);put<float>(record,20,6.5f);put<float>(record,24,7.75f);put<unsigned char>(record,28,1);
    std::vector<int> indices;for(int i=0;i<176;++i)indices.push_back(i);for(int i:{176,177,400,-1})indices.push_back(i);
    for(int index:indices){const bool valid=index>=0&&index<176;const auto h=valid?get<u32>(o,12+index*4):0;const auto* expected=lookup(pool(),nullptr,h);
        check(reinterpret_cast<unsigned char(__fastcall*)(void*,void*,int)>(0x56a9e0)(o,nullptr,index)==get<unsigned char>(expected,28),"TM6 flag reader");
        check(reinterpret_cast<float(__fastcall*)(void*,void*,int)>(0x56aa30)(o,nullptr,index)==get<float>(expected,12),"TM6 magnitude reader");
        for(u32 entry:{0x56aa00u,0x56aa50u}){std::array<float,3> v{};reinterpret_cast<void*(__fastcall*)(void*,void*,void*,int)>(entry)(o,nullptr,v.data(),index);check(std::memcmp(v.data(),static_cast<const unsigned char*>(expected)+(entry==0x56aa00?0:16),12)==0,"TM6 vector reader");}}
    // Prove count independently of the fixed 176 capacity: unused entries
    // contain a valid, distinguishable handle instead of relying on garbage.
    const auto canary_handle=get<u32>(owners[3].data(),12);
    for(unsigned k:{0u,1u,2u}){auto* short_owner=owners[k].data();const auto count=get<u32>(short_owner,0x2cc);
        std::vector<unsigned> invalid{count};if(count!=175)invalid.push_back(175);
        for(auto index:invalid){put<u32>(short_owner,12+index*4,canary_handle);
            check(reinterpret_cast<unsigned char(__fastcall*)(void*,void*,int)>(0x56a9e0)(short_owner,nullptr,index)==0,"TM6 short owner flag uses fallback at/beyond count");
            check(reinterpret_cast<float(__fastcall*)(void*,void*,int)>(0x56aa30)(short_owner,nullptr,index)==0,"TM6 short owner magnitude uses fallback");
            for(u32 entry:{0x56aa00u,0x56aa50u}){std::array<float,3> v{9,9,9};reinterpret_cast<void*(__fastcall*)(void*,void*,void*,int)>(entry)(short_owner,nullptr,v.data(),index);
                check(v==std::array<float,3>{0,0,0},"TM6 short owner vector uses fallback");}}
        if(count){const int index=static_cast<int>(count)-1;const auto* expected=lookup(pool(),nullptr,get<u32>(short_owner,12+index*4));
            check(reinterpret_cast<unsigned char(__fastcall*)(void*,void*,int)>(0x56a9e0)(short_owner,nullptr,index)==get<unsigned char>(expected,28),"TM6 short owner last valid index remains native");}
    }
    const auto telemetry=motion_pool::telemetry_json();check(telemetry.find("\"clip_refusals\":"+std::to_string(refused))!=std::string::npos,"TM5 refusals counted exactly");
    check(telemetry.find("\"reader_refusals\":36")!=std::string::npos,"TM6 every rejected reader is counted");
    for(unsigned k=0;k<6;++k){put<u32>(owners[k].data(),8,0);destroy(owners[k].data(),nullptr);}check(get<u32>(pool(),0xc7610)==0,"owner teardown releases every record");close_pool();
    // Same address, new owner lifetime: constructor hook resets once-only logging.
    make_owner(owners[4].data());const auto old_logs=owner_logs();for(unsigned i=0;i<177;++i)owner_call(owners[4].data(),i);check(owner_logs()==old_logs+1,"new owner lifetime logs again");put<u32>(owners[4].data(),8,0);destroy(owners[4].data(),nullptr);close_pool();
    // Exercise index and append-capacity checks independently. A large index
    // cannot append even to an empty owner; a negative count cannot be indexed.
    make_owner(one.data());owner_call(one.data(),176);check(get<u32>(one.data(),0x2cc)==0&&!pool(),"TM5 bad index refuses before allocating on empty owner");
    put<u32>(one.data(),0x2cc,0xffffffff);owner_call(one.data(),0);check(get<u32>(one.data(),0x2cc)==0xffffffff&&!pool(),"TM5 invalid append count refused independently");
    // A valid-index gap keeps the explicitly specified retail append-at-count.
    make_owner(one.data());std::memset(one.data()+12,0,176*4);owner_call(one.data(),5);
    check(get<u32>(one.data(),0x2cc)==1&&get<u32>(one.data(),12)!=0&&get<u32>(one.data(),12+5*4)==0,"TM5 valid gap retains retail append-at-count");
    put<u32>(one.data(),8,0);destroy(one.data(),nullptr);close_pool();
    // TM4 third lazy allocation in destruction, with a harmless zero handle.
    const auto destructor_allocations=requested_sizes.size();
    std::memset(one.data(),0,one.size());put<u32>(one.data(),0x2cc,1);destroy(one.data(),nullptr);check(requested_sizes.size()==destructor_allocations+1&&requested_sizes.back()==0xdd61c,"TM4 destruction allocation uses new size");close_pool();
    // TM4 second lazy creation inside the existing-record path of 0x56a620.
    const auto existing_allocations=requested_sizes.size();
    make_owner(one.data());put<u32>(one.data(),0x2cc,1);put<u32>(one.data(),12,0);owner_call(one.data(),0);check(requested_sizes.size()==existing_allocations+1&&requested_sizes.back()==0xdd61c,"TM4 owner existing-path allocation uses new size");close_pool();
    for(unsigned k=0;k<127;++k){make_owner(owners[k].data());for(unsigned i=0;i<176;++i)owner_call(owners[k].data(),i);}
    check(get<u32>(pool(),0xc7610)==22352,"127 owners x176 fits raised pool");
    check(motion_pool::telemetry_json().find("\"allocation_refusals\":0")!=std::string::npos,"owner envelope never refuses pool allocation");
    const auto crowd_logs=owner_logs();for(auto& own:owners)owner_call(own.data(),176);
    check(owner_logs()==crowd_logs+127&&get<u32>(pool(),0xc7610)==22352,"all 127 live owners count/log refusal without allocating");
    for(auto& own:owners){put<u32>(own.data(),8,0);destroy(own.data(),nullptr);}check(get<u32>(pool(),0xc7610)==0,"crowd teardown returns live to zero");close_pool();canaries();
    __asm fnstcw cw_after
    check(cw==cw_after&&mxcsr==_mm_getcsr()&&!unsafe,"floating control state and certified startup");
    std::printf("PASS MotionPool TM2-TM8: %u checks\n%s\n",checks,motion_pool::telemetry_json().c_str());return 0;
}
