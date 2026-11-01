// Included in join_in_progress's game-thread namespace. The four producer
// bridges capture values the native code actually uses, without calling any
// world/camera getter or recomputing its floating-point arithmetic.
join_in_progress_diagnostics::window checksum_window;
std::array<join_in_progress_diagnostics::hero, join_in_progress_diagnostics::hero_limit> checksum_heroes{};
std::array<std::uint32_t, 3> checksum_camera{};
bool checksum_camera_readable = false;
bool checksum_read(std::uintptr_t address, void* out, std::size_t size) {
    __try { std::memcpy(out, reinterpret_cast<const void*>(address), size); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
unsigned checksum_period_budget = join_in_progress_diagnostics::periods; // follow-up 13: 30 with [Dev] LateJoinProbe=1 (frames 30..900)
// Follow-up 14 (review: a late drop must name the term): the last producer sample, kept on EVERY period whether or not
// the logging window is open, and written out when the native heads disagree (the compare bridge) - the six raw
// camera components, the hero transforms and the draw count of the period that broke.
struct last_sample_t {
    bool valid = false; std::uint16_t frame = 0; std::uint32_t hash = 0, hero_sum = 0, camera = 0, rng = 0, camera_a[3]{}, camera_b[3]{}; unsigned heroes = 0;
    std::array<std::array<std::uint32_t, 6>, 4> transform{};
} last_sample;
std::uint16_t last_mismatch_frame = 0; bool last_mismatch_logged = false;
// p060fix/AP: [Dev] LockstepChecksumAlways=1 (probe only): the producer sample at every native period of ANY session,
// lobby STARTs included (the host never runs a cohort start there, so nothing armed a window). Release default off.
bool checksum_always = false;
bool checksum_log_every_period = false; // AE15: with [Dev] LateJoinProbe=1 the producer sample (no owner lines, no draw flush) is logged at EVERY period, so a late drop is bracketed on both peers
void arm_checksums() {
    // Follow-up 14 (review S6): the probe's 30-period budget applies to a late restart's epoch only; a lobby START keeps the four.
    checksum_window.begin(GetTickCount()); checksum_window.limit = late_restart ? checksum_period_budget : join_in_progress_diagnostics::periods; checksum_heroes = {}; checksum_camera_readable = false;
    last_sample = {}; last_mismatch_logged = false;
    logger::write("jip checksum: epoch=%u armed for first %u native periods; observation only", checksum_window.epoch, checksum_window.limit);
}
// AE15: a second window in the same epoch (the probe re-arms at a device switch).
void rearm_checksums(unsigned limit, const char* why) {
    checksum_window.rearm(GetTickCount()); checksum_window.limit = limit; checksum_heroes = {}; checksum_camera_readable = false; last_mismatch_logged = false;
    logger::write("jip checksum: epoch=%u re-armed for %u native periods at %s; observation only", checksum_window.epoch, checksum_window.limit, why);
}
void __cdecl checksum_actor_observe(const std::uint32_t* regs) {
    if (regs[2] >= checksum_heroes.size()) return; // every period (the last sample); the window only gates the logging
    auto& h = checksum_heroes[regs[2]]; h = {};
    h.actor = regs[7];
    const auto native_stack = regs[3] + 4; // PUSHAD saved ESP after PUSHFD
    h.readable = checksum_read(native_stack + 0x18, &h.handle, 4) &&
        checksum_read(h.actor + 0x20, h.transform.data(), sizeof h.transform) &&
        checksum_read(h.actor + actor_controller, &h.controller, 4) &&
        checksum_read(h.actor + hero_ai_flags, &h.ai, 4);
}
void __cdecl checksum_hero_observe(const std::uint32_t* regs) {
    if (regs[2] >= checksum_heroes.size()) return;
    auto& h = checksum_heroes[regs[2]];
    h.contribution = regs[1]; h.contributed = true; // native ESI subtotal
}
void __cdecl checksum_camera_observe(const std::uint32_t* regs) {
    checksum_camera_readable = checksum_read(regs[7], checksum_camera.data(), sizeof checksum_camera);
}
void __cdecl checksum_produced(const std::uint32_t* regs) {
    if (checksum_always && !checksum_window.armed) {
        // A window of no captured periods in the current epoch (0 before any cohort start): only the every-period sample
        // lines, no owner/head lines, no budget; a later cohort start arms its own window (a new epoch) as before.
        checksum_window.rearm(GetTickCount()); checksum_window.limit = 0;
        logger::write("jip checksum: epoch=%u [Dev] LockstepChecksumAlways=1 - every native period of this session is sampled from frame %u (observation only)",
            checksum_window.epoch, unsigned(regs[1] & 0xffff));
    }
    const bool in_window = checksum_window.capture(GetTickCount());
    const bool logging = in_window || (checksum_log_every_period && checksum_window.armed);
    if (in_window) ++checksum_window.produced; // charge the budget even if a diagnostic read faults
    __try {
        const auto native_stack = regs[3] + 4;
        std::uint32_t count = 0, seed = 0, visual_seed = 0, camera_a[3]{};
        const bool valid = checksum_read(native_stack + 0x78, &count, 4) &&
            checksum_read(native_stack + 0x20, camera_a, sizeof camera_a) &&
            checksum_read(0x6e0874, &seed, 4) && checksum_read(0x7ad8b0, &visual_seed, 4);
        std::uint32_t heroes = 0; bool complete = count <= checksum_heroes.size();
        for (unsigned i = 0; i < count && i < checksum_heroes.size(); ++i) {
            const auto& h = checksum_heroes[i]; heroes += h.contribution;
            complete &= h.readable && h.contributed;
            if (logging) logger::write("jip checksum hero: epoch=%u frame=%u slot=%u handle=0x%08X actor=0x%08X readable=%u contribution=0x%08X controller=%d ai=0x%08X transform20..34=%08X,%08X,%08X,%08X,%08X,%08X",
                checksum_window.epoch, unsigned(regs[1] & 0xffff), i, h.handle, h.actor, unsigned(h.readable && h.contributed), h.contribution,
                int(h.controller), h.ai, h.transform[0], h.transform[1], h.transform[2], h.transform[3], h.transform[4], h.transform[5]);
        }
        const auto camera = regs[0] - regs[7] - heroes; // hash - native RNG count - native hero subtotals
        last_sample.valid = true; last_sample.frame = std::uint16_t(regs[1] & 0xffff); last_sample.hash = regs[0]; last_sample.hero_sum = heroes; last_sample.camera = camera; last_sample.rng = regs[7];
        last_sample.heroes = count < 4 ? count : 4; std::memcpy(last_sample.camera_a, camera_a, sizeof camera_a); std::memcpy(last_sample.camera_b, checksum_camera.data(), sizeof last_sample.camera_b);
        for (unsigned i = 0; i < last_sample.heroes; ++i) last_sample.transform[i] = checksum_heroes[i].transform;
        if (!logging) { checksum_heroes = {}; checksum_camera_readable = false; return; }
        logger::write("jip checksum produced: epoch=%u sample=%u frame=%u hash=0x%08X heroes=%u complete=%u hero_sum=0x%08X camera=0x%08X rng_draws=%u seed=0x%08X visual_seed=0x%08X readable=%u camera_readable=%u cameraA=%08X,%08X,%08X cameraB=%08X,%08X,%08X",
            checksum_window.epoch, checksum_window.produced, unsigned(regs[1] & 0xffff), regs[0], count, unsigned(complete), heroes, camera, regs[7], seed, visual_seed,
            unsigned(valid), unsigned(checksum_camera_readable), camera_a[0], camera_a[1], camera_a[2], checksum_camera[0], checksum_camera[1], checksum_camera[2]);
        if (!in_window) { checksum_heroes = {}; checksum_camera_readable = false; return; } // every-period mode: the sample lines only
        void* s = session();
        void* n = first(s);
        for (unsigned i = 0; n && i < 4; ++i, n = at<void*>(n, node_next)) {
            const auto* r = at<const void*>(n, node_record);
            const auto index = at<std::uint32_t>(r, record_index_at);
            if (index >= 4) continue;
            const auto input = 0x79fbb0u + index * gns_transport_rules::input_block_size;
            const auto* peer=at<const void*>(r,record_peer);
            logger::write("jip checksum owner: epoch=%u frame=%u index=%u flags=0x%08X controller=%d active=%u pending=%u native_send_pending=%u input_read=%u input_write=%u input_next=%u",
                checksum_window.epoch, unsigned(regs[1] & 0xffff), index, at<std::uint32_t>(r, record_flags), at<int>(r, join_in_progress_rules::record_controller_at),
                unsigned(at<std::uint8_t>(r,record_active)),unsigned(machine.pending(r)),peer ? at<std::uint32_t>(peer,gns_transport_rules::peer_send_count) : 0u,
                at<std::uint32_t>(reinterpret_cast<void*>(input), gns_transport_rules::input_read),
                at<std::uint32_t>(reinterpret_cast<void*>(input), gns_transport_rules::input_write),
                unsigned(at<std::uint16_t>(reinterpret_cast<void*>(input), gns_transport_rules::input_next)));
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        logger::write("jip checksum: epoch=%u sample=%u diagnostic read fault; native checksum unchanged", checksum_window.epoch, checksum_window.produced);
    }
    checksum_heroes = {}; checksum_camera_readable = false;
    probe_period(std::uint16_t(regs[1] & 0xffff));
}
// Follow-up 14: two present heads at one frame with different hashes - the period the native arbitration will drop
// someone on. Logged once per frame, outside the observation window, with this PC's last producer sample.
void checksum_mismatch_observe(const std::array<gns_transport_core::checksum_head, 4>& heads) {
    bool mismatch = false; std::uint16_t frame = 0; std::uint32_t first = 0; bool have = false;
    for (const auto& h : heads) {
        if (!h.present || !h.readable) continue;
        if (!have) { have = true; frame = h.frame; first = h.hash; continue; }
        if (h.frame == frame && h.hash != first) mismatch = true;
    }
    if (!mismatch || (last_mismatch_logged && last_mismatch_frame == frame)) return;
    last_mismatch_logged = true; last_mismatch_frame = frame;
    char heads_text[160]{}; int n = 0;
    for (unsigned i = 0; i < heads.size() && n >= 0 && n < int(sizeof heads_text) - 24; ++i)
        if (heads[i].present) n += std::snprintf(heads_text + n, sizeof heads_text - n, "%s%u:%u:%08X", n ? " " : "", i, unsigned(heads[i].frame), heads[i].hash);
    const auto& l = last_sample;
    logger::write("jip checksum MISMATCH: frame=%u heads(index:frame:hash)=%s; this PC's last producer sample: valid=%u frame=%u hash=0x%08X hero_sum=0x%08X camera=0x%08X rng_draws=%u cameraA=%08X,%08X,%08X cameraB=%08X,%08X,%08X",
        unsigned(frame), heads_text, unsigned(l.valid), unsigned(l.frame), l.hash, l.hero_sum, l.camera, l.rng, l.camera_a[0], l.camera_a[1], l.camera_a[2], l.camera_b[0], l.camera_b[1], l.camera_b[2]);
    for (unsigned i = 0; i < l.heroes; ++i)
        logger::write("jip checksum MISMATCH hero: frame=%u slot=%u transform20..34=%08X,%08X,%08X,%08X,%08X,%08X", unsigned(l.frame), i,
            l.transform[i][0], l.transform[i][1], l.transform[i][2], l.transform[i][3], l.transform[i][4], l.transform[i][5]);
}
void __cdecl checksum_heads_observe(void* m) {
    __try {
        // Match the native walk's active records, without using its cursor.
        std::array<gns_transport_core::checksum_head, 4> heads{};
        for (unsigned index=0;index<heads.size();++index)
            heads[index]=gns_transport_core::read_checksum_head(checksum_read,reinterpret_cast<std::uintptr_t>(m),index);
        checksum_mismatch_observe(heads);
        if (!checksum_window.active(GetTickCount()) || checksum_window.compared >= checksum_window.limit) return;
        bool complete = true, any = false; std::uint16_t frame = 0;
        unsigned walked = 0;
        for (void* n = first(session()); n && walked++ < 4; n = at<void*>(n, node_next)) {
            const auto* r = at<const void*>(n, node_record);
            const auto index = at<std::uint32_t>(r, record_index_at);
            if (index >= 4) { complete = false; break; }
            const auto& h = heads[index];
            complete &= h.present && h.readable && (!any || h.frame == frame);
            if (!any) frame = h.frame;
            any = true;
        }
        if (!checksum_window.compare(GetTickCount(), complete && any, frame)) return;
        for (unsigned index = 0; index < heads.size(); ++index) {
            const auto& h = heads[index];
            logger::write("jip checksum head: epoch=%u period=%u index=%u count=%u readable=%u present=%u frame=%u hash=0x%08X",
                checksum_window.epoch, checksum_window.compared, index, h.count, unsigned(h.readable), unsigned(h.present), unsigned(h.frame), h.hash);
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) { }
}

// Preserve integer registers, flags, x87 stack/control, XMM and MXCSR. These
// helpers can be called with a live native x87 stack (camera producer). EBP
// anchors PUSHAD while the 512-byte FXSAVE area is aligned independently.
#define JIP_CHECKSUM_OBSERVER(callback) \
    __asm { pushfd } \
    __asm { pushad } \
    __asm { mov ebp, esp } \
    __asm { sub esp, 528 } \
    __asm { and esp, -16 } \
    __asm { fxsave [esp] } \
    __asm { push ebp } \
    __asm { call callback } \
    __asm { add esp, 4 } \
    __asm { fxrstor [esp] } \
    __asm { mov esp, ebp } \
    __asm { popad } \
    __asm { popfd }
const std::uint32_t checksum_actor_resume_at = 0x614b0b, checksum_hero_resume_at = 0x614b7d,
    checksum_camera_resume_at = 0x614bb2, checksum_produced_resume_at = 0x614bdb, rng_count_at = 0x560180;
__declspec(naked) void checksum_actor_bridge() {
    JIP_CHECKSUM_OBSERVER(checksum_actor_observe)
    __asm { fld dword ptr [eax+20h]
            mov ecx, dword ptr [eax+28h]
            jmp dword ptr [checksum_actor_resume_at] }
}
__declspec(naked) void checksum_hero_bridge() {
    JIP_CHECKSUM_OBSERVER(checksum_hero_observe)
    __asm { mov eax, dword ptr [esp+78h]
            add edi, esi
            jmp dword ptr [checksum_hero_resume_at] }
}
__declspec(naked) void checksum_camera_bridge() {
    JIP_CHECKSUM_OBSERVER(checksum_camera_observe)
    __asm { fld dword ptr [eax]
            fld dword ptr [eax+4]
            jmp dword ptr [checksum_camera_resume_at] }
}
__declspec(naked) void checksum_produced_bridge() {
    __asm { call dword ptr [rng_count_at]
            add edi, eax }
    JIP_CHECKSUM_OBSERVER(checksum_produced)
    __asm { jmp dword ptr [checksum_produced_resume_at] }
}
#undef JIP_CHECKSUM_OBSERVER
