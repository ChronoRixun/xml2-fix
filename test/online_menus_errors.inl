// Round 15: the complete error table (INGAME-DESIGN section 3.3) - table-driven cause -> scene / next action for every row,
// redacted diagnostics, every button valid under busy/cancel, duplicate and late errors ignored, native failure notices,
// the clipboard's two causes, the departed-guest toast, the host-left latch. Invented broker bodies only; no network.
std::string refusal_text;std::uint32_t refusal_count=0;
void set_refusal(const char* text) { refusal_text=text;++refusal_count; } // a refusal dialog moved the agreement layer's counter
std::uint32_t menu_refusal_serial() { return refusal_count; }
bool host_lost_flag=false;
std::uint32_t menu_retry_in() { return menu_broker.retry_in(ticks); }
const char* menu_refusal() { return refusal_text.c_str(); }
// The kicked-guest END mode drives the room layer's real tracker (online_room's host_loss: the host removed this PC, then the
// game session's connection ended) instead of the flag.
bool host_loss_real=false;online_room_core::host_loss_tracker fixture_host_loss;
bool menu_host_lost() { return host_loss_real ? fixture_host_loss.lost() : host_lost_flag; }
const online_menus::broker_api menu_api15{menu_available,menu_view,menu_tick,menu_list,menu_host,menu_resolve,menu_retry,menu_cancel,menu_leave,menu_version,
    menu_take_preset,menu_refresh_due,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,nullptr,menu_retry_in,menu_refusal,menu_host_lost,menu_refusal_serial};

// The table: every id the client can carry, the cause it selects, what the copy tells the player to do, whether Retry shows.
struct error_row { const char* id; error_cause cause; next_action next; bool retry; };
const error_row error_rows[]{
    {"no_such_room",error_cause::code_not_found,next_action::edit_code,false},{"code_not_found",error_cause::code_not_found,next_action::edit_code,false},
    {"room_closed",error_cause::code_expired,next_action::edit_code,false},{"code_expired",error_cause::code_expired,next_action::edit_code,false},
    {"room_full",error_cause::room_full,next_action::browse,false},{"room_not_joinable",error_cause::room_closed,next_action::retry,true},
    {"version_mismatch",error_cause::version_mismatch,next_action::back,false},{"rate_limited",error_cause::rate_limited,next_action::retry,true},
    {"timeout",error_cause::timeout,next_action::retry,true},
    {"broker_unreachable",error_cause::offline,next_action::retry,true},{"worker_unavailable",error_cause::offline,next_action::retry,true},
    {"send_queue_full",error_cause::offline,next_action::retry,true},{"inbox_overflow",error_cause::offline,next_action::retry,true},
    {"tls_pin_mismatch",error_cause::pin_mismatch,next_action::back,false},{"invalid_certificate",error_cause::pin_mismatch,next_action::back,false},
    {"broker_rejected",error_cause::refused,next_action::back,false},{"bad_auth",error_cause::refused,next_action::back,false},
    {"auth_failed",error_cause::refused,next_action::back,false},{"unsupported_version",error_cause::refused,next_action::back,false},
    {"test_mode_on",error_cause::test_mode,next_action::back,false},
    {"online_dependencies_unavailable",error_cause::unsupported,next_action::other_ways,false},{"native_discovery_guard",error_cause::unsupported,next_action::other_ways,false},
    {"online rooms need the receive validation installed ([Online] ReceiveValidation on and the retail network code)",error_cause::unsupported,next_action::other_ways,false},
    {"joining a room needs the LC save format ([Save] Format) installed, which checks the host's campaign before the game loads it",error_cause::unsupported,next_action::other_ways,false},
    {"transport_identity_busy",error_cause::busy,next_action::wait,false},{"invalid_room_name",error_cause::invalid_name,next_action::back,false},
    {"malformed_reply",error_cause::generic,next_action::retry,true},{"internal",error_cause::generic,next_action::retry,true},
    {"",error_cause::generic,next_action::retry,true},{"weird%s\\^id",error_cause::generic,next_action::retry,true}};

