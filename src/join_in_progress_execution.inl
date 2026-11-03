// AR5 Stage 0/1. Included in the game-thread namespace. No getter, allocation,
// formatting, file I/O or floating-point arithmetic on the observed paths.
bool execution_in_helper = false;
bool execution_skipped = false;
unsigned execution_skips = 0, execution_yields = 0, execution_last_modal = 0, execution_modal_serial = 0;
// Mutable destinations are used only by the mapped-retail fixture.
std::uint32_t execution_helper_at = 0x402920, execution_render_at = 0x5eb300;
std::uint32_t execution_render_resume = 0x402a0e, execution_render_exit = 0x402b50;
std::uint32_t execution_raw_resume = 0x401e89, execution_apply_resume = 0x558db3, execution_retire_resume = 0x5594d6;
std::uint32_t execution_video_resume = 0x402a1e, execution_ordinary_resume = 0x402a81;
std::uint32_t execution_pda_resume = 0x5cec09, execution_pause_resume = 0x60c237;
void (WINAPI* execution_sleep)(DWORD) = &Sleep;

bool execution_video_state(bool& active)
{
    const auto* video = *reinterpret_cast<void* const*>(0x8b13ec);
    if (!video || at<unsigned>(video,0) != 0x6a332c) return false;
    int index = at<int>(video,0x403c);
    if (index < 0 || index >= 3) index = 0;
    // Exact read-only predicate 0x5e9e30; never call its lazy singleton getter.
    active = (at<std::uint8_t>(video,0x1575 + unsigned(index)*0x1560) & 1) != 0;
    return true;
}

join_in_progress_core::restart_wait execution_wait_state()
{
    join_in_progress_core::restart_wait w;
    __try {
        // Singleton backing storage only: none of these reads constructs an object.
        const auto* s = reinterpret_cast<const void*>(gns_transport_rules::session_static);
        w.session = at<unsigned>(s, 0) == gns_transport_rules::session_vtable &&
            at<std::uint8_t>(s, 0x420) && at<unsigned>(s, 0x278) &&
            *reinterpret_cast<const unsigned*>(game_static) == game_vtable;
        // Do not erase restoring state during the native restart's temporary session shutdown.
        if (!w.session && execution.phase == join_in_progress_core::restart_execution_phase::world_ready) execution.clear();
        w.flags = *reinterpret_cast<const unsigned*>(0xa3bbe0);
        w.phase = *reinterpret_cast<const unsigned*>(0xa3bbe4);
        w.mode = *reinterpret_cast<const std::uint8_t*>(0xa3bbde);
        w.run_permission = *reinterpret_cast<const std::uint8_t*>(0x79fbb0 + 0xc688);
        w.loading = (*reinterpret_cast<const unsigned*>(zones_static) != zones_vtable) ||
            ((*reinterpret_cast<const std::uint8_t*>(zones_static + zones_loading_at) & 3) != 0);
        const auto menus = *reinterpret_cast<const std::uintptr_t*>(0x8aff18);
        // An absent menu manager is unclassified, not evidence of an empty stack.
        if (!menus) return w;
        const auto menu = at<std::uintptr_t>(reinterpret_cast<void*>(menus), 0x86090);
        const bool start = menu && std::strncmp(reinterpret_cast<const char*>(menu + 0xc), "start", 6) == 0;
        w.modal = menu != 0 || *reinterpret_cast<const std::uint8_t*>(0xa3e168 + 0x6c) != 0;
        bool video = false;
        if (!execution_video_state(video)) return w;
        w.modal |= video;
        w.loading |= start;
        w.readable = true;
        execution.observe_ready(w.readable && w.session, w.loading, start);
    } __except (EXCEPTION_EXECUTE_HANDLER) { w.readable = false; }
    return w;
}

