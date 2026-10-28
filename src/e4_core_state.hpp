#pragma once
#include <cstdint>

// Preparation state is separate from publication. Consumers must never infer a
// successful install from a requested key or from allocated backing alone.
namespace e4_core_state {
inline bool requested=false,eligible=false,prepared=false,installed=false;
inline bool elide_power=false;
inline std::uint32_t cai=0;
inline void (*on_admitted)(unsigned)=nullptr;
inline void (*on_actor_free)(unsigned)=nullptr;
inline void (*on_controller_pop)()=nullptr;
inline void (*on_controller_freed)(unsigned)=nullptr;
inline void (*on_boundary)()=nullptr;
inline void (*on_event_borrow)(std::uint32_t,bool,std::uint32_t)=nullptr;
// Called under the observer's snapshot lock: native reserved count, three
// pending words, then the atomic fold-back count.
inline void (*on_snapshot)(std::uint32_t*)=nullptr;
inline constexpr std::uint32_t actors=96,actor_generic_base=0x31ca8,allocator_size=0x1aa520;
inline constexpr std::uint32_t primary_constructed=0x24a80,primary_ring=0x24a8c,
    primary_free=0x24c18,primary_active=0x24c1c,primary_live=0x24c28,
    primary_generations=0x24c2c,primary_mask=0x24dac,primary_shift=0x24db0,
    team_offset=0x24db8,cai_size=0x337ec;
inline constexpr std::uint32_t team_constructed=0xe700,team_ring=0xe70c,
    team_enqueue=0xe890,team_dequeue=0xe894,team_free=0xe898,
    team_active=0xe89c,team_live=0xe8a8,team_generations=0xe8ac,
    team_mask=0xea2c,team_shift=0xea30;
}
