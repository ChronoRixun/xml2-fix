#ifdef REMASTERED_UI_BASELINE_SOURCE
#include REMASTERED_UI_BASELINE_SOURCE
#else
#include "../src/remastered_ui.cpp"
#endif
#include <cassert>
#include <bit>
#include <fstream>
#include <iostream>
#include <vector>

namespace {bool requested=true;}
namespace ini {
std::optional<std::string> text(const wchar_t* section,const wchar_t* key) {
    if(std::wstring_view(section)==L"Remastered" && (std::wstring_view(key)==L"Enabled" || std::wstring_view(key)==L"CenteredHud")) return requested?"1":"0";
    return {};
}
}
namespace logger {void write(const char*,...) {} }
namespace {
unsigned flags_before=0,flags_after=0,seen_eax=0,seen_ecx=0,seen_edx=0,seen_esi=0;
float remainder=0,stack_sentinel=3.25f,retail_left=-85.333335f;
void* builder_window=nullptr;
__declspec(naked) void resume_builder() {__asm {ret}}
void builder_abi(void* panel) {
    builder_window=panel;
    remastered_ui::builder_cell=remastered_ui::addr(&builder_window);
    remastered_ui::builder_resume=remastered_ui::addr(resume_builder);
    __asm {
        push esi
        fld dword ptr [stack_sentinel]
        fld dword ptr [retail_left]
        mov eax,panel
        mov ecx,11223344h
        mov edx,22334455h
        mov esi,33445566h
        stc
        pushfd
        pop dword ptr [flags_before]
        call remastered_ui::builder_stub
        pushfd
        pop dword ptr [flags_after]
        mov seen_eax,eax
        mov seen_ecx,ecx
        mov seen_edx,edx
        mov seen_esi,esi
        fstp dword ptr [remainder]
        pop esi
    }
    assert(seen_eax==remastered_ui::addr(panel) && seen_ecx==remastered_ui::addr(panel));
    assert(seen_edx==0x22334455 && seen_esi==0x33445566 && flags_before==flags_after && remainder==stack_sentinel);
}
void __fastcall no_rebuild(void*,void*) {}
void __fastcall widget_event(void*,void*,int) {}
void map(const char* path) {
    std::ifstream in(path,std::ios::binary);std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),{});assert(data.size()>0x1000);
    auto get=[&](unsigned off){unsigned n;std::memcpy(&n,data.data()+off,4);return n;};
    auto* pe=data.data()+get(0x3c);auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(pe);
    auto* mem=reinterpret_cast<unsigned char*>(0x400000);
    DWORD old=0;assert(VirtualProtect(mem+0x1000,0x700000,PAGE_EXECUTE_READWRITE,&old));
    auto* sections=IMAGE_FIRST_SECTION(nt);
    for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i) std::memcpy(mem+sections[i].VirtualAddress,data.data()+sections[i].PointerToRawData,sections[i].SizeOfRawData);
}
void set_dimensions(unsigned w,unsigned h) {*reinterpret_cast<unsigned*>(0xa09ffc)=w;*reinterpret_cast<unsigned*>(0xa0a000)=h;}
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    assert(argc>=2);const std::string mode=argc>2?argv[2]:"all";
    map(argv[1]);requested=mode!="off";
    std::vector<unsigned char> before(reinterpret_cast<unsigned char*>(0x400000),reinterpret_cast<unsigned char*>(0xa70000));
    const unsigned sites[]{0x621ca0,0x624520,0x621ea0,0x61dcab,0x61ff70,0x6a5804,0x625df0,0x61ed69,0x6260ac,0x626105,0x62611c,0x621d07,0x621d19,0x61fe7b,0x6a3be4,0x6a3be8};
    if(mode.starts_with("guard")) *reinterpret_cast<unsigned char*>(sites[std::stoul(mode.substr(5))])^=1;
    remastered_ui::install(reinterpret_cast<HMODULE>(0x400000));
    if(mode=="off" || mode.starts_with("guard")) {
        assert(!remastered_ui::installed);
        for(unsigned i=0;i<4;++i) if(!mode.starts_with("guard") || sites[i]!=sites[std::stoul(mode.substr(5))])
            assert(std::memcmp(reinterpret_cast<void*>(sites[i]),before.data()+sites[i]-0x400000,5)==0);
        std::cout<<"PASS remastered UI "<<mode<<": no partial writes\n";return 0;
    }
    assert(remastered_ui::installed);
    for(const unsigned site:{0x5eecf0u,0x5ef2e0u,0x597c90u,0x5971a0u,0x597200u})
        assert(!std::memcmp(reinterpret_cast<void*>(site),before.data()+site-0x400000,16));
    using namespace remastered_ui;
    std::array<unsigned char,0x100> panel{};
    set_dimensions(1920,1080);builder_abi(panel.data());
    assert(field<float>(panel.data(),0x38)==2.25f);
    if(mode!="raster-control") assert(std::trunc(-double(field<float>(panel.data(),0x40))*2.25)==240);
    set_dimensions(640,480);field<float>(panel.data(),0x38)=field<float>(panel.data(),0x3c)=1;
    retail_left=-0.0f;const auto original_mxcsr=_mm_getcsr();_mm_setcsr(original_mxcsr|0x20);
    builder_abi(panel.data());assert(_mm_getcsr()==(original_mxcsr|0x20));_mm_setcsr(original_mxcsr);
    assert(std::bit_cast<unsigned>(field<float>(panel.data(),0x40))==0x80000000u);
    for(const auto [w,h]:{std::pair{1024u,768u},{2048u,1536u}}) {
        set_dimensions(w,h);field<float>(panel.data(),0x38)=float(w)*(*at<float*>(0x6a3be8));field<float>(panel.data(),0x3c)=float(h)*(*at<float*>(0x6a3be4));field<float>(panel.data(),0x40)=0;
        const auto original=panel;apply_panel(panel.data());refresh(panel.data());assert(panel==original);
    }
    for(const auto [w,h]:{std::pair{640u,480u},{1280u,960u},{1280u,800u},{1920u,1080u},{2520u,1080u},{3840u,1080u}}) {
        set_dimensions(w,h);field<float>(panel.data(),0x38)=float(w)/640;field<float>(panel.data(),0x3c)=float(h)/480;field<float>(panel.data(),0x40)=0;
        const auto old=panel;
        apply_panel(panel.data());
        if(std::uint64_t(w)*480==std::uint64_t(h)*640) assert(panel==old);
        else {
            const auto t=centered_ui::make(w,h,1);const auto f=centered_ui::panel_fields(*t);
            if(mode!="raster-control") assert(field<float>(panel.data(),0x38)==f.sx && field<float>(panel.data(),0x3c)==f.sy && field<float>(panel.data(),0x40)==f.left);
        }
    }
    // Real retail move/rebuild arithmetic, with only GPU rebuild replaced by a no-op.
    std::array<unsigned char,0x250> item{},cycle{};
    std::array<unsigned char,0x600> list{};
    std::array<void*,20> vt{},cv{};vt[1]=cv[1]=reinterpret_cast<void*>(no_rebuild);
    vt[0]=cv[0]=reinterpret_cast<void*>(widget_event);
    vt[6]=reinterpret_cast<void*>(0x61ff70);cv[6]=reinterpret_cast<void*>(0x621810);
    *reinterpret_cast<void***>(item.data())=vt.data();*reinterpret_cast<void***>(cycle.data())=cv.data();
    auto** list_vt=reinterpret_cast<void**>(0x6a57ec);list_vt[1]=reinterpret_cast<void*>(no_rebuild);
    *reinterpret_cast<void***>(list.data())=list_vt;
    std::array<void*,3> children{item.data(),cycle.data(),list.data()};
    field<void**>(panel.data(),0x14)=children.data();field<int>(panel.data(),0x24)=3;field<int>(panel.data(),0x2c)=100;
    for(void* child:children) {
        field<void*>(child,0x2c)=panel.data();field<int>(child,0x14)=33;field<int>(child,0x18)=146;field<int>(child,0x1c)=196;field<int>(child,0x20)=18;
    }
    field<int>(cycle.data(),0x1f4)=177;field<int>(cycle.data(),0x1f8)=146;field<int>(cycle.data(),0x1fc)=40;field<int>(cycle.data(),0x200)=18;
    *reinterpret_cast<void**>(0x67f230)=reinterpret_cast<void*>(PtInRect); // pure geometry API, no desktop input
    for(const auto [w,h]:{std::pair{640u,480u},{1280u,960u},{1280u,800u},{1920u,1080u},{2520u,1080u},{3440u,1440u},{3840u,1080u},{1365u,768u},{1280u,960u}}) {
        set_dimensions(w,h);refresh(panel.data());
        const auto t=centered_ui::make(w,h,1);
        for(void* child:children) assert(std::abs(field<int>(child,4)-(t->ox+33*t->s))<1.01);
        assert(std::abs(field<int>(cycle.data(),0x1e4)-(t->ox+177*t->s))<1.01);
        assert(field<int>(list.data(),0x50c)==int(std::trunc(20*t->s)));
        assert(field<int>(list.data(),0x510)==int(std::trunc(16*t->s)));
        assert(field<int>(list.data(),0x514)==int(std::trunc(2*t->s)));
        const int left=field<int>(item.data(),4),top=field<int>(item.data(),8);
        const int right=field<int>(item.data(),0xc),bottom=field<int>(item.data(),0x10);
        const auto input=reinterpret_cast<bool(__fastcall*)(void*,void*,unsigned,unsigned,unsigned)>(0x6240e0);
        auto hit=[&](int x,int y) {
            const unsigned relative=unsigned(static_cast<unsigned short>(x-left))|(unsigned(static_cast<unsigned short>(y-top))<<16);
            assert(input(item.data(),nullptr,WM_MOUSEMOVE,0,relative));
            return field<unsigned char>(item.data(),0x1b0)!=0;
        };
        assert(hit(left,top) && hit(right-1,bottom-1));
        assert(!hit(left-1,top) && !hit(right,top) && !hit(left,bottom));
        assert(!hit(0,top) && !hit(int(w)-1,top));
        // Exercise each retail Beenox widget family's actual move override.
        // Only GPU rebuilding and event notification are replaced; authored
        // position/size, panel fields and native integer raster math are real.
        for(const unsigned table:{0x6a543cu,0x6a548cu,0x6a54dcu,0x6a552cu,
                0x6a557cu,0x6a55e4u,0x6a5634u,0x6a5684u,0x6a56d4u,
                0x6a5724u,0x6a5774u,0x6a57ecu}) {
            std::array<unsigned char,0x600> widget{};
            std::array<void*,18> native_vt{};
            std::memcpy(native_vt.data(),reinterpret_cast<void*>(table),sizeof(native_vt));
            native_vt[0]=reinterpret_cast<void*>(widget_event);
            native_vt[1]=reinterpret_cast<void*>(no_rebuild);
            *reinterpret_cast<void***>(widget.data())=native_vt.data();
            field<void*>(widget.data(),0x2c)=panel.data();
            field<int>(widget.data(),0x14)=33;field<int>(widget.data(),0x18)=146;
            field<int>(widget.data(),0x1c)=196;field<int>(widget.data(),0x20)=18;
            // Cycle has an additional option rectangle owned by its move.
            field<int>(widget.data(),0x1f4)=177;field<int>(widget.data(),0x1f8)=146;
            field<int>(widget.data(),0x1fc)=40;field<int>(widget.data(),0x200)=18;
            reinterpret_cast<void(__fastcall*)(void*,void*,int,int)>(native_vt[6])(widget.data(),nullptr,0,0);
            assert(std::abs(field<int>(widget.data(),4)-(t->ox+33*t->s))<1.01);
            assert(std::abs(field<int>(widget.data(),8)-146*t->s)<1.01);
            const int edge=field<int>(widget.data(),0xc);
            const int base=field<int>(widget.data(),0x10);
            const auto local_hit=[&](int x,int y) {
                const unsigned relative=unsigned(static_cast<unsigned short>(x-field<int>(widget.data(),4)))|
                    (unsigned(static_cast<unsigned short>(y-field<int>(widget.data(),8)))<<16);
                assert(input(widget.data(),nullptr,WM_MOUSEMOVE,0,relative));
                return field<unsigned char>(widget.data(),0x1b0)!=0;
            };
            assert(local_hit(edge-1,base-1));
            assert(!local_hit(edge,base-1) && !local_hit(edge-1,base));
        }
        // Execute the native slider's inverse/quantization at real pixel widths.
        const auto slider=reinterpret_cast<bool(__fastcall*)(void*,void*,unsigned,unsigned,unsigned)>(0x622f70);
        const int width=right-left+1;
        field<float>(item.data(),0x1b8)=1;field<float>(item.data(),0x1c8)=.001f;field<float>(item.data(),0x1c4)=.001f;
        for(double fraction:{0.,.25,.5,.75,1.}) {
            field<float>(item.data(),0x1b4)=.1f;
            const int relative=int(std::round(width*fraction));
            assert(slider(item.data(),nullptr,WM_LBUTTONDOWN,0,unsigned(relative)));
            assert(std::abs(field<float>(item.data(),0x1b4)-double(relative)/width)<.0011);
        }
        // Native keyboard clamping at both slider endpoints. There is no
        // desktop input: the owned routine receives synthetic messages.
        field<float>(item.data(),0x1b4)=0;
        assert(slider(item.data(),nullptr,WM_KEYUP,VK_LEFT,0));
        assert(field<float>(item.data(),0x1b4)==0);
        field<float>(item.data(),0x1b4)=1;
        assert(slider(item.data(),nullptr,WM_KEYUP,VK_RIGHT,0));
        assert(field<float>(item.data(),0x1b4)==1);
        // The native scrollbar uses the transformed pixel height once. A
        // synthetic thumb drag reaches 0 and 1-page at every tested aspect.
        std::array<unsigned char,0x250> scroll{};
        *reinterpret_cast<void***>(scroll.data())=vt.data();
        field<void*>(scroll.data(),0x2c)=panel.data();
        field<int>(scroll.data(),0x14)=33;field<int>(scroll.data(),0x18)=146;
        field<int>(scroll.data(),0x1c)=18;field<int>(scroll.data(),0x20)=196;
        reinterpret_cast<void(__fastcall*)(void*,void*,int,int)>(vt[6])(scroll.data(),nullptr,0,0);
        field<float>(scroll.data(),0x1c0)=.2f;
        field<int>(scroll.data(),0x1cc)=int(8*t->s);
        field<int>(scroll.data(),0x1dc)=0;
        const auto scrollbar=reinterpret_cast<bool(__fastcall*)(void*,void*,unsigned,unsigned,unsigned)>(0x6234c0);
        const int height=field<int>(scroll.data(),0x10)-field<int>(scroll.data(),8)+1;
        for(const int y:{0,height-2}) {
            field<float>(scroll.data(),0x1bc)=.4f;
            assert(scrollbar(scroll.data(),nullptr,WM_MOUSEMOVE,MK_LBUTTON,1u|(unsigned(static_cast<unsigned short>(y))<<16)));
            assert(std::abs(field<float>(scroll.data(),0x1bc)-(y==0?0.f:.8f))<.00001f);
        }
        // Its public native position setter clamps out-of-range values too.
        const auto scroll_position=reinterpret_cast<void(__fastcall*)(void*,void*,float)>(0x6212e0);
        scroll_position(scroll.data(),nullptr,-1.f);assert(field<float>(scroll.data(),0x1bc)==0);
        scroll_position(scroll.data(),nullptr,2.f);assert(std::abs(field<float>(scroll.data(),0x1bc)-.8f)<.00001f);
        // Check exact integer raster boundaries, not merely subpixel closeness:
        // a rounded-down panel origin or height scale shifts these by one pixel.
        for(int logical:{0,2,3,4,8,16,33,177,320,479}) {
            field<int>(item.data(),0x14)=field<int>(item.data(),0x18)=logical;
            reinterpret_cast<void(__fastcall*)(void*,void*,int,int)>(vt[6])(item.data(),nullptr,0,0);
            const auto expected_x=(std::uint64_t(w)*480-std::uint64_t(h)*640+2*std::uint64_t(h)*logical)/960;
            const auto expected_y=std::uint64_t(h)*logical/480;
            assert(field<int>(item.data(),4)==int(expected_x));
            assert(field<int>(item.data(),8)==int(expected_y));
        }
        field<int>(item.data(),0x14)=33;field<int>(item.data(),0x18)=146;
    }
    std::cout<<"PASS remastered UI: guards/aspects/4:3 identity/12 widget-family moves and hit edges/slider and scrollbar endpoints/ControlList/reset/builder ABI/dialogue untouched\n";
    return 0;
}
