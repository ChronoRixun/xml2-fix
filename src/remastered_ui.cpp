#include "remastered_ui.hpp"
#include "centered_ui_trace.hpp"
#include "patch_transaction.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <array>
#include <cstring>

namespace remastered_ui {
namespace {
using byte=unsigned char;
using address=patch_transaction::address;
byte* image=nullptr;
void* code=nullptr;
bool installed=false;
address builder_resume=0,builder_cell=0;
template<class T> T& field(void* p,unsigned off) {return *reinterpret_cast<T*>(static_cast<byte*>(p)+off);}
template<class T> T at(unsigned va) {return reinterpret_cast<T>(image+va-0x400000);}
address addr(const void* p) {return address(reinterpret_cast<std::uintptr_t>(p));}
struct dimensions {unsigned w,h;};
dimensions size() {return {*at<unsigned*>(0xa09ffc),*at<unsigned*>(0xa0a000)};}
using ctor_t=void*(__fastcall*)(void*,void*,void*,void*);
using draw_t=void(__fastcall*)(void*,void*);
using input_t=int(__fastcall*)(void*,void*,unsigned,unsigned,unsigned);
ctor_t original_ctor=nullptr;
draw_t original_draw=nullptr;
input_t original_input=nullptr;

// No borrowed widget is retained. Called before children exist, or synchronously
// on the UI thread before an event/draw; native animation continues using these fields.
bool apply_panel(void* panel,bool refresh=false) {
    if(!panel) return false;
    const auto d=size();
    if(!d.w || !d.h || d.w>16384 || d.h>16384) return false;
    const bool legacy=std::uint64_t(d.w)*480==std::uint64_t(d.h)*640;
    if(legacy && !refresh) return false;
    const auto t=centered_ui::make(d.w,d.h,1);
    if(!t) return false;
    centered_ui::panel_scale f;
    if(legacy) {
        // Preserve the retail float reciprocal/multiply rounding too. At
        // 1024x768, native sx/sy can differ by one ULP despite exact 4:3.
        f={float(d.w)*(*at<const float*>(0x6a3be8)),float(d.h)*(*at<const float*>(0x6a3be4)),0};
    }
    else if(t->wide_pilot) f=centered_ui::panel_fields(*t);
    else {
        // A reset into an unsupported tall aspect restores the retail formula.
        // It does not extend the centered pilot to vertical letterboxing.
        if(!refresh || field<float>(panel,0x38)!=field<float>(panel,0x3c)) return false;
        f.sx=float(d.w)/640.f;f.sy=float(d.h)/480.f;
        if(panel==*at<void**>(0xa6ad34)) {
            void* display=at<void*(__cdecl*)()>(0x5f6e50)();
            f.left=reinterpret_cast<float(__fastcall*)(void*,void*)>((*static_cast<void***>(display))[11])(display,nullptr);
        }
    }
    if(field<float>(panel,0x38)==f.sx && field<float>(panel,0x3c)==f.sy && field<float>(panel,0x40)==f.left) return false;
    field<float>(panel,0x38)=f.sx;field<float>(panel,0x3c)=f.sy;field<float>(panel,0x40)=f.left;
    return true;
}
void __cdecl builder_panel(void* panel) {centered_ui_trace::preserve_fp fp;apply_panel(panel);}
void* __fastcall construct(void* self,void*,void* a,void* b) {
    void* result=original_ctor(self,nullptr,a,b);
    centered_ui_trace::preserve_fp fp;apply_panel(result);return result;
}
__declspec(naked) void builder_stub() {
    __asm {
        fstp dword ptr [eax+40h]
        pushfd
        pushad
        mov ebx,esp
        and esp,0fffffff0h
        sub esp,200h
        fxsave [esp]
        push eax
        call builder_panel
        add esp,4
        fxrstor [esp]
        mov esp,ebx
        popad
        popfd
        mov ecx,dword ptr [builder_cell]
        mov ecx,dword ptr [ecx]
        jmp dword ptr [builder_resume]
    }
}
void refresh(void* panel) {
    centered_ui_trace::preserve_fp fp;
    // Every native child must still belong to this live window. Validate before
    // changing any field, including the modal window's separately owned controls.
    const int n=field<int>(panel,0x24),capacity=field<int>(panel,0x2c);
    if(n<0 || capacity<0 || n>capacity || capacity>4096) return;
    auto** children=field<void**>(panel,0x14);
    if(n && !children) return;
    for(int i=0;i<n;++i) if(!children[i] || field<void*>(children[i],0x2c)!=panel) return;
    if(!apply_panel(panel,true)) return;
    for(int i=0;i<n;++i) {
        void* child=children[i];
        auto** vt=*static_cast<void***>(child);
        if(vt==at<void**>(0x6a57ec)) {
            // Constructor + the sole retail set-metrics call both author 20/16.
            // These caches are not updated by Item::move or ControlList::rebuild.
            at<void(__fastcall*)(void*,void*,int,int)>(0x625df0)(child,nullptr,20,16);
            field<int>(child,0x514)=int(std::trunc(2*field<float>(panel,0x38)));
        }
        // Cycle's override updates its option rect as well as the base/text/VB.
        // A zero delta preserves logical/animation coordinates on every class.
        reinterpret_cast<void(__fastcall*)(void*,void*,int,int)>(vt[6])(child,nullptr,0,0);
    }
}
void __fastcall draw_window(void* panel,void*) {refresh(panel);original_draw(panel,nullptr);}
int __fastcall input_window(void* panel,void*,unsigned msg,unsigned wparam,unsigned lparam) {
    refresh(panel);
    // BXIG receives actual backbuffer pixels. PtInRect and the native slider/
    // scrollbar inverse already use the transformed pixel rectangles, once.
    return original_input(panel,nullptr,msg,wparam,lparam);
}
struct detour {unsigned va;const char* hex;const void* hook;void** original;};
}
void install(HMODULE game) {
    if(installed || ini::text(L"Remastered",L"Enabled").value_or("0")!="1" ||
       ini::text(L"Remastered",L"CenteredHud").value_or("0")!="1") return;
    image=reinterpret_cast<byte*>(game);
    // X absolute operands in the stolen prefixes and existing installers all
    // require this retail base. Engine DLL detours handle relocation separately.
    if(addr(game)!=0x400000) {logger::write("remastered UI: refused non-retail image base");return;}
    const detour hooks[]{
        {0x621ca0,"568bf15733ff",construct,reinterpret_cast<void**>(&original_ctor)},
        {0x624520,"56578bf18b4608",draw_window,reinterpret_cast<void**>(&original_draw)},
        {0x621ea0,"8b44240483ec38",input_window,reinterpret_cast<void**>(&original_input)},
    };
    patch_transaction::contribution c;c.owner="Remastered Beenox wide pilot";
    for(const auto& h:hooks)c.guards.push_back({h.va,patch_transaction::bytes_of(h.hex),"UI entry"});
    c.guards.push_back({0x61dcab,patch_transaction::bytes_of("d958408b0d34ada600"),"main panel final origin overwrite"});
    for(const auto [va,hex]:{std::pair{0x61ff70u,"83ec088b54240c"},
                            {0x6a5804u,"70ff6100"},
                            {0x625df0u,"db442404568bf1578b7e2cd84f3c"},
                            {0x61ed69u,"6a106a148bcde87c700000"},
                            {0x6260acu,"d94038dcc0e8c6c00400898614050000"},
                            {0x626105u,"8b7e2cd9473cd80dc02c6800"},
                            {0x62611cu,"d9473cd80dfc196800"},
                            {0x621d07u,"db05fc9fa00083c404d80de83b6a00d95e38"},
                            {0x621d19u,"db0500a0a000897e40d80de43b6a00d95e3c"},
                            {0x61fe7bu,"d84f3cd94740d84f38dee9"},
                            {0x6a3be4u,"8988083b"},{0x6a3be8u,"cdcccc3a"}})
        c.guards.push_back({va,patch_transaction::bytes_of(hex),"owner/layout contract"});
    std::string why;
    if(!patch_transaction::verify_expectations(c,why)) {logger::write("remastered UI: %s; native layout retained",why.c_str());return;}
    code=VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    if(!code) return;
    unsigned cursor=0;
    for(const auto& h:hooks) {
        const auto prefix=patch_transaction::bytes_of(h.hex);
        auto* entry=static_cast<byte*>(code)+cursor;*h.original=entry;
        std::memcpy(entry,prefix.data(),prefix.size());
        const auto tail=patch_transaction::relocation(addr(entry)+unsigned(prefix.size()),"e9",true,h.va+unsigned(prefix.size()),0,"UI continuation").bytes;
        std::memcpy(entry+prefix.size(),tail.data(),tail.size());cursor+=32;
        c.writes.push_back(patch_transaction::relocation(h.va,"e9",true,addr(h.hook),unsigned(prefix.size())-5,"UI entry"));
    }
    builder_resume=0x61dcb4;builder_cell=0xa6ad34;
    c.writes.push_back(patch_transaction::relocation(0x61dcab,"e9",true,addr(builder_stub),4,"panel origin"));
    DWORD old=0;
    if(!VirtualProtect(code,4096,PAGE_EXECUTE_READ,&old) || !FlushInstructionCache(GetCurrentProcess(),code,4096)) {
        VirtualFree(code,0,MEM_RELEASE);code=nullptr;return;
    }
    c.publish=[] {installed=true;};
    c.abandon=[] {VirtualFree(code,0,MEM_RELEASE);code=nullptr;};
    c.retain=[] {logger::write("remastered UI: rollback uncertified; refusing startup");TerminateProcess(GetCurrentProcess(),1);};
    patch_transaction::transaction transaction;transaction.add(std::move(c));
    const auto result=transaction.commit();
    logger::write("remastered UI: %s (%s); Beenox uniform canvas/pixel inverse; 4:3 native; wide pilot",patch_transaction::name(result),transaction.report().c_str());
}
}
