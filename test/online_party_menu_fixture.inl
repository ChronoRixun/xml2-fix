// Round 8: the in-session Party screen (lc_party) through the PRODUCTION installed hooks: base-menu tick, mailbox, the
// authored-usecmd queue boundary, the script handler, Back. Only presentation/native services are stubbed (the console
// queue records the command line, the native Back records the call, the transport's agreed-departure seam records the
// index). Nothing here reads a peer's claim: the fixture hook supplies what online_room::party would.
struct party_fixture_member { unsigned index; const char* name; bool self, host; int phase; int ping; int build; };
std::vector<party_fixture_member> party_rows;
bool party_hosting=true,party_session=true;
bool party_rate_facts=false;
std::vector<unsigned> party_removed;
bool party_remove_ok=true;
int party_native_backs=0;
bool party_fields_stub(online_menus::lobby_fixture& out) {
    out={};std::strcpy(out.code,"ABCD-EFGH");out.hosting=party_hosting;out.session=party_session;
    for(const auto& row:party_rows) {
        auto& m=out.members[out.count++];m={};
        m.native_index=row.index;std::strncpy(m.name,row.name,31);m.self=row.self;m.host=row.host;m.phase=row.phase;
        m.ping_ms=row.ping;m.build=row.build;m.route=1;
        if(party_rate_facts) {
            m.local_fps=row.index==1 ? 30 : 60;m.session_fps=60;
            m.rate_capacity=row.index==1 ? linked_frame_rate_core::capacity::slow : linked_frame_rate_core::capacity::holding;
        }
    }
    return true;
}
bool party_remove_stub(unsigned index) { party_removed.push_back(index);return party_remove_ok; }
online_menus::broker_api party_api{};
bool __fastcall party_queue_stub(void*,void*,const char* line) { calls.push_back(std::string("queue:")+line);return true; }
void __fastcall party_back_stub(void*,void*) { ++party_native_backs; }
std::vector<std::string> party_focused;
void __fastcall party_focus_stub(void*,void*,const char* name,bool) { party_focused.push_back(name); }
void party_default_rows(bool host) {
    party_hosting=host;party_session=true;
    party_rows={{0,"Logan",host,true,7,-1,3},{1,"Kitty",!host,false,7,41,3},{2,"Alex",false,false,4,118,2}};
    if(host) { party_rows[1].self=false; }
}
void party_tests(const script_functions_rules::func_entry* registration) {
    const bool host=mode=="r8-party-host" || mode=="r8-party-intents";
    using tick_t=void(__fastcall*)(void*,void*);
    const auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    const auto deactivate=*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69e438);
    const auto on_back=*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69e440);
    auto post=[&](int value) { int_value[1]=unsigned(value);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr); };
    auto queue_command=[&](const char* line) { const auto target=reinterpret_cast<void*>(0x5bc3d5+*reinterpret_cast<unsigned*>(0x5bc3d1));invoke_usecmd(target,child.data(),line); };
    redirect(0x55c410,party_queue_stub);redirect(0x5ad7c0,party_back_stub);redirect(0x5adf40,party_focus_stub);
    std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_party");
    set_current(child.data());
    auto queued=[&](const char* line) { return std::count(calls.begin(),calls.end(),std::string("queue:")+line); };
    
    // ---- mailbox policy (pure) ----
    {
        mailbox box;
        for(int value:{63,64,65,66,67,69,71,72,73,74,6,8}) { assert(box.post(value,1,screen::party));assert(box.take(1,screen::party)==value); }
        for(int value:{68,70,75,2,3,5,9,16,17,18,31,32,62,0,1,7}) assert(!box.post(value,1,screen::party));
        for(int value:{63,64,66,67,69,71,74}) for(auto s:{screen::hub,screen::host,screen::code,screen::browse,screen::lobby_host,screen::lobby_join}) assert(!box.post(value,1,s));
        assert(box.post(67,1,screen::party));assert(box.post(6,1,screen::party));
        assert(box.take(1,screen::party)==67 && box.take(1,screen::party)==6 && box.take(1,screen::party)==0);
        assert(box.post(66,1,screen::party));assert(!box.post(6,2,screen::party));
        assert(box.take(2,screen::party)==0 && box.take(1,screen::party)==0); // stale owner discarded
        assert(identify("lc_party")==screen::party && custom(screen::party) && !lobby(screen::party));
    }
    
    // ---- model rules (pure): stale/self/host/pending kicks, confirmations, departure once ----
    {
        party_roster r;r.hosting=true;r.session=true;
        r.rows={{0,"Logan",true,true,false},{1,"Kitty",false,false,false},{2,"Alex",false,false,false},{-1,"Sam",false,false,true}};
        assert(!r.kickable(0) && r.kickable(1) && r.kickable(2) && !r.kickable(3) && !r.kickable(4));
        party_model m;
        assert(m.apply(71,r).act==party_act::refused && m.apply(74,r).act==party_act::refused && m.confirm==party_confirm::none);
        assert(m.apply(69,r).act==party_act::refused); // Yes without a confirmation
        assert(m.apply(72,r).act==party_act::opened_kick && m.kick_index==1 && m.kick_name=="Kitty");
        assert(m.apply(73,r).act==party_act::refused && m.apply(66,r).act==party_act::refused && m.apply(63,r).act==party_act::refused);
        // The index is reused by somebody else before Yes: stale, nothing removed.
        auto swapped=r;swapped.rows[1].name="Remy";
        assert(m.refresh(swapped) && m.confirm==party_confirm::none && m.apply(69,swapped).act==party_act::refused);
        // Yes whose target vanished between the last refresh and the answer: stale.
        assert(m.apply(72,r).act==party_act::opened_kick);
        assert(m.apply(69,swapped).act==party_act::stale && m.confirm==party_confirm::none && !(m.kicked&2));
        // A real Kick runs once; the index stays marked while listed.
        assert(m.apply(72,r).act==party_act::opened_kick);
        const auto go=m.apply(69,r);assert(go.act==party_act::kick_run && go.kick_index==1 && (m.kicked&2));
        assert(m.apply(72,r).act==party_act::refused && m.apply(69,r).act==party_act::refused);
        auto gone=r;gone.rows.erase(gone.rows.begin()+1);
        assert(!m.refresh(gone) && !(m.kicked&2));
        // A guest (not hosting), no session, or a lost session never opens a Kick.
        for(int variant=0;variant<2;++variant) { auto g=r;if(variant==0) g.hosting=false;else g.session=false;party_model fresh;assert(fresh.apply(72,g).act==party_act::refused); }
        // Leave: confirm, Yes once, then nothing else runs.
        party_model leave;
        assert(leave.apply(67,r).act==party_act::refused && leave.apply(66,r).act==party_act::opened_leave);
        assert(leave.apply(8,r).act==party_act::dismissed && leave.apply(67,r).act==party_act::refused);
        assert(leave.apply(66,r).act==party_act::opened_leave && leave.apply(6,r).act==party_act::dismissed);
        assert(leave.apply(66,r).act==party_act::opened_leave && leave.apply(67,r).act==party_act::leave_run && leave.departed);
        for(int value:{8,64,66,67,69,71,72}) assert(leave.apply(value,r).act==party_act::none);
        assert(leave.apply(6,r).act==party_act::native_back && leave.apply(63,r).act==party_act::native_back); // the queued quit may never run
        // The shared Yes (67) completes a Kick; 69 still does; a Leave outside a session is the native quit at once.
        { party_model k;assert(k.apply(72,r).act==party_act::opened_kick);const auto y=k.apply(67,r);assert(y.act==party_act::kick_run && y.kick_index==1 && !k.departed); }
        { party_model k;auto none=r;none.session=false;assert(k.apply(66,none).act==party_act::leave_run && k.departed && k.confirm==party_confirm::none); }
        { party_model k;assert(k.apply(66,r).act==party_act::opened_leave && k.apply(69,r).act==party_act::refused && k.confirm==party_confirm::leave); }
        // Propose load (65) and reserved values never act; Back and Resume are the native Back; Invite copies.
        party_model idle;
        for(int value:{65,68,70,75}) assert(idle.apply(value,r).act==party_act::refused);
        assert(idle.apply(6,r).act==party_act::native_back && idle.apply(63,r).act==party_act::native_back && idle.apply(64,r).act==party_act::copy);
    }
    
    // ---- the screen through the production hooks (visibility[] true = hidden) ----
    auto shown=[&](const std::string& item) { return visibility.count(item) && !visibility[item]; };
    auto hidden=[&](const std::string& item) { return visibility.count(item) && visibility[item]; };
    auto on=[&](const std::string& item) { return enabled_items.count(item) && enabled_items[item]; };
    auto off=[&](const std::string& item) { return enabled_items.count(item) && !enabled_items[item]; };
    party_default_rows(host);
    tick(child.data(),nullptr);
    assert(texts["lc_room_code"]=="ABCD-EFGH");
    put(child.data(),0x324,unsigned(reinterpret_cast<std::uintptr_t>(item_stub(child.data(),nullptr,"lc_party_invite"))));
    tick(child.data(),nullptr);assert(shown("lc_party_invite_hint_on") && hidden("lc_party_invite_hint"));
    put(child.data(),0x324,0);tick(child.data(),nullptr);assert(hidden("lc_party_invite_hint_on") && shown("lc_party_invite_hint"));
    assert(shown("lc_p1_card") && shown("lc_p2_card") && shown("lc_p3_card") && hidden("lc_p4_card") && shown("lc_p4_empty") && hidden("lc_p3_empty"));
    assert(texts["lc_p1_name"]=="Logan" && texts["lc_p2_name"]=="Kitty" && texts["lc_p3_name"]=="Alex" && texts["lc_p1_slot"]=="P1" && texts["lc_p3_slot"]=="P3");
    assert(shown("lc_p1_host") && hidden("lc_p2_host") && texts["lc_p1_phase"]=="Playing" && texts["lc_p3_phase"]=="Waiting for next area");
    assert(shown(host ? "lc_p1_you" : "lc_p2_you") && hidden(host ? "lc_p2_you" : "lc_p1_you"));
    assert(shown("lc_p2_hero_fallback") && hidden("lc_p2_hero_next") && shown("lc_p3_hero_next") && hidden("lc_p3_hero_fallback"));
    assert(shown("lc_p1_tone_good") && hidden("lc_p1_tone_warn") && shown("lc_p3_tone_warn") && hidden("lc_p3_tone_good"));
    assert(shown("lc_party_waiting") && hidden("lc_party_receiving") && texts["lc_notice_name"]=="Alex" && shown("lc_notice_name"));
    assert(host ? (shown("lc_party_propose_soon") && hidden("lc_party_propose_host_only")) : (shown("lc_party_propose_host_only") && hidden("lc_party_propose_soon")));
    assert(off("lc_party_propose") && on("lc_party_invite") && on("lc_rail_quit") && on("lc_rail_resume"));
    assert(hidden("lc_party_confirm_leave_host") && hidden("lc_party_confirm_leave_guest") && hidden("lc_party_confirm_kick") && hidden("lc_party_leave_yes") && hidden("lc_party_cancel"));
    party_rate_facts=true;tick(child.data(),nullptr);
    assert(texts["lc_p2_fps"]=="30 local / 60 online / slow" && shown("lc_p2_fps") && hidden("lc_p2_hero_fallback") && hidden("lc_p2_same_build"));
    assert(shown("lc_party_rate_warning") && texts["lc_party_rate_warning"]=="Kitty: Cannot hold 60 fps" && hidden("lc_party_recovery_1"));
    party_rate_facts=false;tick(child.data(),nullptr);
    assert(hidden("lc_p2_fps") && hidden("lc_party_rate_warning") && shown("lc_party_recovery_1") && shown("lc_p2_hero_fallback"));
    std::cout<<"PASS linked rate Party: local/session fields, named measured-slow warning, stale facts cleared\n";
    if(host) {
        // Kick buttons: remote admitted cards only; never the host/own card, never a missing card.
        assert(hidden("lc_p1_kick") && shown("lc_p2_kick") && shown("lc_p3_kick") && hidden("lc_p4_kick") && off("lc_p1_kick") && on("lc_p2_kick") && on("lc_p3_kick"));
    } else {
        for(int p=1;p<=4;++p) assert(hidden("lc_p"+std::to_string(p)+"_kick") && off("lc_p"+std::to_string(p)+"_kick"));
    }
    if(mode=="r8-party-intents") {
        // Reserved 68/70 and unknown 76 stay native text. Host-only DROP-IN75
        // is recognized but discarded by Party ownership, never replayed later.
        calls.clear();
        queue_command("runscript lcOnlineIntent(68)");queue_command("runscript lcOnlineIntent(70)");queue_command("runscript lcOnlineIntent(75)");queue_command("runscript lcOnlineIntent(76)");
        assert(queued("runscript lcOnlineIntent(68)")==1 && queued("runscript lcOnlineIntent(70)")==1 && queued("runscript lcOnlineIntent(75)")==0 && queued("runscript lcOnlineIntent(76)")==1);
        tick(child.data(),nullptr);assert(hidden("lc_party_confirm_kick") && hidden("lc_party_confirm_leave_host") && party_removed.empty());
        calls.clear();
        queue_command("runscript lcOnlineIntent(72)");tick(child.data(),nullptr);
        assert(queued("runscript lcOnlineIntent(72)")==0 && shown("lc_party_confirm_kick") && texts["lc_kick_name"]=="Kitty" && shown("lc_party_leave_yes") && shown("lc_party_cancel") && shown("lc_p2_selected") && party_focused.back()=="lc_party_cancel");
        assert(off("lc_rail_quit") && off("lc_party_leave") && off("lc_p3_kick") && hidden("lc_p3_kick") && party_removed.empty());
        // Kick Yes: the transport seam is asked exactly once for index 1; a replay and a re-kick do nothing.
        post(67);tick(child.data(),nullptr);assert(party_removed==std::vector<unsigned>{1} && hidden("lc_party_confirm_kick"));
        // Review B: the closed confirmation leaves focus on a usable item (the Yes/Cancel buttons are hidden now).
        assert(party_focused.back()=="lc_party_invite" && hidden("lc_party_leave_yes") && hidden("lc_party_cancel"));
        post(69);tick(child.data(),nullptr);post(72);tick(child.data(),nullptr);post(69);tick(child.data(),nullptr);
        assert(party_removed==std::vector<unsigned>{1} && hidden("lc_party_confirm_kick"));
        // A stale Yes (no confirmation open) and a Yes whose target was replaced also end on a usable focus.
        party_focused.clear();post(67);tick(child.data(),nullptr);assert(party_removed==std::vector<unsigned>{1} && party_focused.empty());
        post(73);tick(child.data(),nullptr);party_rows[2].name="Zed";post(67);tick(child.data(),nullptr);
        assert(party_removed==std::vector<unsigned>{1} && hidden("lc_party_confirm_kick") && party_focused.back()=="lc_p3_kick"); // origin is still a usable Kick button
        party_rows[2].name="Alex";tick(child.data(),nullptr);
        // The roster stops listing it: the mark clears; a new member reusing index 1 can be kicked again.
        party_rows.erase(party_rows.begin()+1);tick(child.data(),nullptr);
        party_rows.insert(party_rows.begin()+1,{1,"Remy",false,false,2,30,2});tick(child.data(),nullptr);
        post(72);tick(child.data(),nullptr);assert(texts["lc_kick_name"]=="Remy" && shown("lc_party_confirm_kick"));
        // Refused by the transport (no link / not hosting): the mark is dropped so the host may retry.
        party_remove_ok=false;post(69);tick(child.data(),nullptr);assert(party_removed.size()==2 && hidden("lc_party_confirm_kick"));
        party_remove_ok=true;post(72);tick(child.data(),nullptr);post(69);tick(child.data(),nullptr);assert(party_removed==std::vector<unsigned>({1,1,1}));
        party_rows.erase(party_rows.begin()+1);tick(child.data(),nullptr);
        party_rows.insert(party_rows.begin()+1,{1,"Kitty",false,false,7,41,3});tick(child.data(),nullptr);
        // A confirmation whose target is replaced before Yes is dropped; Yes then removes nobody.
        party_removed.clear();
        post(73);tick(child.data(),nullptr);assert(texts["lc_kick_name"]=="Alex" && shown("lc_p3_selected"));
        party_rows[2].name="Zed";tick(child.data(),nullptr);assert(hidden("lc_party_confirm_kick") && hidden("lc_p3_selected"));
        post(69);tick(child.data(),nullptr);assert(party_removed.empty());
        // Host card, own card and an empty card never open a confirmation.
        for(int value:{71,74}) { post(value);tick(child.data(),nullptr);assert(hidden("lc_party_confirm_kick")); }
        // A guest (this PC no longer hosts) cannot open or confirm one.
        post(72);tick(child.data(),nullptr);assert(shown("lc_party_confirm_kick"));
        party_hosting=false;tick(child.data(),nullptr);assert(hidden("lc_party_confirm_kick"));
        post(69);tick(child.data(),nullptr);post(72);tick(child.data(),nullptr);post(69);tick(child.data(),nullptr);assert(party_removed.empty());
        party_hosting=true;
        // Propose load and reserved intents do nothing at all (no native queue, no state).
        calls.clear();
        for(int value:{65,68,70}) { post(value);tick(child.data(),nullptr); }
        assert(calls.empty() && hidden("lc_party_confirm_kick") && hidden("lc_party_confirm_leave_host") && party_native_backs==0);
        std::cout<<"r8 party intents PASS mailbox scope; kick once/stale/self/host/guest/empty; propose and reserved refused; authored queue boundary\n";return;
    }
    // ---- Invite: one of the two feedback scenes ----
    post(64);tick(child.data(),nullptr);
