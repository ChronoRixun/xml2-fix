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
void run() {
    using namespace join_in_progress_rules;
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
    // latejoin-residue P2: the AI brain free ring on a synthetic stock pool: used ring -> identity through retail
    // 0x522f70 when every brain is free; left alone with one brain allocated.
    auto* pool=reinterpret_cast<unsigned char*>(brain_pool_stock);
    put(pool,brain_ring_write,5);put(pool,brain_ring_read,5);put(pool,brain_ring_free,32);
    for(unsigned i=0;i<32;++i) put(pool,0xc384+4*i,unsigned((i*7)%32));
    join_in_progress::late_restart=true;join_in_progress::restart.arm();
    join_in_progress::canonical_brain_ring();
    assert(*reinterpret_cast<int*>(pool+brain_ring_write)==0 && *reinterpret_cast<int*>(pool+brain_ring_read)==0 && *reinterpret_cast<int*>(pool+brain_ring_free)==32);
    for(unsigned i=0;i<32;++i) assert(*reinterpret_cast<unsigned*>(pool+0xc384+4*i)==i);
    put(pool,brain_ring_write,3);put(pool,brain_ring_read,4);put(pool,brain_ring_free,31);put(pool,0xc384,9u);
    join_in_progress::canonical_brain_ring();
    assert(*reinterpret_cast<int*>(pool+brain_ring_read)==4 && *reinterpret_cast<unsigned*>(pool+0xc384)==9u); // one brain allocated: untouched
    join_in_progress::late_restart=false;join_in_progress::restart.disarm();
    // latejoin-residue P3: the draw bridge, entered through a CALL from the probe (the same frame shape as the JMP at
    // 0x560090: [esp] = the caller's return address), notes the normalised caller and the count after the displaced
    // increment, then resumes (redirected to a stub returning to the probe); the wrapper returns are normalised.
    join_in_progress::probe_enabled=true;join_in_progress::draws=&fixture_trace;
    join_in_progress::draw_resume_at=unsigned(reinterpret_cast<std::uintptr_t>(&draw_resume_stub));
    *count=10;*frame=7;
    draw_probe();
    assert(*count==11 && fixture_trace.pending()==1);
    assert(fixture_trace.ring[0].caller==unsigned(reinterpret_cast<std::uintptr_t>(&draw_probe))+6 && fixture_trace.ring[0].count==11 && fixture_trace.ring[0].frame==7);
    join_in_progress::flush_draws("fixture");
    assert(fixture_trace.pending()==0);
    join_in_progress::probe_enabled=false;join_in_progress::draws=nullptr;
    std::cout<<"PASS canonical restart: construction image taken through the bridge, restored only for a late-join cohort restart (lobby START epoch and foreign vtable refused), retail 0x560150 reseeds the synchronized stream once at frame 0 through the frame-gate bridge, 0x47 stamp bridge entered by JUMP writes opcode 0x47 and tail 0/marker with no other byte touched, a given-up added seat is dropped natively (codes 0x10ce/0x10cf) and a pending one is not\n";
}
}
