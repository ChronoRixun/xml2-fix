#pragma once
#include "limits_rules.hpp"

// XML1's stun flag belongs to the current reaction, not to a pending powerup.
// See docs/stun-rule.md for the original dispatcher and handler evidence.
namespace stun_rules
{
 constexpr std::uint32_t dispatch_call = 0x54a11f, native_dispatch = 0x432030;
 constexpr std::uint32_t set_move = 0x41d8ac, set_move_resume = 0x41d8b2;
 constexpr std::uint32_t stun_name = 0x78a934;
 constexpr std::uint32_t resolve_move = 0x427ca0;
 constexpr std::uint32_t input_call = 0x4341de, resolve_action = 0x427c80, clock = 0x45fe60;
 constexpr std::uint32_t expire_flag = 0x54a215, expire_resume = 0x54a21c, expire_done = 0x54a280;
 // XML1 0x44b6f..0x44bb5: the argument is animation speed, not a lifetime.
 constexpr float reaction_speed(float speed, float susceptibility)
 {
  if (!(susceptibility > 0)) return speed; // native entry rejects these requests
  const float result = (speed == 1.0f ? 2.0f : speed) / susceptibility;
  return result < 0 ? 0 : result > 10 ? 10 : result;
 }
 constexpr bool release_input(float health, std::uint32_t pressed, float now, float entered)
 { return health > 0 && ((pressed & 0x30) != 0 || now - entered > 0); }
 constexpr bool party_hero(std::uint32_t flags, int controller)
 {
  return (flags & (1u << 20)) != 0 && controller >= 0 && controller < 4;
 }
 constexpr std::uint8_t reaction_flags(std::uint8_t flags, bool hero, bool in_stun)
 {
  return hero && !in_stun ? flags & 0xfe : flags;
 }
 inline constexpr std::array<limits_rules::guard, 18> guards{{
  {dispatch_call, "e80c7feeff", "stun powerup's reaction request"},
  {set_move, "89b778030000", "resolved move becomes actor's current move"},
  {0x41d890, "568b74240885f6578bf9", "move setter actor and move registers"},
  {0x41d89a, "751068c8a87800e8faa300008bf085f67421", "null move resolves idle or returns before assignment"},
  {resolve_move, "8b4424045150e82554ffff8bc8e89e9d0d00c20400", "actor-specific move resolver"},
  {0x54a0f7, "8a86650700006a010c016a00888665070000", "stun flag set before requesting the move"},
  {0x54a11b, "50558bce", "stun request uses resolved actor and move"},
  {0x46d54d, "8b4004c1e814a801", "party collector's entity membership bit"},
  {0x46d575, "83b8d4030000ff", "party collector's controller assignment"},
  {0x54a215, "80a765070000fe", "native powerup expiry clears the same flag"},
  {input_call, "8b0156ff5048", "current move input dispatch"},
  {0x4341bb, "8a8664070000a801752b8b8e7803000085c97421a820751557c786bc03000000000000", "input lock, pending chain and actor arguments"},
  {resolve_action, "8b4424048b9178030000515052e83e54ffff8bc8e8579e0d00c20400", "current move action resolver"},
  {clock, "8b0d348471008b01ffa060010000", "native game clock getter"},
  {0x54a207, "e824cefeff8bf883c40485ff7450", "expiry resolves a non-null actor in edi"},
  {expire_done, "5f5e5bc20800", "expiry epilogue"},
  {0x54a021, "8a867a050000a8018d8e700400007405e8ea1effff8b8620050000", "native stun susceptibility cache refresh and field"},
  {0x4fbdd0, "e88b40f6ff8b442404d998e8050000c20c00", "default handler records the entry clock"},
 }};
 inline std::vector<std::uint8_t> branch(std::uint32_t site, const void* target, bool call, std::size_t size)
 {
  std::vector<std::uint8_t> bytes(size, 0x90);
  bytes[0] = call ? 0xe8 : 0xe9;
  const auto delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)) - site - 5;
  std::memcpy(bytes.data() + 1, &delta, 4);
  return bytes;
 }
}
