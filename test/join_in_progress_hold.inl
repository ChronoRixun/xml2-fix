// p060fix/AQ: the join hold against the mapped retail image (no launch). The world-section gate bridge is entered by
// CALL exactly as installed over `test byte ptr [esi+0x728], 2` at 0x46f0a9; a main-loop iteration is modelled as
// that gate plus, when the site's je would NOT skip, one world step (a synchronized draw and a game-time step). The
// hold opens through the production entries (the host's image capture, an existing participant's late-restart 0x47
// through the production campaign_notice), the production pump poll runs the cap / readiness / gone paths, and the
// production deferred_campaign_start releases it right before the native campaign start (stubbed: it records what
// the world looked like at that moment). The on-screen message goes through the REAL retail 0x60a600 / 0x60a690 on a
// synthetic scheduler with a synthetic clock.
alignas(16) unsigned char hold_game[0x800]{};
const void* hold_bridge_target=reinterpret_cast<const void*>(&join_in_progress::world_gate_bridge);
unsigned hold_flags=0,hold_regs[7]{};
__declspec(naked) void hold_probe() {
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        lea esi, hold_game
        mov eax, 11111111h
        mov ebx, 22222222h
        mov ecx, 33333333h
        mov edx, 44444444h
        mov edi, 55555555h
        mov ebp, 66666666h
        call dword ptr [hold_bridge_target]
        pushfd
        pop dword ptr [hold_flags]
        mov dword ptr [hold_regs], eax
        mov dword ptr [hold_regs+4], ebx
        mov dword ptr [hold_regs+8], ecx
        mov dword ptr [hold_regs+12], edx
        mov dword ptr [hold_regs+16], esi
        mov dword ptr [hold_regs+20], edi
        mov dword ptr [hold_regs+24], ebp
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
    }
}
// The bridge as the site sees it: ZF decides the je. Registers must come back exactly as they went in.
bool hold_gate_skips(unsigned char flag_byte) {
    hold_game[join_in_progress_rules::world_gate_flag_at]=flag_byte;
    hold_probe();
    assert(hold_regs[0]==0x11111111u && hold_regs[1]==0x22222222u && hold_regs[2]==0x33333333u && hold_regs[3]==0x44444444u);
    assert(hold_regs[4]==unsigned(reinterpret_cast<std::uintptr_t>(hold_game)) && hold_regs[5]==0x55555555u && hold_regs[6]==0x66666666u);
    const unsigned five=hold_flags&0x8c5u;
    assert(five==0x44u || five==0x00u); // exactly the flags retail's test leaves (zero result / bit set)
    return (hold_flags&0x40u)!=0;
}
unsigned hold_world_steps=0;
// One main-loop iteration's game update: the world section runs only when the gate does not skip.
bool hold_iteration() {
    if(hold_gate_skips(0x02)) return false; // bit 1 of game+0x728 set: retail would run the section
    ++hold_world_steps;
    ++*reinterpret_cast<unsigned*>(join_in_progress_rules::rng_count);
    *reinterpret_cast<unsigned*>(join_in_progress_rules::game_static+join_in_progress_rules::game_time_at)+=0x10;
    return true;
}
// Synthetic clock (0x55b610 getter, vt+0x28 = seconds) and scheduler (0x60b210 getter, message slots at +0x1c8).
float hold_seconds=100.0f;
float __fastcall hold_clock_seconds(void*,void*,int) { return hold_seconds; }
unsigned hold_clock_vt[0x40]{};
struct { unsigned* vt; } hold_clock_object{hold_clock_vt};
void* __cdecl hold_clock_get() { return &hold_clock_object; }
alignas(16) unsigned char hold_scheduler[0x400]{};
void* __cdecl hold_scheduler_get() { return hold_scheduler; }
DWORD hold_ms=500000;
DWORD WINAPI hold_fake_ms() { return hold_ms; }
unsigned hold_begins=0;bool hold_at_begin=true;unsigned hold_draws_at_begin=0,hold_steps_at_begin=0;
void __cdecl hold_campaign_begin(int) { ++hold_begins;hold_at_begin=join_in_progress::world_hold!=0;hold_draws_at_begin=*reinterpret_cast<unsigned*>(join_in_progress_rules::rng_count);hold_steps_at_begin=hold_world_steps; }
const char* hold_slot_text(unsigned i) { return *reinterpret_cast<const char**>(hold_scheduler+join_in_progress_rules::message_slots_at+16*i); }
unsigned hold_current() { return *reinterpret_cast<unsigned*>(hold_scheduler+join_in_progress_rules::message_current_at); }
bool hold_logged(std::size_t from,const char* text) {
    for(std::size_t i=from;i<observed_jip_logs.size();++i) if(observed_jip_logs[i].find(text)!=std::string::npos) return true;
    return false;
}
void hold_dump(std::size_t from) { for(std::size_t i=from;i<observed_jip_logs.size();++i) std::cerr<<"  log: "<<observed_jip_logs[i]<<"\n"; }
// Ticks the production pump poll `count` times, `step` ms apart, with one main-loop iteration each.
unsigned hold_ticks(unsigned count,DWORD step) {
    unsigned ran=0;
    for(unsigned i=0;i<count;++i) { hold_ms+=step;hold_seconds+=float(step)/1000.0f;if(hold_iteration()) ++ran;join_in_progress::hold_tick(); }
    return ran;
}
// The production deferred start of a cohort restart (the existing-member path with its natives stubbed).
void hold_deferred_start(bool member) {
    auto& e=join_in_progress::epoch;
    e.active=true;e.requested=true;e.started=false;
    join_in_progress::existing_cohort_member=member;join_in_progress::deferred_start_task=0;join_in_progress::late_restart=false;
    join_in_progress::deferred_campaign_start(nullptr);
    e={};join_in_progress::existing_cohort_member=false;
}
void run_join_hold() {
    using namespace join_in_progress_rules;
    using join_in_progress_core::join_hold;
    using join_in_progress_core::stage;
    auto& machine=join_in_progress::machine;
    redirect(clock_getter,hold_clock_get);hold_clock_vt[0x28/4]=unsigned(reinterpret_cast<std::uintptr_t>(&hold_clock_seconds));
    redirect(scheduler_getter,hold_scheduler_get);
    redirect(clock_reset_step,noop);redirect(network_setup,noop_one);redirect(pause_getter,pause_get);redirect(pause_set,noop_one);redirect(clear_flags,noop_one);
    redirect(zones_getter,zones);redirect(campaign_begin,hold_campaign_begin);
    redirect(session_getter,session);redirect(manager_getter,manager);
    join_in_progress::hold_clock=&hold_fake_ms;
    join_in_progress::hold.configure(6);
    assert(join_in_progress::hold.cap_ms==6000 && join_in_progress_core::join_hold_default_seconds==6);
    auto* draws=reinterpret_cast<unsigned*>(rng_count);auto* frame=reinterpret_cast<unsigned short*>(native_frame);
    // Hold off: the bridge is retail's own test for every bit pattern (the section runs exactly when bit 1 is set).
    join_in_progress::world_hold=0;join_in_progress::world_hold_skips=0;
    assert(hold_gate_skips(0x00) && !hold_gate_skips(0x02) && hold_gate_skips(0xfd) && !hold_gate_skips(0xff) && join_in_progress::world_hold_skips==0);

    // ---- 1. Host, one newcomer, ready before the cap; the restart releases it right before the native start ----
    put(local,record_flags,1u);put(local,0x18,0);put(remote,0x18,1);
    put(s,0x26c,nodes[0]);put(s,0x278,2u);put(nodes[0],node_record,local);put(nodes[0],node_next,nodes[1]);put(nodes[1],node_record,remote);put(nodes[1],node_next,static_cast<void*>(nullptr));
    machine.reset();machine.started=true;machine.seats[0]={remote,1,stage::streaming,hold_ms};
    *draws=4921;*frame=2726;hold_world_steps=0;
    assert(hold_iteration() && *draws==4922); // before the capture the host simulates (the section runs)
    *draws=4921;hold_world_steps=0;
    auto at=observed_jip_logs.size();
    join_in_progress::hold_open(join_hold::role::host,1,"fixture: the image capture at the zone load"); // what start_image calls after the capture
    assert(join_in_progress::world_hold==1 && join_in_progress::hold.active() && join_in_progress::hold.newcomers==1);
    assert(hold_logged(at,"join hold: the game simulation is held on this PC (host) from fixture: the image capture at the zone load: 1 newcomer(s), cap 6000 ms ([Online] JoinHoldSeconds=6); frame 2726, synchronized draws 4921")
           && hold_logged(at,"shows \"A player is joining...\""));
    // the REAL retail 0x60a600 put the text in the current slot with the hold's priority and the cap + restart allowance
    assert(hold_current()==0 && std::strcmp(hold_slot_text(0),"A player is joining...")==0);
    assert(*reinterpret_cast<int*>(hold_scheduler+message_slots_at+4)==join_hold_priority);
    assert(*reinterpret_cast<float*>(hold_scheduler+message_slots_at+12)==10.0f); // 6 s cap + 4 s restart allowance
    // 30 main-loop iterations 100 ms apart: the section never runs, no draw, no game time, the skips counted
    assert(hold_ticks(30,100)==0 && *draws==4921 && hold_world_steps==0 && join_in_progress::world_hold_skips==30 && join_in_progress::world_hold==1);
    // the newcomer's J arrives (the seat goes to starting): ready, still held (the native start's 1 s wait follows)
    machine.seats[0].at=stage::starting;at=observed_jip_logs.size();
    assert(hold_ticks(1,100)==0 && hold_logged(at,"join hold: every newcomer is ready after 3100 ms (1 newcomer(s) have the campaign image); held until this PC's restart begins"));
    assert(hold_ticks(10,100)==0 && join_in_progress::world_hold==1 && *draws==4921);
    // the production deferred start: the hold ends first, the native campaign start sees the world exactly as captured
    at=observed_jip_logs.size();
    hold_deferred_start(true);
    assert(hold_begins==1 && !hold_at_begin && hold_draws_at_begin==4921 && hold_steps_at_begin==0);
    if(!hold_logged(at,"join hold: 4100 ms, 1 newcomer(s) ready - this PC's cohort restart begins (41 world update(s) skipped while held; frame 2726 -> 2726, synchronized draws 4921 -> 4921")) { hold_dump(at);assert(false); }
    assert(!join_in_progress::hold.active() && join_in_progress::world_hold==0 && hold_slot_text(0)==nullptr); // retail 0x60a690 expired the message
    assert(hold_iteration() && *draws==4922); // the restarted world runs
    std::cout<<"PASS join hold (host, 1 newcomer): held from the capture; 41 iterations with no world step, no draw, no game time; ready at 3100 ms; released inside the production deferred start before the native campaign start (draws 4921 -> 4921); retail 0x60a600 showed and 0x60a690 expired \"A player is joining...\"\n";

    // ---- 2. Host, two newcomers admitted at the same zone load (the second rides the transfer) ----
    std::memset(hold_scheduler,0,sizeof hold_scheduler);
    machine.seats[0]={remote,1,stage::streaming,hold_ms};machine.seats[1]={existing,2,stage::streaming,hold_ms};
    *draws=4000;hold_world_steps=0;at=observed_jip_logs.size();
    join_in_progress::hold_open(join_hold::role::host,1,"fixture: the image capture");
    join_in_progress::hold_add_newcomer(); // start_image's rider path
    assert(join_in_progress::hold.newcomers==2 && hold_logged(at,"join hold: 2 newcomer(s) now in this hold"));
    assert(std::strcmp(hold_slot_text(hold_current()),"2 players are joining...")==0 && hold_slot_text(1)==nullptr); // re-pointed, not queued
    assert(hold_ticks(20,100)==0);
    machine.seats[0].at=stage::starting;assert(hold_ticks(5,100)==0 && !join_in_progress::hold.all_ready); // one of two: still waiting
    machine.seats[1].at=stage::starting;at=observed_jip_logs.size();
    assert(hold_ticks(1,100)==0 && hold_logged(at,"every newcomer is ready after 2600 ms (2 newcomer(s)"));
    at=observed_jip_logs.size();hold_deferred_start(true);
    assert(hold_begins==2 && !hold_at_begin && hold_draws_at_begin==4000 && hold_logged(at,"join hold: 2600 ms, 2 newcomer(s) ready - this PC's cohort restart begins (26 world update(s) skipped"));
    std::cout<<"PASS join hold (host, 2 newcomers): the rider joins the open hold, the message re-points to \"2 players are joining...\", ready only when both images are in, released at the restart with no world step\n";

    // ---- 3. Cap reached by a throttled joiner: the host simulates again (today's path), the joiner is not refused ----
    std::memset(hold_scheduler,0,sizeof hold_scheduler);
    machine.seats={};machine.seats[0]={remote,1,stage::streaming,hold_ms};
    *draws=100;hold_world_steps=0;
    join_in_progress::hold_open(join_hold::role::host,1,"fixture: the image capture");
    assert(hold_ticks(59,100)==0 && join_in_progress::world_hold==1); // 5900 ms
    at=observed_jip_logs.size();
    assert(hold_ticks(1,100)==0); // the 6000 ms poll ends it after this iteration's gate
    if(!hold_logged(at,"join hold: cap reached after 6000 ms (0 of 1 newcomer(s) ready) - this PC simulates again until the restart, as without the hold; the newcomer is not refused (60 world update(s) skipped while held")) { hold_dump(at);assert(false); }
    assert(!join_in_progress::hold.active() && join_in_progress::world_hold==0 && hold_slot_text(0)==nullptr);
    assert(hold_ticks(10,100)==10 && *draws==110); // today's path: the host plays on while the image goes out
    assert(machine.seats[0].at==stage::streaming && machine.seats[0].record==remote); // the seat is untouched
    machine.seats[0].at=stage::starting;at=observed_jip_logs.size();hold_deferred_start(true);
    assert(hold_begins==3 && !hold_logged(at,"join hold:")); // the restart still runs; nothing more to release
    std::cout<<"PASS join hold (cap): a joiner not ready at 6000 ms ends the hold (logged), the host simulates again, the seat and the transfer are untouched and the restart still runs\n";

    // ---- 4. The joiner is killed / leaves during the hold: its seat is freed, the hold ends, no stall ----
    machine.seats={};machine.seats[0]={remote,1,stage::streaming,hold_ms};*draws=200;
    join_in_progress::hold_open(join_hold::role::host,1,"fixture: the image capture");
    assert(hold_ticks(15,100)==0);
    machine.seats[0]={}; // advance(): "player 1 left while streaming; the seat is free again"
    at=observed_jip_logs.size();assert(hold_ticks(1,100)==0);
    assert(hold_logged(at,"join hold: ended after 1600 ms - every newcomer of this hold left (seat freed); the session plays on") && join_in_progress::world_hold==0);
    assert(hold_ticks(3,100)==3);
    // two newcomers, one leaves: the hold goes on for the other
    machine.seats[0]={remote,1,stage::streaming,hold_ms};machine.seats[1]={existing,2,stage::streaming,hold_ms};
    join_in_progress::hold_open(join_hold::role::host,1,"fixture: the image capture");join_in_progress::hold_add_newcomer();
    machine.seats[1]={};assert(hold_ticks(5,100)==0 && join_in_progress::world_hold==1);
    machine.seats[0].at=stage::starting;assert(hold_ticks(1,100)==0 && join_in_progress::hold.all_ready && join_in_progress::hold.ready==1);
    hold_deferred_start(true);assert(!join_in_progress::world_hold);
    std::cout<<"PASS join hold (joiner gone): a freed seat ends the hold at the next pump and the host plays on; with two newcomers the hold keeps waiting for the one that stays\n";

    // ---- 5. The host quits during the hold: the session's end releases it (the production pump path) ----
    machine.seats={};machine.seats[0]={remote,1,stage::streaming,hold_ms};
    join_in_progress::hold_open(join_hold::role::host,1,"fixture: the image capture");assert(hold_ticks(10,100)==0);
    put(s,0x26c,static_cast<void*>(nullptr));at=observed_jip_logs.size();
    assert(!join_in_progress::guarded_advance(false,nullptr));
    assert(hold_logged(at,"join hold: ended after 1000 ms by the session's end") && !join_in_progress::world_hold && !machine.started && !machine.any());
    put(s,0x26c,nodes[0]);
    std::cout<<"PASS join hold (host Quit): the session's end in the production pump path releases the hold\n";

    // ---- 6. Existing participants (3 and 4 peers): the production campaign_notice of the late-restart 0x47 ----
    alignas(4) unsigned char body[8]{2,0,0,0,0,0,0,0};
    alignas(4) unsigned char event[8]{};event[0]=opcode_prepare;put(event,4,static_cast<void*>(body));
    for(int joining=1;joining<=3;++joining) {
        std::memset(hold_scheduler,0,sizeof hold_scheduler);
        machine.reset();machine.started=true;join_in_progress::mesh_newcomers=joining; // "new mesh participant" per newcomer
        const auto marker=join_in_progress_core::prepare_stamp(true);std::memcpy(body+join_in_progress_core::prepare_marker_at,&marker,2);
        *draws=4921;hold_world_steps=0;at=observed_jip_logs.size();
        join_in_progress::campaign_notice(event);
        assert(join_in_progress::late_restart && join_in_progress::existing_cohort_member && join_in_progress::hold.active() && join_in_progress::hold.who==join_hold::role::member);
        assert(join_in_progress::hold.newcomers==joining && std::strcmp(hold_slot_text(0),join_in_progress_core::join_hold_text(joining))==0);
        assert(hold_logged(at,"join hold: the game simulation is held on this PC (existing participant) from the late-restart 0x47"));
        assert(hold_ticks(40,100)==0 && !join_in_progress::hold.all_ready); // a member cannot see seats: only its restart or the cap
        at=observed_jip_logs.size();hold_deferred_start(true);
        char line[96];std::snprintf(line,sizeof line,"join hold: 4000 ms, %d newcomer(s) ready - this PC's cohort restart begins",joining);
        assert(hold_logged(at,line) && hold_draws_at_begin==4921 && hold_steps_at_begin==0 && join_in_progress::mesh_newcomers==0);
        join_in_progress::late_restart=false;
    }
    // a member whose host died: the cap releases it (then the host-left path runs as before, off the network)
    machine.started=true;join_in_progress::mesh_newcomers=1;join_in_progress::campaign_notice(event);
    at=observed_jip_logs.size();assert(hold_ticks(60,100)==0 && hold_logged(at,"join hold: cap reached after 6000 ms (0 of 1 newcomer(s) ready)") && !join_in_progress::world_hold);
    join_in_progress::epoch={};join_in_progress::late_restart=false;
    // the newcomer itself (Start Game never passed in this session) and a lobby START (no marker) never hold
    machine.started=false;join_in_progress::campaign_notice(event);assert(!join_in_progress::hold.active() && !join_in_progress::world_hold);
    join_in_progress::epoch={};join_in_progress::late_restart=false;
    machine.started=true;std::memset(body+join_in_progress_core::prepare_marker_at,0,2);join_in_progress::campaign_notice(event);
    assert(!join_in_progress::late_restart && !join_in_progress::hold.active() && !join_in_progress::world_hold);
    join_in_progress::epoch={};machine.reset();join_in_progress::existing_cohort_member=false;
    std::cout<<"PASS join hold (existing participants, 1-3 newcomers = 3 and 4 peers): the production late-restart 0x47 opens the hold with the count from the mesh, only the restart or the cap ends it, the newcomer and a lobby START never hold\n";

    // ---- 7. [Online] JoinHoldSeconds=0: off (today's path) ----
    join_in_progress::hold.configure(0);
    machine.seats[0]={remote,1,stage::streaming,hold_ms};
    join_in_progress::hold_open(join_hold::role::host,1,"fixture: the image capture");
    assert(!join_in_progress::hold.active() && !join_in_progress::world_hold && hold_ticks(5,100)==5);
    join_in_progress::hold.configure(99);assert(join_in_progress::hold.cap_ms==15000);
    join_in_progress::hold.configure(6);machine.reset();
    std::cout<<"PASS join hold (JoinHoldSeconds=0): nothing is held; the range is clamped to 0..15\n";
    join_in_progress::hold_clock=&GetTickCount;
}
