// The real Advanced Options close routine and its production options hooks.
namespace options_close_fixture {
std::array<unsigned char,0x28600> panel{};
std::array<unsigned,64> clock_vt{};
std::array<unsigned,4> clock_object{};
unsigned destroys=0,frees=0,reloads=0;
std::array<std::array<unsigned char,0x100>,4> popups{};
bool owned_window(void* p) {
    if(p==panel.data()) return true;
    for(auto& item:popups) if(p==item.data()) return true;
    return false;
}
void* __cdecl clock_get() { return clock_object.data(); }
float __fastcall clock_now(void*,void*,int) { return 1.0f; }
void __cdecl reload() { ++reloads; }
void __fastcall destroy(void* value,void*) { assert(owned_window(value));++destroys; }
void __cdecl release(void* value) { assert(owned_window(value));++frees; }
unsigned native_deactivations=0,draws=0,saves=0,binding_saves=0,commands=0,saved_fsaa=0;
void __fastcall native_deactivate(void*,void*) { ++native_deactivations; }
void __fastcall sound_restore(void*,void*,int) {}
std::array<unsigned char,0x208> control{};
void* __fastcall find_control(void*,void*,int) { return control.data(); }
float __fastcall slider_selection(void*,void*) { return 0.0f; }
const char* __fastcall pad_name(void*,void*,int) { return "synthetic"; }
void __fastcall save_bindings(void*,void*,void*,int count) { assert(count==42);++binding_saves; }
void __cdecl save() { ++saves;saved_fsaa=*reinterpret_cast<unsigned*>(0xa68c98); }
bool __fastcall console_command(void*,void*,const char*) { ++commands;return true; }
__declspec(naked) void float_to_int() {
    __asm {
        sub esp,8
        fistp qword ptr [esp]
        pop eax
        pop edx
        ret
    }
}
void run() {
    options_menu::install(reinterpret_cast<HMODULE>(0x400000));assert(options_menu::installed());
    clock_vt[0x28/4]=unsigned(reinterpret_cast<std::uintptr_t>(clock_now));
    clock_vt[0x24/4]=unsigned(reinterpret_cast<std::uintptr_t>(anchor_fixture::model_time));
    clock_object[0]=unsigned(reinterpret_cast<std::uintptr_t>(clock_vt.data()));
    redirect(0x55b610,clock_get);redirect(0x5d8920,clock_get);
    redirect(0x617870,destroy);redirect(0x6721f2,release);redirect(0x619770,reload);
    redirect(0x672161,anchor_fixture::noop);
    redirect(0x619440,save);redirect(0x621d60,find_control);redirect(0x620d10,slider_selection);
    redirect(0x625420,pad_name);redirect(0x6263b0,save_bindings);redirect(0x67217c,float_to_int);
    redirect(0x551ed0,clock_get);redirect(0x55c890,clock_get);redirect(0x5ace30,anchor_fixture::noop);
    clock_vt[4/4]=unsigned(reinterpret_cast<std::uintptr_t>(anchor_fixture::noop));
    clock_vt[0x1c/4]=unsigned(reinterpret_cast<std::uintptr_t>(console_command));
    clock_vt[0xd4/4]=unsigned(reinterpret_cast<std::uintptr_t>(native_deactivate));
    clock_vt[0x68/4]=unsigned(reinterpret_cast<std::uintptr_t>(sound_restore));
    put(control.data(),0x1e0,3);*reinterpret_cast<unsigned*>(0xa68da8)=2;

    for(const unsigned result:{1u,2u,3u}) {
        panel.fill(0);destroys=frees=reloads=saves=binding_saves=commands=native_deactivations=0;
        put(panel.data(),0x48,result);
        for(unsigned a=0xa6ad34;a<=0xa6ad44;a+=4) *reinterpret_cast<unsigned*>(a)=0;
        *reinterpret_cast<void**>(0xa6ad34)=panel.data();
        *reinterpret_cast<unsigned*>(0xa6ac00)=0;
        *reinterpret_cast<unsigned char*>(0xa6ad48)=0;
        *reinterpret_cast<unsigned char*>(0x6f3a2d)=0;
        // 622300 animates the synthetic empty item array; 6223b0 tests completion.
        const bool active=reinterpret_cast<bool(__cdecl*)()>(0x61f380)();
        assert(!active && !*reinterpret_cast<void**>(0xa6ad34));
        assert(destroys==1 && frees==1 && reloads==(result==2 ? 1u : 0u));
        std::array<unsigned char,0x1a00> owner{};put(owner.data(),0,0x6a1f64);
        put(owner.data(),0x828,0x3fffffff);
        for(const unsigned off:{0xe4u,0xe8u}) put(owner.data(),off,*reinterpret_cast<unsigned*>(0x69d058));
        (*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x6a1fac))(owner.data(),nullptr);
        reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5d1eb0)(owner.data(),nullptr);
        (*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x6a1f80))(owner.data(),nullptr);
        assert(native_deactivations==1 && commands==1);
        assert(destroys==1 && frees==1 && reloads==(result==2 ? 1u : 0u));
        assert(saves==(result==1 ? 1u : 0u) && binding_saves==saves);
        if(result==1) assert(saved_fsaa==3); // Actual native Accept committed the selected value.
    }
    std::cout<<"Advanced Options close PASS retail 61f380 / 622300 / 6223b0, production Save/Cancel hooks: Accept commits once, Cancel reloads once; terminal close + patched resource close/deactivation preserve both without another cleanup\n";
}
std::array<unsigned,8> panel_vt{};
void __fastcall draw(void*,void*) { ++draws; }
void orphan() {
    options_menu::install(reinterpret_cast<HMODULE>(0x400000));assert(options_menu::installed());
    clock_vt[0xd4/4]=unsigned(reinterpret_cast<std::uintptr_t>(native_deactivate));
    clock_vt[0x68/4]=unsigned(reinterpret_cast<std::uintptr_t>(sound_restore));
    clock_object[0]=unsigned(reinterpret_cast<std::uintptr_t>(clock_vt.data()));
    redirect(0x5d8920,clock_get);redirect(0x551ed0,clock_get);
    redirect(0x617870,destroy);redirect(0x6721f2,release);redirect(0x619770,reload);
    redirect(0x5ace30,anchor_fixture::noop);
    panel_vt[0]=unsigned(reinterpret_cast<std::uintptr_t>(draw));
    std::array<unsigned char,0x1a00> menu{};put(menu.data(),0,0x6a1f64);
    const bool resource_close=mode=="r18-options-orphan-close";
    put(menu.data(),0x828,0x3fffffff);
    for(const unsigned off:{0xe4u,0xe8u}) put(menu.data(),off,*reinterpret_cast<unsigned*>(0x69d058));
    const auto deactivation=resource_close ? *reinterpret_cast<void(__fastcall**)(void*,void*)>(0x6a1fac) : mode=="r18-options-orphan-native" ? reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5d1e50) : *reinterpret_cast<void(__fastcall**)(void*,void*)>(0x6a1f80);
    panel.fill(0);put(panel.data(),0,unsigned(reinterpret_cast<std::uintptr_t>(panel_vt.data())));
    const bool with_popups=mode=="r18-options-orphan-popups";
    *reinterpret_cast<void**>(0xa6ad34)=panel.data();
    for(unsigned a=0xa6ad38;a<=0xa6ad44;a+=4) {
        auto& popup=popups[(a-0xa6ad38)/4];popup.fill(0);put(popup.data(),0,unsigned(reinterpret_cast<std::uintptr_t>(panel_vt.data())));
        *reinterpret_cast<void**>(a)=with_popups ? popup.data() : nullptr;
    }
    *reinterpret_cast<unsigned char*>(0x8afde4)=1;
    // Live fact: the engine leaves options_controller while its panel is still
    // present; Cancel never logged. Actual deactivation followed by actual draw.
    deactivation(menu.data(),nullptr);
    reinterpret_cast<void(__cdecl*)()>(0x619390)();
    if(mode=="r18-options-orphan-native") {
        assert(*reinterpret_cast<void**>(0xa6ad34) && draws==1 && !destroys && !frees && !reloads);
        std::cout<<"EXPECTED ghost: retail Advanced Options deactivation leaves the panel and renderer alive\n";
    } else {
        assert(!*reinterpret_cast<void**>(0xa6ad34) && draws==0 && destroys==(with_popups ? 5u : 1u) && frees==destroys && reloads==1);
        assert(!*reinterpret_cast<unsigned char*>(0x8afde4));
        for(unsigned a=0xa6ad34;a<=0xa6ad44;a+=4) assert(!*reinterpret_cast<void**>(a));
        // A later deactivation cannot reload settings or free a panel twice.
        deactivation(menu.data(),nullptr);
        assert(destroys==(with_popups ? 5u : 1u) && frees==destroys && reloads==1 && native_deactivations==(resource_close ? 0u : 2u));
        std::cout<<"Advanced Options orphan PASS actual deactivation -> native cancellation/teardown -> actual draw; panel gone, no later draw, no double cleanup\n";
    }
}

}
