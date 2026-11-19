#pragma once

// C0 diagnostics at EXISTING guarded adapters. This service never installs a hook,
// modifies a coordinate, or treats enqueue ownership as flush/projection ownership.
#include "centered_ui_rules.hpp"
#include "log.hpp"
#include <array>
#include <atomic>
#include <cfenv>
#include <mutex>
#include <xmmintrin.h>

namespace centered_ui_trace {
enum class owner : unsigned { device, classic_batch, wheel, menu_text, online, bxig, raw_pointer, count };
enum class boundary : unsigned { applied, unavailable, enqueue, render, layout, input, count };
struct event {
    owner family=owner::device;
    boundary at=boundary::applied;
    std::uintptr_t object=0;
    unsigned detail=0;
    std::uint64_t frame=0, generation=0;
    std::array<double,4> coordinates{};
    bool transform_known=false, inside=false, pointer_space_matches=false;
    centered_ui::transform canvas{};
};
constexpr unsigned capacity=256, per_boundary=16;
struct capture {
    std::array<event,capacity> events{};
    unsigned used=0, suppressed=0;
};
inline std::atomic<bool> active{false};
inline std::mutex lock;
inline capture history;
inline std::array<unsigned,unsigned(owner::count)*unsigned(boundary::count)> counts{};
inline std::optional<centered_ui::transform> canvas;
inline std::uint64_t frame=0;

inline bool enabled() { return active.load(std::memory_order_relaxed); }
struct preserve_fp {
    std::fenv_t environment{};
    unsigned mxcsr=_mm_getcsr();
    bool saved=std::fegetenv(&environment)==0;
    ~preserve_fp() { if(saved) std::fesetenv(&environment);_mm_setcsr(mxcsr); }
};
inline void configure(bool profile,bool component,bool diagnostic) {
    active.store(false,std::memory_order_relaxed);
    if (!profile || !component || !diagnostic) return; // OFF: no lock, allocation, hook, query, or game write
    std::lock_guard guard(lock);
    history={};counts={};canvas.reset();frame=0;
    active.store(true,std::memory_order_relaxed);
}
inline const char* name(owner v) {
    switch(v) {
    case owner::device:return "device";case owner::classic_batch:return "ClassicHud-batch";
    case owner::wheel:return "ClassicHud-wheel";case owner::menu_text:return "native-menu-text";
    case owner::online:return "online-menu";case owner::bxig:return "BXIG-AdvancedOptions";
    case owner::raw_pointer:return "raw-pointer";default:return "unknown";
    }
}
inline const char* name(boundary v) {
    switch(v) {
    case boundary::applied:return "applied";case boundary::unavailable:return "unavailable";
    case boundary::enqueue:return "enqueue";case boundary::render:return "render";
    case boundary::layout:return "layout";case boundary::input:return "input";default:return "unknown";
    }
}
inline const char* space(owner v) {
    switch(v) {
    case owner::classic_batch:case owner::wheel:return "native-XZ-enqueue";
    case owner::online:return "native-512x384-view";
    case owner::bxig:return "panel-sx-sy-left";
    case owner::raw_pointer:return "engine-backbuffer-pixels";
    case owner::menu_text:return "native-item-unmodified";
    default:return "actual-backbuffer";
    }
}
inline void record(owner family,boundary at,std::uintptr_t object=0,unsigned detail=0,
                   std::array<double,4> coordinates={}) {
    if (!enabled()) return;
    preserve_fp fp;
    event e;
    {
        std::lock_guard guard(lock);
        const auto bucket=unsigned(family)*unsigned(boundary::count)+unsigned(at);
        if (bucket>=counts.size()) return;
        // Keep repeated menu frames from spending the screen-transition budget.
        // Enqueue/input coordinates may change each call and remain separately sampled.
        if(at==boundary::layout || at==boundary::render) {
            for(unsigned i=0;i<history.used;++i) {
                const auto& old=history.events[i];
                if(old.family==family && old.at==at && old.object==object && old.detail==detail &&
                   old.coordinates==coordinates && old.generation==(canvas ? canvas->generation : 0)) return;
            }
        }
        if (history.used==capacity || counts[bucket]==per_boundary) {
            if (history.suppressed!=0xffffffffu) ++history.suppressed;
            return;
        }
        ++counts[bucket];
        e.family=family;e.at=at;e.object=object;e.detail=detail;e.coordinates=coordinates;e.frame=frame;
        if(canvas) {
            e.canvas=*canvas;e.generation=canvas->generation;e.transform_known=true;
            e.pointer_space_matches=family==owner::raw_pointer && coordinates[2]==canvas->width && coordinates[3]==canvas->height;
            e.inside=e.pointer_space_matches && canvas->inside({coordinates[0],coordinates[1]});
        }
        history.events[history.used++]=e;
    }
    logger::write("centered-ui C0: owner=%s boundary=%s object=%p detail=%u frame=%llu generation=%llu known=%d size=%ux%u s=%.9g origin=%.9g,%.9g fields=%.9g,%.9g,%.9g,%.9g inside=%d pointer-space-matches=%d space=%s decision=observed reason=no-additional-transform queue-flush=unproven camera-viewport=unobserved",
        name(family),name(at),reinterpret_cast<void*>(object),detail,e.frame,e.generation,e.transform_known,
        e.canvas.width,e.canvas.height,e.canvas.s,e.canvas.ox,e.canvas.oy,
        coordinates[0],coordinates[1],coordinates[2],coordinates[3],e.inside,e.pointer_space_matches,space(family));
}
template<class Applied> void applied(const Applied& state) {
    if (!enabled()) return;
    // Math must not leave FP status changes in the caller's rendering path.
    preserve_fp fp;
    {
        std::lock_guard guard(lock);
        canvas.reset();
        using validity=decltype(state.validity);
        using source=decltype(state.source);
        if(state.validity==validity::ready &&
           state.source==source::backbuffer_desc && state.last_hresult>=0)
            canvas=centered_ui::make(state.observed_backbuffer.width,state.observed_backbuffer.height,state.device_generation);
    }
    record(owner::device,state.validity==decltype(state.validity)::ready ? boundary::applied : boundary::unavailable);
}
inline void next_frame() {
    if(!enabled()) return;
    std::lock_guard guard(lock);++frame;
}
inline capture snapshot() { std::lock_guard guard(lock);return history; }
}
