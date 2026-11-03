#pragma once

// Session policy and measurements only. No settings-file, engine or transport access.
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace linked_frame_rate_core {
inline constexpr char capability[] = "lc-online-linked-rate/1";
constexpr std::uint16_t revision = 2;
constexpr unsigned minimum = 10, maximum = 60; // native TickRate ceiling
constexpr bool valid(unsigned fps) { return fps >= minimum && fps <= maximum; }
// A fixed host target, including the native online ceiling. 0 display = unlimited.
constexpr unsigned host_target(unsigned display, unsigned tick) {
    return display ? std::min(display, tick) : tick;
}
// Appended to the existing native admission messages, after GCAP. An old GCAP
// peer refuses the revision/length before invoking any native admission handler.
constexpr std::size_t settings_size = 8;
inline std::array<std::uint8_t, settings_size> encode(unsigned fps) {
    return {'F','P','S','C',1,std::uint8_t(fps),std::uint8_t(fps >> 8),0};
}
inline bool decode(const std::uint8_t* p, std::size_t n, unsigned& fps) {
    fps = 0;
    if (!p || n != settings_size || p[0]!='F' || p[1]!='P' || p[2]!='S' || p[3]!='C' || p[4]!=1 || p[7]) return false;
    const unsigned value = unsigned(p[5]) | (unsigned(p[6]) << 8);
    if (!valid(value)) return false;
    fps = value; return true;
}
class session {
    unsigned target_ = 0;
    bool seen_online_ = false, room_owned_ = false;
public:
    unsigned target() const { return target_; }
    bool room_owned() const { return room_owned_; }
    // Immutable until leave. Repeated roster / mesh replies cannot retarget play.
    bool adopt(unsigned fps, bool room_owned = false) {
        if (!valid(fps) || (target_ && target_ != fps)) return false;
        target_ = fps; room_owned_ = room_owned_ || room_owned; return true;
    }
    void clear() { target_ = 0; seen_online_ = room_owned_ = false; }
    // Native direct-IP/LAN sessions have no room teardown callback. Do not clear
    // a late joiner's settings merely because its first shared frame is pending.
    bool observe_online(bool online) {
        if (!target_) return false;
        if (online) seen_online_ = true;
        else if (seen_online_ && !room_owned_) { clear(); return true; }
        return false;
    }
};
enum class capacity : std::uint8_t { unknown, holding, slow };
class work_meter {
    std::uint32_t since_ = 0, latest_ = 0;
    std::uint64_t work_ = 0;
    unsigned count_ = 0, over_ = 0, good_ = 0, target_ = 0;
    bool started_ = false, collecting_ = false;
    capacity state_ = capacity::unknown;
public:
    void reset() { *this = {}; }
    // Keep established gameplay evidence readable on the Party/pause screen.
    // UI time never becomes a work sample or joins two partial play windows.
    void pause(std::uint32_t now) {
        if (!started_) return;
        if (now-latest_ > 5000) { reset(); return; }
        latest_ = now; collecting_ = false;
        work_ = 0; count_ = over_ = good_ = 0;
    }
    capacity state(std::uint32_t now) const {
        return !started_ || now - latest_ > 5000 ? capacity::unknown : state_;
    }
    // Local work from the passed input barrier through Present, EXCLUDING remote
    // input waits and our deliberate limiter wait. Never use shared fps to blame a PC.
    void sample(std::uint32_t now, std::uint64_t work_us, unsigned fps) {
        if (!valid(fps)) { reset(); return; }
        if (target_ != fps || (started_ && now - latest_ > 5000)) reset();
        if (!started_) { target_ = fps; started_ = true; }
        if (!collecting_) { since_ = now; collecting_ = true; }
        latest_ = now;
        work_ += work_us; ++count_;
        if (work_us * fps > 1100000) ++over_; // >110% of the frame budget
        if (now - since_ < 3000 || count_ < 30) return;
        const bool slow = work_ * fps > std::uint64_t(count_) * 1100000 || over_ * 5 > count_;
        if (slow) { state_ = capacity::slow; good_ = 0; }
        else if (++good_ >= (state_ == capacity::slow ? 2u : 1u)) state_ = capacity::holding;
        work_ = 0; count_ = over_ = 0; since_ = now;
    }
};
// 0 is an unlimited LOCAL setting; 0xffff is an unknown remote setting.
constexpr unsigned unknown_rate = 0xffff;
inline std::string rate_text(unsigned local, unsigned host) {
    if (local == unknown_rate || !valid(host)) return "Frame rate pending";
    return (local ? std::to_string(local) : "Unlimited") + " local / " + std::to_string(host) + " online";
}
inline std::string warning_text(unsigned local, unsigned host, capacity status) {
    if (!valid(host)) return "";
    if (status == capacity::slow) return "Cannot hold " + std::to_string(host) + " fps";
    if (local != unknown_rate && local != host && status == capacity::holding) return "Holding host's " + std::to_string(host) + " fps";
    if (local != unknown_rate && local != host) return "Using host's " + std::to_string(host) + " fps";
    return status == capacity::holding ? "Holding session rate" : "Measuring in play";
}
}