void execution_snapshot(execution_boundary b, const std::uint32_t* saved)
{
    if (!execution_log || execution.phase == join_in_progress_core::restart_execution_phase::off) return;
    auto& log = *execution_log;
    log.last_boundary = unsigned(b);
    if (b == execution_boundary::raw_before) { ++log.iteration; log.applied = log.retired = 0; }
    if (b == execution_boundary::world_before) ++log.worlds;
    if (b == execution_boundary::apply_after) ++log.applied;
    if (b == execution_boundary::retire) ++log.retired;
    auto* out = log.samples.append();
    if (!out) return;
    auto& q = *out; q = {};
    q.iteration = log.iteration; q.generation = execution.generation; q.event = unsigned(b);
    q.worlds = log.worlds; q.applied = log.applied; q.retired = log.retired; q.helper = execution_in_helper;
    __try {
        q.frame = *reinterpret_cast<const std::uint16_t*>(native_frame);
        q.flags = *reinterpret_cast<const unsigned*>(0xa3bbe0); q.phase = *reinterpret_cast<const unsigned*>(0xa3bbe4);
        q.mode = *reinterpret_cast<const std::uint8_t*>(0xa3bbde);
        q.run = *reinterpret_cast<const std::uint8_t*>(0x79fbb0 + 0xc688);
        q.sync_seed = *reinterpret_cast<const unsigned*>(rng_seed); q.sync_count = *reinterpret_cast<const unsigned*>(rng_count);
        q.visual_seed = *reinterpret_cast<const unsigned*>(0x7ad8b0); q.visual_count = log.visual_count;
        q.game_time = *reinterpret_cast<const unsigned*>(game_static + game_time_at);
        // +8..+3c: pause tick, compensation, paused byte, pending step, fixed step, fixed tick.
        std::memcpy(q.clock, reinterpret_cast<const void*>(0x7ac248 + 8), sizeof q.clock);
        const bool paused = (q.clock[6] & 255) != 0;
        const bool fixed = (q.clock[10] | q.clock[11]) != 0;
        if (paused || fixed) {
            const unsigned k = paused ? 0 : 12;
            const auto tick = (std::uint64_t(q.clock[k+1]) << 32) | q.clock[k];
            const auto compensation = (std::uint64_t(q.clock[3]) << 32) | q.clock[2];
            const auto effective = tick - compensation;
            q.effective_time[0] = unsigned(effective); q.effective_time[1] = unsigned(effective >> 32);
            q.readable |= 2; // native effective ticks without calling a clock getter
        }
        std::memcpy(q.camera, reinterpret_cast<const void*>(camera_object + 0x104), 12);
        std::memcpy(q.camera + 3, reinterpret_cast<const void*>(camera_object + 0xe0), 12);
        std::memcpy(q.camera + 6, reinterpret_cast<const void*>(camera_object + camera_basis_at), 36);
        for (unsigned i = 0; i < 4; ++i) {
            q.mapping[i] = *reinterpret_cast<const unsigned*>(0x79ebc0 + 4 + 4*i);
            if (q.mapping[i] < 4) {
                const auto object = 0x79ebc0 + 0x3c + 0x390*q.mapping[i];
                q.current[i] = *reinterpret_cast<const unsigned*>(object + 0x2f8);
                q.previous[i] = *reinterpret_cast<const unsigned*>(object + 0x374);
            } else { q.current[i] = q.previous[i] = ~0u; }
            const auto block = 0x79fbb0 + i*gns_transport_rules::input_block_size;
            q.read[i] = *reinterpret_cast<const unsigned*>(block + gns_transport_rules::input_read);
            q.write[i] = *reinterpret_cast<const unsigned*>(block + gns_transport_rules::input_write);
            if (q.read[i] < 300) q.head_frame[i] = *reinterpret_cast<const std::uint16_t*>(block + gns_transport_rules::input_records + q.read[i]*40 + 4);
            q.consumed_frame[i] = log.consumed[i];
        }
        auto* s = reinterpret_cast<void*>(gns_transport_rules::session_static);
        void* node = first(s);
        for (unsigned n = 0; node && n < 4; ++n, node = at<void*>(node,node_next)) {
            const auto* record = at<const void*>(node,node_record);
            const auto index = at<unsigned>(record,record_index_at);
            if (index < 4) q.participant_controller[index] = at<unsigned>(record,join_in_progress_rules::record_controller_at);
        }
        if (saved && b == execution_boundary::retire) {
            q.input_frame = at<std::uint16_t>(reinterpret_cast<void*>(saved[7]), 4);
            const auto block = saved[2]; // EBP = this participant's input block at the retirement writer
            if (block >= 0x79fbb0 && (block - 0x79fbb0) % gns_transport_rules::input_block_size == 0) {
                const auto index = (block - 0x79fbb0) / gns_transport_rules::input_block_size;
                if (index < 4) q.consumed_frame[index] = log.consumed[index] = q.input_frame;
            }
        }
        if (saved && (b == execution_boundary::apply_before || b == execution_boundary::apply_after)) {
            // ESI already points at the record payload (+4), whose first u16 is its frame.
            q.controller = saved[2]; q.input_frame = at<std::uint16_t>(reinterpret_cast<void*>(saved[1]), 0);
        }
        if (saved && b == execution_boundary::query_after) {
            q.result = saved[7] & 255;
            if (q.result) q.controller = *reinterpret_cast<const unsigned*>(saved[13]);
        }
        if (saved && (b == execution_boundary::pda_open || b == execution_boundary::pause_toggle)) {
            q.caller = saved[11]; q.object = saved[6];
            if (b == execution_boundary::pause_toggle) q.result = saved[12];
        }
        const auto menus = *reinterpret_cast<const std::uintptr_t*>(0x8aff18);
        q.modal = menus ? at<unsigned>(reinterpret_cast<void*>(menus), 0x86090) : 0;
        if (q.modal != execution_last_modal) { ++execution_modal_serial; execution_last_modal = q.modal; }
        q.modal_serial = execution_modal_serial;
        bool video_active = false;
        if (execution_video_state(video_active)) q.video = 1 | (unsigned(video_active) << 1);
        const auto& installed_entities = entity_capacity::installed();
        q.world_live = at<unsigned>(reinterpret_cast<const void*>(installed_entities.world_object),installed_entities.world_core.live_count);
        const auto body = event_manager_object + event_body_at;
        q.event_count = *reinterpret_cast<const unsigned*>(body + event_heap_count_at);
        q.event_hash = 2166136261u;
        // A bounded local head digest, not a canonical cross-PC event identity.
        for (unsigned i = 0; i < std::min(q.event_count, 8u)*2; ++i)
            q.event_hash = (q.event_hash ^ *reinterpret_cast<const unsigned*>(body + event_heap_at + 4*i))*16777619u;
        const auto& d = actor_ai::installed();
        const auto pool = d.primary_base ? d.primary_base : brain_pool_stock;
        q.ai_free = *reinterpret_cast<const unsigned*>(pool + actor_ai::ring_free(d));
        for (unsigned i = 0; i < join_in_progress_core::brain_ring_held_words(d.controllers ? d.controllers : 32); ++i)
            q.ai_held[i] = *reinterpret_cast<const unsigned*>(pool + actor_ai::ring_held(d) + 4*i);
        q.readable |= 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) { /* partial record is explicitly marked unreadable */ }
}

