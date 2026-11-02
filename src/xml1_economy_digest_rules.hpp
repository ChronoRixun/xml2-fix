#pragma once
#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <string_view>
#include <vector>

namespace xml1_economy::digest_rules {
constexpr unsigned name_slots=0x2000,name_arena=0x14400;
inline const char* list_name(int tab,bool stash){
 return tab==0?(stash?"withdraw":"buy"):tab==1?(stash?"deposit":"sell"):tab==2&&!stash?"training":"-";
}
// FNV-1a/64 over canonical little-endian words and length-prefixed byte strings.
// Never feed native structs: they contain pointers, padding and byte-sized fields.
class hash {
 std::uint64_t state=14695981039346656037ull;
public:
 void byte(unsigned char b){state=(state^b)*1099511628211ull;}
 void word(std::uint32_t w){for(unsigned n=0;n<4;++n)byte(static_cast<unsigned char>(w>>(8*n)));}
 void text(std::string_view s){word(static_cast<std::uint32_t>(s.size()));for(unsigned char c:s)byte(c);}
 std::uint64_t value()const{return state;}
};
inline std::string hex(std::uint64_t v){char s[17];std::snprintf(s,sizeof s,"%016llx",static_cast<unsigned long long>(v));return s;}
inline std::string json_string(std::string_view s){
 std::string out="\"";for(unsigned char c:s){if(c=='"'||c=='\\'){out+='\\';out+=static_cast<char>(c);}else if(c<32){char b[7];if(c=='\n')out+="\\n";else {std::snprintf(b,sizeof b,"\\u%04x",c);out+=b;}}else out+=static_cast<char>(c);}return out+'"';
}
struct definition {unsigned index=0;std::string name;unsigned quantity=0;bool obtained=false;};
struct row {unsigned type=0,index=0;std::string name="-";int owner=-1;unsigned detail=0,quantity=0,state=0;int price=0;};
inline std::uint64_t definitions_hash(const std::vector<definition>& defs){hash h;h.text("defs-v1");h.word(static_cast<unsigned>(defs.size()));for(const auto& d:defs){h.word(d.index);h.text(d.name);h.word(d.quantity);h.word(d.obtained?1:0);}return h.value();}
inline std::uint64_t stock_hash(const std::vector<row>& stock,const std::vector<row>& rows){
 hash h;h.text("stock-v1");for(const auto* list:{&stock,&rows}){h.word(static_cast<unsigned>(list->size()));for(const auto& r:*list){h.word(r.type);h.word(r.index);h.text(r.name);h.word(r.owner);h.word(r.detail);h.word(r.quantity);h.word(r.state);h.word(r.price);}}return h.value();
}
struct event {
 std::uint64_t seq=0;int player=-1,gate=-1,tab=-1;std::string list="-";int row_index=-1;std::string definition="-";int owner=-1;
 std::string result="refused";int money=0;std::uint64_t defs=0,stock=0;unsigned rng=0;
};
inline std::string format(const event& e){
 return "econ seq="+std::to_string(e.seq)+" player="+std::to_string(e.player)+" gate="+std::to_string(e.gate)+" tab="+std::to_string(e.tab)+" list="+e.list+" row="+std::to_string(e.row_index)+":"+e.definition+" owner="+std::to_string(e.owner)+" result="+e.result+" money="+std::to_string(e.money)+" defs="+hex(e.defs)+" stock="+hex(e.stock)+" rng="+std::to_string(e.rng);
}
class history {
 std::deque<event> entries;
public:
 void append(const event& e){entries.push_back(e);if(entries.size()>256)entries.pop_front();}
 std::size_t size()const{return entries.size();}
 std::uint64_t first()const{return entries.empty()?0:entries.front().seq;}
 std::uint64_t last()const{return entries.empty()?0:entries.back().seq;}
 std::string json(std::uint64_t after)const{
  std::string out="{\"first\":"+std::to_string(first())+",\"last\":"+std::to_string(last())+",\"lost\":"+(first()>1&&after<first()-1?"true":"false")+",\"lines\":[";bool comma=false;
  for(const auto& e:entries)if(e.seq>after){if(comma)out+=',';comma=true;out+=json_string(format(e));}return out+"]}";
 }
};
}
