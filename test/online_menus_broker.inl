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
bool menu_up=true;
std::string menu_rows_json,menu_error,menu_error_detail; // R5b: invented list body / failure id for the next completion (R15: the other build's token)
bool menu_available() { return menu_up; }
const bm::client_view& menu_view() { return menu_broker.view(); }
bool menu_begin(bm::operation op) {
    if(!menu_broker.begin(op,ticks)) return false;
    menu_reply=menu_broker.view().generation;menu_reply_op=op;return true;
}
bool menu_list() { if(!menu_begin(bm::operation::list)) return false;++menu_lists;return true; }
bool menu_host(const std::string& name,bool public_room) { if(!menu_begin(bm::operation::host)) return false;++menu_hosts;menu_last_name=name;menu_last_public=public_room;return true; }
bool menu_resolve(const std::string& code) { ++menu_resolve_attempts;if(menu_refuse) { menu_broker.refuse("test_mode_on");return false; }if(!menu_begin(bm::operation::resolve)) return false;++menu_resolves;menu_last_code=code;return true; }
// Mirrors online_room::retry(): only a retry_wait/error failure can be retried (the refused phase cannot).
bool menu_retry() { const auto s=menu_view().state;if(s!=bm::phase::retry_wait && s!=bm::phase::error) return false;return menu_begin(menu_view().action); }
void menu_cancel() { ++menu_cancels;menu_broker.cancel();menu_broker.stopped(); }
void menu_leave() { ++menu_leaves;menu_broker.cancel(true);menu_broker.stopped(); }
std::string menu_version() { return "FIXTURE"; }
bool menu_refresh_due() { return menu_broker.refresh_due(ticks); }
bool menu_take_preset(bm::launch_preset& out) { if(!menu_has_preset) return false;out=menu_launch;menu_has_preset=false;return true; }
void menu_tick() {
    if(menu_auth && menu_broker.pending()) { menu_broker.authenticated(menu_reply);menu_broker.issue(); }
    if(!menu_complete) return;
    menu_complete=false;
    if(!menu_error.empty()) { menu_broker.fail(menu_reply,menu_error,ticks);if(!menu_error_detail.empty()) menu_broker.detail(menu_reply,menu_error_detail);menu_error.clear();return; }
    if(menu_reply_op==bm::operation::list && !menu_rows_json.empty()) {
        menu_broker.listed(menu_reply,online_room_core::parse("{\"op\":\"list_rooms_ok\",\"rooms\":["+(menu_rows_json=="-" ? std::string() : menu_rows_json)+"]}"),ticks);
    } else if(menu_reply_op==bm::operation::list) {
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
    redirect(0x5adf40,focus_stub);
    using tick_t=void(__fastcall*)(void*,void*);
    const auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    auto post=[&](intent value) { int_value[1]=unsigned(value);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr); };
    auto screen=[&](const char* name) { std::strcpy(reinterpret_cast<char*>(child.data()+0xc),name);child[4]=1;set_current(child.data()); };
    assert(menu_lists==0 && menu_hosts==0 && menu_resolves==0 && texts["service"]=="Not connected");
    screen("lc_browse");tick(child.data(),nullptr);assert(menu_lists==1 && !visibility["lc_broker_auth"]);
    menu_auth=true;tick(child.data(),nullptr);assert(!visibility["lc_broker_list"]);
    menu_complete=true;tick(child.data(),nullptr);
    assert(menu_view().rows.size()==9 && texts["room_0_0"]=="Remote ? ? ? name" && texts["room_0_6"].empty() && !visibility["lc_row0_build_same"] && visibility["lc_row0_build_other"]);
    if(mode=="r5-browse-empty") {
        assert(focused=="room_0_0");
        menu_rows_json="-";ticks+=2100;post(intent::refresh);tick(child.data(),nullptr);
        menu_complete=true;tick(child.data(),nullptr);
        assert(menu_view().rows.empty() && focused=="prompt_refresh" && enabled_items["prompt_refresh"] && texts["prompt_refresh"]=="F5 Refresh");
        menu_rows_json.clear();ticks+=2100;post(intent::refresh);tick(child.data(),nullptr);
        menu_complete=true;tick(child.data(),nullptr);assert(menu_view().rows.size()==9 && focused=="room_0_0");
        std::cout<<"060s Browse focus PASS populated -> empty -> populated keeps an enabled visible target\n";return;
    }
    if(mode=="r5-browse-join" || mode=="r5-browse-command") {
        if(mode=="r5-browse-join") {
            assert(focused=="room_0_0");assert(texts["prompt_refresh"]=="F5 Refresh");
        }
        // 060q live failure 2: Join right after a list, as a player does (the shared 2 s lookup limiter still closed), and
        // with the auto refresh's list in flight. The old fixtures advanced the clock 2 s before every Join; the real
        // Browse never waits, so the single resolve call was refused and nothing happened.
        const auto before=menu_resolves;
        post(intent::join_selected);tick(child.data(),nullptr);
        assert(menu_resolves==before && menu_resolve_attempts>0 && menu_view().state==bm::phase::idle); // held, not dropped
        for(unsigned i=0;i<3;++i) { ticks+=500;tick(child.data(),nullptr);assert(menu_lists==1); }   // no refresh meanwhile
        ticks+=600;tick(child.data(),nullptr);
        assert(menu_resolves==before+1 && menu_last_code=="AAAAAAAA" && menu_view().state==bm::phase::resolving);
        menu_complete=true;post(intent::back);tick(child.data(),nullptr);assert(menu_view().state==bm::phase::idle);
        // Back on Browse, a list in flight (the 10 s auto refresh): the selected row's own command (Enter / pad A / click
        // on the focused row) joins; the refresh is cancelled, the limiter's wait passes, the room resolves.
        screen("lc_browse");ticks+=11000;tick(child.data(),nullptr);menu_complete=true;tick(child.data(),nullptr);
        assert(menu_view().rows.size()==9);
        // 060s row mouse-down/up may activate a row before an intervening binding tick updates browse_selected.
        // One activation on a DIFFERENT row must join, including while a list is in flight.
        ticks+=11000;tick(child.data(),nullptr);assert(menu_broker.pending() && menu_view().action==bm::operation::list);
        const auto cancels=menu_cancels;
        assert(enabled_items["room_0_0"] && enabled_items["prompt_join"]); // real Enter/click must reach the command while Refresh is in flight
        post(static_cast<intent>(int(intent::row_first)+1));tick(child.data(),nullptr);       // the selected row again: Join
        assert(menu_cancels==cancels+1 && menu_resolves==before+1);                            // refresh cancelled, limiter closed
        ticks+=2100;tick(child.data(),nullptr);
        assert(menu_resolves==before+2 && menu_last_code=="AAAAAAAB" && menu_view().state==bm::phase::resolving);
        std::cout<<"round5 menus r5-browse-join PASS (a Join right after a list is held until the 2 s limiter opens; a Join over "
            "an in-flight refresh cancels it; the selected row's command joins; no refresh starts while a Join waits)\n";
        return;
    }
    for(unsigned i=0;i<20;++i) { post(intent::refresh);tick(child.data(),nullptr); }
    assert(menu_lists==1);
    // One page = browse_rows rows (060p: seven rows overflowed the engine's 175 scene anchors). Next selects row 5, the
    // page scrolls by one, the last row of the page is the selected one; row commands past the page are refused.
    const auto selected_shown=[&](unsigned r) { const auto k="lc_row"+std::to_string(r)+"_selected";return visibility.count(k) && !visibility[k]; };
    post(intent::next);tick(child.data(),nullptr);assert(selected_shown(browse_rows-1));
    post(intent::row_last);tick(child.data(),nullptr);assert(selected_shown(browse_rows-1));
    post(static_cast<intent>(int(intent::row_first)+int(browse_rows)-1));tick(child.data(),nullptr);ticks+=2000;
    post(intent::join_selected);tick(child.data(),nullptr);assert(menu_resolves==1 && menu_last_code=="AAAAAAAH");
    const auto old=menu_view().generation;menu_complete=true;post(intent::back);tick(child.data(),nullptr);
    assert(calls.back()=="request:lc_online" && menu_view().generation!=old && menu_view().state==bm::phase::idle && menu_cancels);
    set_current(hub.data());tick(hub.data(),nullptr);ticks+=2000;
    screen("lc_host");tick(child.data(),nullptr);assert(menu_hosts==0);
    post(intent::visibility);tick(child.data(),nullptr);post(intent::open_room);tick(child.data(),nullptr);
    assert(menu_hosts==0 && calls.back()=="request:game_options");
    post(intent::back);tick(child.data(),nullptr);assert(menu_leaves==0 && menu_view().state==bm::phase::idle);
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
    post(intent::erase);tick(child.data(),nullptr);assert(menu_leaves==1);
    screen("lc_browse");ticks+=10000;tick(child.data(),nullptr);assert(menu_lists==2);
    auto ticket=menu_view().generation;menu_auth=false;menu_complete=true;post(intent::back);tick(child.data(),nullptr);
    assert(menu_view().generation!=ticket && menu_view().state==bm::phase::idle && menu_lists==2);
    menu_launch={bm::operation::resolve,"23456789",{},false};menu_has_preset=true;
    set_current(hub.data());tick(hub.data(),nullptr);
    assert(calls.back()=="request:lc_code" && !menu_has_preset && menu_resolves==3);
    screen("lc_code");ticks+=2000;tick(child.data(),nullptr);assert(texts["code_value"]=="2345-6789" && menu_resolves==4);
    post(intent::clear);tick(child.data(),nullptr);post(intent::back);tick(child.data(),nullptr);
    menu_launch={bm::operation::host,{},"Preset room",true};menu_has_preset=true;
    set_current(hub.data());tick(hub.data(),nullptr);assert(calls.back()=="request:lc_host" && menu_hosts==0);
    screen("lc_host");tick(child.data(),nullptr);ticks+=2000;post(intent::open_room);tick(child.data(),nullptr);
    assert(menu_hosts==0 && calls.back()=="request:game_options");
    post(intent::back);tick(child.data(),nullptr);set_current(hub.data());tick(hub.data(),nullptr);
    ticks+=2000;screen("lc_code");tick(child.data(),nullptr);menu_refuse=true;
    for(int i=32;i<40;++i) { post(static_cast<intent>(i));tick(child.data(),nullptr); }
    const auto attempts=menu_resolve_attempts;
    for(unsigned i=0;i<20;++i) tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::refused && menu_resolve_attempts==attempts && visibility["lc_code_complete"] && !visibility["lc_broker_err_test_mode"] && visibility["retry"]);
    post(intent::done);tick(child.data(),nullptr);assert(menu_resolve_attempts==attempts+1);
    std::cout<<"round5 menus PASS Hub no host; row allowlist/RTT/build; bounded page/select; refresh limiter; host visibility; Back/edit before late completion; confirmed Rejoin storage\n";
}