void execution_flush(const char* reason)
{
    if (!execution_log || (!execution_log->samples.size && !execution_log->draws.size)) return;
    const auto& log = *execution_log;
    logger::write("jip execution end: reason=%s samples=%u omitted=%u draws=%u draw_omitted=%u skips=%u yields=%u; render-tail subset, prefix retained",
        reason, log.samples.size, log.samples.omitted, log.draws.size, log.draws.omitted, execution_skips, execution_yields);
    for (unsigned i = 0; i < log.samples.size; ++i) {
        const auto& q = log.samples.entries[i];
        logger::write("jip execution: iter=%u gen=%u event=%u frame=%u readable=%u flags=%x phase=%u mode=%u run=%u world=%u applied=%u retired=%u sync=%08x/%u visual=%08x/%u game=%08x effective=%08x:%08x pending=%08x:%08x step=%08x:%08x tick=%08x:%08x paused=%u helper=%u input=%u ctl=%u result=%u modal=%08x/%u live=%u events=%u/%08x ai=%u/%08x,%08x,%08x caller=%08x object=%08x video=%u",
            q.iteration,q.generation,q.event,q.frame,q.readable,q.flags,q.phase,q.mode,q.run,q.worlds,q.applied,q.retired,q.sync_seed,q.sync_count,q.visual_seed,q.visual_count,
            q.game_time,q.effective_time[1],q.effective_time[0],q.clock[9],q.clock[8],q.clock[11],q.clock[10],q.clock[13],q.clock[12],q.clock[6]&255,q.helper,q.input_frame,q.controller,q.result,
            q.modal,q.modal_serial,q.world_live,q.event_count,q.event_hash,q.ai_free,q.ai_held[0],q.ai_held[1],q.ai_held[2],q.caller,q.object,q.video);
        logger::write("jip execution camera: iter=%u gen=%u event=%u eye=%08x,%08x,%08x target=%08x,%08x,%08x basis=%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x,%08x",
            q.iteration,q.generation,q.event,q.camera[0],q.camera[1],q.camera[2],q.camera[3],q.camera[4],q.camera[5],q.camera[6],q.camera[7],q.camera[8],q.camera[9],q.camera[10],q.camera[11],q.camera[12],q.camera[13],q.camera[14]);
        for (unsigned k = 0; k < 4; ++k) logger::write("jip execution input: iter=%u gen=%u event=%u logical=%u object=%u cur=%08x prev=%08x participant=%u participant_ctl=%d read=%u write=%u head=%u consumed=%u",
            q.iteration,q.generation,q.event,k,q.mapping[k],q.current[k],q.previous[k],k,int(q.participant_controller[k]),q.read[k],q.write[k],q.head_frame[k],q.consumed_frame[k]);
    }
    for (unsigned i = 0; i < log.draws.size; ++i) {
        const auto& d = log.draws.entries[i];
        logger::write("jip execution rng: iter=%u gen=%u event=%u frame=%u visual=%u count=%u caller=%08x",d.iteration,d.generation,d.boundary,d.frame,d.visual,d.count,d.caller);
    }
    execution_log->samples.clear(); execution_log->draws.clear();
}

