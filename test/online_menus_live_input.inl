// 060s facts: enabled=false actions answer the mouse but retain the parser's 0x20 flag. Unlike the old r16
// fixture, execute the retail parser's flag block, enabled setter, stored-focus setter, neighbour search and usecmd.
namespace live_input_fixture {
using namespace hits_fixture;
using tick_t=void(__fastcall*)(void*,void*);
void __fastcall focus_event(void* it,void*,bool gain) {
    auto* p=static_cast<unsigned char*>(it);p[0x54]=static_cast<unsigned char>((p[0x54]&~1u)|(gain ? 1u : 0u));
    if(gain) rep.focus_set=name_of(it);
}
const char* __fastcall flag_property(void*,void*,const char* key) {
    if(std::string_view(key)=="neverfocus" || std::string_view(key)=="enabled") return "false";
    return nullptr;
}
__declspec(naked) void parse_flags(void*) {
    __asm {
        push ebx
        push esi
        push ebp
        mov esi,[esp+16]
        xor ebx,ebx
        xor ebp,ebp
        mov eax,05bc94eh
        call eax
        pop ebp
        pop esi
        pop ebx
        ret
    }
}
void* scene_item(const char* name) {
    for(auto& i:items) if(i.menu==reinterpret_cast<const char*>(hub.data()+0xc) && i.name==name) return i.mem.data();
    return nullptr;
}
void setup(const char* screen="lc_online") {
    install_recorders();make_items();set_menus();
    // Keep the real CMenu setFocus: it writes +0x324 and calls item focus loss/gain and menu refresh.
    restore_retail(0x5aaf80,5);restore_retail(0x5ade70,5);
    item_vtable[0x4c/4]=unsigned(reinterpret_cast<std::uintptr_t>(&focus_event));
    redirect(0x5649c0,flag_property);redirect(0x672562,reinterpret_cast<const void*>(&std::strcmp));
    *reinterpret_cast<unsigned char*>(0x5bc9c8)=0xc3; // return at the end of the real flag parser block
    for(auto& i:items) if(i.menu=="lc_online") i.menu=screen;
    std::strcpy(reinterpret_cast<char*>(hub.data()+0xc),screen);
    build_menu(hub.data(),screen);scene_lookup=scene_item;
    online_menus::set_pointer_probe(&probe);online_menus::set_hits_trace(false);
    set_current(hub.data());
    for(auto& i:items) if(i.menu==screen && i.bar) put(i.mem.data(),4,unsigned(reinterpret_cast<std::uintptr_t>(hub.data())));
}
void command_on(fitem& i,unsigned id,int action) {
    const auto command="runscript lcOnlineIntent("+std::to_string(action)+")";
    text_of(int(id),command.c_str());put(i.mem.data(),0x24,id);
}
void accept_focused(void* menu) {
    auto* focused=*reinterpret_cast<void**>(static_cast<char*>(menu)+0x324);assert(focused);
    // The item vt+0x40 that CMenuItem's keyboard/pad Accept dispatch calls: real native usecmd -> installed queue hook.
    reinterpret_cast<int(__thiscall*)(void*)>(0x5bc350)(focused);
}
void navigate(void* menu,int x,int y) {
    const signed char step[]{static_cast<signed char>(x),static_cast<signed char>(y),0,0};
    auto* next=reinterpret_cast<void*(__thiscall*)(void*,const void*)>(0x5aafd0)(menu,step);assert(next);
    reinterpret_cast<void*(__thiscall*)(void*,void*)>(0x5aaf80)(menu,next);
}
bool __fastcall pause_queue(void*,void*,const char* line) {
    calls.push_back(std::string("queue:")+line);return true;
}
unsigned pause_keyboard_notes=0;
void pause_keyboard_note() { ++pause_keyboard_notes; }
void pause_party() {
    setup("pda");
    auto& link=item_named("pda","host");link.name="lc_party_link";
    // Positive-top text box, like the classic pause scene: the native hit is shifted up 8 units.
    link.ax=204;link.az=105;link.l=0;link.b=-1;link.r=101;link.t=17;lay_out(link,1);
    fitem fill;fill.menu="pda";fill.name="option07";fill.ax=254;fill.az=114;
    fill.l=-55;fill.b=-14;fill.r=55;fill.t=13;fill.flags=0x28;fill.kind=1;
    items.push_back(fill);auto& bar=items.back();lay_out(bar,111);
    put(bar.mem.data(),0,unsigned(reinterpret_cast<std::uintptr_t>(model_vtable.data())));
    put(bar.mem.data(),8,111);put(bar.mem.data(),0x24,0);put(bar.mem.data(),0x9c,0);text_of(111,"option07");
    // An inactive highlight may have no usable geometry. Its always-visible background is authoritative.
    fitem idle=bar;idle.name="option07_focus";idle.l=0;idle.b=-1;idle.r=1;idle.t=0;
    items.push_back(idle);auto& inactive=items.back();lay_out(inactive,112);
    put(inactive.mem.data(),0,unsigned(reinterpret_cast<std::uintptr_t>(model_vtable.data())));
    put(inactive.mem.data(),8,112);put(inactive.mem.data(),0x24,0);put(inactive.mem.data(),0x9c,0);text_of(112,"option07_focus");
    text_of(int(*reinterpret_cast<unsigned*>(link.mem.data()+8)),"lc_party_link");
    put(hub.data(),0,0x69ed6c);put(hub.data(),0x18d8,0);
    // Retain the real stored-focus service, with only PDA description refresh stubbed.
    put(reinterpret_cast<void*>(0x69ed6c),4,0x5aaf80);
    put(reinterpret_cast<void*>(0x69ed6c),0x68,unsigned(reinterpret_cast<std::uintptr_t>(hover_refresh_rec)));
    build_menu(hub.data(),"pda");
    *reinterpret_cast<void**>(hub.data()+0x324)=nullptr;
    const auto mouse=*reinterpret_cast<mouse_t*>(0x69edec);
    const auto got=ask_unit(mouse,hub.data(),250,102);
    std::cout<<"PDA Party hover: focus="<<name_of(*reinterpret_cast<void**>(hub.data()+0x324))<<"\n";
    assert(*reinterpret_cast<void**>(hub.data()+0x324)==link.mem.data());
    text_of(110,"openmenu lc_party");put(link.mem.data(),0x24,110);
    // A native refresh can restore a different focus after the last mouse event.
    // Enter is sampled later: the bar still under the pointer must own this accept.
    put(hub.data(),0x324,0);
    unsigned char keys[256]{};online_menus::keyboard(keys);
    redirect(0x55c410,pause_queue);calls.clear();
    pause_keyboard_notes=0;online_menus::set_keyboard_note(pause_keyboard_note);
    keys[0x1c]=0x80;online_menus::keyboard(keys);
    assert(*reinterpret_cast<void**>(hub.data()+0x324)==link.mem.data());
    assert(keys[0x1c]==0);
    assert(pause_keyboard_notes==1); // consumed input must still switch Party's footer to keyboard
    assert(std::count(calls.begin(),calls.end(),"queue:openmenu lc_party")==1);
    put(hub.data(),0x324,0);keys[0x1c]=0x80;online_menus::keyboard(keys);
    assert(keys[0x1c]==0 && std::count(calls.begin(),calls.end(),"queue:openmenu lc_party")==1);
    assert(pause_keyboard_notes==1);
    // Explicit keyboard navigation takes ownership; an old mouse hover cannot
    // steal the next Enter. A disabled Party row cannot be restored either.
    keys[0x1c]=0;online_menus::keyboard(keys);ask_unit(mouse,hub.data(),250,102);
    put(hub.data(),0x324,0);keys[0xc8]=0x80;online_menus::keyboard(keys);
    keys[0xc8]=0;keys[0x1c]=0x80;online_menus::keyboard(keys);
    assert(!*reinterpret_cast<void**>(hub.data()+0x324));
    online_menus::set_keyboard_note(nullptr);
    keys[0x1c]=0;online_menus::keyboard(keys);ask_unit(mouse,hub.data(),250,102);
    put(hub.data(),0x324,0);link.mem[0x54]&=~8u;keys[0x1c]=0x80;online_menus::keyboard(keys);
    assert(!*reinterpret_cast<void**>(hub.data()+0x324));link.mem[0x54]|=8;
    // The missing renamed item and other PDA pages still pass to the retail handler.
    link.name="foreign";build_menu(hub.data(),"pda");put(hub.data(),0x324,0);ask_unit(mouse,hub.data(),250,102);
    assert(!*reinterpret_cast<void**>(hub.data()+0x324));
    std::cout<<"PDA Party PASS mouse hover -> native stored focus -> keyboard Accept queues openmenu lc_party once; absent link keeps native mouse\n";
}
void hub_select() {
    setup();
    for(const auto& pair:{std::pair{"normal","~84$MENU_ACCEPT Select"},std::pair{"hover","~80$MENU_ACCEPT Select"}}) {
        fitem source;source.menu="lc_online";source.name=std::string("lc_hub_select_")+pair.first;source.flags=0x24;
        items.push_back(std::move(source));auto& added=items.back();const int id=int(items.size());
        lay_out(added,id);text_of(id,added.name.c_str());put(added.mem.data(),8,unsigned(id));
        const int text_id=pair.first==std::string_view("normal") ? 120 : 121;
        text_of(text_id,pair.second);put(added.mem.data(),0x9c,unsigned(text_id));
    }
    build_menu(hub.data(),"lc_online");
    auto& select=item_named("lc_online","desctext1");
    text_of(122,"~84$MENU_ACCEPT Select");put(select.mem.data(),0x9c,122);
    auto* row=item_named("lc_online","browse").mem.data();put(hub.data(),0x324,unsigned(reinterpret_cast<std::uintptr_t>(row)));
    const auto mouse=*reinterpret_cast<mouse_t*>(0x69e49c);
    const auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    write_table_text(table_text());
    ask_unit(mouse,hub.data(),60,15);
    assert(*reinterpret_cast<int*>(0x6e4b18)==0);
    auto* native_focus=*reinterpret_cast<void**>(hub.data()+0x324);
    tick(hub.data(),nullptr);
    std::cout<<"Hub Select hover text: "<<texts["desctext1"]<<"\n";
    assert(texts["desctext1"]=="~80$MENU_ACCEPT Select" && *reinterpret_cast<void**>(hub.data()+0x324)==native_focus);
    text_of(122,"~80$MENU_ACCEPT Select"); // the engine setter stored the previous tick's string
    ask_unit(mouse,hub.data(),300,100);tick(hub.data(),nullptr);
    assert(texts["desctext1"]=="~84$MENU_ACCEPT Select");
    std::cout<<"PASS Hub Select native hover: localized navy on amber, normal restored, native focus unchanged by color binding\n";
}
void run() {
    const auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    const auto mouse=*reinterpret_cast<mouse_t*>(0x69e49c);
    for(const char* screen:{"lc_code","lc_browse"}) {
        setup(screen);tick(hub.data(),nullptr); // establish broker scope before injecting its live timeout
        auto& retry=item_named(screen,"retry");
        if(std::string_view(screen)=="lc_code") {
            for(int key=32;key<40;++key) { command_on(retry,110,key);*reinterpret_cast<void**>(hub.data()+0x324)=retry.mem.data();accept_focused(hub.data());tick(hub.data(),nullptr); }
            assert(menu_last_code=="ABCDEFGH");
        }
        parse_flags(retry.mem.data());assert((retry.mem[0x54]&0x28)==0x20);
        retry.mem[0x54]|=4;
        command_on(retry,110,20);
        // Fixture facts from 060s: Retry is shown while waiting, then enabled. Only the broker is synthetic.
        menu_broker.cancel();menu_broker.stopped();ticks+=70000;
        assert(menu_begin(std::string_view(screen)=="lc_code" ? bm::operation::resolve : bm::operation::list));
        menu_broker.fail(menu_reply,"timeout",ticks);
        ++asset_generation;tick(hub.data(),nullptr);assert(!(retry.mem[0x54]&8));
        ticks+=70000;tick(hub.data(),nullptr);
        assert((retry.mem[0x54]&0x2c)==8); // enabled, visible AND no parser-imposed neverfocus
        std::string table="lc-online-hits/1\n"+std::string(screen)+" retry 421.6 32.8 471.2 45.6\n";
        write_table_text(table);
        auto& old=item_named(screen,"host");*reinterpret_cast<void**>(hub.data()+0x324)=old.mem.data();
        const auto hover=ask(mouse,hub.data(),997,648,1280,720);
        assert(hover.answered=="retry" && *reinterpret_cast<void**>(hub.data()+0x324)==retry.mem.data());
        const auto click=ask(mouse,hub.data(),997,648,1280,720,0x202);
        assert(click.answered=="retry" && *reinterpret_cast<void**>(hub.data()+0x324)==retry.mem.data());
        const auto generation=menu_view().generation.request;
        accept_focused(hub.data());tick(hub.data(),nullptr);
        assert(menu_broker.pending() && menu_view().generation.request!=generation);
        menu_broker.cancel();menu_broker.stopped();
    }
    // Same retail parser + enabled transition for the host lobby's Leave and Cancel actions; mouse starts on Cancel.
    setup();
    for(const char* name:{"lc_leave_yes","lc_leave_cancel"}) {
        auto& i=item_named("host",name);parse_flags(i.mem.data());
        assert((i.mem[0x54]&0x28)==0x20);
    }
    // The installed lobby tick's binding owns enable(); r4-live-buttons below drives that transition and leave consent.
    std::cout<<"online menus r16-hits-live PASS (real parser enabled=false side effect, native enabled setter, stored focus, "
        "Retry hover/click and native usecmd on Code and Browse; new broker attempt on each)\n";
}
void shape_run() {
    setup();unsigned checks=0;
    std::string table="lc-online-hits/2\n";char line[180];
    for(const auto& a:layout) {
        const float lean=a.R-a.L<50 ? 0.0f : 4.0f;
        std::snprintf(line,sizeof line,"%s %s %g %g %g %g %g\n",a.menu,a.name,a.L,a.B,a.R,a.T,lean);table+=line;
    }
    const auto mouse=*reinterpret_cast<mouse_t*>(0x69e49c);
    const auto host_mouse=*reinterpret_cast<mouse_t*>(0x69f2f4);
    write_table_text(table_text()); // the actual 060s /1 table misses this drawn right-end pixel
    assert(ask(mouse,hub.data(),624,222,1280,720).answered!="host");
    write_table_text(table);
    for(const auto [w,h]:{std::pair{1280,720},std::pair{1920,1080},std::pair{1024,768},std::pair{960,720},std::pair{1440,1080}})
        for(const auto& a:layout) {
            // The slanted Hub bars reported by 060s use CMenu's base walk. Native lobby routing remains exercised by
            // r16-hits and r4-live-buttons, with the lobby's own complete participant/phase services.
            if(std::string_view(a.menu)!="lc_online") continue;
            void* menu=std::string_view(a.menu)=="host" ? lobby_menu.data() : hub.data();
            const auto fn=menu==lobby_menu.data() ? host_mouse : mouse;
            *reinterpret_cast<void**>(static_cast<char*>(menu)+0x324)=item_named(a.menu,a.name).mem.data();
            const float lean=a.R-a.L<50 ? 0.0f : 4.0f;
            const auto range=rows(a,h);
            // Top/bottom corners and the middle of each slanted end; the old /1 matrix already scans all row pixels.
            for(int py:{range.first,(range.first+range.last)/2,range.last}) {
                const float shift=lean*(view_y(py,h)-a.B)/(a.T-a.B);
                int first=-1,last=-1;
                for(int px=0;px<w;++px) if(view_x(px,w,h)>=a.L+shift && view_x(px,w,h)<a.R+shift) { if(first<0) first=px;last=px; }
                assert(first>0 && last<w-1);
                for(int px:{first,last}) {
                    *reinterpret_cast<void**>(static_cast<char*>(menu)+0x324)=item_named(a.menu,a.name).mem.data();
                    const auto got=ask(fn,menu,px,py,w,h);
                    if(got.answered!=a.name) std::cout<<"shape "<<a.name<<" "<<px<<","<<py<<" "<<w<<"x"<<h<<" answered "<<got.answered<<"\n";
                    assert(got.answered==a.name);++checks;
                }
                for(int px:{first-1,last+1}) {
                    *reinterpret_cast<void**>(static_cast<char*>(menu)+0x324)=item_named(a.menu,a.name).mem.data();
                    assert(ask(fn,menu,px,py,w,h).answered!=a.name);++checks;
                }
            }
        }
    // 060s actual right end; the probe at 680,201 was the separate YOU-card triangle, not this bar.
    *reinterpret_cast<void**>(hub.data()+0x324)=item_named("lc_online","code").mem.data();
    assert(ask(mouse,hub.data(),624,222,1280,720).answered=="host");
    assert(ask(mouse,hub.data(),628,201,1280,720).answered=="host");
    assert(ask(mouse,hub.data(),680,201,1280,720).answered!="host");
    H::table parsed;std::string why;
    assert(H::parse(table,parsed,why));
    for(const char* bad:{"lc-online-hits/2\nx y 1 2 3 4\n","lc-online-hits/2\nx y 1 2 3 4 65\n","lc-online-hits/2\nx y 4090 2 4095 4 4\n"}) {
        H::table refused;std::string error;assert(!H::parse(bad,refused,error));++checks;
    }
    std::cout<<"online menus r16-hits-shapes PASS (slanted drawn edges and corners through retail mouse at five resolutions, "<<checks<<" checks; 060s right-end pixels; legacy /1 remains covered by r16-hits)\n";
}
}

