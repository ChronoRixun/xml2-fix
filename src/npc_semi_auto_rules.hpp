#pragma once
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include "limits_rules.hpp"
#include "entity_capacity_layout.hpp"

// Opt-in weapon compatibility for the original weapon brain (0xef390) and input gate
// (0x42f60). Source metadata supplies recoil, weapon range, burst duration, leader,
// warm-up and the weapon flags; input admission is actor-wide. The held-fire branch
// (0xef8df) and the gate are shared by every weapon type: the gate waits for the recoil
// deadline (armed by the semi-auto entry 0xe14d0), then for a warm-up weapon starts the
// warm-up once (deadline actor+0x49c) and waits for it, then requests power_attack with
// play flags 1 (continuous beam: warm-up kept until fire is released) or 9 (discrete:
// warm-up cleared after every shot). A default-handler primary has no semi-auto entry,
// so only the gate's own idle transition arms its recoil deadline.
// The type is read only in the bullet line-of-sight drop (0xef3ff..0xef434 via 0xfe550)
// and in the branches for an empty near-target query 0x103850 (bullet 0xef9bc, other
// 0xefc4a); see docs/npc-weapon-round8.md for which of those are restored.
namespace npc_semi_auto_rules
{
 using u32 = std::uint32_t;
 constexpr unsigned capacity = 64; // source metadata records
 constexpr unsigned timer_slots = entity_capacity_layout::max_world;
 // Low ten bits are unique among live World tokens in both supported layouts:
 // stock has eight index bits (plus two generation bits here), raised has ten.
 constexpr unsigned timer_slot(u32 token) { return token & (timer_slots - 1); }
 // Flags word (schema 3): the original prototype's type enum (parser table 0x44fc44) in
 // bits 0-1, its continuousbeam and rapidfire bits (+0x1d4 bits 1/2) and whether the
 // source primary uses the default handler instead of ch_weapon_semi_auto.
 enum : u32 { type_bullet = 0, type_flame = 1, type_projectile = 2, type_beam = 3, type_mask = 3,
              flag_continuous = 0x10, flag_rapidfire = 0x20, flag_default_handler = 0x40,
              known_flags = type_mask | flag_continuous | flag_rapidfire | flag_default_handler };
 struct weapon { std::string actor; u32 recoil_bits = 0, range_bits = 0, burst_bits = 0, leader = 0, warmup_bits = 0, flags = 0; };
 constexpr unsigned words = 6;
 inline bool continuous(const weapon& w) { return (w.flags & flag_continuous) != 0; }
 inline bool default_handler(const weapon& w) { return (w.flags & flag_default_handler) != 0; }
 inline bool bullet(const weapon& w) { return (w.flags & type_mask) == type_bullet; }
 inline std::optional<weapon> parse(std::string_view text)
 {
  const auto bar = text.find('|');
  if (bar == 0 || bar > 31 || bar == std::string_view::npos || text.size() != bar + 9 * words) return {};
  for (unsigned word = 0; word != words; ++word) if (text[bar + 9 * word] != '|') return {};
  weapon w;
  for (const char c : text.substr(0, bar)) {
   if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '_')) return {};
   w.actor += c >= 'A' && c <= 'Z' ? char(c + ('a' - 'A')) : c;
  }
  u32* fields[] = {&w.recoil_bits, &w.range_bits, &w.burst_bits, &w.leader, &w.warmup_bits, &w.flags};
  for (unsigned word = 0; word != words; ++word)
  for (const char c : text.substr(bar + 1 + word * 9, 8)) {
   const int n = c >= '0' && c <= '9' ? c - '0' :
                 c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
   if (n < 0) return {};
   *fields[word] = (*fields[word] << 4) | unsigned(n);
  }
  if (w.flags & ~known_flags) return {};
  // Zero recoil is the lightning gun's: a continuous beam restarted on every gate update.
  const float recoil = std::bit_cast<float>(w.recoil_bits);
  if (!std::isfinite(recoil) || recoil < 0 || (recoil == 0 && (w.recoil_bits || !continuous(w))) || recoil > 60) return {};
  const float range = std::bit_cast<float>(w.range_bits), burst = std::bit_cast<float>(w.burst_bits);
  if (!std::isfinite(range) || range <= 75 || range > 65535 || std::floor(range) != range ||
      !std::isfinite(burst) || burst <= 0 || burst > 60 || w.leader > 1) return {};
  const float warmup = std::bit_cast<float>(w.warmup_bits);
  if (!std::isfinite(warmup) || warmup < 0 || (warmup == 0 && w.warmup_bits) || warmup > 60) return {};
  // A default-handler primary (the pistol) is discrete and has no warm-up in the source.
  if (default_handler(w) && (continuous(w) || w.warmup_bits)) return {};
  return w;
 }
 inline bool canonicalize(std::vector<weapon>& weapons)
 {
  if (weapons.empty() || weapons.size() > capacity) return false;
  std::sort(weapons.begin(), weapons.end(), [](const auto& a, const auto& b) { return a.actor < b.actor; });
  for (std::size_t i = 1; i < weapons.size(); ++i)
   if (weapons[i - 1].actor == weapons[i].actor) return false;
  return true;
 }
 inline u32 fingerprint(const std::vector<weapon>& weapons)
 {
  u32 h = 2166136261u;
  const auto add = [&h](unsigned char c) { h = (h ^ c) * 16777619u; };
  for (const auto& w : weapons) {
   for (const unsigned char c : w.actor) add(c);
   add(0);
   // Every word of the schema 3 row; a table of discrete rows therefore no longer hashes
   // like its schema 2 form, so older revision 10 candidates are refused by value.
   for (const u32 value : {w.recoil_bits, w.range_bits, w.burst_bits, w.leader, w.warmup_bits, w.flags})
    for (unsigned i = 0; i != 4; ++i) add(static_cast<unsigned char>(value >> (8 * i)));
  }
  return h;
 }
 // next = the recoil deadline (original actor+0x498), warm = the warm-up deadline
 // (actor+0x49c; zero = not started, as the original tests it against 0.0).
 struct state { std::uintptr_t actor = 0; u32 token = 0; float entered = 0, next = 0, warm = 0; };
 inline bool same(const state& s, std::uintptr_t actor, u32 token) { return s.actor == actor && s.token == token; }
 inline bool ready(const state& s, std::uintptr_t actor, u32 token, float now)
 {
  // A new incarnation or a reset game clock cannot inherit an old deadline.
  return std::isfinite(now) && (!same(s, actor, token) || now < s.entered || now >= s.next);
 }
 inline state entered(std::uintptr_t actor, u32 token, float now, u32 recoil)
 { return {actor, token, now, now + std::bit_cast<float>(recoil)}; }
 // Re-arming the recoil deadline (semi-auto entry, idle transition) never touches the
 // warm-up deadline of the same incarnation, as the original writes only actor+0x498.
 inline void rearm(state& s, std::uintptr_t actor, u32 token, float now, float delay)
 {
  const float warm = same(s, actor, token) && !(now < s.entered) ? s.warm : 0.f;
  s = {actor, token, now, now + delay, warm};
 }
 constexpr float transition_delay = .25f; // 0x42f60: fadd [0x3ca9e0] after requesting idle
 // Releasing fire clears a started warm-up (0x4313f..0x43154).
 inline void released(state& s, std::uintptr_t actor, u32 token) { if (same(s, actor, token)) s.warm = 0; }
 enum class admit : unsigned char { wait, warm_up, fire };
 // One update of the gate with power+fire held, after its input-lock and animation tests
 // (0x43030..0x43131): recoil deadline, then warm-up, then the shot.
 inline admit gate(state& s, std::uintptr_t actor, u32 token, float now, const weapon& w)
 {
  if (!ready(s, actor, token, now)) return admit::wait;
  if (!same(s, actor, token) || now < s.entered) s = {actor, token, now, now, 0.f};
  const float warmup = std::bit_cast<float>(w.warmup_bits);
  if (warmup != 0) {
   if (s.warm == 0) { s.warm = now + warmup; return admit::warm_up; }
   if (now < s.warm) return admit::wait; // fire when now >= deadline (or unordered)
  }
  if (!continuous(w)) s.warm = 0;
  return admit::fire;
 }
 inline u32 play_flags(const weapon& w) { return continuous(w) ? 1u : 9u; }
 constexpr u32 lookup_call = 0x4f6bf6, lookup = 0x4fd860;
 constexpr u32 candidate_site = 0x50fe80, candidate_native = 0x50fe89;
 constexpr u32 candidate_allow = 0x50fe94, candidate_deny = 0x50ff65;
 constexpr u32 default_vtable = 0x692f4c, default_entry = 0x4fbdd0;
 constexpr u32 resolve_action = 0x427c80, clock = 0x45fe60;
 inline constexpr std::array<limits_rules::guard, 49> guards{{
  {lookup_call, "e8656c0000", "FightMove handler lookup call"},
  {0x4f6bfb, "83c4045f8986c8000000", "handler is stored at move +0xc8"},
  {candidate_site, "d94728d89e50050000dfe0f6c4050f8ad1000000", "picker's per-slot reuse test"},
  {0x50fe6e, "896c241032db8dbe30040000", "picker slot zero and per-slot cursor"},
  {default_entry, "e88b40f6ff8b442404d998e8050000c20c00", "default handler entry clock"},
  {resolve_action, "8b4424048b9178030000515052e83e54ffff8bc8e8579e0d00c20400", "actor action resolver"},
  {default_vtable, "10d74f00d0bd4f0010eb590010eb5900b0cb480000c54f0050ce4f0060c84f00", "default handler's eight callbacks"},
  {clock, "8b0d348471008b01ffa060010000", "native game clock getter"},
  {0x504f5b, "8b8ec40400008b0156ff5014", "brain constructor completion and binding"},
  {0x518090, "8bc18a501433c980e2fcc74008ffffffffc7", "native ranged constructor"},
  {0x5180ae, "c700f450690089481cc740209a99193fc740240000c842c740280100000089482c884830", "ranged constructor vtable and timers"},
  {0x50fce8, "8b4f108b4104", "power refresh before role reassignment"},
  {0x51a359, "8b4d048b411080a064070000fbb0015f5e5d5b83c458c2", "ranged failed-pickup continuation"},
  {0x51a22d, "d9442418d81df0a16800dfe0f6c4410f85300100008b4d04f68178020000040f8420", "ranged weapon distance and capability checks"},
  {0x4341bb, "8a8664070000a801752b8b8e7803000085c97421a820751557c7", "input dispatcher before move handler"},
  {0x41b120, "56578b7c240c85ff8bf17428803f007423", "move-name interner"},
  {0x427ca0, "8b4424045150e82554ffff8bc8e89e9d0d00c20400", "named move resolver"},
  {0x432030, "558b6c240885ed568bf10f84de000000578bbe7803000085ff0f84ce000000d9", "native requested move entry"},
  {0x43211c, "5b5f5e5dc21800", "native requested move stack contract"},
  {0x500840, "8b81bc000000c3", "move animation enum field"},
  {0x518660, "518b44240885c05356578bf9", "brain target validation"},
  {0x5031b0, "568bf18b46108b887803000085", "AI angle setter"},
  {0x525160, "568b74240885f6744c8b81a0c40000", "AI token resolver"},
  {0x502580, "568bf1808eb502000001", "AI decision defer"},
  {0x5078b0, "6aff64a1000000006840556700506489250000000083ec5053558be9", "nearby AI notice"},
  {0x505a40, "538bd933c98d9384000000", "target AI notice"},
  {0x560190, "8a44240484c07405e9f3feffffe93effffff", "simulation random selector"},
  {0x469760, "d981ec030000c3", "game delta getter"},
  {0x6950f4, "009e5100d07d510080b547007087510030a55100", "ranged brain primary callbacks"},
  {0x517c30, "8b44240489410c6bc00bc7411800", "brain combo level setter"},
  {0x41e24b, "f686d9030000407414", "native crouch state bit"},
  {0x4341b3, "578bcee8d568ffff", "movement call before input dispatch"},
  {0x42aa90, "83ec24568bf18d4c2404ff1560fa6700", "native movement processor"},
  {0x694bec, "05000000060000000700000008000000", "native cached action indices"},
  {0x50fc35, "8db71c04000033db8d4900896e14c746280000a040896e3c8b8bec4b69008b4c8c143bcd746f8b11", "native cached slot initializers and resolved action table"},
  {0x5008c0, "d98140010000c3", "AI reuse getter field"},
  {0x502c50, "568b7424088bceff1558fa67", "AI navigation direction check"},
  {0x512f01, "8b4610f6806407000001740df680d9030000100f8530010000f686a402000001", "attack-state input-lock exit"},
  {0x519fe4, "ff520c84c07419837d08ff7513c7430800000000b0015f5e5d5b83c458c20800", "ranged power decision result and early return"},
  {0x5607b0, "d905300068008b4c2408d901", "direction-to-angles conversion"},
  {0x503810, "568bf18b46108b8878030000", "AI facing setter"},
  {0x517cf0, "8b41048b40108b4c24048b1105ac030000", "ranged brain aim point setter (actor +0x3ac)"},
  {0x519e2d, "e82ee8ffff84c00f84e70300008b5c24708b5500", "ranged brain target resolve and line-of-sight site"},
  // After the entry's 6 bytes: rule 16 (required) has already guarded them and redirected them to its XML1
  // visibility adapter (item 8), so the line-of-sight drop calls XML1's ray as the original 0xfe550 does.
  {0x505256, "53568bf18b4618578d4c", "AI target visibility test (after rule 16's entry redirect)"},
  {0x505030, "83ec0c5356578b7c241c578bf1", "AI state setter"},
  {0x503180, "568bf18b46108b887803000085c97409", "AI turn toward angles"},
  {0x6943f8, "90065000", "move flag getter slot +0x5c"},
  {0x500690, "8a8144010000c0e8022401c3", "move flag getter (+0x144 bit 2)"},
  {0x51a2e4, "e86789feff84c00f842d0200008d442420508d4c243c51e8b06404008b10895424288b4804894c242c8b50088b4d0483c4088d442420505789542430e8eb94feff810b80000000814b0480000000830b10834b04108b168d442438508bceff5224508bcde8a3d9ffff", "native held-fire branch calling convention"},
 }};
}
