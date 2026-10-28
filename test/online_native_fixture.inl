// Real retail Host setup/post, discovery and admission paths with invented
// menu/dialog, save-image, participant, clock and transport services. No game.
namespace native_fixture {
using tick_t=void(__fastcall*)(void*,void*);
void retail_restore(unsigned va);
unsigned searches=0,posts=0,allocations=0,departures=0,freed=0,published=0,save_requests=0;
int maximum=0,difficulty=1;bool selected_danger_room=false;
bool tunnel_ready=false,allocate_ok=true,post_ok=true,save_accept=false,queue_ok=true,host_refuse=false;
std::array<unsigned,8> saves{};
unsigned* save_object=saves.data();
void* __cdecl save_get() { return &save_object; }
void __fastcall save_process(void*,void*,int operation) { assert(operation==3);++save_requests;if(save_accept) put(reinterpret_cast<void*>(0xa3bc40),0x2250,0x123400); }
std::array<unsigned char,0x2100> pool{};
std::array<unsigned char,0xc0> person{},peer{};
std::array<unsigned char,12> address{};
std::array<unsigned,40> dialogs{};
unsigned* dialog=dialogs.data();
std::vector<std::string> choices;
bool host_native(const std::string& name,bool visibility,int limit,bool danger_room) { maximum=limit;selected_danger_room=danger_room;if(host_refuse) return false;return menu_host(name,visibility); }
bool target(bm::ticket ticket,std::uint32_t& result) {
    result=0;if(!tunnel_ready || ticket!=menu_view().generation || menu_view().state!=bm::phase::room || !menu_view().authenticated) return false;
    result=0x0a4d0001;return true;
}
bool posted(bm::ticket ticket) { if(ticket!=menu_view().generation || !post_ok) return false;++published;return true; }
// The room layer's deferred leave: armed by the session-end wrapper, run by the next transport tick (menu_leave_tick).
bool leave_armed=false,leave_throws=false;unsigned leave_arms=0;
void menu_leave_deferred() { ++leave_arms;if(leave_throws) throw std::runtime_error("injected room failure");leave_armed=true; }
void menu_leave_checked() { if(leave_throws) throw std::runtime_error("injected room failure");menu_leave(); } // immediate leave seam
void menu_leave_tick() { if(leave_armed) { leave_armed=false;menu_leave(); } }
const online_menus::broker_api api{menu_available,menu_view,menu_tick,menu_list,menu_host,menu_resolve,menu_retry,menu_cancel,menu_leave_checked,menu_version,menu_take_preset,menu_refresh_due,host_native,target,posted,nullptr,menu_leave_deferred,nullptr,nullptr,nullptr,menu_retry_in,menu_refusal,menu_host_lost,menu_refusal_serial};
void* __cdecl string_pool() { return pool.data(); }
bool __fastcall input_ok(void*,void*,unsigned,int) { return true; }
void __fastcall pop(void*,void*,const char* name,int) {
    const auto* shown=current() ? static_cast<char*>(current())+0xc : "";
    if(name && std::string_view(name)!=shown) calls.push_back(std::string("request:")+name);
    else { calls.push_back("native-pop");set_current(nullptr); }
}
void* __cdecl dialog_get() { return &dialog; }
void __fastcall dialog_flag(void*,void*,int) {}
void __fastcall dialog_title(void*,void*,const char*) { choices.clear(); }
void __fastcall dialog_add(void*,void*,const char*,const char* command,int,int,int,int) { choices.emplace_back(command); }
void __fastcall dialog_select(void*,void*,int) {}
bool __fastcall dialog_busy(void*,void*) { return false; }
void __fastcall dialog_show(void*,void*) {}
void __fastcall set_difficulty(void*,void*,int value) { difficulty=value; }
int __fastcall get_difficulty(void*,void*) { return difficulty; }
bool __fastcall locked(void*,void*) { return false; }
void __cdecl free_image(void*) { ++freed; }
int __cdecl random_byte() { return 1; }
void* __fastcall local_address(void*,void*) { return address.data(); }
void __fastcall set_address(void*,void*,void*) {}
void __fastcall register_person(void*,void*,void*) {}
int __fastcall controller(void*,void*,int,int) { return 2; }
void __fastcall one_int(void*,void*,int) {}
void __fastcall two_ptrs(void*,void*,void*,void*) {}
int __fastcall handler(void*,void*,int,void*,void*,int,int,int) { return 1; }
void __fastcall unhandler(void*,void*,int,int) {}
int __fastcall timer(void*,void*,void*,void*,int) { return 1; }
void __fastcall untimer(void*,void*,int) {}
void __fastcall clear_people(void* session,void*,bool) { reinterpret_cast<tick_t>(0x608d10)(static_cast<unsigned char*>(session)+0x26c,nullptr);put(session,0x78,0); }
void* __fastcall make_person(void* session,void*,int index) {
    ++allocations;if(!allocate_ok) return nullptr;
    put(person.data(),0x18,unsigned(index));put(session,0x78,unsigned(reinterpret_cast<std::uintptr_t>(person.data())));return person.data();
}
void __fastcall release_people(void* session,void*) { ++departures;clear_people(session,nullptr,false);static_cast<unsigned char*>(session)[0x421]=0; }
void __fastcall kick_local(void* session,void*,void*,int,int,int,int) { release_people(session,nullptr); }
void* __fastcall connection(void*,void*,void*,int) { return peer.data(); }
bool __fastcall connection_live(void*,void*) { return false; }
void __fastcall connection_mark(void*,void*,bool) {}
float __fastcall time(void*,void*,int) { return 1.f; }
std::array<unsigned,12> clock_vt{};
unsigned* clock_object=clock_vt.data();
void* __cdecl clock_get() { return &clock_object; }
void __fastcall session_send(void*,void*,unsigned,void*,int) {}
unsigned short __stdcall htons_stub(unsigned short n) { return static_cast<unsigned short>((n>>8)|(n<<8)); }
unsigned __stdcall inet_stub(const char* value) { assert(std::string_view(value)=="10.77.0.1");return 0x01004d0a; }
int __fastcall game_port(void*,void*) { return 5165; }
void __fastcall search_send(void*,void*,const unsigned char* destination,int,void* packet) {
    assert(destination[8]==10 && destination[9]==77 && destination[10]==0 && destination[11]==1);
    assert(static_cast<unsigned char*>(packet)[16]==0x19);++searches;
}
void* __fastcall peer_address(void*,void*) { return address.data(); }
bool __fastcall same_guid(void*,void*,void*,void*) { return true; }
unsigned short __fastcall peer_port(void*,void*) { return 5165; }
bool __fastcall selected_image(void*,void*) { return *reinterpret_cast<void**>(0xa3bc40+0x2250)!=nullptr; }
void __fastcall option_seed(void*,void*,const char* value) { native_seed.fill(0);std::strncpy(native_seed.data(),value,31); }
void __fastcall option_title(void*,void*,const char* value) { native_title.fill(0);std::strncpy(native_title.data(),value,63); }
void __fastcall option_name_format(void*,void*,const char* value,char* output,unsigned size) { std::strncpy(output,value,size-1);output[size-1]=0; }
std::deque<std::string> setup_lines;
bool __fastcall console_queue(void*,void*,const char* line) {
    if(std::string_view(line).starts_with("setuphost")) setup_lines.emplace_back(line);
    calls.push_back(std::string("queue:")+line);
    if(std::string_view(line)=="openmenu host") { ++posts;return queue_ok; }
    return true;
}
void custom_close() { (*reinterpret_cast<tick_t*>(0x69e438))(child.data(),nullptr); }
// Drive the actual retail "openmenu" console handler 0x5f1d80 (registered at 0x5f4a73), the one a queued
// "openmenu host" (0x60a095) runs; it ends in manager +0x6c. Round 6 drove 0x5f1c10 = "closemenu" here.
std::array<unsigned,0x40> idle_vt{};unsigned* idle_object=idle_vt.data();
void* __cdecl idle_get() { return &idle_object; }
const char* __fastcall idle_text(void*,void*,int,int,int) { return ""; }
void console_request(const char* name) { reinterpret_cast<void(__cdecl*)(const char**)>(0x5f1d80)(&name); }
void host_request() { console_request("host"); }
void setup_unmarked() { const char* args="";reinterpret_cast<void(__cdecl*)(const char**)>(0x5f1bb0)(&args); }
void setup_request() {
    if(setup_lines.empty()) { setup_unmarked();return; }
    auto line=std::move(setup_lines.front());setup_lines.pop_front();
    const char* args=line.c_str()+9;reinterpret_cast<void(__cdecl*)(const char**)>(0x5f1bb0)(&args);
}
void restore_services() {
    retail_restore(0x610d10);retail_restore(0x607c40);retail_restore(0x6097f0);retail_restore(0x60b9d0);
    AddVectoredExceptionHandler(1,[](EXCEPTION_POINTERS* error)->LONG {
        if(error->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION)
            std::cerr<<"r6 native fault eip="<<std::hex<<error->ContextRecord->Eip<<" address="<<error->ExceptionRecord->ExceptionInformation[1]<<std::dec<<"\n";
        if(error->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
            auto* stack=reinterpret_cast<unsigned*>(error->ContextRecord->Esp);
            for(unsigned i=0;i<12;++i) std::cerr<<std::hex<<stack[i]<<' ';std::cerr<<std::dec<<"\n";
        }
        return EXCEPTION_CONTINUE_SEARCH;
    });
    redirect(0x5caff0,activated);redirect(0x55c410,console_queue);
    redirect(0x60c6e0,option_seed);redirect(0x60c710,option_title);redirect(0x610e70,option_name_format);
    redirect(0x5b8a50,no_op);redirect(0x5ad080,no_op);
    redirect(0x672161,no_op);redirect(0x672170,reinterpret_cast<const void*>(&std::strncpy));
    redirect(0x5604f0,native_format);redirect(0x602200,string_pool);
    redirect(*reinterpret_cast<unsigned*>(0x6a236c+0x160),input_ok);
    redirect(*reinterpret_cast<unsigned*>(0x6a236c+0xe8),sound_stub);
    redirect(0x5db270,pop);
    // openmenu prologue services: save idle (+0x10), 0x59ee20 +0x6c and 0x55e9a0 +0x74 (the dialog checks see no dialog).
    saves[0x10/4]=unsigned(reinterpret_cast<std::uintptr_t>(&one_int));
    idle_vt.fill(unsigned(reinterpret_cast<std::uintptr_t>(&no_op)));idle_vt[0x74/4]=unsigned(reinterpret_cast<std::uintptr_t>(&idle_text));
    redirect(0x59ee20,idle_get);redirect(0x55e9a0,idle_get);
    dialogs.fill(0);dialogs[0x14/4]=unsigned(reinterpret_cast<std::uintptr_t>(&dialog_flag));
    dialogs[0x18/4]=unsigned(reinterpret_cast<std::uintptr_t>(&dialog_title));
    dialogs[0x1c/4]=unsigned(reinterpret_cast<std::uintptr_t>(&dialog_add));
    dialogs[0x34/4]=unsigned(reinterpret_cast<std::uintptr_t>(&dialog_select));
    dialogs[0x68/4]=unsigned(reinterpret_cast<std::uintptr_t>(&dialog_show));
    dialogs[0x78/4]=unsigned(reinterpret_cast<std::uintptr_t>(&dialog_busy));
    saves[0x14/4]=unsigned(reinterpret_cast<std::uintptr_t>(&save_process));redirect(0x4b2880,save_get);
    redirect(0x5eb300,dialog_get);locale_vt[2]=unsigned(reinterpret_cast<std::uintptr_t>(&locale_text));redirect(0x4bdb30,locale_getter);
    redirect(*reinterpret_cast<unsigned*>(0x689994+0xb0),locked);
    fake_game_vt[0x268/4]=unsigned(reinterpret_cast<std::uintptr_t>(&get_difficulty));
    fake_game_vt[0x26c/4]=unsigned(reinterpret_cast<std::uintptr_t>(&set_difficulty));redirect(0x46dce0,game_getter);
    redirect(0x60a760,local_address);redirect(0x59eb10,set_address);redirect(0x672532,random_byte);
    for(unsigned routine:{0x615980u,0x615990u,0x6159a0u,0x6159b0u,0x6159e0u}) redirect(routine,no_op);
    redirect(0x60edb0,handler);redirect(0x60f1e0,unhandler);redirect(0x60af70,timer);redirect(0x60afe0,untimer);
    redirect(0x612070,clear_people);redirect(0x612a40,make_person);redirect(0x612960,release_people);
    redirect(0x6133a0,kick_local);redirect(0x560700,free_image);
    redirect(0x606f90,selected_image);
    redirect(*reinterpret_cast<unsigned*>(0x69a134+0x38),controller);
    redirect(*reinterpret_cast<unsigned*>(0x6898c4+0x38),one_int);
    redirect(*reinterpret_cast<unsigned*>(0x6898c4+0x44),no_op);
    redirect(*reinterpret_cast<unsigned*>(0x6898c4+0x68),no_op);
    peer_ui_vt[0x1c/4]=unsigned(reinterpret_cast<std::uintptr_t>(&register_person));redirect(0x615580,peer_ui_getter);
    redirect(0x615c60,one_int);redirect(0x48cbb0,two_ptrs);redirect(0x60db70,connection);redirect(0x6106e0,connection_live);redirect(0x6106f0,connection_mark);
    redirect(0x60eec0,reinterpret_cast<void*>(&connection)); // normally unused: an invented connection already exists
    clock_vt[0x28/4]=unsigned(reinterpret_cast<std::uintptr_t>(&time));redirect(0x55b610,clock_get);
    redirect(0x6133f0,no_op);redirect(0x610f30,session_send);
    redirect(0x671f3e,htons_stub);redirect(0x671f38,inet_stub);redirect(0x60d760,game_port);redirect(0x60d9c0,search_send);
    redirect(0x4bdfa0,peer_address);redirect(0x517c20,same_guid);redirect(0x6106c0,peer_port);
    redirect(0x6067d0,participant_node_pool_getter);initialize_node_pool();
    put(int_value.data(),0,0x6905fc);put(reinterpret_cast<void*>(0x6905fc),0x10,0x55d7e0);
    std::memset(reinterpret_cast<void*>(0xa3bc40),0,0x2310);*reinterpret_cast<unsigned char*>(0xa3bc40)=1;
    put(reinterpret_cast<void*>(0xa3bc40),4,0xa3df68);
    put(reinterpret_cast<void*>(0xa53058),0x41c,0xa3df68);put(reinterpret_cast<void*>(0xa53058),0x78,0);
    put(reinterpret_cast<void*>(0xa3df68),0x1c0,1);
    std::strcpy(reinterpret_cast<char*>(0xa53058+0x410),"FX06");
    address.fill(0);address[8]=10;address[9]=77;address[11]=1;
    put(peer.data(),4,123);put(pool.data(),8,0);
}
void screen(const char* name) {
    put(child.data(),0,std::string_view(name)=="game_options" ? 0x69f65c : 0x69e41c);
    std::strcpy(reinterpret_cast<char*>(child.data()+0xc),name);child[4]=1;set_current(child.data());
}
void action(const char* name) {
    focused=name;
    std::strcpy(reinterpret_cast<char*>(pool.data()+0x1008),name);put(text_item.data(),8,1);
    put(child.data(),0x324,unsigned(reinterpret_cast<std::uintptr_t>(text_item.data())));
    mask=0x10;(*reinterpret_cast<tick_t*>(0x69f694))(child.data(),nullptr);mask=0;
}
void script(unsigned routine,int argument) {
    int_value[1]=unsigned(argument);reinterpret_cast<void*(__cdecl*)(void*)>(routine)(nullptr);
}
void reply(unsigned count=1,bool wrong=false,bool enabled=true) {
    // Execute the actual native game-info handler -> actual pool allocation,
    // version check, sender-address copy and native table insertion.
    for(unsigned i=0;i<count;++i) {
        std::array<unsigned char,0x80> payload{};std::strcpy(reinterpret_cast<char*>(payload.data()+0x3e),"FX06");payload[0xf]=1;
        std::array<unsigned char,0x18> packet{};
        put(packet.data(),4,unsigned(reinterpret_cast<std::uintptr_t>(payload.data())));
        put(packet.data(),0xc,0xa3bc40);put(packet.data(),0x10,unsigned(reinterpret_cast<std::uintptr_t>(peer.data())));
        address[11]=wrong ? 2 : 1;
        reinterpret_cast<int(__cdecl*)(void*)>(0x6090d0)(packet.data());
    }
    if(!enabled) reinterpret_cast<unsigned char*>(0xa3bc40+0x5b4)[0x11]=0;
}
void event(int value) {
    assert(value==8 || value==9);
    std::array<unsigned char,16> payload{};payload[0]=1;
    std::array<unsigned char,0x18> packet{};packet[0]=0x1c;
    put(packet.data(),4,unsigned(reinterpret_cast<std::uintptr_t>(payload.data())));
    put(packet.data(),0xc,0xa3bc40);put(packet.data(),0x10,unsigned(reinterpret_cast<std::uintptr_t>(peer.data())));
    // Execute the real native accepted/refused response handler, not a
    // fabricated callback; success must allocate/mark the native local record.
    reinterpret_cast<int(__cdecl*)(void*)>(value==8 ? 0x607810 : 0x609ad0)(packet.data());
}

bool vis(const char* key) { return visibility.count(key) && !visibility[key]; }
bool hid(const char* key) { return visibility.count(key) && visibility[key]; }
// One native status variant at a time, and never beside the broker's own room line: LC draws them at one anchor.
void native_status(const char* shown) {
    (*reinterpret_cast<tick_t*>(0x69e454))(child.data(),nullptr); // the bindings of the frame that follows the phase change
    for(const char* key:{"lc_native_tunnel","lc_native_discovery","lc_native_admission","lc_native_posting"}) {
        const bool want=shown && std::string_view(key)==shown;
        if(want ? !vis(key) : !hid(key)) { std::cerr<<"native status "<<key<<" should be "<<(want ? "shown" : "hidden")<<" while "<<shown<<"\n";assert(false); }
    }
    if(!hid("lc_broker_room")) { std::cerr<<"lc_broker_room is drawn beside "<<shown<<"\n";assert(false); }
}
void cancel_options_in_base() {
    base_mutation=nullptr;(*reinterpret_cast<int(__fastcall**)(void*,void*)>(0x69f688))(child.data(),nullptr);
}
void run(const script_functions_rules::func_entry* registration) {
    restore_services();
    auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    auto post=[&](intent value) { int_value[1]=unsigned(value);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr); };
    auto cancel=[&] { screen("lc_host");post(intent::back);tick(child.data(),nullptr);screen("lc_online");tick(child.data(),nullptr);ticks+=2000; };
    auto begin=[&] { screen("lc_host");tick(child.data(),nullptr);post(intent::open_room);tick(child.data(),nullptr);assert(calls.back()=="request:game_options");custom_close();screen("game_options");(*reinterpret_cast<lobby_activate_t*>(0x69f66c))(child.data(),nullptr,0); };
    // All Game Options selection commands run through the retail action tick.
    begin();const auto canceled_hosts=menu_hosts;
    (*reinterpret_cast<int(__fastcall**)(void*,void*)>(0x69f688))(child.data(),nullptr);
    assert(menu_hosts==canceled_hosts && menu_view().state==bm::phase::idle);cancel();
    begin();const auto before_combined=menu_hosts;base_mutation=cancel_options_in_base;action("text_inviteslots");
    assert(!current() && menu_hosts==before_combined && std::none_of(calls.begin(),calls.end(),[](const auto& line){return line.starts_with("queue:setuphost");}));cancel();
    begin();action("text_difficulty");assert(calls.back()=="queue:startgamedialog");
    reinterpret_cast<void(__cdecl*)()>(0x5f2090)();assert(choices.size()==3 && choices[0]=="setDifficultyLevel(0)");
    script(0x49f8f0,1);script(0x49f9b0,3);script(0x4a0930,2);
    assert(*reinterpret_cast<unsigned char*>(0xa53058+0x3e0)==1 && *reinterpret_cast<unsigned char*>(0xa53058+0x3dd)==3 && *reinterpret_cast<unsigned char*>(0xa53058+0x3df)==2);
    action("text_gamename");assert(native_tag==1 && native_max==31 && calls.back()=="request:text_entry");
    result("New campaign",1);reinterpret_cast<tick_t>(0x5b8f90)(child.data(),nullptr);reinterpret_cast<tick_t>(0x5b8f90)(child.data(),nullptr);
    assert(std::string_view(reinterpret_cast<char*>(0xa53058+0x3b6))=="New campaign");
    const auto host_count=menu_hosts;action("text_inviteslots");assert(menu_hosts==host_count+1 && maximum==3 && menu_last_name=="New campaign");
    action("text_inviteslots");assert(menu_hosts==host_count+1); // no delayed duplicate Post
    screen("lc_host");menu_auth=true;menu_complete=true;tick(child.data(),nullptr);
    assert(!selected_danger_room);
    assert(allocations==1 && *reinterpret_cast<void**>(0xa53058+0x78)==person.data() && calls.back()=="queue:openmenu host" && published==0);
    native_status("lc_native_posting");
    // Missing native lobby activation (including queue failure) times out and
    // releases the actual native Host participant and broker room.
    ticks+=15000;tick(child.data(),nullptr);assert(menu_view().state==bm::phase::idle && departures && published==0);
    const auto before_stale=calls.size();host_request();assert(calls.size()==before_stale); // canceled native console command
    cancel();
    begin();action("text_gametype");assert(std::find(choices.begin(),choices.end(),"saveloadProcess(3)")!=choices.end());
    save_accept=false;script(0x49ef60,3);assert(!*reinterpret_cast<void**>(0xa3bc40+0x2250));
    save_accept=true;script(0x49ef60,3);assert(save_requests==2 && *reinterpret_cast<void**>(0xa3bc40+0x2250));
    const auto freed_before=freed;
    auto back=*reinterpret_cast<int(__fastcall**)(void*,void*)>(0x69f688);back(child.data(),nullptr);
    assert(freed==freed_before+1 && !*reinterpret_cast<void**>(0xa3bc40+0x2250) && menu_hosts==host_count+1);
    screen("lc_online");tick(child.data(),nullptr);ticks+=2000;
    begin();(*reinterpret_cast<tick_t*>(0x69f694))(child.data(),nullptr);std::strcpy(reinterpret_cast<char*>(0xa53058+0x3b6),"Loaded campaign");
    put(reinterpret_cast<void*>(0xa3bc40),0x2250,0x123400);script(0x49f9b0,4);action("text_inviteslots");
    screen("lc_host");allocate_ok=false;menu_complete=true;tick(child.data(),nullptr);allocate_ok=true;
    assert(menu_view().state==bm::phase::idle && !*reinterpret_cast<void**>(0xa3bc40+0x2250));
    cancel();
    begin();script(0x49f9b0,4);save_accept=true;script(0x49ef60,3);
    host_refuse=true;action("text_inviteslots");host_refuse=false;
    assert(menu_view().state==bm::phase::idle && !*reinterpret_cast<void**>(0xa3bc40+0x2250));
    const auto refused_hosts=menu_hosts;action("text_inviteslots");assert(menu_hosts==refused_hosts && std::none_of(calls.begin(),calls.end(),[](const auto& line){return line.starts_with("queue:setuphost");}));cancel();
    auto resolve=[&] {
        screen("lc_code");tick(child.data(),nullptr);
        for(int i=32;i<40;++i) { post(static_cast<intent>(i));tick(child.data(),nullptr); }
        menu_complete=true;tick(child.data(),nullptr);assert(menu_view().state==bm::phase::room);
    };
    resolve();tunnel_ready=false;ticks+=45000;tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::idle && searches==0);cancel();
    // Authenticated broker membership waits for the proven host tunnel.
    resolve();assert(searches==0);native_status("lc_native_tunnel");
    const auto before_old_menu=menu_view().generation;
    std::strcpy(reinterpret_cast<char*>(hub.data()+0xc),"lc_code");(*reinterpret_cast<tick_t*>(0x69e438))(hub.data(),nullptr);
    std::strcpy(reinterpret_cast<char*>(hub.data()+0xc),"lc_online");set_current(child.data());
    assert(menu_view().generation==before_old_menu && menu_view().state==bm::phase::room);
    tunnel_ready=true;tick(child.data(),nullptr);assert(searches==1);native_status("lc_native_discovery");
    reply();tick(child.data(),nullptr);native_status("lc_native_admission");
    assert(*reinterpret_cast<unsigned*>(0xa53058+0x3a4+8)==0x01004d0a && *reinterpret_cast<unsigned*>(0xa3bc40+0x22bc));
    const auto callback=*reinterpret_cast<int(__cdecl**)(int,void*,void*)>(0xa3bc40+0x2298);
    const auto old_generation=*reinterpret_cast<void**>(0xa3bc40+0x229c);
    event(8);tick(child.data(),nullptr);assert(calls.back()=="request:join");
    cancel();assert(!*reinterpret_cast<void**>(0xa3bc40+0x2298));
    resolve();tick(child.data(),nullptr);reply();tick(child.data(),nullptr);
    callback(8,old_generation,nullptr);const auto requests=calls.size();tick(child.data(),nullptr);assert(calls.size()==requests);
    // Round 15: a refusal selects its own notice; with the rules the PCs possibly differ on (allowlisted, bounded) it names them.
    auto notice_count=[&] { unsigned n=0;for(const char* key:{"lc_notice_admission","lc_notice_discovery","lc_notice_refused","lc_notice_refusal_rules","lc_notice_service_lost","lc_notice_timeout","lc_notice_unavailable","lc_notice_resolving"}) n+=vis(key);return n; };
    event(9);tick(child.data(),nullptr);assert(menu_view().state==bm::phase::idle && vis("lc_notice_refused") && notice_count()==1 && texts["lc_refusal_rules"].empty());
    cancel();
    resolve();tick(child.data(),nullptr);reply();tick(child.data(),nullptr);
    set_refusal("DeathRule, GnsTransport");event(9);tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::idle && vis("lc_notice_refusal_rules") && notice_count()==1 && texts["lc_refusal_rules"]=="DeathRule, GnsTransport");
    cancel();tick(child.data(),nullptr);assert(texts["lc_refusal_rules"].empty());
    refusal_text.clear();
    // F4: rules left by an EARLIER refusal (or another host's, seen while browsing the LAN) are not this attempt's: they are never shown.
    set_refusal("Stale, Other host");
    resolve();tick(child.data(),nullptr);reply();tick(child.data(),nullptr);
    event(9);tick(child.data(),nullptr);assert(vis("lc_notice_refused") && !vis("lc_notice_refusal_rules") && notice_count()==1 && texts["lc_refusal_rules"].empty());
    cancel();
    resolve();tick(child.data(),nullptr);reply();tick(child.data(),nullptr);
    set_refusal("Bad%s\n\"names ABCD-EFGH 10.0.0.1 and more and more and more words");event(9);tick(child.data(),nullptr);
    assert(vis("lc_notice_refusal_rules") && texts["lc_refusal_rules"].size()==44);
    for(unsigned char c:texts["lc_refusal_rules"]) assert(display_byte(c)==char(c));
    refusal_text.clear();cancel();
    for(int kind:{0,1,2,3}) {
        resolve();tick(child.data(),nullptr);
        if(kind==0) { ticks+=15000;tick(child.data(),nullptr); }
        else { reply(kind==1 ? 2 : 1,kind==2,kind!=3);tick(child.data(),nullptr);if(kind==2) { ticks+=15000;tick(child.data(),nullptr); } }
        assert(menu_view().state==bm::phase::idle && notice_count()==1 && (kind==0 ? vis("lc_notice_timeout") : vis("lc_notice_discovery") || vis("lc_notice_timeout")));cancel();
    }
    resolve();tick(child.data(),nullptr);reply();tick(child.data(),nullptr);
    allocate_ok=false;event(8);allocate_ok=true;tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::idle && vis("lc_notice_refused") && notice_count()==1);cancel();
    // A console queue refusal must clean the native participant immediately.
    begin();script(0x49f9b0,4);(*reinterpret_cast<tick_t*>(0x69f694))(child.data(),nullptr);
    queue_ok=false;action("text_inviteslots");screen("lc_host");menu_complete=true;tick(child.data(),nullptr);queue_ok=true;
    assert(menu_view().state==bm::phase::idle && !*reinterpret_cast<void**>(0xa53058+0x78));cancel();
    // A request accepted before cancellation cannot activate on a later room.
    begin();script(0x49f9b0,4);action("text_inviteslots");screen("lc_host");menu_complete=true;tick(child.data(),nullptr);host_request();
    assert(calls.back()=="request:host");cancel();
    put(child.data(),0,0x69f274);std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"host");set_current(child.data());
    (*reinterpret_cast<lobby_activate_t*>(0x69f284))(child.data(),nullptr,0);
    assert(calls.back()=="request:lc_online" && menu_view().state==bm::phase::idle);screen("lc_online");tick(child.data(),nullptr);ticks+=2000;
    // Host -> Leave -> Join -> Leave -> Host in the same installed dispatcher.
    begin();(*reinterpret_cast<tick_t*>(0x69f694))(child.data(),nullptr);script(0x49f9b0,4);std::strcpy(reinterpret_cast<char*>(0xa53058+0x3b6),"Host again");action("text_inviteslots");
    screen("lc_host");menu_complete=true;tick(child.data(),nullptr);assert(menu_view().state==bm::phase::room && allocations>=3);cancel();
    assert(menu_view().state==bm::phase::idle && !*reinterpret_cast<void**>(0xa53058+0x78));
    std::cout<<"round6 PASS real Game Options tick/startgamedialog/SetGameType/SetMaxPlayers/difficulty; real setuphost/Host allocation failure/cancel; native discovery query+reply+record/admission; no tunnel/timeout/duplicate/wrong/disabled/refusal/stale callback/reentry\n";
}
void retail_restore(unsigned va) {
    for(const auto& guard:online_menus_guards::guards) if(guard.va==va) {
        const auto bytes=patch_transaction::bytes_of(guard.hex);std::memcpy(reinterpret_cast<void*>(va),bytes.data(),bytes.size());return;
    }
    assert(false);
}
void __fastcall lobby_pop(void*,void*,const char* name,int) {
    assert(name && (std::string_view(name)=="host" || std::string_view(name)=="join"));
    if(current()) {
        const auto deactivate=*reinterpret_cast<tick_t*>(std::string_view(name)=="host" ? 0x69f290 : 0x69f358);
        deactivate(current(),nullptr);
    }
    set_current(nullptr);
}
void lobby_run(const script_functions_rules::func_entry* registration,bool loaded) {
    restore_services();const auto original_mode=mode;
    auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    auto post=[&](intent value) { int_value[1]=unsigned(value);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr); };
    screen("lc_host");tick(child.data(),nullptr);post(intent::open_room);tick(child.data(),nullptr);
    assert(calls.back()=="request:game_options");custom_close();screen("game_options");(*reinterpret_cast<lobby_activate_t*>(0x69f66c))(child.data(),nullptr,0);script(0x49f8f0,1);script(0x49f9b0,3);
    if(loaded) { save_accept=true;script(0x49ef60,3); }
    (*reinterpret_cast<tick_t*>(0x69f694))(child.data(),nullptr);std::strcpy(reinterpret_cast<char*>(0xa53058+0x3b6),loaded ? "Loaded lobby" : "New lobby");action("text_inviteslots");
    screen("lc_host");menu_auth=menu_complete=true;tick(child.data(),nullptr);assert(menu_view().state==bm::phase::room && published==0);
    // Real Host activation + roster refresh, with the reviewed invented UI /
    // participant services. Keep real campaign Leave routines for this bridge.
    host_request();custom_close();
    mode="r4-r6";setup_lobby(true);retail_restore(0x607c40);retail_restore(0x6097f0);
    redirect(0x5db270,lobby_pop);
    { const auto bytes=patch_transaction::bytes_of("8b442404c1");std::memcpy(reinterpret_cast<void*>(0x5d4d60),bytes.data(),bytes.size()); }
    auto activate=*reinterpret_cast<lobby_activate_t*>(0x69f284);activate(child.data(),nullptr,0);
    assert(published==1 && menu_view().state==bm::phase::room);
    activate(child.data(),nullptr,0);assert(published==1 && menu_view().state==bm::phase::room);
    const auto healthy=menu_broker;menu_broker.disconnected("invented service loss",ticks);
    activate(child.data(),nullptr,0);assert(published==1 && menu_view().state==bm::phase::room);menu_broker=healthy;
    {   // 060q live (fix H): Start Game deactivates the host lobby with phase 0 and the retail flags +0x1972 / +0x1973 clear
        // (nothing in XMen2.exe ever sets them). The room stays: JIP admission, the Party screen's code and roster.
        const auto leaves=menu_leaves;auto* const lobby=current();
        assert(*reinterpret_cast<int*>(child.data()+0x1960)==0 && !child[0x1972] && !child[0x1973]);
        (*reinterpret_cast<tick_t*>(0x69f290))(child.data(),nullptr);
        assert(menu_view().state==bm::phase::room && menu_leaves==leaves);
        set_current(lobby);activate(child.data(),nullptr,0);assert(published==1 && menu_view().state==bm::phase::room);
    }
    auto host_tick=*reinterpret_cast<tick_t*>(0x69f2ac);
    ready_person(2,false);refresh_now();mask=0x10;host_tick(child.data(),nullptr);assert(!transfers && !starts && !broadcasts);
    focus_stub(child.data(),nullptr,"text_leaveroom",true);mask=0x10;host_tick(child.data(),nullptr);
    mask=0;post(intent::confirm_leave);host_tick(child.data(),nullptr);host_tick(child.data(),nullptr);
    assert(!current() && menu_view().state==bm::phase::idle && menu_leaves==1 && !*reinterpret_cast<void**>(0xa53058+0x78));
    // Complete a fresh resolve through real discovery / native admission and
    // activate the real Join; Ready remains its native role-aware action.
    mode=original_mode;restore_services();screen("lc_online");tick(child.data(),nullptr);ticks+=2000;
    screen("lc_code");tick(child.data(),nullptr);for(int i=32;i<40;++i) { post(static_cast<intent>(i));tick(child.data(),nullptr); }
    tunnel_ready=true;menu_complete=true;tick(child.data(),nullptr);reply();tick(child.data(),nullptr);event(8);tick(child.data(),nullptr);
    assert(calls.back()=="request:join");custom_close();assert(menu_view().state==bm::phase::room);mode="r4-r6";setup_lobby(false);retail_restore(0x6097f0);redirect(0x6133a0,kick_local);redirect(0x5db270,lobby_pop);
    { const auto bytes=patch_transaction::bytes_of("8b442404c1");std::memcpy(reinterpret_cast<void*>(0x5d4d60),bytes.data(),bytes.size()); }
    activate=*reinterpret_cast<lobby_activate_t*>(0x69f350);activate(child.data(),nullptr,0);
    auto join_tick=*reinterpret_cast<tick_t*>(0x69f374);
    ready_person(1,false);refresh_now();mask=0;join_tick(child.data(),nullptr);
    mask=0x1000000;join_tick(child.data(),nullptr);assert(announces==1 && last_ready && menu_view().state==bm::phase::room);
    join_tick(child.data(),nullptr);assert(announces==1); // held duplicate Ready tool
    mask=0;join_tick(child.data(),nullptr);auto back=*reinterpret_cast<lobby_back_t*>(0x69f368);
    back(child.data(),nullptr);assert(announces==2 && !last_ready && current() && menu_view().state==bm::phase::room);
    // The native waiting/transfer flag keeps Ready idle until the next area;
    // the adapter must leave this native state and its cancellation intact.
    if(!loaded) {
        child[0x196c]=1;mask=0x10;const auto before=announces;join_tick(child.data(),nullptr);
        assert(announces==before && current() && menu_view().state==bm::phase::room);mask=0;
    }
    back(child.data(),nullptr);assert(!current() && menu_view().state==bm::phase::idle && menu_leaves==2);
    std::cout<<"round6 lobby PASS "<<(loaded ? "load" : "new")<<" -> actual Host activation/publication/unready gate/native Leave -> actual discovery/admission/Join activation/Ready duplicate/Unready/Leave; broker and native cleanup\n";
    mode=original_mode;
}