void execution_end(const char* reason)
{
    execution.clear(); execution_in_helper = execution_skipped = false;
    execution_last_modal = execution_modal_serial = 0;
    execution_flush(reason); // teardown only, never called from RNG/camera observation
    execution_skips = execution_yields = 0;
}

void execution_note_draw(const std::uint32_t* stack, bool visual)
{
    if (!execution_log || execution.phase == join_in_progress_core::restart_execution_phase::off) return;
    auto& log = *execution_log;
    if (visual) ++log.visual_count;
    auto* d = log.draws.append(); if (!d) return;
    __try {
        *d = {log.iteration,execution.generation,log.last_boundary,*reinterpret_cast<const std::uint16_t*>(native_frame),
            visual ? log.visual_count : *reinterpret_cast<const unsigned*>(rng_count)+1,
            join_in_progress_core::normalise_draw_caller(stack,draw_wrapper_float_return,draw_wrapper_int_return_a,draw_wrapper_int_return_b),unsigned(visual)};
    } __except (EXCEPTION_EXECUTE_HANDLER) { d->caller = ~0u; }
}

// Saved frame: EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX,flags,return,event,... .
void __cdecl execution_note_saved(const std::uint32_t* saved)
{
    const auto b = execution_boundary(saved[10]);
    if (b == execution_boundary::stall_before) { execution_in_helper = true; execution_skipped = false; execution_wait_state(); }
    execution_snapshot(b, saved);
    if (b == execution_boundary::stall_after) {
        const auto w = execution_wait_state();
        execution_in_helper = false;
        if (execution_wrapper && (execution_skipped || execution.classify(w) == join_in_progress_core::restart_wait_policy::input_only)) {
            ++execution_yields; execution_sleep(1);
        }
    }
}

__declspec(naked) void execution_observe_bridge() { __asm {
    pushfd
    pushad
    mov ebp, esp
    sub esp, 210h
    and esp, 0fffffff0h
    fxsave [esp]
    push ebp
    call execution_note_saved
    add esp, 4
    fxrstor [esp]
    mov esp, ebp
    popad
    popfd
    ret 4
} }

// Call-shaped thiscall-compatible replacement: same CClient*, zero stack args, native return to 0x4021bc.
__declspec(naked) void __fastcall execution_stall_bridge(void*, void*) { __asm {
    push 15
    call execution_observe_bridge
    call dword ptr [execution_helper_at]
    push 16
    call execution_observe_bridge
    ret
} }

