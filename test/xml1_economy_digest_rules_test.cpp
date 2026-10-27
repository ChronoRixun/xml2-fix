#include "xml1_economy_digest_rules.hpp"
#include <cstdio>
using namespace xml1_economy::digest_rules;
int main(){
 int failed=0,checks=0;auto check=[&](bool b,const char* s){++checks;failed+=!b;std::printf("%s %s\n",b?"PASS":"FAIL",s);};
 hash a;a.word(0x12345678);hash b;for(unsigned char c:{0x78,0x56,0x34,0x12})b.byte(c);
 check(a.value()==b.value(),"hash words use fixed little-endian bytes");
 hash c,d;c.text("ab");c.text("c");d.text("a");d.text("bc");check(c.value()!=d.value(),"length-prefixed names cannot alias boundaries");
 std::vector<definition> defs{{160,"unique",1,false},{161,"ordinary",2,true}};
 const auto base=definitions_hash(defs);defs[0].obtained=true;check(base!=definitions_hash(defs),"obtained participates in defs");
 defs[0].obtained=false;defs[1].quantity++;check(base!=definitions_hash(defs),"quantity participates in defs");
 std::vector<row> stock{{0,160,"unique",-1,0,1,3,137}},rows=stock;
 const auto sh=stock_hash(stock,rows);rows[0].owner=1;check(sh!=stock_hash(stock,rows),"normalized equipped owner participates in rows");
 rows=stock;rows[0].price++;check(sh!=stock_hash(stock,rows),"listed prices participate in stock");
 rows=stock;rows[0].detail=7;check(sh!=stock_hash(stock,rows),"course/detail participates in stock");
 event e;e.seq=1;e.player=2;e.gate=0;e.tab=1;e.list="shop_sell";e.row_index=3;e.definition="unique";e.owner=0;e.result="sold";e.money=100;e.defs=base;e.stock=sh;e.rng=42;
 const auto line=format(e);check(line.starts_with("econ seq=1 player=2 gate=0 tab=1 list=shop_sell row=3:unique owner=0 result=sold money=100 defs="),"canonical required field order");check(line.ends_with(" rng=42"),"native lockstep draw counter is explicit");
 history h;for(unsigned n=1;n<=300;++n){e.seq=n;h.append(e);}check(h.size()==256&&h.first()==45&&h.last()==300,"bounded history reports retained sequence range");
 check(h.json(0).find("\"lost\":true")!=std::string::npos,"history detects overwritten evidence");
 check(h.json(299).find("econ seq=299 ")==std::string::npos&&h.json(299).find("econ seq=300 ")!=std::string::npos,"events cursor is exclusive");
 check(json_string("a\"\\\n")=="\"a\\\"\\\\\\n\"","single-line JSON escapes pipe data");
 std::printf("Digest rules: %d checks, %d failure(s)\n",checks,failed);return failed?1:0;
}