void generation_run(const script_functions_rules::func_entry* registration) {
    const auto original_mode=mode;restore_services();
    const auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    auto post=[&](intent value) { int_value[1]=unsigned(value);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr); };
    auto hub_screen=[&] { mode=original_mode;screen("lc_online");tick(child.data(),nullptr);ticks+=2000; };
    auto host=[&] {
        mode=original_mode;screen("lc_host");tick(child.data(),nullptr);post(intent::open_room);tick(child.data(),nullptr);custom_close();
        screen("game_options");(*reinterpret_cast<lobby_activate_t*>(0x69f66c))(child.data(),nullptr,0);
        script(0x49f8f0,1);script(0x49f9b0,4);action("text_inviteslots");
        screen("lc_host");menu_auth=menu_complete=true;tick(child.data(),nullptr);assert(menu_view().state==bm::phase::room);
    };
    auto cancel_host=[&] { screen("lc_host");post(intent::back);tick(child.data(),nullptr);assert(menu_view().state==bm::phase::idle);hub_screen(); };
    auto join=[&] {
        mode=original_mode;screen("lc_code");tick(child.data(),nullptr);
        for(int n=32;n<40;++n) { post(static_cast<intent>(n));tick(child.data(),nullptr); }
        tunnel_ready=true;menu_auth=menu_complete=true;tick(child.data(),nullptr);reply();tick(child.data(),nullptr);event(8);tick(child.data(),nullptr);
        assert(calls.back()=="request:join" && menu_view().state==bm::phase::room);
    };
    auto cancel_join=[&] { post(intent::clear);tick(child.data(),nullptr);post(intent::back);tick(child.data(),nullptr);assert(menu_view().state==bm::phase::idle);hub_screen(); };
    auto denied=[&](bool old_host,bool cleanup=false) {
        hub.fill(0);put(hub.data(),0,old_host ? 0x69f274 : 0x69f33c);hub[4]=1;
        std::strcpy(reinterpret_cast<char*>(hub.data()+0xc),old_host ? "host" : "join");set_current(hub.data());
        const auto before=activations;const auto leaves_before=departures;
        (*reinterpret_cast<lobby_activate_t*>(old_host ? 0x69f284 : 0x69f350))(hub.data(),nullptr,0);
        assert(activations==before);
        const auto before_ticks=native_ticks;const auto before_announces=announces;
        auto* local=*reinterpret_cast<unsigned char**>(0xa53058+0x78);const auto flags=local ? *reinterpret_cast<unsigned*>(local+0x1c) : 0;
        const auto back=*reinterpret_cast<lobby_back_t*>(old_host ? 0x69f2a0 : 0x69f368);assert(back(hub.data(),nullptr)==2);
        const auto mouse=*reinterpret_cast<bool(__fastcall**)(void*,void*,int,int,int,int,int)>(old_host ? 0x69f2f4 : 0x69f3bc);
        assert(mouse(hub.data(),nullptr,510,475,0,WM_LBUTTONUP,0));
        (*reinterpret_cast<tick_t*>(old_host ? 0x69f320 : 0x69f3e8))(hub.data(),nullptr);
        const unsigned base_site=old_host ? 0x5bad1f : 0x5c88be;
        const auto base_input=reinterpret_cast<tick_t>(base_site+5+*reinterpret_cast<unsigned*>(base_site+1));base_input(hub.data(),nullptr);
        assert(native_ticks==before_ticks && announces==before_announces && (!local || *reinterpret_cast<unsigned*>(local+0x1c)==flags));
        (*reinterpret_cast<tick_t*>(old_host ? 0x69f2ac : 0x69f374))(hub.data(),nullptr);
        (*reinterpret_cast<tick_t*>(old_host ? 0x69f290 : 0x69f358))(hub.data(),nullptr);
        assert(cleanup ? departures>leaves_before : departures==leaves_before);
        std::strcpy(reinterpret_cast<char*>(hub.data()+0xc),"lc_online");put(hub.data(),0,0x69e41c);
    };
    // Old Host console command remains ahead of the new Host command; the
    // actual retail openmenu handler must consume their separate generations.
    host();cancel_host();host();const auto replacement=menu_view().generation;
    auto before=calls.size();host_request();assert(calls.size()==before && menu_view().generation==replacement);
    host_request();assert(calls.back()=="request:host");cancel_host();
    // Deliver that old Host activation only after a newer Join token exists.
    join();const auto guest_generation=menu_view().generation;custom_close();denied(true);
    assert(calls.back()=="request:join" && menu_view().generation==guest_generation && menu_view().state==bm::phase::room);
    mode="r4-r6";setup_lobby(false);retail_restore(0x6097f0);redirect(0x6133a0,kick_local);redirect(0x5db270,lobby_pop);
    { const auto bytes=patch_transaction::bytes_of("8b442404c1");std::memcpy(reinterpret_cast<void*>(0x5d4d60),bytes.data(),bytes.size()); }
    (*reinterpret_cast<lobby_activate_t*>(0x69f350))(child.data(),nullptr,0);assert(menu_view().state==bm::phase::room);
    // Once active, losing the broker must not revoke a healthy native lobby.
    const auto healthy=menu_broker;menu_broker.disconnected("after activation",ticks);
    (*reinterpret_cast<lobby_activate_t*>(0x69f350))(child.data(),nullptr,0);assert(menu_view().state==bm::phase::room);menu_broker=healthy;
    ready_person(1,false);refresh_now();(*reinterpret_cast<lobby_back_t*>(0x69f368))(child.data(),nullptr);assert(menu_view().state==bm::phase::idle);
    mode=original_mode;restore_services();hub_screen();
    // Reverse roles: an old guest activation cannot close or mutate a new host.
    join();cancel_join();host();host_request();custom_close();const auto host_generation=menu_view().generation;denied(false);
    assert(calls.back()=="request:host" && menu_view().generation==host_generation && *reinterpret_cast<void**>(0xa53058+0x78));
    mode="r4-r6";setup_lobby(true);retail_restore(0x607c40);retail_restore(0x6097f0);redirect(0x5db270,lobby_pop);
    (*reinterpret_cast<lobby_activate_t*>(0x69f284))(child.data(),nullptr,0);assert(menu_view().state==bm::phase::room);
    // 060q live (fix H): a bare host lobby deactivation (phase 0, the retail flags clear) is what Start Game does - the room
    // stays. Only the host lobby's native Leave (phase 2 consumed by its tick, here through the skin's confirmation)
    // releases it.
    (*reinterpret_cast<tick_t*>(0x69f290))(child.data(),nullptr);assert(menu_view().state==bm::phase::room);
    set_current(child.data());(*reinterpret_cast<lobby_activate_t*>(0x69f284))(child.data(),nullptr,0);assert(menu_view().state==bm::phase::room);
    {   const auto host_tick=*reinterpret_cast<tick_t*>(0x69f2ac);
        put(child.data(),0x1960,2);mask=0;host_tick(child.data(),nullptr);
        post(intent::confirm_leave);host_tick(child.data(),nullptr);host_tick(child.data(),nullptr); }
    set_current(nullptr);assert(menu_view().state==bm::phase::idle);
    mode=original_mode;restore_services();hub_screen();
    // Loss before first activation is a pending-flow failure, not the already-
    // active survival policy exercised above.
    join();menu_broker.disconnected("before activation",ticks);custom_close();denied(false,true);
    assert(menu_view().state==bm::phase::idle && !*reinterpret_cast<void**>(0xa53058+0x78));
    std::cout<<"round6 generations PASS actual console openmenu (0x5f1d80 -> +6c) handler FIFO; delayed Host command after replacement Host; old Host/Join activation across opposite new roles; denied tick/deactivation; first-activation auth loss versus active-lobby survival\n";
    mode=original_mode;
}