// Called only at the helper's render entry, AFTER every prefix service/command.
// Any load, modal, session change, unknown flag/phase or contradictory permit retains native behavior.
unsigned __cdecl execution_render_policy()
{
    const auto w = execution_wait_state();
    const bool skip = execution_wrapper && execution_in_helper && execution.classify(w) == join_in_progress_core::restart_wait_policy::input_only;
    if (skip) { execution_skipped = true; ++execution_skips; }
    execution_snapshot(skip ? execution_boundary::render_skipped : execution_boundary::render_native);
    return skip;
}
__declspec(naked) void execution_render_bridge() { __asm {
    pushfd
    pushad
    mov ebp, esp
    sub esp, 210h
    and esp, 0fffffff0h
    fxsave [esp]
    call execution_render_policy
    test eax, eax
    jnz skip
    fxrstor [esp]
    mov esp, ebp
    popad
    popfd
    call dword ptr [execution_render_at]
    jmp dword ptr [execution_render_resume]
skip:
    fxrstor [esp]
    mov esp, ebp
    popad
    popfd
    jmp dword ptr [execution_render_exit]
} }

__declspec(naked) void execution_raw_bridge() { __asm {
    push 0
    call execution_observe_bridge
    call dword ptr [edx+14h]
    push 1
    call execution_observe_bridge
    mov byte ptr [esp+8], 1
    jmp dword ptr [execution_raw_resume]
} }
__declspec(naked) void execution_input_bridge() { __asm {
    push 8
    call execution_observe_bridge
    mov eax, 55a2b0h
    call eax
    push 9
    call execution_observe_bridge
    ret
} }
__declspec(naked) void execution_gate_fail_bridge() { __asm {
    push 4
    call execution_observe_bridge
    push dword ptr [esp+4]
    call dword ptr [input_manager_run_at]
    push 5
    call execution_observe_bridge
    ret 4
} }
__declspec(naked) void execution_gate_run_bridge() { __asm {
    push 2
    call execution_observe_bridge
    pushfd
    pushad
    mov ebp, esp
    sub esp, 210h
    and esp, 0fffffff0h
    fxsave [esp]
    call frame_gate_reseed
    fxrstor [esp]
    mov esp, ebp
    popad
    popfd
    push dword ptr [esp+4]
    call dword ptr [input_manager_run_at]
    push 3
    call execution_observe_bridge
    ret 4
} }
__declspec(naked) void execution_world_bridge() { __asm {
    push 19
    call execution_observe_bridge
    call dword ptr [edx+16ch]
    push 20
    call execution_observe_bridge
    ret
} }
__declspec(naked) void execution_query_bridge() { __asm {
    push 21
    call execution_observe_bridge
    push dword ptr [esp+8]
    push dword ptr [esp+8]
    call dword ptr [edx+134h]
    push 22
    call execution_observe_bridge
    ret 8
} }
__declspec(naked) void execution_produce_bridge() { __asm {
    push 10
    call execution_observe_bridge
    push dword ptr [esp+12]
    push dword ptr [esp+12]
    push dword ptr [esp+12]
    mov eax, 559700h
    call eax
    push 11
    call execution_observe_bridge
    ret 12
} }
__declspec(naked) void execution_apply_bridge() { __asm {
    push 12
    call execution_observe_bridge
    call dword ptr [edx+24h]
    push 13
    call execution_observe_bridge
    mov edx, dword ptr [ebp*4+0a6ad78h]
    jmp dword ptr [execution_apply_resume]
} }
__declspec(naked) void execution_retire_bridge() { __asm {
    mov byte ptr [eax], bl
    push 14
    call execution_observe_bridge
    mov ecx, dword ptr [ebp+3170h]
    jmp dword ptr [execution_retire_resume]
} }
void __cdecl execution_visual_note(const std::uint32_t* stack) { execution_note_draw(stack, true); }
std::uint32_t execution_visual_resume = 0x5600e6;
__declspec(naked) void execution_visual_bridge() { __asm {
    pushfd
    pushad
    mov ebp, esp
    sub esp, 210h
    and esp, 0fffffff0h
    fxsave [esp]
    lea eax, [ebp+24h]
    push eax
    call execution_visual_note
    add esp, 4
    fxrstor [esp]
    mov esp, ebp
    popad
    popfd
    mov ecx, dword ptr ds:[7ad8b0h]
    jmp dword ptr [execution_visual_resume]
} }
__declspec(naked) void execution_video_branch_bridge() { __asm {
    push 23
    call execution_observe_bridge
    call dword ptr [execution_render_at]
    jmp dword ptr [execution_video_resume]
} }
__declspec(naked) void execution_ordinary_branch_bridge() { __asm {
    push 24
    call execution_observe_bridge
    mov eax, 5d8920h
    call eax
    jmp dword ptr [execution_ordinary_resume]
} }
__declspec(naked) void execution_pda_bridge() { __asm {
    push 25
    call execution_observe_bridge
    push esi
    mov esi, ecx
    mov al, byte ptr [esi+1944h]
    jmp dword ptr [execution_pda_resume]
} }
__declspec(naked) void execution_pause_bridge() { __asm {
    push 26
    call execution_observe_bridge
    push ebx
    push esi
    mov esi, ecx
    mov al, byte ptr [esi+6ch]
    jmp dword ptr [execution_pause_resume]
} }

