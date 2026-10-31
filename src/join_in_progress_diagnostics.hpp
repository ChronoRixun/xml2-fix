#pragma once
#include <array>
#include <cstdint>

// Read-only producer observations. Never synthesize a checksum or consume RNG.
namespace join_in_progress_diagnostics {
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