void error_table_pure() {
    for(const auto& row:error_rows) {
        const auto cause=classify_error(row.id);
        assert(cause==row.cause && cause_next(cause)==row.next && cause_retryable(cause)==row.retry);
        // Every row has exactly one scene key and one next action; a cause that is retryable names Retry/Try again.
        assert(std::string(error_cause_key[int(cause)]).size() && row_of(cause).cause==cause);
    }
    assert(std::size(cause_table)==std::size(error_cause_key));
    for(unsigned i=0;i<std::size(cause_table);++i) assert(int(cause_table[i].cause)==int(i));
    // The retry set is exactly the transient causes; nothing else may offer it.
    unsigned retryable=0;for(const auto& row:cause_table) retryable+=row.retry;assert(retryable==5);
    // Hostile / long / near-miss ids never select a specific cause.
    for(auto id:{"room_full ","TIMEOUT","Room_Full","timeout\n","no_such_room\0x"}) assert(classify_error(std::string_view(id,std::strlen(id)))==error_cause::generic || std::string_view(id)=="no_such_room");
    assert(classify_error(std::string(4000,'x'))==error_cause::generic && classify_error(std::string(4000,'o'))==error_cause::generic);
    assert(classify_error("online rooms")==error_cause::generic && classify_error("joining a room")==error_cause::generic);
    // Diagnostics: the operation and a known id; anything else is "other" and cannot carry a code, address or secret.
    assert(diagnostic("join","room_full")=="join: room_full" && diagnostic("list","timeout")=="list: timeout" && diagnostic("host","internal")=="host: internal");
    assert(diagnostic("join","")=="join: none");
    for(auto hostile:{"ABCD-EFGH","K7QN2X9M","192.0.2.7:27015","token=Bearer abc123","weird%s%n","\x01\x02\x7f","ABCD EFGH 198.51.100.2"}) {
        const auto text=diagnostic("join",hostile);assert(text=="join: other");
    }
    for(const auto& row:error_rows) if(std::strncmp(row.id,"online rooms need",17)==0 || std::strncmp(row.id,"joining a room needs",20)==0) assert(diagnostic("join",row.id)=="join: install_guard");
    for(const auto& row:error_rows) { if(!*row.id) continue;const auto text=diagnostic("join",row.id);assert(text.size()<=48);for(unsigned char c:text) assert(display_byte(c)==char(c) || c==':'); }
    assert(wait_seconds(0)==0 && wait_seconds(1)==1 && wait_seconds(1000)==1 && wait_seconds(1001)==2 && wait_seconds(60000)==60);
    // Every id the room layer / native flow can raise is either classified, registered, or the explicit generic fallback.
    for(auto id:{"malformed_reply","internal"}) assert(known_id(id) && classify_error(id)==error_cause::generic);
    assert(!known_id("weird") && known_id("tls_pin_mismatch") && known_id("transport_identity_busy"));
}

// Every native_flow_error("...") the flow can raise is registered, and each registered id selects a notice.
void native_failure_table_checks() {
    std::string text;
    for(const char* name:{"online_native_flow.inl","online_menus.cpp"}) { // the flow and the lobby activation both raise native failures
        std::ifstream file(std::filesystem::path(__FILE__).parent_path().parent_path()/"src"/name);assert(file);
        text+=std::string((std::istreambuf_iterator<char>(file)),{});
    }
    std::set<std::string> raised;
    for(std::size_t at=0;(at=text.find("native_flow_error(\"",at))!=std::string::npos;) {
        at+=std::strlen("native_flow_error(\"");raised.insert(text.substr(at,text.find('"',at)-at));
    }
    assert(raised.size()>=25);
    for(const auto& why:raised) assert(find_native_failure(why) && "an unregistered native failure id");
    for(const auto& row:native_failure_table) assert(raised.count(row.why) && "a registered id the flow never raises");
    assert(std::string(native_notice("admission_refused"))=="lc_notice_refused" && std::string(native_notice("admission_call_refused"))=="lc_notice_refused");
    assert(std::string(native_notice("native_timeout"))=="lc_notice_timeout" && std::string(native_notice("discovery_ambiguous_or_wrong"))=="lc_notice_discovery");
    assert(std::string(native_notice("native_search_refused"))=="lc_notice_discovery" && std::string(native_notice("room_generation_or_service_lost"))=="lc_notice_service_lost");
    assert(std::string(native_notice("host_post_refused"))=="lc_notice_admission" && std::string(native_notice("never_heard_of_it"))=="lc_notice_admission");
    assert(refusal_rules_text("DeathRule, GnsTransport")=="DeathRule, GnsTransport" && refusal_rules_text("a%s\n\"b")=="a?s??b");
    assert(refusal_rules_text(std::string(300,'x')).size()==44);
}

