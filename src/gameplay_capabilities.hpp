#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include "sha256.hpp"

namespace gameplay_capabilities
{
 // Rule IDs (docs/gameplay-agreement.md): 1-4 the 1.4.0 rules; 5..16 the 0.2.2 wave.
 constexpr std::uint16_t entity_layout_id=1, explosive_first_hit_id=2, master_mold_id=3, turret_controller_id=4,
  rank_costs_id=5, multipart_id=6, x1_leader_id=7, solo_mode_id=8, death_threshold_id=9, xtraction_services_id=10,
  placement_crush_id=11, double_heal_id=12, pack_amount_id=13, script_functions_id=14,
  death_switch_clear_id=15, npc_power_rule_id=16, object_world_impact_id=17, stun_rule_id=18,
  danger_room_party_id=19, lockon_aim_id=20, box_harm_origin_id=21, danger_room_healthbars_id=22, harm_single_chain_id=23, fast_transfer_id=24,
  gns_transport_id=25, join_in_progress_id=26, online_conversations_id=27, model_pools_id=28, tick_rate_id=29,
  world_backfaces_id=30;
 // Rules 17..29 (24 = [Online] FastTransfer, docs/online-campaign-transfer.md; 25 = [Online] Transport=gns, docs/gns-transport.md;
 // 26 = [Online] JoinInProgress, docs/join-in-progress.md; 27 = [Game] OnlineConversations=lockstep, docs/gameplay-agreement.md;
 // 28 = [Engine] ModelPools, docs/model-pools.md; 29 = [Online] TickRate, docs/online-tick-rate.md;
 // 30 = [Game] WorldBackfaces=xml1, docs/world-backfaces.md)
 // participate only when requested; OFF preserves the released digest.
 constexpr std::uint16_t fast_transfer_revision=1;
 // 10: the first game's Xtraction services and revival price (xtract::capability; issue #63). Revision 2: the online
 // rule greys Load and the campaign-gated options (revision 1, never released, greyed Load only).
 constexpr std::uint16_t xtraction_services_revision=2;
 // Protocol revision 3: the agreement record is a fixed 108-byte digest (docs/gameplay-agreement.md, "Record and
 // transport"): header, SHA-256 over the canonical sorted rule list (ID, revision, four effective values - the
 // revision-2 entry encoding) and a 64-byte rule summary that only names differing rules in refusal messages. Its size
 // no longer grows with the number of rules, so every queued admission body stays under the native 300-byte
 // single-message limit (a longer body takes the 0x12/0x13 fragment path, whose receiver 0x6101b0 dereferences null
 // at 0x61020c for a stream it never opened). Revision 2 (1.4.0 and the 1.5 pre-release, the full list on the wire)
 // and revision 1 (0.2.0i and older) peers refuse as another protocol, both ways. Phase A's receive lifecycle is still
 // not part of it (docs/entity-capacity-phase-a-plan.md section 10, deviation 17).
 // Revision 4 retains that framing and requires the native fresh-tail provenance contract.
 // Older peers cannot restore marked append tails and must refuse before native transfer.
 constexpr std::uint16_t protocol_revision=4;
 constexpr std::uint32_t magic=0x50414347; // GCAP, little endian
 // max_entries bounds registration only (the wire size is fixed): 16 rules are assigned in 1.5.x.
 constexpr std::size_t max_entries=256, entry_size=20;
 constexpr std::size_t header_size=12, digest_size=32, buckets=64, digest_at=header_size, summary_at=digest_at+digest_size;
 constexpr std::size_t max_size=summary_at+buckets, record_size=max_size;
 // The rule summary's bucket for a rule ID: exact for IDs 1..64, higher IDs share a bucket with ID - 64k.
 constexpr std::size_t bucket_of(std::uint16_t id) { return (std::size_t(id)+buckets-1)%buckets; }
 // What a log or dialog calls each rule (its xml2-fix.ini key).
 constexpr const char* rule_name(std::uint16_t id)
 {
  switch(id){
  case entity_layout_id: return "entity layout";
  case explosive_first_hit_id: return "ExplosiveFirstHit";
  case master_mold_id: return "Xml1MoldStage";
  case turret_controller_id: return "Xml1Turrets";
  case rank_costs_id: return "RankCosts/LevelSkillPoints/LevelStatPoints";
  case multipart_id: return "Xml1Multipart";
  case x1_leader_id: return "X1Leader";
  case solo_mode_id: return "SoloMode";
  case death_threshold_id: return "DeathRule";
  case xtraction_services_id: return "Xtraction services/RevivalCost";
  case placement_crush_id: return "PlacementCrush";
  case double_heal_id: return "DoubleHeal";
  case pack_amount_id: return "PackAmount";
  case script_functions_id: return "ScriptFunctions";
  case death_switch_clear_id: return "DeathSwitchClear";
  case npc_power_rule_id: return "NpcPowerRule";
  case object_world_impact_id: return "ObjectWorldImpact";
  case stun_rule_id: return "StunRule";
  case danger_room_party_id: return "DangerRoomParty";
  case lockon_aim_id: return "Xml1LockOnAim";
  case box_harm_origin_id: return "BoxHarmOrigin";
  case danger_room_healthbars_id: return "DangerRoomHealthBars";
  case harm_single_chain_id: return "HarmSingleChain";
  case fast_transfer_id: return "FastTransfer";
  case gns_transport_id: return "GnsTransport";
  case join_in_progress_id: return "JoinInProgress";
  case online_conversations_id: return "OnlineConversations";
  case model_pools_id: return "ModelPools";
  case tick_rate_id: return "TickRate";
  case world_backfaces_id: return "WorldBackfaces";
  default: return "a newer rule";
  }
 }
 struct entry { std::uint16_t id,revision; std::array<std::uint32_t,4> values; };
 // A rule whose effective state does not fit its four words (rule 16's weapon rows) also registers a detail: the
 // SHA-256 of its complete canonical data. Details extend the canonical list only when present (docs/gameplay-
 // agreement.md, "Rule details"), so a record without one hashes exactly as before.
 using detail_digest=std::array<std::uint8_t,32>;
 constexpr std::size_t max_details=16, detail_size=2+32;
 constexpr std::uint32_t detail_magic=0x4c544447; // GDTL, little endian
 enum class result { match, unavailable, malformed, protocol, entries, revision, value };
 // Which rule-summary buckets differ (bit b = bucket b). Never an admission decision: the digest decides.
 struct difference { std::uint64_t buckets=0,local_only=0,peer_only=0; };
 class record
 {
  std::array<entry,max_entries> entries_{};
  std::array<std::uint8_t,max_size> bytes_{};
  struct detail_entry { std::uint16_t id; detail_digest digest; };
  std::array<detail_entry,max_details> details_{};
  std::size_t count_=1,detail_count_=0;
  bool valid_=true,frozen_=false,layout_registered_=false;
  static void put(std::uint8_t* at,std::uint32_t value,std::size_t n){for(std::size_t i=0;i<n;++i)at[i]=std::uint8_t(value>>(i*8));}
  static std::uint32_t get(std::span<const std::uint8_t> b,std::size_t at,std::size_t n){std::uint32_t v=0;for(std::size_t i=0;i<n;++i)v|=std::uint32_t(b[at+i])<<(i*8);return v;}
  static void encode(std::uint8_t* at,const entry& e){put(at,e.id,2);put(at+2,e.revision,2);for(std::size_t j=0;j<4;++j)put(at+4+j*4,e.values[j],4);}
 public:
  record() { entries_[0]={entity_layout_id,1,{32,224,256,8}}; }
  bool ready() const { return valid_ && frozen_; }
  void invalidate() { valid_=false; }
  std::size_t count() const { return count_; }
  const entry& rule(std::size_t i) const { return entries_[i]; }
  bool add(std::uint16_t id,std::uint16_t revision,std::array<std::uint32_t,4> values,bool verified,const detail_digest* extra=nullptr)
  {
   if(frozen_ || !verified || !id || id==entity_layout_id || !revision || count_==max_entries || (extra && detail_count_==max_details)) { valid_=false;return false; }
   for(std::size_t i=0;i<count_;++i)if(entries_[i].id==id){valid_=false;return false;}
   entries_[count_++]={id,revision,values};
   if(extra)details_[detail_count_++]={id,*extra};
   return true;
  }
  // The detail registered with rule `id`, or null.
  const detail_digest* detail(std::uint16_t id) const
  { for(std::size_t i=0;i<detail_count_;++i)if(details_[i].id==id)return &details_[i].digest; return nullptr; }
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
   std::sort(details_.begin(),details_.begin()+detail_count_,[](const detail_entry&a,const detail_entry&b){return a.id<b.id;});
   // Canonical list: "GCAP", protocol, count, then every sorted entry in the revision-2 encoding; then, only when a
   // rule registered a detail, "GDTL", the detail count and every detail sorted by ID (ushort ID, 32 digest bytes).
   std::array<std::uint8_t,8+max_entries*entry_size+6+max_details*detail_size> canonical{};
   put(canonical.data(),magic,4);put(canonical.data()+4,protocol_revision,2);put(canonical.data()+6,std::uint32_t(count_),2);
   for(std::size_t i=0;i<count_;++i)encode(canonical.data()+8+i*entry_size,entries_[i]);
   std::size_t length=8+count_*entry_size;
   if(detail_count_){
    put(canonical.data()+length,detail_magic,4);put(canonical.data()+length+4,std::uint32_t(detail_count_),2);length+=6;
    for(std::size_t i=0;i<detail_count_;++i){put(canonical.data()+length,details_[i].id,2);std::memcpy(canonical.data()+length+2,details_[i].digest.data(),32);length+=detail_size;}
   }
   const auto digest=sha256_fips::digest(canonical.data(),length);
   put(bytes_.data(),magic,4);put(bytes_.data()+4,protocol_revision,2);put(bytes_.data()+6,std::uint32_t(count_),2);put(bytes_.data()+8,std::uint32_t(max_size),2);put(bytes_.data()+10,0,2);
   std::memcpy(bytes_.data()+digest_at,digest.data(),digest_size);
   // Rule summary: per bucket a non-zero FNV-1a-32 fold of its entries (sorted), zero for an empty bucket.
   std::array<std::uint32_t,buckets> fnv{};std::array<bool,buckets> used{};
   for(std::size_t i=0;i<count_;++i){
    const auto b=bucket_of(entries_[i].id);if(!used[b]){fnv[b]=2166136261u;used[b]=true;}
    std::array<std::uint8_t,entry_size> one{};encode(one.data(),entries_[i]);for(const auto c:one)fnv[b]=(fnv[b]^c)*16777619u;
    // A detail joins its rule's bucket, so a detail-only difference still names the rule (advisory, as every bucket).
    if(const auto* d=detail(entries_[i].id))for(const auto c:*d)fnv[b]=(fnv[b]^c)*16777619u;
   }
   for(std::size_t b=0;b<buckets;++b){const auto h=fnv[b];const auto folded=std::uint8_t(h^(h>>8)^(h>>16)^(h>>24));bytes_[summary_at+b]=used[b] ? (folded ? folded : 1) : 0;}
   return true;
  }
  std::span<const std::uint8_t> bytes() const { return ready() ? std::span<const std::uint8_t>(bytes_.data(),max_size) : std::span<const std::uint8_t>{}; }
  result compare(std::span<const std::uint8_t> peer,difference* diff=nullptr) const
  {
   if(diff)*diff={};
   if(!ready())return result::unavailable;
   if(peer.size()<header_size)return result::malformed;
   if(get(peer,0,4)!=magic || get(peer,4,2)!=protocol_revision)return result::protocol;
   const auto n=get(peer,6,2);
   if(n==0 || peer.size()!=max_size || get(peer,8,2)!=max_size || get(peer,10,2)!=0)return result::malformed;
   const bool same=std::memcmp(peer.data()+digest_at,bytes_.data()+digest_at,digest_size)==0;
   if(same && n==count_ && std::memcmp(peer.data()+summary_at,bytes_.data()+summary_at,buckets)==0)return result::match;
   if(same)return result::malformed; // the digest covers the count and every entry: an equal digest with another count or summary is not a record we wrote
   difference d;
   for(std::size_t b=0;b<buckets;++b){
    const auto mine=bytes_[summary_at+b],theirs=peer[summary_at+b];const auto bit=std::uint64_t(1)<<b;
    if(mine!=theirs)d.buckets|=bit;
    if(mine && !theirs)d.local_only|=bit;
    if(!mine && theirs)d.peer_only|=bit;
   }
   if(diff)*diff=d;
   return n!=count_ || d.local_only || d.peer_only ? result::entries : result::value;
  }
  // Names the differing rules for a log line or a dialog ("9 DeathRule, 15 DeathSwitchClear (not on the other PC)");
  // ids: with the rule IDs. Empty when no bucket differs (a summary collision: the digest still refused).
  void describe(std::uint64_t mask,std::uint64_t local_only,bool ids,char* out,std::size_t size) const
  {
   if(!out || !size)return;
   out[0]=0;std::size_t used=0;
   auto add=[&](const char* text){for(;*text && used+1<size;++text)out[used++]=*text;out[used]=0;};
   auto number=[&](unsigned v){char digits[8]{};int n=0;do{digits[n++]=char('0'+v%10);v/=10;}while(v && n<7);while(n){const char one[2]{digits[--n],0};add(one);}};
   for(std::size_t b=0;b<buckets;++b){
    const auto bit=std::uint64_t(1)<<b;if(!(mask & bit))continue;
    bool any=false;
    for(std::size_t i=0;i<count_;++i){
     if(bucket_of(entries_[i].id)!=b)continue;
     if(used)add(", ");
     if(ids){number(entries_[i].id);add(" ");}
     add(rule_name(entries_[i].id));if(local_only & bit)add(" (not on the other PC)");any=true;
    }
    if(!any){
     if(used)add(", ");if(ids){number(unsigned(b+1));add(" ");}
     // These requested-only rules are absent when OFF, but this DLL still knows
     // their names. The bucket summary remains advisory; the digest admits peers.
     if(b+1==object_world_impact_id || b+1==stun_rule_id || b+1==danger_room_party_id || b+1==lockon_aim_id || b+1==box_harm_origin_id || b+1==danger_room_healthbars_id || b+1==harm_single_chain_id || b+1==fast_transfer_id || b+1==gns_transport_id || b+1==join_in_progress_id || b+1==online_conversations_id || b+1==model_pools_id || b+1==tick_rate_id || b+1==world_backfaces_id){add(rule_name(static_cast<std::uint16_t>(b+1)));add(" (disabled on this PC)");}
     else add("a rule this PC does not have");
    }
   }
  }
 };
}
