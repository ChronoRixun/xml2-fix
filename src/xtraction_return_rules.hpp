#pragma once
#include "limits_rules.hpp"
#include <array>
#include <cmath>

// A side-return position is already authored/captured. The native floor helper
// traces only +/-24 around its selected position. A NAV result on a different
// floor cannot be repaired by that trace; retain its existing input fallback.
namespace xtraction_return_rules {
constexpr float floor_span=24.f;
constexpr DWORD restore_call=0x5f4504, restore_actors=0x489300;
constexpr DWORD nav_slot=0x689c38, nav_query=0x494300;
inline constexpr std::array<limits_rules::guard,8> guards{{
 {0x5f4500,"8d4c2410e8f74de9ff","side pop invokes snapshot actor placement with ECX=the copied snapshot"},
 {nav_slot,"00434900","navigation manager vt+0x0c: bool query(input, output, int, int), ret16"},
 {nav_query,"8b44241056508bf18b4c2414518d54241c","navigation query's four stack arguments"},
 {0x494347,"b0015ec2100032c05ec21000","navigation query success/failure and ret16"},
 {0x423d24,"e8271607008b106a018bc8ff5208e8191607008b106a016a008d4c242c51578bc8ff520c84c0740f","floor helper's NAV query and existing input-position fallback"},
 {0x682fac,"0000c041","native floor trace half-span is 24"},
 {0x423d92,"d805ac2f6800","floor trace upper endpoint adds the guarded span"},
 {0x423db7,"d825ac2f6800","floor trace lower endpoint subtracts the guarded span"},
}};
inline bool accept(bool scoped,bool found,const float* input,const float* output) {
 if(!scoped||!found)return found;
 for(unsigned i=0;i<3;++i)if(!std::isfinite(input[i]))return found;
 for(unsigned i=0;i<3;++i)if(!std::isfinite(output[i]))return false;
 return std::fabs(output[2]-input[2])<=floor_span;
}
}