void error_models() {
    // Departed-guest toast: named, six seconds, a repeated report is the same departure, a different one replaces it.
    toast t;assert(!t.visible(0));
    t.show("Kitty",1000);t.show("Kitty",2000);t.show("Kitty",3000); // duplicates inside the window did not restart it
    assert(t.visible(1000) && t.name=="Kitty" && t.visible(6999) && !t.visible(7000));
    t.show("Alex",8000);assert(t.visible(8000) && t.name=="Alex");t.show("Sam",9000);assert(t.name=="Sam");
    t.show("",9500);assert(t.name=="Sam");t.clear();assert(!t.visible(9500));
    t.show("Kitty",std::uint32_t(0xfffffff0u));assert(t.visible(std::uint32_t(0xfffffff0u)+5999u) && !t.visible(std::uint32_t(0xfffffff0u)+6000u)); // 32-bit wrap
    // Roster watch: first sight primes; a removed member is named once; the local member is never a toast; growth is silent.
    roster_watch w;char names[4][32]{};unsigned ids[4]{1,2,3,0};
    std::strcpy(names[0],"Logan");std::strcpy(names[1],"Kitty");std::strcpy(names[2],"Alex");
    assert(w.observe(ids,names,3,1,true).empty());                       // primed
    assert(w.observe(ids,names,3,1,true).empty());                       // unchanged
    unsigned after[4]{1,3,0,0};char after_names[4][32]{};std::strcpy(after_names[0],"Logan");std::strcpy(after_names[1],"Alex");
    assert(w.observe(after,after_names,2,1,true)=="Kitty");               // Kitty left
    assert(w.observe(after,after_names,2,1,true).empty());               // reported once
    unsigned grown[4]{1,3,5,0};char grown_names[4][32]{};std::strcpy(grown_names[0],"Logan");std::strcpy(grown_names[1],"Alex");std::strcpy(grown_names[2],"Sam");
    assert(w.observe(grown,grown_names,3,1,true).empty());               // a join is not a departure
    unsigned alone[4]{3,0,0,0};char alone_names[4][32]{};std::strcpy(alone_names[0],"Alex");
    (void)alone;(void)alone_names;
    // The local member leaving is not a toast even when it vanishes from its own roster.
    roster_watch self;assert(self.observe(ids,names,3,2,true).empty());unsigned only[4]{1,3,0,0};
    assert(self.observe(only,after_names,2,2,true).empty());
    // Reused native id with a different name: the old name is reported gone.
    roster_watch reuse;assert(reuse.observe(ids,names,3,1,true).empty());char swapped[4][32]{};std::strcpy(swapped[0],"Logan");std::strcpy(swapped[1],"Mallory");std::strcpy(swapped[2],"Alex");
    assert(reuse.observe(ids,swapped,3,1,true)=="Kitty");
    // The room layer's host-loss tracker: only a connection the game session ran over counts; a fresh connection clears it; a local
    // Leave/Quit or a removal by the host blocks it for the rest of the room.
    { online_room_core::host_loss_tracker h;
      assert(!h.lost());h.ended(false);assert(!h.lost());                                  // never carried the game session
      h.ended(true);assert(h.lost());h.connected();assert(!h.lost());                      // reconnected: clean slate
      h.ended(true);h.connected();h.local_departure();h.ended(true);assert(!h.lost());     // reconnect, then a deliberate quit
      h.reset();h.ended(true);assert(h.lost());h.local_departure();assert(!h.lost());h.ended(true);assert(!h.lost()); // quit after the loss: nothing more
      h.reset();h.removed_by_host();h.ended(true);assert(!h.lost());h.connected();h.ended(true);assert(!h.lost());      // kicked: not a host loss
      h.reset();h.ended(true);assert(h.lost());h.reset();assert(!h.lost()); }
    // Host-left latch: one raise per loss; dismiss clears; a second report while up changes nothing.
    host_left h;assert(!h.active);h.raise();assert(h.active && h.generation==1);h.raise();assert(h.generation==1);h.dismiss();assert(!h.active);h.raise();assert(h.generation==2);
}

