#include "test_state.hpp"
#include "test_state_rules.hpp"
#include <Windows.h>
#include <mutex>
namespace test_state {
namespace {
struct memory {
 bool read(std::uint32_t a,void* out,std::size_t n) {
  __try {std::memcpy(out,reinterpret_cast<const void*>(a),n);return true;}
  __except(EXCEPTION_EXECUTE_HANDLER){return false;}
 }
};
std::mutex lock;
test_state_rules::tracker history;
std::string state="{\"schema\":1,\"error\":\"no game-thread sample\"}";
std::string objectives=state;
std::uint64_t last=0;
// Refuse unsupported code layouts before following any object pointers. These
// sites are read-only accessors, not any of the fix's patched call sites.
bool supported() {
 constexpr std::array<limits_rules::guard,7> guards{{
  {0x4c6bd0,"8b442404568bf18b9638","entity generation lookup"},
  {0x488aa0,"0fb681a90100008a8018","objective count lookup"},
  {0x488ab0,"0fb681a90100008a8018","objective completion lookup"},
  {0x488ad0,"0fb681a90100008a8018","objective hidden lookup"},
  {0x5d8640,"8b819060080085c07404","current menu name"},
  {0x4a36f0,"8b4c240483ec08566a00","health script reader"},
  {0x4a1ac0,"8b4c240483ec10566a00","position script reader"}
 }};
 memory mem;
 for(auto& g:guards) {
  std::uint8_t bytes[10]{};
  if(!mem.read(g.va,bytes,sizeof bytes)||!limits_rules::matches(bytes,g.hex))return false;
 }
 return true;
}
}
void sample() {
 const auto now=GetTickCount64();if(last&&now-last<100)return;last=now;
 static const bool valid=supported();
 if(!valid) {std::lock_guard guard(lock);state=objectives="{\"schema\":1,\"error\":\"unsupported executable layout\"}";return;}
 memory mem;test_state_rules::reader<memory> reader(mem);auto s=reader.read(now);
 std::lock_guard guard(lock);
 history.observe(s);state=test_state_rules::state_json(s);objectives=test_state_rules::objectives_json(s);
}
std::string reply(const std::string& command) {
 std::lock_guard guard(lock);
 if(command=="state")return state;
 if(command=="objectives")return objectives;
 return history.drain();
}
}
