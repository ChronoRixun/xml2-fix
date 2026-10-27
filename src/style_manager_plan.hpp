#pragma once
#include "limits_rules.hpp"
#include "patch_transaction.hpp"

// One owner and one transaction for both keys. Code is emitted into unpublished
// executable storage; only a successful transaction publishes the descriptor.
namespace style_manager_plan
{
 using namespace limits_rules;
 struct request
 {
  int styles = stock_fight_styles, events = stock_style_events;
  bool valid = true, requested = false;
 };
 inline request choose(std::optional<std::string_view> styles, std::optional<std::string_view> events)
 {
  request r;
  const auto parse = [&](auto text, int stock, int maximum, int& out)
  {
   if (!text) return;
   const auto n = parse_count(*text);
   if (!n || *n < stock || *n > maximum) { r.valid = false; r.requested = true; return; }
   out = *n; r.requested |= out != stock;
  };
  parse(styles, stock_fight_styles, max_fight_styles, r.styles);
  parse(events, stock_style_events, max_style_events, r.events);
  return r;
 }
 inline constexpr auto& cave_guards = style_manager_cave_guards;
 inline void dword(std::vector<std::uint8_t>& b, DWORD n)
 { for (unsigned i=0;i<4;++i) b.push_back(static_cast<std::uint8_t>(n>>(i*8))); }
 inline void jump(std::vector<std::uint8_t>& b, DWORD origin, DWORD target)
 { const auto at=origin+static_cast<DWORD>(b.size()); b.push_back(0xe9); dword(b,target-at-5); }
 inline std::vector<std::uint8_t> clear_code(unsigned index, const style_layout& l, DWORD origin)
 {
  // Preserve all registers and EFLAGS: the replaced MOV did not change either.
  std::vector<std::uint8_t> b{0x9c,0x50,0x51,0x57};
  const std::array<std::uint8_t,4> modrm{0xbf,0xbe,0xb9,0xbe};
  const std::array<DWORD,4> offset{0,l.node_bitmap,l.node_bitmap,l.bitmap};
  b.push_back(0x8d); b.push_back(modrm[index]); dword(b,offset[index]);
  b.push_back(0xb9); dword(b,l.bitmap_words);
  b.insert(b.end(),{0x33,0xc0,0xfc,0xf3,0xab,0x5f,0x59,0x58,0x9d});
  jump(b,origin,cave_guards[index].va+static_cast<DWORD>(hex_size(cave_guards[index].hex)));
  return b;
 }
 inline std::vector<std::uint8_t> refusal_code(unsigned index, DWORD counter, DWORD origin)
 {
  std::vector<std::uint8_t> b{0x9c,0xff,0x05}; dword(b,counter); b.push_back(0x9d);
  auto original=patch_transaction::bytes_of(cave_guards[index].hex);
  b.insert(b.end(),original.begin(),original.end());
  if(index==5) jump(b,origin,cave_guards[index].va+static_cast<DWORD>(original.size()));
  return b;
 }
 struct plan
 {
  patch_transaction::contribution transaction;
  std::array<std::vector<std::uint8_t>,6> code;
 };
 inline plan make(const style_layout& l, DWORD construct, DWORD push, DWORD storage,
                  DWORD style_refusals, DWORD event_refusals)
 {
  plan p; auto& c=p.transaction; c.owner="style manager (FightStyles + StyleEvents)";
  const auto guard_rows=[&](const auto& rows) { for(const auto& s:rows) c.guards.push_back({s.va,patch_transaction::bytes_of(s.hex),"style manager ledger"}); };
  guard_rows(style_sites); guard_rows(style_guards);
  for(const auto& s:event_sites)
   if(l.events.capacity!=stock_style_events||s.field!=event_field::parser_size)
    c.guards.push_back({s.va,patch_transaction::bytes_of(s.hex),"style manager ledger"});
  guard_rows(cave_guards);
  if(l.events.capacity!=stock_style_events)guard_rows(event_owner_guards);
  const bool expanded=l.capacity>32;
  for(const auto& w:style_writes(l,construct,push))
  {
   bool cave=false;
   for(unsigned i=1;i<4;++i) cave|=expanded && w.va>=cave_guards[i].va && w.va<cave_guards[i].va+hex_size(cave_guards[i].hex);
   if(cave) continue;
   std::vector<std::uint8_t> b; for(unsigned j=0;j<w.size;++j)b.push_back(static_cast<std::uint8_t>(w.value>>(j*8)));
   c.writes.push_back({w.va,std::move(b),"style operand"});
  }
  if(l.events.capacity!=stock_style_events)
   for(const auto& s:event_sites)
   {
    std::vector<std::uint8_t> b; const auto v=value_of(s.field,l.events);
    for(unsigned j=0;j<s.size;++j)b.push_back(static_cast<std::uint8_t>(v>>(j*8)));
    c.writes.push_back({s.va+s.offset,std::move(b),"event operand"});
   }
  // Legacy installs through 32 styles retain the exact revision-1 byte plan.
  for(unsigned i=0;i<6;++i)
  {
   if(i<4 && !expanded)continue;
   if(i>=4 && !expanded && l.events.capacity==stock_style_events)continue;
   const auto origin=storage+i*128;
   p.code[i]=i<4?clear_code(i,l,origin):refusal_code(i,i==4?style_refusals:event_refusals,origin);
   c.writes.push_back(patch_transaction::relocation(cave_guards[i].va,"e9",true,origin,
    static_cast<unsigned>(hex_size(cave_guards[i].hex))-5,"style manager cave"));
  }
  return p;
 }
}