void error_clipboard_causes() {
    auto clip=[](std::string_view value) { fake_clipboard c;for(unsigned char ch:value)c.data.push_back(ch);c.data.push_back(0);return c; };
    std::string out;auto ok=clip("abcd-efgh");assert(read_clipboard_result(ok,out)==clip_result::ok && out=="ABCDEFGH");
    auto closed=clip("ABCDEFGH");closed.available=false;assert(read_clipboard_result(closed,out)==clip_result::unavailable && out.empty() && !closed.locked);
    auto locked=clip("ABCDEFGH");locked.lockable=false;assert(read_clipboard_result(locked,out)==clip_result::unavailable && locked.closed==1);
    fake_clipboard empty;assert(read_clipboard_result(empty,out)==clip_result::invalid && empty.closed==1 && !empty.locked); // no text on the clipboard: "no code", not "could not read"
    for(auto text:{"hello world","ABCDEFGHjunk","ABCDEFG"}) { auto bad=clip(text);assert(read_clipboard_result(bad,out)==clip_result::invalid && bad.closed==1 && bad.unlocked==1); }
    // 060q live: a code-sized text with symbols no code has (0, O, 1, I, L, punctuation) says so - the wrong-symbols wording, not "no code".
    for(auto text:{"ABCD0FGH","AB0O1IL!","ABCD-EF!H"," ab0o1il! "}) { auto bad=clip(text);assert(read_clipboard_result(bad,out)==clip_result::bad_symbols && out.empty() && bad.closed==1 && bad.unlocked==1); }
    for(auto text:{"ABCD EFG","ABCDXEFGH","ABCD-EFG\x01"}) { auto bad=clip(text);assert(read_clipboard_result(bad,out)==clip_result::invalid); } // not code-shaped
    auto odd=clip("ABCDEFGH");odd.byte_size=3;assert(read_clipboard_result(odd,out)==clip_result::invalid);
    auto big=clip("ABCDEFGH");big.byte_size=4098;assert(read_clipboard_result(big,out)==clip_result::invalid);
    // Unicode / missing terminator are not codes; words after the first terminator are ignored (060p fix C).
    auto wide=clip("ABCDEFGH");wide.data[0]=0x100;assert(read_clipboard_result(wide,out)==clip_result::invalid);
    auto hidden=clip("ABCDEFGH");hidden.data.insert(hidden.data.end(),{'x',0});assert(read_clipboard_result(hidden,out)==clip_result::ok && out=="ABCDEFGH"); // 060p fix C: the text ends at its first terminator (unspecified spare words)
    auto hidden_bad=clip("ABCD0FGH");hidden_bad.data.insert(hidden_bad.data.end(),{'J',0});assert(read_clipboard_result(hidden_bad,out)==clip_result::bad_symbols);
}

