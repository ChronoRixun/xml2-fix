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
alignas(16) unsigned char fake_entity_storage[4][0x900]{};
unsigned char* fake_entities[4]={fake_entity_storage[0],fake_entity_storage[1],fake_entity_storage[2],fake_entity_storage[3]};
void* __fastcall fake_entity_of(const unsigned* handle,void*) { const unsigned slot=*handle&0xff;return (*handle==0x1801u||*handle==0x3802u)?fake_entities[slot]:nullptr; }
// p060fix/AK: a fake world behind the redirected getter: vt+0x10 (entity by dense index) answers slots 1 and 2 only.
unsigned fake_world_vtable[0x48/4]{};alignas(16) unsigned char fake_world_object[0xc000]{};unsigned fake_world_walks=0;
void* __fastcall fake_world_entity_at(void*,void*,unsigned index) { ++fake_world_walks;return (index==1||index==2)?fake_entities[index]:nullptr; }
void* __cdecl fake_world_get() { return fake_world_object; }
unsigned fake_visibility_grid_vtable[0x44/4]{};unsigned char fake_visibility_grid[0x10900]{};
void* __cdecl fake_visibility_grid_get() { return fake_visibility_grid; }
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
// Follow-up 14: the CActor OnAttached override bridge (installed as a JMP at 0x429710: [esp] = the caller's return,
// [esp+4] = the child, ecx = this) is entered by a thiscall CALL - the same frame shape - with the resume redirected
// to a stub that undoes the displaced prologue; FindGround is redirected to a counter; the party-binding tail bridge
// (a CALL over `call 0x495350`) has its getter redirected to the counting fake getter.
unsigned override_runs=0,find_ground_calls=0;void* find_ground_order[4]{};
__declspec(naked) void attach_resume_stub() { __asm { inc dword ptr [override_runs]
                                                      pop esi
                                                      add esp, 18h
                                                      ret 4 } }
