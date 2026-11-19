#include "../src/texture_filtering.cpp"
#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <fstream>
#include <iostream>
#include <vector>
namespace logger {void write(const char*,...) {} }
namespace {
using namespace texture_filtering;
struct texture {void** vt;unsigned refs=0;bool rt=false;unsigned mip_levels=2;unsigned kind=3;};
struct device {
    void** vt;
    std::array<std::array<unsigned,22>,8> samplers{};
    std::array<texture*,8> textures{};
    bool ortho=false,rhw=false,expect0=false,expect1=false,reenter=false;
    unsigned sets=0,gets=0,draws=0,fail_set=0;int original_slot=0;
    unsigned unknown_shader=0;
};
texture *tex0=nullptr,*tex1=nullptr;
device* active_device=nullptr;
unsigned return_handle=0x10001;
bool source_ok=true;
std::array<byte,0x500> context{};
std::array<byte,0x14> table{};
std::array<byte,4*0x6c> records{};
ULONG STDMETHODCALLTYPE release(void* p) {auto& t=*static_cast<texture*>(p);assert(t.refs);return --t.refs;}
DWORD STDMETHODCALLTYPE type(void* p) {return static_cast<texture*>(p)->kind;}
DWORD STDMETHODCALLTYPE levels(void* p) {return static_cast<texture*>(p)->mip_levels;}
HRESULT STDMETHODCALLTYPE desc(void* p,UINT level,d3d8::surface_desc* out) {assert(!level);*out={};out->width=out->height=64;out->usage=static_cast<texture*>(p)->rt?1:0;return S_OK;}
HRESULT STDMETHODCALLTYPE caps(void*,d3d8::caps8* out) {*out={};out->raster_caps=0x20000;out->texture_filter_caps=0x400;out->max_anisotropy=8;out->max_texture_blend_stages=out->max_simultaneous_textures=8;return S_OK;}
HRESULT STDMETHODCALLTYPE transform(void* p,DWORD kind,float* matrix) {assert(kind==3);std::fill(matrix,matrix+16,0.f);matrix[0]=matrix[5]=1;auto& d=*static_cast<device*>(p);matrix[11]=d.ortho?0.f:1.f;matrix[15]=d.ortho?1.f:0.f;return S_OK;}
// Slot 76 is IDirect3DDevice8::SetVertexShader: world_draw must never call it (the 0.7 live probe found AF black frames when it did).
HRESULT STDMETHODCALLTYPE set_vertex_shader_trap(void*,DWORD) {std::fprintf(stderr,"SetVertexShader (slot 76) called by the AF classifier\n");std::abort();}
HRESULT STDMETHODCALLTYPE shader(void* p,DWORD* out) {auto& d=*static_cast<device*>(p);*out=d.unknown_shader?d.unknown_shader:d.rhw?4:0x112;return S_OK;}
HRESULT STDMETHODCALLTYPE get(void* p,DWORD stage,DWORD key,DWORD* out) {auto& d=*static_cast<device*>(p);++d.gets;*out=d.samplers[stage][key];return S_OK;}
HRESULT STDMETHODCALLTYPE set(void* p,DWORD stage,DWORD key,DWORD value) {auto& d=*static_cast<device*>(p);if(++d.sets==d.fail_set) return E_FAIL;d.samplers[stage][key]=value;return S_OK;}
HRESULT STDMETHODCALLTYPE bound(void* p,DWORD stage,void** out) {auto* t=static_cast<device*>(p)->textures[stage];*out=t;if(t)++t->refs;return S_OK;}
HRESULT actual_draw(device& d,unsigned slot) {
    ++d.draws;d.original_slot=slot;
    assert((d.samplers[0][17]==3)==d.expect0 && (d.samplers[1][17]==3)==d.expect1);
    if(d.expect0) assert(d.samplers[0][21]==8);
    if(d.expect1) assert(d.samplers[1][21]==8);
    if(d.reenter) {
        d.reenter=false;d.expect0=d.expect1=false;
        // Reentry loses the engine owner completely; it still must unwind AF.
        draw_scope unknown({});
        const auto fn=reinterpret_cast<primitive_t>(d.vt[70]);assert(fn(&d,4,17,9)==S_FALSE);
    }
    return S_FALSE;
}
HRESULT STDMETHODCALLTYPE dp(void* p,DWORD type,UINT start,UINT n) {assert(type==4 && start==17 && n==9);return actual_draw(*static_cast<device*>(p),70);}
HRESULT STDMETHODCALLTYPE dip(void* p,DWORD type,UINT min,UINT n,UINT start,UINT count) {assert(type==4 && min==2 && n==3 && start==17 && count==9);return actual_draw(*static_cast<device*>(p),71);}
HRESULT STDMETHODCALLTYPE up(void* p,DWORD type,UINT n,const void* data,UINT stride) {assert(type==4 && n==9 && data==reinterpret_cast<void*>(0x1234) && stride==24);return actual_draw(*static_cast<device*>(p),72);}
HRESULT STDMETHODCALLTYPE iup(void* p,DWORD type,UINT min,UINT n,UINT count,const void* indices,DWORD format,const void* data,UINT stride) {assert(type==4 && min==2 && n==3 && count==9 && indices==reinterpret_cast<void*>(0x4321) && format==101 && data==reinterpret_cast<void*>(0x1234) && stride==24);return actual_draw(*static_cast<device*>(p),73);}
bool __fastcall source(void*,void*,int,void**,unsigned) {return source_ok;}
int __fastcall create(void*,void*,int,int,int,int,unsigned,unsigned,int,int) {return return_handle;}
void __fastcall destroy(void*,void*,int) {}
void submit(unsigned slot) {
    auto& d=*active_device;HRESULT hr=E_FAIL;
    if(slot==70) hr=reinterpret_cast<primitive_t>(d.vt[70])(&d,4,17,9);
    if(slot==71) hr=reinterpret_cast<indexed_primitive_t>(d.vt[71])(&d,4,2,3,17,9);
    if(slot==72) hr=reinterpret_cast<primitive_up_t>(d.vt[72])(&d,4,9,reinterpret_cast<void*>(0x1234),24);
    if(slot==73) hr=reinterpret_cast<indexed_up_t>(d.vt[73])(&d,4,2,3,9,reinterpret_cast<void*>(0x4321),101,reinterpret_cast<void*>(0x1234),24);
    assert(hr==S_FALSE);
}
void __fastcall native_nonindexed(void*,void*,int,int,int) {submit(70);}
void __fastcall native_indexed(void*,void*,int,int,int,int,int) {submit(71);}
void named(unsigned handle,const std::string& path) {
    std::array<byte,0x60> image{};field<const char*>(image.data(),0x58)=path.c_str();void* images[]{image.data()};
    assert(set_source(context.data(),nullptr,int(handle),images,0));
}
void* map_engine(const char* path) {
    std::ifstream input(path,std::ios::binary);std::vector<byte> file((std::istreambuf_iterator<char>(input)),{});assert(file.size()>0x1000);
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(file.data()+reinterpret_cast<IMAGE_DOS_HEADER*>(file.data())->e_lfanew);
    auto* image=static_cast<byte*>(VirtualAlloc(nullptr,nt->OptionalHeader.SizeOfImage,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(image);
    std::memcpy(image,file.data(),nt->OptionalHeader.SizeOfHeaders);
    auto* sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) std::memcpy(image+sections[i].VirtualAddress,file.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
    const auto reloc=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC];
    for(unsigned pos=0;pos<reloc.Size;) {
        auto* block=reinterpret_cast<IMAGE_BASE_RELOCATION*>(image+reloc.VirtualAddress+pos);if(!block->SizeOfBlock)break;
        auto* entries=reinterpret_cast<unsigned short*>(block+1);
        for(unsigned i=0;i<(block->SizeOfBlock-8)/2;++i) if((entries[i]>>12)==IMAGE_REL_BASED_HIGHLOW)
            *reinterpret_cast<unsigned*>(image+block->VirtualAddress+(entries[i]&0xfff))+=addr(image)-nt->OptionalHeader.ImageBase;
        pos+=block->SizeOfBlock;
    }
    return image;
}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    assert(argc>=2);const std::string mode=argc>2?argv[2]:"all";
    auto* engine=static_cast<byte*>(map_engine(argv[1]));
    const unsigned sites[]{0x419f0,0x42e10,0x42d10,0x352d0,0x354f0,0x41d1c,0x41d62,0xcfd40};
    std::array<std::array<byte,16>,8> before{};for(unsigned i=0;i<8;++i)std::memcpy(before[i].data(),engine+sites[i],16);
    if(mode.starts_with("guard")) engine[sites[std::stoul(mode.substr(5))]]^=1;
    const bool installed=install_engine(reinterpret_cast<HMODULE>(engine));
    if(mode.starts_with("guard")) {
        assert(!installed);
        for(unsigned i=0;i<5;++i) if(i!=std::stoul(mode.substr(5))) assert(!std::memcmp(before[i].data(),engine+sites[i],16));
        std::cout<<"PASS AF atomic "<<mode<<"\n";return 0;
    }
    assert(installed && engine_installed);
    assert(!std::memcmp(original_create,before[0].data(),8));
    original_source=source;original_create=create;original_delete=destroy;original_nonindexed=native_nonindexed;original_indexed=native_indexed;
    std::array<void*,15> tv{};tv[2]=reinterpret_cast<void*>(release);tv[10]=reinterpret_cast<void*>(type);tv[13]=reinterpret_cast<void*>(levels);tv[14]=reinterpret_cast<void*>(desc);
    texture a{tv.data()},b{tv.data()},foreign{tv.data()};tex0=&a;tex1=&b;
    std::array<void*,80> dv{};dv[7]=reinterpret_cast<void*>(caps);dv[38]=reinterpret_cast<void*>(transform);dv[60]=reinterpret_cast<void*>(bound);dv[62]=reinterpret_cast<void*>(get);dv[63]=reinterpret_cast<void*>(set);dv[76]=reinterpret_cast<void*>(set_vertex_shader_trap);dv[77]=reinterpret_cast<void*>(shader);
    dv[70]=reinterpret_cast<void*>(dp);dv[71]=reinterpret_cast<void*>(dip);dv[72]=reinterpret_cast<void*>(up);dv[73]=reinterpret_cast<void*>(iup);
    const auto raw_vtable=dv;
    device d{dv.data()};active_device=&d;
    for(auto& sampler:d.samplers) {sampler[16]=2;sampler[17]=2;sampler[18]=2;sampler[21]=1;}
    d.textures[0]=&a;d.textures[1]=&b;
    field<void*>(context.data(),0x144)=&d;field<void*>(context.data(),0x330)=table.data();field<unsigned>(context.data(),0x490)=2;
    field<unsigned short>(table.data(),0)=4;field<byte*>(table.data(),0x10)=records.data();
    for(unsigned i=0;i<2;++i) {
        field<int>(context.data(),0x334+4*i)=int(i+1);field<byte>(context.data(),0x354+i)=1;field<int>(context.data(),0x384+4*i)=int(i);
        auto* r=records.data()+(i+1)*0x6c;r[4]=5;r[5]=1;field<void*>(r,0x40)=i?&b:&a;
    }
    selected.enabled=true;selected.anisotropy=16;
    named(0x10001,"\\\\ravendata1\\xmen\\Assets\\tilesets\\haarp\\floor.png");
    // A name past the old 260-byte probe length remains a full C-string identity.
    named(0x10002,"\\\\ravendata1\\xmen\\Art\\Models\\Actors\\"+std::string(280,'a')+"/skin.png");
    assert(sources().find(reinterpret_cast<std::uintptr_t>(context.data()),0x10002)->source.path().size()>260);
    device_ready(&d,1);assert(devices().at(&d).state.amount==8);
    const auto native=d.samplers;d.expect0=d.expect1=true;
    {draw_scope scope({&d,reinterpret_cast<std::uintptr_t>(context.data()),pass::world_material});
        for(unsigned slot=70;slot<=73;++slot) {submit(slot);assert(d.samplers==native && a.refs==0 && b.refs==0);}
    }
    d.expect0=d.expect1=false;const auto sets=d.sets;submit(70);assert(d.sets==sets);
    d.expect0=d.expect1=true;
    nonindexed(context.data(),nullptr,4,17,9);indexed(context.data(),nullptr,4,17,9,2,3);
    assert(d.samplers==native);
    auto run=[&] {nonindexed(context.data(),nullptr,4,17,9);assert(d.samplers==native && !a.refs && !b.refs);};
    d.expect0=d.expect1=false;d.ortho=true;const auto prior=d.sets;run();assert(d.sets==prior);
    d.ortho=false;d.rhw=true;run();assert(d.sets==prior);d.rhw=false;
    d.unknown_shader=0x80000002;run();assert(d.sets==prior);d.unknown_shader=0;
    for(const auto* path:{"","textures/ui/lch.png","assets/tilesets/haarp/font.png","assets/tilesets/haarp/floor_atlas.png"}) {
        named(0x10001,path);d.expect0=false;d.expect1=true;run();
    }
    named(0x10001,"assets/tilesets/haarp/floor.png");
    // Authored and actual POINT minification only change with the second opt-in.
    records[0x6c+4]=3;d.samplers[0][17]=1;
    auto point_native=d.samplers;d.expect0=false;d.expect1=true;
    nonindexed(context.data(),nullptr,4,17,9);assert(d.samplers==point_native);
    selected.anisotropy_on_point_art=true;device_ready(&d,2);d.expect0=true;
    nonindexed(context.data(),nullptr,4,17,9);assert(d.samplers==point_native);
    assert(d.samplers[0][16]==2 && d.samplers[0][18]==2);
    selected.anisotropy_on_point_art=false;records[0x6c+4]=5;d.samplers=native;device_ready(&d,3);
    // Exact current binding (+0x40 vs GetTexture), type and mip checks stay live.
    d.textures[0]=&foreign;d.expect0=false;run();assert(devices().at(&d).state.rejected_stage[0]);
    d.textures[0]=&a;device_ready(&d,4);d.expect0=true;run();
    // Cube resources use the record's separate cube slot, so the ordinary 2D
    // +40 pointer can be null. Exclude type before treating that as a bad bind.
    foreign.kind=5;field<void*>(records.data()+0x6c,0x40)=nullptr;field<void*>(records.data()+0x6c,0x44)=&foreign;d.textures[0]=&foreign;d.expect0=false;run();
    assert(!devices().at(&d).state.rejected_stage[0]);
    field<void*>(records.data()+0x6c,0x40)=&a;field<void*>(records.data()+0x6c,0x44)=nullptr;d.textures[0]=&a;d.expect0=true;run();
    a.rt=true;d.expect0=false;run();a.rt=false;d.expect0=true;run();
    a.mip_levels=1;d.expect0=false;run();a.mip_levels=2;d.expect0=true;run();
    // Invalid/duplicate physical maps refuse the whole draw before sampler writes.
    field<int>(context.data(),0x384+4)=0;d.expect0=d.expect1=false;const auto mapped_sets=d.sets;run();assert(d.sets==mapped_sets);
    field<int>(context.data(),0x384+4)=1;field<unsigned>(context.data(),0x490)=9;run();assert(d.sets==mapped_sets);field<unsigned>(context.data(),0x490)=2;
    // Reuse of the low handle index cannot inherit the previous source identity.
    return_handle=0x20001;assert(create_texture(context.data(),nullptr,1,1,1,1,0,0,0,0)==0x20001);
    d.expect0=false;d.expect1=true;run();
    assert(!sources().find(reinterpret_cast<std::uintptr_t>(context.data()),0x10001));
    named(0x20001,"assets/tilesets/haarp/floor.png");d.expect0=true;run();
    source_ok=false;std::array<byte,0x60> image{};void* images[]{image.data()};
    assert(!set_source(context.data(),nullptr,0x20001,images,0));d.expect0=false;run();source_ok=true;
    named(0x20001,"assets/tilesets/haarp/floor.png");delete_texture(context.data(),nullptr,0x20001);run();
    named(0x30001,"assets/tilesets/haarp/floor.png");d.expect0=d.expect1=false;
    device_unavailable(&d);const auto lost_sets=d.sets;run();assert(d.sets==lost_sets);
    device_recovered(&d);d.expect0=d.expect1=true;run();
    // Reentry through a raw owner restores first and disables this generation.
    d.reenter=true;run();assert(devices().at(&d).state.disabled);d.expect0=d.expect1=false;run();
    device_ready(&d,6);d.expect0=d.expect1=true;run();
    // A distinct device class receives its own originals; no recursive hook chain.
    auto second_vtable=raw_vtable;device second{second_vtable.data()};second.samplers=native;second.textures={&a,&b};second.expect0=second.expect1=true;
    device_ready(&second,7);assert(classes().size()==2 && classes().at(second_vtable.data()).original[0]==raw_vtable[70]);
    active_device=&second;field<void*>(context.data(),0x144)=&second;
    nonindexed(context.data(),nullptr,4,17,9);assert(second.samplers==native);
    std::cout<<"PASS AF runtime: relocated engine guards/C-string lifetime/full handles/four draws/binding/2D exclusions/POINT opt-in/restore/loss/reentry/two classes\n";
}