void error_table_tests(const script_functions_rules::func_entry* registration) {
    using tick_t=void(__fastcall*)(void*,void*);
    error_table_pure();native_failure_table_checks();error_models();error_clipboard_causes();
    const auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    auto post=[&](intent value) { int_value[1]=unsigned(value);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr); };
    auto screen=[&](const char* name) { std::strcpy(reinterpret_cast<char*>(child.data()+0xc),name);child[4]=1;set_current(child.data()); };
    auto shown=[&](const std::string& key) { return visibility.count(key) && !visibility[key]; };
    auto gone=[&](const std::string& key) { return visibility.count(key) && visibility[key]; };
    auto settle=[&] { menu_broker.cancel();menu_broker.stopped();ticks+=70000; };
    // Per screen, every cause the screen can show: exactly one scene, the diagnostic redacted and bounded, Retry only where
    // trying again can help and only once the shared limiter allows it.
    const char* screens[]{"lc_code","lc_browse","lc_host","lc_online"};
    for(const char* name:screens) {
        screen(name);menu_broker.cancel();menu_broker.stopped();ticks+=70000;tick(child.data(),nullptr);
        if(std::string(name)=="lc_code") { for(int n=32;n<40;++n) { post(static_cast<intent>(n));tick(child.data(),nullptr); } } // a complete code, so Retry resolves it again
        for(const auto& row:error_rows) {
            if(row.cause==error_cause::invalid_name && std::string(name)!="lc_host") continue;
            if(!*row.id) continue;
            settle();
            // A phase-refused failure and a retry_wait failure both reach the scene selector.
            const bool refused_phase=row.cause==error_cause::test_mode || row.cause==error_cause::unsupported || row.cause==error_cause::invalid_name || row.cause==error_cause::busy;
            if(refused_phase) menu_broker.refuse(row.id);
            else {
                const auto op=std::string(name)=="lc_browse" ? bm::operation::list : std::string(name)=="lc_host" ? bm::operation::host : bm::operation::resolve;
                assert(menu_begin(op));
                menu_auth=true;menu_error=row.id;menu_complete=true;tick(child.data(),nullptr);
            }
            tick(child.data(),nullptr);
            const bool failed_state=menu_view().state==bm::phase::retry_wait || menu_view().state==bm::phase::refused;
            assert(failed_state);
            unsigned visible=0;
            for(unsigned i=0;i<std::size(error_cause_key);++i) {
                const auto node=std::string("lc_broker_err_")+error_cause_key[i];
                if(shown(node)) { ++visible;assert(i==unsigned(row.cause)); } else assert(gone(node));
            }
            assert(visible==1);
            // Retry: shown for the transient causes (a refused phase shows it only for the busy link), never otherwise;
            // enabled only when the limiter allows a new request.
            const bool retry_visible=row.retry && menu_view().state==bm::phase::retry_wait; // never in the refused phase: online_room::retry() refuses it
            assert(retry_visible ? shown("retry") : gone("retry"));
            if(row.cause==error_cause::busy) assert(!row.retry && !retry_visible); // F1: no dead Retry in the refused phase
            if(retry_visible) assert(enabled_items["retry"]==(menu_retry_in()==0));
            else assert(!enabled_items["retry"]);
            const auto& diag=texts["broker_error"];
            assert(diag.size()<=64);for(unsigned char c:diag) assert(display_byte(c)==char(c) || c==':');
            assert(diag.find("ABCD")==std::string::npos && diag.find('%')==std::string::npos);
            // The countdown belongs to Retry: a failure Retry cannot help never counts down.
            if(!retry_visible) assert(diag.rfind("Retry in",0)!=0);
            if(row.cause==error_cause::busy) assert(menu_view().state==bm::phase::refused && gone("retry") && !enabled_items["retry"]);
            assert(gone("lc_service_ok") && texts["service"]=="Unavailable");
            // Press the REAL button: it starts a request exactly when it is visible and the limiter allows it.
            if(retry_visible) {
                assert(!enabled_items["retry"] || menu_retry_in()==0);
                post(intent::retry);tick(child.data(),nullptr);
                if(menu_retry_in()==0 && enabled_items["retry"]) assert(menu_broker.pending());
                ticks+=menu_retry_in()+1;tick(child.data(),nullptr);
                if(!menu_broker.pending()) { assert(enabled_items["retry"]);post(intent::retry);tick(child.data(),nullptr);assert(menu_broker.pending() && gone("retry")); }
            }
        }
    }
    // The shared limiter counts down on the diagnostic field; Retry waits for it; Refresh cannot bypass it.
    screen("lc_browse");settle();menu_rows_json="-";
    assert(menu_begin(bm::operation::list));menu_auth=true;menu_complete=true;tick(child.data(),nullptr);tick(child.data(),nullptr); // a good list clears the backoff
    assert(menu_view().state==bm::phase::idle && menu_view().list_known);
    settle();settle();
    assert(menu_begin(bm::operation::list));menu_auth=true;menu_error="timeout";menu_complete=true;tick(child.data(),nullptr);tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::retry_wait && shown("lc_broker_err_timeout") && texts["broker_error"]=="Retry in 2 s" && shown("retry") && !enabled_items["retry"]);
    assert(!enabled_items["prompt_refresh"]);
    const auto lists=menu_lists;post(intent::refresh);post(intent::retry);tick(child.data(),nullptr);assert(menu_lists==lists && menu_view().state==bm::phase::retry_wait);
    ticks+=1000;tick(child.data(),nullptr);assert(texts["broker_error"]=="Retry in 1 s" && !enabled_items["retry"]);
    ticks+=1000;tick(child.data(),nullptr);assert(texts["broker_error"]=="list: timeout" && enabled_items["retry"]);
    settle();
    assert(menu_begin(bm::operation::list));menu_auth=true;menu_error="rate_limited";menu_complete=true;tick(child.data(),nullptr);tick(child.data(),nullptr);
    assert(texts["broker_error"]=="Retry in 10 s" && shown("lc_broker_err_rate_limited") && !enabled_items["retry"]); // the service limit is at least 10 s
    // Retry works without a restart once the wait is over: the same screen, a new request, the failure gone.
    ticks+=10000;tick(child.data(),nullptr);assert(enabled_items["retry"]);
    post(intent::retry);tick(child.data(),nullptr);
    assert(menu_broker.pending() && gone("retry") && gone("lc_broker_err_rate_limited") && texts["broker_error"].empty());
    // Busy: Retry again while pending does nothing; Back cancels and leaves a clean client.
    const auto pending_lists=menu_lists;const auto pending_generation=menu_view().generation.request;post(intent::retry);post(intent::refresh);tick(child.data(),nullptr);assert(menu_lists==pending_lists && menu_broker.pending() && menu_view().generation.request==pending_generation);
    menu_auth=true;menu_error="";menu_complete=false;
    // Duplicate and late failures are ignored: the first failure of a generation wins and the backoff does not grow.
    const auto generation=menu_view().generation;menu_error="room_full";menu_complete=true;menu_auth=true;tick(child.data(),nullptr);tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::retry_wait && menu_view().error=="room_full" && shown("lc_broker_err_room_full"));
    const auto wait_before=menu_retry_in();
    menu_broker.fail(generation,"timeout",ticks);menu_broker.fail(generation,"timeout",ticks);menu_broker.disconnected("timeout",ticks);
    tick(child.data(),nullptr);assert(menu_view().error=="room_full" && menu_retry_in()==wait_before && shown("lc_broker_err_room_full") && gone("lc_broker_err_timeout"));
    settle();
    // A late error from a canceled request reaches nothing.
    assert(menu_begin(bm::operation::resolve));const auto old_ticket=menu_view().generation;menu_cancel();tick(child.data(),nullptr);
    menu_broker.fail(old_ticket,"version_mismatch",ticks);menu_broker.detail(old_ticket,"LC99");tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::idle && menu_view().error.empty() && texts["broker_error"].empty());
    for(unsigned i=0;i<std::size(error_cause_key);++i) assert(gone(std::string("lc_broker_err_")+error_cause_key[i]));
    // Another build names both tokens (the host's from the reply, ours from the room layer); nothing else is echoed.
    screen("lc_code");tick(child.data(),nullptr);settle();
    assert(menu_begin(bm::operation::resolve));menu_auth=true;menu_error="version_mismatch";menu_error_detail="LC36";menu_complete=true;tick(child.data(),nullptr);tick(child.data(),nullptr);
    assert(shown("lc_broker_err_version_mismatch") && texts["broker_error"]=="LC36 vs your FIXTURE" && gone("retry"));
    settle();menu_error_detail="LC36%s\"~^" ;tick(child.data(),nullptr);
    assert(menu_begin(bm::operation::resolve));menu_auth=true;menu_error="version_mismatch";menu_complete=true;tick(child.data(),nullptr);tick(child.data(),nullptr);
    for(unsigned char c:texts["broker_error"]) assert(display_byte(c)==char(c));
    assert(texts["broker_error"].find('%')==std::string::npos && texts["broker_error"].size()<=8+9+8);
    menu_error_detail.clear();
    // Back from a failure returns cleanly; the client is idle and the next request starts (no stuck process state).
    post(intent::back);tick(child.data(),nullptr);
    menu_broker.cancel();menu_broker.stopped();ticks+=70000;
    // Healthy P2P is not ended by the service: a committed room whose service failed keeps the room and shows the offline line.
    screen("lc_code");tick(child.data(),nullptr);
    assert(menu_resolve("AAAAAAAB") && menu_broker.pending());menu_auth=true;menu_complete=true;tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::room);
    menu_broker.disconnected("broker_unreachable",ticks);tick(child.data(),nullptr);
    assert(menu_view().state==bm::phase::room && shown("lc_broker_offline_room") && shown("lc_broker_room") && gone("retry"));
    for(unsigned i=0;i<std::size(error_cause_key);++i) assert(gone(std::string("lc_broker_err_")+error_cause_key[i]));
    // Late second disconnect report while the room is open changes nothing.
    menu_broker.disconnected("timeout",ticks);tick(child.data(),nullptr);assert(menu_view().state==bm::phase::room && shown("lc_broker_offline_room"));
    menu_broker.cancel(true);menu_broker.stopped();ticks+=70000;
    // Assets that lack a new cause's scene (broker/2-only LC) fall back to the nearest true one; assets that author it never show two.
    screen("lc_code");tick(child.data(),nullptr);
    for(auto [id,fallback]:{std::pair<const char*,const char*>{"tls_pin_mismatch","refused"},{"room_closed","code_not_found"},{"room_not_joinable","generic"},
        {"test_mode_on","generic"},{"online_dependencies_unavailable","generic"}}) {
        const auto cause=classify_error(id);const auto own=std::string("lc_broker_err_")+error_cause_key[int(cause)];
        absent_items.insert(own);settle();assert(menu_begin(bm::operation::resolve));menu_auth=true;menu_error=id;menu_complete=true;tick(child.data(),nullptr);tick(child.data(),nullptr);
        assert(shown(std::string("lc_broker_err_")+fallback));
        absent_items.erase(own);
    }
    absent_items.clear();
    // No service at all: no scene, no diagnostic, no Retry.
    menu_up=false;screen("lc_browse");tick(child.data(),nullptr);
    for(unsigned i=0;i<std::size(error_cause_key);++i) assert(gone(std::string("lc_broker_err_")+error_cause_key[i]));
    assert(gone("retry") && texts["broker_error"].empty());
    menu_up=true;
    std::cout<<"round15 error table PASS "<<std::size(error_rows)<<" id rows x 4 screens, one scene + redacted diagnostic + Retry gating, limiter countdown, retry without restart, duplicate/late errors, build pair, native failure registry, clipboard causes, toast/roster/host-left models\n";
}