void* __fastcall leave_item_lookup(void* menu,void* unused,const char* name) {
    if(std::string_view(name)=="text_playername1") return lobby_names.data();
    if(std::string_view(name)=="text_readycheck1") return lobby_checks.data();
    return hits_fixture::item_lookup(menu,unused,name);
}
void live_start_buttons() {
    using namespace live_input_fixture;
    ready_person(2,true);refresh_now();
    setup();restore_retail(0x5adf40,5);restore_retail(0x5ab550,5);
    for(auto& i:items) if(i.menu=="host" && i.kind==1)
        put(i.mem.data(),0,unsigned(reinterpret_cast<std::uintptr_t>(item_vtable.data())));
    redirect(0x5adc10,leave_item_lookup);
    scene_lookup=[](const char* name)->void* {
        for(auto& i:items) if(i.menu=="host" && i.name==name) return i.mem.data();return nullptr;
    };
    std::array<unsigned char,0x1a00-0x1954> state{};
    std::memcpy(state.data(),child.data()+0x1954,state.size());
    build_menu(child.data(),"host");std::memcpy(child.data()+0x1954,state.data(),state.size());
    set_current(child.data());mask=0;
    const auto tick=*reinterpret_cast<lobby_tick_t*>(0x69f2ac);
    tick(child.data(),nullptr);
    write_table_text(table_text());
    auto& start=item_named("host","text_startgame");
    auto& copy=item_named("host","lc_copy_code");
    const bool pad=mode=="r4-start-pad";
    online_menus::set_pad_probe(pad ? pad_probe_yes : pad_probe_no);
    auto press=[&](bool expect_release) {
        *reinterpret_cast<void**>(child.data()+0x324)=copy.mem.data();
        if(pad) mask=0x100000;
        else {
            *reinterpret_cast<unsigned char*>(0xa09fa0)=0;
            const auto mouse=*reinterpret_cast<mouse_t*>(0x69f2f4);
            ask(mouse,child.data(),340,630,1280,720,0x202);
            const bool released=*reinterpret_cast<unsigned char*>(0xa09fa0)==1;
            if(expect_release) assert(released && *reinterpret_cast<unsigned*>(0xa09f54)==4);
            put(reinterpret_cast<void*>(0xa09f54),0,0);*reinterpret_cast<unsigned char*>(0xa09fa0)=0;mask=released ? 0x10 : 0;
        }
        tick(child.data(),nullptr);
    };
    for(int deny=0;deny<5;++deny) {
        host_role=true;ready_person(2,true);*reinterpret_cast<unsigned char*>(0x5bae37)=1;
        child[0x195c]=1;child[0x1970]=0;mask=0;tick(child.data(),nullptr);
        if(deny==0) host_role=false;
        if(deny==1) ready_person(2,false);
        if(deny==2) *reinterpret_cast<unsigned char*>(0x5bae37)=4;
        if(deny==3) child[0x195c]=0;
        if(deny==4) child[0x1970]=1;
        // A stale enabled bit must never bypass the retail policy checks.
        start.mem[0x54]|=8;
        press(false);assert(!transfers && !broadcasts && !starts);
    }
    host_role=true;ready_person(2,true);*reinterpret_cast<unsigned char*>(0x5bae37)=1;
    child[0x195c]=1;child[0x1970]=0;mask=0;tick(child.data(),nullptr);
    press(true);
    std::cout<<"Start from Copy: focus "<<name_of(*reinterpret_cast<void**>(child.data()+0x324))<<", transfers "<<transfers<<"\n";
    assert(*reinterpret_cast<void**>(child.data()+0x324)==start.mem.data());
    assert(transfers==1 && child[0x1970]);
    mask=pad ? 0x100000 : 0;tick(child.data(),nullptr);assert(transfers==1);
    // The live trigger: both guests departed, native refresh leaves the host's
    // one ready row and the effective solo minimum is still the native byte.
    child[0x1970]=0;people_count=1;build_people_chain();refresh_now();
    mask=0;tick(child.data(),nullptr);press(true);
    assert(transfers==2 && child[0x1970] && *reinterpret_cast<unsigned*>(people_table.data()+0xc)==1);
    std::cout<<"PASS native START from Copy by mouse/pad, including guests departed; denied authority, readiness, minimum, admission, transfer; held Start does not repeat\n";
}
void live_leave_buttons() {
    using namespace live_input_fixture;
    std::cout<<"leave setup\n";
    setup();restore_retail(0x5adf40,5);restore_retail(0x5ab550,5);
    // This fixture tests input, not the model manager (covered by r16-model-start). Give decorative models a bit-only
    // visibility setter so opening the panel cannot ask the absent renderer to load/restore a model handle.
    for(auto& i:items) if(i.menu=="host" && i.kind==1)
        put(i.mem.data(),0,unsigned(reinterpret_cast<std::uintptr_t>(item_vtable.data())));
    redirect(0x5adc10,leave_item_lookup);
    scene_lookup=[](const char* name)->void* {
        for(auto& i:items) if(i.menu=="host" && i.name==name) return i.mem.data();return nullptr;
    };
    std::array<unsigned char,0x1a00-0x1954> native_state{};
    std::memcpy(native_state.data(),child.data()+0x1954,native_state.size());
    build_menu(child.data(),"host");std::memcpy(child.data()+0x1954,native_state.data(),native_state.size());
    set_current(child.data());
    *reinterpret_cast<void**>(child.data()+0x324)=item_named("host","text_startgame").mem.data();
    auto& yes=item_named("host","lc_leave_yes");auto& cancel=item_named("host","lc_leave_cancel");
    for(auto* i:{&yes,&cancel}) { parse_flags(i->mem.data());i->mem[0x54]|=4; }
    command_on(yes,110,17);command_on(cancel,111,8);
    const auto tick=*reinterpret_cast<lobby_tick_t*>(0x69f2ac);
    // Negative control: the old enabled setter alone leaves 0x20 in place. At the exact point handed to retail by
    // the 060s trace the real mouse walk answers Leave but cannot move the stored focus off Cancel.
    for(auto* i:{&yes,&cancel}) {
        reinterpret_cast<void(__thiscall*)(void*,const char*,bool)>(0x5ade70)(child.data(),i->name.c_str(),true);
        reinterpret_cast<void(__thiscall*)(void*,bool)>(0x5bcdb0)(i->mem.data(),false);
        assert((i->mem[0x54]&0x28)==0x28);
    }
    *reinterpret_cast<void**>(child.data()+0x324)=cancel.mem.data();rep=reply();
    reinterpret_cast<std::uint32_t(__thiscall*)(void*,int,int,int,int,int)>(0x5ae0a0)(child.data(),341,86,0,0x200,0);
    assert(rep.answered=="lc_leave_yes" && *reinterpret_cast<void**>(child.data()+0x324)==cancel.mem.data());
    for(auto* i:{&yes,&cancel}) { parse_flags(i->mem.data());i->mem[0x54]|=4; }
    std::cout<<"leave ready\n";mask=0;
    std::cout<<"leave confirm\n";mask=0x200000;tick(child.data(),nullptr);mask=0;tick(child.data(),nullptr);
    assert(!visibility["lc_leave_confirm"] && (yes.mem[0x54]&0x2c)==8 && (cancel.mem[0x54]&0x2c)==8);
    write_table_text(table_text());
    const auto mouse=*reinterpret_cast<mouse_t*>(0x69f2f4);
    *reinterpret_cast<void**>(child.data()+0x324)=cancel.mem.data();
    const auto hover=ask(mouse,child.data(),1167,828,1920,1080);
    assert(hover.answered=="lc_leave_yes" && *reinterpret_cast<void**>(child.data()+0x324)==yes.mem.data());
    const auto click=ask(mouse,child.data(),1167,828,1920,1080,0x202);
    assert(click.answered=="lc_leave_yes" && *reinterpret_cast<void**>(child.data()+0x324)==yes.mem.data());
    accept_focused(child.data());tick(child.data(),nullptr);mask=0;tick(child.data(),nullptr);
    assert(leaves==1 && current()==nullptr);
    std::cout<<"online menus r4-live-buttons PASS (060s Leave 1167,828: retail flag parser -> production enabling -> retail mouse "
        "and stored focus -> real usecmd -> consent -> retail host leave and cleanup; never Cancel)\n";
}

