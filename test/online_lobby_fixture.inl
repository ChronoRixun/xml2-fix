// Included in online_menus_test.cpp's fixture namespace. All retail routines
// below execute in the mapped owned PE; engine services/participants are fake.
void* current() { return *reinterpret_cast<void**>(mgr.data()+0x86090); }
std::array<std::array<unsigned char,0xc0>,4> lobby_people{};
std::array<unsigned char,0x90> names_table{},checks_table{};
auto& people_table=*reinterpret_cast<std::array<unsigned char,0x90>*>(0xa53058+0x26c);
std::array<unsigned char*,4> people_nodes{};
alignas(16) std::array<unsigned char,0x3dac> participant_node_pool{};
void* __cdecl participant_node_pool_getter() { return participant_node_pool.data(); }
void initialize_node_pool() {
    participant_node_pool.fill(0);
    for(unsigned i=0;i<650;++i) put(participant_node_pool.data(),0x331c+4*i,i);
    put(participant_node_pool.data(),0x3d50,650);
}
bool pending_local=false,hold_native_close=false;
unsigned people_count=3,local_id=42;
unsigned participant_ids[]{42,7,99,2};
std::string focused="text_startgame";
bool host_role=true,image_present=true,image_success=true,admitted=true;
int broadcasts=0,transfers=0,starts=0,kicks=0,leaves=0,announces=0,refreshes=0,activations=0,cleanups=0;
bool last_ready=false;
bool fixture_throw=false,fixture_bad_code=false;
void* participant(unsigned id) {
    for(unsigned i=0;i<people_count;++i) if(participant_ids[i]==id) return lobby_people[i].data();
    return nullptr;
}
bool __fastcall host_role_stub(void*,void*) { lobby_fault_at(3);return host_role; }
void __fastcall no_op(void*,void*) {}
void __fastcall activated(void*,void*,int) { ++activations; }
void* __cdecl cast_list(void* p,int,void*,void*,int) { return p; }
const char* __fastcall focused_name(void*,void*) { return focused.c_str(); }
void __fastcall focus_stub(void* menu,void*,const char* name,bool) {
    lobby_fault_at(2);focused=name;put(menu,0x324,unsigned(reinterpret_cast<std::uintptr_t>(item_stub(menu,nullptr,name))));
}
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
void __fastcall manager_close(void*,void*,const char* name,int) {
    assert(name && (std::string_view(name)=="host" || std::string_view(name)=="join"));if(hold_native_close) return;
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
const char* __fastcall locale_text(void*,void*,int id) { return id==0xfbd ? "Unready" : id==0xfc4 ? "Host" : id==0xfc0 ? "Ready" : id==0xfa2 ? "Waiting" : "Leave"; }
std::array<unsigned,4> dialog_vt{};
unsigned* dialog_object=dialog_vt.data();
void* __cdecl dialog_getter() { return &dialog_object; }
void __fastcall show_dialog(void*,void*,const char*,float,int) { calls.push_back("native-admission-error"); }
bool lobby_fields_stub(online_menus::lobby_fixture& out) {
    if(fixture_throw) throw std::bad_alloc();
    std::strcpy(out.code,fixture_bad_code ? "BAD" : "ABCD-EFGH");out.count=3;
    out.members[0]={99,112,false};out.members[1]={42,0,true};out.members[2]={7,38,true};return true;
}
void ready_person(unsigned row,bool value) {
    put(lobby_people[row].data(),0x1c,(value ? 4 : 0) | (row==0 ? 2 : 0) | (admitted && participant_ids[row]==local_id ? 1 : 0));
}
void set_admission(bool value) {
    admitted=value;put(reinterpret_cast<void*>(0xa53058),0x78,0);
    for(unsigned row=0;row<4;++row) ready_person(row,(*reinterpret_cast<unsigned*>(lobby_people[row].data()+0x1c)&4)!=0);
}
void set_pending_local(bool value) {
    pending_local=value;
    put(reinterpret_cast<void*>(0xa53058),0x78,value ? unsigned(reinterpret_cast<std::uintptr_t>(lobby_people[1].data())) : 0);
}
void build_people_chain() {
    people_table.fill(0);people_nodes.fill(nullptr);initialize_node_pool();
    put(reinterpret_cast<void*>(0xa53058),0x78,0);
    redirect(0x6067d0,participant_node_pool_getter); // only backing-store singleton is fake
    auto insert=reinterpret_cast<bool(__fastcall*)(void*,void*,void*,unsigned)>(0x611e00);
    // Real insert calls the real allocator and sorts these reversed inserts.
    for(unsigned remaining=people_count;remaining;--remaining) {
        const auto i=remaining-1;assert(insert(people_table.data(),nullptr,lobby_people[i].data(),i));
    }
    auto* node=*reinterpret_cast<unsigned char**>(people_table.data());unsigned walked=0;
    unsigned char* previous=nullptr;
    while(node) {
        assert(walked<people_count && *reinterpret_cast<unsigned char**>(node+0xc)==previous);
        assert(*reinterpret_cast<void**>(node+0x10)==lobby_people[walked].data());
        people_nodes[walked++]=node;previous=node;node=*reinterpret_cast<unsigned char**>(node+8);
    }
    assert(walked==people_count && *reinterpret_cast<void**>(people_table.data()+4)==previous);
    assert(*reinterpret_cast<unsigned*>(participant_node_pool.data()+0x3da8)==people_count);
    if(people_count) assert(!*reinterpret_cast<void**>(people_nodes[0]+0xc) && !*reinterpret_cast<void**>(people_nodes[people_count-1]+8));
}
void __fastcall native_name_presentation(void*,void*,const char* source,char* output,unsigned size) {
    auto getter=reinterpret_cast<const char*(__fastcall*)(void*,void*)>(0x6141e0);
    assert(getter(lobby_people[0].data(),nullptr)==reinterpret_cast<char*>(lobby_people[0].data()+0x40)); // even inside UI scope, other consumers retain native pointers
    if(lobby_marker) assert(std::string_view(source).find_first_of("^%\\")==std::string_view::npos); // before the real refresh formats or measures it
    if(std::string_view(source).starts_with("Logan")) ++refreshes;
    std::strncpy(output,source,size-1);output[size-1]=0;
}
int __cdecl native_format(char* output,unsigned size,const char* format,...) {
    va_list args;va_start(args,format);const auto n=std::vsnprintf(output,size,format,args);va_end(args);return n;
}
void __fastcall columns_without_font(void*,void*,const char*,char*,int,char** columns,int count) { for(int i=0;i<count;++i) columns[i]=nullptr; }
std::array<unsigned,12> peer_ui_vt{};
unsigned* peer_ui_object=peer_ui_vt.data();
void* __cdecl peer_ui_getter() { return &peer_ui_object; }
bool __fastcall peer_is_local(void*,void*,void* record) { return record==participant(local_id); }
std::array<float,4> ui_color{1,1,1,1};
unsigned __fastcall color_value(void*,void*,int) { return unsigned(reinterpret_cast<std::uintptr_t>(ui_color.data())); }
void refresh_now() {
    auto refresh=*reinterpret_cast<void(__fastcall**)(void*,void*)>(*reinterpret_cast<unsigned*>(child.data())+0xac);
    refresh(child.data(),nullptr);
}
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
    build_people_chain();
    std::memset(reinterpret_cast<void*>(0x8a83f0),0,0x3a34);
    reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5bfac0)(reinterpret_cast<void*>(0x8a83f0),nullptr);
    child.fill(0);put(child.data(),0,host ? 0x69f274 : 0x69f33c);child[4]=1;child[0x195c]=1;
    std::strcpy(reinterpret_cast<char*>(child.data()+0xc),host ? "host" : "join");
    put(child.data(),host ? 0x1964 : 0x1960,unsigned(reinterpret_cast<std::uintptr_t>(lobby_names.data())));
    put(child.data(),host ? 0x1968 : 0x1964,unsigned(reinterpret_cast<std::uintptr_t>(lobby_checks.data())));
    put(child.data(),host ? 0x196c : 0x1968,unsigned(reinterpret_cast<std::uintptr_t>(people_table.data())));
    child[0x196f]=1;
    for(auto* control:{&start_item,&kick_item,&leave_item,&ready_item}) { control->fill(0);put(control->data(),0,0x6a0244);(*control)[0x54]=8; }
    back_help_item.fill(0);put(back_help_item.data(),0,0x6a0244);back_help_item[0x54]=8;
    auto* back_rect=reinterpret_cast<short*>(back_help_item.data()+0x70);back_rect[0]=500;back_rect[1]=470;back_rect[2]=100;back_rect[3]=20;
    unsigned index=0;
    for(auto* control:{&start_item,&kick_item,&leave_item}) {
        auto* geometry=reinterpret_cast<short*>(control->data()+0x70);geometry[0]=short(20+140*index++);geometry[1]=430;geometry[2]=100;geometry[3]=20;
    }
    focus_stub(child.data(),nullptr,host ? "text_startgame" : "text_readygame",true);set_current(child.data());
    lobby_back_callback=*reinterpret_cast<int(__fastcall**)(void*,void*)>(host ? 0x69f2a0 : 0x69f368);
    redirect(0x610d10,host_role_stub);

    redirect(0x5caff0,activated);redirect(0x67234e,cast_list);
    // The actual Host/Join refreshes, add wrapper, base add, clear, row ctor
    // and setRowValue all execute. Only font/locale/session presentation is fake.
    redirect(0x672170,reinterpret_cast<const void*>(&std::strncpy));redirect(0x672161,no_op);
    redirect(0x610e70,native_name_presentation);redirect(0x5604f0,native_format);redirect(0x5c0be0,columns_without_font);
    peer_ui_vt[0x28/4]=unsigned(reinterpret_cast<std::uintptr_t>(&peer_is_local));redirect(0x615580,peer_ui_getter);
    const auto color=*reinterpret_cast<unsigned*>(0x6a236c+0x224);redirect(color,color_value);
    lobby_names[0xdb]=lobby_checks[0xdb]=4; // real +c4 clamp/scroll uses a parsed visible-row count
    put(reinterpret_cast<void*>(0x67f230),0,unsigned(reinterpret_cast<std::uintptr_t>(&PtInRect)));
    redirect(0x5ab550,focused_name);redirect(0x5adf40,focus_stub);
    redirect(0x5d4410,sound_stub);redirect(0x5bc6b0,description_stub);redirect(0x5ab030,no_op);
    // Preserve the class vtable addresses while stubbing its presentation and
    // close services; the action ticks and ready-aware Back remain retail.
    put(reinterpret_cast<void*>(host ? 0x69f274 : 0x69f33c),0x68,unsigned(reinterpret_cast<std::uintptr_t>(&no_op)));

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
void observe_native_render(bool host) {
    const bool hidden=(lobby_names[0x54]&4)!=0;
    if(!host) { assert(hidden);return; }
    const auto count=*reinterpret_cast<unsigned*>(names_table.data()+0x84);
    auto text=reinterpret_cast<const char*(__fastcall*)(void*,void*,int)>(0x5bfcd0);
    for(unsigned i=0;i<count;++i) assert(std::string_view(text(lobby_names.data(),nullptr,int(i))).find_first_of("^%\\~")==std::string_view::npos);
    if(mode=="r4-refresh-paths") std::cout<<"render boundary HOST hidden="<<hidden<<" rows="<<count<<" first="<<(count ? text(lobby_names.data(),nullptr,0) : "")<<"\n";
}
void restore_retail(unsigned va) {
    for(const auto& guard:online_menus_guards::guards) if(guard.va==va) {
        const auto bytes=patch_transaction::bytes_of(guard.hex);std::memcpy(reinterpret_cast<void*>(va),bytes.data(),bytes.size());FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(va),bytes.size());return;
    }
    assert(false);
}
const char* __fastcall row_text_fault(void*,void*,int) { RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return nullptr; }
using lobby_mouse_t=bool(__fastcall*)(void*,void*,int,int,int,int,int);
void mouse_release(unsigned slot,unsigned x) {
    auto mouse=*reinterpret_cast<lobby_mouse_t*>(slot);
    put(reinterpret_cast<void*>(0x6f3920),0,0);put(reinterpret_cast<void*>(0xa09f54),0,0);*reinterpret_cast<unsigned char*>(0xa09fa0)=0;
    assert(mouse(child.data(),nullptr,int(x),420,0,WM_LBUTTONUP,0));
    assert(*reinterpret_cast<unsigned char*>(0xa09fa0)==1 && *reinterpret_cast<unsigned*>(0xa09f54)==4);
    // Fake input provider consumes the native mouse event, just as the frame
    // input pump does. The action itself is the actual retail class tick.
    put(reinterpret_cast<void*>(0xa09f54),0,0);*reinterpret_cast<unsigned char*>(0xa09fa0)=0;mask=0x10;
}
void install_copy_failure() {
    copy_open_ok=false;
    import_stub("OpenClipboard",reinterpret_cast<void*>(&copy_open));import_stub("EnumThreadWindows",reinterpret_cast<void*>(&copy_enum));
    import_stub("GetWindowThreadProcessId",reinterpret_cast<void*>(&copy_window_process));import_stub("GetClassNameA",reinterpret_cast<void*>(&copy_class));
}
void retail_list_review_tests(const script_functions_rules::func_entry* registration) {
    auto post=[&](intent action) { int_value[1]=unsigned(action);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr); };
    auto activate=*reinterpret_cast<lobby_activate_t*>(0x69f284);
    auto tick=*reinterpret_cast<lobby_tick_t*>(0x69f2ac);
    auto back=*reinterpret_cast<lobby_back_t*>(0x69f2a0);
    if(mode=="r4-retail-list") {
        auto lookup=reinterpret_cast<void*(__fastcall*)(void*,void*,unsigned)>(0x6110c0);
        auto local=reinterpret_cast<void*(__fastcall*)(void*,void*)>(0x6111f0);
        for(bool host:{true,false}) for(unsigned count=1;count<=4;++count) {
            people_count=count;setup_lobby(host);activate=*reinterpret_cast<lobby_activate_t*>(host ? 0x69f284 : 0x69f350);
            activate(child.data(),nullptr,0);
            assert(texts["lc_member_count"]==std::to_string(count)); // fails on 2454786's terminal-sentinel predicate
            assert(*reinterpret_cast<void**>(people_table.data()+8)==nullptr); // adapter restores native refresh's iterator cursor
            for(unsigned i=0;i<count;++i) assert(lookup(reinterpret_cast<void*>(0xa53058),nullptr,participant_ids[i])==lobby_people[i].data());
            assert(lookup(reinterpret_cast<void*>(0xa53058),nullptr,0xfeedbeef)==nullptr); // walks the entire real NULL-terminated chain
            put(reinterpret_cast<void*>(0xa53058),0x78,0);
            assert(local(reinterpret_cast<void*>(0xa53058),nullptr)==(host ? lobby_people[0].data() : count>1 ? lobby_people[1].data() : nullptr));
            assert(!*reinterpret_cast<void**>(people_nodes[count-1]+8));
            std::cout<<"retail list "<<(host ? "Host" : "Join")<<" count="<<count<<" tail.next=NULL allocator nodes="<<*reinterpret_cast<unsigned*>(participant_node_pool.data()+0x3da8)<<" real lookup/missing/local PASS\n";
        }
        std::cout<<"retail list regression PASS real 0x611da0/0x611e00 sorted insertion and unstubbed 0x6110c0/0x6111f0; real LISTBOX +c4\n";return;
    }
    if(mode=="r4-back-combos") {
        setup_lobby(true);activate(child.data(),nullptr,0);ready_person(2,true);refresh_now();mask=0;tick(child.data(),nullptr);
        install_copy_failure();focus_stub(child.data(),nullptr,"text_startgame",true);post(intent::copy_code);mask=0x200010;tick(child.data(),nullptr);
        assert(!transfers && !starts && !broadcasts && !kicks); // Copy suppression must not leak Accept through Back
        mask=0;post(intent::dismiss);tick(child.data(),nullptr);
        put(child.data(),0x1960,1);lobby_names[0x54]|=8;focus_stub(child.data(),nullptr,"text_playername1",true);
        *reinterpret_cast<short*>(lobby_names.data()+0xac)=0;mask=0x200010;tick(child.data(),nullptr);
        assert(!kicks && *reinterpret_cast<int*>(child.data()+0x1960)==0); // self/unknown target still allows only Back
        std::cout<<"Back+Accept regression PASS modal/tool/Copy and picker target suppression cannot be bypassed; no Start/Kick\n";return;
    }
    if(mode=="r4-unknown-kick") {
        participant_ids[2]=0;setup_lobby(true);activate(child.data(),nullptr,0);
        put(child.data(),0x1960,1);lobby_names[0x54]|=8;focus_stub(child.data(),nullptr,"text_playername1",true);
        *reinterpret_cast<short*>(lobby_names.data()+0xac)=-1;put(checks_table.data(),0x84,2);
        mask=0x10;tick(child.data(),nullptr);assert(!kicks); // native selectedValue would otherwise alias remote index zero
        mask=0x200010;tick(child.data(),nullptr);assert(!kicks && *reinterpret_cast<int*>(child.data()+0x1960)==0);
        refresh_now();put(child.data(),0x1960,1);lobby_names[0x54]|=8;focus_stub(child.data(),nullptr,"text_playername1",true);
        *reinterpret_cast<short*>(lobby_names.data()+0xac)=1;set_admission(false);mask=0x10;tick(child.data(),nullptr);assert(!kicks);
        mask=0x200000;tick(child.data(),nullptr);assert(*reinterpret_cast<int*>(child.data()+0x1960)==0);
        participant_ids[2]=99;set_admission(true);
        std::cout<<"Kick regression PASS invalid view/selection cannot alias remote zero; unknown local cannot authorize Kick; Back exits\n";return;
    }
    if(mode=="r4-dismiss-invalid") {
        setup_lobby(false);activate=*reinterpret_cast<lobby_activate_t*>(0x69f350);activate(child.data(),nullptr,0);
        tick=*reinterpret_cast<lobby_tick_t*>(0x69f374);back=*reinterpret_cast<lobby_back_t*>(0x69f368);
        ready_person(1,false);install_copy_failure();mask=0;post(intent::copy_code);tick(child.data(),nullptr);assert(!visibility["lc_lobby_error"]);
        put(checks_table.data(),0x84,2);assert(back(child.data(),nullptr)==2 && current()==child.data() && !leaves);
        tick(child.data(),nullptr);assert(visibility["lc_lobby_error"] && focused=="text_readygame");
        refresh_now();back(child.data(),nullptr);assert(current()==nullptr && leaves==1);
        std::cout<<"notice regression PASS invalid roster cannot prevent dismissal; first Back dismisses, later native Back leaves\n";return;
    }
    assert(false);
}
void review_lobby_tests(const script_functions_rules::func_entry* registration,bool host) {
    auto tick=*reinterpret_cast<lobby_tick_t*>(host ? 0x69f2ac : 0x69f374);
    auto activate=*reinterpret_cast<lobby_activate_t*>(host ? 0x69f284 : 0x69f350);
    auto back=*reinterpret_cast<lobby_back_t*>(host ? 0x69f2a0 : 0x69f368);
    auto post=[&](intent action) { int_value[1]=unsigned(action);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr); };
    if(mode=="r4-refresh-paths") {
        for(bool hosting:{true,false}) {
            people_count=4;setup_lobby(hosting);activate=*reinterpret_cast<lobby_activate_t*>(hosting ? 0x69f284 : 0x69f350);activate(child.data(),nullptr,0);
            tick=*reinterpret_cast<lobby_tick_t*>(hosting ? 0x69f2ac : 0x69f374);
            auto value=reinterpret_cast<unsigned(__fastcall*)(void*,void*,int)>(0x5bfc20);
            for(int i=0;i<4;++i) assert(value(lobby_names.data(),nullptr,i)==(hosting ? participant_ids[i] : 0));
            assert(std::string_view(reinterpret_cast<const char*>(lobby_people[0].data()+0x40))=="Logan^n%"); // native names are never changed
            for(unsigned slot:{0xa0u,0xacu,0xb4u}) {
                if(hosting) { put(child.data(),0x1960,1);lobby_names[0x54]|=8; }
                auto refresh=*reinterpret_cast<lobby_tick_t*>(*reinterpret_cast<unsigned*>(child.data())+slot);
                refresh(child.data(),nullptr);observe_native_render(hosting);
                assert(*reinterpret_cast<unsigned*>(0x8abe20)==8);
            }
            if(hosting) put(child.data(),0x1960,0);
            mask=0;tick(child.data(),nullptr);assert(texts["lc_member_count"]=="4");
            if(!hosting) {
                put(people_nodes[0],0x10,unsigned(reinterpret_cast<std::uintptr_t>(lobby_people[2].data())));
                put(people_nodes[1],0x10,unsigned(reinterpret_cast<std::uintptr_t>(lobby_people[0].data())));
                put(people_nodes[2],0x10,unsigned(reinterpret_cast<std::uintptr_t>(lobby_people[1].data())));
                refresh_now();tick(child.data(),nullptr);assert(texts["lc_p1_name"]=="Alex" && texts["lc_p2_name"]=="Logan?n?");
            }
            people_count=0;build_people_chain();refresh_now();observe_native_render(hosting);
            assert(*reinterpret_cast<unsigned*>(names_table.data()+0x84)==1 && *reinterpret_cast<unsigned*>(checks_table.data()+0x84)==0);
            assert(value(lobby_names.data(),nullptr,0)==(hosting ? 0xffffffffu : 0u));
            if(!hosting) { set_pending_local(true);put(lobby_people[1].data(),0x1c,0);child[0x196c]=1; }
            tick(child.data(),nullptr);set_pending_local(false);assert(texts["lc_member_count"]=="0" && visibility["lc_ready_all"]);
        }
        std::cout<<"review refresh PASS REAL Host/Join refresh + add/clear/ctor/value writes; Join values all zero; participant order, 4/empty rosters; render-boundary sanitation\n";return;
    }
    if(mode=="r4-join-fallback") {
        put(checks_table.data(),0x84,2);ready_person(1,false);focus_stub(child.data(),nullptr,"text_readygame",true);mask=0x10;tick(child.data(),nullptr);
        assert(announces==1 && last_ready && current()==child.data()); // invalid view does not veto Ready
        assert(visibility["lc_lobby_error"]); // no latched read error
        mask=0;
        // Explicit notice while the REAL native Join tick tail-dispatches Back.
        copy_open_ok=false;import_stub("OpenClipboard",reinterpret_cast<void*>(&copy_open));import_stub("EnumThreadWindows",reinterpret_cast<void*>(&copy_enum));
        import_stub("GetWindowThreadProcessId",reinterpret_cast<void*>(&copy_window_process));import_stub("GetClassNameA",reinterpret_cast<void*>(&copy_class));
        post(intent::copy_code);tick(child.data(),nullptr);assert(!visibility["lc_lobby_error"]);
        mask=0;tick(child.data(),nullptr); // release Copy before a fresh native Accept
        focus_stub(child.data(),nullptr,"text_leaveroom",true);mask=0x10;tick(child.data(),nullptr);assert(!last_ready && current()==child.data());
        tick(child.data(),nullptr);assert(current()==nullptr && leaves==1); // notice cannot swallow the native Leave dispatch
        setup_lobby(false);ready_person(1,false);activate(child.data(),nullptr,0);mask=0;
        post(intent::copy_code);tick(child.data(),nullptr);assert(!visibility["lc_lobby_error"]);
        auto mouse=*reinterpret_cast<lobby_mouse_t*>(0x69f3bc);
        assert(mouse(child.data(),nullptr,510,460,0,WM_LBUTTONUP,0));
        assert(current()==nullptr && leaves==2); // actual mouse help -> virtual Back, with the notice still showing
        people_count=0;setup_lobby(false);set_pending_local(true);put(lobby_people[1].data(),0x1c,0);activate(child.data(),nullptr,0);child[0x196c]=1;mask=0;tick(child.data(),nullptr);
        assert(texts["lc_member_count"]=="0" && visibility["lc_lobby_error"] && current()==child.data());
        child[0x196c]=0;const auto pending_announcements=announces;mask=0x1000000;tick(child.data(),nullptr);
        assert(announces==pending_announcements && current()==child.data());mask=0;
        back(child.data(),nullptr);assert(current()==nullptr);set_pending_local(false);
        people_count=3;participant_ids[0]=0;setup_lobby(false);set_admission(false);activate(child.data(),nullptr,0);
        // No-local is not index zero: the remote zero-index member is not You.
        assert(visibility["lc_p1_you"] && texts["lc_self_name"].empty());
        back(child.data(),nullptr);assert(current()==nullptr);participant_ids[0]=42;set_admission(true);
        std::cout<<"review Join fallback PASS zero-valued rows; invalid view Ready/Leave, native tail Back through notice; pending/empty exit; no-local versus index zero\n";return;
    }
    if(mode=="r4-host-fallback" || mode=="r4-negative") {
        // View failures must not latch an error or suppress native Back/Leave.
        for(int failure=0;failure<6;++failure) {
            people_count=3;setup_lobby(true);activate(child.data(),nullptr,0);mask=0;
            if(failure==0) put(checks_table.data(),0x84,2);
            if(failure==1) put(reinterpret_cast<void*>(0x8a83f0+names_table[1]*0x70),0x5c,42);
            if(failure==2) names_table[0]=255;
            if(failure==3) put(reinterpret_cast<void*>(0x8abe20),0,129);
            if(failure==4) put(people_table.data(),0xc,5);
            if(failure==5) put(lobby_people[1].data(),0x18,99);
            tick(child.data(),nullptr);assert(visibility["lc_lobby_error"] && (start_item[0x54]&8));
            focus_stub(child.data(),nullptr,"text_leaveroom",true);mask=0x10;tick(child.data(),nullptr);
            tick(child.data(),nullptr);assert(current()==nullptr);
        }
        const auto prior=activations;
        for(bool ready_missing:{false,true}) {
            people_count=3;setup_lobby(true);missing_list=!ready_missing;missing_ready_list=ready_missing;
            activate(child.data(),nullptr,0);assert(activations==prior && calls.back()=="request:online");
            missing_list=missing_ready_list=false;
        }
        // A consumed native state 2 cannot authorize a later departure.
        setup_lobby(true);activate(child.data(),nullptr,0);focus_stub(child.data(),nullptr,"text_leaveroom",true);mask=0x10;tick(child.data(),nullptr);
        mask=0;post(intent::confirm_leave);tick(child.data(),nullptr);hold_native_close=true;tick(child.data(),nullptr);hold_native_close=false;
        const auto count=leaves;mask=0x200000;tick(child.data(),nullptr);assert(leaves==count && !visibility["lc_leave_confirm"]);
        back(child.data(),nullptr);mask=0;tick(child.data(),nullptr);assert(focused=="text_startgame" && visibility["lc_leave_confirm"]);
        put(child.data(),0x1960,2);tick(child.data(),nullptr);post(intent::confirm_leave);tick(child.data(),nullptr);
        hold_native_close=true;lobby_fault_stage=3;tick(child.data(),nullptr);hold_native_close=false;assert(!lobby_fault_stage);
        const auto consumed_with_fault=leaves;put(child.data(),0x1960,2);tick(child.data(),nullptr);
        assert(leaves==consumed_with_fault && current()==child.data() && !visibility["lc_leave_confirm"]);
        std::cout<<"review Host fallback PASS invalid snapshots preserve native exit, malformed activation returns online, consumed consent re-confirms, safe restored focus\n";return;
    }
    if(mode=="r4-mouse") {
        ready_person(2,true);refresh_now();mask=0;tick(child.data(),nullptr);focus_stub(child.data(),nullptr,"text_startgame",true);
        mouse_release(0x69f2f4,30);tick(child.data(),nullptr);assert(transfers==1 && child[0x1970]);child[0x1970]=0;
        focus_stub(child.data(),nullptr,"text_kickplayer",true);mouse_release(0x69f2f4,170);tick(child.data(),nullptr);
        assert(*reinterpret_cast<int*>(child.data()+0x1960)==1);observe_native_render(true);
        mask=0x200000;tick(child.data(),nullptr);
        focus_stub(child.data(),nullptr,"text_leaveroom",true);mouse_release(0x69f2f4,310);tick(child.data(),nullptr);assert(!leaves && !visibility["lc_leave_confirm"]);
        mask=0;post(intent::confirm_leave);tick(child.data(),nullptr);tick(child.data(),nullptr);assert(current()==nullptr && leaves==1);
        std::cout<<"review mouse PASS REAL Host hit-test/release path -> native Start/image, Kick picker, Leave confirmation/native cleanup\n";return;
    }
    if(mode=="r4-native-faults") {
        for(int stage:{1,2,3,4}) {
            lobby_fault_stage=stage;mask=stage==2 ? 0x1000000 : 0;tick(child.data(),nullptr);
            assert(!lobby_fault_stage && current()==child.data());mask=0;tick(child.data(),nullptr);assert(visibility["lc_lobby_error"]);
        }
        put(child.data(),0x1960,1);lobby_names[0x54]|=8;redirect(0x5bfcd0,row_text_fault);refresh_now();assert(lobby_names[0x54]&4);restore_retail(0x5bfcd0);refresh_now();
        assert(!(lobby_names[0x54]&4));observe_native_render(true);
        auto host_tick=*reinterpret_cast<lobby_tick_t*>(0x69f2ac);host_tick(reinterpret_cast<void*>(1),nullptr); // raw menu fields contained
        mask=0x200000;tick(child.data(),nullptr);assert(*reinterpret_cast<int*>(child.data()+0x1960)==0);
        mask=0;tick(child.data(),nullptr);mask=0x200000;tick(child.data(),nullptr);assert(!visibility["lc_leave_confirm"]);
        std::cout<<"review native faults PASS contained mask/focus/session/authority/row-text/raw-menu accesses; native escape retained and picker hidden until safe\n";return;
    }
    if(mode=="r4-no-marker") {
        focus_stub(child.data(),nullptr,"text_leaveroom",true);mask=0x10;tick(child.data(),nullptr);mask=0;assert(!visibility["lc_leave_confirm"]);
        std::array<unsigned char,0x1a00> cached{};put(cached.data(),0,0x69f274);std::strcpy(reinterpret_cast<char*>(cached.data()+0xc),"host");
        lobby_marker=false;activate(cached.data(),nullptr,0);lobby_marker=true;tick(child.data(),nullptr);assert(!visibility["lc_leave_confirm"]);
        auto name=reinterpret_cast<const char*(__fastcall*)(void*,void*)>(0x6141e0);
        assert(name(lobby_people[0].data(),nullptr)==reinterpret_cast<char*>(lobby_people[0].data()+0x40));
        back(child.data(),nullptr);tick(child.data(),nullptr);assert(visibility["lc_leave_confirm"]);
        std::cout<<"review unmarked PASS cached unmarked activation retains current lobby state; native name getter unchanged outside refresh scope\n";return;
    }
    assert(false);
}
void lobby_tests(const script_functions_rules::func_entry* registration) {
    const bool host=mode.find("join")==std::string::npos;setup_lobby(host);
    auto tick=*reinterpret_cast<lobby_tick_t*>(host ? 0x69f2ac : 0x69f374);
    auto activate=*reinterpret_cast<lobby_activate_t*>(host ? 0x69f284 : 0x69f350);
    auto back=*reinterpret_cast<lobby_back_t*>(host ? 0x69f2a0 : 0x69f368);
    auto post=[&](intent action) {
        int_value[1]=unsigned(action);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(reinterpret_cast<void*>(1));
    };
    if(mode=="r4-retail-list" || mode=="r4-back-combos" || mode=="r4-unknown-kick" || mode=="r4-dismiss-invalid") {
        retail_list_review_tests(registration);return;
    }
    activate(child.data(),nullptr,0);assert(activations==1 && refreshes==1);
    mask=0;tick(child.data(),nullptr);
    assert(texts["lc_p1_name"]=="Logan?n?" && texts["lc_p2_name"]=="Kitty?n");
    assert(texts["lc_room_code"]=="ABCD-EFGH" && texts["lc_ready_count"]=="2" && texts["lc_member_count"]=="3");
    assert(texts["lc_waiting_name"]=="Alex" && !visibility["lc_ready_waiting"]);
    assert(!visibility["lc_p3_hero_fallback"] && texts[host ? "lc_p2_ping" : "lc_p1_ping"]==(host ? "38" : "0"));
    const auto pool=*reinterpret_cast<unsigned*>(0x8abe20);
    auto row_text=reinterpret_cast<const char*(__fastcall*)(void*,void*,int)>(0x5bfcd0);
    auto row_value=reinterpret_cast<unsigned(__fastcall*)(void*,void*,int)>(0x5bfc20);
    if(host) assert(std::string_view(row_text(lobby_names.data(),nullptr,0))=="Logan?n?" && row_value(lobby_names.data(),nullptr,0)==42);
    else {
        for(int i=0;i<3;++i) assert(row_value(lobby_names.data(),nullptr,i)==0); // real JOIN leaves every value at ctor default
        assert(std::string_view(row_text(lobby_names.data(),nullptr,0)).find("~")!=std::string_view::npos);
        assert(std::string_view(row_text(lobby_names.data(),nullptr,0)).find("Logan?n?")!=std::string_view::npos);
        assert(std::string_view(row_text(lobby_names.data(),nullptr,0)).find("Host")!=std::string_view::npos);
        assert(visibility["text_playername1"]); // raw native formatting never renders in JOIN
    }
    const auto name_backing=names_table,ready_backing=checks_table;
    for(unsigned i=0;i<40;++i) tick(child.data(),nullptr);
    assert(*reinterpret_cast<unsigned*>(0x8abe20)==pool && names_table==name_backing && checks_table==ready_backing);
    if(mode=="r4-copy") { copy_tests(registration,tick);return; }
    if(mode=="r4-pool") { pool_tests();return; }
    if(mode=="r4-host") {
        people_count=4;setup_lobby(true);activate(child.data(),nullptr,0);tick(child.data(),nullptr);
        assert(texts["lc_p4_name"]=="Storm" && texts["lc_member_count"]=="4" && !visibility["lc_p4_card"]);
        assert(!visibility["lc_p1_host"] && visibility["lc_p2_host"]);
        people_count=3;setup_lobby(true);activate(child.data(),nullptr,0);tick(child.data(),nullptr);
    }
    if(mode=="r4-negative" || mode=="r4-refresh-paths" || mode=="r4-join-fallback" || mode=="r4-host-fallback" || mode=="r4-mouse" || mode=="r4-native-faults" || mode=="r4-no-marker") {
        review_lobby_tests(registration,host);return;
    }
    if(mode=="r4-join") {
        ready_person(1,false);mask=0x1000000;tick(child.data(),nullptr);assert(announces==1 && last_ready && current()==child.data());
        tick(child.data(),nullptr);assert(announces==1);mask=0;tick(child.data(),nullptr);
        fixture_throw=true;tick(child.data(),nullptr);fixture_throw=false;
        back_in_base=true;mask=0x200000;tick(child.data(),nullptr);back_in_base=false;mask=0;
        assert(announces==2 && !last_ready && current()==child.data());
        ready_person(1,true);refresh_now();tick(child.data(),nullptr);
        assert(!visibility["lc_guest_unready"] && visibility["lc_guest_leave"]);
        // Invoke the original native Back to independently prove Unready then
        // leave ordering, without replacing it with the close-service stub.
        auto native_back=reinterpret_cast<lobby_back_t>(0x5c8c60);
        put(child.data(),0x324,0);assert(native_back(child.data(),nullptr)==2);assert(!last_ready && announces==3 && current()==child.data());
        mask=0;tick(child.data(),nullptr);assert(visibility["lc_guest_unready"] && !visibility["lc_guest_leave"]);
        assert(native_back(child.data(),nullptr)==2 && current()==nullptr && child[0x196e]==1 && leaves==1 && cleanups==1);
        set_current(child.data());child[0x196e]=0;focus_stub(child.data(),nullptr,"text_readygame",true);
        mask=0x10;tick(child.data(),nullptr);assert(last_ready);
        focus_stub(child.data(),nullptr,"text_leaveroom",true);tick(child.data(),nullptr);assert(!last_ready && current()==child.data());
        tick(child.data(),nullptr);assert(current()==nullptr && leaves==2 && cleanups==2);
        set_current(child.data());set_admission(false);auto announcements=announces;tick(child.data(),nullptr);
        assert(announces==announcements && current()==nullptr && std::find(calls.begin(),calls.end(),"native-admission-error")!=calls.end());
        std::cout<<"r4 join PASS actual native activation, admission-before-ready, Ready, Unready, Back and Leave\n";return;
    }
    assert(!enabled_items["text_startgame"]);mask=0x10;tick(child.data(),nullptr);assert(!transfers && !broadcasts);
    ready_person(2,true);mask=0;tick(child.data(),nullptr);assert(enabled_items["text_startgame"] && !visibility["lc_ready_all"]);
    mask=0x10;image_success=false;tick(child.data(),nullptr);assert(transfers==1 && !child[0x1970]);
    image_success=true;tick(child.data(),nullptr);assert(transfers==2 && child[0x1970]);
    child[0x1970]=0;image_present=false;tick(child.data(),nullptr);assert(broadcasts==1 && starts==1);
    people_count=1;build_people_chain();refresh_now();
    mask=0;tick(child.data(),nullptr);assert(enabled_items["text_startgame"] && !visibility["lc_ready_solo"]);
    mask=0x10;tick(child.data(),nullptr);assert(starts==2);people_count=3;
    build_people_chain();refresh_now();
    mask=0x1000000;tick(child.data(),nullptr);
    assert(*reinterpret_cast<int*>(child.data()+0x1960)==1 && focused=="text_playername1" && !visibility["text_playername1"]);
    mask=0x10;tick(child.data(),nullptr);assert(kicks==0 && current()==child.data()); // local row 0 refused
    *reinterpret_cast<short*>(lobby_names.data()+0xac)=-1;tick(child.data(),nullptr);assert(!kicks);
    *reinterpret_cast<short*>(lobby_names.data()+0xac)=3;tick(child.data(),nullptr);assert(!kicks);
    *reinterpret_cast<short*>(lobby_names.data()+0xac)=1;base_mutation=invalidate_selection;tick(child.data(),nullptr);base_mutation=nullptr;
    assert(!kicks && *reinterpret_cast<int*>(child.data()+0x1960)==1 && !visibility["lc_lobby_error"]);
    *reinterpret_cast<short*>(lobby_names.data()+0xac)=1;tick(child.data(),nullptr);assert(kicks==1 && *reinterpret_cast<int*>(child.data()+0x1960)==0);
    back_in_base=true;mask=0x200000;tick(child.data(),nullptr);back_in_base=false;
    assert(current()==child.data() && *reinterpret_cast<int*>(child.data()+0x1960)==0);
    post(intent::dismiss);mask=0;tick(child.data(),nullptr); // close the host Leave confirmation
    // A fresh picker can still be opened after a transient target error.
    mask=0x10;
    focus_stub(child.data(),nullptr,"text_kickplayer",true);tick(child.data(),nullptr);
    mask=0x200000;tick(child.data(),nullptr);assert(*reinterpret_cast<int*>(child.data()+0x1960)==0 && current()==child.data());
    // Error dismissal and host leave confirmation stay within the native menu.
    mask=0;post(intent::dismiss);tick(child.data(),nullptr);tick(child.data(),nullptr);
    *reinterpret_cast<int*>(child.data()+0x1960)=2;tick(child.data(),nullptr);
    assert(!leaves && current()==child.data() && !visibility["lc_leave_confirm"] && focused=="lc_leave_cancel");
    post(intent::dismiss);tick(child.data(),nullptr);
    assert(visibility["lc_leave_confirm"] && !enabled_items["lc_leave_yes"] && focused=="text_startgame");
    focus_stub(child.data(),nullptr,"text_leaveroom",true);mask=0x10;tick(child.data(),nullptr);assert(current()==child.data() && !leaves);
    mask=0;tick(child.data(),nullptr);assert(!visibility["lc_leave_confirm"]);
    back(child.data(),nullptr);tick(child.data(),nullptr);assert(visibility["lc_leave_confirm"] && current()==child.data());
    mask=0x200000;tick(child.data(),nullptr);mask=0;tick(child.data(),nullptr);assert(!visibility["lc_leave_confirm"]);
    post(intent::confirm_leave);tick(child.data(),nullptr);assert(current()==child.data() && !leaves);
    tick(child.data(),nullptr);assert(current()==nullptr && leaves==1);
    assert(*reinterpret_cast<unsigned*>(0x8abe20)==pool);
    std::cout<<"r4 host PASS retail Start unready/solo/image failure+success/new-game broadcast; native Kick picker/self/stale-row/Back; inline cancel/confirm; zero added rows\n";
}
