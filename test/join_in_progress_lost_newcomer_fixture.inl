// p060fix/AR: a newcomer lost between its admission and the shared start, through the PRODUCTION join-in-progress
// paths (included by join_in_progress_hold.inl; same synthetic natives: session s with local/remote/existing records,
// manager m, the stubbed native campaign start). The transport half (the host's reliable decision, a guest carrying it
// out) is the cohort model of join_in_progress_test.cpp; here: the host's admission accounting, its hold staying on
// for the restart that still comes, the owed restart when every newcomer was lost before its image, the restart entry
// running before the hold ends and before the native campaign start, and the session's end clearing it all.
unsigned lost_owed_starts=0;int lost_owed_index=-1;bool lost_owed_held=false;
bool lost_owed_start(void*,const join_in_progress_core::seat& seat) { ++lost_owed_starts;lost_owed_index=seat.index;lost_owed_held=join_in_progress::world_hold!=0;return true; }
bool lost_entry_held=false;unsigned lost_entry_begins=0;
void run_lost_newcomer_fixture() {
    using namespace join_in_progress_rules;
    using join_in_progress_core::join_hold;
    using join_in_progress_core::stage;
    auto& machine=join_in_progress::machine;
    const int was_state=join_in_progress::state.load();join_in_progress::state=1; // the exports answer only for an installed module
    auto* draws=reinterpret_cast<unsigned*>(rng_count);
    join_in_progress::owed_start=&lost_owed_start;
    // host (index 0) + newcomer `remote` (1) + old peer `existing` (2)
    put(local,record_flags,1u);put(local,0x18,0);put(remote,0x18,1);put(existing,0x18,2);put(remote,record_flags,0u);put(existing,record_flags,0u);
    auto three=[&]() { put(s,0x26c,nodes[0]);put(s,0x278,3u);put(nodes[0],node_record,local);put(nodes[0],node_next,nodes[1]);put(nodes[1],node_record,remote);put(nodes[1],node_next,nodes[2]);put(nodes[2],node_record,existing);put(nodes[2],node_next,static_cast<void*>(nullptr)); };
    auto lose_remote=[&]() { put(nodes[0],node_next,nodes[2]);put(s,0x278,2u); }; // the native removal took it off the list
    auto admit=[&](int newcomers) {
        join_in_progress::hold_end(join_hold::event::session);join_in_progress::epoch={};join_in_progress::admission.clear();
        machine.reset();machine.started=true;join_in_progress::hold_playing=true;
        assert(machine.defer_add(remote,1,false,hold_ms));
        if(newcomers==2) assert(machine.defer_add(existing,2,false,hold_ms));
        hold_loading=true;join_in_progress::area_barrier_bridge(s,nullptr,5.f);hold_loading=false;
        hold_ms+=50;join_in_progress::hold_tick();
        assert(join_in_progress::hold.active() && !join_in_progress::hold.armed && join_in_progress::world_hold==1);
    };
    three();
    // ---- AR-1. Lost before its image, one newcomer, an old peer present: admission accounting + the owed restart ----
    admit(1);
    assert(join_in_progress::admission.announced==0x02 && gns_transport::fixture_announced_mask==0x02);
    assert(join_in_progress::admission_newcomer(1) && !join_in_progress::admission_newcomer(2) && !join_in_progress::admission_newcomer(0));
    assert(!join_in_progress::restart_begun() && !join_in_progress::restart_removal_held() && !join_in_progress::restart_started());
    assert(join_in_progress::admission_remaining(1)==0);
    // the transport's decision (host_newcomer_decision) records it, then its native removal takes the record off the list
    join_in_progress::newcomer_lost_decided(1);lose_remote();
    auto at=observed_jip_logs.size();
    put(m,manager_wait,static_cast<unsigned char>(0));put(m,manager_sender,0u);
    join_in_progress::guarded_advance(false,nullptr); // production advance: the seat is free
    HOLD_EXPECT(at,"join in progress: player 1 left while adding; the seat is free again (lost before the restart: every peer removed it by the host's decision; the cohort restart goes on without it)");
    assert(!machine.any());
    assert(hold_ticks(10,100)==0 && join_in_progress::hold.active() && join_in_progress::world_hold==1); // NOT "every newcomer left": the restart still comes
    assert(!hold_logged(at,"every newcomer of this hold left"));
    *draws=777;at=observed_jip_logs.size();
    join_in_progress::owed_restart_tick();
    if(!(lost_owed_starts==1 && lost_owed_index==1 && lost_owed_held)) { std::cerr<<"owed: starts "<<lost_owed_starts<<" index "<<lost_owed_index<<" held "<<lost_owed_held<<" lost "<<unsigned(join_in_progress::admission.lost)<<" active "<<join_in_progress::epoch.active<<" late "<<join_in_progress::late_restart<<"\n";hold_dump(at);assert(false); } // the image of the still-held world
    HOLD_EXPECT(at,"join in progress: every newcomer of this admission was lost before its image (mask 0x2) - the host restarts the cohort anyway from one live image");
    // the restart entry (production deferred start): the transport's entry first, while the hold is still on, before the native start
    const auto entries=gns_transport::fixture_restart_entries;const auto begins=hold_begins;
    at=observed_jip_logs.size();hold_deferred_start(true);
    assert(gns_transport::fixture_restart_entries==entries+1 && hold_begins==begins+1 && !hold_at_begin && hold_draws_at_begin==777);
    HOLD_EXPECT(at,"join in progress: the cohort restart goes on without 1 newcomer(s) lost before it (mask 0x2)");
    HOLD_EXPECT(at,"this PC's cohort restart begins");
    assert(!join_in_progress::admission.lost && !join_in_progress::admission.announced && !join_in_progress::hold.active() && !join_in_progress::world_hold);
    std::cout<<"PASS AR-1 (host, newcomer lost before its image, old peer present): the decided seat leaves through the production advance, the host stays held (no 'every newcomer left'), the owed restart captures the still-held world (draws 777), and the restart entry runs the transport's entry before the hold ends and before the native campaign start\n";

    // ---- AR-2. Lost during the transfer (streaming), one newcomer: the running transfer restarts; the hold stays until it ----
    three();admit(1);
    machine.seats[0].at=stage::streaming;put(m,manager_sender,0x1234u); // the image was captured, the native sender runs
    join_in_progress::newcomer_lost_decided(1);lose_remote();
    join_in_progress::guarded_advance(false,nullptr);assert(!machine.any());
    lost_owed_starts=0;at=observed_jip_logs.size();
    join_in_progress::owed_restart_tick();
    assert(lost_owed_starts==0); // the sender's completion (0x26) restarts the cohort; nothing owed
    assert(hold_ticks(20,100)==0 && join_in_progress::world_hold==1);
    // the 0x26 broadcast: from here no removal on this PC until its restored zone runs
    join_in_progress::epoch.announced=true;
    assert(join_in_progress::restart_begun() && join_in_progress::restart_removal_held() && join_in_progress::admission_newcomer(1)); // still a newcomer, but the restart has begun: the ordinary in-epoch path
    put(m,manager_sender,0u);
    hold_deferred_start(true);assert(!join_in_progress::world_hold && !join_in_progress::admission.lost);
    std::cout<<"PASS AR-2 (host, lost during the transfer): no owed restart while the native sender runs, the host stays held until its restart, the 0x26 opens the no-removal window\n";

    // ---- AR-3. Two newcomers, one lost (the other must still get in): the hold waits for the other, then 'ready' ----
    three();put(existing,0x18,2);admit(2);
    assert(join_in_progress::hold.newcomers==2 && join_in_progress::admission.announced==0x06);
    machine.seats[0].at=stage::streaming;machine.seats[1].at=stage::streaming;put(m,manager_sender,0x1234u);
    assert(join_in_progress::admission_remaining(1)==0x04);
    join_in_progress::newcomer_lost_decided(1);lose_remote();
    at=observed_jip_logs.size();join_in_progress::guarded_advance(false,nullptr);
    HOLD_EXPECT(at,"player 1 left while streaming; the seat is free again (lost before the restart");
    assert(machine.seats[1].at==stage::streaming && machine.seats[1].index==2);
    assert(hold_ticks(10,100)==0 && join_in_progress::world_hold==1 && !hold_logged(at,"every newcomer"));
    machine.seats[1].at=stage::starting;at=observed_jip_logs.size();
    assert(hold_ticks(1,100)==0);
    HOLD_EXPECT(at,"join hold: every newcomer is ready after");
    lost_owed_starts=0;join_in_progress::owed_restart_tick();assert(lost_owed_starts==0);
    put(m,manager_sender,0u);hold_deferred_start(true);assert(!join_in_progress::world_hold);
    std::cout<<"PASS AR-3 (host, two newcomers, one lost during the transfer): the other stays streaming, the hold waits for it and reports ready when its image is in, no owed restart\n";

    // ---- AR-4. Lost with no old peer left (host + newcomer only): no restart is owed; the host plays on ----
    put(s,0x26c,nodes[0]);put(s,0x278,2u);put(nodes[0],node_record,local);put(nodes[0],node_next,nodes[1]);put(nodes[1],node_record,remote);put(nodes[1],node_next,static_cast<void*>(nullptr));
    admit(1);join_in_progress::newcomer_lost_decided(1);put(nodes[0],node_next,static_cast<void*>(nullptr));put(s,0x278,1u);
    join_in_progress::guarded_advance(false,nullptr);
    lost_owed_starts=0;at=observed_jip_logs.size();const auto ends_before=gns_transport::fixture_session_entries;join_in_progress::owed_restart_tick();
    assert(lost_owed_starts==0 && !join_in_progress::admission.lost);
    HOLD_EXPECT(at,"no other participant remains - no restart needed; the host plays on");
    assert(gns_transport::fixture_session_entries==ends_before+1); // the admission's end reaches the transport (its decision record reset: review P1)
    at=observed_jip_logs.size();assert(hold_ticks(1,100)==0);HOLD_EXPECT(at,"every newcomer of this hold left (seat freed); the session plays on");
    assert(!join_in_progress::world_hold && hold_ticks(2,100)==2);
    std::cout<<"PASS AR-4 (host + lost newcomer only): no restart owed, the hold ends as before and the host plays on\n";

    // ---- AR-5. An existing guest: the accepted boundary mask is recorded for the transport (never removed by its own timeout) ----
    three();put(local,0x18,1);put(remote,0x18,0);put(local,record_flags,1u);
    join_in_progress::hold_end(join_hold::event::session);join_in_progress::epoch={};machine.reset();machine.started=true;join_in_progress::hold_playing=true;
    gns_transport::fixture_admitted=0;gns_transport::fixture_boundary_mask=0x0c;gns_transport::fixture_boundary_ready=true;
    hold_loading=true;join_in_progress::area_barrier_bridge(s,nullptr,5.f);hold_loading=false;
    assert(join_in_progress::hold.armed && gns_transport::fixture_admitted==0x0c);
    // its restart entry: the transport's entry (carrying out the host's decisions) before the hold ends
    const auto guest_entries=gns_transport::fixture_restart_entries;
    hold_deferred_start(true);
    assert(gns_transport::fixture_restart_entries==guest_entries+1 && gns_transport::fixture_admitted==0 && !join_in_progress::world_hold);
    // the restart window as an existing guest sees it: its 0x26 (requested) .. the restored zone (epoch ends)
    join_in_progress::epoch.begin();assert(!join_in_progress::restart_removal_held());
    join_in_progress::epoch.request();assert(join_in_progress::restart_removal_held() && !join_in_progress::restart_started());
    join_in_progress::epoch.start();assert(join_in_progress::restart_removal_held() && join_in_progress::restart_started());
    join_in_progress::epoch.observe(true,false);assert(join_in_progress::restart_removal_held()); // its load seen, the zone not back: still held
    join_in_progress::restart_restored=join_in_progress::restart_zone_back=true;assert(!join_in_progress::restart_removal_held() && join_in_progress::restart_begun()); // the agreed frame 0 may remove
    assert(join_in_progress::epoch.observe(false,false) && !join_in_progress::restart_removal_held() && !join_in_progress::restart_begun());
    join_in_progress::restart_restored=join_in_progress::restart_zone_back=false;
    gns_transport::fixture_boundary_mask=4;
    std::cout<<"PASS AR-5 (existing guest): the accepted admission mask reaches the transport's ledger, its restart entry runs before the hold ends, the no-removal window spans its 0x26 .. restored zone\n";

    // ---- AR-5b. A host decision this PC cannot carry out yet holds its restart entry (bounded), never skipped ----
    {
        const auto polls=gns_transport::fixture_carry_polls,begins=hold_begins;
        gns_transport::fixture_carry_refusals=2;
        auto& e=join_in_progress::epoch;
        e.active=true;e.requested=true;e.started=false;join_in_progress::existing_cohort_member=true;join_in_progress::late_restart=false;
        join_in_progress::deferred_start_task=0;
        join_in_progress::deferred_campaign_start(nullptr); // refused: the task stays, nothing of the restart ran
        assert(hold_begins==begins && !e.started && e.requested && gns_transport::fixture_carry_polls==polls+1);
        join_in_progress::deferred_campaign_start(nullptr); // refused again
        assert(hold_begins==begins && gns_transport::fixture_carry_polls==polls+2);
        join_in_progress::deferred_campaign_start(nullptr); // carried out: the restart begins
        assert(hold_begins==begins+1 && gns_transport::fixture_carry_polls==polls+3);
        e={};join_in_progress::existing_cohort_member=false;
        // a decision that can never be carried out: after 3 s the restart goes on and says so
        gns_transport::fixture_carry_refusals=-1;
        e.active=true;e.requested=true;e.started=false;join_in_progress::existing_cohort_member=true;
        join_in_progress::deferred_campaign_start(nullptr);assert(hold_begins==begins+1);
        join_in_progress::restart_entry_wait=(GetTickCount()-3001)|1;at=observed_jip_logs.size();
        join_in_progress::deferred_campaign_start(nullptr);assert(hold_begins==begins+2);
        HOLD_EXPECT(at,"a host removal decision could not be carried out within 3000 ms - the restart goes on");
        gns_transport::fixture_carry_refusals=0;e={};join_in_progress::existing_cohort_member=false;
    }
    // the no-removal window: from the 0x26 until the restored zone runs again (the mode-2 load returned, then no load)
    {
        auto& e=join_in_progress::epoch;
        e={};e.begin();e.request();assert(join_in_progress::restart_removal_held());
        e.start();join_in_progress::restart_restored=false;join_in_progress::restart_zone_back=false;
        assert(join_in_progress::restart_removal_held()); // inside its native start (ZJ-7j: after resetgame, before the mode-2 load)
        join_in_progress::restart_restored=true;assert(join_in_progress::restart_removal_held()); // restored, the zone still loading
        join_in_progress::restart_zone_back=true;assert(!join_in_progress::restart_removal_held() && join_in_progress::restart_begun()); // the agreed frame 0 may remove
        join_in_progress::restart_restored=join_in_progress::restart_zone_back=false;e={};
    }
    std::cout<<"PASS AR-5b (restart entry): a host decision not yet carried out holds the production deferred start (the task stays, no native start), it begins once carried out, and after 3 s it goes on with a log; the no-removal window spans the 0x26 .. the restored zone running again\n";

    // ---- AR-6. The session's end clears the admission and the transport's ledger (host and existing guest) ----
    put(local,0x18,0);put(remote,0x18,1);three();admit(1);join_in_progress::newcomer_lost_decided(1);
    gns_transport::fixture_admitted=0x02;
    const auto session_entries=gns_transport::fixture_session_entries;
    put(s,0x26c,static_cast<void*>(nullptr));
    assert(!join_in_progress::guarded_advance(false,nullptr));
    assert(gns_transport::fixture_session_entries==session_entries+1 && gns_transport::fixture_admitted==0 && !join_in_progress::admission.lost && !join_in_progress::admission.announced && !join_in_progress::world_hold);
    // an existing guest's ledger (no host admission state at all) is cleared at its session's end too (review P3)
    gns_transport::fixture_admitted=0x0c;
    assert(!join_in_progress::guarded_advance(false,nullptr));
    assert(gns_transport::fixture_session_entries==session_entries+2 && gns_transport::fixture_admitted==0);
    three();join_in_progress::epoch={};join_in_progress::area_load=false;machine.reset();
    std::cout<<"PASS AR-6 (session end): the host's admission and every PC's transport ledger are cleared unconditionally, the hold released\n";

    // restore the shared fixture state for the blocks after this one
    put(s,0x26c,nodes[0]);put(s,0x278,2u);put(nodes[0],node_record,local);put(nodes[0],node_next,nodes[1]);put(nodes[1],node_record,remote);put(nodes[1],node_next,static_cast<void*>(nullptr));
    join_in_progress::owed_start=&join_in_progress::start_image;join_in_progress::state=was_state;
    join_in_progress::hold_end(join_hold::event::session);join_in_progress::admission.clear();
}
