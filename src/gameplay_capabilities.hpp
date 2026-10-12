#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>

namespace gameplay_capabilities
{
 constexpr std::uint16_t entity_layout_id=1, explosive_first_hit_id=2, master_mold_id=3, turret_controller_id=4,
  rank_costs_id=5, multipart_id=6, x1_leader_id=7, placement_crush_id=11, double_heal_id=12;
 // [Game] DeathRule=xml1, X-Men Legends 1's death threshold (docs/death-threshold.md).
 constexpr std::uint16_t death_threshold_id=9;
 // 10: the first game's Xtraction services and revival price (xtract::capability; issue #63). Revision 2: the online
 // rule greys Load and the campaign-gated options (revision 1, never released, greyed Load only).
 constexpr std::uint16_t xtraction_services_id=10, xtraction_services_revision=2;
 constexpr std::uint16_t script_functions_id=14; // [Limits] ScriptFunctions (script_functions_rules.hpp); 5..13 belong to the 0.2.2 wave's other rules
 // Protocol revision 2: the Phase A agreement - the record appended to the admission messages 18/1A/1B/1F, compared
 // before the native admission callback, a mismatch refused with 1D and the dialog on both sides, and the entity-layout
 // rule with the actor+generic <= world headroom condition. Revision-1 peers (0.2.0i and older) refuse. The plan's
 // receive-lifecycle machinery (certificate epochs, a dispatch floor, quiescent retirement, Node purge; section 5) is
 // NOT part of it: docs/entity-capacity-phase-a-plan.md section 10, deviation 17.
 constexpr std::uint16_t protocol_revision=2;
 constexpr std::uint32_t magic=0x50414347; // GCAP, little endian
 // Sixteen entries: IDs 1-12 are assigned (docs/gameplay-agreement.md), with room to spare.
 constexpr std::size_t max_entries=16, header_size=12, entry_size=20, max_size=header_size+entry_size*max_entries;
 struct entry { std::uint16_t id,revision; std::array<std::uint32_t,4> values; };
 enum class result { match, unavailable, malformed, protocol, entries, revision, value };
 class record
 {
  std::array<entry,max_entries> entries_{};
  std::array<std::uint8_t,max_size> bytes_{};
  std::size_t count_=1,size_=0;
  bool valid_=true,frozen_=false,layout_registered_=false;
 public:
  record() { entries_[0]={entity_layout_id,1,{32,224,256,8}}; }
  bool ready() const { return valid_ && frozen_; }
  bool add(std::uint16_t id,std::uint16_t revision,std::array<std::uint32_t,4> values,bool verified)
  {
   if(frozen_ || !verified || !id || id==entity_layout_id || !revision || count_==max_entries) { valid_=false;return false; }
   for(std::size_t i=0;i<count_;++i)if(entries_[i].id==id){valid_=false;return false;}
   entries_[count_++]={id,revision,values};return true;
  }
  // actors + generic <= world (the world keeps registrations beyond the two arenas, plan section 5), world within the
  // token width; a failed or unverified layout poisons the record even when the fallback is physically retail.
  bool layout(std::uint16_t revision,std::array<std::uint32_t,4> values,bool verified)
  {
   const std::uint64_t capacity=values[3]>0 && values[3]<=32 ? (1ull<<values[3]) : 0;
   if(frozen_ || layout_registered_ || !verified || !revision || !values[0] || !values[1] || !values[2] ||
      std::uint64_t(values[0])+values[1]>values[2] || values[2]>capacity) { valid_=false;return false; }
   layout_registered_=true;entries_[0]={entity_layout_id,revision,values};return true;
  }
  bool finalize()
  {
   if(frozen_)return ready();
   frozen_=true;if(!valid_)return false;
   std::sort(entries_.begin(),entries_.begin()+count_,[](const entry&a,const entry&b){return a.id<b.id;});
   size_=header_size+count_*entry_size;
   auto put=[&](std::size_t at,std::uint32_t value,std::size_t n){for(std::size_t i=0;i<n;++i)bytes_[at+i]=std::uint8_t(value>>(i*8));};
   put(0,magic,4);put(4,protocol_revision,2);put(6,count_,2);put(8,size_,2);put(10,0,2);
   for(std::size_t i=0;i<count_;++i){const auto at=header_size+i*entry_size;const auto&e=entries_[i];put(at,e.id,2);put(at+2,e.revision,2);for(std::size_t j=0;j<4;++j)put(at+4+j*4,e.values[j],4);}
   return true;
  }
  std::span<const std::uint8_t> bytes() const { return ready() ? std::span<const std::uint8_t>(bytes_.data(),size_) : std::span<const std::uint8_t>{}; }
  result compare(std::span<const std::uint8_t> peer) const
  {
   if(!ready())return result::unavailable;
   if(peer.size()<header_size)return result::malformed;
   auto get=[&](std::size_t at,std::size_t n){std::uint32_t v=0;for(std::size_t i=0;i<n;++i)v|=std::uint32_t(peer[at+i])<<(i*8);return v;};
   if(get(0,4)!=magic || get(4,2)!=protocol_revision)return result::protocol;
   const auto n=get(6,2);
   if(n==0 || n>max_entries || get(8,2)!=peer.size() || peer.size()!=header_size+n*entry_size || get(10,2)!=0)return result::malformed;
   if(n!=count_)return result::entries;
   for(std::size_t i=0;i<count_;++i){const auto at=header_size+i*entry_size;const auto&e=entries_[i];if(get(at,2)!=e.id)return result::entries;if(get(at+2,2)!=e.revision)return result::revision;for(std::size_t j=0;j<4;++j)if(get(at+4+j*4,4)!=e.values[j])return result::value;}
   return result::match;
  }
 };
}
