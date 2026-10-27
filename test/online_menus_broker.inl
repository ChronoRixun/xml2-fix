// Invented backend for the real menu dispatcher. No network or game launched.
namespace bm=online_browser_core;
bm::client_state menu_broker;
bm::ticket menu_reply{};
bm::operation menu_reply_op=bm::operation::none;
bool menu_auth=false,menu_complete=false;
unsigned menu_lists=0,menu_hosts=0,menu_resolves=0,menu_cancels=0,menu_leaves=0;
std::string menu_last_code,menu_last_name;
bool menu_last_public=false;
bool menu_refuse=false;
unsigned menu_resolve_attempts=0;
bool menu_has_preset=false;
bm::launch_preset menu_launch;
bool menu_available() { return true; }
const bm::client_view& menu_view() { return menu_broker.view(); }
bool menu_begin(bm::operation op) {
    if(!menu_broker.begin(op,ticks)) return false;
    menu_reply=menu_broker.view().generation;menu_reply_op=op;return true;
}
bool menu_list() { if(!menu_begin(bm::operation::list)) return false;++menu_lists;return true; }
bool menu_host(const std::string& name,bool public_room) { if(!menu_begin(bm::operation::host)) return false;++menu_hosts;menu_last_name=name;menu_last_public=public_room;return true; }
bool menu_resolve(const std::string& code) { ++menu_resolve_attempts;if(menu_refuse) { menu_broker.refuse("test_mode_on");return false; }if(!menu_begin(bm::operation::resolve)) return false;++menu_resolves;menu_last_code=code;return true; }
bool menu_retry() { return menu_begin(menu_view().action); }
void menu_cancel() { ++menu_cancels;menu_broker.cancel();menu_broker.stopped(); }
void menu_leave() { ++menu_leaves;menu_broker.cancel(true);menu_broker.stopped(); }
std::string menu_version() { return "FIXTURE"; }
bool menu_refresh_due() { return menu_broker.refresh_due(ticks); }
bool menu_take_preset(bm::launch_preset& out) { if(!menu_has_preset) return false;out=menu_launch;menu_has_preset=false;return true; }
void menu_tick() {
    if(menu_auth && menu_broker.pending()) { menu_broker.authenticated(menu_reply);menu_broker.issue(); }
    if(!menu_complete) return;
    menu_complete=false;
    if(menu_reply_op==bm::operation::list) {
        std::string json="{\"op\":\"list_rooms_ok\",\"rooms\":[";
        for(unsigned i=0;i<9;++i) {
            if(i) json+=',';
            json+="{\"code\":\"AAAAAAA";json+=alphabet[i];json+="\",\"name\":\"Remote % ^ \\\\ name\",\"version_token\":\"FIXTURE\"}";
        }
        menu_broker.listed(menu_reply,online_room_core::parse(json+"]}"),ticks);
    } else menu_broker.joined(menu_reply,menu_reply_op,menu_reply_op==bm::operation::host ? "ABCDEFGH" : menu_last_code,"Remote ^ % name");
}
const online_menus::broker_api menu_api{menu_available,menu_view,menu_tick,menu_list,menu_host,menu_resolve,menu_retry,menu_cancel,menu_leave,menu_version,menu_take_preset,menu_refresh_due};