// Combined round-5 broker data and the reviewed retail participant-list path.
// No lobby_fields provider: code/title/visibility come through the production
// broker snapshot branch, while native cards use real insert/lookup/refresh.
void broker_retail_lobby_tests(const script_functions_rules::func_entry* registration) {
    const auto base_tick=*reinterpret_cast<lobby_tick_t*>(0x69e454);
    auto post=[&](intent value) { int_value[1]=unsigned(value);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr); };
    menu_refuse=false;menu_auth=true;menu_complete=false;menu_cancel();ticks+=10000;
    std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_browse");set_current(child.data());base_tick(child.data(),nullptr);
    assert(menu_view().action==bm::operation::list && menu_broker.pending());
    menu_complete=true;base_tick(child.data(),nullptr);assert(menu_view().rows.size()==9);
    post(intent::previous);base_tick(child.data(),nullptr); // reset the prior fixture's scrolled viewport
    post(static_cast<intent>(int(intent::row_first)+1));base_tick(child.data(),nullptr);ticks+=2000;
    post(intent::join_selected);base_tick(child.data(),nullptr);menu_complete=true;base_tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::room && menu_view().code=="AAAAAAAB");
    menu_broker.metadata("Broker ^ % title",true);
    const auto saved_mode=mode;mode="r4-broker-combined"; // enable native presentation service stubs only
    import_stub("OpenClipboard",reinterpret_cast<void*>(&copy_open));import_stub("CloseClipboard",reinterpret_cast<void*>(&copy_close));
    import_stub("EmptyClipboard",reinterpret_cast<void*>(&copy_empty));import_stub("SetClipboardData",reinterpret_cast<void*>(&copy_set));
    import_stub("EnumThreadWindows",reinterpret_cast<void*>(&copy_enum));
    import_stub("GetWindowThreadProcessId",reinterpret_cast<void*>(&copy_window_process));import_stub("GetClassNameA",reinterpret_cast<void*>(&copy_class));
    for(bool host:{true,false}) for(unsigned count=0;count<=4;++count) {
        people_count=count;setup_lobby(host);mask=0;
        const auto activate=*reinterpret_cast<lobby_activate_t*>(host ? 0x69f284 : 0x69f34c);
        const auto tick=*reinterpret_cast<lobby_tick_t*>(host ? 0x69f2ac : 0x69f374);
        activate(child.data(),nullptr,0);tick(child.data(),nullptr);
        assert(texts["lc_room_code"]=="AAAA-AAAB" && texts["lc_room_name"]=="Broker ? ? title" && texts["lc_room_visibility"]=="Public");
        assert(texts["lc_member_count"]==std::to_string(count)); // never nine broker Browse rows
        if(!host && count<2) { assert(current()!=child.data());continue; } // retail Join exits when there is no local participant
        for(unsigned i=0;i<count;++i) {
            const auto prefix="lc_p"+std::to_string(i+1)+"_";
            assert(!visibility[prefix+"card"] && visibility[prefix+"same_build"] && texts[prefix+"ping"].empty());
            assert(visibility[prefix+"you"]==(participant_ids[i]!=local_id));
        }
        assert(!*reinterpret_cast<void**>(people_table.data()+8));
        // Both +a0 and +ac aliases run their actual retail refresh and preserve
        // a non-NULL native iterator cursor through the UI snapshot read.
        for(unsigned slot:{host ? 0x69f314u : 0x69f3dcu,host ? 0x69f320u : 0x69f3e8u}) {
            auto refresh=*reinterpret_cast<lobby_tick_t*>(slot);refresh(child.data(),nullptr);
            auto* cursor=count ? people_nodes[count-1] : nullptr;put(people_table.data(),8,unsigned(reinterpret_cast<std::uintptr_t>(cursor)));
            tick(child.data(),nullptr);assert(*reinterpret_cast<void**>(people_table.data()+8)==cursor);
            assert(texts["lc_member_count"]==std::to_string(count) && texts["lc_room_code"]=="AAAA-AAAB");
        }
        const auto old_copies=copies;post(intent::copy_code);tick(child.data(),nullptr);
        if(copies!=old_copies+1 || copied_code!=L"AAAA-AAAB") std::cerr<<"combined Copy host="<<host<<" count="<<count<<" copies="<<copies<<" previous="<<old_copies<<" opens="<<copy_opens<<" enabled="<<enabled_items["lc_copy_code"]<<" current="<<(current()==child.data())<<"\n";
        assert(copies==old_copies+1 && copied_code==L"AAAA-AAAB");post(intent::dismiss);tick(child.data(),nullptr);
    }
    // An invalid native row count fails closed without losing broker code or
    // suppressing the native escape path; recovery restores the real cards.
    people_count=3;setup_lobby(false);auto activate=*reinterpret_cast<lobby_activate_t*>(0x69f34c);activate(child.data(),nullptr,0);
    auto tick=*reinterpret_cast<lobby_tick_t*>(0x69f374);put(checks_table.data(),0x84,2);
    tick(child.data(),nullptr);assert(texts["lc_member_count"]=="0" && texts["lc_room_code"]=="AAAA-AAAB");
    refresh_now();tick(child.data(),nullptr);assert(texts["lc_member_count"]=="3");
    // Round 15 inline lobby scenes: a member leaving the native roster is named for six seconds (reported once, a join is
    // not a departure); losing the service while the room stays shows the strip. The lobby is never closed.
    tick(child.data(),nullptr);assert(visibility["lc_lobby_guest_left"] && texts["lc_toast_name"].empty() && visibility["lc_lobby_service_lost"]);
    people_count=2;build_people_chain();refresh_now();tick(child.data(),nullptr);
    assert(!visibility["lc_lobby_guest_left"] && texts["lc_toast_name"]=="Alex" && current()==child.data());
    ticks+=5999;tick(child.data(),nullptr);assert(!visibility["lc_lobby_guest_left"] && texts["lc_toast_name"]=="Alex");
    ticks+=1;tick(child.data(),nullptr);assert(visibility["lc_lobby_guest_left"] && texts["lc_toast_name"].empty());
    people_count=3;build_people_chain();refresh_now();tick(child.data(),nullptr);assert(visibility["lc_lobby_guest_left"]);
    menu_broker.disconnected("broker_unreachable",ticks);tick(child.data(),nullptr);
    assert(!visibility["lc_lobby_service_lost"] && menu_view().state==bm::phase::room && current()==child.data());
    mode=saved_mode;
    std::cout<<"round5 combined PASS Browse resolve -> broker code/title/visibility + retail NULL list, Host/Join 0..4, real refresh aliases, cursor preservation, Copy and malformed-list recovery\n";
}

