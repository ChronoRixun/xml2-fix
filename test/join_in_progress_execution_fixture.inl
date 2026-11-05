// AR5: mapped retail CClient wait branch and helper, with fixture-owned service/render objects.
// No window, game process, network or desktop input. The command dispatcher and controller edge reader are retail code.
namespace execution_fixture {
using namespace join_in_progress_rules;
using namespace join_in_progress_core;
unsigned polls=0, produces=0, feedback=0, seat_ticks=0, manager_ticks=0, menu_ticks=0, hud_ticks=0, draws=0, presents=0, worlds=0, cleanups=0, sleeps=0;
unsigned controller_commands=0, save_commands=0, service_order=0, mutation=0, actual_pauses=0;
unsigned restart_entries=0, mode2_loads=0, network_setups=0;
unsigned consumed_step=0;
unsigned event_state=0,ai_state=0;
unsigned video_updates=0, network_notice_polls=0;
unsigned diagnostic_failures=0;
unsigned fixture_stage=0;
bool message=true, command_load=false, command_end=false;
alignas(16) unsigned char client[0x40]{}, menus_image[0x86100]{}, modal[0x40]{}, command_image[0x700]{}, native_manager[0xe0]{}, fake_clock[0x40]{}, video[0x4060]{}, hud[8]{}, display_image[8]{};
unsigned clock_vt[32]{}, controller_vt2[64]{}, seat_vt2[40]{}, manager_vt[4]{}, menu_vt[160]{}, command_vt[8]{}, video_vt[32]{}, hud_vt[12]{}, display_vt[8]{}, game_vt2[160]{};
unsigned* controller_object=reinterpret_cast<unsigned*>(0x79ebc0);
unsigned* seat_object=seat_vt2;
unsigned query_return=0;
const void* observer_target=reinterpret_cast<const void*>(&join_in_progress::execution_observe_bridge);
alignas(16) unsigned char fpu_before[512]{},fpu_after[512]{};
unsigned abi_before=0,abi_after=0,abi_ebx=0,abi_esi=0,abi_edi=0;
__declspec(naked) void observer_abi() { __asm {
    push ebx
    push esi
    push edi
    mov dword ptr [abi_before], esp
    mov ebx, 12345678h
    mov esi, 23456789h
    mov edi, 3456789ah
    fld1
    fldpi
    pcmpeqd xmm0, xmm0
    fxsave fpu_before
    push 1
    call dword ptr [observer_target]
    fxsave fpu_after
    mov dword ptr [abi_after], esp
    mov dword ptr [abi_ebx], ebx
    mov dword ptr [abi_esi], esi
    mov dword ptr [abi_edi], edi
    fstp st(0)
    fstp st(0)
    pop edi
    pop esi
    pop ebx
    ret
} }
float __fastcall seconds(void*,void*,int) { return 1.0f; }
void __fastcall set_step(void*,void*,float step) { std::memcpy(&consumed_step,&step,4); }
void* __cdecl clock_get() { return fake_clock; }
void* __cdecl controller_get() { return controller_object; }
void* __cdecl seat_get() { return &seat_object; }
void* __cdecl command_get() { return command_image; }
void* __cdecl manager_get() { return native_manager; }
void* __cdecl scheduler_get() { return m; }
void* __cdecl video_get() { return video; }
void* __cdecl menu_get() { return menus_image; }
void* __cdecl hud_get() { return hud; }
void* __cdecl display_get() { return display_image; }
void* __cdecl game_get() { return reinterpret_cast<void*>(game_static); }
void* __cdecl session_get() { return reinterpret_cast<void*>(gns_transport_rules::session_static); }
void* __cdecl zones_get() { return reinterpret_cast<void*>(zones_static); }
void* __cdecl cohort_manager_get() { return m; }
void* __cdecl raw_pause_get() { return reinterpret_cast<void*>(0xa3e168); }
char __fastcall zone_loading(void*,void*) { return (*reinterpret_cast<unsigned char*>(zones_static+zones_loading_at)&3)!=0; }
void __fastcall network_setup_fake(void*,void*,unsigned flags) { assert(flags==0x10);++network_setups; }
void __fastcall load_mode2(void*,void*,void*,int mode) { assert(mode==2);++mode2_loads;*reinterpret_cast<unsigned char*>(zones_static+zones_loading_at)=3; }
void __cdecl native_restart(int mode) {
    assert(mode==0 && join_in_progress::execution.phase==restart_execution_phase::restoring);
    ++restart_entries;
    assert(join_in_progress::campaign_start_notice());
    join_in_progress::campaign_load_bridge(reinterpret_cast<void*>(game_static),nullptr,nullptr,0);
}
int __fastcall count(void*,void*) { return 2; }
char __fastcall no(void*,void*) { return 0; }
char __fastcall yes(void*,void*) { return 1; }
void __fastcall feedback_set(void*,void*,int,int,int,int) { fixture_stage=3;++feedback; }
char __fastcall seats_service(void*,void*) { fixture_stage=4;++seat_ticks; return 1; }
void __fastcall manager_service(void* self,void*) {
    fixture_stage=6;++manager_ticks;
    // The real manager tick also calls seats before testing pending state.
    reinterpret_cast<void(__thiscall*)(void*)>(0x4af9e0)(self);
}
void __fastcall raw_poll(void*,void*,float,int) { fixture_stage=1;++polls; }
void __fastcall produce(void*,void*,int,int,int) { fixture_stage=2;++produces; }
const char* __fastcall message_get(void*,void*) { fixture_stage=7;++network_notice_polls;return message ? "fixture wait" : nullptr; }
void WINAPI yield(DWORD n) { assert(n==1); ++sleeps; }
void __fastcall execute_command(void*,void*,const char* text) {
    fixture_stage=5;
    if (std::strstr(text,"controllersContinue")) { ++controller_commands; service_order=service_order*10+1; }
    if (std::strstr(text,"saveload")) {
        ++save_commands; service_order=service_order*10+2;
        if(command_load) *reinterpret_cast<unsigned char*>(zones_static+zones_loading_at)=3;
        if(command_end) *reinterpret_cast<unsigned char*>(gns_transport_rules::session_static+0x420)=0;
    }
}
// Real controller writer through its +0x2f8/+0x374 stores and analog copy; fixture continuation ends before audio/feedback.
__declspec(naked) void writer_done() { __asm { pop edi
    pop esi
    pop ebx
    ret 12
} }
void __fastcall apply_controller(void*,void*,int logical,const unsigned* buttons,const float* analog) {
    const unsigned mapped=controller_object[1+logical]; assert(mapped<4);
    auto* object=reinterpret_cast<void*>(0x79ebc0+0x3c+0x390*mapped);
    reinterpret_cast<void(__thiscall*)(void*,int,const unsigned*,const float*)>(0x550fb0)(object,logical,buttons,analog);
}
char __fastcall query(void*,void*,int code,int* logical) {
    assert(code==0x13);
    // Guest logical controller 1 maps to object 3; on that guest logical 0 maps to object 2.
    for (int k=0;k<2;++k) {
        auto* object=reinterpret_cast<void*>(0x79ebc0+0x3c+0x390*controller_object[1+k]);
        const auto edge=reinterpret_cast<unsigned(__thiscall*)(void*)>(0x550b70)(object);
        if(edge&0x80000) { *logical=k;return 1; }
    }
    return 0;
}
// Jump into the actual patched query CALL with its native stack arguments (no synthetic return between them).
__declspec(naked) char __fastcall query_site(void*,void*,int,int*) { __asm {
    pop dword ptr [query_return]
    mov edx, [ecx]
    mov eax, 5dae69h
    jmp eax
} }
__declspec(naked) void query_done() { __asm { jmp dword ptr [query_return] } }
void query_and_mutate() {
    ++mutation;
    reinterpret_cast<unsigned(__cdecl*)()>(0x560090)();
    int controller=-1;
    if(query_site(menus_image,nullptr,0x13,&controller)) ++actual_pauses;
}
void __fastcall menu_update(void*,void*,float) { ++menu_ticks;query_and_mutate(); }
void __fastcall hud_update(void*,void*,float) { ++hud_ticks; }
void __fastcall menu_draw(void*,void*) { ++draws; }
void __fastcall present(void*,void*,int) { ++presents; }
void __fastcall video_update(void*,void*,float) { ++video_updates; }
void __fastcall modal_service(void*,void*) {}
void __fastcall world_update(void*,void*) {
    ++worlds;query_and_mutate();
    const auto frame=*reinterpret_cast<unsigned short*>(native_frame);
    auto* basis=reinterpret_cast<unsigned*>(camera_object+camera_basis_at);
    basis[0]=(basis[0]*33u)^unsigned(frame+1);
    event_state=event_state*17u+frame+1;ai_state=ai_state*31u+frame+3;
}

__declspec(naked) void common_cleanup() { __asm { inc dword ptr [cleanups]
    pop esi
    ret
} }
__declspec(naked) void normal_step() { __asm {
    mov ecx, 729960h
    mov edx, [ecx]
    mov eax, 40235ah
    jmp eax
} }
__declspec(naked) void after_raw() { __asm {
    add esp, 20h
    mov eax, 55a390h
    call eax
    mov ecx, eax
    mov eax, 40219eh
    jmp eax
} }
// Poll the real patched raw site, input-manager CALL, native wait test/call/jump and common cleanup shape.
__declspec(naked) void __fastcall outer_tick(void*) { __asm {
    push esi
    mov esi, ecx
    sub esp, 20h
    push 1
    push 0
    mov ecx, 79ebc0h
    mov edx, [ecx]
    mov eax, 401e81h
    jmp eax
} }
__declspec(naked) void gate_done() { __asm { pop edi
    pop esi
    pop ebp
    pop ebx
    add esp, 0ch
    ret
} }
// Retail gate's participant walk -> buffer-count/head check -> real input-manager permission writer.
// Its time/arbitration tail is outside this fixture. Missing input still takes the retail failure path.
__declspec(naked) void gate_fragment() { __asm {
    sub esp, 0ch
    push ebx
    push ebp
    push esi
    push edi
    mov esi, 0a3bbd8h
    xor ebp, ebp
    mov bl, 1
    mov dword ptr [esp+14h], 447a0000h
    mov eax, 606ae2h
    jmp eax
} }

void queue(bool pending) {
    std::memset(command_image,0,sizeof command_image);put(command_image,0,command_vt);
    put(command_image,0x634,pending ? 0 : -1);put(command_image,0x638,pending ? 1 : -1);
    if(pending) {
        put(command_image,0x500,1);put(command_image,0x504,-1);
        put(command_image,0x588,-1);put(command_image,0x58c,0);
        std::strcpy(reinterpret_cast<char*>(command_image+0x508),"controllersContinue fixture");
        std::strcpy(reinterpret_cast<char*>(command_image+0x590),"saveload fixture");
        put(command_image,0x62c,3u);put(command_image,0x630,2u);
    }
}
void reset(bool wrapper=true) {
    join_in_progress::execution_end("fixture reset");
    if(join_in_progress::execution_log) {
        auto& log=*join_in_progress::execution_log;
        log.samples.clear();log.draws.clear();log.iteration=log.worlds=log.applied=log.retired=log.visual_count=log.last_boundary=0;
        for(auto& f:log.consumed) f=~0u;
    }
    join_in_progress::execution_wrapper=wrapper;join_in_progress::execution_observer=true;
    join_in_progress::execution.begin(true);join_in_progress::execution.image_returned();
    put(reinterpret_cast<void*>(gns_transport_rules::session_static),0,gns_transport_rules::session_vtable);
    put(reinterpret_cast<void*>(gns_transport_rules::session_static),0x420,static_cast<unsigned char>(1));
    put(reinterpret_cast<void*>(gns_transport_rules::session_static),0x278,2u);
    put(reinterpret_cast<void*>(gns_transport_rules::session_static),0x26c,nodes[0]);
    put(nodes[0],node_record,local);put(nodes[0],node_next,nodes[1]);put(nodes[1],node_record,remote);put(nodes[1],node_next,static_cast<void*>(nullptr));
    put(local,record_flags,1u);put(local,0x18,0);put(remote,record_flags,0u);put(remote,0x18,1);
    // Keep the retail vtable identity in backing storage for read-only eligibility; game_get supplies the fixture table.
    *reinterpret_cast<unsigned*>(game_static)=game_vtable;
    put(reinterpret_cast<void*>(zones_static),0,zones_vtable);put(reinterpret_cast<void*>(zones_static),zones_loading_at,0u);
    *reinterpret_cast<void**>(0x8aff18)=menus_image;put(menus_image,0x86090,static_cast<void*>(nullptr));
    *reinterpret_cast<unsigned char*>(0xa3e168+0x6c)=0;
    put(reinterpret_cast<void*>(0xa3bbd8),8,1u);put(reinterpret_cast<void*>(0xa3bbd8),0xc,1u);
    *reinterpret_cast<unsigned char*>(0xa3bc0c)=1; // gate singleton already exists; no CRT exit registration in this fixture
    put(reinterpret_cast<void*>(0xa3bbd8),6,static_cast<unsigned char>(1));
    put(reinterpret_cast<void*>(0xa3bbd8),0x24,0x44fa0000u); // gate's optional peer-clock sampling is outside this fragment
    *reinterpret_cast<unsigned short*>(native_frame)=0;
    *reinterpret_cast<unsigned char*>(0x7ac240)=1; // input manager exists; never invoke its constructor
    std::memset(reinterpret_cast<void*>(0x79fbb0),0,0xc690);
    *reinterpret_cast<unsigned char*>(0x79fbb0+0xc68c)=1;
    controller_object[0]=unsigned(reinterpret_cast<std::uintptr_t>(controller_vt2));
    controller_object[1]=2;controller_object[2]=3;controller_object[3]=0;controller_object[4]=1;
    for(unsigned k=0;k<4;++k) { put(reinterpret_cast<void*>(0x79ebc0+0x3c+0x390*k),0x2f8,0u);put(reinterpret_cast<void*>(0x79ebc0+0x3c+0x390*k),0x374,0u); }
    *reinterpret_cast<unsigned*>(rng_seed)=0x12345678;*reinterpret_cast<unsigned*>(rng_count)=0;
    std::memset(reinterpret_cast<void*>(camera_object+camera_basis_at),0,camera_basis_bytes);
    consumed_step=event_state=ai_state=0;
    video_updates=0;put(video,0x403c,0u);put(video,0x1575,static_cast<unsigned char>(0));
    *reinterpret_cast<void**>(0x8b13ec)=video;
    std::memset(client,0,sizeof client);queue(false);command_load=command_end=false;message=true;
    polls=produces=feedback=seat_ticks=manager_ticks=menu_ticks=hud_ticks=draws=presents=worlds=cleanups=sleeps=0;
    controller_commands=save_commands=service_order=mutation=actual_pauses=0;
    join_in_progress::execution_wait_state();
    assert(join_in_progress::execution.phase==restart_execution_phase::world_ready);
}
void packet(unsigned index,unsigned frame,unsigned buttons) {
    auto* block=reinterpret_cast<void*>(0x79fbb0+index*gns_transport_rules::input_block_size);
    const auto read=join_in_progress::at<unsigned>(block,gns_transport_rules::input_read);
    put(block,gns_transport_rules::input_write,(read+1)%300);
    put(block,gns_transport_rules::input_records+40*read,1u);
    put(block,gns_transport_rules::input_records+40*read+4,static_cast<unsigned short>(frame));
    put(block,gns_transport_rules::input_records+40*read+6,static_cast<unsigned short>(33));
    put(block,gns_transport_rules::input_records+40*read+8,buttons);
}
void install() {
    join_in_progress::execution_observer=true;join_in_progress::execution_wrapper=true;
    assert(join_in_progress::mismatch()==nullptr);
    // Every optional contract byte refuses before ANY write, including the complete call and its cleanup context.
    for(const auto& group:{std::span<const guard>(execution_wrapper_guards),std::span<const guard>(execution_observer_guards)})
        for(const auto& g:group) for(unsigned k=0;k<g.hex.size()/2;++k) {
            auto* p=reinterpret_cast<unsigned char*>(g.va+k);*p^=1;assert(join_in_progress::mismatch()!=nullptr);*p^=1;
        }
    std::vector<xml1_hook_transaction::patch> writes;
    join_in_progress::execution_observer=join_in_progress::execution_wrapper=false;
    join_in_progress::execution_add_patches(writes);assert(writes.empty());
    join_in_progress::execution_observer=join_in_progress::execution_wrapper=true;
    join_in_progress::execution_add_patches(writes);
    writes.push_back({frame_gate_run_call,join_in_progress::bytes(frame_gate_run_original),join_in_progress::rel(0xe8,frame_gate_run_call,reinterpret_cast<const void*>(&join_in_progress::execution_gate_run_bridge),5)});
    writes.push_back({draw_entry,join_in_progress::bytes(draw_entry_original),join_in_progress::rel(0xe9,draw_entry,reinterpret_cast<const void*>(&join_in_progress::draw_trace_bridge),6)});
    xml1_hook_transaction::memory memory;
    bool safe=true;
    // A wrong expected byte fails the whole plan without a partial write.
    auto bad=writes;bad.back().before[0]^=1;
    assert(!xml1_hook_transaction::apply(memory,bad,&safe) && safe);
    assert(std::memcmp(reinterpret_cast<void*>(execution_stall),execution_stall_original.data(),5)==0);
    assert(xml1_hook_transaction::apply(memory,writes,&safe) && safe);
    for(const auto& p:writes) assert(std::memcmp(reinterpret_cast<void*>(p.address),p.after.data(),p.after.size())==0);
    std::cout<<"PASS AR5 installed transaction: every optional guard byte, OFF no writes, atomic mismatch and exact readback\n";

    redirect(clock_getter,clock_get);redirect(controllers_getter,controller_get);redirect(seats_getter,seat_get);
    redirect(0x401b00,clock_get); // native retirement's local timer setter (fixture-owned clock)
    redirect(0x55c890,command_get);redirect(0x4b2880,manager_get);redirect(scheduler_getter,scheduler_get);
    redirect(native_stall_message,message_get);redirect(0x5eb300,video_get);redirect(0x5d8920,menu_get);
    redirect(0x59ee20,hud_get);redirect(0x585e70,display_get);redirect(game_getter,game_get);redirect(session_getter,session_get);
    redirect(zones_getter,zones_get);redirect(manager_getter,cohort_manager_get);redirect(pause_getter,raw_pause_get);
    redirect(network_setup,network_setup_fake);redirect(campaign_begin,native_restart);
    redirect(0x559700,produce);redirect(0x550fde,writer_done);
    redirect(0x5dae6f,query_done);redirect(0x4021c5,normal_step);redirect(0x402360,common_cleanup);redirect(0x4028db,common_cleanup);
    redirect(0x606b9d,gate_done);redirect(0x606cc6,gate_done);
    put(reinterpret_cast<void*>(0x67f148),0,&std::strncpy);put(reinterpret_cast<void*>(0x67f130),0,static_cast<const char*(__cdecl*)(const char*,const char*)>(&std::strstr));
    put(fake_clock,0,clock_vt);clock_vt[0x28/4]=unsigned(reinterpret_cast<std::uintptr_t>(&seconds));
    clock_vt[0x30/4]=clock_vt[0x3c/4]=unsigned(reinterpret_cast<std::uintptr_t>(&set_step));
    controller_vt2[0x14/4]=unsigned(reinterpret_cast<std::uintptr_t>(&raw_poll));
    controller_vt2[0x24/4]=unsigned(reinterpret_cast<std::uintptr_t>(&apply_controller));
    controller_vt2[0x28/4]=unsigned(reinterpret_cast<std::uintptr_t>(&no));
    controller_vt2[0x70/4]=unsigned(reinterpret_cast<std::uintptr_t>(&count));
    controller_vt2[0x84/4]=unsigned(reinterpret_cast<std::uintptr_t>(&feedback_set));
    seat_vt2[0x28/4]=unsigned(reinterpret_cast<std::uintptr_t>(&seats_service));
    manager_vt[0]=unsigned(reinterpret_cast<std::uintptr_t>(&manager_service));put(native_manager,0,manager_vt);
    command_vt[1]=0x55c320;command_vt[6]=unsigned(reinterpret_cast<std::uintptr_t>(&execute_command));
    put(video,0,0x6a332cu); // real retail +0x78 read-only active-video predicate
    put(reinterpret_cast<void*>(0x6a332c),4,&video_update);put(reinterpret_cast<void*>(0x6a332c),0x30,&no);
    put(menus_image,0,menu_vt);menu_vt[0x1c/4]=unsigned(reinterpret_cast<std::uintptr_t>(&menu_update));
    menu_vt[0xa8/4]=unsigned(reinterpret_cast<std::uintptr_t>(&menu_draw));menu_vt[0x134/4]=unsigned(reinterpret_cast<std::uintptr_t>(&query));
    menu_vt[0x204/4]=unsigned(reinterpret_cast<std::uintptr_t>(&no));menu_vt[0x104/4]=unsigned(reinterpret_cast<std::uintptr_t>(&modal_service));
    put(hud,0,hud_vt);hud_vt[0x1c/4]=unsigned(reinterpret_cast<std::uintptr_t>(&hud_update));
    put(display_image,0,display_vt);display_vt[0x14/4]=unsigned(reinterpret_cast<std::uintptr_t>(&present));
    // Both required game virtual callbacks are replaced in the mapped vtable, preserving its identity.
    put(reinterpret_cast<void*>(game_vtable),0x34,&no);put(reinterpret_cast<void*>(game_vtable),0x254,&yes);
    put(reinterpret_cast<void*>(game_vtable),0x16c,&world_update);
    put(reinterpret_cast<void*>(game_vtable),0x20c,&load_mode2);
    put(reinterpret_cast<void*>(zones_vtable),zones_loading_slot,&zone_loading);
    join_in_progress::execution_raw_resume=unsigned(reinterpret_cast<std::uintptr_t>(&after_raw));
    join_in_progress::execution_sleep=&yield;
    join_in_progress::execution_log=new join_in_progress_diagnostics::execution_capture();
}
void run() {
    install();reset(false);
    outer_tick(client);
    assert(menu_ticks==1 && hud_ticks==1 && draws==1 && presents==1 && mutation==1 && worlds==0 && cleanups==1 && sleeps==0);
    std::cout<<"PASS AR5 unfixed/observer-only control: actual stalled helper runs menu/HUD and mutates digest; normal world bypassed\n";
    reset();queue(true);
    const auto waiting_samples=frame_rate::fixture_shared_starts;
    const auto notice_polls=network_notice_polls;
    for(unsigned i=0;i<20;++i) outer_tick(client);
    assert(frame_rate::fixture_shared_starts==waiting_samples); // protected waits are never local-work samples
    assert(menu_ticks==0 && hud_ticks==0 && draws==0 && presents==0 && mutation==0 && worlds==0 && cleanups==20 && polls==20 && sleeps==20);
    assert(feedback==4 && seat_ticks==40 && manager_ticks==20);
    assert(controller_commands==1 && save_commands==1 && service_order==12 && join_in_progress::at<int>(command_image,0x634)==-1);
    assert(!join_in_progress::execution.canonical_seen);
    std::cout<<"PASS AR5 pre-canonical waits: native prefix, first-stall feedback, real filtered pending queues exactly once/in order, manager/seat service, raw polling, cleanup and bounded yield; no render/world callbacks\n";
    assert(message && network_notice_polls==notice_polls+20 && presents==0);
    // A42 is a pre-Present observer. An active native notice does not authorize
    // rendering during a protected wait; a modal resumes the native tail.
    put(menus_image,0x86090,modal);outer_tick(client);
    assert(network_notice_polls==notice_polls+21 && presents==1);
    std::cout<<"PASS beta.2 A42 + AR5: active notice polling survives protected waits, pre-Present lane stays suppressed, modal resumes presentation\n";
    for(unsigned phase:{1u,2u}) for(unsigned mode:{0u,1u}) for(unsigned frame:{0u,1u,2u,5u,65535u}) {
        reset();put(reinterpret_cast<void*>(0xa3bbd8),0xc,phase);put(reinterpret_cast<void*>(0xa3bbd8),6,static_cast<unsigned char>(mode));
        *reinterpret_cast<unsigned short*>(native_frame)=static_cast<unsigned short>(frame);
        for(unsigned n=0;n<20;++n) outer_tick(client);
        assert(!mutation && !worlds && polls==20 && sleeps==20);
    }
    std::cout<<"PASS AR5 gate/resync both modes: frame 0/1/2/5/wrap with 20 waits each, no timeout or frame-window permission\n";
    for(unsigned reason=0;reason<6;++reason) {
        reset();
        if(reason==0) put(reinterpret_cast<void*>(0xa3bbd8),8,5u);
        if(reason==1) put(reinterpret_cast<void*>(0xa3bbd8),0xc,3u);
        if(reason==2) put(menus_image,0x86090,modal);
        if(reason==3) put(reinterpret_cast<void*>(zones_static),zones_loading_at,3u);
        if(reason==4) *reinterpret_cast<unsigned char*>(0x79fbb0+0xc688)=1;
        if(reason==5) *reinterpret_cast<void**>(0x8aff18)=nullptr;
        outer_tick(client);assert(menu_ticks==1 && hud_ticks==1 && presents==1);
    }
    std::cout<<"PASS AR5 exclusions: unknown bit/phase, modal overlap, load overlap and contradictory input permission retain native render\n";
    reset();put(menus_image,0x86090,modal);outer_tick(client);
    put(menus_image,0x86090,static_cast<void*>(nullptr));outer_tick(client);
    put(menus_image,0x86090,modal);outer_tick(client);
    assert(menu_ticks==2 && presents==2);
    std::cout<<"PASS AR5 modal replacement: close and reuse of the same object never grants stale authorization to bypass native service\n";
    for(unsigned action=0;action<2;++action) {
        reset();queue(true);command_load=action==0;command_end=action==1;
        outer_tick(client);assert(save_commands==1 && controller_commands==1 && menu_ticks==1 && presents==1);
        if(action==1) assert(join_in_progress::execution.phase==restart_execution_phase::off);
    }
    std::cout<<"PASS AR5 queued lifecycle changes: a load/session end in the retained prefix is reclassified before the render decision\n";
    reset();message=false;put(reinterpret_cast<void*>(0xa3bbd8),6,static_cast<unsigned char>(0));outer_tick(client);
    assert(!menu_ticks && !presents && sleeps==1 && manager_ticks==1);
    std::cout<<"PASS AR5 native no-message/no-render branch still services prefix and yields once\n";
    reset();put(video,0x1575,static_cast<unsigned char>(1));outer_tick(client);
    if(!(video_updates==1 && hud_ticks==1 && presents==1)) {
        std::cerr<<"FAIL AR5 active-video exclusion: video="<<video_updates<<" hud="<<hud_ticks<<" display="<<presents<<"\n";ExitProcess(1);
    }
    std::cout<<"PASS AR5 active video is a native modal even with an empty current-menu stack\n";

    for(bool existing_member:{false,true}) {
        reset();join_in_progress::execution.clear();
        join_in_progress::epoch.begin();assert(join_in_progress::epoch.request());
        join_in_progress::late_restart=true;join_in_progress::existing_cohort_member=existing_member;
        join_in_progress::restart_reset_enabled=false;join_in_progress::deferred_start_task=0;
        const auto entry=restart_entries,loads=mode2_loads,setups=network_setups;
        join_in_progress::deferred_campaign_start(nullptr);
        assert(restart_entries==entry+1 && mode2_loads==loads+1 && network_setups==setups+unsigned(existing_member));
        assert(join_in_progress::execution.restored && join_in_progress::execution.phase==restart_execution_phase::restoring);
        join_in_progress::epoch.active=false;join_in_progress::late_restart=false;
        join_in_progress::observe_epoch();assert(join_in_progress::execution.phase==restart_execution_phase::restoring);
        *reinterpret_cast<unsigned char*>(zones_static+zones_loading_at)=0;
        std::strcpy(reinterpret_cast<char*>(modal+0xc),"start");put(menus_image,0x86090,modal);
        join_in_progress::observe_epoch();assert(join_in_progress::execution.phase==restart_execution_phase::restoring);
        put(menus_image,0x86090,static_cast<void*>(nullptr));join_in_progress::observe_epoch();
        assert(join_in_progress::execution.phase==restart_execution_phase::world_ready);
        outer_tick(client);assert(!menu_ticks && !worlds);
        join_in_progress::zone_load_entry_notice();assert(join_in_progress::execution.phase==restart_execution_phase::off);
    }
    join_in_progress::existing_cohort_member=false;
    std::cout<<"PASS AR5 production lifecycle: deferred late entry -> native start notice -> mode-2 load return -> zone/start-menu completion after epoch/late-marker clear; both roles protect pre-canonical wait, ordinary area load clears\n";

    reset();
    // Missing remote frame takes the native failure branch; matching input takes its real permit writer.
    const auto gate_samples=frame_rate::fixture_shared_starts;
    put(reinterpret_cast<void*>(0xa3bbd8),8,0u);gate_fragment();
    assert(frame_rate::fixture_shared_starts==gate_samples);
    assert(*reinterpret_cast<unsigned char*>(0x79fbb0+0xc688)==0 && (*reinterpret_cast<unsigned*>(0xa3bbe0)&1));
    packet(0,0,0);packet(1,0,0x80000);put(reinterpret_cast<void*>(0xa3bbd8),8,0u);
    join_in_progress::restart_reset_enabled=false;join_in_progress::restart.arm();
    gate_fragment();
    assert(*reinterpret_cast<unsigned char*>(0x79fbb0+0xc688)==1 && join_in_progress::execution.canonical_seen);
    assert(frame_rate::fixture_shared_starts==gate_samples+1);
    outer_tick(client);
    assert(frame_rate::fixture_shared_starts==gate_samples+1); // helper/render callbacks never restart the meter
    std::cout<<"PASS beta.2 AR5 + linked rate: protected waits and failed gate do not sample; successful canonical gate starts exactly one sample before world execution\n";
    assert(worlds==1 && mutation==1 && actual_pauses==1 && !menu_ticks && !hud_ticks && cleanups==1);
    int logical=-1;
    assert(query(menus_image,nullptr,0x13,&logical) && logical==1);
    assert(query(menus_image,nullptr,0x13,&logical) && logical==1); // getter does not consume the edge
    assert(*reinterpret_cast<unsigned*>(0x79ebc0+0x3c+3*0x390+0x2f8)==0x80000);
    put(reinterpret_cast<void*>(0x79ebc0+0x3c+3*0x390),0x2f8,0u);
    unsigned real_press=0x80000;float analog[30]{};
    apply_controller(controller_object,nullptr,0,&real_press,analog);
    logical=-1;assert(query(menus_image,nullptr,0x13,&logical) && logical==0);
    assert(*reinterpret_cast<unsigned*>(0x79ebc0+0x3c+2*0x390+0x2f8)==0x80000);
    std::cout<<"PASS AR5 native gate and shared controller application: frame-0 canonical once, permit/input CALL, real button writer/getter; one unblocking world step and genuine early ESC retained, repeated edge reads unchanged\n";
    // The native input retirement writer is separately reached on this accepted frame.
    reinterpret_cast<void(__thiscall*)(void*)>(0x559190)(reinterpret_cast<void*>(0x79fbb0));
    assert(*reinterpret_cast<unsigned*>(0x79fbb0+gns_transport_rules::input_read)==1);
    assert(*reinterpret_cast<unsigned*>(0x79fbb0+gns_transport_rules::input_block_size+gns_transport_rules::input_read)==1);
    bool saw_retire=false,saw_query=false,saw_apply=false;
    for(unsigned k=0;k<join_in_progress::execution_log->samples.size;++k) {
        const auto& q=join_in_progress::execution_log->samples.entries[k];
        saw_retire|=q.event==unsigned(join_in_progress_diagnostics::boundary::retire);
        saw_query|=q.event==unsigned(join_in_progress_diagnostics::boundary::query_after) && q.result && q.controller==1;
        saw_apply|=q.event==unsigned(join_in_progress_diagnostics::boundary::apply_after);
    }
    assert(saw_retire && saw_query && saw_apply);
    std::cout<<"PASS AR5 observer: actual applied controller, non-consuming query and per-record retirement boundaries are distinct\n";
    observer_abi();
    assert(abi_before==abi_after && abi_ebx==0x12345678 && abi_esi==0x23456789 && abi_edi==0x3456789a);
    // FXSAVE reserved bytes are undefined; compare control/status/tag/MXCSR, all x87 registers and SSE registers.
    assert(std::memcmp(fpu_before,fpu_after,5)==0 && std::memcmp(fpu_before+24,fpu_after+24,8)==0 && std::memcmp(fpu_before+32,fpu_after+32,256)==0);
    std::cout<<"PASS AR5 observer ABI: stack/nonvolatile registers and x87/SSE state preserved\n";
    unsigned owner_probe[32]{};owner_probe[0x40/4]=7;
    const auto saved_descriptor=entity_capacity::fixture_descriptor;
    entity_capacity::fixture_descriptor.world_object=unsigned(reinterpret_cast<std::uintptr_t>(owner_probe));
    entity_capacity::fixture_descriptor.world_core.live_count=0x40;
    join_in_progress::execution_snapshot(join_in_progress_diagnostics::boundary::input_after);
    const auto observed_live=join_in_progress::execution_log->samples.entries[join_in_progress::execution_log->samples.size-1].world_live;
    entity_capacity::fixture_descriptor=saved_descriptor;
    if(observed_live!=7) { std::cerr<<"FAIL AR5 installed World descriptor: expected live=7 observed="<<observed_live<<"\n";++diagnostic_failures; }
    std::cout<<"PASS AR5 World summary reads installed owner/offset without a native getter\n";
    using digest=std::array<unsigned,8>;
    auto state_digest=[&]() -> digest {
        return {worlds,*reinterpret_cast<unsigned*>(rng_seed),*reinterpret_cast<unsigned*>(rng_count),
            *reinterpret_cast<unsigned*>(camera_object+camera_basis_at),consumed_step,event_state,ai_state,actual_pauses};
    };
    auto history=[&](unsigned shape,unsigned arrival) {
        reset();queue(true);std::array<digest,6> states{};
        for(unsigned f=0;f<6;++f) {
            *reinterpret_cast<unsigned short*>(native_frame)=static_cast<unsigned short>(f);
            put(reinterpret_cast<void*>(0xa3bbd8),8,1u);put(reinterpret_cast<void*>(0xa3bbd8),0xc,1u+(f%2));
            reinterpret_cast<void(__thiscall*)(void*,int)>(input_manager_run)(reinterpret_cast<void*>(0x79fbb0),0);
            const auto before=state_digest();
            const unsigned waits=(arrival ? (f==1 ? 20u : 1u) : (f==0 ? 20u : 0u))+(shape==3 && f==5 ? 200u : shape);
            for(unsigned n=0;n<waits;++n) { outer_tick(client);assert(state_digest()==before); }
            packet(0,f,0);packet(1,f,f==1 ? 0x80000u : 0u);
            put(reinterpret_cast<void*>(0xa3bbd8),8,0u);
            if(f==0) join_in_progress::restart.arm();
            gate_fragment();assert(*reinterpret_cast<unsigned char*>(0x79fbb0+0xc688)==1);
            const auto samples_before=join_in_progress::execution_log->samples.size;
            outer_tick(client);
            for(unsigned k=samples_before;k<join_in_progress::execution_log->samples.size;++k) {
                const auto& sample=join_in_progress::execution_log->samples.entries[k];
                if(sample.event==unsigned(join_in_progress_diagnostics::boundary::apply_after) && sample.input_frame!=f) {
                    std::cerr<<"FAIL AR5 applied input frame: expected="<<f<<" observed="<<sample.input_frame<<"\n";++diagnostic_failures;
                }
            }
            reinterpret_cast<void(__thiscall*)(void*)>(0x559190)(reinterpret_cast<void*>(0x79fbb0));
            assert(worlds==f+1 && menu_ticks==0 && hud_ticks==0 && presents==0 && consumed_step!=0);
            states[f]=state_digest();
        }
        assert(controller_commands==1 && save_commands==1 && actual_pauses==1);
        return states;
    };
    for(unsigned shape=0;shape<4;++shape) {
        const auto host_first=history(shape,0),guest_first=history(shape,1);
        assert(host_first==guest_first);
    }
    std::cout<<"PASS AR5 paired fixture schedules: four loss-shape schedules/both arrival orders, native input application+retirement, six accepted world steps with equal RNG sequence/count, camera basis, consumed clock step and fixture event/AI state; 200 later waits and real frame-1 ESC\n";
    join_in_progress::execution_end("fixture teardown");assert(join_in_progress::execution.phase==restart_execution_phase::off);
    assert(!join_in_progress::execution_log->samples.size);
    std::cout<<"PASS AR5 teardown: bounded diagnostic records flushed to existing logger, generation cleared\n";
    if(diagnostic_failures) { std::cerr<<"FAIL AR5 diagnostic checks="<<diagnostic_failures<<"\n";ExitProcess(1); }
}
}
