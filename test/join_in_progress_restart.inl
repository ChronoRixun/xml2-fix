// AE: the canonical cohort restart against the mapped retail image (no launch): the pristine camera image is
// taken by the construction bridge, restored before the native campaign start, and the real retail seed setter
// (0x560150) runs at the first shared frame through the frame-gate bridge.
namespace restart_fixture {
unsigned constructor_calls=0,input_runs=0;void* input_run_this=nullptr;
void __fastcall fake_constructor(void* self,void*,int) {
    ++constructor_calls;
    put(self,0,join_in_progress_rules::camera_vtable_value);
    put(self,join_in_progress_rules::camera_yaw_at,0u);
    put(self,join_in_progress_rules::camera_mode_at,0u);
    put(self,0x2f8,0x3f490fdbu); // pitch 45 deg as the constructor stores it
}
void __fastcall fake_input_run(void* self,void*) { ++input_runs;input_run_this=self; }
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
__declspec(naked) void resume_stub() { __asm { ret } }
unsigned char probe_opcode=0;unsigned short probe_marker=0;unsigned probe_eax=0;
const void* stamp_bridge_target=reinterpret_cast<const void*>(&join_in_progress::prepare_opcode_bridge);
// Start Game's frame at 0x6095a5: the body at [esp+0x18] (its tail bytes at +0x1e), the opcode byte at [esp+0x30].
__declspec(naked) void stamp_probe() {
    __asm {
        sub esp, 40h
        mov byte ptr [esp+30h], 0
        mov word ptr [esp+1eh], 0eeeeh
        mov eax, 5a5aa5a5h
        call dword ptr [stamp_bridge_target]
        mov dword ptr [probe_eax], eax
        mov al, byte ptr [esp+30h]
        mov byte ptr [probe_opcode], al
        mov ax, word ptr [esp+1eh]
        mov word ptr [probe_marker], ax
        add esp, 40h
        ret
    }
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
    join_in_progress::epoch.active=false;
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
    // The frame-gate bridge itself: the native input-manager run is replaced by a counter; the thiscall this survives.
    join_in_progress::input_manager_run_at=unsigned(reinterpret_cast<std::uintptr_t>(&fake_input_run));
    join_in_progress::restart.arm();*frame=0;*seed=0x22222222u;*count=5;
    const auto bridge=reinterpret_cast<void(__fastcall*)(void*,void*)>(&join_in_progress::frame_gate_run_bridge);
    bridge(reinterpret_cast<void*>(0x1234),nullptr);
    assert(input_runs==1 && input_run_this==reinterpret_cast<void*>(0x1234) && *seed==native_seed && *count==0);
    bridge(reinterpret_cast<void*>(0x1234),nullptr);
    assert(input_runs==2 && *seed==native_seed);
    // The 0x47 stamp bridge on a synthetic Start Game frame: the displaced opcode write lands at [esp+0x30], the two
    // unused body bytes at [esp+0x1e] become 0 (lobby START) or the marker (late restart); EAX survives.
    join_in_progress::prepare_opcode_resume_at=unsigned(reinterpret_cast<std::uintptr_t>(&resume_stub));
    join_in_progress::late_restart_sending=false;stamp_probe();
    assert(probe_opcode==0x47 && probe_marker==0 && probe_eax==0x5a5aa5a5u);
    join_in_progress::late_restart_sending=true;stamp_probe();
    assert(probe_opcode==0x47 && probe_marker==join_in_progress_core::late_restart_marker && probe_eax==0x5a5aa5a5u);
    join_in_progress::late_restart_sending=false;
    std::cout<<"PASS canonical restart: construction image taken through the bridge, restored only for a late-join cohort restart (lobby START epoch and foreign vtable refused), 0x47 stamp 0/marker on a synthetic Start Game frame, retail 0x560150 reseeds the synchronized stream once at frame 0 through the frame-gate bridge\n";
}
}
