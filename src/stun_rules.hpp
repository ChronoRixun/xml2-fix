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
 constexpr bool party_hero(std::uint32_t flags, int controller)
 {
  return (flags & (1u << 20)) != 0 && controller >= 0 && controller < 4;
 }
 constexpr std::uint8_t reaction_flags(std::uint8_t flags, bool hero, bool in_stun)
 {
  return hero && !in_stun ? flags & 0xfe : flags;
 }
 inline constexpr std::array<limits_rules::guard, 10> guards{{
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
