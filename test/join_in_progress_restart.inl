// AE: the canonical cohort restart against the mapped retail image (no launch): the pristine camera image is
// taken by the construction bridge, restored before the native campaign start, the real retail seed setter
// (0x560150) runs at the first shared frame through the frame-gate bridge, the 0x47 stamp bridge is entered BY JUMP
// exactly as installed, and a given-up seat is released through the native participant drop.
namespace restart_fixture {
unsigned constructor_calls=0,input_runs=0,drops=0;void* input_run_this=nullptr;
void* drop_session=nullptr;void* drop_record=nullptr;unsigned drop_codes[4]{};
void __fastcall fake_constructor(void* self,void*,int) {
    ++constructor_calls;
    put(self,0,join_in_progress_rules::camera_vtable_value);
    put(self,join_in_progress_rules::camera_yaw_at,0u);
    put(self,join_in_progress_rules::camera_mode_at,0u);
    put(self,0x2f8,0x3f490fdbu); // pitch 45 deg as the constructor stores it
}
void __fastcall fake_input_run(void* self,void*) { ++input_runs;input_run_this=self; }
void __fastcall fake_drop(void* self,void*,void* record,unsigned a,unsigned b,unsigned c,unsigned d) {
    ++drops;drop_session=self;drop_record=record;drop_codes[0]=a;drop_codes[1]=b;drop_codes[2]=c;drop_codes[3]=d;
}
// The getter's call site shape: `push 0; mov ecx,0x7171d0; call ctor` with a return into the getter.
const void* construct_bridge_target=reinterpret_cast<const void*>(&join_in_progress::camera_construct_bridge);
__declspec(naked) void call_through_bridge() {
    __asm {
        push 0
        mov ecx, 7171d0h
        call dword ptr [construct_bridge_target]
        ret
    }
}
// The stamp bridge is installed as a JMP at 0x6095a5: it sees Start Game's ESP with no return address. The probe
// builds that frame (0x40 bytes filled with 0xee), enters the bridge through a thunk that pops its own return
// address (so the bridge sees exactly a JMP entry), and the bridge's resume (0x6095aa in retail) is redirected to a
// stub that jumps back behind the thunk call.
unsigned getter_calls=0;unsigned char getter_object[16]{};
void* __cdecl fake_getter() { ++getter_calls;return getter_object; }
join_in_progress_core::draw_trace fixture_trace;
__declspec(naked) void draw_resume_stub() { __asm { ret } }
const void* draw_bridge_target=reinterpret_cast<const void*>(&join_in_progress::draw_trace_bridge);
// `call dword ptr [draw_bridge_target]` is 6 bytes: the bridge's [esp] (the caller's return) is this function + 6.
__declspec(naked) void draw_probe() { __asm { call dword ptr [draw_bridge_target]
                                             ret } }
// Follow-up 7: a fixture-owned timed-event manager image and the owner resolver's fake entities (slots 1 and 2 alive).
alignas(16) unsigned char event_manager_image[join_in_progress_rules::event_body_bytes+join_in_progress_rules::event_body_at+16]{};
alignas(16) unsigned char fake_entity_storage[4][0x400]{};
unsigned char* fake_entities[4]={fake_entity_storage[0],fake_entity_storage[1],fake_entity_storage[2],fake_entity_storage[3]};
void* __fastcall fake_entity_of(const unsigned* handle,void*) { const unsigned slot=*handle&0xff;return (*handle==0x1801u||*handle==0x3802u)?fake_entities[slot]:nullptr; }
// Follow-up 5: a fake CSmartEntMgr (count 3 through vt+0x14, cursor at +4) behind the redirected getter.
unsigned smart_vtable[8]{};unsigned char smart_object[0x20]{};unsigned smart_getter_calls=0;
int __fastcall fake_smart_count(void*,void*) { return 3; }
void* __cdecl fake_smart_getter() { ++smart_getter_calls;return smart_object; }
__declspec(naked) void spawn_resume_stub() { __asm { pop esi
                                                     pop ebp
                                                     pop ebx
                                                     pop ecx
                                                     ret } }
unsigned anchor_vtable[0x130/4]{};unsigned* anchor_game=anchor_vtable;unsigned party_calls=0;
void* __cdecl anchor_game_get() { return &anchor_game; }
void __fastcall anchor_party(void*,void*,void* list) { ++party_calls;put(list,0,0x3803u);put(list,4,0x3801u);put(list,8,0x3804u);put(list,0xc,0x3802u);put(list,0x14,4u); }
void __fastcall moved_party(void*,void*,void* list) { put(list,0,0x3802u);put(list,4,0u);put(list,8,0u);put(list,0xc,0u);put(list,0x14,1u); } // follow-up 12: one party hero (fake entity slot 2)
const void* anchor_bridge_target=reinterpret_cast<const void*>(&join_in_progress::camera_anchor_table_bridge);
unsigned anchor_ebx=0;
// The site is `mov ebx, 0x70b814` replaced by a CALL: the bridge returns with EBX set; the thunk keeps the C++ caller's EBX.
__declspec(naked) void anchor_probe() {
    __asm {
        push ebx
        mov ebx, 0deadbeefh
        call dword ptr [anchor_bridge_target]
        mov dword ptr [anchor_ebx], ebx
        pop ebx
        ret
    }
}
const void* stamp_bridge_target=reinterpret_cast<const void*>(&join_in_progress::prepare_opcode_bridge);
std::uint32_t stamp_return=0;
unsigned char stamp_frame[0x40]{};unsigned stamp_eax=0;
__declspec(naked) void resume_stub() { __asm { jmp dword ptr [stamp_return] } }
__declspec(naked) void jump_thunk() {
    __asm {
        pop eax
        mov dword ptr [stamp_return], eax
        mov eax, 5a5aa5a5h
        jmp dword ptr [stamp_bridge_target]
    }
}
__declspec(naked) void stamp_probe() {
    __asm {
        push esi
        push edi
        sub esp, 40h
        mov edi, esp
        mov ecx, 10h
        mov eax, 0eeeeeeeeh
        rep stosd
        call jump_thunk
        mov dword ptr [stamp_eax], eax
        mov esi, esp
        lea edi, stamp_frame
        mov ecx, 10h
        rep movsd
        add esp, 40h
        pop edi
        pop esi
        ret
    }
}
void check_stamp(bool late) {
    join_in_progress::late_restart_sending=late;
    stamp_probe();
    assert(stamp_eax==0x5a5aa5a5u); // EAX (the body pointer at the site) survives
    assert(stamp_frame[0x30]==0x47); // the displaced opcode write, at the site's [esp+0x30]
    unsigned short tail=0;std::memcpy(&tail,stamp_frame+0x1e,2);
    assert(tail==(late ? join_in_progress_core::late_restart_marker : 0)); // the body's tail bytes [esp+0x1e..0x1f]
    for(unsigned i=0;i<0x40;++i) if(i!=0x30 && i!=0x1e && i!=0x1f) assert(stamp_frame[i]==0xee); // nothing else touched
}
// Follow-up 11: the world-gate bridge is installed as a CALL over the 7-byte `test byte ptr [esi+0x728], 2` at
// 0x46f0a9 (ESI = the game object at the site). The probe enters it by CALL - the same frame shape as the installed
// site - with ESI = a fake game object and every other register a known value, and captures EFLAGS and the
// registers right after the bridge returns: hold off, the flags are exactly the retail instruction's; hold on, ZF=1.
alignas(16) unsigned char world_game[0x800]{};
const void* world_bridge_target=reinterpret_cast<const void*>(&join_in_progress::world_gate_bridge);
unsigned world_flags=0,world_regs[7]{},world_clock_now=0;
DWORD WINAPI world_fake_clock() { return world_clock_now; }
__declspec(naked) void world_probe() {
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        lea esi, world_game
        mov eax, 11111111h
        mov ebx, 22222222h
        mov ecx, 33333333h
        mov edx, 44444444h
        mov edi, 55555555h
        mov ebp, 66666666h
        call dword ptr [world_bridge_target]
        pushfd
        pop dword ptr [world_flags]
        mov dword ptr [world_regs], eax
        mov dword ptr [world_regs+4], ebx
        mov dword ptr [world_regs+8], ecx
        mov dword ptr [world_regs+12], edx
        mov dword ptr [world_regs+16], esi
        mov dword ptr [world_regs+20], edi
        mov dword ptr [world_regs+24], ebp
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
    }
}
// flag byte -> the bridge's ZF: a zero result (bit 1 clear, or held) leaves ZF=1 PF=1 SF=0 CF=0 OF=0 (0x44 of the
// mask 0x8c5), a set bit (result 2: odd parity) leaves all five clear - exactly what retail's test produces.
void check_world_gate(unsigned char flag_byte,bool expect_skip) {
    world_game[join_in_progress_rules::world_gate_flag_at]=flag_byte;
    world_probe();
    assert((world_flags&0x8c5u)==(expect_skip ? 0x44u : 0x00u));
    assert(world_regs[0]==0x11111111u && world_regs[1]==0x22222222u && world_regs[2]==0x33333333u && world_regs[3]==0x44444444u);
    assert(world_regs[4]==unsigned(reinterpret_cast<std::uintptr_t>(world_game)) && world_regs[5]==0x55555555u && world_regs[6]==0x66666666u);
}
void run() {
    using namespace join_in_progress_rules;
    // Follow-up 12: every frame-0 epoch below now walks the party list (game vt+0x11c) and resolves its heroes; the
    // fake game (party 0x3803/0x3801/0x3804/0x3802) and the fake resolver (0x1801 and 0x3802 alive) stand in from the
    // start, never the retail getter/resolver on the mapped image.
    redirect(game_getter,anchor_game_get);anchor_vtable[game_party_list_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&anchor_party));
    join_in_progress::entity_of_handle_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_entity_of));
    auto* camera=reinterpret_cast<unsigned char*>(camera_object);
    std::memset(camera,0,camera_bytes);
    // The construction bridge: the (stubbed) constructor runs with this = the singleton and its stack argument, then
    // the image is taken. 0x4449e0 itself needs the settings/console singletons, so the stub stands in for it.
    join_in_progress::camera_constructor_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_constructor));
    call_through_bridge();
    assert(constructor_calls==1 && join_in_progress::restart.snapshot);
    assert(std::memcmp(join_in_progress::camera_pristine,camera,camera_bytes)==0);
    // Play changes the camera; a cohort restart returns it before the native campaign start.
    put(camera,camera_yaw_at,0x3ec9f3f8u); put(camera,camera_mode_at,2u); put(camera,camera_yaw_history_at,0x3ec9f3f8u); put(camera,0x104,0x4502caceu);
    join_in_progress::epoch.active=false;join_in_progress::late_restart=false;
    join_in_progress::restore_camera("fixture: no epoch");
    assert(*reinterpret_cast<unsigned*>(camera+camera_yaw_at)==0x3ec9f3f8u); // not a restart: untouched
    join_in_progress::epoch.active=true;
    join_in_progress::restore_camera("fixture: lobby START epoch");
    assert(*reinterpret_cast<unsigned*>(camera+camera_yaw_at)==0x3ec9f3f8u); // a lobby START's epoch: untouched
    join_in_progress::late_restart=true;
    join_in_progress::restore_camera("fixture: cohort restart");
    assert(std::memcmp(join_in_progress::camera_pristine,camera,camera_bytes)==0 && join_in_progress::restart.restores==1);
    assert(*reinterpret_cast<unsigned*>(camera+camera_yaw_at)==0 && *reinterpret_cast<unsigned*>(camera+0x104)==0);
    // A foreign vtable (the singleton replaced by something else) is never overwritten.
    put(camera,0,0x12345678u);
    join_in_progress::restore_camera("fixture: foreign vtable");
    assert(*reinterpret_cast<unsigned*>(camera)==0x12345678u && join_in_progress::restart.restores==1);
    put(camera,0,camera_vtable_value);
    join_in_progress::epoch.active=false;join_in_progress::late_restart=false;
    // The first shared frame: the real retail 0x560150 runs from the mapped image. Not armed: nothing. Armed at a
    // nonzero frame (the old world still ticking): nothing. Armed at frame 0: seed 0x845fed, count 0, once.
    auto* seed=reinterpret_cast<unsigned*>(rng_seed);auto* count=reinterpret_cast<unsigned*>(rng_count);auto* frame=reinterpret_cast<unsigned short*>(native_frame);
    *seed=0x19EB54C7u;*count=272;*frame=0;
    join_in_progress::frame_gate_reseed();
    assert(*seed==0x19EB54C7u && *count==272);
    join_in_progress::restart.arm();
    *frame=31;join_in_progress::frame_gate_reseed();
    assert(*seed==0x19EB54C7u && *count==272 && join_in_progress::restart.armed);
    *frame=0;join_in_progress::frame_gate_reseed();
    assert(*seed==native_seed && *count==0 && !join_in_progress::restart.armed && join_in_progress::restart.reseeds==1);
    *seed=0x11111111u;*count=9;join_in_progress::frame_gate_reseed();
    assert(*seed==0x11111111u && *count==9); // once per epoch
    // The frame-gate bridge itself (installed as a CALL): the native input-manager run is replaced by a counter; the
    // thiscall this survives and the bridge returns to its caller through the replaced run.
    join_in_progress::input_manager_run_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_input_run));
    join_in_progress::restart.arm();*frame=0;*seed=0x22222222u;*count=5;
    const auto bridge=reinterpret_cast<void(__fastcall*)(void*,void*)>(&join_in_progress::frame_gate_run_bridge);
    bridge(reinterpret_cast<void*>(0x1234),nullptr);
    assert(input_runs==1 && input_run_this==reinterpret_cast<void*>(0x1234) && *seed==native_seed && *count==0);
    bridge(reinterpret_cast<void*>(0x1234),nullptr);
    assert(input_runs==2 && *seed==native_seed);
    // The 0x47 stamp bridge, entered by JUMP exactly as installed, on Start Game's frame: opcode 0x47 at [esp+0x30],
    // the tail bytes [esp+0x1e] 0 (lobby START) or the marker (late restart), every other byte untouched, EAX kept.
    join_in_progress::prepare_opcode_resume_at=unsigned(reinterpret_cast<std::uintptr_t>(&resume_stub));
    check_stamp(false);check_stamp(true);check_stamp(false);
    join_in_progress::late_restart_sending=false;
    // A seat given up after its native add is released through the native participant drop (redirected here): the
    // add's wait outlives its limit while the record is still in the session; a pending seat (never added) is not.
    redirect(session_getter,session);redirect(manager_getter,manager);redirect(participant_drop,fake_drop);
    put(local,record_flags,1u);put(local,0x18,0);put(remote,0x18,1);put(remote,record_flags,0u);
    put(s,0x26c,nodes[0]);put(s,0x278,2u);put(nodes[0],node_record,local);put(nodes[0],node_next,nodes[1]);put(nodes[1],node_record,remote);put(nodes[1],node_next,static_cast<void*>(nullptr));
    auto& machine=join_in_progress::machine;machine.reset();machine.started=true;
    assert(machine.defer_add(remote,1,false,GetTickCount()));
    auto* seat=machine.find(remote);assert(seat && seat->at==join_in_progress_core::stage::pending);
    seat->at=join_in_progress_core::stage::adding;seat->since=GetTickCount()-join_in_progress_core::add_limit_ms-1;
    put(m,join_in_progress_rules::manager_wait,static_cast<unsigned char>(1)); // the add's wait is still on: give up
    join_in_progress::area_load=false;
    join_in_progress::advance(s,true,nullptr);
    assert(!machine.find(remote) && drops==1 && drop_session==s && drop_record==remote);
    assert(drop_codes[0]==drop_notice_code && drop_codes[1]==drop_removal_code && drop_codes[2]==drop_removal_code && drop_codes[3]==0);
    // A pending seat whose record left is freed without a drop (nothing was added natively).
    assert(machine.defer_add(remote,1,false,GetTickCount()));put(nodes[0],node_next,static_cast<void*>(nullptr));put(s,0x278,1u);
    join_in_progress::advance(s,true,nullptr);
    assert(!machine.find(remote) && drops==1);
    machine.reset();
    // Follow-up 5: the zone-load notices below also hold the dormant-entity respawn, and the frame-0 reseed releases it
    // through the manager getter: a fake manager (count 3 through vt+0x14, cursor at +4) stands behind it from here on.
    *reinterpret_cast<unsigned**>(smart_object)=smart_vtable;smart_vtable[smart_count_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&fake_smart_count));
    join_in_progress::smart_manager_getter_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_smart_getter));
    join_in_progress::smart_getter_at=join_in_progress::smart_manager_getter_at;
    // latejoin-residue P1: the two zone-load bridges, entered by CALL exactly as installed (their getters redirected
    // to counters that return a fake object), take an epoch only while a late restart is armed, through the real
    // retail 0x560150; the frame-0 epoch then ends the segment epochs.
    join_in_progress::zone_load_entry_target_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_getter));
    join_in_progress::zone_start_entry_target_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_getter));
    const auto load_bridge=reinterpret_cast<void*(__cdecl*)()>(&join_in_progress::zone_load_entry_bridge);
    const auto start_bridge=reinterpret_cast<void*(__cdecl*)()>(&join_in_progress::zone_start_entry_bridge);
    join_in_progress::late_restart=false;join_in_progress::restart.disarm();
    *seed=0x33333333u;*count=44;
    assert(load_bridge()==&getter_object && getter_calls==1 && *seed==0x33333333u && *count==44); // not a late restart: untouched
    join_in_progress::late_restart=true;join_in_progress::restart.arm();
    assert(start_bridge()==&getter_object && getter_calls==2 && *seed==native_seed && *count==0);
    *seed=0x44444444u;*count=5;
    assert(load_bridge()==&getter_object && getter_calls==3 && *seed==native_seed && *count==0);
    *frame=0;join_in_progress::frame_gate_reseed();
    *seed=0x55555555u;*count=7;
    assert(start_bridge()==&getter_object && getter_calls==4 && *seed==0x55555555u && *count==7); // after frame 0: no segment epoch
    // Follow-up 10 (live round 7, failing first on AE10): an EXISTING member's zone start must leave the pump's
    // controls resume pending - the host's frame-0 input is only produced after that later resume ("resumed after
    // world restore", AE9 reached frame 0 within 40 ms of it). AE10 resumed at the zone-start notice and marked the
    // epoch's resume spent, so the pump never resumed, the host never produced frame 0 and both peers froze at the
    // barrier for 120 s. The production notice as the host (existing member, epoch started, loading seen, armed late
    // restart): afterwards the pump's resume is still due, exactly once.
    join_in_progress::existing_cohort_member=true;join_in_progress::epoch.begin();join_in_progress::epoch.start();join_in_progress::epoch.observe(true,false);
    join_in_progress::late_restart=true;join_in_progress::restart.arm();
    assert(start_bridge()==&getter_object && getter_calls==5);
    assert(join_in_progress::epoch.resume_controls(true)); // the pump's resume after the world restore still happens
    assert(!join_in_progress::epoch.resume_controls(true)); // once
    join_in_progress::existing_cohort_member=false;join_in_progress::epoch.active=false;join_in_progress::late_restart=false;join_in_progress::restart.disarm();
    // latejoin-residue P2: the AI brain free ring on a synthetic stock pool: used ring -> identity through retail
    // 0x522f70 when every brain is free; left alone with one brain allocated.
    auto* pool=reinterpret_cast<unsigned char*>(brain_pool_stock);
    put(pool,brain_ring_write,5);put(pool,brain_ring_read,5);put(pool,brain_ring_free,32);put(pool,brain_ring_bitmap,0u);
    for(unsigned i=0;i<32;++i) put(pool,0xc384+4*i,unsigned((i*7)%32));
    join_in_progress::late_restart=true;join_in_progress::restart.arm();
    join_in_progress::canonical_brain_ring();
    assert(*reinterpret_cast<int*>(pool+brain_ring_write)==0 && *reinterpret_cast<int*>(pool+brain_ring_read)==0 && *reinterpret_cast<int*>(pool+brain_ring_free)==32);
    for(unsigned i=0;i<32;++i) assert(*reinterpret_cast<unsigned*>(pool+0xc384+4*i)==i);
    // The probe round: the warm host holds slot 31 (bits 0x80000007 with 0,1,2 also held): the ring is rebuilt over
    // the 28 free slots in ascending order, read 0, write 28, free 28; the held slots stay out of it.
    put(pool,brain_ring_write,7);put(pool,brain_ring_read,11);put(pool,brain_ring_free,28);put(pool,brain_ring_bitmap,0x80000007u);
    for(unsigned i=0;i<32;++i) put(pool,0xc384+4*i,unsigned((i*5+3)%32));
    join_in_progress::canonical_brain_ring();
    assert(*reinterpret_cast<int*>(pool+brain_ring_write)==28 && *reinterpret_cast<int*>(pool+brain_ring_read)==0 && *reinterpret_cast<int*>(pool+brain_ring_free)==28);
    for(unsigned i=0;i<28;++i) assert(*reinterpret_cast<unsigned*>(pool+0xc384+4*i)==i+3);
    assert(*reinterpret_cast<unsigned*>(pool+brain_ring_bitmap)==0x80000007u);
    // The LoadZone bridge (the zone load a mode-2 restore takes), entered by call: an epoch while armed.
    join_in_progress::zone_load_zone_entry_target_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_getter));
    const auto zone_bridge=reinterpret_cast<void*(__cdecl*)()>(&join_in_progress::zone_load_zone_entry_bridge);
    join_in_progress::restart.arm();*seed=0x66666666u;*count=9;
    assert(zone_bridge()==&getter_object && *seed==native_seed && *count==0);
    join_in_progress::late_restart=false;join_in_progress::restart.disarm();
    // latejoin-residue P3: the draw bridge, entered through a CALL from the probe (the same frame shape as the JMP at
    // 0x560090: [esp] = the caller's return address), notes the normalised caller and the count after the displaced
    // increment, then resumes (redirected to a stub returning to the probe); the wrapper returns are normalised.
    join_in_progress::probe_enabled=true;join_in_progress::draws=&fixture_trace;fixture_trace.phase=1; // phase 0 is not recorded (follow-up 4)
    join_in_progress::draw_resume_at=unsigned(reinterpret_cast<std::uintptr_t>(&draw_resume_stub));
    *count=10;*frame=7;
    draw_probe();
    assert(*count==11 && fixture_trace.pending()==1);
    assert(fixture_trace.ring[0].caller==unsigned(reinterpret_cast<std::uintptr_t>(&draw_probe))+6 && fixture_trace.ring[0].count==11 && fixture_trace.ring[0].frame==7);
    assert(fixture_trace.ring[0].a==0 && fixture_trace.ring[0].b==0); // follow-up 6: no actor for an unknown caller
    join_in_progress::flush_draws("fixture");
    assert(fixture_trace.pending()==0);
    join_in_progress::probe_enabled=false;join_in_progress::draws=nullptr;
    // Follow-up 3: the camera anchor table. Not a late restart: the retail controller table. Armed late restart: a copy
    // of the party list (game vt+0x11c, lockstep order) on every peer; the fake game's list is read through the bridge.
    redirect(game_getter,anchor_game_get);anchor_vtable[game_party_list_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&anchor_party));
    join_in_progress::late_restart=false;join_in_progress::restart.disarm();
    party_calls=0; // follow-up 12: the frame-0 epochs above already read the fake party list
    anchor_probe();assert(anchor_ebx==hero_table && party_calls==0);
    join_in_progress::late_restart=true;join_in_progress::restart.arm();
    anchor_probe();
    assert(party_calls==1 && anchor_ebx==unsigned(reinterpret_cast<std::uintptr_t>(join_in_progress::camera_anchor_handles)));
    assert(join_in_progress::camera_anchor_handles[0]==0x3803u && join_in_progress::camera_anchor_handles[1]==0x3801u && join_in_progress::camera_anchor_handles[3]==0x3802u && join_in_progress::camera_anchor_handles[4]==0);
    join_in_progress::late_restart=false;join_in_progress::restart.disarm();
    anchor_probe();assert(anchor_ebx==hero_table && party_calls==1);
    // Follow-up 5: the dormant-entity respawn hold. The site bridge replaces `call 0x4b76f0` (entered by CALL exactly as
    // installed, the getter redirected): outside a late restart it reaches the getter; held, it returns the stand-in
    // whose vt+4 update does nothing and whose other slots are retail's (the mapped image's vtable); the frame-0 epoch
    // releases it and puts the real manager's cursor at 0; the session's end releases it too.
    // (The P1/LoadZone checks above armed holds through the zone-load notices: the frame-0 reseed released the first,
    // the LoadZone bridge's is still on - a session's end releases it; the counts start again from there.)
    assert(join_in_progress::smart_hold);
    join_in_progress::release_smart_hold("fixture: session end after the LoadZone epoch");
    assert(!join_in_progress::smart_hold && *reinterpret_cast<unsigned*>(smart_object+smart_cursor_at)==0);
    smart_getter_calls=0;
    const auto smart_bridge=reinterpret_cast<void*(__cdecl*)()>(&join_in_progress::smart_update_getter_bridge);
    join_in_progress::late_restart=false;join_in_progress::restart.disarm();
    join_in_progress::hold_smart_update();
    assert(!join_in_progress::smart_hold && smart_bridge()==smart_object && smart_getter_calls==1); // not a late restart: the real getter
    join_in_progress::late_restart=true;join_in_progress::restart.arm();
    join_in_progress::hold_smart_update();
    assert(join_in_progress::smart_hold);
    void* held=smart_bridge();
    assert(held==&join_in_progress::smart_stand_in && smart_getter_calls==1 && join_in_progress::smart_holds==1);
    const unsigned* held_vtable=*reinterpret_cast<unsigned**>(held);
    assert(held_vtable[1]==unsigned(reinterpret_cast<std::uintptr_t>(&join_in_progress::smart_update_held)));
    assert(held_vtable[0]==0x4b76d0u && held_vtable[2]==0x4b7000u && held_vtable[3]==0x4b70e0u && held_vtable[5]==0x4b6fb0u && held_vtable[7]==0x4b75c0u);
    reinterpret_cast<void(__thiscall*)(void*,unsigned)>(held_vtable[1])(held,1); // the site's call: nothing happens
    put(smart_object,smart_cursor_at,7u);
    join_in_progress::late_restart_epoch_for_camera=false; // the anchor check above armed a re-anchor; not under test here
    *frame=0;join_in_progress::frame_gate_reseed(); // the frame-0 epoch releases the hold
    assert(!join_in_progress::smart_hold && *reinterpret_cast<unsigned*>(smart_object+smart_cursor_at)==0 && smart_getter_calls==2);
    assert(smart_bridge()==smart_object && smart_getter_calls==3);
    assert(!join_in_progress::late_restart && !join_in_progress::restart.armed); // follow-up 9: the epoch ended the late restart too
    join_in_progress::hold_smart_update();assert(!join_in_progress::smart_hold); // disarmed by the reseed: no second hold this epoch
    join_in_progress::restart.arm();join_in_progress::hold_smart_update();assert(!join_in_progress::smart_hold); // armed alone (no late restart): still none
    join_in_progress::late_restart=true;join_in_progress::restart.arm();join_in_progress::hold_smart_update();assert(join_in_progress::smart_hold); // a new late epoch holds again
    join_in_progress::release_smart_hold("fixture: session end");
    assert(!join_in_progress::smart_hold && smart_bridge()==smart_object);
    join_in_progress::late_restart=false;join_in_progress::restart.disarm();
    // The spawn-entry probe bridge, entered through a CALL from C++ (the same frame as the JMP at 0x4612e0: [esp] = the
    // caller's return, the five cdecl arguments above it); the displaced prologue is undone by the resume stub, so the
    // caller's ecx/ebx/ebp/esi come back as they were. Noted only with the probe on inside a late epoch.
    join_in_progress::spawn_resume_at=unsigned(reinterpret_cast<std::uintptr_t>(&spawn_resume_stub));
    const auto spawn=reinterpret_cast<void(__cdecl*)(const char*,const char*,const float*,const float*,unsigned)>(&join_in_progress::spawn_entry_bridge);
    const float where[3]={1.5f,2.5f,-3.5f};
    join_in_progress::probe_enabled=false;join_in_progress::probe_epoch_late=false;join_in_progress::spawn_notes=0;
    spawn("def_off","inst_off",where,nullptr,0);
    assert(join_in_progress::spawn_notes==0);
    join_in_progress::probe_enabled=true;join_in_progress::probe_epoch_late=true;
    spawn("def_a","inst_b",where,nullptr,0);
    assert(join_in_progress::spawn_notes==1 && join_in_progress::last_spawn.caller!=0);
    assert(std::strcmp(join_in_progress::last_spawn.definition,"def_a")==0 && std::strcmp(join_in_progress::last_spawn.instance,"inst_b")==0);
    assert(join_in_progress::last_spawn.position[0]==1.5f && join_in_progress::last_spawn.position[1]==2.5f && join_in_progress::last_spawn.position[2]==-3.5f);
    join_in_progress::probe_enabled=false;join_in_progress::probe_epoch_late=false;
    // Follow-up 7: the timed-event queue rebuilt in canonical order on a fixture-owned manager image (the retail
    // vtable value at +0, the body at +4) through the mapped retail pool init / record alloc / store / heap push, the
    // owner resolver redirected to a table of fake entities. Owners: 0x1801 (slot 1, a cold joiner's generation) and
    // 0x3802 (slot 2) alive, 0x35ce and 0x35de dead (a warm host's old world), 0 a non-token owner (kept).
    {
        using namespace join_in_progress_rules;
        std::memset(event_manager_image,0,sizeof event_manager_image);
        put(event_manager_image,0,event_manager_vtable);
        unsigned char* body=event_manager_image+event_body_at;
        reinterpret_cast<void(__thiscall*)(void*)>(static_cast<std::uintptr_t>(event_pool_init))(body);
        assert(*reinterpret_cast<unsigned*>(body+event_ring_count_at)==event_records && *reinterpret_cast<unsigned*>(body+event_ring_read_at)==0);
        join_in_progress::event_manager_at=unsigned(reinterpret_cast<std::uintptr_t>(event_manager_image));
        join_in_progress::entity_of_handle_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_entity_of));
        const auto add=[&](float time,unsigned handle,unsigned event,unsigned char tag) {
            const auto index=reinterpret_cast<unsigned(__thiscall*)(void*)>(static_cast<std::uintptr_t>(event_record_alloc))(body);
            unsigned char record[event_record_bytes]{};std::memcpy(record,&handle,4);std::memcpy(record+4,&event,4);record[8]=tag;
            reinterpret_cast<void(__thiscall*)(void*,unsigned,const void*)>(static_cast<std::uintptr_t>(event_record_store))(body,index,record);
            struct { float time;unsigned index; } pair{time,index};
            reinterpret_cast<void(__thiscall*)(void*,const void*)>(static_cast<std::uintptr_t>(event_heap_push))(body+event_heap_at,&pair);
        };
        const unsigned e1=unsigned(reinterpret_cast<std::uintptr_t>(fake_entities[1])),e2=unsigned(reinterpret_cast<std::uintptr_t>(fake_entities[2]));
        add(2.0f,0x3802,e2+0x320,0);add(2.0f,0x35ce,0x12345678,0);add(2.0f,0x1801,e1+0x328,0);add(64.0f,0x35de,0x23456789,0);
        add(2.0f,0x1801,e1+0x320,0);add(1.5f,0x1801,e1+0x330,0);add(2.0f,0,0x6d55fc,0);add(2.0f,0x1801,e1+0x320,7);
        assert(*reinterpret_cast<unsigned*>(body+event_heap_count_at)==8 && *reinterpret_cast<unsigned*>(body+event_record_count_at)==8);
        unsigned char before[event_body_bytes];std::memcpy(before,body,sizeof before);
        join_in_progress::late_restart=false;join_in_progress::restart.disarm();
        join_in_progress::canonical_event_queue("fixture: not a late restart");
        assert(join_in_progress::event_rebuilds==0 && std::memcmp(before,body,sizeof before)==0); // untouched
        join_in_progress::late_restart=true;join_in_progress::restart.arm();
        join_in_progress::canonical_event_queue("fixture: the first shared frame");
        assert(join_in_progress::event_rebuilds==1 && join_in_progress::event_last_live==6 && join_in_progress::event_last_dead==2);
        assert(*reinterpret_cast<unsigned*>(body+event_heap_count_at)==6 && *reinterpret_cast<unsigned*>(body+event_record_count_at)==6);
        assert(*reinterpret_cast<unsigned*>(body+event_ring_read_at)==6 && *reinterpret_cast<unsigned*>(body+event_ring_count_at)==event_records-6);
        assert(*reinterpret_cast<unsigned*>(body+event_used_bitmap_at)==0x3f && *reinterpret_cast<unsigned*>(body+event_alloc_bitmap_at)==0x3f);
        // An ascending push sequence needs no sift: the heap array IS the canonical sequence, records 0..5 in that order.
        const float times[6]={1.5f,2.0f,2.0f,2.0f,2.0f,2.0f};const unsigned handles[6]={0x1801,0x1801,0x1801,0x1801,0x3802,0};
        const unsigned events[6]={e1+0x330,e1+0x320,e1+0x320,e1+0x328,e2+0x320,0x6d55fc};const unsigned char tags[6]={0,0,7,0,0,0};
        for(unsigned i=0;i<6;++i) {
            const unsigned char* pair=body+event_heap_at+i*8;float t;unsigned index;std::memcpy(&t,pair,4);std::memcpy(&index,pair+4,4);
            assert(t==times[i] && index==i);
            const unsigned char* record=body+index*event_record_bytes;unsigned handle,event;std::memcpy(&handle,record,4);std::memcpy(&event,record+4,4);
            assert(handle==handles[i] && event==events[i] && record[8]==tags[i]);
        }
        // A second rebuild is byte-identical, and so is a rebuild of the same entries pushed in another order (the
        // claim: the queue no longer depends on the process's push/pop history). Then the retail pops drain it with
        // non-decreasing times over a permutation of the six records (ties pop in heap-shape order, now the same shape).
        // The queue's observable state: the heap pairs within the count, the records they reference, both counts,
        // the ring and both bitmaps (record and heap slots beyond the counts keep whatever a history left: the retail
        // pool init and alloc never clear them, store copies all 24 bytes and push the whole pair).
        const auto view=[&](std::vector<unsigned char>& v) {
            v.clear();const auto push=[&](const void* p,std::size_t k) { const auto* b=static_cast<const unsigned char*>(p);v.insert(v.end(),b,b+k); };
            const unsigned n=*reinterpret_cast<unsigned*>(body+event_heap_count_at);push(body+event_heap_at,n*8);
            for(unsigned i=0;i<n;++i) { unsigned idx;std::memcpy(&idx,body+event_heap_at+i*8+4,4);push(body+idx*event_record_bytes,event_record_bytes); }
            push(body+event_heap_count_at,4);push(body+event_record_count_at,4);push(body+event_ring_write_at,12);push(body+event_used_bitmap_at,0x24*4);push(body+event_alloc_bitmap_at,0x24*4);
        };
        std::vector<unsigned char> after,now;view(after);
        unsigned char after_bytes[event_body_bytes];std::memcpy(after_bytes,body,sizeof after_bytes);
        join_in_progress::restart.arm();join_in_progress::canonical_event_queue("fixture: again");
        assert(join_in_progress::event_rebuilds==2 && std::memcmp(after_bytes,body,sizeof after_bytes)==0); // idempotent, byte for byte
        reinterpret_cast<void(__thiscall*)(void*)>(static_cast<std::uintptr_t>(event_pool_init))(body);
        std::memset(body+event_used_bitmap_at,0,0x24*4);std::memset(body+event_alloc_bitmap_at,0,0x24*4);
        *reinterpret_cast<unsigned*>(body+event_record_count_at)=0;*reinterpret_cast<unsigned*>(body+event_heap_count_at)=0;
        add(2.0f,0x1801,e1+0x320,7);add(2.0f,0,0x6d55fc,0);add(1.5f,0x1801,e1+0x330,0);add(2.0f,0x1801,e1+0x320,0);
        add(64.0f,0x35de,0x23456789,0);add(2.0f,0x1801,e1+0x328,0);add(2.0f,0x35ce,0x12345678,0);add(2.0f,0x3802,e2+0x320,0);
        view(now);assert(now!=after); // a different history: a different queue before the rebuild
        join_in_progress::restart.arm();join_in_progress::canonical_event_queue("fixture: another history");
        view(now);assert(join_in_progress::event_rebuilds==3 && now==after); // the same queue after it
        unsigned char* heap=body+event_heap_at;unsigned seen=0;float last=0.0f;
        for(unsigned i=0;i<6;++i) {
            unsigned index;float t;std::memcpy(&t,heap,4);std::memcpy(&index,heap+4,4);
            assert(index<6 && !(seen&(1u<<index)) && t>=last);seen|=1u<<index;last=t;
            reinterpret_cast<void(__thiscall*)(void*)>(static_cast<std::uintptr_t>(event_heap_pop))(heap);
        }
        assert(seen==0x3f && *reinterpret_cast<unsigned*>(body+event_heap_count_at)==0);
        // Follow-up 9 (round 6): the frame-0 epoch itself rebuilds the queue LAST (it never did: `armed` was consumed by
        // should_reseed before the rebuild asked for it) and ends the late restart on this peer; `cohort synchronized`
        // keeps the flag while the epoch is still armed. Entered through the production frame_gate_reseed with the
        // fixture-owned manager image: 3 entries (one dead owner) -> 2 kept, rebuilds 3 -> 4, seed reset, flags cleared.
        {
            reinterpret_cast<void(__thiscall*)(void*)>(static_cast<std::uintptr_t>(event_pool_init))(body);
            std::memset(body+event_used_bitmap_at,0,0x24*4);std::memset(body+event_alloc_bitmap_at,0,0x24*4);
            *reinterpret_cast<unsigned*>(body+event_record_count_at)=0;*reinterpret_cast<unsigned*>(body+event_heap_count_at)=0;
            add(2.0f,0x3802,e2+0x320,0);add(2.0f,0x35ce,0x12345678,0);add(2.0f,0x1801,0x6d7c5c,0);
            auto* frame9=reinterpret_cast<unsigned short*>(native_frame);auto* seed9=reinterpret_cast<unsigned*>(rng_seed);auto* count9=reinterpret_cast<unsigned*>(rng_count);
            join_in_progress::late_restart=true;join_in_progress::restart.arm();*frame9=0;*seed9=0x33333333u;*count9=11;
            assert(!join_in_progress_core::cohort_sync_clears_late_restart(join_in_progress::restart.armed)); // a cohort sync now would keep the flag
            join_in_progress::frame_gate_reseed();
            assert(join_in_progress::event_rebuilds==4 && join_in_progress::event_last_live==2 && join_in_progress::event_last_dead==1);
            assert(*reinterpret_cast<unsigned*>(body+event_heap_count_at)==2 && *reinterpret_cast<unsigned*>(body+event_record_count_at)==2);
            assert(*seed9==native_seed && *count9==0 && !join_in_progress::restart.armed && !join_in_progress::late_restart); // the epoch ended the late restart
            assert(join_in_progress_core::cohort_sync_clears_late_restart(join_in_progress::restart.armed)); // and a later cohort sync clears as before
            unsigned char* pair0=body+event_heap_at;unsigned i0,i1;std::memcpy(&i0,pair0+4,4);std::memcpy(&i1,pair0+12,4);
            unsigned h0,h1,k0,k1;std::memcpy(&h0,body+i0*event_record_bytes,4);std::memcpy(&k0,body+i0*event_record_bytes+4,4);std::memcpy(&h1,body+i1*event_record_bytes,4);std::memcpy(&k1,body+i1*event_record_bytes+4,4);
            assert(h0==0x1801 && k0==0x6d7c5c && h1==0x3802 && k1==e2+0x320); // dense index 1 (the static kind) before index 2 (a member)
            join_in_progress::frame_gate_reseed(); // not armed any more: nothing
            assert(join_in_progress::event_rebuilds==4 && *seed9==native_seed);
        }
        join_in_progress::event_manager_at=event_manager_object;join_in_progress::entity_of_handle_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_entity_of)); // follow-up 12: the fake resolver stays (later frame-0 epochs walk the party)
        join_in_progress::late_restart=false;join_in_progress::restart.disarm();
    }
    // Follow-up 11: the world hold. The bridge entered by CALL exactly as installed: hold off, every flag as retail's
    // test leaves it for every bit pattern; hold on, ZF=1 whatever the byte, no register changed, the skip counted.
    // The window opens only through the production milestone of an ARMED late restart once the epoch saw the world
    // loading and the loading flags are clear (`zone started`), idempotently; it closes first thing in the frame-0
    // epoch (the production frame_gate_reseed: the release line precedes the RNG epoch line, the skip count logged),
    // at a session's end (the production pump path with no first record) and by the watchdog exactly at its limit
    // (the clock stubbed); a lobby START (never a late restart), [Dev] LateJoinWorldHold=0 and
    // [Dev] LateJoinRestartReset=0 never open it.
    {
        join_in_progress::world_hold_clock=&world_fake_clock;world_clock_now=100000;
        join_in_progress::restart_reset_enabled=true;join_in_progress::world_hold_enabled=true;join_in_progress::world_hold=0;
        join_in_progress::late_restart=false;join_in_progress::restart.disarm();join_in_progress::epoch.active=false;
        check_world_gate(0x00,true);check_world_gate(0x02,false);check_world_gate(0xfd,true);check_world_gate(0xff,false);
        assert(join_in_progress::world_hold_skips==0);
        // a lobby START's epoch (never a late restart): the milestone never opens the hold
        join_in_progress::epoch.begin();join_in_progress::epoch.start();join_in_progress::epoch.observe(true,false);
        join_in_progress::world_hold_milestone(false);assert(!join_in_progress::world_hold);
        // an armed late restart: closed while the world loads, open at `zone started`, idempotent; the bridge skips
        join_in_progress::late_restart=true;join_in_progress::restart.arm();
        join_in_progress::world_hold_milestone(true);assert(!join_in_progress::world_hold);
        const auto opened_at=observed_jip_logs.size();
        join_in_progress::world_hold_milestone(false);
        assert(join_in_progress::world_hold==1 && join_in_progress::world_hold_skips==0 && join_in_progress::world_holds==1);
        assert(observed_jip_logs.size()==opened_at+1 && observed_jip_logs.back().find("is held from the zone start until the first shared frame on every peer")!=std::string::npos);
        check_world_gate(0x02,true);check_world_gate(0xff,true);check_world_gate(0x00,true);
        assert(join_in_progress::world_hold_skips==3);
        join_in_progress::world_hold_milestone(false);assert(join_in_progress::world_hold==1 && join_in_progress::world_hold_skips==3 && join_in_progress::world_holds==1);
        world_clock_now+=world_hold_watchdog_ms-1;join_in_progress::world_hold_watchdog();assert(join_in_progress::world_hold==1); // not before the limit
        // the frame-0 epoch through the production frame_gate_reseed: the release is its first step (the line precedes
        // the RNG epoch line), the skip count is logged, the bridge is retail's test again
        *frame=0;*seed=0x77777777u;*count=3;join_in_progress::late_restart_epoch_for_camera=false;
        const auto epoch_at=observed_jip_logs.size();
        join_in_progress::frame_gate_reseed();
        assert(!join_in_progress::world_hold && *seed==native_seed && *count==0 && !join_in_progress::restart.armed && !join_in_progress::late_restart);
        std::size_t release_line=0,rng_line=0;
        for(std::size_t i=epoch_at;i<observed_jip_logs.size();++i) {
            if(observed_jip_logs[i].find("the world update resumes at the first shared frame (3 world update(s) skipped on this PC while held)")!=std::string::npos) release_line=i+1;
            if(observed_jip_logs[i].find("synchronized RNG epoch reset")!=std::string::npos && !rng_line) rng_line=i+1;
        }
        assert(release_line && rng_line && release_line<rng_line);
        check_world_gate(0x02,false);check_world_gate(0x00,true);
        join_in_progress::frame_gate_reseed();assert(!join_in_progress::world_hold); // not armed: nothing
        // the watchdog releases a hold whose frame 0 never came, exactly at its limit, and names itself
        join_in_progress::late_restart=true;join_in_progress::restart.arm();join_in_progress::epoch.begin();join_in_progress::epoch.start();join_in_progress::epoch.observe(true,false);
        join_in_progress::world_hold_milestone(false);assert(join_in_progress::world_hold==1 && join_in_progress::world_holds==2);
        check_world_gate(0x02,true);
        world_clock_now+=world_hold_watchdog_ms-1;join_in_progress::world_hold_watchdog();assert(join_in_progress::world_hold==1);
        world_clock_now+=1;join_in_progress::world_hold_watchdog();
        assert(!join_in_progress::world_hold && observed_jip_logs.back().find("resumes at the watchdog")!=std::string::npos && observed_jip_logs.back().find("(1 world update(s) skipped")!=std::string::npos);
        check_world_gate(0x02,false);
        join_in_progress::world_hold_watchdog();assert(!join_in_progress::world_hold); // closed: nothing
        // a session's end releases it through the production pump path (no first record): the seat machine, the arm,
        // the late restart and the hold all go
        join_in_progress::world_hold_milestone(false);assert(join_in_progress::world_hold==1 && join_in_progress::world_holds==3); // still armed: a new window
        machine.started=true;put(s,0x26c,static_cast<void*>(nullptr));
        assert(!join_in_progress::guarded_advance(false,nullptr));
        assert(!join_in_progress::world_hold && !machine.started && !join_in_progress::restart.armed && !join_in_progress::late_restart);
        assert(observed_jip_logs.back().find("resumes at the session's end")!=std::string::npos);
        put(s,0x26c,nodes[0]);check_world_gate(0x02,false);
        // [Dev] LateJoinWorldHold=0 and [Dev] LateJoinRestartReset=0: the window never opens, the bridge stays retail
        join_in_progress::late_restart=true;join_in_progress::restart.arm();join_in_progress::epoch.begin();join_in_progress::epoch.start();join_in_progress::epoch.observe(true,false);
        join_in_progress::world_hold_enabled=false;join_in_progress::world_hold_milestone(false);assert(!join_in_progress::world_hold);
        join_in_progress::world_hold_enabled=true;join_in_progress::restart_reset_enabled=false;join_in_progress::world_hold_milestone(false);assert(!join_in_progress::world_hold);
        join_in_progress::restart_reset_enabled=true;check_world_gate(0x02,false);
        assert(join_in_progress::world_holds==3);
        join_in_progress::late_restart=false;join_in_progress::restart.disarm();join_in_progress::epoch.active=false;
        join_in_progress::world_hold_clock=&GetTickCount;
    }
    // Follow-up 12: the party heroes' moved-check state canonical at the first shared frame, then the rebuild drops
    // their queued "moved" events. Party = {0x3802} (fake entity slot 2); 0x1801 (slot 1) is a live non-party owner.
    {
        using namespace join_in_progress_rules;
        std::memset(event_manager_image,0,sizeof event_manager_image);put(event_manager_image,0,event_manager_vtable);
        unsigned char* body=event_manager_image+event_body_at;
        reinterpret_cast<void(__thiscall*)(void*)>(static_cast<std::uintptr_t>(event_pool_init))(body);
        join_in_progress::event_manager_at=unsigned(reinterpret_cast<std::uintptr_t>(event_manager_image));
        join_in_progress::entity_of_handle_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_entity_of));
        anchor_vtable[game_party_list_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&moved_party)); // game vt+0x11c -> {0x3802}
        const auto add=[&](float time,unsigned handle,unsigned event) {
            const auto index=reinterpret_cast<unsigned(__thiscall*)(void*)>(static_cast<std::uintptr_t>(event_record_alloc))(body);
            unsigned char record[event_record_bytes]{};std::memcpy(record,&handle,4);std::memcpy(record+4,&event,4);
            reinterpret_cast<void(__thiscall*)(void*,unsigned,const void*)>(static_cast<std::uintptr_t>(event_record_store))(body,index,record);
            struct { float time;unsigned index; } pair{time,index};
            reinterpret_cast<void(__thiscall*)(void*,const void*)>(static_cast<std::uintptr_t>(event_heap_push))(body+event_heap_at,&pair);
        };
        unsigned char* w=fake_entities[2];unsigned char* c=fake_entities[1];
        const float pos[3]={2003.f,1741.f,1.01f},ori[3]={0.f,0.f,4.71f},old[3]={1.f,2.f,3.f};
        std::memcpy(w+actor_position_at,pos,12);std::memcpy(w+actor_orientation_at,ori,12);std::memcpy(w+actor_last_position_at,old,12);std::memcpy(w+actor_last_orientation_at,old,12);w[actor_moved_flags_at]=0x0a;
        std::memcpy(c+actor_position_at,pos,12);std::memcpy(c+actor_orientation_at,ori,12);std::memcpy(c+actor_last_position_at,old,12);std::memcpy(c+actor_last_orientation_at,old,12);c[actor_moved_flags_at]=0x0a;
        const unsigned e1=unsigned(reinterpret_cast<std::uintptr_t>(c)),e2=unsigned(reinterpret_cast<std::uintptr_t>(w));
        add(2.0f,0x3802,moved_event_kind);add(2.0f,0x3802,e2+0x320);add(2.0f,0x1801,moved_event_kind);add(2.0f,0x35ce,moved_event_kind);add(2.0f,0,0x6d55fc);add(2.0f,0x1801,e1+0x328);
        // off ([Dev] LateJoinMovedCanonical=0 or the reset off): nothing touched, nothing marked for the rebuild
        join_in_progress::canonical_party_moved_state("fixture: off",false);
        assert(join_in_progress::moved_drop_count==0 && w[actor_moved_flags_at]==0x0a && std::memcmp(w+actor_last_position_at,old,12)==0);
        // on: the party hero's last-known = current, its pending bit cleared (other bits kept), the non-party entity untouched
        join_in_progress::canonical_party_moved_state("fixture: the first shared frame",true);
        assert(join_in_progress::moved_drop_count==1 && join_in_progress::moved_drop_handles[0]==0x3802u);
        assert(w[actor_moved_flags_at]==0x08 && std::memcmp(w+actor_last_position_at,pos,12)==0 && std::memcmp(w+actor_last_orientation_at,ori,12)==0);
        assert(c[actor_moved_flags_at]==0x0a && std::memcmp(c+actor_last_position_at,old,12)==0);
        // the rebuild: the party hero's moved event and the dead owner's go; the non-party moved, the member events and the non-token owner stay
        join_in_progress::late_restart=true;join_in_progress::restart.arm();
        const auto rebuilds_before=join_in_progress::event_rebuilds;
        join_in_progress::canonical_event_queue("fixture: the first shared frame");
        assert(join_in_progress::event_rebuilds==rebuilds_before+1 && join_in_progress::moved_dropped_last==1 && join_in_progress::moved_drop_count==0);
        assert(*reinterpret_cast<unsigned*>(body+event_heap_count_at)==4 && *reinterpret_cast<unsigned*>(body+event_record_count_at)==4);
        unsigned moved_kept=0;
        for(unsigned i=0;i<4;++i) {
            unsigned index,handle,event;std::memcpy(&index,body+event_heap_at+i*8+4,4);std::memcpy(&handle,body+index*event_record_bytes,4);std::memcpy(&event,body+index*event_record_bytes+4,4);
            assert(!(handle==0x3802u && event==moved_event_kind) && handle!=0x35ceu);
            if(event==moved_event_kind) ++moved_kept;
        }
        assert(moved_kept==1); // 0x1801's (non-party, live); the non-token owner's entry is another static kind (0x6d55fc) and stays too
        // a rebuild with no canonicalisation before it drops no moved event
        join_in_progress::restart.arm();join_in_progress::canonical_event_queue("fixture: again");
        assert(join_in_progress::moved_dropped_last==0 && *reinterpret_cast<unsigned*>(body+event_heap_count_at)==4);
        anchor_vtable[game_party_list_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&anchor_party));
        join_in_progress::event_manager_at=event_manager_object;join_in_progress::entity_of_handle_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_entity_of)); // follow-up 12: the fake resolver stays (later frame-0 epochs walk the party)
        join_in_progress::late_restart=false;join_in_progress::restart.disarm();
    }
    std::cout<<"PASS canonical restart: construction image taken through the bridge, restored only for a late-join cohort restart (lobby START epoch and foreign vtable refused), retail 0x560150 reseeds the synchronized stream once at frame 0 through the frame-gate bridge, 0x47 stamp bridge entered by JUMP writes opcode 0x47 and tail 0/marker with no other byte touched, a given-up added seat is dropped natively (codes 0x10ce/0x10cf) and a pending one is not; the world-gate bridge entered by CALL returns retail's flags with the hold off and ZF=1 held, the hold opens at zone started of an armed late restart only and closes first at frame 0, at a session's end and by the watchdog\n";
}
// Follow-up 8 (live round 5): xml1_turrets installs its scheduler dispatch hook at the timed-event manager's pump slot
// (vt+0xc, 0x68de20) BEFORE join_in_progress; a 16-byte vtable guard refused the whole install. This mode installs a
// stand-in of that hook first (the mapped vtable slot := a dispatcher that counts and tail-calls retail 0x4b2d60, the
// module's accessor reporting it), asserts the install check accepts it (and refuses a foreign pointer), rebuilds a
// queue canonically and drains it THROUGH THE HOOKED PUMP (the retail pump fires, frees and pops every record).
unsigned fixture_hook_pumps=0;
void __fastcall fixture_pump(void* self,void* edx,float time) { ++fixture_hook_pumps;reinterpret_cast<void(__fastcall*)(void*,void*,float)>(static_cast<std::uintptr_t>(join_in_progress_rules::event_manager_pump))(self,edx,time); }
void* __fastcall null_resolver(const unsigned*,void*) { return nullptr; }
void run_turrets() {
    using namespace join_in_progress_rules;
    auto* slot=reinterpret_cast<unsigned*>(static_cast<std::uintptr_t>(event_manager_pump_slot));
    assert(*slot==event_manager_pump && join_in_progress_rules::first_mismatch(reinterpret_cast<const std::uint8_t*>(0x400000))==nullptr);
    // retail slot, no module hook: accepted
    xml1_turrets::fixture_hook=0;assert(join_in_progress::event_pump_slot_ok());
    // the turrets hook installed first (its install writes the slot and reports the pointer): accepted; the guards still match
    const unsigned hook=unsigned(reinterpret_cast<std::uintptr_t>(&fixture_pump));
    *slot=hook;xml1_turrets::fixture_hook=hook;
    assert(join_in_progress::event_pump_slot_ok() && join_in_progress_rules::first_mismatch(reinterpret_cast<const std::uint8_t*>(0x400000))==nullptr);
    // a foreign pointer in the slot (or the module reporting nothing while the slot is hooked): refused
    xml1_turrets::fixture_hook=0;assert(!join_in_progress::event_pump_slot_ok());
    *slot=0x12345678u;xml1_turrets::fixture_hook=hook;assert(!join_in_progress::event_pump_slot_ok());
    *slot=hook;assert(join_in_progress::event_pump_slot_ok());
    // a queue rebuilt canonically, then drained through the hooked pump: the dispatcher runs once, retail fires
    // (owner resolver redirected to "gone": no-op fires), frees and pops everything.
    std::memset(event_manager_image,0,sizeof event_manager_image);put(event_manager_image,0,event_manager_vtable);
    unsigned char* body=event_manager_image+event_body_at;
    reinterpret_cast<void(__thiscall*)(void*)>(static_cast<std::uintptr_t>(event_pool_init))(body);
    join_in_progress::event_manager_at=unsigned(reinterpret_cast<std::uintptr_t>(event_manager_image));
    join_in_progress::entity_of_handle_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_entity_of));
    const auto add=[&](float time,unsigned handle,unsigned event) {
        const auto index=reinterpret_cast<unsigned(__thiscall*)(void*)>(static_cast<std::uintptr_t>(event_record_alloc))(body);
        unsigned char record[event_record_bytes]{};std::memcpy(record,&handle,4);std::memcpy(record+4,&event,4);
        reinterpret_cast<void(__thiscall*)(void*,unsigned,const void*)>(static_cast<std::uintptr_t>(event_record_store))(body,index,record);
        struct { float time;unsigned index; } pair{time,index};
        reinterpret_cast<void(__thiscall*)(void*,const void*)>(static_cast<std::uintptr_t>(event_heap_push))(body+event_heap_at,&pair);
    };
    const unsigned e1=unsigned(reinterpret_cast<std::uintptr_t>(fake_entities[1]));
    add(2.0f,0x1801,e1+0x320);add(2.0f,0x35ce,0x12345678);add(1.5f,0x1801,e1+0x328);add(2.0f,0x3802,unsigned(reinterpret_cast<std::uintptr_t>(fake_entities[2]))+0x320);
    join_in_progress::late_restart=true;join_in_progress::restart.arm();join_in_progress::event_rebuilds=0;
    join_in_progress::canonical_event_queue("fixture: turrets");
    assert(join_in_progress::event_rebuilds==1 && *reinterpret_cast<unsigned*>(body+event_heap_count_at)==3 && *reinterpret_cast<unsigned*>(body+event_record_count_at)==3);
    redirect(entity_of_handle,null_resolver); // retail 0x4199f0 resolves the owner through 0x4654b0: every fire is a no-op here
    const auto pump=reinterpret_cast<void(__fastcall*)(void*,void*,float)>(static_cast<std::uintptr_t>(*slot));
    pump(event_manager_image,nullptr,100.0f);
    assert(fixture_hook_pumps==1 && *reinterpret_cast<unsigned*>(body+event_heap_count_at)==0 && *reinterpret_cast<unsigned*>(body+event_record_count_at)==0);
    assert(*reinterpret_cast<unsigned*>(body+event_ring_count_at)==event_records && *reinterpret_cast<unsigned*>(body+event_used_bitmap_at)==0);
    pump(event_manager_image,nullptr,100.0f);assert(fixture_hook_pumps==2 && *reinterpret_cast<unsigned*>(body+event_heap_count_at)==0); // empty: nothing to pop
    *slot=event_manager_pump;xml1_turrets::fixture_hook=0;join_in_progress::late_restart=false;join_in_progress::restart.disarm();
    join_in_progress::event_manager_at=event_manager_object;join_in_progress::entity_of_handle_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_entity_of)); // follow-up 12: the fake resolver stays (later frame-0 epochs walk the party)
    std::cout<<"PASS turrets composition: the xml1 turrets dispatch hook at the timed-event manager's pump slot is accepted by the JoinInProgress install check (retail accepted, a foreign pointer and an unreported hook refused), the retail guards still match, and a canonically rebuilt queue drains through the hooked pump (dispatcher ran, retail fired/freed/popped every record)\n";
}
}
