#include "danger_room_party_rules.hpp"
#include <cassert>
#include <iostream>
#include <map>

namespace
{
 using u32 = std::uint32_t;
 struct memory
 {
  std::map<u32, unsigned char> bytes;
  int writes = 0, acquires = 0, restores = 0, fail_write = 0;
  bool read(u32 a, void* out, std::size_t n)
  {
   for (std::size_t i = 0; i < n; ++i)
   {
    if (!bytes.contains(a + u32(i))) return false;
    static_cast<unsigned char*>(out)[i] = bytes[a + u32(i)];
   }
   return true;
  }
  bool write(u32 a, const void* in, std::size_t n)
  {
   if (++writes == fail_write) return false;
   for (std::size_t i = 0; i < n; ++i) bytes[a + u32(i)] = static_cast<const unsigned char*>(in)[i];
   return true;
  }
  bool acquire(u32, u32& old) { ++acquires; old = PAGE_EXECUTE_READ; return true; }
  bool restore(u32, u32 old) { ++restores; return old == PAGE_EXECUTE_READ; }
  bool flush() { return true; }
 };
 memory retail()
 {
  memory m;
  for (const auto& site : danger_room_party_rules::sites)
  {
   auto bytes = danger_room_party_rules::push(site.expected);
   for (std::size_t i = 0; i < bytes.size(); ++i) m.bytes[site.site + u32(i)] = bytes[i];
  }
  return m;
 }
}
int main()
{
 using namespace danger_room_party_rules;
 assert(parse("") == choice::off && parse(" XML2 ; untouched") == choice::off);
 assert(parse(" XmL1 ; enabled") == choice::xml1 && parse("typo") == choice::invalid);
 const std::array<u32, 4> names{0x10101010, 0x20202020, 0x30303030, 0x40404040};
 const auto plan = patches(names);
 auto m = retail(); bool safe = false;
 assert(xml1_hook_transaction::apply(m, plan, &safe) && safe);
 assert(m.acquires == m.restores);
 for (std::size_t i = 0; i < sites.size(); ++i)
 {
  std::array<unsigned char, 5> got{};
  assert(m.read(sites[i].site, got.data(), got.size()));
  u32 target = 0; std::memcpy(&target, got.data() + 1, 4);
  assert(got[0] == 0x68 && target == names[i]);
 }
 for (const auto& site : sites)
 {
  auto bad = retail(); bad.bytes[site.site] ^= 1;
  const auto before = bad.bytes;
  assert(!xml1_hook_transaction::apply(bad, plan, &safe) && safe);
  assert(bad.bytes == before && bad.writes == 0 && bad.acquires == 0);
 }
 for (int failure = 1; failure <= 4; ++failure)
 {
  auto bad = retail(); const auto before = bad.bytes; bad.fail_write = failure;
  assert(!xml1_hook_transaction::apply(bad, plan, &safe) && safe);
  assert(bad.bytes == before && bad.acquires == bad.restores);
 }
 std::cout << "danger room party: guarded four-site install and rollback PASS\n";
}