std::cout<<"\n";    assert(shown("lc_party_copied")!=shown("lc_party_copy_failed"));
    // ---- Leave confirmation: role copy, everything else disabled while it is open, one native quit ----
    calls.clear();
    post(66);tick(child.data(),nullptr);
    assert(!party_focused.empty() && party_focused.back()=="lc_party_cancel");
    assert(shown(host ? "lc_party_confirm_leave_host" : "lc_party_confirm_leave_guest") && hidden(host ? "lc_party_confirm_leave_guest" : "lc_party_confirm_leave_host"));
    assert(shown("lc_party_leave_yes") && shown("lc_party_cancel") && off("lc_rail_resume") && off("lc_party_invite") && off("lc_party_leave") && off("lc_p2_kick"));
    post(67);tick(child.data(),nullptr);
    assert(queued("mainmenuexit 1")==1 && off("lc_rail_resume"));
    post(67);tick(child.data(),nullptr);post(66);tick(child.data(),nullptr);post(67);tick(child.data(),nullptr);
    assert(queued("mainmenuexit 1")==1 && party_native_backs==0);
    // ---- return / menu generation: a new menu object starts clean and a stale mailbox entry never reaches it ----
    deactivate(child.data(),nullptr);
    std::strcpy(reinterpret_cast<char*>(hub.data()+0xc),"lc_party");set_current(child.data());
    post(66);set_current(hub.data());tick(hub.data(),nullptr);
    assert(hidden("lc_party_confirm_leave_host") && hidden("lc_party_confirm_leave_guest") && queued("mainmenuexit 1")==1);
    post(67);tick(hub.data(),nullptr);assert(queued("mainmenuexit 1")==1); // Yes without its own confirmation
    post(66);tick(hub.data(),nullptr);assert(shown(host ? "lc_party_confirm_leave_host" : "lc_party_confirm_leave_guest"));
    post(6);tick(hub.data(),nullptr);
    assert(party_focused.back()=="lc_party_leave"); // the confirmation closes onto the button that opened it
    assert(hidden("lc_party_confirm_leave_host") && hidden("lc_party_confirm_leave_guest") && party_native_backs==0); // Back closes the confirmation first
    // Back and Resume are the native Back, once each, and nothing else.
    post(6);tick(hub.data(),nullptr);assert(party_native_backs==1);
    post(63);tick(hub.data(),nullptr);assert(party_native_backs==2 && queued("mainmenuexit 1")==1);
    // The base Back slot posts only for a live party menu; otherwise the native Back runs unchanged (once) and nothing is queued.
    set_current(nullptr);on_back(hub.data(),nullptr);set_current(hub.data());tick(hub.data(),nullptr);assert(party_native_backs==3);
    // Review D/C/B: the Leave latch never freezes the screen; outside a session Leave is the native quit at once; focus is
    // moved only after the bindings (a hidden button cannot hold it).
    {
        calls.clear();party_native_backs=0;party_focused.clear();
        deactivate(hub.data(),nullptr);
        std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_party");set_current(child.data());party_default_rows(host);
        tick(child.data(),nullptr);
        post(66);tick(child.data(),nullptr);post(67);tick(child.data(),nullptr);
        assert(queued("mainmenuexit 1")==1);
        post(64);tick(child.data(),nullptr);post(66);tick(child.data(),nullptr);assert(queued("mainmenuexit 1")==1 && hidden("lc_party_confirm_leave_host") && hidden("lc_party_confirm_leave_guest")); // latched
        post(6);tick(child.data(),nullptr);assert(party_native_backs==1); // Back still reaches the native Back after the latch
        deactivate(child.data(),nullptr);set_current(child.data());tick(child.data(),nullptr);
        post(66);tick(child.data(),nullptr);post(67);tick(child.data(),nullptr);assert(queued("mainmenuexit 1")==2);
        ticks+=4001;tick(child.data(),nullptr);post(66);tick(child.data(),nullptr);
        assert(shown(host ? "lc_party_confirm_leave_host" : "lc_party_confirm_leave_guest")); // the timeout cleared the latch
        post(8);tick(child.data(),nullptr);assert(party_focused.back()=="lc_party_leave" && hidden("lc_party_cancel"));
        party_session=false;tick(child.data(),nullptr);
        post(66);tick(child.data(),nullptr);
        assert(queued("mainmenuexit 1")==2 && queued("mainmenuexit")==1 && hidden("lc_party_confirm_leave_host") && hidden("lc_party_confirm_leave_guest")); // no Party consent: preserve the stock question
        party_session=true;
    }
    std::cout<<"r8 party "<<(host ? "HOST" : "GUEST")<<" PASS cards/roles/tones/kick buttons; confirmation disables the rest; one native quit; Back/Resume native; stale menu generation\n";
}
