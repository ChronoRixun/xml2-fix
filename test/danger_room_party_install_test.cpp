#include "danger_room_party.hpp"
#include "danger_room_party_rules.hpp"
#include "gameplay_capabilities.hpp"
#include "ini.hpp"
#include <cassert>
#include <iostream>

namespace
{
 std::optional<std::string> option;
 int refused_online = 0;
 bool fatal = false;
}
namespace ini { std::optional<std::string> text(const wchar_t*, const wchar_t*) { return option; } }
namespace logger { void write(const char*, ...) {} }
namespace gameplay_agreement { void reject_unregistered_rule(const char*) { ++refused_online; } }
namespace game_version { void refuse_startup() { fatal = true; } }

int main(int argc, char** argv)
{
 using namespace danger_room_party_rules;
 assert(argc == 2);
 const std::string mode = argv[1];
 if (mode != "off") option = mode == "invalid" ? "typo" : "xml1";
 std::vector<std::vector<unsigned char>> before;
 for (const auto& s : sites)
 {
  DWORD old = 0;
  assert(VirtualProtect(reinterpret_cast<void*>(s.site & ~4095u), 4096, PAGE_EXECUTE_READWRITE, &old));
  const auto bytes = push(s.expected);
  std::memcpy(reinterpret_cast<void*>(s.site), bytes.data(), bytes.size());
 }
 if (mode.starts_with("guard"))
 {
  const auto index = unsigned(mode.back() - '0'); assert(index < sites.size());
  *reinterpret_cast<unsigned char*>(sites[index].site) ^= 1;
 }
 for (const auto& s : sites)
  before.emplace_back(reinterpret_cast<unsigned char*>(s.site), reinterpret_cast<unsigned char*>(s.site + 5));
 danger_room_party::install(reinterpret_cast<HMODULE>(mode == "base" ? 0x500000 : 0x400000));
 assert(!fatal);
 assert(danger_room_party::requested() == (mode != "off"));
 assert(refused_online == 0);
 assert(danger_room_party::verified() == (mode == "off" || mode == "on"));
 gameplay_capabilities::record agreement;
 if (danger_room_party::requested())
  agreement.add(gameplay_capabilities::danger_room_party_id,1,
   {danger_room_party::installed() ? 1u : 0u,0,0,0},danger_room_party::verified());
 assert(agreement.finalize() == (mode == "off" || mode == "on"));
 if (mode == "off")
 {
  gameplay_capabilities::record base; base.finalize();
  assert(std::equal(agreement.bytes().begin(),agreement.bytes().end(),base.bytes().begin(),base.bytes().end()));
 }
 if (mode == "on")
 {
  assert(danger_room_party::installed());
  const std::array<const char*, 4> expected{"cyclops", "phoenix", "storm", "wolverine"};
  for (std::size_t i = 0; i < sites.size(); ++i)
  {
   const char* name = *reinterpret_cast<const char**>(sites[i].site + 1);
   assert(std::strcmp(name, expected[i]) == 0);
  }
 }
 else
 {
  assert(!danger_room_party::installed());
  for (std::size_t i = 0; i < sites.size(); ++i)
   assert(std::memcmp(reinterpret_cast<void*>(sites[i].site), before[i].data(), 5) == 0);
 }
 std::cout << "production party installer " << mode << ": PASS\n";
}