void broker_menu_tests(const script_functions_rules::func_entry* registration) {
    using tick_t=void(__fastcall*)(void*,void*);
    const auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    auto post=[&](intent value) { int_value[1]=unsigned(value);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr); };
    auto screen=[&](const char* name) { std::strcpy(reinterpret_cast<char*>(child.data()+0xc),name);child[4]=1;set_current(child.data()); };
    assert(menu_lists==0 && menu_hosts==0 && menu_resolves==0 && texts["service"]=="Not connected");
    screen("lc_browse");tick(child.data(),nullptr);assert(menu_lists==1 && !visibility["lc_broker_auth"]);
    menu_auth=true;tick(child.data(),nullptr);assert(!visibility["lc_broker_list"]);
    menu_complete=true;tick(child.data(),nullptr);
    assert(menu_view().rows.size()==9 && texts["room_0_0"]=="Remote ? ? ? name" && texts["room_0_5"]=="--" && texts["room_0_6"]=="SAME");
    for(unsigned i=0;i<20;++i) { post(intent::refresh);tick(child.data(),nullptr); }
    assert(menu_lists==1);
    post(intent::next);tick(child.data(),nullptr);assert(!visibility["lc_row6_selected"]);
    post(intent::row_last);tick(child.data(),nullptr);ticks+=2000;
    post(intent::join_selected);tick(child.data(),nullptr);assert(menu_resolves==1 && menu_last_code=="AAAAAAAH");
    const auto old=menu_view().generation;menu_complete=true;post(intent::back);tick(child.data(),nullptr);
    assert(calls.back()=="request:lc_online" && menu_view().generation!=old && menu_view().state==bm::phase::idle && menu_cancels);
    set_current(hub.data());tick(hub.data(),nullptr);ticks+=2000;
    screen("lc_host");tick(child.data(),nullptr);assert(menu_hosts==0);
    post(intent::visibility);tick(child.data(),nullptr);post(intent::open_room);tick(child.data(),nullptr);
    assert(menu_hosts==1 && menu_last_public && bm::valid_room_name(menu_last_name));
    menu_complete=true;tick(child.data(),nullptr);assert(menu_view().state==bm::phase::room && texts["room_code"]=="ABCDEFGH");
    post(intent::back);tick(child.data(),nullptr);assert(menu_leaves==1 && menu_view().state==bm::phase::idle);
    set_current(hub.data());tick(hub.data(),nullptr);ticks+=2000;
    screen("lc_code");tick(child.data(),nullptr);
    for(int i=32;i<40;++i) { post(static_cast<intent>(i));tick(child.data(),nullptr); }
    assert(menu_resolves==2 && menu_view().state==bm::phase::resolving && visibility["lc_code_complete"] && !enabled_items["done"]);
    menu_complete=true;post(intent::back);tick(child.data(),nullptr);assert(texts["code_value"]=="ABCD-EFG_" && menu_view().state==bm::phase::idle);
    assert(!std::filesystem::exists(root/L"local/XML2 Fix/online/last-game.txt"));
    ticks+=2000;post(static_cast<intent>(39));tick(child.data(),nullptr);menu_complete=true;tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::room && std::filesystem::exists(root/L"local/XML2 Fix/online/last-game.txt"));
    auto record=std::ifstream(root/L"local/XML2 Fix/online/last-game.txt",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(record)),{});
    assert(decode_recent(bytes)->name=="Remote ? ? name");
    post(intent::erase);tick(child.data(),nullptr);assert(menu_leaves==2);
    screen("lc_browse");ticks+=10000;tick(child.data(),nullptr);assert(menu_lists==2);
    auto ticket=menu_view().generation;menu_auth=false;menu_complete=true;post(intent::back);tick(child.data(),nullptr);
    assert(menu_view().generation!=ticket && menu_view().state==bm::phase::idle && menu_lists==2);
    menu_launch={bm::operation::resolve,"23456789",{},false};menu_has_preset=true;
    set_current(hub.data());tick(hub.data(),nullptr);
    assert(calls.back()=="request:lc_code" && !menu_has_preset && menu_resolves==3);
    screen("lc_code");ticks+=2000;tick(child.data(),nullptr);assert(texts["code_value"]=="2345-6789" && menu_resolves==4);
    post(intent::clear);tick(child.data(),nullptr);post(intent::back);tick(child.data(),nullptr);
    menu_launch={bm::operation::host,{},"Preset room",true};menu_has_preset=true;
    set_current(hub.data());tick(hub.data(),nullptr);assert(calls.back()=="request:lc_host" && menu_hosts==1);
    screen("lc_host");tick(child.data(),nullptr);ticks+=2000;post(intent::open_room);tick(child.data(),nullptr);
    assert(menu_hosts==2 && menu_last_public && menu_last_name=="Preset room");
    post(intent::back);tick(child.data(),nullptr);set_current(hub.data());tick(hub.data(),nullptr);
    ticks+=2000;screen("lc_code");tick(child.data(),nullptr);menu_refuse=true;
    for(int i=32;i<40;++i) { post(static_cast<intent>(i));tick(child.data(),nullptr); }
    const auto attempts=menu_resolve_attempts;
    for(unsigned i=0;i<20;++i) tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::refused && menu_resolve_attempts==attempts && visibility["lc_code_complete"] && !visibility["lc_broker_error"]);
    post(intent::done);tick(child.data(),nullptr);assert(menu_resolve_attempts==attempts+1);
    std::cout<<"round5 menus PASS Hub no host; row allowlist/RTT/build; bounded page/select; refresh limiter; host visibility; Back/edit before late completion; confirmed Rejoin storage\n";
}
