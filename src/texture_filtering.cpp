#include "texture_filtering.hpp"
#ifdef AF_SAMPLER_RULES_HEADER
#include AF_SAMPLER_RULES_HEADER
#else
#include "texture_filtering_rules.hpp"
#endif
#include "log.hpp"
#include "patch_transaction.hpp"
#include "centered_ui_trace.hpp"
#include <mutex>
#include <cstring>

namespace texture_filtering {
namespace {
using byte=unsigned char;
using address=patch_transaction::address;
address addr(const void* p) {return address(reinterpret_cast<std::uintptr_t>(p));}
template<class T> T& field(void* p,unsigned off) {return *reinterpret_cast<T*>(static_cast<byte*>(p)+off);}
remastered_rules::settings selected;
bool engine_installed=false;
void* engine_code=nullptr;
std::recursive_mutex& lock() {static std::recursive_mutex value;return value;}
associations& sources() {static associations value;return value;}
auto& full_handles() {static std::map<std::pair<std::uintptr_t,unsigned>,std::uint32_t> value;return value;}
using source_t=bool(__fastcall*)(void*,void*,int,void**,unsigned);
using create_t=int(__fastcall*)(void*,void*,int,int,int,int,unsigned,unsigned,int,int);
using delete_t=void(__fastcall*)(void*,void*,int);
using nonindexed_t=void(__fastcall*)(void*,void*,int,int,int);
using indexed_t=void(__fastcall*)(void*,void*,int,int,int,int,int);
source_t original_source=nullptr;
create_t original_create=nullptr;
delete_t original_delete=nullptr;
nonindexed_t original_nonindexed=nullptr;
indexed_t original_indexed=nullptr;
struct device_record {device_state state;};
struct device_class {std::array<void*,4> original{};};
auto& devices() {static std::map<void*,device_record> value;return value;}
auto& classes() {static std::map<void**,device_class> value;return value;}
using primitive_t=HRESULT(STDMETHODCALLTYPE*)(void*,DWORD,UINT,UINT);
using indexed_primitive_t=HRESULT(STDMETHODCALLTYPE*)(void*,DWORD,UINT,UINT,UINT,UINT);
using primitive_up_t=HRESULT(STDMETHODCALLTYPE*)(void*,DWORD,UINT,const void*,UINT);
using indexed_up_t=HRESULT(STDMETHODCALLTYPE*)(void*,DWORD,UINT,UINT,UINT,const void*,DWORD,const void*,UINT);

// Read the +0x58 char* as a NUL-terminated C string. The failed probe's fixed
// 260-byte interpretation is deliberately not used. Oversized/unreadable paths
// are excluded, never truncated into a possibly eligible prefix.
bool read_name(void** images,char* out,std::size_t capacity) {
    __try {
        if(!images || !images[0]) return false;
        const char* path=field<const char*>(images[0],0x58);
        if(!path) return false;
        const auto length=std::strlen(path);
        if(!length || length>=capacity) return false;
        std::memcpy(out,path,length+1);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {return false;}
}
void retire_index(void* context,unsigned handle) {
    const auto key=std::pair{reinterpret_cast<std::uintptr_t>(context),handle&0xffffu};
    const auto it=full_handles().find(key);
    if(it!=full_handles().end()) {sources().retire(key.first,it->second);full_handles().erase(it);}
}
int __fastcall create_texture(void* context,void*,int w,int h,int format,int source,unsigned flags,unsigned levels,int palette,int kind) {
    const int handle=original_create(context,nullptr,w,h,format,source,flags,levels,palette,kind);
    if(handle!=-1) {
        std::lock_guard guard(lock());retire_index(context,unsigned(handle));
        sources().create(reinterpret_cast<std::uintptr_t>(context),unsigned(handle));
        full_handles()[{reinterpret_cast<std::uintptr_t>(context),unsigned(handle)&0xffffu}]=unsigned(handle);
    }
    return handle;
}
bool __fastcall set_source(void* context,void*,int handle,void** images,unsigned flags) {
    char name[4096]{};const bool named=read_name(images,name,sizeof(name));
    // Replacement invalidates the previous identity even if upload fails.
    {std::lock_guard guard(lock());retire_index(context,unsigned(handle));}
    const bool result=original_source(context,nullptr,handle,images,flags);
    if(result) {
        std::lock_guard guard(lock());
        sources().create(reinterpret_cast<std::uintptr_t>(context),unsigned(handle));
        auto identity=source_identity::from_path(named?name:"");
        sources().replace_source(reinterpret_cast<std::uintptr_t>(context),unsigned(handle),std::move(identity),nullptr,0,0,0);
        full_handles()[{reinterpret_cast<std::uintptr_t>(context),unsigned(handle)&0xffffu}]=unsigned(handle);
    }
    return result;
}
void __fastcall delete_texture(void* context,void*,int handle) {
    {std::lock_guard guard(lock());retire_index(context,unsigned(handle));}
    original_delete(context,nullptr,handle);
}
void __fastcall nonindexed(void* context,void*,int kind,int first,int count) {
    draw_scope scope({field<void*>(context,0x144),reinterpret_cast<std::uintptr_t>(context),pass::world_material});
    original_nonindexed(context,nullptr,kind,first,count);
}
void __fastcall indexed(void* context,void*,int kind,int first,int count,int min,int max) {
    draw_scope scope({field<void*>(context,0x144),reinterpret_cast<std::uintptr_t>(context),pass::world_material});
    original_indexed(context,nullptr,kind,first,count,min,max);
}
bool world_draw(void* device) {
    centered_ui_trace::preserve_fp fp;
    float projection[16]{};DWORD shader=0;
    if(FAILED(d3d8::method<HRESULT(STDMETHODCALLTYPE*)(void*,DWORD,float*)>(device,38)(device,3,projection)) ||
       FAILED(d3d8::method<HRESULT(STDMETHODCALLTYPE*)(void*,DWORD*)>(device,77)(device,&shader))) return false; // 77 = GetVertexShader (76 = SetVertexShader)
    // Live proof: UI is the 384-high orthographic canvas; layer_dispatch and
    // cached ZENABLE are shared/stale and cannot classify a world draw.
    return std::all_of(std::begin(projection),std::end(projection),[](float v){return std::isfinite(v);}) &&
        projection[0]>0 && projection[5]>0 && std::abs(projection[11]-1.f)<.001f &&
        std::abs(projection[15])<.001f && world_fvf(shader);
}
unsigned bindings(void* context,std::uint64_t generation,std::array<binding,8>& bound) {
    const unsigned n=field<unsigned>(context,0x490);
    if(n>8) return 9;
    void* table=field<void*>(context,0x330);if(!table) return 9;
    const unsigned count=field<unsigned short>(table,0);
    auto* records=field<byte*>(table,0x10);if(count && !records) return 9;
    unsigned used=0;
    for(unsigned i=0;i<n;++i) {
        if(!field<byte>(context,0x354+i)) continue;
        const int index=field<int>(context,0x334+4*i),stage=field<int>(context,0x384+4*i);
        if(index<0 || unsigned(index)>=count || stage<0 || stage>=8) return 9;
        const auto hit=full_handles().find({reinterpret_cast<std::uintptr_t>(context),unsigned(index)});
        auto* source=hit==full_handles().end()?nullptr:sources().find(reinterpret_cast<std::uintptr_t>(context),hit->second);
        if(source) {
            auto& value=*source;auto* record=records+unsigned(index)*0x6c;
            value.object=field<void*>(record,0x40);value.authored_min=record[4];value.authored_mag=record[5];value.device_generation=generation;
            bound[used]={unsigned(stage),&value};
        } else bound[used]={unsigned(stage),nullptr};
        ++used;
    }
    return used;
}
template<class Call> HRESULT forward(void* device,Call call) {
    std::lock_guard guard(lock());
    // This check precedes every owner/device/capability early-out: even an
    // untracked second device or a nested raw UI draw must see native sampling.
    if(auto* active=active_transaction<d3d8_sampler_api>) {return active->abandon()?call():d3d8::err_invalid_call;}
    const auto it=devices().find(device);
    if(it==devices().end() || current_scope.device!=device || !current_scope.context ||
       it->second.state.lost || it->second.state.disabled || !it->second.state.amount) return call();
    auto& state=it->second.state;
    d3d8_sampler_api api{device};
    if(!world_draw(device)) return call();
    std::array<binding,8> bound{};
    const auto count=bindings(reinterpret_cast<void*>(current_scope.context),state.generation,bound);
    if(count>8) return call();
    const HRESULT result=draw(api,state,device,bound.data(),count,call);
    if(result==d3d8::err_device_lost || result==d3d8::err_device_not_reset) state.lost=true;
    return result;
}
void* original(void* device,unsigned slot) {std::lock_guard guard(lock());return classes().at(*static_cast<void***>(device)).original[slot-70];}
HRESULT STDMETHODCALLTYPE primitive(void* d,DWORD type,UINT start,UINT count) {
    const auto fn=reinterpret_cast<primitive_t>(original(d,70));return forward(d,[&]{return fn(d,type,start,count);});
}
HRESULT STDMETHODCALLTYPE indexed_primitive(void* d,DWORD type,UINT min,UINT vertices,UINT start,UINT count) {
    const auto fn=reinterpret_cast<indexed_primitive_t>(original(d,71));return forward(d,[&]{return fn(d,type,min,vertices,start,count);});
}
HRESULT STDMETHODCALLTYPE primitive_up(void* d,DWORD type,UINT count,const void* data,UINT stride) {
    const auto fn=reinterpret_cast<primitive_up_t>(original(d,72));return forward(d,[&]{return fn(d,type,count,data,stride);});
}
HRESULT STDMETHODCALLTYPE indexed_up(void* d,DWORD type,UINT min,UINT vertices,UINT count,const void* indices,DWORD format,const void* data,UINT stride) {
    const auto fn=reinterpret_cast<indexed_up_t>(original(d,73));return forward(d,[&]{return fn(d,type,min,vertices,count,indices,format,data,stride);});
}
bool hook_device(void* device) {
    const void* hooks[]{primitive,indexed_primitive,primitive_up,indexed_up};
    auto** vt=*static_cast<void***>(device);
    if(classes().contains(vt)) {
        for(unsigned i=0;i<4;++i) if(vt[70+i]!=hooks[i]) return false;
        return true;
    }
    patch_transaction::contribution c;c.owner="Remastered draw-scoped AF device class";
    device_class values;
    for(unsigned i=0;i<4;++i) {
        values.original[i]=vt[70+i];if(!values.original[i] || values.original[i]==hooks[i]) return false;
        std::vector<byte> expected(4),replacement(4);std::memcpy(expected.data(),&values.original[i],4);const auto target=addr(hooks[i]);std::memcpy(replacement.data(),&target,4);
        c.guards.push_back({addr(vt+70+i),expected,"current draw method"});c.writes.push_back({addr(vt+70+i),replacement,"draw method"});
    }
    // Allocate metadata before publishing executable dispatch pointers.
    classes().emplace(vt,values);
    c.abandon=[vt] {classes().erase(vt);};
    c.retain=[] {logger::write("remastered AF: device rollback uncertified");TerminateProcess(GetCurrentProcess(),1);};
    patch_transaction::transaction transaction;transaction.add(std::move(c));
    const auto result=transaction.commit();
    return result==patch_transaction::outcome::committed;
}
bool install_engine(HMODULE module) {
    if(!module) return false;
    auto* base=reinterpret_cast<byte*>(module);
    struct detour {unsigned rva;const char* hex;const void* hook;void** original;};
    const detour hooks[]{
        {0x419f0,"83ec0ca00a971810",create_texture,reinterpret_cast<void**>(&original_create)},
        {0x42e10,"83ec0c5355",set_source,reinterpret_cast<void**>(&original_source)},
        {0x42d10,"568bf1578b7c240c",delete_texture,reinterpret_cast<void**>(&original_delete)},
        {0x352d0,"83ec08558b6c2414",nonindexed,reinterpret_cast<void**>(&original_nonindexed)},
        {0x354f0,"51558b6c2410",indexed,reinterpret_cast<void**>(&original_indexed)},
    };
    patch_transaction::contribution c;c.owner="Remastered authored-path AF bridge";
    for(const auto& h:hooks) {
        auto expected=patch_transaction::bytes_of(h.hex);
        if(h.rva==0x419f0) {const auto global=addr(base+0x18970a);std::memcpy(expected.data()+4,&global,4);}
        c.guards.push_back({addr(base+h.rva),expected,"engine entry"});
    }
    for(const auto [rva,hex]:{std::pair{0x41d1cu,"8b8e3003000033c0668b013be8"},
        {0x41d62u,"578b7c24208b8cbe60030000"},{0xcfd40u,"010000000200000001000000010000000200000002000000"}})
        c.guards.push_back({addr(base+rva),patch_transaction::bytes_of(hex),"binding/filter layout"});
    std::string why;if(!patch_transaction::verify_expectations(c,why)) {logger::write("remastered AF: %s",why.c_str());return false;}
    engine_code=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);if(!engine_code) return false;
    unsigned cursor=0;
    for(const auto& h:hooks) {
        const auto length=unsigned(std::strlen(h.hex)/2);auto* entry=static_cast<byte*>(engine_code)+cursor;*h.original=entry;
        // Copy live relocated operands; stolen prefixes contain no relative calls/branches.
        std::memcpy(entry,base+h.rva,length);
        const auto tail=patch_transaction::relocation(addr(entry)+length,"e9",true,addr(base+h.rva+length),0,"engine continuation").bytes;
        std::memcpy(entry+length,tail.data(),tail.size());cursor+=32;
        c.writes.push_back(patch_transaction::relocation(addr(base+h.rva),"e9",true,addr(h.hook),length-5,"engine entry"));
    }
    DWORD old=0;if(!VirtualProtect(engine_code,4096,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),engine_code,4096)) {
        VirtualFree(engine_code,0,MEM_RELEASE);engine_code=nullptr;return false;
    }
    c.publish=[] {engine_installed=true;};
    c.abandon=[] {VirtualFree(engine_code,0,MEM_RELEASE);engine_code=nullptr;};
    c.retain=[] {logger::write("remastered AF: engine rollback uncertified");TerminateProcess(GetCurrentProcess(),1);};
    patch_transaction::transaction transaction;transaction.add(std::move(c));
    return transaction.commit()==patch_transaction::outcome::committed;
}
}
void configure(const remastered_rules::settings& settings) {
    if(!settings.enabled || !settings.anisotropy) return;
    selected=settings;
    if(engine_installed || install_engine(GetModuleHandleW(L"libIGGfx.dll")))
        logger::write("remastered: selective AF authored-path bridge active; tilesets/actors, perspective engine draws, current binding required; point art=%u",unsigned(selected.anisotropy_on_point_art));
    else logger::write("remastered: selective AF unavailable: engine bridge guard/module refused; native sampling retained");
}
void device_ready(void* device,std::uint64_t generation) {
    if(!engine_installed || !selected.enabled || !selected.anisotropy) return;
    std::lock_guard guard(lock());d3d8::caps8 caps{};
    auto& value=devices()[device];
    if(FAILED(d3d8::method<HRESULT(STDMETHODCALLTYPE*)(void*,d3d8::caps8*)>(device,7)(device,&caps))) {value.state.disabled=true;return;}
    value.state.reset(generation,selected.anisotropy,caps);value.state.on_point_art=selected.anisotropy_on_point_art;
    if(value.state.amount && !hook_device(device)) value.state.disabled=true;
    logger::write("remastered AF: device generation=%llu amount=%u point-art=%u disabled=%u",generation,value.state.amount,unsigned(value.state.on_point_art),unsigned(value.state.disabled));
}
void device_unavailable(void* device) {
    if(!engine_installed) return;
    std::lock_guard guard(lock());if(auto it=devices().find(device);it!=devices().end()) it->second.state.lost=true;
}
void device_recovered(void* device) {
    if(!engine_installed) return;
    std::lock_guard guard(lock());if(auto it=devices().find(device);it!=devices().end()) it->second.state.lost=false;
}
}