// Called after the existing native host-end fixture has raised the actual production modal latch.
void live_hostleft_buttons() {
    using namespace live_input_fixture;
    const auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    const auto mouse=*reinterpret_cast<mouse_t*>(0x69e49c);
    setup();
    auto& veil=item_named("lc_online","lc_hub_host_left_veil");
    put(veil.mem.data(),0,unsigned(reinterpret_cast<std::uintptr_t>(item_vtable.data())));
    for(const char* name:{"lc_hub_host_left","lc_hostleft_again","lc_hostleft_back"}) {
        fitem i;i.menu="lc_online";i.name=name;i.ax=name==std::string_view("lc_hostleft_back") ? 248 : 104;i.az=152;
        i.l=0;i.b=-22;i.r=114;i.t=0;i.flags=0x2c;items.push_back(std::move(i));
        auto& added=items.back();const int id=int(items.size());lay_out(added,id);text_of(id,name);put(added.mem.data(),8,unsigned(id));
        parse_flags(added.mem.data());added.mem[0x54]|=4;
    }
    build_menu(hub.data(),"lc_online");
    auto& again=item_named("lc_online","lc_hostleft_again");auto& back=item_named("lc_online","lc_hostleft_back");
    command_on(again,110,2);command_on(back,111,8);
    for(auto* pair:{&again,&back}) for(unsigned off:{0x5cu,0x60u,0x64u,0x68u})
        put(pair->mem.data(),off,unsigned(reinterpret_cast<std::uintptr_t>((pair==&again ? back : again).mem.data())));
    write_table_text("lc-online-hits/1\nlc_online lc_hostleft_again 92 128 238 157\nlc_online lc_hostleft_back 236.8 128 383.2 157\n");
    tick(hub.data(),nullptr);
    assert((again.mem[0x54]&0x2c)==8 && (back.mem[0x54]&0x2c)==8);
    assert(*reinterpret_cast<void**>(hub.data()+0x324)==again.mem.data());
    for(int repetition=0;repetition<2;++repetition) { // keyboard and pad both use this retail directional dispatch
        navigate(hub.data(),0,1);assert(*reinterpret_cast<void**>(hub.data()+0x324)==back.mem.data());
        navigate(hub.data(),0,1);assert(*reinterpret_cast<void**>(hub.data()+0x324)==again.mem.data());
    }
    // A refresh restoring the underlying startactive row is repaired while the latch is still active.
    *reinterpret_cast<void**>(hub.data()+0x324)=item_named("lc_online","host").mem.data();
    tick(hub.data(),nullptr);assert(*reinterpret_cast<void**>(hub.data()+0x324)==again.mem.data());
    const bool host_again=mode.find("-again-")!=std::string::npos;
    const bool keyboard_or_pad=mode.find("-keyboard-")!=std::string::npos || mode.find("-pad-")!=std::string::npos;
    if(keyboard_or_pad) {
        if(mode.find("-pad-")!=std::string::npos) assert(pad_menu_mask(raw_menu_mask(pad_key::a))&0x10);
        navigate(hub.data(),0,1);assert(*reinterpret_cast<void**>(hub.data()+0x324)==back.mem.data());
    } else {
        const auto* target=host_again ? "lc_hostleft_again" : "lc_hostleft_back";
        void* wanted=(host_again ? again : back).mem.data();
        const int px=host_again ? 470 : 730;
        const auto hover=ask(mouse,hub.data(),px,458,1280,720);
        assert(hover.answered==target && *reinterpret_cast<void**>(hub.data()+0x324)==wanted);
        const auto click=ask(mouse,hub.data(),px,458,1280,720,0x202);
        assert(click.answered==target);
    }
    accept_focused(hub.data());tick(hub.data(),nullptr);
    if(host_again) assert(calls.back()=="request:lc_host");
    else {
        assert(back.mem[0x54]&4); // Dismiss ran through the native usecmd and production intent mailbox
        assert((item_named("lc_online","host").mem[0x54]&8)!=0);
    }
    std::cout<<"online menus host-left buttons PASS (real host-end latch, real parser flags, enabled setter, setFocus, "
        "navigation, mouse at the 060s 730,458 probe, native Back to Online usecmd)\n";
}
