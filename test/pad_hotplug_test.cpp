// Execute owned retail binding remap, device scan and frame reads against fake
// COM devices. No game process, hardware input, registry or game data writes.
#include <Windows.h>
#define DIRECTINPUT_VERSION 0x0800
#include <dinput.h>
#include "pad_hotplug.hpp"
#include "pad_hotplug_rules.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>
#include <cstdarg>
#include <string>
#include <filesystem>

namespace logger
{
    const std::filesystem::path& module_dir()
    {
        static const auto path = std::filesystem::current_path();
        return path;
    }
    std::vector<std::string> lines;
    void write(const char* format, ...)
    {
        char line[2048]{};va_list args;va_start(args,format);
        std::vsnprintf(line,sizeof(line),format,args);va_end(args);lines.emplace_back(line);
    }
}
namespace game_version { void refuse_startup() { std::abort(); } }
#ifndef PAD_HOTPLUG_LIVE_FIXTURE
namespace gamepad_fix { void update_attached(const GUID*, unsigned) {} }
#endif
extern "C" unsigned char fixture_space[];
namespace
{
    unsigned checks = 0;
    void check(bool ok, const char* what)
    {
        ++checks;
        if (!ok) { std::fprintf(stderr, "FAIL %u: %s\n", checks, what); std::exit(1); }
    }
    bool configure_latepads(int argc, char** argv)
    {
        check(argc==2 || (argc==3 && (std::strcmp(argv[2],"0")==0 || std::strcmp(argv[2],"1")==0)), "LatePads fixture mode absent/1/0");
        std::ofstream config(logger::module_dir()/"xml2-fix.ini", std::ios::trunc);
        config << "[Test]\nNoActivate=1\n[Input]\n";
        if(argc==3) config << "LatePads=" << argv[2] << "\n";
        config.close();check(bool(config),"fixture INI written for production reader");
        return argc!=3 || std::strcmp(argv[2],"0")!=0;
    }
    unsigned char* at(unsigned va) { return reinterpret_cast<unsigned char*>(va); }
    template<class T> T& field(void* p, unsigned offset) { return *reinterpret_cast<T*>(static_cast<unsigned char*>(p) + offset); }
    std::vector<unsigned char> image;
    void load(const char* file)
    {
        std::ifstream f(file, std::ios::binary);
        std::vector<unsigned char> raw((std::istreambuf_iterator<char>(f)), {});
        check(raw.size() > sizeof(IMAGE_DOS_HEADER), "retail executable readable");
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
        check(dos->e_magic == IMAGE_DOS_SIGNATURE && unsigned(dos->e_lfanew) < raw.size()-sizeof(IMAGE_NT_HEADERS32), "PE bounds");
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data() + dos->e_lfanew);
        check(nt->Signature == IMAGE_NT_SIGNATURE && nt->OptionalHeader.ImageBase == 0x400000 && nt->OptionalHeader.SizeOfImage <= 0x700000, "retail image range");
        image.resize(nt->OptionalHeader.SizeOfImage);
        for (unsigned i=0; i<nt->FileHeader.NumberOfSections; ++i)
        {
            const auto& s = IMAGE_FIRST_SECTION(nt)[i];
            const auto size = s.Misc.VirtualSize && s.Misc.VirtualSize < s.SizeOfRawData ? s.Misc.VirtualSize : s.SizeOfRawData;
            check(s.PointerToRawData + size <= raw.size() && s.VirtualAddress+size <= image.size(), "section bounds");
            std::memcpy(image.data()+s.VirtualAddress, raw.data()+s.PointerToRawData, size);
        }
        DWORD old;
        check(VirtualProtect(at(0x401000), 0x6ff000, PAGE_EXECUTE_READWRITE, &old) != 0, "owned fixture writable");
        std::memcpy(at(0x401000), image.data()+0x1000, image.size()-0x1000);
    }
    void restore(unsigned va, size_t size) { std::memcpy(at(va), image.data()+va-0x400000, size); }
    void jump(unsigned va, const void* target)
    {
        at(va)[0]=0xe9;
        const auto rel=unsigned(reinterpret_cast<std::uintptr_t>(target))-va-5;
        std::memcpy(at(va)+1, &rel, 4);
    }
    void* __cdecl copy(void* dst, const void* src, size_t n) { return std::memcpy(dst,src,n); }
    unsigned mapping_entry=0x6184fc, read_entry=0x6287d0;
    void* read_manager=nullptr;
    // Authored caller frames; retail code itself is supplied by the owned PE.
    __declspec(naked) void remap()
    {
        __asm {
            pushad
            sub esp, 40h
            call dword ptr [mapping_entry]
            add esp, 40h
            popad
            ret
        }
    }
    __declspec(naked) void read_frame()
    {
        __asm {
            pushad
            sub esp, 40h
            mov eax, read_manager
            mov dword ptr [esp+0ch], eax
            call dword ptr [read_entry]
            add esp, 40h
            popad
            ret
        }
    }
    struct pad { void** table; unsigned id; unsigned refs=0, reads=0, formats=0; bool pressed=false; };
    struct input { void** table; std::vector<unsigned> attached; unsigned creates=0; };
    GUID guid(unsigned id) { GUID g{}; g.Data1=id; return g; }
    std::array<pad, 4> pads{};
    HRESULT STDMETHODCALLTYPE create(void* self, REFGUID id, void** out, void*)
    {
        auto& di=*static_cast<input*>(self); ++di.creates;
        if (id.Data1<1 || id.Data1>4) return DIERR_DEVICENOTREG;
        auto& p=pads[id.Data1-1]; ++p.refs; *out=&p; return DI_OK;
    }
    ULONG STDMETHODCALLTYPE release(void* self) { return --static_cast<pad*>(self)->refs; }
    HRESULT STDMETHODCALLTYPE enumerate(void* self, DWORD, LPDIENUMDEVICESCALLBACKA cb, void* user, DWORD)
    {
        for (auto id : static_cast<input*>(self)->attached)
        {
            DIDEVICEINSTANCEA item{}; item.dwSize=sizeof(item); item.guidInstance=guid(id);
            item.guidProduct.Data1=0xc216046d; item.dwDevType=4;
            std::snprintf(item.tszInstanceName, MAX_PATH, "Invented pad %u", id);
            if (cb(&item,user)==DIENUM_STOP) break;
        }
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE format(void* self, const DIDATAFORMAT*) { ++static_cast<pad*>(self)->formats; return DI_OK; }
    HRESULT STDMETHODCALLTYPE coop(void*, HWND, DWORD) { return DI_OK; }
    HRESULT STDMETHODCALLTYPE objects(void*, void*, void*, DWORD) { return DI_OK; }
    HRESULT STDMETHODCALLTYPE poll(void*) { return DI_OK; }
    HRESULT STDMETHODCALLTYPE state(void* self, DWORD size, void* out)
    {
        auto& p=*static_cast<pad*>(self); ++p.reads;
        std::memset(out,0,size);
        static_cast<DIJOYSTATE2*>(out)->rgbButtons[0]=p.pressed ? 0x80 : 0;
        return DI_OK;
    }
    void* pad_table[32]{};
    void* input_table[10]{};
    input di{input_table};
    std::array<unsigned char, 0x13000> manager{};
    void reset_manager()
    {
        manager.fill(0); di.attached.clear(); di.creates=0;
        for (unsigned i=0;i<4;++i) pads[i]={pad_table,i+1};
        field<void*>(manager.data(),0x27e4)=&di;
        // Prevent keyboard/mouse creation; this fixture owns only the pads.
        field<void*>(manager.data(),4)=&pads[0]; field<void*>(manager.data(),8)=&pads[0];
        read_manager=manager.data();
    }
    void scan()
    {
        // The real recurring worker uses rebuild=1 (618CF8), as does Controls.
        using fn=void(__thiscall*)(void*,bool);
        reinterpret_cast<fn>(0x628e20)(manager.data(),true);
    }
    pad* device(unsigned i) { return field<pad*>(manager.data(),0xc+i*4); }
    void seed(const std::array<unsigned,4>& saved, const std::array<unsigned,4>& current)
    {
        for (unsigned i=0;i<4;++i) for (unsigned which=0;which<2;++which)
        {
            auto* text=reinterpret_cast<char*>(at((which ? 0xa6ac08 : 0xa6aca0)+i*0x25));
            const unsigned id=which ? current[i] : saved[i];
            std::snprintf(text,0x25,"%08x-0000-0000-0000-000000000000",id);
        }
        for (unsigned a=0;a<42;++a) for (unsigned p=0;p<4;++p)
        {
            auto* slots=reinterpret_cast<unsigned*>(at(0xa68f98+a*0x94+0x30+p*0x14));
            slots[0]=((p+3)<<16)|(21+a%12); slots[1]=0x10011; // pad buttons and keyboard
        }
        remap();
    }
    unsigned binding(unsigned p,unsigned a=0) { return *reinterpret_cast<unsigned*>(at(0xa68f98+a*0x94+0x30+p*0x14)); }
    void bindings(const std::array<unsigned,4>& ordinals)
    {
        for (unsigned a=0;a<42;++a) for (unsigned p=0;p<4;++p)
        {
            check(binding(p,a)==(((ordinals[p]+3)<<16)|(21+a%12)),"pad control retained with unique mapped device");
            check(*reinterpret_cast<unsigned*>(at(0xa68f98+a*0x94+0x34+p*0x14))==0x10011,"keyboard unchanged");
        }
    }
    bool pressed(unsigned player)
    {
        const auto b=binding(player);
        if ((b&0xffff)!=21 || (b>>16)<3 || (b>>16)>=13) return false;
        return manager[0x4f0+((b>>16)-3)*0x110+DIJOFS_BUTTON(0)]==0x80;
    }
}
int main(int argc,char** argv)
{
    if (argc<2) { std::fprintf(stderr,"usage: xml2_pad_hotplug_test <owned retail exe> [1|0]\n"); return 2; }
    const bool on=configure_latepads(argc,argv);
    check(fixture_space==at(0x401000),"fixed owned fixture address");
    load(argv[1]);
    if (!on)
    {
        const std::vector<unsigned char> before(at(0x400000),at(0x400000)+image.size());
        pad_hotplug::install(reinterpret_cast<HMODULE>(0x400000));
        pad_hotplug::on_keyboard_read(reinterpret_cast<void*>(1),256);
        check(std::memcmp(at(0x400000),before.data(),before.size())==0,"LatePads=0 leaves entire mapped retail image identical, bridge and globals included");
        std::printf("pad hotplug OFF: %u checks PASS (no bridge or image writes)\n",checks);
        return 0;
    }
    // Guard signatures are compared to the owned executable, including every
    // dependency; each mutated byte must refuse without writing the hook site.
    for (const auto& g:pad_hotplug_rules::guards) for(size_t i=0;i<g.hex.size()/2;++i)
    {
        restore(0x61862a,6); at(g.va)[i]^=1;
        const std::vector<unsigned char> before(at(0x61862a),at(0x618630));
        pad_hotplug::install(reinterpret_cast<HMODULE>(0x400000));
        check(std::memcmp(at(0x61862a),before.data(),6)==0,"guard mutation refuses hook");
        restore(g.va,g.hex.size()/2);
    }
    pad_hotplug::install(reinterpret_cast<HMODULE>(0x500000));
    check(at(0x61862a)[0]==0x39,"wrong base leaves retail intact");
    // Limit execution to the actual mapping block and actual ten-pad read loop.
    at(0x618844)[0]=0xc3; at(0x62887d)[0]=0xc3;
    jump(0x672170,reinterpret_cast<const void*>(&copy));
    pad_table[2]=reinterpret_cast<void*>(&release); pad_table[4]=reinterpret_cast<void*>(&objects);
    pad_table[7]=reinterpret_cast<void*>(&poll); pad_table[9]=reinterpret_cast<void*>(&state);
    pad_table[11]=reinterpret_cast<void*>(&format); pad_table[13]=reinterpret_cast<void*>(&coop); pad_table[25]=reinterpret_cast<void*>(&poll);
    input_table[3]=reinterpret_cast<void*>(&create); input_table[4]=reinterpret_cast<void*>(&enumerate);
    // Preserve the separate Advanced Options regression: accepting that panel
    // with a missing saved pad can erase its actions. This is not startup code;
    // the real separate-manager startup failure is in pad_hotplug_live_test.
    seed({1,0,0,0},{0,0,0,0});
    check(binding(0)==0,"stock Options remap erases absent saved pad binding");
    reset_manager(); scan(); di.attached={1}; scan();
    check(device(0)==&pads[0] && pads[0].formats==1,"stock late device does get created/configured");
    pads[0].pressed=true; read_frame();
    check(pads[0].reads==1 && !pressed(0),"stock reads late device but Options erased player 1's binding");
    pad_hotplug::install(reinterpret_cast<HMODULE>(0x400000));
    check(at(0x61862a)[0]==0xe9,"production installer applied");
    // Present at launch, normal order; repeat scan must reuse the device.
    seed({1,2,0,0},{1,2,0,0}); bindings({0,1,2,3});
    reset_manager(); di.attached={1,2}; scan(); scan();
    check(di.creates==2 && device(0)==&pads[0] && device(1)==&pads[1],"startup two-pad reuse");
    pads[0].pressed=true; read_frame(); check(pressed(0) && !pressed(1),"present player 1 routes alone");
    pads[0].pressed=false; pads[1].pressed=true; read_frame(); check(!pressed(0) && pressed(1),"present player 2 routes alone");
    // Off at launch, then on. Missing saved instance GUIDs retain all actions.
    seed({1,2,3,4},{0,0,0,0}); bindings({0,1,2,3});
    reset_manager(); scan(); check(!device(0),"empty startup");
    di.attached={1}; scan(); pads[0].pressed=true; read_frame();
    check(pressed(0) && !pressed(1),"late first pad reaches player 1");
    // Second (and later) arrival never steals the first player's controls.
    di.attached={1,2}; scan(); pads[0].pressed=false; pads[1].pressed=true; read_frame();
    check(!pressed(0) && pressed(1),"late second pad reaches player 2 only");
    di.attached={1,2,3,4}; scan(); pads[1].pressed=false; pads[3].pressed=true; read_frame();
    check(pressed(3) && !pressed(0) && !pressed(1),"fourth late pad routes independently");
    // Remove and re-add: native release/compaction and subsequent scan still run.
    di.attached={1,2,3}; scan(); read_frame(); check(!pressed(3) && pads[3].refs==0,"removed fourth pad idle/released");
    di.attached={1,2,3,4}; scan(); read_frame(); check(pressed(3) && pads[3].refs==1,"re-added pad works");
    reset_manager(); di.attached={1}; scan(); pads[0].pressed=true; read_frame(); check(pressed(0),"first pad before removal");
    di.attached.clear(); scan(); read_frame(); check(!pressed(0) && pads[0].refs==0,"removed first pad idle");
    di.attached={1}; scan(); read_frame(); check(pressed(0) && pads[0].refs==1,"re-added first pad works");
    // Saved GUID matches survive a changed enumeration order. An absent first
    // pad cannot collide with the known second pad already occupying device 3.
    seed({1,2,3,4},{2,1,0,0}); bindings({1,0,2,3});
    seed({1,2,3,4},{2,0,0,0}); bindings({1,0,2,3});
    reset_manager(); di.attached={2}; scan(); pads[1].pressed=true; read_frame(); check(!pressed(0) && pressed(1),"known pad 2 keeps its player");
    di.attached={2,1}; scan(); pads[1].pressed=false; pads[0].pressed=true; read_frame(); check(pressed(0) && !pressed(1),"absent pad 1 gets distinct late slot");
    // Stale Bluetooth GUID replaced by a new instance: unmatched current device
    // is available to the missing saved slot instead of erasing its bindings.
    seed({99,0,0,0},{1,0,0,0}); bindings({0,1,2,3});
    seed({0,0,0,0},{1,2,0,0}); bindings({0,1,2,3});
    // All permutations of native matches/sentinels resolve to a permutation,
    // preserving every distinct native match. Includes malformed duplicate input.
    constexpr unsigned values[]{0,1,2,3,666,777,888,UINT32_MAX};
    for(auto a:values) for(auto b:values) for(auto c:values) for(auto d:values)
    {
        const std::array<unsigned,4> src{a,b,c,d}; const auto dst=pad_hotplug_rules::resolve(src);
        unsigned used=0,known=0;
        for(unsigned i=0;i<4;++i)
        {
            check(dst[i]<4 && !(used&(1u<<dst[i])),"unique bounded resolved slot"); used|=1u<<dst[i];
            if(src[i]<4 && !(known&(1u<<src[i]))) { check(dst[i]==src[i],"known match preserved"); known|=1u<<src[i]; }
        }
    }
    std::printf("pad hotplug: %u checks PASS (Advanced Options preservation; native scan/read, 4 players, reconnect, GUID change)\n",checks);
    return 0;
}
