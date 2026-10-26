#include "xml1_economy_rules.hpp"
#include "gameplay_capabilities.hpp"
#include "lcsave/internal.hpp"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <fstream>
using namespace xml1_economy_rules;
int checks=0,failures=0;
void check(bool yes,const char* what){++checks;if(!yes){++failures;std::printf("FAIL %s\n",what);}}
std::string file(std::string body,unsigned count=1){return "x1economy 1\ncount "+std::to_string(count)+"\nsha256 "+digest_hex(sha256_fips::digest(reinterpret_cast<const std::uint8_t*>(body.data()),body.size()))+"\n"+body;}
void builder_contract(const std::filesystem::path& build){
 std::ifstream input(build/L"Data"/L"x1economy.txt",std::ios::binary);
 const std::string bytes{std::istreambuf_iterator<char>(input),{}};
 auto data=parse(bytes);check(data.ok(),"actual builder sidecar parses");
 if(!data.ok()){std::printf("builder sidecar line %u: %s\n",data.error.line,data.error.message.c_str());return;}
 lcsave::Tables tables;std::string error;
 check(lcsave::load_tables(build.wstring(),tables,error),"actual builder catalogue/binds load");
 if(!tables.loaded()){std::printf("builder tables: %s\n",error.c_str());return;}
 const auto& catalogue=tables.data->catalogue;const auto& items=tables.data->build->T.ITEM;
 check(items.size()>160&&tables.data->build->T.ENHC.size()>512,"actual W2 catalogue/binds exceed both retail table ceilings");
 const auto lookup=[&](std::string_view name)->std::optional<unsigned>{
  const auto id=catalogue.lookup(2,lcsave::key_hash1(name,lcsave::Enc::cp1252));
  const auto found=std::find(items.begin(),items.end(),id);
  if(!id||found==items.end())return {};return static_cast<unsigned>(found-items.begin());
 };
 check(data.resolve(lookup),"actual builder rows resolve in PC definition-index order");
 if(!data.ok()){std::printf("builder resolution line %u: %s\n",data.error.line,data.error.message.c_str());return;}
 unsigned inert=0,equipment=0,packs=0,rebuilt=0,catalogue_rebuilt=0,awarded_group_minus1=0,noaward=0;
 bool flags_match=true,forge=true,helmet_noaward=false;
 // Catalogue attribute mask0x1 (ITEM_OWN_REBUILT) and sidecar mask0x4 are
 // different contracts. Reading the catalogue must preserve all attribute bits.
 constexpr unsigned item_own_rebuilt=1;
 for(const auto& entry:catalogue.entries)if(entry.domain==2&&(entry.attributes&item_own_rebuilt))++catalogue_rebuilt;
 for(const auto& row:data.rows){
  inert+=row.flags==0;equipment+=(row.flags&0x40)!=0;rebuilt+=(row.flags&4)!=0;noaward+=(row.flags&8)!=0;
  packs+=row.shop==vendor::healer&&!row.cost;
  const auto* entry=catalogue.entry_of(items[row.definition]);
  flags_match&=entry&&bool(entry->attributes&item_own_rebuilt)==bool(row.flags&4);
  if(row.flags&0x40)forge&=row.shop==vendor::forge;
  if((row.flags&0x40)&&row.group==-1&&!(row.flags&8))++awarded_group_minus1;
  if(row.name=="lc_x1_magnetos_helmet")helmet_noaward=(row.flags&8)!=0;
 }
 check(data.rows.size()==131&&equipment==116,"final builder shape:131 rows including116 equipment");
 check(inert>0&&packs==2,"inert flags-zero rows and absent-cost packs accepted");
 check(forge&&awarded_group_minus1>0&&noaward==1&&helmet_noaward,"all equipment incl group-1 use Forge; helmet noaward retained");
 check(rebuilt==19&&catalogue_rebuilt==19&&flags_match,"catalogue ITEM_OWN_REBUILT=1 preserved and agrees with19 sidecar rebuilt rows");
 check(agreement_values(plan::zero_draw,data)[3]==131,"actual builder rule52 count is131, not132");
 std::printf("Builder contract: table %zu/%zu; defs %zu; equipment %u; inert %u; packs %u; rebuilt %u; noaward %u; digest %s\n",items.size(),tables.data->build->T.ENHC.size(),data.rows.size(),equipment,inert,packs,rebuilt,noaward,digest_hex(data.digest).substr(0,8).c_str());
}
int main(int argc,char** argv){
 const std::string row="lc_x1_item 1 2 51 1500 forge -2 16\n";
 auto good=parse(file(row));check(good.ok(),"valid sidecar");check(good.rows.size()==1&&good.rows[0].group==1&&good.rows[0].frequency==2&&good.rows[0].flags==0x51&&good.rows[0].cost==1500&&good.rows[0].shop==vendor::forge&&good.rows[0].quality==-2&&good.rows[0].source==16,"all seven metadata fields");
 auto reject=[&](std::string text,unsigned line){const auto p=parse(text);check(!p.ok()&&p.error.line==line,"refusal and line number");};
 reject("",1);reject("x1economy 2\n",1);reject("\xef\xbb\xbf"+file(row),1);
 reject("x1economy 1\n",2);reject("x1economy 1\ncount -1\n",2);reject("x1economy 1\ncount 1025\n",2);reject("x1economy 1\ncount 1 \n",2);
 reject("x1economy 1\ncount 1\n",3);reject("x1economy 1\ncount 1\nsha256 ab\n",3);
 auto crlf=file(row);crlf.insert(crlf.find('\n'),"\r");reject(crlf,1);
 reject(file("lc_x1_item 1 2 51 1500 forge -2 16 \n"),4);
 reject(file("lc_x1_item  1 2 51 1500 forge -2 16\n"),4);
 reject(file("lc_x1_item 6 2 51 1500 forge -2 16\n"),4);
 reject(file("lc_x1_item 1 0 51 1500 forge -2 16\n"),4);
 reject(file("lc_x1_item 1 2 80 1500 forge -2 16\n"),4);
 reject(file("lc_x1_item 1 2 5A 1500 forge -2 16\n"),4);
 reject(file("lc_x1_item 1 2 0x51 1500 forge -2 16\n"),4);
 reject(file("lc_x1_item 1 2 51 -1 forge -2 16\n"),4);
 reject(file("lc_x1_item 1 2 51 1500 shop -2 16\n"),4);
 reject(file("lc_x1_item 1 2 51 1500 forge 3 16\n"),4);
 reject(file("lc_x1_item 1 2 51 1500 forge -2 junk\n"),4);
 reject(file("lc_x1_item 1 2 51 1500 forge -2 16 extra\n"),4);
 reject(file("lc_x1_item 1 2 51 2147483648 forge -2 16\n"),4);
 reject(file(row+row,2),5);reject(file(row,2),2);
 auto digest_bad=file(row);digest_bad[digest_bad.find("sha256 ")+7]^=1;reject(digest_bad,3);
 auto uppercase=file(row);uppercase[uppercase.find("sha256 ")+7]='A';reject(uppercase,3);
 auto no_lf=file(row);no_lf.pop_back();reject(no_lf,4);
 reject(file(std::string("bad\tname 1 2 51 1500 forge -2 16\n")),4);
 reject(file(std::string("bad")+char(0x80)+" 1 2 51 1500 forge -2 16\n"),4);
 check(parse(file("pack - - 0 - healer - -1\n")).ok(),"optional tokens and signed source integer");
 const auto forms=parse(file("inert - - 0 - none - 0\npack 1 4 10 - healer - 1\nreward -1 2 45 0 forge - 2\nnoaward -1 2 59 0 forge - 3\n",4));
 check(forms.ok()&&forms.rows[0].flags==0&&!forms.rows[0].group&&!forms.rows[0].cost,"flags-zero inert row accepted without invented metadata");
 check(forms.ok()&&!forms.rows[1].cost&&forms.rows[1].shop==vendor::healer,"pack cost remains absent for the code price");
 check(forms.ok()&&forms.rows[2].group==-1&&forms.rows[2].shop==vendor::forge,"group-1 equipment vendor is accepted as Forge");
 check(forms.ok()&&(forms.rows[3].flags&8),"noaward flag retained");
 check(parse(file(std::string(400,'a')+" -1 5 7f 0 none 2 2147483647\n")).ok(),"full long name, all known flags and maximum source integer");
 reject(file("a - - 0 - none - 2147483648\n"),4);
 auto resolve=[](std::string_view name)->std::optional<unsigned>{if(name=="a")return 3;if(name=="b")return 9;if(name=="alias")return 3;return {};};
 auto resolved=parse(file("a -1 5 40 - none - 1\nb - 4 20 0 none 0 2\n",2));check(resolved.resolve(resolve)&&resolved.rows[1].definition==9,"native index walk order");
 auto missing=parse(file(row));check(!missing.resolve(resolve)&&missing.error.line==4,"unresolved name");
 auto order=parse(file("b - - 0 - none - 1\na - - 0 - none - 2\n",2));check(!order.resolve(resolve)&&order.error.line==5,"order violation");
 auto alias=parse(file("a - - 0 - none - 1\nalias - - 0 - none - 2\n",2));check(!alias.resolve(resolve)&&alias.error.line==5,"duplicate resolved index");
 check(preflight(1023,4091,5,1024,4096)&&!preflight(1024,0,1,1024,4096)&&!preflight(1023,4092,5,1024,4096),"W2 exact free-space edges");
 check(plain(1,0,0,0,0)&&!plain(2,0,0,0,0)&&!plain(1,1,0,0,0)&&!plain(1,0,1,0,0)&&!plain(1,0,0,1,0)&&!plain(1,0,0,0,1),"only exact plain argument form selected");
 gameplay_capabilities::record off,reference;off.finalize();reference.finalize();check(std::memcmp(off.bytes().data(),reference.bytes().data(),off.bytes().size())==0,"OFF digest unchanged");
 const auto values=agreement_values(plan::zero_draw,good);check(values[0]==32&&values[1]==1&&values[3]==1,"step 3a values");
 gameplay_capabilities::record on;check(on.add(52,1,values,true)&&on.finalize(),"requested verified rule");
 for(unsigned i=0;i<4;++i){auto other=values;other[i]^=1;gameplay_capabilities::record peer;peer.add(52,1,other,true);peer.finalize();gameplay_capabilities::difference diff;check(on.compare(peer.bytes(),&diff)!=gameplay_capabilities::result::match,"each value differs");char description[128];on.describe(diff.buckets,0,true,description,sizeof description);check(std::strstr(description,"52 Xml1Economy")!=nullptr,"refusal names rule 52");}
 gameplay_capabilities::record bad;check(!bad.add(52,1,values,false)&&!bad.finalize()&&bad.bytes().empty(),"unverified closes agreement");
 gameplay_capabilities::difference disabled_diff;off.compare(on.bytes(),&disabled_diff);char disabled[128];off.describe(disabled_diff.buckets,0,true,disabled,sizeof disabled);check(std::strstr(disabled,"52 Xml1Economy (disabled on this PC)")!=nullptr,"OFF peer also names rule 52");
 check(agreement_values(plan::one_roll,good)[0]==0,"fallback bit clear");
 if(argc==3&&std::strcmp(argv[1],"--builder")==0)builder_contract(std::filesystem::path(argv[2]));
 std::printf("Economy rules: %d checks, %d failure(s)\n",checks,failures);return failures?1:0;
}