// Round 5b: every row/list/error/service selector the DLL publishes, with invented broker bodies only.
void broker_presentation_tests(const script_functions_rules::func_entry* registration) {
    redirect(0x5adf40,focus_stub);
    using tick_t=void(__fastcall*)(void*,void*);
    const auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    auto post=[&](intent value) { int_value[1]=unsigned(value);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr); };
    auto screen=[&](const char* name) { std::strcpy(reinterpret_cast<char*>(child.data()+0xc),name);child[4]=1;set_current(child.data()); };
    auto shown=[&](const std::string& key) { return visibility.count(key) && !visibility[key]; };
    auto gone=[&](const std::string& key) { return visibility.count(key) && visibility[key]; };
    // Pure selectors first: boundaries, wraparound and hostile ids.
    {   // 060q live: a screen name that does not fit its field ends in "..." (never clipped silently, never empty, never longer).
        assert(fit_name("Player",112,1.0)=="Player" && fit_name("Logan",64*.8,.69)=="Logan" && fit_name("",112,1.0).empty());
        assert(fit_name("abcdefghijklmn",112,1.0)=="abcdefghijklmn"); // 14 lowercase letters fit the 140-unit title field
        for(const std::string full:{std::string(31,'W'),std::string(31,'a'),std::string("The Long Named Player Of Doom"),std::string(31,'i'),std::string("Wolverine Wolverine Wolverine!!")}) {
            for(const auto [limit,scale]:{std::pair{112.0,1.0},std::pair{51.2,.69},std::pair{144.0,.69}}) {
                const auto cut=fit_name(full,limit,scale);
                if(name_units(full)*scale<=limit) { assert(cut==full);continue; }
                assert(cut.size()>=4 && cut.ends_with("...") && full.starts_with(cut.substr(0,cut.size()-3)) && cut.size()<full.size()+3);
                assert((name_units(cut)*scale)<=limit || cut.size()==4); // fits, or is the single leading character the field can hold
            }
        }
        assert(fit_name(std::string(31,'i'),112,1.0).ends_with("...")==(name_units(std::string(31,'i'))>112));
    }
    assert(age_text(0)=="Updated 0 s ago" && age_text(119999)=="Updated 119 s ago" && age_text(120000)=="Updated 2 min ago");
    assert(age_text(5999999)=="Updated 99 min ago" && age_text(6000000)=="Updated long ago");
    assert(age_text(std::uint32_t(5u-0xfffffffcu))=="Updated 0 s ago"); // 9 ms across a 32-bit wrap
    assert(age_segments(0)==0 && age_segments(1999)==0 && age_segments(2000)==1 && age_segments(9999)==4 && age_segments(10000)==5 && age_segments(99999999)==5);
    assert(relative_age(100,0).empty() && relative_age(100,200)=="just now" && relative_age(7300,100)=="2 hours ago" && relative_age(160,100)=="1 minute ago");
    assert(relative_age(100+86400,100)=="1 day ago" && relative_age(100+86400ull*400,100)=="a long time ago");
    assert(classify_row(4,4,true,false)==row_status::full && classify_row(-1,4,true,false)==row_status::lobby && classify_row(2,4,true,true)==row_status::progress);
    assert(classify_row(2,4,false,false)==row_status::closed && classify_row(0,1,true,false)==row_status::lobby && pip_count(-1)==0 && pip_count(9)==4);
    assert(relate_build("LC33","LC33")==build_relation::same && relate_build("LC33","LC34")==build_relation::other);
    assert(relate_build("","LC33")==build_relation::check && relate_build("LC33","")==build_relation::check && relate_build("","")==build_relation::check);
    assert(classify_error("")==error_cause::generic && classify_error("no_such_room")==error_cause::code_not_found && classify_error("unsupported_version")==error_cause::refused);
    assert(classify_error(std::string(4000,'x'))==error_cause::generic && classify_error("room_full ")==error_cause::generic && classify_error("TIMEOUT")==error_cause::generic);

    // Browse with eight invented rows covering every status, build relation, pip count and the hostile/maximal row.
    auto code=[&](unsigned i) { return std::string("\"code\":\"AAAAAAA")+alphabet[i]+"\""; };
    menu_rows_json="{"+code(0)+",\"name\":\"Weekend co-op\",\"host_display_name\":\"Logan\",\"zone_name\":\"Mansion\",\"players\":2,\"max_players\":4,\"joinable\":true,\"version_token\":\"FIXTURE\"},"
        "{"+code(1)+",\"name\":\"Kitty's game\",\"players\":3,\"in_progress\":true,\"joinable\":true,\"version_token\":\"FIXTURE\"},"
        "{"+code(2)+",\"name\":\"Full house\",\"players\":4,\"max_players\":4,\"joinable\":false,\"version_token\":\"FIXTURE\"},"
        "{"+code(3)+",\"name\":\"Other build\",\"players\":1,\"joinable\":true,\"version_token\":\"LC36\"},"
        "{"+code(4)+",\"name\":\"No data\"},"
        "{"+code(5)+",\"name\":\"Closed lobby\",\"players\":1,\"joinable\":false,\"in_progress\":false,\"version_token\":\"FIXTURE\"},"
        "{"+code(6)+",\"name\":\"Passworded\",\"players\":1,\"password_protected\":true,\"version_token\":\"FIXTURE\"},"
        "{"+code(7)+",\"name\":\""+std::string(180,'A')+"%s%n~^|;$\",\"host_display_name\":\""+std::string(120,'H')+"\",\"zone_name\":\""+std::string(180,'Z')+"\",\"players\":0,\"version_token\":\"ABCDEFGHIJKLMNOPQRSTUVWXYZ012345\"}";
    screen("lc_browse");tick(child.data(),nullptr);menu_auth=true;tick(child.data(),nullptr);
    menu_complete=true;tick(child.data(),nullptr);
    const auto done_at=ticks;
    assert(menu_view().rows.size()==8 && menu_view().list_known && !menu_view().stale);
    assert(texts["list_count"]=="- 8" && texts["list_age"]=="Updated 0 s ago" && gone("lc_age_fill1"));
    assert(texts["self_initial"].size()==1 && texts["self_initial"]==std::string(1,char(std::toupper(texts["self_name"][0]))));
    ticks+=4000;tick(child.data(),nullptr);
    assert(texts["list_age"]=="Updated 4 s ago" && shown("lc_age_fill1") && shown("lc_age_fill2") && gone("lc_age_fill3") && gone("lc_age_fill5"));
    // Connected service: the scene shows, the plain text stays empty so the word is never drawn twice.
    assert(shown("lc_service_ok") && texts["service"].empty() && texts["self_state"].empty());
    const char* kinds[]{"lobby","progress","full","lobby","lobby","closed","lobby"};
    const int pips[]{2,3,4,1,0,1,1};
    const char* builds[]{"same","same","same","other","check","same","same"};
    const bool dims[]{false,false,true,true,false,true,true};
    for(unsigned r=0;r<browse_rows;++r) {
        const auto id="lc_row"+std::to_string(r);
        for(const char* k:row_status_key) assert((std::string(k)==kinds[r] ? shown(id+"_st_"+k) : gone(id+"_st_"+k)));
        for(int k=0;k<=4;++k) assert((k==pips[r] ? shown(id+"_pips_"+std::to_string(k)) : gone(id+"_pips_"+std::to_string(k))));
        for(const char* k:build_key) assert((std::string(k)==builds[r] ? shown(id+"_build_"+k) : gone(id+"_build_"+k)));
        assert(dims[r] ? shown(id+"_dim") : gone(id+"_dim"));
        // The status word, the ping "--" (no measurement exists) and SAME/CHECK are scenes: no DLL text, no cells [4]/[5].
        assert(!texts.count("room_"+std::to_string(r)+"_4") && !texts.count("room_"+std::to_string(r)+"_5"));
    }
    assert(texts["room_0_3"]=="2/4" && texts["room_4_3"]=="--/4" && texts["room_0_2"]=="Mansion" && texts["room_1_2"]=="--");
    assert(texts["room_3_6"]=="LC36" && texts["room_0_6"].empty() && texts["room_4_6"].empty()); // only the other build's token
    assert(texts["details"]=="Friends join at the next area - Same build as you (FIXTURE)" && gone("lc_broker_empty"));
    // Selecting the other-build row (row 3): the line names both tokens; its Join stays off.
    focus_stub(child.data(),nullptr,"room_3_0",true);tick(child.data(),nullptr);
    assert(texts["details"]=="Friends join at the next area - Other build (LC36 vs your FIXTURE)" && !enabled_items["prompt_join"]);
    focus_stub(child.data(),nullptr,"room_4_0",true);tick(child.data(),nullptr);
    assert(texts["details"]=="Friends join at the next area - Build not checked");
    // Hostile / maximal row, on the page that shows it: every bound field is bounded, ASCII-allowlisted and `%` free.
    post(intent::next);tick(child.data(),nullptr);
    unsigned hostile=browse_rows;
    for(unsigned r=0;r<browse_rows;++r) if(texts["room_"+std::to_string(r)+"_0"]==std::string(24,'A')) hostile=r;
    assert(hostile<browse_rows);
    const auto hostile_key=[&](unsigned c) { return "room_"+std::to_string(hostile)+"_"+std::to_string(c); };
    for(unsigned c=0;c<7;++c) {
        const auto& t=texts[hostile_key(c)];
        static const unsigned limits[]{24,12,17,5,13,9,8};
        assert(t.size()<=limits[c]);
        if(c!=3) for(unsigned char ch:t) assert(display_byte(ch)==char(ch)); // [3] is the DLL's own "n/4"
    }
    assert(texts[hostile_key(0)]==std::string(24,'A') && texts[hostile_key(6)]=="ABCDEFGH" && texts["details"].find('%')==std::string::npos);
    // Oversized body (one name byte over the broker's 192): the whole list is refused, the old rows stay labelled stale.
    menu_rows_json="{"+code(0)+",\"name\":\""+std::string(193,'N')+"\"}";
    ticks+=70000;tick(child.data(),nullptr);assert(menu_broker.pending());
    menu_auth=true;menu_complete=true;tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::retry_wait && menu_view().error=="malformed_reply" && menu_view().stale && menu_view().rows.size()==8);
    assert(shown("lc_broker_stale") && shown("lc_broker_err_generic") && shown("retry") && enabled_items["retry"] && !enabled_items["prompt_join"]);
    assert(texts["list_age"]==age_text(ticks-done_at) && texts["list_count"]=="- 8"); // the age keeps counting from the last good list
    // A stale list far in the past reads in minutes.
    ticks=done_at+125000;tick(child.data(),nullptr);assert(texts["list_age"]=="Updated 2 min ago" && shown("lc_age_fill5"));

    // Every cause selects exactly one scene; Retry is shown only where trying again can help.
    struct cause_case { const char* id; const char* node; bool retry; };
    const cause_case causes[]{{"no_such_room","code_not_found",false},{"room_closed","code_expired",false},{"room_full","room_full",false},
        {"room_not_joinable","room_closed",true},{"version_mismatch","version_mismatch",false},{"rate_limited","rate_limited",true},
        {"timeout","timeout",true},{"broker_unreachable","offline",true},{"worker_unavailable","offline",true},{"tls_pin_mismatch","pin_mismatch",false},
        {"broker_rejected","refused",false},{"malformed_reply","generic",true},{"weird%s\\^id","generic",true}};
    menu_rows_json.clear();
    for(const auto& c:causes) {
        ticks+=70000;
        if(menu_view().state==bm::phase::retry_wait) post(intent::retry);
        tick(child.data(),nullptr);assert(menu_broker.pending());
        menu_auth=true;menu_error=c.id;menu_complete=true;tick(child.data(),nullptr);
        assert(menu_view().state==bm::phase::retry_wait);
        unsigned visible=0;
        for(const char* k:error_cause_key) {
            const auto node=std::string("lc_broker_err_")+k;
            if(shown(node)) { ++visible;assert(std::string(k)==c.node); }
            else assert(gone(node));
        }
        assert(visible==1 && shown(std::string("lc_broker_err_")+c.node));
        assert(c.retry ? (shown("retry") && enabled_items["retry"]) : (gone("retry") && !enabled_items["retry"]));
        assert(gone("lc_service_ok") && texts["service"]=="Unavailable"); // not connected during a failure
        for(unsigned char ch:texts["broker_error"]) assert(display_byte(ch)==char(ch) || ch==':'); // the diagnostic is "<operation>: <known id>"
        assert(texts["broker_error"].size()<=64);
    }
    // The client's refused phase (guards, test mode) never offers Retry whatever the id.
    ticks+=70000;post(intent::retry);tick(child.data(),nullptr);menu_broker.cancel();menu_broker.stopped();
    menu_broker.refuse("test_mode_on");tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::refused && shown("lc_broker_err_test_mode") && gone("retry") && !enabled_items["retry"]);
    // Empty list: the table frame's message belongs to lc_broker_empty alone (the detail line stays empty).
    menu_broker.cancel();menu_broker.stopped();menu_rows_json="-";
    ticks+=70000;tick(child.data(),nullptr);assert(menu_broker.pending());
    menu_auth=true;menu_complete=true;tick(child.data(),nullptr);
    assert(menu_view().rows.empty() && menu_view().list_known && !menu_view().stale);
    assert(texts["list_count"]=="- 0" && shown("lc_broker_empty") && texts["details"].empty() && texts["details_name"].empty());
    for(const char* kind:row_status_key) assert(gone(std::string("lc_row0_st_")+kind));
    for(unsigned r=0;r<browse_rows;++r) assert(gone("lc_row"+std::to_string(r)+"_dim") && gone("lc_row"+std::to_string(r)+"_build_check") && gone("lc_row"+std::to_string(r)+"_pips_0"));

    // 060s live: an empty cached list read "0 last known games". Exercise the
    // production tick/binder after a failed refresh, preserving the real empty list.
    assert(texts["browse_hint"]=="No public games");
    ticks+=70000;post(intent::refresh);tick(child.data(),nullptr);
    menu_error="broker_unreachable";menu_complete=true;tick(child.data(),nullptr);
    assert(menu_view().stale && menu_view().rows.empty());
    assert(texts["browse_hint"]=="No games in the last list");
    assert(shown("lc_broker_empty") && texts["lc_broker_empty"]=="No games in the last list");
    ticks+=70000;menu_rows_json="{"+code(0)+",\"name\":\"Only room\"}";
    post(intent::refresh);tick(child.data(),nullptr);menu_complete=true;tick(child.data(),nullptr);
    assert(texts["browse_hint"]=="1 public game");
    ticks+=70000;post(intent::refresh);tick(child.data(),nullptr);
    menu_error="broker_unreachable";menu_complete=true;tick(child.data(),nullptr);
    assert(texts["browse_hint"]=="1 last known game");

    // A committed room whose broker went away: the offline-room line shows, the stale label never beside it.
    menu_broker.cancel();menu_broker.stopped();ticks+=70000;
    assert(menu_resolve("AAAAAAAB") && menu_broker.pending());
    menu_auth=true;menu_complete=true;tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::room && shown("lc_broker_room") && gone("lc_broker_offline_room") && gone("lc_broker_stale"));
    menu_broker.disconnected("broker_unreachable",ticks);tick(child.data(),nullptr);
    assert(menu_view().stale && shown("lc_broker_offline_room") && gone("lc_broker_stale") && shown("lc_broker_room") && gone("lc_broker_err_offline"));
    assert(texts["self_state"]=="In a room" && gone("lc_service_ok"));

    // Hub: a service never asked shows neutral text, the connected scene hidden.
    menu_broker.cancel();menu_broker.stopped();set_current(hub.data());tick(hub.data(),nullptr);
    assert(gone("lc_service_ok") && texts["service"]=="Not connected");
    // No broker at all: a consistent private host card from the first frame, no error scene, no Retry.
    menu_up=false;screen("lc_host");tick(child.data(),nullptr);
    assert(shown("lc_host_private") && gone("lc_host_public") && gone("retry") && gone("lc_service_ok") && texts["service"]=="Unavailable");
    for(const char* k:error_cause_key) assert(gone(std::string("lc_broker_err_")+k));
    // 0.6 preview fix L (060r live failure 7): the amber side marker was baked beside CAMPAIGN and stayed there while the focus bar moved
    // to VISIBILITY (the earlier tests never moved the focus on the Host screen). It is one scene per focusable settings row now and
    // follows the focused row; none while the focus is on the Open bar / a prompt (or nowhere yet).
    {
        const char* markers[]{"lc_host_marker_visibility","lc_host_marker_room_name","lc_host_marker_campaign"};
        const std::pair<const char*,const char*> rows[]{{"visibility",markers[0]},{"room_name",markers[1]},{"edit_room",markers[1]},{"campaign",markers[2]}};
        for(const auto& row:rows) {
            put(child.data(),0x324,static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(item_stub(nullptr,nullptr,row.first))));tick(child.data(),nullptr);
            for(const char* m:markers) assert(std::string_view(m)==row.second ? shown(m) : gone(m));
        }
        put(child.data(),0x324,0);tick(child.data(),nullptr);for(const char* m:markers) assert(gone(m));
    }
    menu_up=true;
    std::cout<<"round5b views PASS row status/pips/build/dim selectors, list count + age (wrap/stale), every error cause + Retry visibility, empty list, service scene, host pair without broker, hostile/oversized bodies\n";
}
