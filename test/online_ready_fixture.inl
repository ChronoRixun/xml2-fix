// A37 presentation adapters exercised through their installed retail call sites.
// Invented rows, font manager, viewport, and RGBA service; no readiness writes.
namespace ready_fixture {
unsigned target(unsigned site) {
    assert(*reinterpret_cast<const unsigned char*>(site)==0xe8);
    return site+5+*reinterpret_cast<const int*>(site+1);
}
int measured_font=-1;
float measured_scale=0;
std::array<float,4> rgba{};
float __fastcall width(void*,void*,const char* value,float scale,int index) {
    assert(std::string_view(value)=="Test player (Test hero): Not ready");
    measured_font=index;measured_scale=scale;return 123.25f;
}
float __fastcall height(void*,void*,int length,const char* value,float scale,int index,int flags) {
    assert(length==33 && std::string_view(value)=="Test player (Test hero): Not ready" && flags==9);
    measured_font=index;measured_scale=scale;return 17.25f;
}
void* __fastcall color(void* out,void*,float r,float g,float b,float a) {
    rgba={r,g,b,a};return out;
}
__declspec(naked) float __cdecl invoke_width(unsigned,void*,const char*,float,int) {
    __asm {
        mov edx,[esp+4]
        mov eax,[esp+8]
        push dword ptr [esp+20]
        push dword ptr [esp+20]
        push dword ptr [esp+20]
        call edx
        ret
    }
}
__declspec(naked) float __cdecl invoke_height(unsigned,void*,const char*,float,int,bool) {
    __asm {
        push edi
        mov edx,[esp+8]
        mov eax,[esp+12]
        mov edi,eax
        push 9
        push dword ptr [esp+28]
        push dword ptr [esp+28]
        push dword ptr [esp+28]
        push 33
        call edx
        pop edi
        ret
    }
}
void run() {
    std::array<unsigned,16> methods{};
    methods[0x34/4]=unsigned(reinterpret_cast<std::uintptr_t>(&width));
    methods[0x38/4]=unsigned(reinterpret_cast<std::uintptr_t>(&height));
    auto* font=methods.data();
    constexpr char value[]="Test player (Test hero): Not ready";
    for(int screen_height:{480,600,720,768,960,1080}) {
    *reinterpret_cast<int*>(0xa0a000)=screen_height;
    const float expected_scale=(10.f/64.f)*(float(screen_height)/384.f);
    for(bool available:{false,true}) {
        lc_fonts::body_ready=available;
        for(const auto site:{0x5d958bu,0x5d9644u}) {
            assert(invoke_width(target(site),&font,value,1.f,0)==123.25f);
            assert(measured_font==(available?6:0) && measured_scale==(available?expected_scale:1.f));
        }
        for(const auto site:{0x5d941du,0x5d9673u}) {
            assert(invoke_height(target(site),&font,value,1.f,0,true)==17.25f);
            assert(measured_font==(available?6:0) && measured_scale==(available?expected_scale:1.f));
        }
        *reinterpret_cast<unsigned*>(0x67f9ac)=unsigned(reinterpret_cast<std::uintptr_t>(&color));
        for(const auto site:{0x5d9742u,0x5d97d7u,0x5d9c40u,0x5d9d14u}) {
            using fn=void*(__fastcall*)(void*,void*,float,float,float,float);
            assert(reinterpret_cast<fn>(target(site))(&font,nullptr,0,0,0,.5f)==&font);
            assert(rgba[0]==(available?17.f/255:0) && rgba[1]==(available?40.f/255:0) &&
                   rgba[2]==(available?74.f/255:0) && rgba[3]==(available?.92f:.5f));
        }
    }
    }
    *reinterpret_cast<int*>(0xa0a000)=768;lc_fonts::body_ready=true;
    std::cout<<"PASS ready roster: 4 measure sites, 4 panel sites, native/LC measurement and panel arguments\n";
}

}
