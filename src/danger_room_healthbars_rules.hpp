#pragma once
#include "ini_rules.hpp"
#include "xml1_hook_transaction.hpp"
#include <cctype>
#include <string>

namespace danger_room_healthbars_rules
{
 enum class choice { off, xml1, invalid };
 inline choice parse(std::string_view raw)
 {
  std::string key(ini_rules::value_text(raw));
  for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (key.empty() || key == "xml2") return choice::off;
  return key == "xml1" ? choice::xml1 : choice::invalid;
 }
 // CHudTarget::update: the course-only undying test suppresses a selected
 // training opponent's gauge. Keep its protection and the other HUD gates.
 constexpr std::uint32_t site = 0x5a8b5b;
 inline std::vector<xml1_hook_transaction::patch> patches()
 {
  std::vector<unsigned char> before{0xf6,0x85,0x0c,0x03,0,0,0x10,0x0f,0x85,0xf6,0x08,0,0};
  auto after = before;
  for (std::size_t i = 7; i < after.size(); ++i) after[i] = 0x90;
  return {{site, before, after}};
 }
}
