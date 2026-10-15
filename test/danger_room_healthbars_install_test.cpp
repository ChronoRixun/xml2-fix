#include "danger_room_healthbars.hpp"
#include "danger_room_healthbars_rules.hpp"
#include "ini.hpp"
#include <cassert>
#include <iostream>

namespace { std::optional<std::string> option; bool fatal = false; int logs = 0; }
namespace ini { std::optional<std::string> text(const wchar_t*, const wchar_t*) { return option; } }
namespace logger { void write(const char*, ...) { ++logs; } }
namespace game_version { void refuse_startup() { fatal = true; } }

int main(int argc, char** argv)
{
 using namespace danger_room_healthbars_rules;
 assert(argc == 2);
 const std::string mode = argv[1];
 if (mode != "off") option = mode == "invalid" ? "typo" : mode == "xml2" ? "xml2" : "xml1";
 const auto plan = patches(); const auto& p = plan[0];
 DWORD old = 0;
 assert(VirtualProtect(reinterpret_cast<void*>(site & ~4095u), 4096, PAGE_EXECUTE_READWRITE, &old));
 std::memcpy(reinterpret_cast<void*>(site), p.before.data(), p.before.size());
 if (mode == "guard0") *reinterpret_cast<unsigned char*>(site) ^= 1;
 if (mode == "guard1") *reinterpret_cast<unsigned char*>(site + 7) ^= 1;
 std::vector<unsigned char> before(reinterpret_cast<unsigned char*>(site), reinterpret_cast<unsigned char*>(site + p.before.size()));
 assert(VirtualProtect(reinterpret_cast<void*>(site & ~4095u), 4096, old, &old));
 danger_room_healthbars::install(reinterpret_cast<HMODULE>(mode == "base" ? 0x500000 : 0x400000));
 assert(!fatal && danger_room_healthbars::installed() == (mode == "on"));
 const auto& expected = mode == "on" ? p.after : before;
 assert(std::memcmp(reinterpret_cast<void*>(site), expected.data(), expected.size()) == 0);
 assert(logs == ((mode == "off" || mode == "xml2") ? 0 : 1));
 if (mode == "on") { danger_room_healthbars::install(reinterpret_cast<HMODULE>(0x400000)); assert(logs == 1); }
 std::cout << "production health-bar installer " << mode << ": PASS\n";
}