char __fastcall fake_find_ground(void* self,void*) { if(find_ground_calls<4) find_ground_order[find_ground_calls]=self;++find_ground_calls;return 1; }
unsigned attach_eax=0,attach_regs[6]{};
const void* attach_bridge_target=reinterpret_cast<const void*>(&join_in_progress::attach_override_bridge);
// ecx = the parent (fake entity 1), the child pushed; EAX/EBX/EDX/ESI/EDI/EBP known and captured after the bridge.
__declspec(naked) void attach_probe(void* parent,void* child) {
    __asm {
        push ebx
        push esi
        push edi
        push ebp
        mov ecx, dword ptr [esp+14h]
        mov eax, dword ptr [esp+18h]
        push eax
        mov eax, 11111111h
        mov ebx, 22222222h
        mov edx, 44444444h
        mov esi, 55555555h
        mov edi, 66666666h
        mov ebp, 77777777h
        call dword ptr [attach_bridge_target]
        mov dword ptr [attach_eax], eax
        mov dword ptr [attach_regs], ebx
        mov dword ptr [attach_regs+4], edx
        mov dword ptr [attach_regs+8], esi
        mov dword ptr [attach_regs+12], edi
        mov dword ptr [attach_regs+16], ebp
        pop ebp
        pop edi
        pop esi
        pop ebx
        ret
    }
}
#include "join_in_progress_audit.inl"
#include "join_in_progress_visibility.inl"
#include "join_in_progress_hold.inl"
void run() {
    using namespace join_in_progress_rules;
    // Follow-up 12: every frame-0 epoch below now walks the party list (game vt+0x11c) and resolves its heroes; the
    // fake game (party 0x3803/0x3801/0x3804/0x3802) and the fake resolver (0x1801 and 0x3802 alive) stand in from the
    // start, never the retail getter/resolver on the mapped image.
    redirect(game_getter,anchor_game_get);anchor_vtable[game_party_list_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&anchor_party));
    join_in_progress::entity_of_handle_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_entity_of));
    join_in_progress::find_ground_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_find_ground)); // follow-up 14: every frame-0 epoch re-probes the fake party's ground
    // p060fix/AK: every frame-0 epoch walks the world through the getter's vt+0x10; the fake world (slots 1 and 2) stands in from the start, never the retail getter
    fake_world_vtable[world_entity_by_index_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&fake_world_entity_at));
    put(fake_world_object,0,unsigned(reinterpret_cast<std::uintptr_t>(fake_world_vtable)));
    // Execute the actual mapped retail cache/bitmap update and getters; never a model of that update.
    fake_world_vtable[world_flag_update_slot/4]=0x4c6990;
    fake_world_vtable[world_flag_members_slot/4]=0x4c6950;
    fake_world_vtable[world_flag_record_slot/4]=0x4c6970;
    join_in_progress::world_getter_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_world_get));
    put(fake_visibility_grid,0,unsigned(reinterpret_cast<std::uintptr_t>(fake_visibility_grid_vtable)));
    fake_visibility_grid_vtable[0x38/4]=0x464330; // actual retail bookkeeping-only cache clear
    join_in_progress::visibility_grid_getter_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_visibility_grid_get));
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
    // p060fix/AP: the rebuild resets the consumers' per-slot state (the npc power rule's cells) of exactly the free slots.
    static unsigned slot_resets[32]; for(auto& r:slot_resets) r=0;
    const auto saved_reset=actor_ai::reset_controller_slot;
    actor_ai::reset_controller_slot=[](std::uint32_t slot){ if(slot<32) ++slot_resets[slot]; };
    join_in_progress::canonical_brain_ring();
    actor_ai::reset_controller_slot=saved_reset;
    for(unsigned i=0;i<32;++i) assert(slot_resets[i]==((i<3||i==31)?0u:1u));
    assert(*reinterpret_cast<int*>(pool+brain_ring_write)==28 && *reinterpret_cast<int*>(pool+brain_ring_read)==0 && *reinterpret_cast<int*>(pool+brain_ring_free)==28);
    for(unsigned i=0;i<28;++i) assert(*reinterpret_cast<unsigned*>(pool+0xc384+4*i)==i+3);
    assert(*reinterpret_cast<unsigned*>(pool+brain_ring_bitmap)==0x80000007u);
    // p060fix/AI S2: the same rebuild on a synthetic E4 96-slot pool (stage 2 / 96 controllers published, the ring,
    // counters and three held words at the E4 ledger's operands): held 0,1,2 / 40 / 95 -> the 91 free slots in
    // ascending order, read 0, write 91, free 91; the held words, the live count and the stock pool untouched.
    {
        static alignas(16) unsigned char e4_pool[0x24e00]{};
        std::memset(e4_pool,0,sizeof e4_pool);
        const auto cai=unsigned(reinterpret_cast<std::uintptr_t>(e4_pool));
        actor_ai::published={2,cai,cai+4,0x61c,96,cai+0x24db8,0x268,96,2,0x24db0,0x525302};
        unsigned char* e4=e4_pool+4;
        put(e4,0x24c10,17);put(e4,0x24c14,23);put(e4,0x24c18,91);put(e4,0x24c1c,0x7u);put(e4,0x24c20,0x100u);put(e4,0x24c24,0x80000000u);put(e4,0x24c28,5);
        for(unsigned i=0;i<96;++i) put(e4,0x24a8c+4*i,unsigned((i*11+5)%96));
        const auto stock_before=std::vector<unsigned char>(pool+0xc384,pool+0xc41c);
        join_in_progress::canonical_brain_ring();
        assert(*reinterpret_cast<int*>(e4+0x24c10)==91 && *reinterpret_cast<int*>(e4+0x24c14)==0 && *reinterpret_cast<int*>(e4+0x24c18)==91 && *reinterpret_cast<int*>(e4+0x24c28)==5);
        unsigned expect=3;
        for(unsigned i=0;i<91;++i) { if(expect==40)++expect; assert(*reinterpret_cast<unsigned*>(e4+0x24a8c+4*i)==expect); ++expect; }
        for(unsigned i=91;i<96;++i) assert(*reinterpret_cast<unsigned*>(e4+0x24a8c+4*i)==0);
        assert(*reinterpret_cast<unsigned*>(e4+0x24c1c)==0x7u && *reinterpret_cast<unsigned*>(e4+0x24c20)==0x100u && *reinterpret_cast<unsigned*>(e4+0x24c24)==0x80000000u);
        assert(std::vector<unsigned char>(pool+0xc384,pool+0xc41c)==stock_before);
        // a stage-2 descriptor with another controller count is left alone (unknown layout)
        put(e4,0x24c14,23);actor_ai::published.controllers=32;join_in_progress::canonical_brain_ring();assert(*reinterpret_cast<int*>(e4+0x24c14)==23);
        // stage 1 (the standalone ActorPools=124 install publishes the stock layout) rebuilds the stock pool as stage 0 does
        actor_ai::published=actor_ai::stock;actor_ai::published.stage=1;
        put(pool,brain_ring_write,7);put(pool,brain_ring_read,11);put(pool,brain_ring_free,28);put(pool,brain_ring_bitmap,0x80000007u);
        for(unsigned i=0;i<32;++i) put(pool,0xc384+4*i,unsigned((i*5+3)%32));
        join_in_progress::canonical_brain_ring();
        assert(*reinterpret_cast<int*>(pool+brain_ring_write)==28 && *reinterpret_cast<int*>(pool+brain_ring_read)==0 && *reinterpret_cast<int*>(pool+brain_ring_free)==28);
        for(unsigned i=0;i<28;++i) assert(*reinterpret_cast<unsigned*>(pool+0xc384+4*i)==i+3);
        actor_ai::published=actor_ai::stock;
    }
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
    // p060fix/AJ: the frame-0 epochs above put the far marker into the mapped game+0x1f0; a held tick that reads it
    // reports "after the finish". This section models the other side: a camera eye stored there before the hold.
    { auto* anchor=reinterpret_cast<unsigned*>(game_static+game_respawn_anchor_at);anchor[0]=0x44f6d70fu;anchor[1]=0x44f3b1ecu;anchor[2]=0x43aa3853u; }
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
    // p060fix/AJ: the held tick was logged with what game+0x1f0 held (the mapped static: a camera eye here, not the
    // finish's far marker); the frame-0 epoch then puts the far marker (1e30, 0, 0) back so the first real update's
    // `all` is 1 on every peer whatever the held tick stored.
    {
        bool held_line=false;for(const auto& l:observed_jip_logs) held_line|=l.find("the held dormant-entity update ran at native frame")!=std::string::npos && l.find("this tick came before the finish or another update ran since")!=std::string::npos;
        assert(held_line && join_in_progress::held_tick_had_marker==false);
    }
    auto* respawn_anchor=reinterpret_cast<unsigned*>(game_static+game_respawn_anchor_at);
    respawn_anchor[0]=0x44f6d70fu;respawn_anchor[1]=0x44f3b1ecu;respawn_anchor[2]=0x43aa3853u; // a camera eye the site stored over the marker
    const auto sweep_logs_at=observed_jip_logs.size();const auto sweep_runs_before=join_in_progress::respawn_sweep_runs;
    *frame=0;join_in_progress::frame_gate_reseed(); // the frame-0 epoch releases the hold
    assert(!join_in_progress::smart_hold && *reinterpret_cast<unsigned*>(smart_object+smart_cursor_at)==0 && smart_getter_calls==2);
    assert(respawn_anchor[0]==join_in_progress_core::respawn_far_marker_bits && respawn_anchor[1]==0 && respawn_anchor[2]==0 && join_in_progress::respawn_sweep_runs==sweep_runs_before+1);
    {
        bool sweep_line=false;for(std::size_t i=sweep_logs_at;i<observed_jip_logs.size();++i) sweep_line|=observed_jip_logs[i].find("the first dormant-entity sweep after the first shared frame examines every record on every peer (game+0x1f0 := the far marker 7149f2ca,0,0; was 44f6d70f,44f3b1ec,43aa3853 = a camera eye; held tick at frame")!=std::string::npos;
        bool complete_line=false;for(std::size_t i=sweep_logs_at;i<observed_jip_logs.size();++i) complete_line|=observed_jip_logs[i].find("respawn release and full first sweep")!=std::string::npos;
        assert(sweep_line && complete_line);
    }
    // a second epoch finds the marker already there (a peer whose held tick came before the finish): still written, reported as such
    join_in_progress::canonical_respawn_sweep("fixture: marker already there",true);
    assert(respawn_anchor[0]==join_in_progress_core::respawn_far_marker_bits && observed_jip_logs.back().find("was 7149f2ca,00000000,00000000 = the marker already")!=std::string::npos);
    join_in_progress::canonical_respawn_sweep("fixture: reset off",false);assert(join_in_progress::respawn_sweep_runs==sweep_runs_before+2); // [Dev] LateJoinRestartReset=0: nothing
    assert(smart_bridge()==smart_object && smart_getter_calls==3);
    assert(!join_in_progress::late_restart && !join_in_progress::restart.armed); // follow-up 9: the epoch ended the late restart too
    join_in_progress::hold_smart_update();assert(!join_in_progress::smart_hold); // disarmed by the reseed: no second hold this epoch
    join_in_progress::restart.arm();join_in_progress::hold_smart_update();assert(!join_in_progress::smart_hold); // armed alone (no late restart): still none
    join_in_progress::late_restart=true;join_in_progress::restart.arm();join_in_progress::hold_smart_update();assert(join_in_progress::smart_hold); // a new late epoch holds again
    assert(join_in_progress::smart_holds==0 && join_in_progress::held_tick_frame==0 && !join_in_progress::held_tick_had_marker); // p060fix/AJ: the held-tick record starts again per hold
    respawn_anchor[0]=0x44f6d70fu;respawn_anchor[1]=0x44f3b1ecu;respawn_anchor[2]=0x43aa3853u;
    join_in_progress::release_smart_hold("fixture: session end");
    assert(!join_in_progress::smart_hold && smart_bridge()==smart_object);
    assert(respawn_anchor[0]==0x44f6d70fu && respawn_anchor[1]==0x44f3b1ecu); // a session's end releases the hold without touching game+0x1f0
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
        // p060fix/AJ2 (ZB-4Pb): the party hero w is an AI hero released at runtime (0x71: AI bit + the pending bit of the
        // periodic kind 0x6d5e30) with its event still queued, as the lobby guest had it; the non-party c is a player
        // hero (0x70) with its own. The pass clears w's bit and the rebuild drops w's event only.
        w[hero_ai_flags]=0x71;c[hero_ai_flags]=0x70;put(w,0x774,0x12340001u);
        add(3.0f,0x3802,hero_event_kind);add(3.0f,0x1801,hero_event_kind);
        join_in_progress::canonical_party_seat_events("fixture: off",false);
        assert(join_in_progress::seat_drop_count==0 && w[hero_ai_flags]==0x71);
        const auto seat_logs_at=observed_jip_logs.size();
        join_in_progress::canonical_party_seat_events("fixture: the first shared frame",true);
        assert(join_in_progress::seat_drop_count==1 && join_in_progress::seat_drop_handles[0]==0x3802u && w[hero_ai_flags]==0x51 && c[hero_ai_flags]==0x70 && *reinterpret_cast<unsigned*>(w+0x774)==0x12340001u);
        {
            bool seat_line=false;for(std::size_t i=seat_logs_at;i<observed_jip_logs.size();++i) seat_line|=observed_jip_logs[i].find("periodic event (kind 0x6d5e30) follows the AI bit at fixture: the first shared frame on every peer: 1 hero(s), 1 AI (their queued periodic events go in the rebuild), 1 pending bit(s) cleared on released seats, 0 player hero(s) without one; flags (slot:before->after/+0x774) 0:71->51/12340001")!=std::string::npos;
            assert(seat_line);
        }
        join_in_progress::restart.arm();const auto seat_logs_at2=observed_jip_logs.size();join_in_progress::canonical_event_queue("fixture: the first shared frame");
        assert(join_in_progress::seat_dropped_last==1 && join_in_progress::seat_drop_count==0 && *reinterpret_cast<unsigned*>(body+event_heap_count_at)==5);
        {
            unsigned kept=0;
            for(unsigned i=0;i<5;++i) {
                unsigned index,handle,event;std::memcpy(&index,body+event_heap_at+i*8+4,4);std::memcpy(&handle,body+index*event_record_bytes,4);std::memcpy(&event,body+index*event_record_bytes+4,4);
                assert(!(handle==0x3802u && event==hero_event_kind));
                if(event==hero_event_kind) ++kept;
            }
            assert(kept==1); // the player hero's stays
            bool rebuild_line=false;for(std::size_t i=seat_logs_at2;i<observed_jip_logs.size();++i) rebuild_line|=observed_jip_logs[i].find("0 party-hero moved event(s) dropped, 1 AI-hero periodic event(s) dropped")!=std::string::npos;
            assert(rebuild_line);
        }
        // a player hero (AI bit clear) keeps its pending bit; one without the bit is only counted
        w[hero_ai_flags]=0x70;c[hero_ai_flags]=0x50;
        join_in_progress::canonical_party_seat_events("fixture: player",true);
        assert(join_in_progress::seat_drop_count==0 && w[hero_ai_flags]==0x70 && observed_jip_logs.back().find("1 hero(s), 0 AI (their queued periodic events go in the rebuild), 0 pending bit(s) cleared on released seats, 0 player hero(s) without one")!=std::string::npos);
        w[hero_ai_flags]=0x50;join_in_progress::canonical_party_seat_events("fixture: player without",true);
        assert(join_in_progress::seat_drop_count==0 && w[hero_ai_flags]==0x50 && observed_jip_logs.back().find("1 player hero(s) without one")!=std::string::npos);
        w[hero_ai_flags]=0;c[hero_ai_flags]=0;
        anchor_vtable[game_party_list_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&anchor_party));
        join_in_progress::event_manager_at=event_manager_object;join_in_progress::entity_of_handle_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_entity_of)); // follow-up 12: the fake resolver stays (later frame-0 epochs walk the party)
        join_in_progress::late_restart=false;join_in_progress::restart.disarm();
    }
    // Follow-up 14 (AE15). (1) The camera's derived spans back to the pristine image: junk in every span and in two
    // configured fields; the restore puts the spans back, leaves the configured fields, counts the dwords; nothing
    // without a pristine image or on a foreign vtable.
    {
        put(camera,0,camera_vtable_value);
        for(const auto& x:camera_derived_spans) for(unsigned i=0;i<x.bytes;++i) camera[x.at+i]=static_cast<unsigned char>(0xa5+i);
        put(camera,camera_mode_at,3u);put(camera,camera_flags_at,0x5528u);
        const unsigned changed=join_in_progress::restore_camera_derived("fixture");
        bool spans_back=true;for(const auto& x:camera_derived_spans) spans_back&=std::memcmp(camera+x.at,join_in_progress::camera_pristine+x.at,x.bytes)==0;
        assert(spans_back && changed>0 && *reinterpret_cast<unsigned*>(camera+camera_mode_at)==3u && *reinterpret_cast<unsigned*>(camera+camera_flags_at)==0x5528u);
        assert(join_in_progress::restore_camera_derived("fixture: again")==0);
        put(camera,0,0x12345678u);camera[0x104]=0x99;
        assert(join_in_progress::restore_camera_derived("fixture: foreign vtable")==0 && camera[0x104]==0x99);
        put(camera,0,camera_vtable_value);
        const bool had=join_in_progress::restart.snapshot;join_in_progress::restart.snapshot=false;
        assert(join_in_progress::restore_camera_derived("fixture: no image")==0 && camera[0x104]==0x99);
        join_in_progress::restart.snapshot=had;
    }
    // (2) The OnAttached override bridge, entered exactly as installed. Party = {0x3802} (fake entity slot 2) through
    // the fake game; slot 1 (0x1801) is a non-party entity. Outside a late restart the override runs (the displaced
    // prologue, then the resume) for everyone; armed, a party hero's call is answered `ret 4` with every register
    // kept and the stack balanced, a non-party child's still runs; after the frame-0 epoch (armed consumed) it runs again.
    {
        anchor_vtable[game_party_list_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&moved_party));
        join_in_progress::attach_override_resume_at=unsigned(reinterpret_cast<std::uintptr_t>(&attach_resume_stub));
        unsigned char* parent=fake_entities[1];unsigned char* hero=fake_entities[2];put(parent,0x1c,0x1801u);put(hero,0x1c,0x3802u);
        join_in_progress::late_restart=false;join_in_progress::restart.disarm();override_runs=0;join_in_progress::attach_suppressions=0;
        attach_probe(parent,hero);assert(override_runs==1 && join_in_progress::attach_suppressions==0);
        assert(join_in_progress::last_attach_parent==unsigned(reinterpret_cast<std::uintptr_t>(parent)) && join_in_progress::last_attach_child==unsigned(reinterpret_cast<std::uintptr_t>(hero)));
        join_in_progress::late_restart=true;join_in_progress::restart.arm();
        attach_probe(parent,hero);
        assert(override_runs==1 && join_in_progress::attach_suppressions==1); // suppressed: ret 4, the override never ran
        assert(attach_regs[0]==0x22222222u && attach_regs[1]==0x44444444u && attach_regs[2]==0x55555555u && attach_regs[3]==0x66666666u && attach_regs[4]==0x77777777u);
        assert(observed_jip_logs.back().find("ground attach callback (CActor OnAttached 0x429710")!=std::string::npos && observed_jip_logs.back().find("hero 00003802 on entity 00001801")!=std::string::npos);
        attach_probe(hero,parent);assert(override_runs==2 && join_in_progress::attach_suppressions==1); // a non-party child: the override runs
        attach_probe(parent,nullptr);assert(override_runs==3 && join_in_progress::attach_suppressions==1); // a null child: never suppressed (the override's own null test would follow)
        join_in_progress::restart_reset_enabled=false;attach_probe(parent,hero);assert(override_runs==4 && join_in_progress::attach_suppressions==1); // [Dev] LateJoinRestartReset=0: retail
        join_in_progress::restart_reset_enabled=true;
        // (3) The frame-0 epoch (the production frame_gate_reseed): the party hero's ground entity goes back to 0 and
        // FindGround runs on it (the redirected counter) with the override no longer suppressed; a probe-time attach
        // callback is counted, not suppressed.
        put(hero,actor_ground_at,0x1801u);find_ground_calls=0;join_in_progress::late_restart_epoch_for_camera=false;
        // AE16: warm motion stamps and a velocity on the party hero; a non-party entity keeps its own
        put(hero,actor_gravity_hold_at,0x43340000u);put(hero,actor_last_fall_at,0x42b40000u);put(hero,actor_last_update_at,0x42b40000u);put(hero,actor_velocity_at,0x3f800000u);put(hero,actor_velocity_prev_at+8,0x40000000u);put(hero,actor_angular_at+4,0x3e800000u);put(hero,hero_auto_move_flag_at,1u);
        put(parent,actor_gravity_hold_at,0x43340000u);put(parent,actor_last_update_at,0x42b40000u);
        // p060fix/AK: the non-party entity is a CActor without a controller carrying a one-step move goal (a speed, a
        // goal vector, the +6 flag, a follow handle); the party hero carries a speed too and must keep it (its passes own it).
        fake_world_vtable[world_entity_by_index_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&fake_world_entity_at));
        join_in_progress::world_getter_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_world_get));fake_world_walks=0;
        put(parent,0,actor_vtable);put(parent,actor_controller,0xffffffffu);put(parent,actor_move_speed_a_at,0x3f800000u);put(parent,actor_move_speed_b_at,0u);put(parent,actor_move_goal_at,0x40a00000u);put(parent,actor_move_start_at+8,0x3f000000u);parent[actor_entity_flags6_at]=0x01;put(parent,actor_follow_handle_at,0x1917u);
        put(hero,0,actor_vtable);put(hero,actor_controller,0u);put(hero,actor_move_speed_a_at,0x3f800000u);put(hero,actor_move_goal_at,0x41200000u);
        put(parent,entity_registered_at,1u);put(hero,entity_registered_at,1u);hero[actor_entity_flags6_at]=1;
        std::memcpy(fake_world_object+0x756c+16,parent+entity_flags_at,entity_flag_bytes);
        std::memcpy(fake_world_object+0x756c+32,hero+entity_flags_at,entity_flag_bytes);
        put(fake_world_object,0x8570+16*32,6u); // registered NPC 1 and party hero 2 initially active
        *frame=0;*seed=0x88888888u;*count=4;
        const auto epoch_at=observed_jip_logs.size();
        const auto turret_epochs_before=xml1_turrets::fixture_epochs;
        join_in_progress::frame_gate_reseed();
        assert(xml1_turrets::fixture_epochs==turret_epochs_before+1);
        assert(!join_in_progress::restart.armed && !join_in_progress::late_restart && *seed==native_seed);
        assert(find_ground_calls==1 && find_ground_order[0]==hero && *reinterpret_cast<unsigned*>(hero+actor_ground_at)==0);
        bool ground_line=false;for(std::size_t i=epoch_at;i<observed_jip_logs.size();++i) ground_line|=observed_jip_logs[i].find("ground is re-probed at the first shared frame on every peer: 1 hero(s), 1 had a ground entity before, 1 ray hit(s), 0 attach callback(s)")!=std::string::npos;
        assert(ground_line && join_in_progress::attach_suppressions==0);
        { // AE16: the motion pass ran after the re-probe: stamps := the game time (the mapped static's +0x3e8), hold 0, velocities 0, the auto-move flag only read
            const unsigned t=*reinterpret_cast<const unsigned*>(game_static+game_time_at);
            assert(*reinterpret_cast<unsigned*>(hero+actor_gravity_hold_at)==0 && *reinterpret_cast<unsigned*>(hero+actor_last_fall_at)==t && *reinterpret_cast<unsigned*>(hero+actor_last_update_at)==t);
            for(unsigned off:{actor_velocity_at,actor_angular_at,actor_velocity_prev_at,actor_angular_prev_at}) for(unsigned i=0;i<12;i+=4) assert(*reinterpret_cast<unsigned*>(hero+off+i)==0);
            assert(*reinterpret_cast<unsigned*>(hero+hero_auto_move_flag_at)==1u && *reinterpret_cast<unsigned*>(parent+actor_gravity_hold_at)==0x43340000u && *reinterpret_cast<unsigned*>(parent+actor_last_update_at)==0x42b40000u);
            bool motion_line=false;for(std::size_t i=epoch_at;i<observed_jip_logs.size();++i) motion_line|=observed_jip_logs[i].find("motion fields are canonical at the first shared frame on every peer")!=std::string::npos && observed_jip_logs[i].find("1 hero(s); gravity hold cleared on 1, last fall set on 1, last update set on 1, velocity zeroed on 1, auto-move flag set on 1 (not written)")!=std::string::npos;
            assert(motion_line);
        }
        { // p060fix/AK: the NPC pass walked the world once (256 slots through vt+0x10): the NPC's speeds := 0, goal and start := FLT_MIN; the party hero's speed and goal untouched
            assert(fake_world_walks==256);
            assert(*reinterpret_cast<unsigned*>(parent+actor_move_speed_a_at)==0 && *reinterpret_cast<unsigned*>(parent+actor_move_speed_b_at)==0);
            for(unsigned i=0;i<24;i+=4) assert(*reinterpret_cast<unsigned*>(parent+actor_move_goal_at+i)==move_goal_none_bits);
            assert(parent[actor_entity_flags6_at]==0x00 && *reinterpret_cast<unsigned*>(parent+actor_follow_handle_at)==0x1917u); // p060fix/AL: the active bit cleared; the follow handle counted, not written
            assert(std::memcmp(fake_world_object+0x756c+16,parent+entity_flags_at,entity_flag_bytes)==0);
            assert(*reinterpret_cast<unsigned*>(fake_world_object+0x8570+16*32)==4u); // NPC cache AND bitmap cleared; hero kept
            assert(hero[actor_entity_flags6_at]==1 && std::memcmp(fake_world_object+0x756c+32,hero+entity_flags_at,entity_flag_bytes)==0);
            assert(observed_jip_logs.back().find("1 world-index entries corrected")!=std::string::npos || std::any_of(observed_jip_logs.begin()+epoch_at,observed_jip_logs.end(),[](const auto& l){return l.find("1 world-index entries corrected")!=std::string::npos;}));
            assert(*reinterpret_cast<unsigned*>(hero+actor_move_speed_a_at)==0x3f800000u && *reinterpret_cast<unsigned*>(hero+actor_move_goal_at)==0x41200000u);
            bool npc_line=false;for(std::size_t i=epoch_at;i<observed_jip_logs.size();++i) npc_line|=observed_jip_logs[i].find("the AI actors' move intent is canonical at the first shared frame on every peer: 1 AI actor(s) (CActor without a controller; 1 party hero(s) left to their own passes, 256 entity slot(s) walked), 1 had a move speed")!=std::string::npos && observed_jip_logs[i].find("1 a move goal (+0x1d0), 1 the active bit (+4 bit 16 = +6 bit 0, the other every-frame input: cleared on 1), 1 a follow handle (+0x3ec, counted only); goal, start and speeds now 0x421b90's values on all; with a speed: 1 (run ")!=std::string::npos; // the run counter counts the fixture's earlier epochs too
            if(!npc_line) for(std::size_t i=epoch_at;i<observed_jip_logs.size();++i) if(observed_jip_logs[i].find("intent")!=std::string::npos) std::cerr<<"npc pass logged: "<<observed_jip_logs[i]<<"\n";
            assert(npc_line);
            // a second epoch with nothing carrying a speed logs `with a speed: none`
            join_in_progress::canonical_npc_move_intent("fixture: again",true);
            assert(observed_jip_logs.back().find("0 had a move speed")!=std::string::npos && observed_jip_logs.back().find("0 the active bit (+4 bit 16 = +6 bit 0, the other every-frame input: cleared on none)")!=std::string::npos && observed_jip_logs.back().find("with a speed: none (run ")!=std::string::npos);
            assert(observed_jip_logs.back().find("0 world-index entries corrected")!=std::string::npos);
            // Unregistered NPC: the object bit clears, but there is no world index to notify (0x45fe40's guard).
            put(parent,entity_registered_at,0u);parent[actor_entity_flags6_at]=1;
            const std::vector<unsigned char> index_before(fake_world_object,fake_world_object+sizeof fake_world_object);
            join_in_progress::canonical_npc_move_intent("fixture: unregistered",true);
            assert(parent[actor_entity_flags6_at]==0 && std::memcmp(index_before.data(),fake_world_object,sizeof fake_world_object)==0);
            assert(observed_jip_logs.back().find("0 world-index entries corrected")!=std::string::npos);
            put(parent,entity_registered_at,1u);
            // the hero keeps its own active bit (only the NPC's is cleared); a second NPC flag set now is cleared by the next run
            hero[actor_entity_flags6_at]=0x01;parent[actor_entity_flags6_at]=0x01;join_in_progress::canonical_npc_move_intent("fixture: flag again",true);
            assert(parent[actor_entity_flags6_at]==0 && hero[actor_entity_flags6_at]==0x01 && observed_jip_logs.back().find("1 the active bit (+4 bit 16 = +6 bit 0, the other every-frame input: cleared on 1)")!=std::string::npos);
            hero[actor_entity_flags6_at]=0;
            const auto runs_logged=observed_jip_logs.size();
            join_in_progress::canonical_npc_move_intent("fixture: off",false);
            assert(observed_jip_logs.size()==runs_logged); // not wanted: nothing walked, nothing logged
            put(parent,0,0u);put(hero,0,0u);put(hero,actor_move_speed_a_at,0u);put(hero,actor_move_goal_at,0u);parent[actor_entity_flags6_at]=0;put(parent,actor_follow_handle_at,0u);put(parent,actor_controller,0u);
        }
        attach_probe(parent,hero);assert(override_runs==5); // after the epoch: retail again
        // a callback during the re-probe itself (FindGround finding an actor below) runs and is counted on every peer
        join_in_progress::ground_probe_running=true;attach_probe(parent,hero);assert(override_runs==6 && join_in_progress::attach_calls_during_probe==1);join_in_progress::ground_probe_running=false;join_in_progress::attach_calls_during_probe=0;
        // (4) The party-binding tail bridge (a CALL over `call 0x495350`, the getter redirected): the cold moved pass
        // runs on the (now bound) party only in an armed late restart; the site's getter result comes back either way.
        join_in_progress::party_bound_target_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_getter));
        const auto bound_bridge=reinterpret_cast<void*(__cdecl*)()>(&join_in_progress::party_bound_bridge);
        const float pos[3]={2003.f,1741.f,1.01f},old[3]={1.f,2.f,3.f};
        std::memcpy(hero+actor_position_at,pos,12);std::memcpy(hero+actor_last_position_at,old,12);hero[actor_moved_flags_at]=0x0a;
        join_in_progress::late_restart=false;join_in_progress::restart.disarm();const auto runs_before=join_in_progress::moved_canonical_runs;const auto getter_before=getter_calls;
        assert(bound_bridge()==&getter_object && getter_calls==getter_before+1 && join_in_progress::moved_canonical_runs==runs_before && hero[actor_moved_flags_at]==0x0a);
        join_in_progress::late_restart=true;join_in_progress::restart.arm();
        assert(bound_bridge()==&getter_object && getter_calls==getter_before+2 && join_in_progress::moved_canonical_runs==runs_before+1);
        assert(hero[actor_moved_flags_at]==0x08 && std::memcmp(hero+actor_last_position_at,pos,12)==0 && join_in_progress::moved_drop_count==1 && join_in_progress::moved_drop_handles[0]==0x3802u);
        assert(observed_jip_logs.back().find("moved-check state is canonical at the zone start (after the party is bound, before the first physics step)")!=std::string::npos);
        join_in_progress::moved_drop_count=0;join_in_progress::late_restart=false;join_in_progress::restart.disarm();
        anchor_vtable[game_party_list_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&anchor_party));
    }
    // Int12: the initial 60-frame diagnostic window expires; later movement
    // starts exactly one further 60-frame window, including the moving frame.
    {
        redirect(entity_of_handle,fake_entity_of); // the probe uses retail's entry, not the canonicalisation seam
        anchor_vtable[game_party_list_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&moved_party));
        auto* hero=fake_entities[2];
        join_in_progress::probe_enabled=true;join_in_progress::probe_epoch_late=true;
        join_in_progress::hero_move_rearmed=false;
        for(auto& valid:join_in_progress::hero_last_valid)valid=false;
        join_in_progress::hero_lines_arm("fixture initial",0);
        auto lines=[]() { return std::count_if(observed_jip_logs.begin(),observed_jip_logs.end(),[](const auto& line) { return line.find("jip hero: frame=")==0; }); };
        const auto initial=lines();
        for(unsigned frame=1;frame<=60;++frame)join_in_progress::probe_hero_frame(frame);
        assert(lines()==initial+60);
        join_in_progress::probe_hero_frame(61);assert(lines()==initial+60);
        put(hero,actor_position_at,0x42c80000u);
        join_in_progress::probe_hero_frame(62);
        assert(join_in_progress::hero_move_rearmed && join_in_progress::hero_lines==1 && lines()==initial+61);
        assert(std::any_of(observed_jip_logs.begin(),observed_jip_logs.end(),[](const auto& line) { return line.find("jip move: party slot 0 (handle 00003802) first moved at frame 62") == 0; }));
        for(unsigned frame=63;frame<=121;++frame)join_in_progress::probe_hero_frame(frame);
        assert(lines()==initial+120);
        put(hero,actor_position_at,0x43480000u);
        join_in_progress::probe_hero_frame(122);assert(lines()==initial+120); // only once per restart
        join_in_progress::hero_lines_arm("fixture device switch",123);
        join_in_progress::probe_hero_frame(123);assert(lines()==initial+121); // explicit rearm still works
        join_in_progress::probe_enabled=false;
        join_in_progress::probe_hero_frame(124);assert(lines()==initial+121);
        join_in_progress::probe_enabled=true;join_in_progress::probe_epoch_late=false;
        join_in_progress::probe_hero_frame(125);assert(lines()==initial+121); // no late epoch
        join_in_progress::probe_enabled=false;
        anchor_vtable[game_party_list_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&anchor_party));
    }
    run_audit_probe();
    run_visibility_pattern();
    run_join_hold(); // p060fix/AQ: the join hold (last: it redirects the clock, scheduler and campaign-start natives)
    std::cout<<"PASS canonical restart: construction image taken through the bridge, restored only for a late-join cohort restart (lobby START epoch and foreign vtable refused), retail 0x560150 reseeds the synchronized stream once at frame 0 through the frame-gate bridge, 0x47 stamp bridge entered by JUMP writes opcode 0x47 and tail 0/marker with no other byte touched, a given-up added seat is dropped natively (codes 0x10ce/0x10cf) and a pending one is not; the camera\'s derived spans go back to the pristine image, the OnAttached override bridge answers ret 4 for a party hero in an armed late restart only, the frame-0 epoch re-probes the party heroes\' ground, and the party-binding tail bridge runs the cold moved pass on the bound party; initial and first-movement hero probe windows are bounded and rearm correctly\n";
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
// p060fix/AI S1: the standalone ActorPools=124 install (no Actors=96) relocates the brain alloc's ring read at
// 0x522a18 into its W11 cave BEFORE join_in_progress installs (exports.cpp order). The production plan
// (actor_pools_plan::make, the code the DLL commits) is built against a fixture cave and ONLY its W11 relocation is
// applied to the mapped retail image, the module stub reporting the committed bytes as the DLL does; the brain alloc
// guard composes them, every other prefix byte stays guarded, and a ring rebuilt canonically on the stock pool is
// popped IN ORDER through the relocated alloc (the cave ran the displaced read, the observer saw pool 3 / the slot).
unsigned w11_observer_calls=0,w11_observer_pool=0,w11_observer_slot=0;
void __cdecl w11_observer(unsigned pool,unsigned live) { ++w11_observer_calls;w11_observer_pool=pool;w11_observer_slot=live; }
void run_actor_pools() {
    using namespace join_in_progress_rules;
    auto* site=reinterpret_cast<unsigned char*>(static_cast<std::uintptr_t>(brain_ring_pop_relocated));
    const auto guard=std::find_if(guards.begin(),guards.end(),[](const auto& g){return g.va==brain_ring_pop;});
    assert(guard!=guards.end() && guard->hex.size()==32 && join_in_progress::mismatch()==nullptr);
    const std::vector<unsigned char> retail_site(site,site+7);
    assert(patch_transaction::bytes_of(entity_capacity_b_sites::controller_reset.before)==retail_site); // the module's own guard = the mapped retail bytes
    // the module installed first: its production plan against a fixture cave, only the W11 relocation applied
    auto* block=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x3674,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    auto* caves=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));
    assert(block && caves);
    const auto plan=actor_pools_plan::make(unsigned(reinterpret_cast<std::uintptr_t>(block)),unsigned(reinterpret_cast<std::uintptr_t>(caves)),unsigned(reinterpret_cast<std::uintptr_t>(&w11_observer)));
    assert(plan.has_value() && plan->code.size()<=4096);
    std::memcpy(caves,plan->code.data(),plan->code.size());
    const auto w11=std::find_if(plan->contribution.writes.begin(),plan->contribution.writes.end(),[](const auto& w){return w.va==brain_ring_pop_relocated;});
    assert(w11!=plan->contribution.writes.end() && w11->bytes.size()==7 && w11->bytes[0]==0xe9 && w11->bytes[5]==0x90 && w11->bytes[6]==0x90);
    const auto w11_guard=std::find_if(plan->contribution.guards.begin(),plan->contribution.guards.end(),[](const auto& g){return g.va==brain_ring_pop_relocated;});
    assert(w11_guard!=plan->contribution.guards.end() && w11_guard->bytes==retail_site);
    std::memcpy(site,w11->bytes.data(),7);
    actor_ai::published=actor_ai::stock;actor_ai::published.stage=1; // what the standalone install publishes
    // failing-first: the raw guard refuses the relocated site while the module reports nothing (int11 at f9ffb9f)
    actor_pools::fixture_controller_reset.clear();
    assert(join_in_progress::mismatch()==&*guard);
    // the module reports its committed bytes: accepted; every one of the 16 prefix bytes stays guarded
    actor_pools::fixture_controller_reset=w11->bytes;
    assert(join_in_progress::mismatch()==nullptr);
    for(unsigned i=0;i<16;++i) { auto* b=reinterpret_cast<unsigned char*>(static_cast<std::uintptr_t>(brain_ring_pop+i));*b^=1;assert(join_in_progress::mismatch()==&*guard);*b^=1; }
    assert(join_in_progress::mismatch()==nullptr);
    // a report that differs from the image (another cave), a report at stage 0, or E4 reporting installation: refused
    auto other=w11->bytes;other[1]^=0x10;actor_pools::fixture_controller_reset=other;assert(join_in_progress::mismatch()==&*guard);
    actor_pools::fixture_controller_reset=w11->bytes;actor_ai::published.stage=0;assert(join_in_progress::mismatch()==&*guard);actor_ai::published.stage=1;
    e4_core::fixture_installed=true;assert(join_in_progress::mismatch()!=nullptr);e4_core::fixture_installed=false;
    assert(join_in_progress::mismatch()==nullptr);
    // a ring rebuilt canonically on the stock pool (held 0,1,2,31; stage 1 = the stock layout) is popped in ascending
    // order through the relocated alloc: the cave executes the displaced read, the observer sees pool 3 with the
    // slot, retail marks the slot held, advances read, drops free and raises live.
    auto* pool=reinterpret_cast<unsigned char*>(brain_pool_stock);
    put(pool,brain_ring_write,7);put(pool,brain_ring_read,11);put(pool,brain_ring_free,28);put(pool,brain_ring_bitmap,0x80000007u);put(pool,0xc418,4);
    for(unsigned i=0;i<32;++i) put(pool,0xc384+4*i,unsigned((i*5+3)%32));
    join_in_progress::late_restart=true;join_in_progress::restart.arm();
    join_in_progress::canonical_brain_ring();
    join_in_progress::late_restart=false;join_in_progress::restart.disarm();
    assert(*reinterpret_cast<int*>(pool+brain_ring_read)==0 && *reinterpret_cast<int*>(pool+brain_ring_write)==28 && *reinterpret_cast<int*>(pool+brain_ring_free)==28);
    const auto alloc=reinterpret_cast<unsigned(__thiscall*)(void*)>(static_cast<std::uintptr_t>(brain_ring_pop));
    assert(alloc(pool)==3 && w11_observer_calls==1 && w11_observer_pool==3 && w11_observer_slot==3);
    assert(*reinterpret_cast<int*>(pool+brain_ring_read)==1 && *reinterpret_cast<int*>(pool+brain_ring_free)==27 && *reinterpret_cast<unsigned*>(pool+brain_ring_bitmap)==0x8000000fu && *reinterpret_cast<int*>(pool+0xc418)==5);
    assert(alloc(pool)==4 && w11_observer_calls==2 && w11_observer_slot==4 && *reinterpret_cast<int*>(pool+brain_ring_read)==2 && *reinterpret_cast<int*>(pool+brain_ring_free)==26);
    // retail restored: nothing composes, the raw guard matches again
    std::memcpy(site,retail_site.data(),7);actor_pools::fixture_controller_reset.clear();actor_ai::published=actor_ai::stock;
    assert(join_in_progress::mismatch()==nullptr);
    std::cout<<"PASS ActorPools composition: the standalone ActorPools=124 W11 controller-reset relocation (production plan, installed first) at the AI brain alloc is accepted by the JoinInProgress guards when the module reports its committed bytes (unreported, differing, stage-0 and E4-installed reports refused; every prefix byte still guarded), and a canonically rebuilt stock ring is popped in ascending order through the relocated alloc (cave ran, observer saw the slot)\n";
}
}
