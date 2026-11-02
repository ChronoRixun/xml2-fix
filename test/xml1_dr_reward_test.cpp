// Execute the complete retail DR dispatcher (4c8f50..4c9284).
// Synthetic engine services delimit the fixture; no game process is launched.
#include "xml1_roll.hpp"
#include "xml1_economy.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <intrin.h>
extern "C" unsigned char fixture_space[0x700000];
namespace logger {void write(const char*,...) {}}
namespace item_capacity {void* prepared_definition_picker(){return nullptr;}}
namespace xml1_economy {
bool installed(){return true;}
const std::vector<xml1_economy_rules::record>& records(){static std::vector<xml1_economy_rules::record> r;return r;}
bool is_economy(const void*){return false;}
descriptor* __fastcall make_instance(void*,void*,descriptor* out,void*,int,int,int,int,int){*out={};return out;}
}
namespace {
using xml1_economy::descriptor;
int checks=0,failures=0;
void check(bool b,const char* s){++checks;if(!b)++failures;std::printf("%s %s\n",b?"PASS":"FAIL",s);}
void* at(unsigned n){return reinterpret_cast<void*>(n);}
template<class T>T read(const void* p,unsigned o=0){T t;std::memcpy(&t,static_cast<const unsigned char*>(p)+o,sizeof t);return t;}
template<class T>void put(void* p,unsigned o,T t){std::memcpy(static_cast<unsigned char*>(p)+o,&t,sizeof t);}
void jump(unsigned n,const void* p){auto w=patch_transaction::relocation(n,"e9",true,static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(p)),0,"fixture service");std::memcpy(at(n),w.bytes.data(),w.bytes.size());FlushInstructionCache(GetCurrentProcess(),at(n),w.bytes.size());}
bool load(const char* path){std::ifstream f(path,std::ios::binary);std::vector<unsigned char>b{std::istreambuf_iterator<char>(f),{}};if(b.size()<0x1000)return false;auto pe=read<unsigned>(b.data(),0x3c),sections=pe+24+read<unsigned short>(b.data(),pe+20);for(unsigned i=0;i<read<unsigned short>(b.data(),pe+6);++i){unsigned r=sections+40*i,va=0x400000+read<unsigned>(b.data(),r+12),size=read<unsigned>(b.data(),r+16),off=read<unsigned>(b.data(),r+20);if(off+std::uint64_t(size)>b.size()||va<0x401000||va+std::uint64_t(size)>0xb01000)return false;std::memcpy(at(va),b.data()+off,size);}return true;}
void* manager_vt[0x140/4]{};void* registry_vt[0xe0/4]{};void* pool_vt[1]{};
struct object {void** vt;};object manager{manager_vt},registry{registry_vt},pool{pool_vt};
std::array<unsigned char,0x2a4> hero{};
struct observations {unsigned points=0,point_calls=0,characters=0,character_calls=0,named=0,generated=0,grants=0,costumes=0,bonus=0;bool operator==(const observations& b)const{return std::memcmp(this,&b,sizeof b)==0;}} seen;
unsigned bonus=0;bool missing=false,eligible=true;
void* __cdecl get_manager(){return &manager;}
void* __cdecl get_registry(){return &registry;}
bool __fastcall eligibility(void*,void*,void*){return eligible;}
void __fastcall points(void*,void*,unsigned n,int one){check(one==1,"points reward keeps native constant");seen.points+=n;++seen.point_calls;}
void __fastcall character(void*,void*,unsigned n){seen.characters=n;++seen.character_calls;}
void* __fastcall definition(void*,void*,unsigned index){check(index==7,"named reward definition index preserved");return missing?nullptr:at(0x12340000);}
bool __fastcall valid(void*,void*,unsigned h){return h==99;}
bool __fastcall unavailable(void*,void*){return false;}
descriptor* __fastcall named(void*,void*,descriptor* out,void* def,int level,int p,int s,int a,int b){check(def==at(0x12340000)&&level==1&&p==0&&s==0&&a==0&&b==0,"native named constructor arguments unchanged");++seen.named;*out={99,&pool};return out;}
descriptor* __fastcall generated(void*,void*,descriptor* out,int level,int count,int zero,int kind,int rarity,int tail){check(level==17&&count==1&&zero==0&&kind==2&&rarity==4&&tail==0,"OFF generated reward arguments follow retail path");++seen.generated;*out={99,&pool};return out;}
int __fastcall reward_level(void*,void*){return 17;}
void __fastcall grant(void*,void*,unsigned h,void* p,int one,int two){check(h==99&&p==&pool&&one==1&&two==2,"native inventory reward grant arguments unchanged");++seen.grants;}
void* __fastcall resolve_hero(void*,void*,unsigned h){check(h==42,"reward character handle preserved");return hero.data();}
void __fastcall costume(void*,void*,void* h,int one,int z1,int z2,int z3){check(h==hero.data()&&one==1&&!z1&&!z2&&!z3,"native costume receiver and arguments unchanged");++seen.costumes;}
int __fastcall roster_count(void*,void*){return 1;}
unsigned __fastcall roster_handle(void*,void*,int index){check(index==0,"native roster loop index preserved");return 42;}
bool __fastcall active(void*,void*,unsigned h){return h==42;}
void* __fastcall get_stats(void*,void*){return &bonus;}
unsigned __fastcall get_bonus(void*,void*){return bonus;}
void __fastcall set_bonus(void*,void*,unsigned n){bonus=n;++seen.bonus;}
void __fastcall other(void*,void*,unsigned n){seen.characters=n;++seen.character_calls;}
void services(){manager_vt[0x18/4]=reinterpret_cast<void*>(&definition);manager_vt[0xe0/4]=reinterpret_cast<void*>(&named);manager_vt[0x124/4]=reinterpret_cast<void*>(&unavailable);manager_vt[0x138/4]=reinterpret_cast<void*>(&generated);manager_vt[0xec/4]=reinterpret_cast<void*>(&grant);pool_vt[0]=reinterpret_cast<void*>(&valid);registry_vt[0xd4/4]=reinterpret_cast<void*>(&points);registry_vt[0xd8/4]=reinterpret_cast<void*>(&character);registry_vt[0x74/4]=reinterpret_cast<void*>(&resolve_hero);registry_vt[0xdc/4]=reinterpret_cast<void*>(&costume);registry_vt[0x48/4]=reinterpret_cast<void*>(&roster_count);registry_vt[0x4c/4]=reinterpret_cast<void*>(&roster_handle);registry_vt[0x78/4]=reinterpret_cast<void*>(&active);registry_vt[0x2c/4]=reinterpret_cast<void*>(&other);put(hero.data(),0x2a0,0x1du);jump(0x480a00,&get_manager);jump(0x44b8f0,&get_registry);jump(0x4c86d0,&eligibility);jump(0x46eae0,&reward_level);jump(0x4692f0,&get_stats);jump(0x544a30,&get_bonus);jump(0x43a5b0,&set_bonus);}
// Three retail reward rows are evaluated by the real native loop. Only row 0
// is active; the other rows carry the already-awarded bit. Type/flags match
// native tests; eligibility, object lookup and inventory mutation are edges.
observations run(int kind){std::array<unsigned char,0x50> course{};auto* r=course.data()+4;put(course.data(),0x18+0x11,static_cast<unsigned char>(1));put(course.data(),0x2c+0x11,static_cast<unsigned char>(1));put(r,8,static_cast<unsigned char>(0xff));put(r,0,static_cast<unsigned char>(4));missing=false;eligible=true;bonus=10;seen={};switch(kind){case 0:put(r,4,123u);break;case 1:put(r,0,static_cast<unsigned char>(3));break;case 2:put(r,8,static_cast<unsigned char>(7));break;case 3:put(r,1,static_cast<unsigned char>(2));break;case 4:put(r,8,static_cast<unsigned char>(7));missing=true;break;case 5:put(r,1,static_cast<unsigned char>(6));put(r,0x11,static_cast<unsigned char>(2));put(course.data(),0x4c,static_cast<unsigned short>(42));break;case 6:put(r,1,static_cast<unsigned char>(6));put(r,0x11,static_cast<unsigned char>(2));break;case 7:put(r,1,static_cast<unsigned char>(6));put(r,0x11,static_cast<unsigned char>(4));break;case 8:put(r,1,static_cast<unsigned char>(6));put(r,0xa,static_cast<unsigned short>(23));break;case 9:put(r,4,123u);eligible=false;break;}
reinterpret_cast<void(__fastcall*)(void*,void*,unsigned,unsigned)>(at(0x4c8f50))(course.data(),nullptr,0x51515151,0x61616161);check(!eligible||((read<unsigned char>(r,0x11)&9)==9),"full dispatcher records native awarded flags");if(kind==7)check(bonus==11,"native bonus reward increments stat");return seen;}
unsigned rng_calls=0;bool lockstep=true;float placement_result=0;void* placement_stack=nullptr;unsigned saved_esp=0,returned_esp=0;
float __cdecl unit(char lock){check(reinterpret_cast<unsigned>(_ReturnAddress())==0x46be75,"RNG spy is reached by the native placement call instruction");++rng_calls;lockstep=lockstep&&lock==1;return .25f;}
__declspec(naked) void placement_entry(){__asm {
 pushad
 mov saved_esp,esp
 mov esp,placement_stack
 mov eax,046be66h
 jmp eax
}}
__declspec(naked) void placement_return(){__asm {
 fstp placement_result
 mov returned_esp,esp
 mov esp,saved_esp
 popad
 ret
}}
void placement(){jump(0x560190,&unit);jump(0x46be7e,&placement_return);std::array<unsigned char,0x1000>s{};placement_stack=s.data()+0x800;const auto scale=read<float>(at(0x687280));for(unsigned i=0;i<3;++i){placement_entry();check(returned_esp==reinterpret_cast<unsigned>(placement_stack)+8,"native placement consumes two pending cdecl arguments and its RNG argument");check(placement_result==.25f*scale,"native placement multiplies and consumes x87 draw");}check(rng_calls==3&&lockstep,"three native spawn placements retain exactly one lockstep RNG draw each");}
}
int main(int argc,char** argv){std::setvbuf(stdout,nullptr,_IONBF,0);SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);const char* path=argc>1?argv[1]:std::getenv("XML2_FIX_TEST_EXE");if(!path)return 2;DWORD old;VirtualProtect(fixture_space,sizeof fixture_space,PAGE_EXECUTE_READWRITE,&old);check(reinterpret_cast<unsigned>(fixture_space)==0x401000&&load(path),"map owner retail executable without launch");if(failures)return 1;const std::vector<unsigned char> original(fixture_space,fixture_space+sizeof fixture_space);services();std::array<observations,10> off;for(int i=0;i<10;++i)off[i]=run(i);check(off[0].points==123&&off[0].point_calls==1,"OFF full dispatcher awards points once");check(off[1].characters==3&&off[1].character_calls==1,"OFF full dispatcher awards character once");check(off[2].named==1&&off[2].grants==1,"OFF named reward constructs and grants once");check(off[3].generated==1&&off[3].grants==1,"OFF full dispatcher reaches gear generation spy");check(off[4]==observations{},"OFF missing named definition grants nothing");check(off[5].costumes==1&&off[6].costumes==1&&off[7].bonus==1&&off[8].characters==23,"OFF costume direct/roster bonus and other rewards execute");check(off[9]==observations{},"native eligibility rejection awards nothing");patch_transaction::transaction tx;std::string why;check(xml1_economy::roller::contribute(tx,why)&&tx.commit()==patch_transaction::outcome::committed,"install production roller transaction");if(failures)return 1;for(int i=0;i<10;++i){auto on=run(i);if(i==3)check(on==observations{},"ON complete dispatcher suppresses generated gear and grant");else check(on==off[i],"ON complete dispatcher preserves OFF non-generated reward observations");check(on.generated==0,"ON gear-generation spy remains zero for every reward kind");}check(std::equal(original.begin()+0x46be66-0x401000,original.begin()+0x46be7e-0x401000,static_cast<unsigned char*>(at(0x46be66))),"production transaction preserves native spawn placement instructions");placement();std::printf("DR execution: %d checks, %d failure(s)\n",checks,failures);return failures?1:0;}
