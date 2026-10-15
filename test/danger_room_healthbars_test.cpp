#include "danger_room_healthbars_rules.hpp"
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
  bool fail_acquire = false, fail_flush = false, fail_restore = false, corrupt_write = false;
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
   if (corrupt_write) { corrupt_write = false; bytes[a] ^= 1; }
   return true;
  }
  bool acquire(u32, u32& old) { ++acquires; old = 1; return !fail_acquire; }
  bool restore(u32, u32 old) { ++restores; return old == 1 && !fail_restore; }
  bool flush() { if (fail_flush) { fail_flush = false; return false; } return true; }
 };
 memory retail()
 {
  memory m;
  const auto plan = danger_room_healthbars_rules::patches();
  for (const auto& p : plan)
   for (std::size_t i = 0; i < p.before.size(); ++i) m.bytes[p.address + u32(i)] = p.before[i];
  return m;
 }
}
int main()
{
 using namespace danger_room_healthbars_rules;
 assert(parse("") == choice::off && parse(" XML2 ; native") == choice::off);
 assert(parse(" XmL1 ; enabled") == choice::xml1 && parse("typo") == choice::invalid);
 const auto plan = patches();
 auto m = retail(); bool safe = false;
 assert(xml1_hook_transaction::apply(m, plan, &safe) && safe);
 assert(m.acquires == m.restores);
 for (std::size_t i = 0; i < plan[0].before.size(); ++i)
  assert(m.bytes[site + u32(i)] == (i < 7 ? plan[0].before[i] : 0x90));
 // Every byte of the condition is guarded before any write/protection change.
 for (std::size_t i = 0; i < plan[0].before.size(); ++i)
 {
  auto bad = retail(); bad.bytes[site + u32(i)] ^= 1; const auto before = bad.bytes;
  assert(!xml1_hook_transaction::apply(bad, plan, &safe) && safe);
  assert(bad.bytes == before && bad.writes == 0 && bad.acquires == 0);
 }
 for (int failure = 0; failure < 4; ++failure)
 {
  auto bad = retail(); const auto before = bad.bytes;
  bad.fail_write = failure == 0 ? 1 : 0;
  bad.fail_acquire = failure == 1;
  bad.fail_flush = failure == 2;
  bad.corrupt_write = failure == 3;
  assert(!xml1_hook_transaction::apply(bad, plan, &safe) && safe);
  assert(bad.bytes == before);
 }
 auto unsafe = retail(); unsafe.fail_restore = true;
 assert(!xml1_hook_transaction::apply(unsafe, plan, &safe) && !safe);
 std::cout << "danger room health bars: guards, rollback and protection refusal PASS\n";
}