const char* __fastcall empty_text(void*,void*) { return ""; }
bool __fastcall terminal_console(void*,void*,const char*) { return true; }
void __fastcall end_notice(void*,void*,int,int,int,int) { std::cout<<"end native notice\n"; }
void __fastcall remove_record(void* session,void*,void* record,bool) {
    std::cout<<"end remove record "<<record<<"\n";auto* bytes=static_cast<unsigned char*>(session);
    if(*reinterpret_cast<void**>(bytes+0x78)==record) put(session,0x78,0);
    if(*reinterpret_cast<void**>(bytes+0x7c)==record) put(session,0x7c,0);
    for(auto* node=*reinterpret_cast<unsigned char**>(bytes+0x26c);node;node=*reinterpret_cast<unsigned char**>(node+8)) {
        if(*reinterpret_cast<void**>(node+0x10)==record) {
            reinterpret_cast<bool(__thiscall*)(void*,void*)>(0x611e90)(bytes+0x26c,node);return;
        }
    }
}
void __fastcall end_one(void*,void*,int value) { std::cout<<"end int "<<value<<" return "<<_ReturnAddress()<<"\n"; }
void __fastcall end_noop(void*,void*) { std::cout<<"end service return "<<_ReturnAddress()<<"\n"; }
void session_end_services(bool hosting) {
    // Keep the real 612070/612c40/6123e0/6133a0 caller chain. Only destruction
    // of invented participants and UI/network/scheduler services are supplied.
    for(auto [va,hex]:{std::pair{0x612070u,"558be98b85"},std::pair{0x6133a0u,"568b742408"}}) {
        const auto bytes=patch_transaction::bytes_of(hex);std::memcpy(reinterpret_cast<void*>(va),bytes.data(),bytes.size());
    }
    redirect(0x611f50,remove_record);redirect(0x611550,end_notice);redirect(0x55beb0,terminal_console);
    fake_game_vt[0x234/4]=unsigned(reinterpret_cast<std::uintptr_t>(&end_one));
    for(unsigned slot:{0x30u,0x34u,0x38u,0x40u,0x68u}) fake_game_vt[slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&end_one));
    fake_game_vt[0x44/4]=unsigned(reinterpret_cast<std::uintptr_t>(&end_noop));
    for(unsigned getter:{0x5d8920u,0x551ed0u,0x48de40u,0x401b00u}) redirect(getter,game_getter);
    redirect(0x5eb300,dialog_get);dialogs[0x10/4]=unsigned(reinterpret_cast<std::uintptr_t>(&end_one));
    dialogs[0x50/4]=unsigned(reinterpret_cast<std::uintptr_t>(&empty_text));dialogs[0x6c/4]=unsigned(reinterpret_cast<std::uintptr_t>(&end_noop));
    peer_ui_vt[8/4]=unsigned(reinterpret_cast<std::uintptr_t>(&end_noop));
    clock_vt[0xc/4]=unsigned(reinterpret_cast<std::uintptr_t>(&end_noop));
    redirect(0x55a390,network_stub);redirect(0x6162c0,end_one);redirect(0x6096c0,end_one);redirect(0x60c230,end_one);
    for(unsigned routine:{0x606d00u,0x6068e0u,0x615120u,0x614fd0u,0x60b270u,0x559e50u,0x615da0u}) redirect(routine,end_noop);
    redirect(0x559150,end_one);
    *reinterpret_cast<unsigned char*>(0xa3bc40)=0; // real gameplay branch, not the native lobby
    *reinterpret_cast<unsigned char*>(0xa53058+0x420)=1;*reinterpret_cast<unsigned char*>(0xa53058+0x421)=hosting ? 1 : 0;
    put(lobby_people[0].data(),0x1c,hosting ? 3 : 1);put(lobby_people[1].data(),0x1c,hosting ? 0 : 2);
    put(reinterpret_cast<void*>(0xa53058),0x7c,unsigned(reinterpret_cast<std::uintptr_t>(lobby_people[hosting ? 0 : 1].data())));
    put(reinterpret_cast<void*>(0xa53058),0x424,4); // native full-session disconnect path
    *reinterpret_cast<unsigned char*>(0x70b70d)=1;*reinterpret_cast<unsigned char*>(0x6f3c2d)=0;
}

