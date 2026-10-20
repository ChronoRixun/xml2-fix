#pragma once
#include "ini_rules.hpp"
#include "xml1_hook_transaction.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <string>
#include <string_view>

// Original target HUD: default.xbe 0x15ff60 checks def_damage (18) in
// physical/energy/mental order. 0x94a70 formats the localized immunity or
// resistance label; three matching scopes collapse to string 103.
namespace resistance_display_rules
{
 enum class choice { off, xml1, invalid };
 inline choice parse(std::string_view raw)
 {
  std::string key(ini_rules::value_text(raw));
  for (auto& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (key.empty() || key == "xml2") return choice::off;
  return key == "xml1" ? choice::xml1 : choice::invalid;
 }
 struct range { bool present = false; float low = 1, high = 1; };
 struct scope { unsigned mask; int color, word, legacy; };
 // The PC swaps the energy/mental color and string indices. Its actual
 // colors match the corresponding original colors; do not change the map.
 constexpr std::array<scope,3> scopes{{{0x80,22,210,13},{0x8000,24,212,15},{0x800,23,211,14}}};
 enum class label { none, immune, resistant };
 inline range product(range a, range b)
 {
  const std::array<float,4> values{a.low*b.low,a.low*b.high,a.high*b.low,a.high*b.high};
  const auto ends = std::minmax_element(values.begin(),values.end());
  return {a.present || b.present,*ends.first,*ends.second};
 }
 inline label classify(const range& value)
 {
  if (!value.present || !std::isfinite(value.low) || !std::isfinite(value.high)) return label::none;
  if (value.high <= 0) return label::immune;
  return value.low > 0 && value.high < 1 ? label::resistant : label::none;
 }
 template<class Query> std::array<range,3> sample(Query& query)
 {
  std::array<range,3> values;
  for (std::size_t i=0;i<scopes.size();++i)
  {
   const auto& s=scopes[i];
   auto legacy=query(s.legacy,s.mask,0);
   // Gear converted to the PC's additive resist_* spelling still contributes
   // to the same displayed damage factor. This does not apply an affecter.
   legacy={legacy.present,1-legacy.high,1-legacy.low};
   values[i]=product(product(query(39,s.mask,1),query(56,s.mask,1)),legacy);
  }
  return values;
 }
 template<class Text> std::string render(const std::array<range,3>& values, Text& text)
 {
  if (std::count_if(values.begin(),values.end(),[](const range& r){return r.present;})==3)
   return text(103,0); // the original's three-scope summary
  std::string result;
  for (std::size_t i=0;i<values.size();++i)
  {
   const auto kind=classify(values[i]);
   if (kind==label::none) continue;
   char color[8]; std::snprintf(color,sizeof color,"~%02d",scopes[i].color);
   result+=color;
   result+=text(kind==label::immune ? 201 : 202,scopes[i].word);
   result+='\n';
  }
  return result;
 }
 constexpr std::uint32_t site=0x5a8dec, resume=0x5a8e14;
 inline std::vector<xml1_hook_transaction::patch> patches(const void* append)
 {
  std::vector<unsigned char> before{0xbd,0x0d,0,0,0,0xe8,0x8a,0xb1,0xf9,0xff,0x8b,0x10,0x6a,0,0x55,0x57,
   0x8b,0xc8,0xff,0x52,0x68,0x50,0x8d,0x8c,0x24,0x30,0x01,0,0,0xe8,0xb2,0xf9,0xff,0xff,0x45,0x83,0xfd,0x14,0x7c,0xdd};
  std::vector<unsigned char> after(before.size(),0x90);
  const unsigned char lead[]{0x57,0x8d,0x8c,0x24,0x30,0x01,0,0,0xe8};
  std::copy(std::begin(lead),std::end(lead),after.begin());
  const auto target=static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(append));
  auto displacement=target-(site+13);
  std::memcpy(after.data()+9,&displacement,4);
  after[13]=0xe9;displacement=resume-(site+18);
  std::memcpy(after.data()+14,&displacement,4);
  return {{site,std::move(before),std::move(after)}};
 }
}
