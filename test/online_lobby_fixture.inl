// Included in online_menus_test.cpp's fixture namespace. All retail routines
// below execute in the mapped owned PE; engine services/participants are fake.
void* current() { return *reinterpret_cast<void**>(mgr.data()+0x86090); }
std::array<std::array<unsigned char,0xc0>,4> lobby_people{};
std::array<unsigned char,0x90> names_table{},checks_table{},people_table{};
unsigned people_count=3,local_id=42,iterator_row=0;
constexpr unsigned participant_ids[]{42,7,99,2};
std::string focused="text_startgame";
bool host_role=true,image_present=true,image_success=true,admitted=true;
int broadcasts=0,transfers=0,starts=0,kicks=0,leaves=0,announces=0,refreshes=0,activations=0,cleanups=0;
bool last_ready=false;
bool fixture_throw=false,fixture_bad_code=false;
void* participant(unsigned id) {
    for(unsigned i=0;i<people_count;++i) if(participant_ids[i]==id) return lobby_people[i].data();
    return nullptr;
}
void* __fastcall participant_stub(void*,void*,unsigned id) { return participant(id); }
void* __fastcall local_stub(void*,void*) { return admitted ? participant(local_id) : nullptr; }
unsigned __fastcall local_index_stub(void*,void*) { return local_id; }
bool __fastcall host_role_stub(void*,void*) { return host_role; }
void* __fastcall first_person(void*,void*) { iterator_row=0;return people_count ? lobby_people[0].data() : nullptr; }
void* __fastcall next_person(void*,void*) { ++iterator_row;return iterator_row<people_count ? lobby_people[iterator_row].data() : nullptr; }
void __fastcall no_op(void*,void*) {}
void __fastcall activated(void*,void*,int) { ++activations; }
void __fastcall refreshed(void*,void*) { ++refreshes; }
void* __fastcall participant_list(void*,void*) { return people_table.data(); }
void* __cdecl cast_list(void* p,int,void*,void*,int) { return p; }
const char* __fastcall focused_name(void*,void*) { return focused.c_str(); }
void __fastcall focus_stub(void* menu,void*,const char* name,bool) {
    focused=name;put(menu,0x324,unsigned(reinterpret_cast<std::uintptr_t>(focused=="text_playername1" ? lobby_names.data() : text_item.data())));
}
bool __fastcall accept_stub(void*,void*,unsigned value,int n) { assert(n==1);return (value&0x1100110)!=0; }
void __fastcall sound_stub(void*,void*,int) {}
void __fastcall description_stub(void*,void*,int,const char*) {}
void __fastcall join_leave(void*,void*) { ++leaves; }
void __fastcall shared_cleanup(void*,void*) { ++cleanups; }
int __fastcall previous_controller(void*,void*) { return 0; }
bool __fastcall menu_is(void*,void*,const char*) { return false; }
void close_join(void* menu) {
    auto deactivate=*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69f358);
    deactivate(menu,nullptr);
}
void __fastcall native_close_stub(void* menu,void*) {
    calls.push_back("native-close");
    if(*static_cast<unsigned*>(menu)==0x69f33c) {
        static_cast<unsigned char*>(menu)[0x196e]=1;
        close_join(menu);
    }
    set_current(nullptr);
}
void __fastcall manager_close(void*,void*,const char* name,int) {
    assert(name && (std::string_view(name)=="host" || std::string_view(name)=="join"));
    if(current() && std::string_view(name)=="join") close_join(current());
    set_current(nullptr);
}
bool __fastcall has_image(void*,void*) { return image_present; }
bool __fastcall transfer_image(void*,void*) { ++transfers;return image_success; }
void __fastcall waiting_image(void* menu,void*) { static_cast<unsigned char*>(menu)[0x1970]=1; }
void __fastcall host_leave(void*,void*,bool closing) { assert(!closing);++leaves; }
void __fastcall announce_ready(void*,void*,bool value) { ++announces;last_ready=value; }
void __fastcall kick_person(void*,void*,void* p,int a,int b,int c,bool option) {
    assert(p && p!=participant(local_id) && a==0x10ce && b==0x10cf && c==0x10cf && option);++kicks;
}
std::array<unsigned,160> fake_game_vt{};
unsigned* fake_game=fake_game_vt.data();
void* __cdecl game_getter() { return &fake_game; }
int __fastcall game_mode(void*,void*) { return 2; }
int __fastcall game_type(void*,void*) { return 1; }
void __fastcall init_message(void* p,void*) { std::memset(p,0,20); }
void __fastcall send_start(void*,void*,void* p,int destination,int local,int reliable) {
    const auto* message=static_cast<const unsigned char*>(p);
    assert(destination==-1 && local==0 && reliable==2);
    assert(*reinterpret_cast<const unsigned short*>(message+8)==8 && message[16]==0x26);
    const auto* payload=*reinterpret_cast<const unsigned char* const*>(message);
    assert(*reinterpret_cast<const unsigned*>(payload)==2 && payload[4]==0 && payload[5]==1);++broadcasts;
}
void __fastcall time_scale(void*,void*,float value) { assert(value==1.f);calls.push_back("clock-reset"); }
void __fastcall campaign_start(void*,void*) { calls.push_back("campaign-start"); }
void __cdecl start_local(int value) { assert(value==0);++starts; }
std::array<unsigned,3> locale_vt{};
unsigned* locale_object=locale_vt.data();
void* __cdecl locale_getter() { return &locale_object; }
const char* __fastcall locale_text(void*,void*,int id) { return id==0xfbd ? "Unready" : "Leave"; }
std::array<unsigned,4> dialog_vt{};
unsigned* dialog_object=dialog_vt.data();
void* __cdecl dialog_getter() { return &dialog_object; }
void __fastcall show_dialog(void*,void*,const char*,float,int) { calls.push_back("native-admission-error"); }
bool lobby_fields_stub(online_menus::lobby_fixture& out) {
    if(fixture_throw) throw std::bad_alloc();
    std::strcpy(out.code,fixture_bad_code ? "BAD" : "ABCD-EFGH");out.count=3;
    out.members[0]={99,112,false};out.members[1]={42,0,true};out.members[2]={7,38,true};return true;
}
void ready_person(unsigned row,bool value) { put(lobby_people[row].data(),0x1c,(value ? 4 : 0) | (row==0 ? 2 : 0)); }
void setup_lobby(bool host) {
    host_role=host;local_id=host ? 42 : 7;
    for(unsigned i=0;i<4;++i) {
        lobby_people[i].fill(0);put(lobby_people[i].data(),0x18,participant_ids[i]);ready_person(i,i!=2);
        std::strcpy(reinterpret_cast<char*>(lobby_people[i].data()+0x40),i==0 ? "Logan^n%" : i==1 ? "Kitty\\n" : i==2 ? "Alex" : "Storm");
    }
    names_table.fill(0);checks_table.fill(0);lobby_names.fill(0);lobby_checks.fill(0);
    put(lobby_names.data(),0,0x6a062c);put(lobby_checks.data(),0,0x6a062c);
    put(lobby_names.data(),0xbc,unsigned(reinterpret_cast<std::uintptr_t>(names_table.data())));
    put(lobby_checks.data(),0xbc,unsigned(reinterpret_cast<std::uintptr_t>(checks_table.data())));
    put(names_table.data(),0x84,people_count);put(checks_table.data(),0x84,people_count);
    put(people_table.data(),0xc,people_count);
    put(reinterpret_cast<void*>(0x8abe20),0,people_count*2);
    std::memset(reinterpret_cast<void*>(0x8a83f0+0x3800),0,16);
    std::memset(reinterpret_cast<void*>(0x8a83f0+0x3a20),0,16);
    for(unsigned i=0;i<people_count;++i) {
        names_table[i]=static_cast<unsigned char>(i);checks_table[i]=static_cast<unsigned char>(i+4);
        auto* row=reinterpret_cast<unsigned char*>(0x8a83f0+i*0x70);
        std::memset(row,0,0x70);std::strcpy(reinterpret_cast<char*>(row+4),"unsafe^%\\name");put(row,0x5c,participant_ids[i]);
        for(auto index:{i,i+4}) for(auto offset:{0x3800u,0x3a20u})
            *reinterpret_cast<unsigned*>(0x8a83f0+offset+4*(index/32))|=1u<<(index&31);
    }
    child.fill(0);put(child.data(),0,host ? 0x69f274 : 0x69f33c);child[4]=1;child[0x195c]=1;
    std::strcpy(reinterpret_cast<char*>(child.data()+0xc),host ? "host" : "join");
    put(child.data(),host ? 0x1964 : 0x1960,unsigned(reinterpret_cast<std::uintptr_t>(lobby_names.data())));
    put(child.data(),host ? 0x1968 : 0x1964,unsigned(reinterpret_cast<std::uintptr_t>(lobby_checks.data())));
    put(child.data(),host ? 0x196c : 0x1968,unsigned(reinterpret_cast<std::uintptr_t>(people_table.data())));
    child[0x196f]=1;
    focus_stub(child.data(),nullptr,host ? "text_startgame" : "text_readygame",true);set_current(child.data());
    lobby_back_callback=*reinterpret_cast<int(__fastcall**)(void*,void*)>(host ? 0x69f2a0 : 0x69f368);
    redirect(0x6110c0,participant_stub);redirect(0x6111f0,local_stub);redirect(0x611250,local_index_stub);redirect(0x610d10,host_role_stub);
    redirect(0x5ba4a0,first_person);redirect(0x5ba4c0,next_person);
    redirect(0x5caff0,activated);redirect(0x610ca0,participant_list);redirect(0x67234e,cast_list);
    redirect(0x5ba4e0,refreshed);redirect(0x5c8150,refreshed);
    redirect(0x5ab550,focused_name);redirect(0x5adf40,focus_stub);
    redirect(0x5d4410,sound_stub);redirect(0x5bc6b0,description_stub);redirect(0x5ab030,no_op);
    // Preserve the class vtable addresses while stubbing its presentation and
    // close services; the action ticks and ready-aware Back remain retail.
    put(reinterpret_cast<void*>(host ? 0x69f274 : 0x69f33c),0x68,unsigned(reinterpret_cast<std::uintptr_t>(&no_op)));
    if(!host) put(reinterpret_cast<void*>(0x69f33c),0x2c,unsigned(reinterpret_cast<std::uintptr_t>(&native_close_stub)));
    redirect(0x5db270,manager_close);redirect(0x606f90,has_image);redirect(0x6094f0,transfer_image);
    redirect(0x6097f0,join_leave);redirect(0x5cb120,shared_cleanup);redirect(0x551370,previous_controller);
    redirect(0x550730,no_op);redirect(0x5d71b0,menu_is);
    redirect(0x5ba200,waiting_image);redirect(0x607c40,host_leave);redirect(0x6070b0,announce_ready);redirect(0x6133a0,kick_person);
    fake_game_vt[0x268/4]=unsigned(reinterpret_cast<std::uintptr_t>(&game_mode));
    redirect(0x46dce0,game_getter);redirect(0x60b9d0,game_type);redirect(0x615810,init_message);
    redirect(0x611270,send_start);redirect(0x60a920,time_scale);redirect(0x609400,campaign_start);redirect(0x5f3c20,start_local);
    locale_vt[2]=unsigned(reinterpret_cast<std::uintptr_t>(&locale_text));redirect(0x4bdb30,locale_getter);
    dialog_vt[3]=unsigned(reinterpret_cast<std::uintptr_t>(&show_dialog));redirect(0x5eb300,dialog_getter);
    // Rule 26's already-reviewed solo minimum; fixture applies its effective
    // byte, never a production policy change from this round.
    *reinterpret_cast<unsigned char*>(0x5bae37)=1;
}
using lobby_tick_t=void(__fastcall*)(void*,void*);
using lobby_activate_t=void(__fastcall*)(void*,void*,int);
using lobby_back_t=int(__fastcall*)(void*,void*);
void invalidate_selection() { *reinterpret_cast<short*>(lobby_names.data()+0xac)=-1; }
bool copy_open_ok=true,copy_empty_ok=true,copy_set_ok=true;
bool copy_has_window=true,copy_own_process=true,copy_engine_class=true;
int copies=0,copy_opens=0,copy_closes=0;
std::wstring copied_code;
BOOL WINAPI copy_open(HWND window) { assert(window==reinterpret_cast<HWND>(0x1234));++copy_opens;return copy_open_ok; }
BOOL WINAPI copy_enum(DWORD thread,WNDENUMPROC callback,LPARAM context) {
    assert(thread==GetCurrentThreadId());
    return copy_has_window ? callback(reinterpret_cast<HWND>(0x1234),context) : TRUE;
}
DWORD WINAPI copy_window_process(HWND window,LPDWORD process) {
    assert(window==reinterpret_cast<HWND>(0x1234));*process=GetCurrentProcessId()+(copy_own_process ? 0 : 1);return GetCurrentThreadId();
}
int WINAPI copy_class(HWND window,LPSTR name,int capacity) {
    assert(window==reinterpret_cast<HWND>(0x1234) && capacity>=32);
    std::strcpy(name,copy_engine_class ? "igWin32WindowClass" : "foreign-window");return int(std::strlen(name));
}
BOOL WINAPI copy_close() { ++copy_closes;return TRUE; }
BOOL WINAPI copy_empty() { return copy_empty_ok; }
HANDLE WINAPI copy_set(UINT format,HANDLE memory) {
    assert(format==CF_UNICODETEXT);
    if(!copy_set_ok) return nullptr;
    const auto* data=static_cast<const wchar_t*>(GlobalLock(memory));assert(data);
    copied_code=data;GlobalUnlock(memory);GlobalFree(memory);++copies;return memory;
}
void copy_tests(const script_functions_rules::func_entry* registration,lobby_tick_t tick) {
    import_stub("OpenClipboard",reinterpret_cast<void*>(&copy_open));import_stub("CloseClipboard",reinterpret_cast<void*>(&copy_close));
    import_stub("EmptyClipboard",reinterpret_cast<void*>(&copy_empty));import_stub("SetClipboardData",reinterpret_cast<void*>(&copy_set));
    import_stub("EnumThreadWindows",reinterpret_cast<void*>(&copy_enum));
    import_stub("GetWindowThreadProcessId",reinterpret_cast<void*>(&copy_window_process));import_stub("GetClassNameA",reinterpret_cast<void*>(&copy_class));
    auto post=[&](intent action) {
        int_value[1]=unsigned(action);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(reinterpret_cast<void*>(1));
    };
    std::array<unsigned char,256> keys{};online_menus::keyboard(keys.data());
    ready_person(2,true);focus_stub(child.data(),nullptr,"text_startgame",true);
    keys[0x1d]=keys[0x2e]=0x80;online_menus::keyboard(keys.data());assert(!keys[0x2e]);
    // Coincident Accept on Start must not start/transfer while Copy owns input.
    mask=0x10;tick(child.data(),nullptr);assert(copies==1 && !transfers && !broadcasts && copied_code==L"ABCD-EFGH");
    keys[0x2e]=0x80;online_menus::keyboard(keys.data());mask=0x10;tick(child.data(),nullptr);assert(copies==1 && !transfers && !broadcasts);
    keys.fill(0);online_menus::keyboard(keys.data());
    mask=0;post(intent::dismiss);tick(child.data(),nullptr);focus_stub(child.data(),nullptr,"text_startgame",true);
    mask=0x400010;tick(child.data(),nullptr);assert(copies==2 && !transfers && !broadcasts);
    tick(child.data(),nullptr);assert(copies==2 && !transfers && !broadcasts);
    mask=0x10;tick(child.data(),nullptr);assert(!transfers && !broadcasts); // Y released, Accept still held
    mask=0;tick(child.data(),nullptr);
    const auto queue_target=reinterpret_cast<void*>(0x5bc3d5+*reinterpret_cast<unsigned*>(0x5bc3d1));
    assert(invoke_usecmd(queue_target,nullptr,"runscript lcOnlineIntent(16)"));tick(child.data(),nullptr);assert(copies==3);
    copy_open_ok=false;post(intent::copy_code);tick(child.data(),nullptr);assert(copies==3 && copy_closes==copy_opens-1);
    copy_open_ok=true;copy_empty_ok=false;post(intent::copy_code);tick(child.data(),nullptr);assert(copies==3);
    copy_empty_ok=true;copy_set_ok=false;post(intent::copy_code);tick(child.data(),nullptr);assert(copies==3);
    auto opens=copy_opens;
    copy_has_window=false;post(intent::copy_code);tick(child.data(),nullptr);assert(copy_opens==opens);copy_has_window=true;
    copy_own_process=false;post(intent::copy_code);tick(child.data(),nullptr);assert(copy_opens==opens);copy_own_process=true;
    copy_engine_class=false;post(intent::copy_code);tick(child.data(),nullptr);assert(copy_opens==opens);copy_engine_class=true;
    mask=0;tick(child.data(),nullptr);assert(!visibility["lc_lobby_error"] && texts["lc_room_code"]=="ABCD-EFGH" && current()==child.data());
    auto back=*reinterpret_cast<lobby_back_t*>(0x69f2a0);back(child.data(),nullptr);tick(child.data(),nullptr);
    assert(visibility["lc_lobby_error"] && current()==child.data());
    fixture_bad_code=true;copy_set_ok=true;post(intent::copy_code);tick(child.data(),nullptr);assert(copies==3 && !enabled_items["lc_copy_code"]);
    fixture_bad_code=false;fixture_throw=true;auto native_before=native_ticks;tick(child.data(),nullptr);
    assert(native_ticks==native_before+1 && current()==child.data());fixture_throw=false;
    // Real deactivation followed by the same allocation invalidates queued Copy.
    post(intent::copy_code);
    auto deactivate=*reinterpret_cast<lobby_tick_t*>(0x69f290);
    // Exercise production wrapper; retail close service is safely stubbed.
    redirect(0x5ba280,deactivate_stub);deactivate(child.data(),nullptr);set_current(child.data());
    auto activate=*reinterpret_cast<lobby_activate_t*>(0x69f284);activate(child.data(),nullptr,0);
    tick(child.data(),nullptr);assert(copies==3 && current()==child.data());
    std::cout<<"r4 Copy PASS keyboard/pad/usecmd once; coincident Accept suppressed; failure preserves code/lobby; exception exactly-once tick; stale deactivation\n";
}
void pool_tests() {
    // Exercise actual retail shared allocator/base add/clear (the measuring
    // wrapper is unnecessary for a fixture with no font renderer).
    std::memset(reinterpret_cast<void*>(0x8a83f0),0,0x3a34);
    reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5bfac0)(reinterpret_cast<void*>(0x8a83f0),nullptr);
    names_table.fill(0);checks_table.fill(0);
    redirect(0x672170,reinterpret_cast<const void*>(&std::strncpy));
    auto add=reinterpret_cast<int(__fastcall*)(void*,void*,const char*,bool)>(0x5c07e0);
    auto clear=reinterpret_cast<bool(__fastcall*)(void*,void*)>(0x5c0780);
    for(int pass=0;pass<12;++pass) {
        for(int row=0;row<64;++row) {
            assert(add(lobby_names.data(),nullptr,std::string(83,'N').c_str(),false)==row);
            assert(add(lobby_checks.data(),nullptr,"Ready",false)==row);
        }
        assert(*reinterpret_cast<unsigned*>(0x8abe20)==128);
        assert(add(lobby_names.data(),nullptr,"Overflow",false)==-1);
        assert(add(lobby_checks.data(),nullptr,"Overflow",false)==-1);
        clear(lobby_names.data(),nullptr);assert(*reinterpret_cast<unsigned*>(0x8abe20)==64);
        clear(lobby_checks.data(),nullptr);assert(*reinterpret_cast<unsigned*>(0x8abe20)==0);
    }
    std::cout<<"r4 pool PASS 12 actual retail add/clear cycles; 128 shared rows; 83-byte payloads; pool fully released\n";
}
void lobby_tests(const script_functions_rules::func_entry* registration) {
    const bool host=mode!="r4-join";setup_lobby(host);
    auto tick=*reinterpret_cast<lobby_tick_t*>(host ? 0x69f2ac : 0x69f374);
    auto activate=*reinterpret_cast<lobby_activate_t*>(host ? 0x69f284 : 0x69f350);
    auto back=*reinterpret_cast<lobby_back_t*>(host ? 0x69f2a0 : 0x69f368);
    auto post=[&](intent action) {
        int_value[1]=unsigned(action);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(reinterpret_cast<void*>(1));
    };
    activate(child.data(),nullptr,0);assert(activations==1 && refreshes==1);
    mask=0;tick(child.data(),nullptr);
    assert(texts["lc_p1_name"]=="Logan?n?" && texts["lc_p2_name"]=="Kitty?n");
    assert(texts["lc_room_code"]=="ABCD-EFGH" && texts["lc_ready_count"]=="2" && texts["lc_member_count"]=="3");
    assert(texts["lc_waiting_name"]=="Alex" && !visibility["lc_ready_waiting"]);
    assert(!visibility["lc_p3_hero_fallback"] && texts[host ? "lc_p2_ping" : "lc_p1_ping"]==(host ? "38" : "0"));
    const auto pool=*reinterpret_cast<unsigned*>(0x8abe20);
    assert(std::string_view(reinterpret_cast<char*>(0x8a83f4))=="Logan?n?");
    for(unsigned i=0;i<40;++i) tick(child.data(),nullptr);
    assert(*reinterpret_cast<unsigned*>(0x8abe20)==pool && names_table[0]==0 && checks_table[0]==4);
    if(mode=="r4-copy") { copy_tests(registration,tick);return; }
    if(mode=="r4-pool") { pool_tests();return; }
    if(mode=="r4-host") {
        people_count=4;setup_lobby(true);activate(child.data(),nullptr,0);tick(child.data(),nullptr);
        assert(texts["lc_p4_name"]=="Storm" && texts["lc_member_count"]=="4" && !visibility["lc_p4_card"]);
        assert(!visibility["lc_p1_host"] && visibility["lc_p2_host"]);
        people_count=3;setup_lobby(true);activate(child.data(),nullptr,0);tick(child.data(),nullptr);
    }
    if(mode=="r4-negative") {
        missing_list=true;activate(child.data(),nullptr,0);assert(activations==1 && !visibility["lc_lobby_error"]);
        mask=0x10;tick(child.data(),nullptr);assert(!transfers && !broadcasts && current()==child.data());missing_list=false;
        missing_ready_list=true;activate(child.data(),nullptr,0);assert(activations==1);missing_ready_list=false;
        auto reject=[&]() { mask=0x10;tick(child.data(),nullptr);assert(!transfers && !broadcasts && current()==child.data()); };
        put(names_table.data(),0x84,5);reject();put(names_table.data(),0x84,3);
        put(checks_table.data(),0x84,2);reject();put(checks_table.data(),0x84,3);
        put(reinterpret_cast<void*>(0x8a83f0+0x70),0x5c,42);reject();put(reinterpret_cast<void*>(0x8a83f0+0x70),0x5c,7);
        put(reinterpret_cast<void*>(0x8a83f0+0x70),0x5c,123456);reject();put(reinterpret_cast<void*>(0x8a83f0+0x70),0x5c,7);
        put(lobby_people[1].data(),0x18,99);reject();put(lobby_people[1].data(),0x18,7);
        names_table[0]=255;reject();names_table[0]=0;
        checks_table[0]=0;reject();checks_table[0]=4;
        put(reinterpret_cast<void*>(0x8abe20),0,129);reject();put(reinterpret_cast<void*>(0x8abe20),0,pool);
        // Missing skin marker forwards native ticks, no extra card bindings.
        lobby_marker=false;mask=0;auto writes=text_writes;tick(child.data(),nullptr);assert(text_writes==writes);
        std::cout<<"r4 negative PASS missing-list activation refused; counts/IDs/duplicate pool slots/invalid pool refused; no close\n";return;
    }
    if(mode=="r4-join") {
        ready_person(1,false);mask=0x1000000;tick(child.data(),nullptr);assert(announces==1 && last_ready && current()==child.data());
        tick(child.data(),nullptr);assert(announces==1);mask=0;tick(child.data(),nullptr);
        fixture_throw=true;tick(child.data(),nullptr);fixture_throw=false;
        back_in_base=true;mask=0x200000;tick(child.data(),nullptr);back_in_base=false;mask=0;
        assert(announces==1 && last_ready && current()==child.data());
        assert(!visibility["lc_guest_unready"] && visibility["lc_guest_leave"]);
        // Invoke the original native Back to independently prove Unready then
        // leave ordering, without replacing it with the close-service stub.
        auto native_back=reinterpret_cast<lobby_back_t>(0x5c8c60);
        put(child.data(),0x324,0);assert(native_back(child.data(),nullptr)==2);assert(!last_ready && announces==2 && current()==child.data());
        mask=0;tick(child.data(),nullptr);assert(visibility["lc_guest_unready"] && !visibility["lc_guest_leave"]);
        assert(native_back(child.data(),nullptr)==2 && current()==nullptr && child[0x196e]==1 && leaves==1 && cleanups==1);
        set_current(child.data());child[0x196e]=0;focus_stub(child.data(),nullptr,"text_readygame",true);
        mask=0x10;tick(child.data(),nullptr);assert(last_ready);
        focus_stub(child.data(),nullptr,"text_leaveroom",true);tick(child.data(),nullptr);assert(!last_ready && current()==child.data());
        tick(child.data(),nullptr);assert(current()==nullptr && leaves==2 && cleanups==2);
        set_current(child.data());admitted=false;auto announcements=announces;tick(child.data(),nullptr);
        assert(announces==announcements && current()==nullptr && std::find(calls.begin(),calls.end(),"native-admission-error")!=calls.end());
        std::cout<<"r4 join PASS actual native activation, admission-before-ready, Ready, Unready, Back and Leave\n";return;
    }
    assert(!enabled_items["text_startgame"]);mask=0x10;tick(child.data(),nullptr);assert(!transfers && !broadcasts);
    ready_person(2,true);mask=0;tick(child.data(),nullptr);assert(enabled_items["text_startgame"] && !visibility["lc_ready_all"]);
    mask=0x10;image_success=false;tick(child.data(),nullptr);assert(transfers==1 && !child[0x1970]);
    image_success=true;tick(child.data(),nullptr);assert(transfers==2 && child[0x1970]);
    child[0x1970]=0;image_present=false;tick(child.data(),nullptr);assert(broadcasts==1 && starts==1);
    people_count=1;put(names_table.data(),0x84,1);put(checks_table.data(),0x84,1);put(people_table.data(),0xc,1);
    mask=0;tick(child.data(),nullptr);assert(enabled_items["text_startgame"] && !visibility["lc_ready_solo"]);
    mask=0x10;tick(child.data(),nullptr);assert(starts==2);people_count=3;
    put(names_table.data(),0x84,3);put(checks_table.data(),0x84,3);put(people_table.data(),0xc,3);
    mask=0x1000000;tick(child.data(),nullptr);
    assert(*reinterpret_cast<int*>(child.data()+0x1960)==1 && focused=="text_playername1" && !visibility["text_playername1"]);
    mask=0x10;tick(child.data(),nullptr);assert(kicks==0 && current()==child.data()); // local row 0 refused
    *reinterpret_cast<short*>(lobby_names.data()+0xac)=-1;tick(child.data(),nullptr);assert(!kicks);
    *reinterpret_cast<short*>(lobby_names.data()+0xac)=3;tick(child.data(),nullptr);assert(!kicks);
    *reinterpret_cast<short*>(lobby_names.data()+0xac)=1;base_mutation=invalidate_selection;tick(child.data(),nullptr);base_mutation=nullptr;
    assert(!kicks && *reinterpret_cast<int*>(child.data()+0x1960)==1 && !visibility["lc_lobby_error"]);
    *reinterpret_cast<short*>(lobby_names.data()+0xac)=1;tick(child.data(),nullptr);assert(kicks==1 && *reinterpret_cast<int*>(child.data()+0x1960)==0);
    back_in_base=true;mask=0x200000;tick(child.data(),nullptr);back_in_base=false;
    assert(current()==child.data() && *reinterpret_cast<int*>(child.data()+0x1960)==0); // dismiss error, no Leave
    mask=0x10;
    focus_stub(child.data(),nullptr,"text_kickplayer",true);tick(child.data(),nullptr);
    mask=0x200000;tick(child.data(),nullptr);assert(*reinterpret_cast<int*>(child.data()+0x1960)==0 && current()==child.data());
    // Error dismissal and host leave confirmation stay within the native menu.
    mask=0;post(intent::dismiss);tick(child.data(),nullptr);tick(child.data(),nullptr);
    *reinterpret_cast<int*>(child.data()+0x1960)=2;tick(child.data(),nullptr);
    assert(!leaves && current()==child.data() && !visibility["lc_leave_confirm"] && focused=="lc_leave_cancel");
    post(intent::dismiss);tick(child.data(),nullptr);
    assert(visibility["lc_leave_confirm"] && !enabled_items["lc_leave_yes"] && focused=="text_leaveroom");
    focus_stub(child.data(),nullptr,"text_leaveroom",true);mask=0x10;tick(child.data(),nullptr);assert(current()==child.data() && !leaves);
    mask=0;tick(child.data(),nullptr);assert(!visibility["lc_leave_confirm"]);
    back(child.data(),nullptr);tick(child.data(),nullptr);assert(visibility["lc_leave_confirm"] && current()==child.data());
    mask=0x200000;tick(child.data(),nullptr);mask=0;tick(child.data(),nullptr);assert(!visibility["lc_leave_confirm"]);
    post(intent::confirm_leave);tick(child.data(),nullptr);assert(current()==child.data() && !leaves);
    tick(child.data(),nullptr);assert(current()==nullptr && leaves==1);
    assert(*reinterpret_cast<unsigned*>(0x8abe20)==pool);
    std::cout<<"r4 host PASS retail Start unready/solo/image failure+success/new-game broadcast; native Kick picker/self/stale-row/Back; inline cancel/confirm; zero added rows\n";
}
