// In restart_fixture: exercise production observers with the retail CALL/JMP stack shapes.
unsigned audit_native_calls=0,audit_native_arg=0,audit_native_this=0;
unsigned audit_vtable[0x180/4]{},audit_query=0x12345678;
const void* audit_bridge_target=nullptr;void* audit_entity=nullptr;
const void* audit_observer_target=reinterpret_cast<const void*>(&join_in_progress::audit_observe_bridge);
__declspec(naked) void audit_observer_probe(){__asm{
    mov ecx,audit_entity
    push 0
    call dword ptr [audit_observer_target]
    ret
}}
__declspec(naked) void audit_resume_stub(){__asm{ret}}
__declspec(naked) void audit_native_active(){__asm{
    mov audit_native_this,ecx
    mov eax,[esp+4]
    mov audit_native_arg,eax
    inc audit_native_calls
    ret 4
}}
__declspec(naked) void audit_native_spawn(){__asm{
    mov audit_native_this,ecx
    mov eax,[esp+4]
    mov audit_native_arg,eax
    inc audit_native_calls
    ret 8
}}
__declspec(naked) void audit_native_projection(){__asm{mov eax,1
    ret 12
}}
void __cdecl audit_native_remove(unsigned token){++audit_native_calls;audit_native_arg=token;}
__declspec(naked) void audit_activation_probe(){__asm{
    push esi
    mov ecx,audit_entity
    lea edx,audit_vtable
    lea esi,audit_query
    push offset done
    push 1
    jmp dword ptr [audit_bridge_target]
done:
    pop esi
    ret
}}
__declspec(naked) void audit_spawn_probe(){__asm{
    mov ecx,audit_entity
    lea eax,audit_vtable
    push 0
    push 0
    call dword ptr [audit_bridge_target]
    ret
}}
__declspec(naked) void audit_projection_probe(){__asm{
    push ebp
    mov ebp,audit_entity
    mov ecx,audit_entity
    lea edx,audit_vtable
    push offset done
    push 0
    push 0
    push 0
    jmp dword ptr [audit_bridge_target]
done:
    pop ebp
    ret
}}
void run_audit_probe(){
    using namespace join_in_progress;using namespace join_in_progress_rules;
    auto* entity=fake_entities[1];auto* hero=fake_entities[2];
    put(entity,0x1c,0x1801u);put(entity,0,actor_vtable);put(entity,entity_registered_at,1u);
    put(entity,entity_flags_at,1u<<entity_active_flag);put(hero,entity_registered_at,1u);
    std::memcpy(fake_world_object+0x756c+32,hero+entity_flags_at,entity_flag_bytes);
    put(fake_world_object,0x756c+16,0u);put(fake_world_object,0x8570+16*32,4u);
    anchor_vtable[game_party_list_slot/4]=unsigned(reinterpret_cast<std::uintptr_t>(&moved_party));
    probe_enabled=true;probe_epoch_late=true;late_restart=true;restart.arm();audit_zone_load();
    const auto first=observed_jip_logs.size();
    probe_audit_frame(1,false);probe_audit_frame(30,false);
    assert(observed_jip_logs.size()==first&&audit_samples==0&&audit_collecting); // no samples before the epoch
    probe_audit_frame(0,true);probe_audit_frame(0,true);probe_audit_frame(2,false);
    assert(observed_jip_logs.size()==first+1&&observed_jip_logs.back().find("diff=1[1:00010000/00000000/0]")!=std::string::npos);
    assert(notify_npc_flags(fake_world_object,entity,1));
    audit_entity=entity;audit_vtable[0x5c/4]=unsigned(reinterpret_cast<std::uintptr_t>(&audit_native_active));
    audit_vtable[0xd4/4]=unsigned(reinterpret_cast<std::uintptr_t>(&audit_native_spawn));
    audit_vtable[0x178/4]=unsigned(reinterpret_cast<std::uintptr_t>(&audit_native_projection));
    audit_activate_resume_at=unsigned(reinterpret_cast<std::uintptr_t>(&audit_resume_stub));
    audit_deactivate_resume_at=audit_activate_resume_at;audit_shy_resume_at=audit_activate_resume_at;
    audit_remove_at=unsigned(reinterpret_cast<std::uintptr_t>(&audit_native_remove));
    audit_bridge_target=reinterpret_cast<const void*>(&audit_activate_bridge);audit_native_calls=0;
    audit_activation_probe();assert(audit_native_calls==1&&audit_native_arg==1&&audit_native_this==unsigned(reinterpret_cast<std::uintptr_t>(entity))&&audit_counts[0]==1);
    audit_bridge_target=reinterpret_cast<const void*>(&audit_deactivate_bridge);audit_activation_probe();assert(audit_counts[1]==1);
    reinterpret_cast<void(__cdecl*)(unsigned)>(&audit_remove_bridge)(0x1801);assert(audit_counts[2]==1&&audit_native_arg==0x1801);
    audit_bridge_target=reinterpret_cast<const void*>(&audit_actonvisible_bridge);audit_spawn_probe();assert(audit_counts[3]==1&&audit_native_arg==0);
    audit_bridge_target=reinterpret_cast<const void*>(&audit_shy_bridge);audit_projection_probe();assert(audit_counts[4]==1&&audit_counts[5]==0);
    std::uint32_t saved[13]{};saved[10]=4;saved[2]=unsigned(reinterpret_cast<std::uintptr_t>(entity));audit_note_saved(saved);assert(audit_counts[5]==1);
    const auto entity_before=std::vector<unsigned char>(entity,entity+0x900);
    const auto world_before=std::vector<unsigned char>(fake_world_object,fake_world_object+sizeof fake_world_object);
    probe_audit_frame(1,false);probe_audit_frame(1,false);
    assert(observed_jip_logs.size()==first+2&&observed_jip_logs.back().find("diff=0[none] vis=1/1/1/1/1/1")!=std::string::npos&&observed_jip_logs.back().find("ids=1[1:3f]")!=std::string::npos);
    probe_audit_frame(30,false);probe_audit_frame(30,false);assert(observed_jip_logs.size()==first+3&&!audit_collecting);
    assert(std::memcmp(entity_before.data(),entity,0x900)==0&&std::memcmp(world_before.data(),fake_world_object,sizeof fake_world_object)==0);
    probe_enabled=false;audit_collecting=true;const auto h=audit_vis_hash;audit_note_saved(saved);probe_audit_frame(0,true);
    assert(audit_vis_hash==h&&observed_jip_logs.size()==first+3);
    probe_enabled=true;probe_epoch_late=false;audit_note_saved(saved);probe_audit_frame(0,true);assert(audit_vis_hash==h&&observed_jip_logs.size()==first+3);
    probe_epoch_late=true;
    // Observer preserves live x87 and SSE data/control, including a nonempty x87 stack, and EFLAGS.
    alignas(16) unsigned char fp_before[512]{},fp_after[512]{};unsigned flags_before=0,flags_after=0;
    __asm {
        fld1
        pcmpeqd xmm0,xmm0
        stc
        fxsave fp_before
        pushfd
        pop flags_before
        call audit_observer_probe
        pushfd
        pop flags_after
        fxsave fp_after
        fstp st(0)
    }
    assert(std::memcmp(fp_before,fp_after,sizeof fp_before)==0&&flags_before==flags_after);
    late_restart=false;audit_collecting=true;audit_zone_load(); // an ordinary new zone cancels this trace
    assert(!audit_collecting);probe_audit_frame(0,true);assert(observed_jip_logs.size()==first+3);
    probe_enabled=false;audit_collecting=false;restart.disarm();late_restart=false;
    std::cout<<"PASS audit probe: production bridge call shapes/arguments; flag mismatch detection and repair; six visibility/spawner categories; exactly epoch/1/30 samples; party excluded; read-only; disabled/no-late-epoch no-ops\n";
}
