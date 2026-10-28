#pragma once
#include "gameplay_capabilities.hpp"
#include "limits_rules.hpp"
#include "move_triggers_rules.hpp"

// Each independent resource-loading limit gets a named, requested-only rule.
// Use effective capacities, but poison agreement if a requested raise failed.
// An absent/stock configuration contributes nothing to the released record.
namespace limits_agreement_rules
{
 using capacities = std::array<int, 6>;
 inline constexpr capacities stock{limits_rules::stock_resource_names, limits_rules::stock_actor_slots,
  limits_rules::stock_item_enhancements, limits_rules::stock_fight_styles,
  limits_rules::stock_effect_curves, move_triggers_rules::stock_capacity};
 inline constexpr std::array<std::uint16_t, 6> ids{gameplay_capabilities::resource_names_id,
  gameplay_capabilities::actor_slots_id, gameplay_capabilities::item_enhancements_id,
  gameplay_capabilities::fight_styles_id, gameplay_capabilities::effect_curves_id,
  gameplay_capabilities::move_triggers_id};
 template<class Register> void register_rules(const capacities& wanted, const capacities& effective, Register add, bool manager_verified = true)
 {
  for (std::size_t i = 0; i < ids.size(); ++i)
   if (wanted[i] != stock[i] || effective[i] != stock[i] || (i == 3 && !manager_verified))
    add(ids[i], static_cast<std::uint16_t>(i == 3 && wanted[i] > 32 ? 2 : 1), std::array<std::uint32_t, 4>{static_cast<std::uint32_t>(effective[i]), 0, 0, 0},
        wanted[i] == effective[i] && (i != 3 || manager_verified));
 }
 template<class Register> void register_events(int wanted, int effective, bool manager_verified, Register add)
 {
  if (wanted != limits_rules::stock_style_events || !manager_verified)
   add(gameplay_capabilities::style_events_id, 1, std::array<std::uint32_t, 4>{static_cast<std::uint32_t>(effective),0,0,0},
       manager_verified && wanted == effective);
 }
}
