#pragma once
#include "ini_rules.hpp"
#include "xml1_hook_transaction.hpp"
#include <array>
#include <cctype>
#include <string>

namespace danger_room_party_rules
{
 enum class choice { off, xml1, invalid };
 inline choice parse(std::string_view raw)
 {
  std::string key(ini_rules::value_text(raw));
  for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (key.empty() || key == "xml2") return choice::off;
  return key == "xml1" ? choice::xml1 : choice::invalid;
 }
 struct operand { std::uint32_t site, expected; };
 // Four default-string pushes in the non-solo course fallback, XMen2.exe 0x4d1d80.
 // XBE 0xc7414..0xc75af supplies Cyclops / Phoenix / Storm / Wolverine.
 constexpr std::array<operand, 4> sites{{
  {0x4d21d6, 0x6824a8}, {0x4d223d, 0x682550},
  {0x4d22a5, 0x6824c4}, {0x4d230d, 0x682564},
 }};
 inline std::vector<unsigned char> push(std::uint32_t value)
 {
  std::vector<unsigned char> bytes{0x68, 0, 0, 0, 0};
  std::memcpy(bytes.data() + 1, &value, 4);
  return bytes;
 }
 inline std::vector<xml1_hook_transaction::patch> patches(const std::array<std::uint32_t, 4>& names)
 {
  std::vector<xml1_hook_transaction::patch> out;
  for (std::size_t i = 0; i < sites.size(); ++i)
   out.push_back({sites[i].site, push(sites[i].expected), push(names[i])});
  return out;
 }
}