void execution_add_patches(std::vector<xml1_hook_transaction::patch>& writes)
{
    if (execution_observer || execution_wrapper) {
        writes.push_back({execution_stall,bytes(execution_stall_original),rel(0xe8,execution_stall,reinterpret_cast<const void*>(&execution_stall_bridge),5)});
        writes.push_back({execution_render,bytes(execution_render_original),rel(0xe9,execution_render,reinterpret_cast<const void*>(&execution_render_bridge),5)});
    }
    if (!execution_observer) return;
    writes.push_back({0x5cec00,bytes(execution_pda_original),rel(0xe9,0x5cec00,reinterpret_cast<const void*>(&execution_pda_bridge),9)});
    writes.push_back({0x60c230,bytes(execution_pause_original),rel(0xe9,0x60c230,reinterpret_cast<const void*>(&execution_pause_bridge),7)});
    writes.push_back({0x402a19,bytes(execution_video_branch_original),rel(0xe9,0x402a19,reinterpret_cast<const void*>(&execution_video_branch_bridge),5)});
    writes.push_back({0x402a7c,bytes(execution_ordinary_branch_original),rel(0xe9,0x402a7c,reinterpret_cast<const void*>(&execution_ordinary_branch_bridge),5)});
    writes.push_back({execution_raw,bytes(execution_raw_original),rel(0xe9,execution_raw,reinterpret_cast<const void*>(&execution_raw_bridge),execution_raw_original.size())});
    writes.push_back({execution_input,bytes(execution_input_original),rel(0xe8,execution_input,reinterpret_cast<const void*>(&execution_input_bridge),5)});
    writes.push_back({execution_world,bytes(execution_world_original),rel(0xe8,execution_world,reinterpret_cast<const void*>(&execution_world_bridge),6)});
    writes.push_back({execution_query,bytes(execution_query_original),rel(0xe8,execution_query,reinterpret_cast<const void*>(&execution_query_bridge),6)});
    writes.push_back({execution_gate_fail,bytes(execution_gate_fail_original),rel(0xe8,execution_gate_fail,reinterpret_cast<const void*>(&execution_gate_fail_bridge),5)});
    writes.push_back({execution_produce,bytes(execution_produce_original),rel(0xe8,execution_produce,reinterpret_cast<const void*>(&execution_produce_bridge),5)});
    writes.push_back({execution_apply,bytes(execution_apply_original),rel(0xe9,execution_apply,reinterpret_cast<const void*>(&execution_apply_bridge),10)});
    writes.push_back({execution_retire,bytes(execution_retire_original),rel(0xe9,execution_retire,reinterpret_cast<const void*>(&execution_retire_bridge),8)});
    writes.push_back({execution_visual,bytes(execution_visual_original),rel(0xe9,execution_visual,reinterpret_cast<const void*>(&execution_visual_bridge),6)});
}