// Review regressions exercise the retail producers/handlers, not bridge internals.
void review_run(const script_functions_rules::func_entry* registration) {
    const auto original_mode=mode;restore_services();
    const auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    auto post=[&](intent value) { int_value[1]=unsigned(value);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr); };
    auto begin=[&] {
        screen("lc_host");tick(child.data(),nullptr);post(intent::open_room);tick(child.data(),nullptr);custom_close();
        screen("game_options");(*reinterpret_cast<lobby_activate_t*>(0x69f66c))(child.data(),nullptr,0);script(0x49f9b0,4);
    };
    auto resolve=[&] {
        screen("lc_code");tick(child.data(),nullptr);
        for(int n=32;n<40;++n) { post(static_cast<intent>(n));tick(child.data(),nullptr); }
        menu_auth=menu_complete=true;tick(child.data(),nullptr);assert(menu_view().state==bm::phase::room);
    };
    if(mode=="r6-end-id-order") {
        begin();script(0x49faa0,0);script(0x49faa0,0);assert(setup_lines.size()==2);
        std::swap(setup_lines[0],setup_lines[1]);const auto before=menu_hosts;setup_request();assert(menu_hosts==before+1);
        setup_request();assert(menu_hosts==before+1 && allocations==0);
    } else if(mode=="r6-end-options" || mode=="r6-end-unmarked") {
        begin();script(0x49faa0,0);const auto before=menu_hosts;
        if(mode=="r6-end-options") { screen("text_entry");setup_request();assert(menu_hosts==before);screen("game_options");script(0x49faa0,0);setup_request();assert(menu_hosts==before+1); }
        else {
            setup_unmarked();setup_unmarked();assert(menu_hosts==before);
            const char* raw="unmarked";const auto* saved=raw;
            reinterpret_cast<void(__cdecl*)(const char**)>(0x5f1bb0)(&raw);assert(raw==saved && menu_hosts==before);
            const char* unknown="lc-native:ffffffffffffffff";reinterpret_cast<void(__cdecl*)(const char**)>(0x5f1bb0)(&unknown);
            setup_request();assert(menu_hosts==before+1);
        }
    } else if((mode.starts_with("r6-end-") || mode=="r6-nit-end" || mode=="r6-nit-end-guest")) {
        std::cout<<"end fixture: establish room\n";
        const bool hosting=mode.ends_with("host") || mode=="r6-nit-end";
        if(hosting) { begin();action("text_inviteslots");screen("lc_host");menu_auth=menu_complete=true;tick(child.data(),nullptr);host_request(); }
        else { tunnel_ready=true;resolve();reply();tick(child.data(),nullptr);event(8);tick(child.data(),nullptr); }
        custom_close();mode="r4-r6";local_id=hosting ? 0 : 1;participant_ids[0]=local_id;participant_ids[1]=hosting ? 1 : 0;participant_ids[2]=2;participant_ids[3]=3;
        if(original_mode=="r6-end-prune-host") people_count=2;setup_lobby(hosting);
        local_id=hosting ? 0 : 1;put(lobby_people[0].data(),0x1c,hosting ? 3 : 1);put(lobby_people[1].data(),0x1c,hosting ? 0 : 2);
        put(reinterpret_cast<void*>(0xa53058),0x78,unsigned(reinterpret_cast<std::uintptr_t>(lobby_people[0].data())));
        (*reinterpret_cast<lobby_activate_t*>(hosting ? 0x69f284 : 0x69f350))(child.data(),nullptr,0);
        assert(menu_view().state==bm::phase::room && menu_leaves==0);
        std::cout<<"end fixture: activated native lobby\n";session_end_services(hosting);std::cout<<"end fixture: services ready\n";
        const bool direct=original_mode=="r6-end-direct-guest",lobby=original_mode=="r6-end-lobby-guest";
        if(direct) put(reinterpret_cast<void*>(0xa53058),0x424,0);
        if(lobby) *reinterpret_cast<unsigned char*>(0xa3bc40)=1;
        if(original_mode.starts_with("r6-end-prune-")) {
            const bool solo=hosting;const auto remote=solo ? 1u : 2u;
            if(solo) *reinterpret_cast<unsigned char*>(0x612d98)=1; // effective existing rule26 keep-hosting byte
            put(lobby_people[remote].data(),0x1c,0);
            reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x612c40)(reinterpret_cast<void*>(0xa53058),lobby_people[remote].data(),0x1107,false);
            assert(*reinterpret_cast<void**>(0xa53058+0x78) && *reinterpret_cast<unsigned char*>(0xa53058+0x420));
            assert(menu_view().state==bm::phase::room && menu_leaves==0 && *reinterpret_cast<unsigned*>(0xa53058+0x278)==(solo ? 1u : 2u));
            reinterpret_cast<void(__thiscall*)(void*,bool)>(0x612070)(reinterpret_cast<void*>(0xa53058),true);
            const auto original=patch_transaction::bytes_of("51a1f8386f");std::memcpy(reinterpret_cast<void*>(0x609400),original.data(),original.size());
            redirect(0x6159c0,end_noop);reinterpret_cast<void(__thiscall*)(void*)>(0x609400)(reinterpret_cast<void*>(0xa3bc40));
            assert(*reinterpret_cast<void**>(0xa53058+0x78) && *reinterpret_cast<unsigned char*>(0xa53058+0x420) && menu_view().state==bm::phase::room && menu_leaves==0);
            mode=original_mode;std::cout<<"retail keep PASS "<<mode<<"; remote prune/keep-local/campaign-start preserve room\n";return;
        }
        // Round 6 nits: (a) a C++ exception from the room layer must never unwind through the retail frames (0x6123e0 /
        // 0x612c40 / 0x6133a0); (b) the room leave waits for the transport tick after the quit notices retail just queued.
        const bool throws=original_mode.find("-throw-")!=std::string::npos;leave_throws=throws;
        // Round 15: the room layer reports that this guest's connection to the host ended (never set for a local leave).
        host_lost_flag=original_mode=="r6-end-loss-guest" || original_mode=="r6-end-disconnect-guest" || original_mode=="r6-end-loss-preset-guest" || original_mode=="r6-end-loss-noassets-guest";
        // 0.6 preview fix E: a KICKED guest. The host's native Kick (0x6133a0(record, 0x10ce, 0x10cf, 0x10cf, 1)) reaches the
        // removed PC as the removal notice 0x2b; its transport-bound host connection ends too, so the room layer's host-loss
        // latch IS set (nothing on this PC told it otherwise). Only the native notice tells the kick apart: the END path must show
        // the plain "removed" notice, never the host-left panel. Nothing calls removed_by_host() here.
        const bool kicked=original_mode=="r6-end-kicked-guest";
        // Negative control: the native slow-player drop sends the same reason 0x10ce with flag 0. It is not a Kick: no "removed"
        // notice, and the lost host connection keeps the host-left panel.
        const bool slowdrop=original_mode=="r6-end-slowdrop-guest";
        host_loss_real=kicked || slowdrop;fixture_host_loss.reset();
        if(slowdrop) host_lost_flag=true;
        if(kicked || slowdrop) { fixture_host_loss.connected();fixture_host_loss.ended(true);assert(fixture_host_loss.lost()); }
        bool escaped=false;
        try {
            if(original_mode.starts_with("r6-end-quit-") || original_mode=="r6-nit-end" || original_mode=="r6-nit-end-guest") {
                const char* args="main";reinterpret_cast<void(__cdecl*)(const char**)>(0x5f27a0)(&args);
            } else if(kicked || slowdrop) {
                menu_broker.disconnected("broker already unavailable",ticks);
                put(lobby_people[1].data(),0x1c,2); // actual remote host flag
                // The removal notice as the network layer hands it to the retail 0x2b handler 0x612f60: the event's body
                // (+4) is the 12-byte notice (this PC's index, its reason 0x10ce, flag 1), +0xc the session.
                std::array<unsigned char,12> body{};put(body.data(),0,unsigned(local_id));put(body.data(),4,0x10ce);body[8]=slowdrop ? 0 : 1;
                std::array<unsigned,4> event{0,unsigned(reinterpret_cast<std::uintptr_t>(body.data())),0,0xa53058};
                reinterpret_cast<void(__cdecl*)(void*)>(0x612f60)(event.data());
            } else if(original_mode.starts_with("r6-end-loss-")) {
                menu_broker.disconnected("broker already unavailable",ticks);
                put(lobby_people[1].data(),0x1c,2); // actual remote host flag
                reinterpret_cast<void(__thiscall*)(void*,void*,int,bool)>(0x612c40)(reinterpret_cast<void*>(0xa53058),lobby_people[1].data(),0x1107,false);
            } else {
                reinterpret_cast<void(__thiscall*)(void*,void*,int,int,int,bool)>(0x6133a0)(reinterpret_cast<void*>(0xa53058),lobby_people[0].data(),0x10cc,0x10cc,0x1107,false);
            }
        } catch(...) { escaped=true; }
        assert(!escaped);
        assert(!*reinterpret_cast<void**>(0xa53058+0x78));
        if(!direct && !lobby) assert(!*reinterpret_cast<unsigned char*>(0xa53058+0x420));
        assert(leave_arms==1); // the wrapper asked for exactly one deferred room leave
        if(throws) {
            // The injected failure was contained: no leave ran, native ownership is gone, the retail teardown finished.
            assert(menu_leaves==0 && !leave_armed);
            leave_throws=false;menu_leave_tick();assert(menu_leaves==0);
            script(0x49f990,0);
            std::cout<<"retail end chain PASS "<<original_mode<<"; injected room failure contained, retail frames not unwound\n";
            mode=original_mode;return;
        }
        // The quit notices retail queued are still ahead of the room leave: nothing left the room in the retail frame.
        assert(menu_leaves==0 && menu_view().state==bm::phase::room && leave_armed);
        menu_leave_tick();
        assert(menu_view().state==bm::phase::idle && menu_leaves==1);
        menu_leave_tick();script(0x49f990,0);assert(menu_view().state==bm::phase::idle && menu_leaves==1);
        if(!hosting && !direct && !lobby) {
            // The 0.6 host-left panel: shown at the next Hub only when the host connection was lost, modal while up (Host again
            // and Back act, everything else is ignored), never for a deliberate local leave, and raised once per loss.
            restore_services();redirect(0x5d8920,manager_stub);redirect(0x48de40,seats_stub); // the end chain redirected the manager/seat getters; the Hub ticks on the harness manager again
            const bool noassets=original_mode=="r6-end-loss-noassets-guest";
            // F5: assets that do not author the panel (an older LC) must never leave the Hub rows disabled behind nothing.
            if(noassets) absent_items={"lc_hub_host_left","lc_hub_host_left_veil","lc_hostleft_again","lc_hostleft_back"};
            screen("lc_online");tick(child.data(),nullptr);
            const bool lost=host_lost_flag && !noassets;
            if(noassets) {
                assert(enabled_items["host"] && enabled_items["code"] && enabled_items["browse"] && enabled_items["other"] && enabled_items["desctext3"] && enabled_items["desctext4"]);
                const auto before=calls.size();post(intent::browse);tick(child.data(),nullptr);assert(calls.back()=="request:lc_browse" && calls.size()==before+1);
                absent_items.clear();
            } else if(!lost) {
                assert(hid("lc_hub_host_left") && hid("lc_hostleft_again") && !enabled_items["lc_hostleft_again"] && enabled_items["host"]);
                if(kicked) {
                    // The kicked guest's Hub: the plain notice "The host removed you from the game." (lc_notice_removed), not the
                    // generic inline error; and the Hub is fully usable: no veil, every row and prompt enabled, actions act.
                    assert(vis("lc_notice_removed") && hid("lc_inline_error"));
                    assert(hid("lc_hub_host_left_veil") && hid("lc_hostleft_back") && enabled_items["code"] && enabled_items["browse"] && enabled_items["other"] && enabled_items["desctext3"] && enabled_items["desctext4"]);
                    const auto before=calls.size();post(intent::browse);tick(child.data(),nullptr);assert(calls.size()==before+1 && calls.back()=="request:lc_browse");
                    screen("lc_online");tick(child.data(),nullptr);assert(hid("lc_hub_host_left") && hid("lc_notice_removed")); // both stay down at the next Hub entry
                }
            }
            else {
                assert(vis("lc_hub_host_left") && vis("lc_hostleft_again") && vis("lc_hostleft_back") && enabled_items["lc_hostleft_again"] && enabled_items["lc_hostleft_back"]);
                assert(!vis("lc_notice_removed")); // a host loss (or the slow-player drop) is never shown as a removal
                assert(!enabled_items["host"] && !enabled_items["code"] && !enabled_items["browse"] && !enabled_items["other"] && !enabled_items["change_name"] && !enabled_items["rejoin"]);
                assert(!enabled_items["desctext3"] && !enabled_items["desctext4"]); // the prompt items that repeat Change name / Rejoin cannot take focus behind the veil
                const auto before=calls.size();
                for(auto ignored:{intent::browse,intent::code,intent::other,intent::edit_name,intent::rejoin,intent::unavailable}) { post(ignored);tick(child.data(),nullptr); }
                assert(calls.size()==before && vis("lc_hub_host_left"));
                if(original_mode=="r6-end-loss-preset-guest") {
                    // F5: a launcher preset leaves the Hub: the panel ends with it (it must not wait for the next Hub entry).
                    menu_launch={bm::operation::host,{},"Preset room",true};menu_has_preset=true;
                    tick(child.data(),nullptr);assert(calls.back()=="request:lc_host");
                    screen("lc_online");tick(child.data(),nullptr);assert(hid("lc_hub_host_left") && enabled_items["host"] && enabled_items["desctext3"]);
                } else if(original_mode=="r6-end-loss-guest") {
                    post(intent::host);tick(child.data(),nullptr);assert(calls.back()=="request:lc_host");
                } else {
                    post(intent::back);tick(child.data(),nullptr);assert(calls.size()==before && hid("lc_hub_host_left") && enabled_items["host"] && !enabled_items["lc_hostleft_again"]);
                }
                tick(child.data(),nullptr);assert(hid("lc_hub_host_left"));
            }
            host_lost_flag=false;host_loss_real=false;
        }
        std::cout<<"retail end chain PASS "<<original_mode<<"; local removed; broker Leave one transport tick after the quit notices, without Hub navigation\n";
    } else if(mode=="r6-nit-script") {
        begin();const auto before=allocations,hosts_before=menu_hosts;
        script(0x49faa0,0);assert(calls.back().starts_with("queue:setuphost"));
        setup_request();
        assert(menu_hosts==hosts_before+1 && allocations==before); // no native Host before broker confirmation
        screen("lc_host");menu_auth=menu_complete=true;tick(child.data(),nullptr);
        assert(allocations==before+1 && menu_view().state==bm::phase::room);
        setup_request();assert(allocations==before+1);
        post(intent::back);tick(child.data(),nullptr);assert(menu_view().state==bm::phase::idle && !*reinterpret_cast<void**>(0xa53058+0x78));
        const auto after=allocations;setup_request();assert(allocations==after); // canceled queued script
    } else if(mode=="r6-nit-tokens") {
        for(const char* name:{"host","join"}) for(unsigned n=0;n<40;++n) {
            const auto before=calls.size();console_request(name);
            assert(calls.size()==before+1 && calls.back()==std::string("request:")+name);
        }
        begin();(*reinterpret_cast<int(__fastcall**)(void*,void*)>(0x69f688))(child.data(),nullptr);
        screen("lc_online");tick(child.data(),nullptr);
        for(unsigned n=0;n<40;++n) { const auto before=calls.size();host_request();assert(calls.size()==before+1); }
        for(unsigned n=0;n<24;++n) {
            ticks+=2000;begin();action("text_inviteslots");screen("lc_host");menu_auth=menu_complete=true;tick(child.data(),nullptr);
            assert(menu_view().state==bm::phase::room);post(intent::back);tick(child.data(),nullptr);assert(menu_view().state==bm::phase::idle);
            screen("lc_online");tick(child.data(),nullptr);
        }
        // Canceled console commands still drain in FIFO order, without occupying
        // token slots needed by the 24 replacement rooms above.
        for(unsigned n=0;n<24;++n) { const auto before=calls.size();host_request();assert(calls.size()==before); }
        const auto before=calls.size();host_request();assert(calls.size()==before+1);
    } else if(mode=="r6-nit-mixed") {
        setup_request(); // queued legacy Host first
        begin();action("text_inviteslots");screen("lc_host");menu_auth=menu_complete=true;tick(child.data(),nullptr);
        post(intent::back);tick(child.data(),nullptr);screen("lc_online");tick(child.data(),nullptr);
        auto before=calls.size();host_request();assert(calls.size()==before+1 && calls.back()=="request:host");
        before=calls.size();host_request();assert(calls.size()==before); // canceled owned command second
    } else if(mode=="r6-nit-same-host" || mode=="r6-nit-same-guest") {
        const bool hosting=mode=="r6-nit-same-host";
        auto open=[&] {
            if(hosting) { begin();action("text_inviteslots");screen("lc_host");menu_auth=menu_complete=true;tick(child.data(),nullptr);host_request(); }
            else { tunnel_ready=true;resolve();reply();tick(child.data(),nullptr);event(8);tick(child.data(),nullptr); }
        };
        open();if(!hosting) { post(intent::clear);tick(child.data(),nullptr); }
        post(intent::back);tick(child.data(),nullptr);screen("lc_online");tick(child.data(),nullptr);ticks+=2000;
        open();custom_close();const auto generation=menu_view().generation;
        mode="r4-r6";setup_lobby(hosting);
        hub.fill(0);put(hub.data(),0,hosting ? 0x69f274 : 0x69f33c);hub[4]=1;
        std::strcpy(reinterpret_cast<char*>(hub.data()+0xc),hosting ? "host" : "join");set_current(hub.data());
        auto activate=*reinterpret_cast<lobby_activate_t*>(hosting ? 0x69f284 : 0x69f350);
        const auto before=activations;activate(hub.data(),nullptr,0);assert(activations==before && menu_view().generation==generation);
        set_current(child.data());activate(child.data(),nullptr,0);assert(activations==before+1 && menu_view().state==bm::phase::room);
    } else if(mode=="r6-nit-setup-mixed") {
        screen("game_options");script(0x49f9b0,4);action("text_inviteslots");assert(calls.back().starts_with("queue:setuphost"));
        begin();script(0x49faa0,0);(*reinterpret_cast<int(__fastcall**)(void*,void*)>(0x69f688))(child.data(),nullptr);
        screen("lc_online");tick(child.data(),nullptr);ticks+=2000;begin();const auto before=menu_hosts;
        setup_request();assert(menu_hosts==before);
        setup_request();assert(menu_hosts==before);
        script(0x49faa0,0);setup_request();assert(menu_hosts==before+1);
    } else if(mode=="r6-nit-script-old") {
        begin();script(0x49faa0,0);(*reinterpret_cast<int(__fastcall**)(void*,void*)>(0x69f688))(child.data(),nullptr);
        screen("lc_online");tick(child.data(),nullptr);ticks+=2000;begin();
        const auto before=menu_hosts;setup_request();assert(menu_hosts==before);
        script(0x49faa0,0);setup_request();assert(menu_hosts==before+1 && allocations==0);
    } else if(mode=="r6-nit-hosttokens") {
        for(unsigned n=0;n<40;++n) {
            const auto before=posts;setup_request();assert(posts==before+1);
        }
    } else if(mode=="r6-nit-discovery") {
        tunnel_ready=true;resolve();reply(1,true);reply();
        assert(*reinterpret_cast<unsigned*>(0xa3bc40+0x2208+0xc)==2);
        tick(child.data(),nullptr);
        assert(menu_view().state==bm::phase::room && *reinterpret_cast<unsigned*>(0xa3bc40+0x22bc));
        assert(*reinterpret_cast<unsigned*>(0xa53058+0x3a4+8)==0x01004d0a); // admit host, ignore another member
    } else if(mode=="r6-nit-tunnel") {
        resolve();ticks+=15000;tick(child.data(),nullptr);
        assert(menu_view().state==bm::phase::room && searches==0);
        ticks+=29999;tick(child.data(),nullptr);assert(menu_view().state==bm::phase::room && searches==0);
        ++ticks;tick(child.data(),nullptr);assert(menu_view().state==bm::phase::idle);
        post(intent::clear);tick(child.data(),nullptr);ticks+=2000;resolve();
        ticks+=44000;tick(child.data(),nullptr);assert(menu_view().state==bm::phase::room && searches==0);
        tunnel_ready=true;tick(child.data(),nullptr);assert(searches==1);
        reply();tick(child.data(),nullptr);event(8);tick(child.data(),nullptr);
        assert(calls.back()=="request:join" && menu_view().state==bm::phase::room);
    } else assert(false);
    mode=original_mode;
    std::cout<<"round6 review PASS "<<mode<<" (retail path; invented external services)\n";
}

