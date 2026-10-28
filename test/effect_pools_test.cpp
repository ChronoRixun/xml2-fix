// Execute retail owner routines in a reserved image. No entry point, renderer,
// game process or engine DLL runs. Vector arithmetic and primitive record services
// use invented inputs; pool constructors, allocation, release and teardown are native.
#include <Windows.h>
#include "effect_pools.hpp"
#include "effect_vector_sites.hpp"
#include "primitive_def_sites.hpp"
#include "gameplay_capabilities.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace {
using namespace effect_pools_rules;
unsigned checks=0,mutations=0;
bool unsafe=false,required=false;
gameplay_capabilities::record agreement;
std::optional<std::string> vector_key,primitive_key;
std::vector<unsigned char> retail;
void check(bool ok,const char* what){++checks;if(!ok){std::printf("FAIL %s\n",what);std::fflush(stdout);std::abort();}}
void* ptr(u32 a){return reinterpret_cast<void*>(a);}
u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
template<class T>T get(const void* p,u32 o=0){T v;std::memcpy(&v,static_cast<const unsigned char*>(p)+o,sizeof v);return v;}
template<class T>void put(void* p,u32 o,T v){std::memcpy(static_cast<unsigned char*>(p)+o,&v,sizeof v);}
template<class T>void jump(u32 a,T f){put<unsigned char>(ptr(a),0,0xe9);put<u32>(ptr(a),1,address(reinterpret_cast<const void*>(f))-a-5);}
void load(){
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(retail.data()+get<u32>(retail.data(),0x3c));
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i){const auto& s=IMAGE_FIRST_SECTION(nt)[i];
        if(s.VirtualAddress>=0x1000&&s.VirtualAddress<0x401000){
            const auto size=std::min<u32>(s.Misc.VirtualSize,0x401000-s.VirtualAddress);
            std::memset(ptr(0x400000+s.VirtualAddress),0,size);
            std::memcpy(ptr(0x400000+s.VirtualAddress),retail.data()+s.PointerToRawData,std::min<u32>(size,s.SizeOfRawData));
        }
    }
}
struct block {
    std::vector<unsigned char> bytes;u32 size;
    explicit block(u32 n):bytes(n+64,0xa5),size(n){std::memset(data(),0,n);}
    unsigned char* data(){return bytes.data()+32;}
    u32 word(u32 o){return get<u32>(data(),o);}
    bool intact(){return std::all_of(bytes.begin(),bytes.begin()+32,[](auto c){return c==0xa5;})&&
        std::all_of(bytes.end()-32,bytes.end(),[](auto c){return c==0xa5;});}
};
using unary=void*(__fastcall*)(void*,void*);
using free_t=void(__fastcall*)(void*,void*,u32);
using find_t=u32(__fastcall*)(void*,void*,u32,bool);
using vector_alloc_t=u32(__fastcall*)(void*,void*,float,float,float,float,float,float);
auto vector_allocate=reinterpret_cast<vector_alloc_t>(0x416210);
void* __fastcall vector_init(void* p,void*,float x,float y,float z){auto* v=static_cast<float*>(p);v[0]=x;v[1]=y;v[2]=z;return p;}
void* __fastcall vector_copy(void* p,void*,const void* in){std::memcpy(p,in,12);return p;}
float __fastcall length_squared(void* p,void*){const auto* v=static_cast<float*>(p);return v[0]*v[0]+v[1]*v[1]+v[2]*v[2];}
std::set<u32> live_records;
unsigned record_constructions=0,record_destructions=0;
block* heap_block=nullptr;
unsigned heap_frees=0;
void* __cdecl heap_allocate(u32 size,int heap,int tag){check(heap_block&&size==heap_block->size&&heap==14&&tag==71,"native lazy allocation uses raised size and retail heap/tag");return heap_block->data();}
void __cdecl heap_release(void* p,int heap){check(heap_block&&p==heap_block->data()&&heap==14,"native singleton teardown uses retail heap");++heap_frees;}
void* __fastcall primitive_construct(void* p,void*){check(live_records.insert(address(p)).second,"native allocator never constructs a live record twice");++record_constructions;std::memset(p,0x69,0x118);return p;}
void __fastcall primitive_destroy(void* p,void*){check(live_records.erase(address(p))==1,"native teardown visits a constructed record exactly once");++record_destructions;}
void services(){
    put<u32>(ptr(0x67fa1c),0,address(reinterpret_cast<void*>(&vector_init)));
    put<u32>(ptr(0x67fa2c),0,address(reinterpret_cast<void*>(&vector_copy)));
    put<u32>(ptr(0x67fa58),0,address(reinterpret_cast<void*>(&length_squared)));
    jump(0x416a30,&primitive_construct);jump(0x417f80,&primitive_destroy);
}
std::span<const site> sites(pool p){return p==pool::vectors?std::span<const site>(effect_vector_sites::sites):std::span<const site>(primitive_def_sites::sites);}
std::span<const guard> guards(pool p){return p==pool::vectors?std::span<const guard>(effect_vector_sites::guards):std::span<const guard>(primitive_def_sites::guards);}
void apply(pool p,u32 n,u32 object){
    if(n==stock_capacity(p))return;
    patch_transaction::contribution c;std::string why;
    const bool ready=effect_pools::prepare(p,n,object,c,why);
    if(!ready)std::printf("  prepare: %s\n",why.c_str());
    check(ready,"production preparation accepts complete retail ledger");
    patch_transaction::transaction tx;check(tx.add(std::move(c)),"one contribution per row");
    check(tx.commit()==patch_transaction::outcome::committed,"one protected transaction commits all operands");
}
void find_checks(pool p,const descriptor& d){
    const auto find=reinterpret_cast<find_t>(p==pool::vectors?0x4159b0:0x4183a0);
    block bits(d.bitmap_words*4);
    put<u32>(bits.data(),(d.capacity-1)/32*4,1u<<((d.capacity-1)%32));
    check(find(bits.data(),nullptr,0,true)==d.capacity-1,"private findNext sees the final slot");
    check(find(bits.data(),nullptr,d.capacity,true)==d.capacity,"private findNext returns N for out of range");
    check(find(bits.data(),nullptr,d.capacity-1,false)==d.capacity,"private findNext unset-bit tail respects N");
    std::memset(bits.data(),0,d.bitmap_words*4);
    check(find(bits.data(),nullptr,0,true)==d.capacity&&find(bits.data(),nullptr,0,false)==0,"empty findNext sentinel and unset scan");
    check(bits.intact(),"findNext bitmap canaries intact");
}
void vectors(u32 n){
    std::printf("  vectors %u\n",n);load();const auto d=describe(pool::vectors,n);block b(d.size);
    apply(pool::vectors,n,address(b.data()));services();
    check(reinterpret_cast<unary>(0x416400)(b.data(),nullptr)==b.data(),"native vector constructor returns object");
    check(b.word(d.live)==84&&b.word(d.ring_count)==n-84&&b.word(d.ring_read)==84&&b.word(d.ring_write)==0&&b.word(d.preset_count)==1,"constructor fields match model");
    for(u32 i=0;i<n;++i)check(b.word(d.ring+i*4)==i,"constructor ring contains every id");
    for(u32 i=0;i<84;++i){check(b.word(d.presets+i*4)==i,"84 preset IDs retained");for(u32 j=0;j<24;j+=4)check(b.word(i*24+j)==0,"preset vectors remain zero");}
    for(u32 w=0;w<d.bitmap_words;++w){const u32 expected=w<2?0xffffffffu:w==2?0xfffffu:0;
        check(b.word(d.bitmap_a+w*4)==expected&&b.word(d.bitmap_b+w*4)==expected,"constructor clears both bitmap tails");}
    check(vector_allocate(b.data(),nullptr,0,0,0,0,0,0)==0&&b.word(d.live)==84,"equal preset shares id zero");
    for(u32 i=84;i<n;++i)check(vector_allocate(b.data(),nullptr,1000.0f+i,2,3,4,5,6)==i,"native allocator gives each id exactly once");
    check(vector_allocate(b.data(),nullptr,1,2,3,4,5,6)==0&&b.word(d.live)==n,"native vector exhaustion returns zero only at N");
    check(b.word(d.ring_count)==0&&b.intact(),"full vector pool canaries intact");
    const auto release=reinterpret_cast<free_t>(0x415fb0);
    release(b.data(),nullptr,n-1);release(b.data(),nullptr,500);
    check(b.word(d.live)==n-2&&b.word(d.ring_count)==2,"native free updates live and ring counts");
    check(vector_allocate(b.data(),nullptr,7,2,3,4,5,6)==n-1&&vector_allocate(b.data(),nullptr,8,2,3,4,5,6)==500,"first-freed first-reused across ring wrap");
    find_checks(pool::vectors,d);
    // Reader takes a dword ID pointer and copies the record. Its active-bit test
    // must use the raised descriptor, including slots past the retail capacity.
    std::array<float,6> read{};u32 id=n-1;
    // Aim getter directly at this constructed canaried pool for the reader only.
    const u32 old_guard=get<u32>(ptr(0x70b648));put<u32>(ptr(0x70b648),0,1);
    for(const auto& s:effect_vector_sites::sites)if(s.member==field::object)put<u32>(ptr(s.va),s.offset,address(b.data()));
    reinterpret_cast<void*(__cdecl*)(void*,u32*)>(0x4060d0)(read.data(),&id);
    check(read[0]==7&&read[5]==6,"native vector reader accesses raised last slot");
    put<u32>(ptr(0x70b648),0,old_guard);
    reinterpret_cast<unary>(0x416530)(b.data(),nullptr);
    for(u32 w=0;w<d.bitmap_words;++w)check(b.word(d.bitmap_a+w*4)==0,"native vector destructor clears all constructed bits");
    check(b.word(d.live)==n-84&&b.intact(),"native vector destructor preserves canaries and frees only presets");
}
void primitives(u32 n){
    std::printf("  primitive definitions %u\n",n);load();const auto d=describe(pool::primitive_defs,n);block b(d.size);
    apply(pool::primitive_defs,n,0);services();live_records.clear();record_constructions=record_destructions=0;
    check(reinterpret_cast<unary>(0x418720)(b.data(),nullptr)==b.data(),"native primitive pool constructor returns object");
    u32 mask=0,shift=0;for(u32 v=n-1;v;v>>=1){++shift;mask=mask*2+1;}
    check(b.word(d.live)==0&&b.word(d.ring_count)==n&&b.word(d.ring_read)==0&&b.word(d.ring_write)==0,"primitive ring/live field model");
    check(b.word(d.mask)==mask&&b.word(d.shift)==shift,"native mask seed N-1 computes correct split");
    for(u32 i=0;i<n;++i)check(b.word(d.ring+i*4)==i&&b.word(d.ids+i*4)==((1u<<shift)|i),"constructor ring and generation fields");
    for(u32 w=0;w<d.bitmap_words;++w)check(b.word(d.bitmap_a+w*4)==0&&b.word(d.bitmap_b+w*4)==0,"primitive constructor clears both bitmap tails");
    // Admission is through the native effect append routine. Reset the invented
    // effect's local count between calls so the independent ten-primitive rule
    // does not constrain this global pool test.
    block effect(0x44);put<u32>(ptr(0x70b64c),0,address(b.data()));
    auto append=reinterpret_cast<unary>(0x4188a0);
    for(u32 i=0;i<n;++i){put<u32>(effect.data(),0x30,0);auto* actual=append(effect.data(),nullptr);
        if(actual!=b.data()+i*0x118)std::printf("  slot %u got %p expected %p live %u full operand %x\n",i,actual,b.data()+i*0x118,b.word(d.live),get<u32>(ptr(0x4188ae)));
        check(actual==b.data()+i*0x118,"native admission constructs each primitive slot");}
    put<u32>(effect.data(),0x30,0);
    const auto full=b.bytes;
    check(append(effect.data(),nullptr)==nullptr&&full==b.bytes,"native admission refuses N+1 without touching any pool byte");
    check(b.word(d.live)==n&&b.word(d.ring_count)==0&&b.intact()&&effect.intact(),"full primitive owner and effect canaries");
    // Native pointer release masks its generation ID, calls the native free, and
    // advances the generation. This is the extra 0x4187d0 consumer census found.
    const auto release=reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(0x4187d0);
    const auto old_generation=b.word(d.ids+(n-1)*4);
    release(b.data(),nullptr,b.data()+(n-1)*0x118);release(b.data(),nullptr,b.data()+31*0x118);
    check(b.word(d.ids+(n-1)*4)==old_generation+(1u<<shift),"pointer release advances generation with raised shift");
    check(b.word(d.live)==n-2&&b.word(d.ring_count)==2,"native primitive frees update counts");
    const auto allocate=reinterpret_cast<unary>(0x418660);
    check(allocate(b.data(),nullptr)==b.data()+(n-1)*0x118&&allocate(b.data(),nullptr)==b.data()+31*0x118,"native primitive allocator recycles in FIFO order");
    find_checks(pool::primitive_defs,d);
    reinterpret_cast<unary>(0x418850)(b.data(),nullptr);
    for(u32 w=0;w<d.bitmap_words;++w)check(b.word(d.bitmap_a+w*4)==0,"native primitive destructor clears every constructed bit");
    check(live_records.empty()&&record_constructions==n+2&&record_destructions==n+2,"native destructor visits every live record once");
    check(b.intact(),"primitive destructor preserves owner canaries");
}
void mutation_tests(pool p){
    load();const u32 n=p==pool::vectors?4096:2048;block backing(describe(p,n).size);
    std::map<u32,unsigned char> bytes;
    for(const auto& s:sites(p))for(u32 j=0;j<s.hex.size()/2;++j)bytes[s.va+j]=get<unsigned char>(ptr(s.va+j));
    for(const auto& s:guards(p))for(u32 j=0;j<s.hex.size()/2;++j)bytes[s.va+j]=get<unsigned char>(ptr(s.va+j));
    std::string why;patch_transaction::contribution c;
    check(effect_pools::prepare(p,n,address(backing.data()),c,why),"mutation positive control prepares");
    for(const auto& [va,b]:bytes){
        put<unsigned char>(ptr(va),0,b^1);why.clear();c={};
        check(!effect_pools::prepare(p,n,address(backing.data()),c,why),"every byte mutation refuses before any write");
        check(get<unsigned char>(ptr(va))==(b^1),"refusal preserves mutated byte");put<unsigned char>(ptr(va),0,b);++mutations;
    }
    for(const u32 bad:{stock_capacity(p),maximum(p)+1,0u})check(!effect_pools::prepare(p,bad,address(backing.data()),c,why),"out of range install refused");
    put<u32>(ptr(init_guard(p)),0,1);check(!effect_pools::prepare(p,n,address(backing.data()),c,why),"already built install refused");put<u32>(ptr(init_guard(p)),0,0);
    if(p==pool::vectors){put<u32>(ptr(init_guard(p)),0,2);check(effect_pools::prepare(p,n,address(backing.data()),c,why),"unrelated init guard bits retained");put<u32>(ptr(init_guard(p)),0,0);}
    check(effect_pools::prepare(p,n,address(backing.data()),c,why),"retail guards restored");
    std::map<u32,unsigned char> snapshot;for(const auto& w:c.writes)for(u32 i=0;i<w.bytes.size();++i)snapshot[w.va+i]=get<unsigned char>(ptr(w.va+i));
    patch_transaction::hooks hooks;unsigned calls=0;
    hooks.protect=[&](void* a,std::size_t s,DWORD protection,DWORD* old){if(++calls==(p==pool::vectors?2u:1u))return false;return VirtualProtect(a,s,protection,old)!=0;};
    patch_transaction::transaction tx;tx.add(c);
    const auto outcome=tx.commit(&hooks);
    if(outcome!=patch_transaction::outcome::refused_protect)std::printf("  transaction: %s %s (protect calls %u)\n",patch_transaction::name(outcome),tx.report().c_str(),calls);
    check(outcome==patch_transaction::outcome::refused_protect,"page acquisition failure refuses whole row");
    for(const auto& [a,b]:snapshot)check(get<unsigned char>(ptr(a))==b,"no write before all pages acquired");
    patch_transaction::hooks rollback;rollback.after_apply=[&]{const auto& w=c.writes.front();put<unsigned char>(ptr(w.va),0,get<unsigned char>(ptr(w.va))^1);};
    patch_transaction::transaction t2;t2.add(c);check(t2.commit(&rollback)==patch_transaction::outcome::rolled_back,"readback failure rolls back complete row");
    for(const auto& [a,b]:snapshot)check(get<unsigned char>(ptr(a))==b,"rollback byte identity");
    patch_transaction::contribution conflict;conflict.owner="foreign guard";const auto& w=c.writes.front();auto bad=w.bytes;bad[0]^=1;conflict.guards.push_back({w.va,bad,"overlap"});
    patch_transaction::transaction t3;t3.add(c);t3.add(conflict);check(t3.commit()==patch_transaction::outcome::refused_conflict,"foreign conflicting guard refuses before write");
}
void rules_tests(){
    for(const auto p:{pool::vectors,pool::primitive_defs}){
        check(capacity(p,std::nullopt)==stock_capacity(p),"absent key stock");
        for(const auto& s:{std::string(""),std::string("-1"),std::string("1junk"),std::string(40,'9')})check(capacity(p,s)==stock_capacity(p),"invalid key stock");
        check(capacity(p,std::to_string(stock_capacity(p)))==stock_capacity(p),"explicit stock key stock");
        for(const auto n:{stock_capacity(p)+1,maximum(p)})check(capacity(p,std::to_string(n))==n,"capacity boundary accepted");
        const auto stock=describe(p,stock_capacity(p));
        for(const auto& s:sites(p))check(value(s.member,stock,owner(p))==s.retail,"stock descriptor reproduces every generated operand");
        for(u32 n=stock_capacity(p);n<=maximum(p);++n){const auto d=describe(p,n);
            check(d.bitmap_words==(n+31)/32&&d.bitmap_a==n*record_size(p)&&d.ring==d.bitmap_a+d.bitmap_words*4&&d.ring_write==d.ring+(n+1)*4&&d.live==d.bitmap_b+d.bitmap_words*4,"all layouts preserve every native field boundary");}
    }
    check(describe(pool::vectors,710).size==0x4fc8&&describe(pool::primitive_defs,816).size==0x396ec,"retail constructor allocation sizes");
    const auto& v=effect_vector_sites::constructor_model;const auto vd=describe(pool::vectors,710);
    check(v==std::array<u32,13>{710,709,vd.bitmap_words,vd.bitmap_a,vd.ring,vd.ring_write,vd.ring_read,vd.ring_count,vd.bitmap_b,vd.live,vd.presets,vd.preset_count,vd.size},"vector constructor field-for-field model");
    const auto& r=primitive_def_sites::constructor_model;const auto pd=describe(pool::primitive_defs,816);
    check(r==std::array<u32,14>{816,815,pd.bitmap_words,pd.bitmap_a,pd.ring,pd.ring_write,pd.ring_read,pd.ring_count,pd.bitmap_b,pd.live,pd.ids,pd.mask,pd.shift,pd.size},"primitive constructor field-for-field model");
    gameplay_capabilities::record base,on;
    check(on.add(56,1,{2048,0,0,0},true)&&on.add(57,1,{4096,0,0,0},true)&&base.finalize()&&on.finalize(),"named installed rules build valid records");
    check(base.compare(on.bytes())!=gameplay_capabilities::result::match&&on.compare(base.bytes())!=gameplay_capabilities::result::match,"mismatch refused in both directions");
    check(std::strcmp(gameplay_capabilities::rule_name(56),"PrimitiveDefs")==0&&std::strcmp(gameplay_capabilities::rule_name(57),"EffectVectors")==0,"both mismatches named by INI key");
    for(bool reverse:{false,true}){
        const auto& local=reverse?on:base;const auto& peer=reverse?base:on;
        gameplay_capabilities::difference diff;local.compare(peer.bytes(),&diff);char message[512]{};
        local.describe(diff.buckets,diff.local_only,true,message,sizeof message);
        check(std::strstr(message,"PrimitiveDefs")&&std::strstr(message,"EffectVectors"),"enabled and disabled peers both name the mismatching keys");
    }
}
}
namespace ini {std::optional<std::string> text(const wchar_t*,const wchar_t* k){return std::wcscmp(k,L"EffectVectors")==0?vector_key:std::wcscmp(k,L"PrimitiveDefs")==0?primitive_key:std::nullopt;}}
namespace logger {void write(const char*,...){}}
namespace game_version {bool require_gameplay_agreement(HMODULE){required=true;return true;}void refuse_startup(){unsafe=true;}}
namespace gameplay_agreement {bool register_rule(std::uint16_t id,std::uint16_t rev,std::array<u32,4> values,bool verified){return agreement.add(id,rev,values,verified);}}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    const char* exe=argc>1?argv[1]:std::getenv("XML2_FIX_TEST_EXE");check(exe!=nullptr,"owned retail fixture argument");
    std::ifstream file(exe,std::ios::binary);retail.assign(std::istreambuf_iterator<char>(file),{});check(retail.size()>0x280000,"retail fixture read");
    DWORD old=0;check(VirtualProtect(ptr(0x401000),0x400000,PAGE_EXECUTE_READWRITE,&old)!=0,"reserved test image writable");load();
    if(argc>2){
        const bool v=std::strcmp(argv[2],"refuse-vector")==0;const auto p=v?pool::vectors:pool::primitive_defs;
        vector_key="4096";primitive_key="2048";
        put<unsigned char>(ptr(sites(p).front().va),0,get<unsigned char>(ptr(sites(p).front().va))^1);
        effect_pools::install(reinterpret_cast<HMODULE>(0x400000));
        check(!effect_pools::installed(p)&&required&&!agreement.finalize(),"failed production install poisons agreement");
        check(effect_pools::layout(p).capacity==stock_capacity(p),"failed production install retains stock published descriptor");
        check(effect_pools::installed(v?pool::primitive_defs:pool::vectors),"a refused row does not prevent the independent row from installing");
    }else{
        rules_tests();
        effect_pools::require_agreement(reinterpret_cast<HMODULE>(0x400000));
        check(!required,"OFF does not enlist agreement before startup");
        vector_key="4096";effect_pools::require_agreement(reinterpret_cast<HMODULE>(0x400000));
        check(required,"standalone key enlists agreement before startup");
        vector_key.reset();required=false;
        const std::vector<unsigned char> before(static_cast<unsigned char*>(ptr(0x401000)),static_cast<unsigned char*>(ptr(0x801000)));
        effect_pools::install(reinterpret_cast<HMODULE>(0x400000));
        check(std::memcmp(before.data(),ptr(0x401000),before.size())==0&&!required&&agreement.count()==1,"OFF production install changes zero bytes and adds zero rules");
        vector_key="710";primitive_key="816";effect_pools::install(reinterpret_cast<HMODULE>(0x400000));
        check(std::memcmp(before.data(),ptr(0x401000),before.size())==0&&!required&&agreement.count()==1,"explicit stock install changes zero bytes and adds zero rules");
        vector_key.reset();primitive_key.reset();mutation_tests(pool::vectors);mutation_tests(pool::primitive_defs);
        for(u32 n:{710,711,1024,4096,16384})vectors(n);
        for(u32 n:{816,817,2048,8192})primitives(n);
        load();vector_key="4096";primitive_key="2048";
        effect_pools::install(reinterpret_cast<HMODULE>(0x400000));
        check(effect_pools::installed(pool::vectors)&&effect_pools::installed(pool::primitive_defs)&&required&&!unsafe,"both production installers publish after commit");
        check(agreement.count()==3&&agreement.finalize(),"exactly two installed-only verified rules");
        check(effect_pools::status().find("vectors 0/4096; prim defs -")!=std::string::npos,"status reads published lazy descriptors");
        // Zero-filled relocated vector block is built by the native lazy getter.
        services();jump(0x67211e,+[](void*)->int{return 0;});
        auto* object=reinterpret_cast<void*(__cdecl*)()>(0x4165a0)();
        check(address(object)==effect_pools::object_address(pool::vectors)&&get<u32>(object,effect_pools::layout(pool::vectors).live)==84,"native first getter builds relocated zero block");
        check(effect_pools::status().find("vectors 84/4096")!=std::string::npos,"status follows raised live offset");
        reinterpret_cast<unary>(0x416530)(object,nullptr);
        block lazy_primitive(effect_pools::layout(pool::primitive_defs).size);heap_block=&lazy_primitive;
        jump(0x5605e0,&heap_allocate);jump(0x5606a0,&heap_release);
        object=reinterpret_cast<void*(__cdecl*)()>(0x418860)();
        check(object==lazy_primitive.data()&&address(object)==effect_pools::object_address(pool::primitive_defs),"native primitive getter creates raised canaried allocation");
        check(effect_pools::status().find("prim defs 0/2048")!=std::string::npos,"primitive counter reads published heap descriptor");
        reinterpret_cast<unary>(0x418660)(object,nullptr);
        check(effect_pools::status().find("prim defs 1/2048")!=std::string::npos,"primitive counter reads native live count");
        reinterpret_cast<void(__cdecl*)()>(0x4189f0)();
        check(heap_frees==1&&get<u32>(ptr(0x70b64c))==0&&live_records.empty()&&lazy_primitive.intact(),"native singleton teardown clears records, frees heap and resets pointer");
    }
    std::printf("PASS effect pools: %u checks, %u unique guarded-byte mutations\n",checks,mutations);return 0;
}
