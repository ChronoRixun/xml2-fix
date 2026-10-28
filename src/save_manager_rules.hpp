#pragma once

// W2-E15-01/03/04: regenerate the reviewed Phase A save operands from a
// native profile. Retail instructions remain the guard; E10A2 is the T1 oracle.
#include "entity_capacity_sites.hpp"
#include "save_format_rules.hpp"
#include "patch_transaction.hpp"
#include <stdexcept>

namespace save_manager_rules
{
 using save_format_rules::native_profile;
 constexpr std::uint32_t records_end(const native_profile& p) { return p.record_count*p.record_size; }
 constexpr std::uint32_t snapshot(const native_profile& p) { return records_end(p)+8; }
 constexpr std::uint32_t ready(const native_profile& p) { return snapshot(p)+p.snapshot_size; }
 constexpr std::uint32_t fog(const native_profile& p) { return save_format_rules::smart_count(p)+4; }
 // The side-mission block in the stream: two snapshots, a pad and the count (retail / E10A2 0x4e0, W2 0xa88).
 constexpr std::uint32_t side_block(const native_profile& p) { return 2*p.snapshot_size+8; }
 static_assert(side_block(save_format_rules::retail_profile)==0x4e0 && side_block(save_format_rules::e10a2_profile)==0x4e0 &&
               side_block(save_format_rules::w2_profile)==0xa88 && side_block(save_format_rules::w2_profile)==save_format_rules::w2_profile.side_count_offset+4);
 // The serializer's side-mission cursor advance, add dword ptr [ebx+0x2fc00], 0x4e0: its displacement is the payload
 // operand below and its immediate the side block. W2 widens the block's bound check and copy (save_limits 0x46bcea /
 // 0x46bcfa) and the loader's advance (0x46e423); leaving this immediate at 0x4e0 let the next section overwrite the
 // block's last 0x5a8 bytes, so every W2 save read a garbage side-mission count and was refused.
 constexpr std::uint32_t side_cursor_va = 0x46bd01, side_cursor_immediate = 6;
 struct operand { std::uint32_t old_value, value; };
 inline std::vector<operand> operands(const native_profile& p, std::uint32_t va)
 {
  using namespace save_format_rules;
  return {{0x4804,p.state_count-4},{0x4808,p.state_count},{0x480c,p.ordinal_map},
   {0x4a10,dynamic_list(p)},{0x4c6c,dynamic_count(p)},{0x4c70,smart_list(p)},
   {0x5274,smart_count(p)},{0x5278,fog(p)},{0x55c4,fog(p)+0x34c},
   {0x55d8,p.time_stamp},{0x55dc,p.time_stamp+4},{0x55e0,p.record_size},
   {0xffffaa20,0u-p.record_size},{0xabb8,p.record_size+p.time_stamp},
   {0x1ad64,records_end(p)+4},{0x1ad68,snapshot(p)},
   {0x1afd4,ready(p)},{0x1afd8,ready(p)+4},{0x1afdc,p.manager_size},
   {0x2fc00,p.payload},{0x2fc04,p.description},{0x2fc44,p.flag_offset},{0x2fc78,p.wrapper},
   {0xbf1e,p.wrapper/4},{0x300,p.states},{0x100,va==0x465f32?p.ordinals:1024}};
 }
 inline std::vector<std::uint8_t> literal(const entity_capacity_sites::literal_site& s, const native_profile& p)
 {
  const auto old=patch_transaction::bytes_of(s.old_hex);
  const auto reviewed=patch_transaction::bytes_of(s.new_hex);
  const auto reference=operands(save_format_rules::e10a2_profile,s.va);
  const auto target=operands(p,s.va);
  // Identify an operand by reproducing the entire reviewed instruction, not
  // by a substring or an instruction-length guess (C6/C7 include an immediate).
  for(std::size_t n=0;n<reference.size();++n) for(std::size_t off=0;off+4<=old.size();++off)
  {
   auto b=old; std::uint32_t v=0;std::memcpy(&v,b.data()+off,4);
   if(v!=reference[n].old_value)continue;
   std::memcpy(b.data()+off,&reference[n].value,4);
   if(b!=reviewed)continue;
   std::memcpy(b.data()+off,&target[n].value,4);
   if(s.va==side_cursor_va)
   {
    std::uint32_t retail=0,phase_a=0;
    if(b.size()!=side_cursor_immediate+4)throw std::runtime_error("side-mission cursor advance is not add [reg+disp32], imm32");
    std::memcpy(&retail,old.data()+side_cursor_immediate,4);std::memcpy(&phase_a,reviewed.data()+side_cursor_immediate,4);
    if(retail!=side_block(save_format_rules::retail_profile)||phase_a!=side_block(save_format_rules::e10a2_profile))
     throw std::runtime_error("side-mission cursor advance immediate is not the reviewed 0x4e0");
    const auto side=side_block(p);std::memcpy(b.data()+side_cursor_immediate,&side,4);
   }
   return b;
  }
  throw std::runtime_error("unclassified Phase A save operand at "+patch_transaction::hex(s.va));
 }
 inline std::vector<std::uint8_t> party_names(const native_profile& p)
 {
  auto b=patch_transaction::bytes_of("8d84170cae0100");
  const std::uint32_t offset=snapshot(p)+0xa4;std::memcpy(b.data()+3,&offset,4);return b;
 }
 inline bool replaced_body(std::uint32_t va)
 { return va>=0x465ec0 && va<0x465f89; }
}
