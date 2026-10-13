#pragma once
#include "limits_rules.hpp"
#include <array>
#include <cstdint>

// [Game] DeathSwitchClear (docs/death-switch.md): when the controlled hero dies, the hero the game hands the
// controller to stops the walk its AI had started, as in X-Men Legends I.
//
// CActor onDeath (0x429de0) hands a dead hero's controller on at 0x42a042: cdecl 0x420860(out, team 0x1d,
// controller) picks the first living AI party member, writes its +0x3d4 and makes it the controller's hero
// (0x4205d0 -> 0x4204f0). That path never clears the member's move intent. The manual hero switch (0x424030)
// clears it first (0x42bd60), and the PC control update's player branch (0x434540, 0x434c83) drives any move goal
// still set through the AI's move driver 0x424b70 while the player gives no movement input. The first game's
// player branch (XBE 0x439a0 -> 0x3bad0) reads the pad only. The hook wraps that one call: after the native
// hand-over it clears the successor's move intent the way 0x42bd60 does, except its target (+0x3e0), which
// 0x4204f0 may just have given it from the dead hero (the manual switch keeps that carry-over too).
namespace death_switch_rules
{
 constexpr std::uint32_t call_site=0x42a042;          // CALL 0x420860 in onDeath
 constexpr std::uint32_t native_successor=0x420860;   // cdecl u32*(u32* out, int team, int controller), ret
 constexpr std::uint32_t clear_goal=0x421b90;         // thiscall(actor): +0x1d0/+0x1dc = FLT_MIN, +0x180/+0x184 = 0
 constexpr std::uint32_t resolve=0x4654b0;            // thiscall(const u32* handle) -> entity or null
 constexpr std::uint32_t null_handle=0x72988c;        // the game's null entity handle (what the call site compares)

 // Actor fields (CActor, vtable 0x682ccc).
 constexpr std::uint32_t health=0x27c;      // float, > 0 alive
 constexpr std::uint32_t controller=0x3d4;  // controller index, the one just handed over
 constexpr std::uint32_t ai_flags=0x3d8;    // bit 0: AI-controlled (cleared by 0x4204f0 on hand-over)
 constexpr std::uint32_t follow=0x3ec;      // move-to / follow entity handle (dword)
 constexpr std::uint32_t steer=0x808;       // byte: direct steer toward +0x7e4
 constexpr std::uint32_t mouse_mode=0x820;  // dword
 constexpr std::uint32_t mouse_828=0x828;   // dword, cleared by 0x42bd60
 constexpr std::uint32_t stuck=0x82c;       // byte, cleared by 0x42bd60

 inline constexpr std::array<limits_rules::guard,4> guards{{
  {0x42a034,"8b86d4030000508d4c24346a1d51e81968ffff8b44243c","onDeath hands the controller on (push controller, 0x1d, &out; call 0x420860; read out)"},
  {native_successor,"6aff68902d670064a1","controller hand-over to the next AI party member"},
  {clear_goal,"83ec0c5668000080","actor move-goal clear"},
  {resolve,"568bf1e8682a0600","entity handle resolver"},
 }};

 // E8 rel32 at `site` calling `target`.
 constexpr std::array<std::uint8_t,5> call_bytes(std::uint32_t site,std::uint32_t target)
 {
  const std::uint32_t rel=target-(site+5);
  return {0xe8,std::uint8_t(rel),std::uint8_t(rel>>8),std::uint8_t(rel>>16),std::uint8_t(rel>>24)};
 }
 constexpr std::uint32_t call_target(std::uint32_t site,const std::uint8_t* bytes)
 {
  return site+5+(std::uint32_t(bytes[1])|std::uint32_t(bytes[2])<<8|std::uint32_t(bytes[3])<<16|std::uint32_t(bytes[4])<<24);
 }
 constexpr auto native_call=call_bytes(call_site,native_successor);
 static_assert(native_call[0]==0xe8 && native_call[1]==0x19 && native_call[2]==0x68 && native_call[3]==0xff && native_call[4]==0xff,
               "the retail call at 0x42a042");

 // The successor whose move intent is cleared: the hand-over produced a handle, it resolved, and it is the
 // living, player-controlled hero of exactly that controller. Anything else is left as the game made it.
 constexpr bool should_clear(std::uint32_t handle,std::uint32_t none,int handed,bool resolved,std::uint32_t actor_controller,
                             std::uint8_t flags,float hp)
 {
  return handle!=0 && handle!=none && resolved && handed>=0 && handed<4 && actor_controller==std::uint32_t(handed) &&
         (flags&1)==0 && hp>0.f;
 }

 // Shared gameplay rule 15 (docs/gameplay-agreement.md): online play is lockstep, so every peer must hand over
 // the same way. Revision 1, {installed ? 1 : 0,0,0,0}; off/absent is verified {0,0,0,0}; requested but not
 // installed is unverified (online closed).
 constexpr std::uint16_t revision=1;
 struct registration { std::array<std::uint32_t,4> values{}; bool verified=true; };
 constexpr registration capability(bool requested,bool installed)
 {
  if(!requested)return {{0,0,0,0},true};
  if(!installed)return {{0,0,0,0},false};
  return {{1,0,0,0},true};
 }
}
