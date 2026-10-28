#pragma once
// Data/x1economy.txt revision 1. No Windows/game dependencies: builder contract,
// definition-order resolution, constructor selection and capacity arithmetic.
#include "sha256.hpp"
#include <array>
#include <charconv>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace xml1_economy_rules {
inline constexpr unsigned rule_id=52,rule_revision=1,format_revision=1;
enum class plan {zero_draw,one_roll};
// Coordinator may select the accepted fallback here; agreement and dispatch use
// this single selection. No independent switch can misreport the installed plan.
inline constexpr plan default_plan=plan::zero_draw;
enum class vendor {none,forge,healer};
struct record {
 std::string name;
 std::optional<int> group,frequency,cost,quality;
 unsigned flags=0,definition=0,line=0;
 vendor shop=vendor::none;
 int source=0;
};
struct refusal {unsigned line=0;std::string message;};
inline std::string digest_hex(const std::array<std::uint8_t,32>& digest){
 std::string out;out.reserve(64);for(auto c:digest){out.push_back("0123456789abcdef"[c>>4]);out.push_back("0123456789abcdef"[c&15]);}return out;
}
struct sidecar {
 std::vector<record> rows;
 std::array<std::uint8_t,32> digest{};
 refusal error;
 bool ok()const{return error.message.empty();}
 template<class Lookup>bool resolve(Lookup lookup){
  if(!ok())return false;
  std::optional<unsigned> previous;
  for(auto& r:rows){const auto index=lookup(r.name);
   if(!index||*index>=1024){error={r.line,"unresolved definition name: "+r.name};return false;}
   if(previous&&*index<=*previous){error={r.line,"row out of PC definition-index order (or alias): "+r.name};return false;}
   r.definition=*index;previous=index;
  }return true;
 }
};
inline bool integer(std::string_view token,int& value,int base=10){
 if(token.empty())return false;
 const auto result=std::from_chars(token.data(),token.data()+token.size(),value,base);
 return result.ec==std::errc{}&&result.ptr==token.data()+token.size();
}
inline sidecar parse(std::string_view text){
 sidecar out;
 auto fail=[&](unsigned line,const char* why){out.error={line,why};return out;};
 std::vector<std::string_view> lines;std::size_t start=0;unsigned line=1;
 for(std::size_t i=0;i<text.size();++i){const auto c=static_cast<unsigned char>(text[i]);
  if(c=='\n'){lines.push_back(text.substr(start,i-start));start=i+1;++line;}
  else if(c<32||c>126)return fail(line,"expected ASCII and LF only (no BOM, CR or control bytes)");
 }
 if(start!=text.size())return fail(line,"missing final LF");
 if(lines.empty()||lines[0]!="x1economy 1")return fail(1,"bad x1economy format header");
 int count=0;
 if(lines.size()<2||!lines[1].starts_with("count ")||!integer(lines[1].substr(6),count)||count<0||count>1024)return fail(2,"bad count header");
 if(lines.size()<3||!lines[2].starts_with("sha256 ")||lines[2].size()!=71)return fail(3,"bad sha256 header");
 for(auto c:lines[2].substr(7))if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return fail(3,"digest must be lowercase hex");
 const auto body_start=lines[0].size()+lines[1].size()+lines[2].size()+3;
 const auto body=text.substr(body_start);
 out.digest=sha256_fips::digest(reinterpret_cast<const std::uint8_t*>(body.data()),body.size());
 if(digest_hex(out.digest)!=lines[2].substr(7))return fail(3,"body SHA-256 mismatch");
 if(lines.size()-3!=static_cast<unsigned>(count))return fail(2,"body count mismatch");
 std::set<std::string> names;
 for(unsigned n=3;n<lines.size();++n){
  std::array<std::string_view,8> tokens{};unsigned used=0;std::size_t begin=0;
  const auto row=lines[n];
  for(std::size_t i=0;i<=row.size();++i)if(i==row.size()||row[i]==' '){
   if(i==begin||used==tokens.size())return fail(n+1,"expected eight tokens with single spaces and no trailing space");
   tokens[used++]=row.substr(begin,i-begin);begin=i+1;
  }
  if(used!=8)return fail(n+1,"expected eight tokens");
  record r;r.name=tokens[0];r.line=n+1;
  if(!names.insert(r.name).second)return fail(n+1,"duplicate definition name");
  auto optional=[&](unsigned i,int lo,int hi,std::optional<int>& destination){
   if(tokens[i]=="-")return true;int v=0;if(!integer(tokens[i],v)||v<lo||v>hi)return false;destination=v;return true;
  };
  if(!optional(1,-1,5,r.group))return fail(n+1,"unknown group token");
  if(!optional(2,1,5,r.frequency))return fail(n+1,"unknown frequency token");
  for(auto c:tokens[3])if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return fail(n+1,"flags must be lowercase hex without prefix");
  int flags=0;if(!integer(tokens[3],flags,16)||flags<0||flags>0x7f)return fail(n+1,"unknown flag bits");r.flags=static_cast<unsigned>(flags);
  if(!optional(4,0,2147483647,r.cost))return fail(n+1,"unknown cost token");
  if(tokens[5]=="forge")r.shop=vendor::forge;else if(tokens[5]=="healer")r.shop=vendor::healer;else if(tokens[5]!="none")return fail(n+1,"unknown vendor token");
  if(!optional(6,-2,2,r.quality))return fail(n+1,"unknown quality token");
  if(!integer(tokens[7],r.source))return fail(n+1,"unknown source token");
  out.rows.push_back(std::move(r));
 }
 return out;
}
inline std::array<std::uint32_t,4> agreement_values(plan mode,const sidecar& data){
 // Digest bytes packed little endian, like the agreement's other u32 values.
 const auto& d=data.digest;
 return {mode==plan::zero_draw?32u:0u,format_revision,std::uint32_t(d[0])|(std::uint32_t(d[1])<<8)|(std::uint32_t(d[2])<<16)|(std::uint32_t(d[3])<<24),static_cast<unsigned>(data.rows.size())};
}
inline bool preflight(unsigned live_instances,unsigned live_attached,unsigned needed,unsigned instance_capacity,unsigned attached_capacity){
 return live_instances<instance_capacity&&live_attached<=attached_capacity&&needed<=attached_capacity-live_attached;
}
inline bool plain(int level,int prefixes,int suffixes,int special_a,int special_b){return level==1&&!prefixes&&!suffixes&&!special_a&&!special_b;}
}
