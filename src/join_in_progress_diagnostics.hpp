#pragma once
#include <array>
#include <cstdint>

// Read-only producer observations. Never synthesize a checksum or consume RNG.
namespace join_in_progress_diagnostics {
// AR5: append-only bounded capture. Keep the first divergence and explicitly count omissions.
// Captured on the game thread; allocation and log output happen outside observed callbacks.
enum class boundary : unsigned {
    raw_before, raw_after, gate_before, gate_after, gate_failed_before, gate_failed_after,
    canonical_before, canonical_after, input_before, input_after, produce_before, produce_after,
    apply_before, apply_after, retire, stall_before, stall_after, render_native, render_skipped,
    world_before, world_after, query_before, query_after, video_branch, ordinary_branch, pda_open, pause_toggle
};
struct execution_sample {
    unsigned iteration = 0, generation = 0, event = 0, frame = 0, readable = 0;
    unsigned flags = 0, phase = 0, mode = 0, run = 0, worlds = 0, applied = 0, retired = 0;
    unsigned sync_seed = 0, sync_count = 0, visual_seed = 0, visual_count = 0;
    unsigned game_time = 0, effective_time[2]{}, clock[14]{};
    unsigned camera[15]{}; // eye +0x104, target +0xe0, basis +0x2cc (raw float words)
    unsigned mapping[4]{}, current[4]{}, previous[4]{};
    unsigned participant_controller[4]{~0u,~0u,~0u,~0u};
    unsigned read[4]{}, write[4]{}, head_frame[4]{}, consumed_frame[4]{};
    unsigned input_frame = 0, controller = ~0u, result = 0, helper = 0, modal = 0, modal_serial = 0;
    unsigned caller = 0, object = 0;
    unsigned video = 0; // bit 0: validated object/predicate layout, bit 1: active video
    unsigned world_live = 0, event_count = 0, event_hash = 0, ai_free = 0, ai_held[3]{};
};
template<class T, unsigned N> struct bounded_capture {
    std::array<T, N> entries{};
    unsigned size = 0, omitted = 0;
    T* append() { if (size == N) { ++omitted; return nullptr; } return &entries[size++]; }
    void clear() { size = omitted = 0; }
};
struct execution_draw {
    unsigned iteration = 0, generation = 0, boundary = 0, frame = 0, count = 0, caller = 0, visual = 0;
};
struct execution_capture {
    bounded_capture<execution_sample, 16384> samples;
    bounded_capture<execution_draw, 32768> draws;
    unsigned iteration = 0, worlds = 0, applied = 0, retired = 0, visual_count = 0, last_boundary = 0;
    unsigned consumed[4]{~0u,~0u,~0u,~0u};
};
constexpr unsigned periods = 4, hero_limit = 8;
struct hero {
    std::uint32_t handle = 0, actor = 0, controller = 0, ai = 0, contribution = 0;
    std::array<std::uint32_t, 6> transform{};
    bool readable = false, contributed = false;
};
struct window {
    unsigned epoch = 0, produced = 0, compared = 0;
    std::uint32_t since = 0;
    std::uint16_t last_frame = 0;
    bool have_frame = false;
    bool armed = false;
    unsigned limit = periods; // follow-up 13: the probe raises it (30 periods = frame 900) for a late restart's epoch
    void begin(std::uint32_t now) { ++epoch; produced = compared = 0; since = now; armed = true; have_frame = false; }
    // AE15: a second observation window inside the same epoch (the probe re-arms at a device switch): the epoch number
    // stays so the offline tool still pairs this PC's samples with the other peer's.
    void rearm(std::uint32_t now) { produced = compared = 0; since = now; armed = true; have_frame = false; }
    bool active(std::uint32_t now) const { return armed && std::uint32_t(now - since) < 180000; }
    bool capture(std::uint32_t now) const { return active(now) && produced < limit; }
    bool compare(std::uint32_t now, bool complete, std::uint16_t frame) {
        // Native arbitration repeats while remote packets arrive. Observe each
        // completed period once, before arbitration can clear its lists.
        if (!active(now) || !complete || compared >= limit || (have_frame && frame == last_frame)) return false;
        ++compared; have_frame = true; last_frame = frame; return true;
    }
};
}
