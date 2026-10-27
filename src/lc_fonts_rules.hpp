#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string_view>

namespace lc_fonts_rules {
inline constexpr unsigned stride=0x1c18, slots=8, manager_size=slots*stride+16;
inline constexpr std::array<std::string_view,4> names{"lc_title","lc_row","lc_body","lc_label"};
inline bool enabled(std::string_view s) { return s=="1"; }
inline bool key_equal(std::string_view a,std::string_view b) {
 if(a.size()!=b.size())return false;
 for(unsigned i=0;i<a.size();++i) {
  const auto upper=[](char c){return c>='a'&&c<='z'?static_cast<char>(c-'a'+'A'):c;};
  if(upper(a[i])!=upper(b[i]))return false;
 }
 return true;
}
inline int font_index(std::string_view name) {
 // Retail uses _stricmp, including for BIG/BiG. Admitted names are ASCII.
 for(unsigned i=0;i<names.size();++i)if(key_equal(name,names[i]))return 4+static_cast<int>(i);
 return key_equal(name,"big")?1:0;
}
inline float scale(std::string_view text) {
 if(text.empty()||text.size()>32)return 1;
 char buffer[33]{};std::memcpy(buffer,text.data(),text.size());char* end=nullptr;
 const float value=std::strtof(buffer,&end);
 return end==buffer+text.size()&&std::isfinite(value)&&value>=0.0625f&&value<=4?value:1;
}
inline unsigned char pad_glyph(std::string_view button) {
 for(unsigned i=0;i<4;++i)if(button==std::array<std::string_view,4>{"A","B","X","Y"}[i])return static_cast<unsigned char>(0x80+i);
 if(button=="Start")return 0x84;if(button=="Back")return 0x85;
 if(button.starts_with("D-pad"))return 0x86;
 return 0;
}
inline unsigned char key_glyph(std::string_view key) {
 constexpr std::array<std::string_view,16> special{"Enter","Esc","Space","Tab","Backspace","Delete","Insert","Home","End","Page Up","Page Down","Up","Down","Left","Right","Shift"};
 for(unsigned i=0;i<special.size();++i)if(key_equal(key,special[i]))return static_cast<unsigned char>(0x90+i);
 if(key_equal(key,"Return"))return 0x90;if(key_equal(key,"Escape"))return 0x91;
 if(key.size()==1&&key[0]>='a'&&key[0]<='z')return static_cast<unsigned char>(0xc0+key[0]-'a');
 if(key.size()==1&&key[0]>='A'&&key[0]<='Z')return static_cast<unsigned char>(0xc0+key[0]-'A');
 if(key.size()==1&&key[0]>='0'&&key[0]<='9')return static_cast<unsigned char>(0xe0+key[0]-'0');
 if(key_equal(key,"Ctrl")||key_equal(key,"Control"))return 0xf6;if(key_equal(key,"Alt"))return 0xf7;
 if(key_equal(key,"Ctrl+V"))return 0xf9;
 for(unsigned i=1;i<=12;++i) {
  const char* keys[]{"F1","F2","F3","F4","F5","F6","F7","F8","F9","F10","F11","F12"};
  if(key_equal(key,keys[i-1]))return static_cast<unsigned char>(0xe9+i);
 }
 return 0;
}
}
