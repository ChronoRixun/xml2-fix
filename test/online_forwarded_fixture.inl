// Forwarded b2b F5/F6: production keyboard/mask entry points and backend model.
namespace forwarded_fixtures {
void run() {
    const auto tick=*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69e454);
    redirect(0x5adf40,focus_stub);
    if(mode=="r29-service") {
        set_current(hub.data());tick(hub.data(),nullptr);
        std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_browse");child[4]=1;put(child.data(),0,0x69e41c);set_current(child.data());
        tick(child.data(),nullptr);assert(menu_lists==1);
        menu_error="broker_unreachable";menu_complete=true;tick(child.data(),nullptr);
        assert(texts["service"]=="Unavailable" && texts["broker_error"].find("broker_unreachable")==std::string::npos);
        int_value[1]=6;const auto* entry=script_function_overflow::table().find(function_name);
        reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(entry->handler))(nullptr);tick(child.data(),nullptr);
        set_current(hub.data());tick(hub.data(),nullptr);
        assert(texts["service"]=="Unavailable");
        ticks+=3000;std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_browse");set_current(child.data());tick(child.data(),nullptr);
        assert(texts["service"]=="Connecting...");
        menu_auth=menu_complete=true;tick(child.data(),nullptr);
        assert(!visibility["lc_service_ok"]);
        set_current(hub.data());tick(hub.data(),nullptr);assert(!visibility["lc_service_ok"]);
        std::cout<<"PASS service failure survives Back; fresh request connects; successful service survives cancel; raw ids hidden\n";return;
    }
    mode="r29-host-input";
    text_fixture::real_base_tick();
    std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_host");child[4]=1;put(child.data(),0,0x69e41c);set_current(child.data());
    // Distinct real CText objects, so focus comparisons cannot alias.
    static std::map<std::string,std::array<unsigned char,0xb0>> controls;
    scene_lookup=+[](const char* name)->void* {
        auto& item=controls[name];if(!*reinterpret_cast<unsigned*>(item.data())) {item=text_item;item[0x54]=8;}
        return item.data();
    };
    focus_stub(child.data(),nullptr,"visibility",true);mask=0;tick(child.data(),nullptr);
    const auto key=[&](unsigned scan) {
        std::array<unsigned char,256> state{};online_menus::keyboard(state.data());state[scan]=128;online_menus::keyboard(state.data());assert(!state[scan]);tick(child.data(),nullptr);
    };
    key(0xd0);assert(focused=="room_name");key(0xd0);assert(focused=="campaign");key(0xd0);assert(focused=="drop_in");
    const auto before=menu_drop_changes;
    key(0xcd);assert(menu_drop_changes==before+1 && focused=="drop_in");
    key(0xcb);assert(menu_drop_changes==before+2 && focused=="drop_in");
    key(0xd3);assert(menu_drop_changes==before+3);
    key(0x1c);assert(menu_drop_changes==before+4);
    // Holding the Host Enter into another menu must not activate its default Post.
    std::array<unsigned char,256> held{};held[0x1c]=128;set_current(main_menu.data());
    online_menus::keyboard(held.data());assert(held[0x1c]==0);
    held.fill(0);online_menus::keyboard(held.data());set_current(child.data());
    key(0xd0);std::cout<<"Open row focus="<<focused<<" change=["<<texts["desctext2"]<<"] flags="<<unsigned(controls["open"][0x54])<<"\n";assert(focused=="open" && texts["desctext2"].empty());key(0xc8);assert(focused=="drop_in");
    const auto pad=[&](unsigned m) {mask=m;tick(child.data(),nullptr);mask=0;tick(child.data(),nullptr);};
    pad(1u<<16);assert(menu_drop_changes==before+5 && focused=="drop_in");
    pad(1u<<17);assert(menu_drop_changes==before+6 && focused=="drop_in");
    pad(raw_menu_mask(pad_key::y));assert(menu_drop_changes==before+7);
    pad(raw_menu_mask(pad_key::a));assert(menu_drop_changes==before+8);
    pad(1u<<12);assert(focused=="open");pad(1u<<11);assert(focused=="drop_in");
    const auto stick=[&](float x,float y) {
        stick_x=x;stick_y=y;mask=(x ? 1u : 0u)|(y ? 2u : 0u);tick(child.data(),nullptr);
        stick_x=stick_y=0;mask=0;tick(child.data(),nullptr);
    };
    stick(0,-1);assert(focused=="open");stick(0,1);assert(focused=="drop_in");
    stick(1,0);stick(-1,0);assert(menu_drop_changes==before+10);
    focus_stub(child.data(),nullptr,"visibility",true);tick(child.data(),nullptr);
    separate_held_mask=true;held_mask=2;stick_y=-1;mask=2;tick(child.data(),nullptr);mask=0;
    assert(focused=="room_name");ticks+=299;tick(child.data(),nullptr);assert(focused=="room_name");
    ++ticks;tick(child.data(),nullptr);assert(focused=="campaign");ticks+=100;tick(child.data(),nullptr);assert(focused=="drop_in");
    stick_y=0;held_mask=0;separate_held_mask=false;tick(child.data(),nullptr);
    std::cout<<"PASS b2e Host signed stick directions and held 300/100 ms row repeat\n";
    menu_drop_locked=true;tick(child.data(),nullptr);pad(raw_menu_mask(pad_key::y));assert(menu_drop_changes==before+10);
    online_menus::set_pad_probe(pad_probe_yes);tick(child.data(),nullptr);assert(texts["desctext1"]=="A Select");
    std::cout<<"PASS Host DI/pad rows, focused values, Enter/A, Del/Y, Open Up, locked policy and device prompts\n";
}
}
