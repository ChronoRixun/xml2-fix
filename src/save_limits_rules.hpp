#pragma once
#include "lcsave/layout.hpp"
#include <cstdint>
namespace save_limits_rules {
inline constexpr unsigned multiplier=lcsave::w2_layout.talent_multiplier;
inline constexpr unsigned capacity=lcsave::w2_layout.talent_pool;
static_assert(multiplier==200 && capacity==256 && 298*multiplier-1<=65535);
// A position is an index, so M-1 is valid; the next owner's base is refused.
constexpr bool position(unsigned owner,unsigned p) {
 return owner<=297 && p<multiplier && (owner!=0 || p<multiplier-1) && owner*multiplier+p<=65535;
}
constexpr unsigned id(unsigned stats,unsigned p){return (stats+1)*multiplier+p;}
}
