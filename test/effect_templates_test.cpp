// Own retail instructions executed in a reserved image. No game/engine entry runs.
#include "effect_templates.hpp"
#include "effect_templates_rules.hpp"
#include "primitives_per_effect.hpp"
#include "primitives_per_effect_rules.hpp"
#include "model_pools_rules.hpp"
#include "gameplay_capabilities.hpp"
#include <Windows.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <set>

extern "C" unsigned char union_space[];
using namespace effect_templates_rules;
using bytes=std::vector<std::uint8_t>;
static unsigned checks=0,failures=0;
static LONG WINAPI fault(EXCEPTION_POINTERS* e){
    if(e->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION){
        const auto* c=e->ContextRecord;
        std::printf("AV ip=%08lx eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx esp=%08lx address=%08lx\n",c->Eip,c->Eax,c->Ebx,c->Ecx,c->Edx,c->Esi,c->Edi,c->Esp,static_cast<unsigned long>(e->ExceptionRecord->ExceptionInformation[1]));
    }return EXCEPTION_CONTINUE_SEARCH;
}
#define CHECK(x) do{++checks;if(!(x)){++failures;std::printf("FAIL line %d: %s\n",__LINE__,#x);}}while(0)
static u32 address(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
static void* ptr(u32 p){return reinterpret_cast<void*>(p);}
static u32& word(void* p,unsigned off=0){return *reinterpret_cast<u32*>(static_cast<unsigned char*>(p)+off);}
static std::string mode="unit";
static gameplay_capabilities::record agreement;
static unsigned registrations=0;
static u32 model_target=0;
namespace ini {std::optional<std::string> text(const wchar_t*,const wchar_t* key){
    if(std::wstring_view(key)==L"PrimitivesPerEffect")return mode.starts_with("prim-")?std::optional<std::string>(mode=="prim-invalid"?"33":mode=="prim-stock"?"10":"32"):std::nullopt;
    if(mode.starts_with("prim-"))return mode=="prim-raised"?std::optional<std::string>("512"):std::nullopt;
    if(std::wstring_view(key)!=L"EffectTemplates"||mode=="off")return {};
    return mode=="invalid"?"219":mode=="stock"?"220":"512";
}}
namespace logger {void write(const char* f,...){va_list a;va_start(a,f);std::vprintf(f,a);std::puts("");va_end(a);}}
namespace gameplay_agreement {bool register_rule(std::uint16_t id,std::uint16_t rev,std::array<u32,4> values,bool verified){++registrations;return agreement.add(id,rev,values,verified);}}
namespace model_pools {u32 installed_target(u32 va){return va==0x571724?model_target:0;}}
namespace game_version {void refuse_startup(){CHECK(false);}}
namespace effect_templates_test {
void* allocate(std::size_t size){return mode=="allocation"||mode=="prim-allocation"?nullptr:VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);}
const patch_transaction::hooks* transaction_hooks(){
    if(mode!="protect"&&mode!="prim-protect")return nullptr;
    static patch_transaction::hooks h;
    h.protect=[](void* p,std::size_t n,DWORD protection,DWORD* old){return protection!=PAGE_EXECUTE_READWRITE&&VirtualProtect(p,n,protection,old);};return &h;
}
}
static bytes raw;
static bool map_image(){
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.ImageBase!=0x400000)return false;
    DWORD old=0;if(!VirtualProtect(union_space,0x700000,PAGE_EXECUTE_READWRITE,&old))return false;
    std::memset(union_space,0,0x700000);
    auto* s=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)std::memcpy(ptr(0x400000+s[i].VirtualAddress),raw.data()+s[i].PointerToRawData,s[i].SizeOfRawData);
    return FlushInstructionCache(GetCurrentProcess(),union_space,0x700000)!=FALSE;
}
static void redirect(u32 va,const void* target){auto w=patch_transaction::relocation(va,"e9",true,address(target),0,"test dependency");std::memcpy(ptr(va),w.bytes.data(),w.bytes.size());}
static void* heap_block=nullptr;
static unsigned heap_bytes=0;
static void* __cdecl heap(unsigned n,unsigned,unsigned){heap_bytes=n;heap_block=VirtualAlloc(nullptr,n+64,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);std::memset(static_cast<unsigned char*>(heap_block)+n,0xa5,64);return heap_block;}
static int __cdecl at_exit(void*){return 0;}
static void __cdecl nop(){}
static void __fastcall cookie(u32,void*){}
static const char* __cdecl normalize(const char* p,const char*,const char*){return p;}
static char* __cdecl copy(char* d,const char* s,unsigned n){return strncpy(d,s,n);}
static int __cdecl compare(const char* a,const char* b){return strcmp(a,b);}
static void __cdecl key(unsigned scope,const char* name,char* out){std::snprintf(out,256,"%u:%s",scope,name);}
static int __cdecl path(const char* name,char* out){strcpy(out,name);return 1;}
static unsigned __fastcall parse_effect(void* owner,void*,void** def,u32* handle,unsigned,const char*,const char*){
    *def=reinterpret_cast<void*(__fastcall*)(void*,void*,u32*)>(0x418d90)(owner,nullptr,handle);return *def!=nullptr;
}
static void dependencies(){
    redirect(0x5605e0,reinterpret_cast<void*>(&heap));redirect(0x401d50,reinterpret_cast<void*>(&at_exit));
    redirect(0x416560,reinterpret_cast<void*>(&nop));redirect(0x4165a0,reinterpret_cast<void*>(&nop));
    redirect(0x592520,reinterpret_cast<void*>(&normalize));redirect(0x672170,reinterpret_cast<void*>(&copy));
    redirect(0x672562,reinterpret_cast<void*>(&compare));redirect(0x5647f0,reinterpret_cast<void*>(&key));
    redirect(0x564860,reinterpret_cast<void*>(&path));redirect(0x672161,reinterpret_cast<void*>(&cookie));
    redirect(0x418e00,reinterpret_cast<void*>(&parse_effect));
}
static bool canary(){for(unsigned i=0;i<64;++i)if(static_cast<unsigned char*>(heap_block)[heap_bytes+i]!=0xa5)return false;return true;}
using method=void(__fastcall*)(void*,void*);
using load_method=u32(__fastcall*)(void*,void*,const char*,unsigned);
using find_next_method=int(__fastcall*)(void*,void*,int,unsigned char);
static void plan_checks(){
    const auto stock=layout_for(220);
    CHECK(stock.base==0x4874&&stock.size==0x8a18);
    CHECK(stock.files.ring==0x4154&&stock.files.bitmap==0x44d4&&stock.files.live==0x44f0);
    CHECK(stock.defs.bitmap_a==0x3a70&&stock.defs.bitmap_b==0x3e0c&&stock.defs.live==0x3e28&&stock.defs.mask==0x419c);
    CHECK(!parse({},220,2048).requested);CHECK(!parse(std::string("220"),220,2048).requested);
    for(auto s:{"0","219","2049","oops","-1"})CHECK(!parse(std::string(s),220,2048).valid);
    for(auto s:{"221","512","2048"})CHECK(parse(std::string(s),220,2048).valid);
    for(u32 n:{220u,221u,512u,2048u}){
        CHECK(map_image());auto p=plan_for(n,0x30000000);
        if(n==220){CHECK(p.patches.writes.empty()&&p.code.empty());continue;}
        std::string why;CHECK(patch_transaction::verify_expectations(p.patches,why));
        std::set<u32> seen;
        for(const auto& w:p.patches.writes)for(unsigned j=0;j<w.bytes.size();++j)CHECK(seen.insert(w.va+j).second);
        // Every guarded byte is essential, including read-only helpers and owner quiescence.
        for(const auto& g:p.patches.guards)for(unsigned j=0;j<g.bytes.size();++j){
            auto* b=static_cast<unsigned char*>(ptr(g.va+j));*b^=1;
            CHECK(!patch_transaction::verify_expectations(p.patches,why));*b^=1;
        }
        const auto a=layout_for(n);CHECK(a.size==a.base+a.defs.size&&a.base==12+a.files.size);
    }
}
static void descriptor_checks(){
    for(u32 n:{220u,512u}){
        const auto a=layout_for(n);const auto d=descriptor_for(a);bytes owner(a.size+32,0xa5);
        word(owner.data(),a.base+a.defs.mask)=n==220?255:511;
        for(u32 slot:{0u,n-1}){
            const auto h=slot|((n==220?1u<<8:1u<<9));
            auto* rec=owner.data()+a.base+slot*68;word(rec,0x34)=0x3f800000;word(rec,0x38)=0x40000000;
            CHECK(effect_templates::definitions(owner.data(),d)==owner.data()+a.base);
            CHECK(effect_templates::definition(owner.data(),h,d)==rec);
            if(n==220)CHECK(effect_templates::definition(owner.data(),h,d)==owner.data()+0x4874+(h&word(owner.data(),0x8a10))*0x44);
        }
        CHECK(std::all_of(owner.end()-32,owner.end(),[](auto b){return b==0xa5;}));
    }
}
static void agreement_checks(){
    using namespace gameplay_capabilities;
    record stock;CHECK(stock.finalize());
    for(const auto id:{effect_files_id,primitives_per_effect_id}){
        const u32 capacity=id==effect_files_id?512:32;
        record on,same,other,failed;
        CHECK(on.add(id,1,{capacity,0,0,0},true)&&on.finalize());
        CHECK(same.add(id,1,{capacity,0,0,0},true)&&same.finalize());
        CHECK(other.add(id,1,{capacity-1,0,0,0},true)&&other.finalize());
        CHECK(on.compare(same.bytes())==result::match);
        difference a{},b{};
        CHECK(on.compare(stock.bytes(),&a)==result::entries&&stock.compare(on.bytes(),&b)==result::entries);
        char text[128]{};on.describe(a.buckets,a.local_only,true,text,sizeof text);CHECK(std::strstr(text,rule_name(id))!=nullptr);
        stock.describe(b.buckets,b.local_only,true,text,sizeof text);CHECK(std::strstr(text,rule_name(id))!=nullptr&&std::strstr(text,"disabled on this PC")!=nullptr);
        CHECK(on.compare(other.bytes())==result::value&&other.compare(on.bytes())==result::value);
        CHECK(!failed.add(id,1,{0,0,0,0},false)&&!failed.finalize());
    }
    CHECK(primitives_per_effect_rules::plan_for(10,effect_templates::descriptor{},0,0).patches.writes.empty());
    for(unsigned n=11;n<=32;++n)CHECK(parse(std::to_string(n),10,32).valid);
    for(auto s:{"9","33","-1","oops"})CHECK(!parse(std::string(s),10,32).valid);
}
static void lifecycle(u32 n,bool models){
    CHECK(map_image());const auto a=layout_for(n);
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    void* model_block=nullptr;void* systems=nullptr;void* model_code=nullptr;
    if(models){
        const auto ml=model_pools_rules::aggregate_layout_for(model_pools_rules::x4_capacities);
        model_block=VirtualAlloc(nullptr,ml.size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        systems=VirtualAlloc(nullptr,ml.psys.size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
        model_code=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
        auto mp=model_pools_rules::plan_for(ml,{address(model_block),address(systems)},address(model_code));CHECK(mp.has_value());
        std::memcpy(model_code,mp->cave_code.data(),mp->cave_code.size());
        for(const auto& w:mp->writes){CHECK(std::memcmp(ptr(w.va),w.before.data(),w.before.size())==0);std::memcpy(ptr(w.va),w.bytes.data(),w.bytes.size());if(w.va==0x571724)std::memcpy(&model_target,w.bytes.data()+1,4);}
        model_target+=0x571729;
    }
    auto p=plan_for(n,address(code),models?model_target:0);
    if(n!=220){std::memcpy(code,p.code.data(),p.code.size());patch_transaction::transaction t;t.add(std::move(p.patches));CHECK(t.commit()==patch_transaction::outcome::committed);}
    // Clone matches retail for every valid starting index and both set/unset searches.
    std::puts("  helper");std::vector<u32> bits(a.words,0);auto stock=reinterpret_cast<find_next_method>(0x403d40);
    auto clone=reinterpret_cast<find_next_method>(n==220?0x403d40:p.find_next);
    for(u32 bit:{0u,9u,31u,32u,219u}){
        std::fill(bits.begin(),bits.end(),0);bits[bit/32]|=1u<<(bit%32);
        for(int start=0;start<=220;++start)for(unsigned char set:std::array<unsigned char,2>{0,1}){
            const int r=stock(bits.data(),nullptr,start,set),s=clone(bits.data(),nullptr,start,set);
            CHECK(r==std::min(s,220));
        }
    }
    std::puts("  getter");dependencies();
    auto* owner=reinterpret_cast<void*(__cdecl*)()>(0x4078d0)();CHECK(owner==heap_block&&heap_bytes==a.size&&canary());
    CHECK(word(owner,12+a.files.free)==n&&word(owner,a.base+a.defs.free)==n);
    CHECK(word(owner,a.base+a.defs.mask)==(n==220?255u:n==221?255u:n==512?511u:2047u));
    std::puts("  load");std::vector<u32> handles;
    for(u32 i=0;i<n;++i){char name[64];std::snprintf(name,sizeof(name),"synthetic_%04u",i);
        const auto h=reinterpret_cast<load_method>(0x419430)(owner,nullptr,name,11);
        CHECK(h!=0);handles.push_back(h);
        CHECK(word(owner,12+a.files.live)==i+1&&word(owner,a.base+a.defs.live)==i+1);
        CHECK(reinterpret_cast<load_method>(0x419430)(owner,nullptr,name,11)==h);
        CHECK(canary());
    }
    CHECK(reinterpret_cast<load_method>(0x419430)(owner,nullptr,"synthetic_overflow",11)==0);
    std::puts("  unload");reinterpret_cast<method>(0x419570)(owner,nullptr);
    CHECK(word(owner,12+a.files.live)==0&&word(owner,a.base+a.defs.live)==0&&canary());
    for(u32 i=0;i<a.words;++i)CHECK(word(owner,12+a.files.bitmap+i*4)==0&&word(owner,a.base+a.defs.bitmap_a+i*4)==0&&word(owner,a.base+a.defs.bitmap_b+i*4)==0);
    CHECK(reinterpret_cast<load_method>(0x419430)(owner,nullptr,"synthetic_reuse",11)!=0);
    // The owner's native destructor adjustment reaches the definition destructor.
    reinterpret_cast<method>(0x407630)(owner,nullptr);CHECK(canary());
    for(u32 i=0;i<a.words;++i)CHECK(word(owner,a.base+a.defs.bitmap_a+i*4)==0);
    VirtualFree(heap_block,0,MEM_RELEASE);heap_block=nullptr;VirtualFree(code,0,MEM_RELEASE);
    if(models){VirtualFree(model_block,0,MEM_RELEASE);VirtualFree(systems,0,MEM_RELEASE);VirtualFree(model_code,0,MEM_RELEASE);}
    model_target=0;
}
static void installer(){
    CHECK(map_image());bytes before(union_space,union_space+0x700000);
    if(mode=="built")word(ptr(owner_pointer))=1;
    if(mode=="guard")*static_cast<unsigned char*>(ptr(0x405f76))^=1;
    if(mode=="foreign-model")*static_cast<unsigned char*>(ptr(0x571725))^=1;
    const bytes initial(union_space,union_space+0x700000);
    effect_templates::install(reinterpret_cast<HMODULE>(mode=="base"?0x500000:0x400000));
    const bool on=mode=="on";
    CHECK(effect_templates::layout().capacity==(on?512u:220u));
    if(!on)CHECK(std::memcmp(initial.data(),union_space,initial.size())==0);
    CHECK(registrations==(mode=="off"||mode=="stock"?0u:1u));
    CHECK(agreement.layout(1,{32,224,256,8},true));
    CHECK(agreement.finalize()==(on||mode=="off"||mode=="stock"));
    if(on)CHECK(effect_templates::status().find("fx files 0/512; fx defs 0/512")!=std::string::npos);
}

static std::array<unsigned char,0x38a24> primitive_manager{};
static std::array<std::array<unsigned char,0x118>,64> primitive_records{};
static std::vector<u32> freed;
static unsigned primitive_taken=0;
static void* __cdecl get_primitives(){return primitive_manager.data();}
static void* __fastcall take_primitive(void*,void*){return primitive_records[primitive_taken++].data();}
static void __fastcall free_primitive(void*,void*,u32 p){freed.push_back(p);}
static void primitive_dependencies(){
    redirect(0x418860,reinterpret_cast<void*>(&get_primitives));
    redirect(0x418660,reinterpret_cast<void*>(&take_primitive));
    redirect(0x4187d0,reinterpret_cast<void*>(&free_primitive));
}
static u32 input_regs[8]{},result_regs[9]{};
static void* access_function=nullptr;
__declspec(naked) static void run_access(){__asm {
    pushad
    pushfd
    mov eax,dword ptr [input_regs]
    mov ecx,dword ptr [input_regs+4]
    mov edx,dword ptr [input_regs+8]
    mov ebx,dword ptr [input_regs+12]
    mov ebp,dword ptr [input_regs+20]
    mov esi,dword ptr [input_regs+24]
    mov edi,dword ptr [input_regs+28]
    push 202h
    popfd
    stc
    call dword ptr [access_function]
    pushfd
    pushad
    mov esi,esp
    mov edi,offset result_regs
    mov ecx,9
    cld
    rep movsd
    popad
    popfd
    popfd
    popad
    ret
}}
static std::array<u32,9> element_result(const effect_template_sites::span& s,void* rec,unsigned i){
    const auto r=address(rec),list=r+4;
    for(unsigned j=0;j<8;++j)input_regs[j]=0x11111111u*(j+1);
    if(s.name=="append"){input_regs[6]=r;input_regs[1]=i;input_regs[0]=address(primitive_records[i].data());}
    if(s.name=="classify"){input_regs[1]=list;input_regs[2]=i;}
    if(s.name=="spawn_cursor"){input_regs[0]=list;input_regs[1]=i;input_regs[7]=list+i*4;}
    if(s.name=="spawn"){input_regs[2]=list;input_regs[7]=i;}
    if(s.name=="release"){input_regs[6]=list;input_regs[7]=i;}
    if(s.name=="flags"){input_regs[0]=list;input_regs[1]=i;input_regs[6]=0x29000000;}
    unsigned char old[5]{};std::memcpy(old,ptr(s.end),5);
    const unsigned char ret[]={0xc3,0x90,0x90,0x90,0x90};
    const unsigned char popret[]={0x8d,0x64,0x24,0x04,0xc3};
    std::memcpy(ptr(s.end),s.name=="release"||s.name=="spawn_cursor"?popret:ret,5);
    FlushInstructionCache(GetCurrentProcess(),ptr(s.end),5);access_function=ptr(s.va);run_access();
    std::array<u32,9> out{};std::copy(std::begin(result_regs),std::end(result_regs),out.begin());
    std::memcpy(ptr(s.end),old,5);FlushInstructionCache(GetCurrentProcess(),ptr(s.end),5);return out;
}
static void primitive_checks(u32 n,u32 capacity){
    using primitives_per_effect_rules::spill_columns;
    CHECK(map_image());const auto a=layout_for(n);const auto d=descriptor_for(a);
    auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    auto w4=effect_templates_rules::plan_for(n,address(code));
    if(n>220){std::memcpy(code,w4.code.data(),w4.code.size());patch_transaction::transaction t;t.add(std::move(w4.patches));CHECK(t.commit()==patch_transaction::outcome::committed);}
    dependencies();primitive_dependencies();std::memset(primitive_manager.data(),0,primitive_manager.size());
    auto* owner=reinterpret_cast<void*(__cdecl*)()>(0x4078d0)();CHECK(heap_bytes==a.size);
    auto* rec=static_cast<unsigned char*>(owner)+a.base;
    for(unsigned j=0;j<primitive_records.size();++j){word(primitive_records[j].data(),8)=j;word(primitive_records[j].data(),0x40)=j+123;word(primitive_records[j].data(),0x44)=0x29000000;}
    // Capture every retail element instruction at 0..9, including all registers
    // and status flags. Replaying the caves must give exactly the same result.
    std::array<std::array<std::array<u32,9>,10>,6> reference{};
    for(unsigned i=0;i<10;++i)word(rec,4+i*4)=address(primitive_records[i].data());
    for(unsigned s=0;s<6;++s)for(unsigned i=0;i<10;++i)reference[s][i]=element_result(effect_template_sites::elements[s],rec,i);
    word(ptr(owner_pointer))=0;
    bytes spill(n*spill_columns*4+64,0);std::fill(spill.end()-64,spill.end(),static_cast<unsigned char>(0xa5));
    auto* caves=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_COMMIT|MEM_RESERVE,PAGE_EXECUTE_READWRITE));
    auto p=primitives_per_effect_rules::plan_for(capacity,d,address(caves),address(spill.data()));
    std::string why;CHECK(patch_transaction::verify_expectations(p.patches,why));
    for(const auto& g:p.patches.guards)for(unsigned j=0;j<g.bytes.size();++j){auto* b=static_cast<unsigned char*>(ptr(g.va+j));*b^=1;CHECK(!patch_transaction::verify_expectations(p.patches,why));*b^=1;}
    std::memcpy(caves,p.code.data(),p.code.size());
    const auto helper_before=bytes(static_cast<unsigned char*>(ptr(0x5615b0)),static_cast<unsigned char*>(ptr(0x561684)));
    patch_transaction::transaction t;t.add(std::move(p.patches));CHECK(t.commit()==patch_transaction::outcome::committed);
    CHECK(std::memcmp(helper_before.data(),ptr(0x5615b0),helper_before.size())==0);
    word(ptr(owner_pointer))=address(owner);rec=static_cast<unsigned char*>(owner)+a.base;
    // Same allocator backing is normally reused; rec addresses may differ after
    // VirtualAlloc. Normalize saved native-base/cell registers before comparison.
    for(unsigned i=0;i<10;++i)word(rec,4+i*4)=address(primitive_records[i].data());
    for(unsigned s=0;s<6;++s)for(unsigned i=0;i<10;++i){
        const auto actual=element_result(effect_template_sites::elements[s],rec,i);
        for(unsigned reg=0;reg<9;++reg)if(reg!=3)CHECK(actual[reg]==reference[s][i][reg]);
    }
    for(unsigned slot:{0u,n-1}){
        rec=static_cast<unsigned char*>(owner)+a.base+slot*68;
        for(unsigned count:{10u,11u,31u,32u,33u}){
            std::memset(rec,0,68);primitive_taken=0;freed.clear();
            for(unsigned i=0;i<count;++i){const auto p0=reinterpret_cast<void*(__fastcall*)(void*,void*)>(0x4188a0)(rec,nullptr);CHECK((p0!=nullptr)==(i<capacity));}
            const unsigned admitted=std::min(count,capacity);CHECK(word(rec,0x30)==admitted&&primitive_taken==admitted);
            CHECK(word(rec,0x2c)==(admitted==32?0xffffffffu:(1u<<admitted)-1));
            for(unsigned i=0;i<admitted;++i){
                const auto cell=i<10?word(rec,4+i*4):word(spill.data(),(slot*spill_columns+i-10)*4);
                CHECK(cell==address(primitive_records[i].data()));
                for(unsigned s=1;s<6;++s){const auto r=element_result(effect_template_sites::elements[s],rec,i);
                    if(s==1)CHECK(r[7]==cell);
                    if(s==2)CHECK(r[0]==(i<10?address(rec)+4+i*4:address(spill.data())+(slot*spill_columns+i-10)*4)&&r[4]==cell+0x14);
                    if(s==3)CHECK(r[1]==cell&&r[7]==i+123);
                    if(s==4)CHECK(r[7]==address(primitive_manager.data()));
                    if(s==5)CHECK(r[0]==cell);
                }
            }
            reinterpret_cast<method>(0x4189b0)(rec,nullptr);
            CHECK(freed.size()==admitted&&word(rec,0x30)==0&&word(rec,0x2c)==0);
            for(unsigned i=0;i<admitted;++i)CHECK(freed[i]==address(primitive_records[i].data()));
            for(unsigned i=0;i<spill_columns;++i)CHECK(word(spill.data(),(slot*spill_columns+i)*4)==0);
            CHECK(canary()&&std::all_of(spill.end()-64,spill.end(),[](auto b){return b==0xa5;}));
        }
    }
    // The helper's effect is identical for every bitmap confined to native bits.
    std::uint32_t delta=0;std::memcpy(&delta,ptr(0x4189d6),4);const auto helper=0x4189da+delta;
    for(unsigned bitmap=0;bitmap<1024;++bitmap){
        std::array<u32,11> stock{},raised{};stock[10]=raised[10]=bitmap;
        reinterpret_cast<method>(0x5615b0)(stock.data(),nullptr);reinterpret_cast<method>(helper)(raised.data(),nullptr);CHECK(stock==raised);
    }
    VirtualFree(heap_block,0,MEM_RELEASE);heap_block=nullptr;VirtualFree(code,0,MEM_RELEASE);VirtualFree(caves,0,MEM_RELEASE);
}
static void primitive_installer(){
    CHECK(map_image());if(mode=="prim-built")word(ptr(owner_pointer))=1;
    if(mode=="prim-guard")*static_cast<unsigned char*>(ptr(0x4188b4))^=1;
    effect_templates::install(reinterpret_cast<HMODULE>(0x400000));
    bytes initial(union_space,union_space+0x700000);
    primitives_per_effect::install(reinterpret_cast<HMODULE>(0x400000));
    const bool on=mode=="prim-on"||mode=="prim-raised";
    if(!on)CHECK(std::memcmp(initial.data(),union_space,initial.size())==0);
    CHECK(agreement.layout(1,{32,224,256,8},true));CHECK(agreement.finalize()==(on||mode=="prim-stock"));
    CHECK(primitives_per_effect::status().find(on?"/32":"/10")!=std::string::npos);
}
int main(int argc,char** argv){
    std::setvbuf(stdout,nullptr,_IONBF,0);
    AddVectoredExceptionHandler(1,fault);
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    const char* exe=argc>1?argv[1]:std::getenv("XML2_FIX_TEST_EXE");if(!exe)return 2;
    std::ifstream f(exe,std::ios::binary);raw.assign(std::istreambuf_iterator<char>(f),{});if(raw.size()<0x400)return 2;
    if(argc>2)mode=argv[2];
    if(mode=="unit"){std::puts("plans");plan_checks();std::puts("descriptors");descriptor_checks();agreement_checks();for(u32 n:{220u,221u,512u,2048u})for(bool models:{false,true}){std::printf("lifecycle %u models %d\n",n,models);lifecycle(n,models);}}
    else if(mode=="primitives"){for(u32 n:{220u,512u,2048u})for(u32 cap:{11u,32u}){std::printf("primitive spill N=%u cap=%u\n",n,cap);primitive_checks(n,cap);}}
    else if(mode.starts_with("prim-"))primitive_installer();
    else installer();
    std::printf("effect templates: %u checks, %u failures (%s)\n",checks,failures,mode.c_str());return failures?1:0;
}