// Item 1 (060p Browse crash at 0x587654): drive the actual retail anchor-vector push 0x587e40 (-> 0x587620, the
// faulting copy) that the playfield build 0x5882e0 / walk 0x587f90 uses for every ItemName anchor of a menu scene.
// 175 pushes stay inside the vector; the 182nd lands on the count and replaces it with the anchor pointer, so the
// 183rd would write to vector + pointer*4 (the live "writing <heap address>"). LC caps every scene at 175.
unsigned anchor_refs=0;
void __cdecl anchor_ref(void*) { ++anchor_refs; }
void anchor_capacity_run(const script_functions_rules::func_entry*) {
    const auto old_ref=*reinterpret_cast<unsigned*>(0x67f65c); // Alchemy igObject::ref import slot
    put(reinterpret_cast<void*>(0x67f65c),0,unsigned(reinterpret_cast<std::uintptr_t>(&anchor_ref)));
    std::vector<unsigned char> block(0x2d8+64,0xcc);
    std::memset(block.data(),0,0x2d8);
    auto* vec=block.data();
    std::array<std::array<unsigned,4>,200> anchors{};
    auto push=[&](unsigned i) { void* object=anchors[i].data();reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(0x587e40)(vec,nullptr,&object); };
    auto count=[&] { return *reinterpret_cast<unsigned*>(vec+0x2d4); };
    for(unsigned i=0;i<175;++i) push(i);
    assert(count()==175 && anchor_refs==175);
    for(unsigned i=0;i<175;++i) assert(*reinterpret_cast<void**>(vec+i*4)==anchors[i].data());
    for(unsigned b=0;b<5;++b) assert(*reinterpret_cast<unsigned*>(vec+0x2bc+b*4)==0xffffffffu);
    assert(*reinterpret_cast<unsigned*>(vec+0x2bc+5*4)==0x7fffu);
    for(unsigned i=0;i<64;++i) assert(block[0x2d8+i]==0xcc); // nothing past the vector yet
    for(unsigned i=175;i<182;++i) push(i);                      // no bound check: anchors 176..182
    const auto anchor181=unsigned(reinterpret_cast<std::uintptr_t>(anchors[181].data()));
    if(count()!=anchor181+1) { std::cerr<<"retail anchor vector changed: count "<<std::hex<<count()<<"\n";assert(false); }
    put(reinterpret_cast<void*>(0x67f65c),0,old_ref);
    std::cout<<"round9 anchor capacity PASS actual retail push 0x587e40/0x587620: 175 anchors fit; the 182nd overwrites the count with its pointer (next push = wild write, 060p 0x587654)\n";
}
// Item 3 (060p x4): after a failed hub Host, Change name -> Enter landed in Game Options. Drive the actual retail
// history push 0x5d74d0 (manager opens a menu: the outgoing one is stacked unless already stacked) and pop 0x5d7080
// (a menu closes without a deferred next, e.g. text_entry Enter) on the fixture manager, in the live order.
void history_run(const script_functions_rules::func_entry*) {
    restore_services();
    redirect(0x672562,reinterpret_cast<const void*>(static_cast<int(*)(const char*,const char*)>(&std::strcmp))); // CRT strcmp import thunk
    auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    std::array<std::array<unsigned char,0x400>,8> menus{};unsigned used=0;
    auto menu=[&](const char* name) {
        auto& m=menus[used++%menus.size()];m.fill(0);put(m.data(),0,0x69e41c);m[4]=1;std::strcpy(reinterpret_cast<char*>(m.data()+0xc),name);return m.data();
    };
    auto push=[&](const char* outgoing) { reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(0x5d74d0)(mgr.data(),nullptr,menu(outgoing)); };
    auto pop=[&](const char* closing) {
        std::array<char,0x8c> out{};reinterpret_cast<void*(__fastcall*)(void*,void*,void*,void*)>(0x5d7080)(mgr.data(),nullptr,out.data(),menu(closing));
        return std::string(out.data());
    };
    auto& depth=*reinterpret_cast<int*>(mgr.data()+0x86610);
    // Retail baseline: forward requests only, no trim -> the stale Game Options entry wins.
    depth=0;push("main");push("lc_online");push("lc_host");push("game_options"); // ... Post: request lc_host
    push("lc_host");push("lc_online");                                          // Back to the Hub, then Change name
    if(pop("text_entry")!="game_options") { std::cerr<<"retail history baseline changed\n";assert(false); }
    // With the dispatcher: the LC screen that becomes current trims its own stacked entry and everything above it.
    depth=0;push("main");push("lc_online");push("lc_host");push("game_options");
    screen("lc_host");tick(child.data(),nullptr);assert(depth==2);
    push("lc_host");screen("lc_online");tick(child.data(),nullptr);assert(depth==1);
    push("lc_online");                                                          // Change name opens text_entry
    const auto back=pop("text_entry");
    if(back!="lc_online") { std::cerr<<"text_entry closed to "<<back<<" instead of the Hub\n";assert(false); }
    // A native menu above a current-free stack is untouched (Other ways -> native online keeps its own Back).
    depth=0;push("main");push("lc_online");screen("lc_online");tick(child.data(),nullptr);assert(depth==1);
    push("lc_online");assert(depth==2);assert(pop("online")=="lc_online");
    // Hub Back -> main stacked lc_online above main; the next request from main trims it, so Options Back -> main.
    depth=0;push("main");push("lc_online");assert(depth==2);
    std::strcpy(reinterpret_cast<char*>(main_menu.data()+0xc),"main");set_current(main_menu.data());
    (*reinterpret_cast<void(__fastcall**)(void*,void*,const char*,const char*)>(0x6a23d8))(mgr.data(),nullptr,"options",nullptr);
    assert(depth==0 && calls.back()=="request:options");
    push("main");assert(pop("options")=="main");
    std::cout<<"round9 history PASS actual retail push 0x5d74d0 / pop 0x5d7080: failed Host -> Hub -> Change name -> Enter returns to the Hub; main -> Options -> Back returns to main\n";
}
}
