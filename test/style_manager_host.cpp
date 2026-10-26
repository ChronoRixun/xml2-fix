// Execute the retail shared style manager at its own addresses, with synthetic
// strings/payloads only. Never starts the game, engine or executable entry point.
#include "style_manager_plan.hpp"
#include "limits_agreement_rules.hpp"
#include <Windows.h>
#include <cstdio>
#include <fstream>
#include <map>
#include <set>
#include <string>
#include <vector>
extern "C" unsigned char union_space[];
using namespace limits_rules;
using bytes=std::vector<std::uint8_t>;
static unsigned checks=0,failures=0;
static int capacity=19;
static DWORD style_refused=0,event_refused=0;
static std::map<std::string,DWORD> names;
static std::map<DWORD,unsigned> destroyed;
static void check(bool ok,const char* what) { ++checks; failures+=!ok; std::printf("%s %s\n",ok?"PASS":"FAIL",what);std::fflush(stdout); }
static DWORD address(const void* p) { return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(p)); }
static void* ptr(DWORD p) { return reinterpret_cast<void*>(p); }
static DWORD& word(void* p,DWORD offset=0) { return *reinterpret_cast<DWORD*>(static_cast<std::uint8_t*>(p)+offset); }
static bytes read(const char* path) { std::ifstream f(path,std::ios::binary);return bytes(std::istreambuf_iterator<char>(f),{}); }
static bool load(const bytes& raw)
{
 auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
 auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data()+dos->e_lfanew);
 DWORD old=0;if(!VirtualProtect(union_space,0x700000,PAGE_EXECUTE_READWRITE,&old))return false;
 std::memset(union_space,0,0x700000);
 const auto* sec=IMAGE_FIRST_SECTION(nt);
 for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)
  std::memcpy(ptr(image_base+sec[i].VirtualAddress),raw.data()+sec[i].PointerToRawData,sec[i].SizeOfRawData);
 return true;
}
static void jump(DWORD va,const void* target)
{
 const auto b=patch_transaction::relocation(va,"e9",true,address(target),0,"synthetic payload").bytes;
 std::memcpy(ptr(va),b.data(),b.size());
}
static void* __fastcall ring_construct(void* p,void*) { return style_ring_construct(p,capacity); }
static DWORD __fastcall ring_push(void* p,void*,const DWORD* v) { return style_ring_push(p,*v,capacity); }
static void __fastcall destroy(void* p,void*,DWORD) { ++destroyed[address(p)]; }
static void __fastcall set_name(void*,void*,DWORD) {}
static DWORD vtable[]{address(reinterpret_cast<void*>(&destroy)),0,address(reinterpret_cast<void*>(&set_name))};
static void* __fastcall payload(void* p,void*) { word(p)=address(vtable);return p; }
static void* __fastcall event_payload(void* p,void*) { return payload(p,nullptr); }
static bool __fastcall parse_style(void*,void*,const char*) { return true; }
static void __fastcall reset_payload(void*,void*) {}
static void __fastcall payload_name(void*,void*,const DWORD*) {}
static void __fastcall parse_event(void*,void*,const char*) {}
static DWORD __fastcall intern(void*,void*,const char* name,int,int)
{ auto [i,added]=names.emplace(name,static_cast<DWORD>(names.size()+1));return i->second; }
static void __fastcall string_value(void* p,void*,const char* name) { word(p)=intern(nullptr,nullptr,name,0,0); }
static int __cdecl compare(const char* a,const char* b) { return std::strcmp(a,b); }
static void dependencies()
{
 // Keep the retail registry, tree operations, bitmaps, event admission/rings and
 // generation logic. Replace only content parsing, string storage and payloads.
 jump(0x41a500,reinterpret_cast<void*>(&intern));
 jump(0x41b120,reinterpret_cast<void*>(&string_value));
 jump(0x500fe0,reinterpret_cast<void*>(&payload));
 jump(0x4feb80,reinterpret_cast<void*>(&payload_name));
 jump(0x501240,reinterpret_cast<void*>(&parse_style));
 jump(0x500cf0,reinterpret_cast<void*>(&reset_payload));
 jump(0x4f9800,reinterpret_cast<void*>(&event_payload));
 jump(0x53f8f0,reinterpret_cast<void*>(&parse_event));
 jump(0x672562,reinterpret_cast<void*>(&compare));
}
using method=void*(__fastcall*)(void*,void*);
using with_int=DWORD(__fastcall*)(void*,void*,DWORD);
using register_t=void*(__fastcall*)(void*,void*,DWORD,const char*,DWORD);
using named=void*(__fastcall*)(void*,void*,const char*);
static bool canary(const bytes& b,std::size_t size)
{ return std::all_of(b.begin(),b.begin()+64,[](auto v){return v==0xa5;})&&std::all_of(b.begin()+64+size,b.end(),[](auto v){return v==0xa5;}); }
static bool clear(void* p,DWORD offset,DWORD words)
{ for(DWORD i=0;i<words;++i)if(word(p,offset+i*4))return false;return true; }
static bool install(const style_layout& l,void* cave,bool negative=false)
{
 auto p=style_manager_plan::make(l,address(reinterpret_cast<void*>(&ring_construct)),address(reinterpret_cast<void*>(&ring_push)),address(cave),address(&style_refused),address(&event_refused));
 for(unsigned i=0;i<p.code.size();++i)if(!p.code[i].empty())std::memcpy(static_cast<std::uint8_t*>(cave)+i*128,p.code[i].data(),p.code[i].size());
 if(negative) { // Same layout and host, deliberately retain retail one-word destructor clear.
  auto& w=p.transaction.writes;
  w.erase(std::remove_if(w.begin(),w.end(),[](const auto& x){return x.va==0x4feab5;}),w.end());
 }
 p.transaction.guards.push_back({style_manager_pointer,{0,0,0,0},"manager absent"});
 patch_transaction::transaction t;t.add(std::move(p.transaction));
 const auto result=t.commit();
 if(result!=patch_transaction::outcome::committed)std::printf("transaction: %s\n",t.report().c_str());
 return result==patch_transaction::outcome::committed;
}
static void lifecycle(const bytes& retail,int n,int e,void* cave)
{
 load(retail);capacity=n;style_refused=event_refused=0;names.clear();destroyed.clear();
 const auto l=manager_layout(n,e);check(install(l,cave),"shared production transaction installs");dependencies();
 bytes backing(l.manager_size+128,0xa5);void* manager=backing.data()+64;
 std::memset(manager,0xcd,l.manager_size);
 check(reinterpret_cast<method>(0x4ffcc0)(manager,nullptr)==manager,"retail manager constructor returns owner");
 check(word(manager,l.count)==0&&clear(manager,style_pool_offset+l.node_bitmap,l.bitmap_words)&&clear(manager,l.bitmap,l.bitmap_words),"constructor clears both complete style bitmaps");
 void* events=static_cast<std::uint8_t*>(manager)+l.auxiliary;
 DWORD mask=0;unsigned shift=0;for(DWORD seed=e-1;seed;seed>>=1){mask=mask*2+1;++shift;}
 check(word(events,l.events.mask)==mask&&word(events,l.events.shift)==shift&&word(events,l.events.live)==0,"retail event constructor derives dynamic mask and shift");
 check(word(events,l.events.free)==static_cast<DWORD>(e)&&clear(events,l.events.built,l.events.words)&&clear(events,l.events.used,l.events.words),"retail event constructor initializes all bitmap/ring words");
 std::vector<void*> slots;bool good=true;
 for(int i=0;i<n;++i){auto name="style"+std::to_string(i);auto* p=reinterpret_cast<register_t>(0x4ffb30)(manager,nullptr,0,name.c_str(),0);slots.push_back(p);good&=p==static_cast<std::uint8_t*>(manager)+l.styles+i*style_record_size&&word(manager,l.count)==static_cast<DWORD>(i+1);}
 check(good,"retail registration fills every style slot and exact live count");
 check(reinterpret_cast<register_t>(0x4ffb30)(manager,nullptr,0,"overflow",0)==nullptr,"retail registration refuses N+1");
 check(reinterpret_cast<register_t>(0x4ffb30)(manager,nullptr,0,"style0",0)==slots[0]&&word(manager,l.count)==static_cast<DWORD>(n),"duplicate registration reuses style at capacity");
 std::set<void*> iterated;
 for(void* p=reinterpret_cast<method>(0x4ffd30)(manager,nullptr);p;p=reinterpret_cast<method>(0x4ffda0)(manager,nullptr)) { iterated.insert(p);if(iterated.size()>static_cast<std::size_t>(n))break; }
 check(iterated==std::set<void*>(slots.begin(),slots.end()),"retail first/next iteration visits every style once");
 for(int index:{n>32?31:0,n>32?32:n-1})
 {
  auto name="style"+std::to_string(index);
  reinterpret_cast<named>(0x4ff930)(manager,nullptr,name.c_str());
  check(word(manager,l.count)==static_cast<DWORD>(n-1)&&!(word(manager,l.bitmap+(index/32)*4)&(1u<<(index%32))),"retail removal clears selected style bit and node");
  auto* p=reinterpret_cast<register_t>(0x4ffb30)(manager,nullptr,0,name.c_str(),0);
  check(p==slots[index]&&word(manager,l.count)==static_cast<DWORD>(n),"retail registration reuses released slot across bit 31/32");
 }
 // Event payload type is the retail allocator's first recognized type string.
 const char* type=reinterpret_cast<const char*>(0x692f18);
 std::vector<DWORD> handles;good=true;
 for(int i=0;i<e;++i){auto h=reinterpret_cast<with_int>(0x4faea0)(events,nullptr,address(type));handles.push_back(h);good&=h!=0&&(h&mask)==static_cast<DWORD>(i)&&word(events,l.events.live)==static_cast<DWORD>(i+1);}
 check(good&&std::set<DWORD>(handles.begin(),handles.end()).size()==static_cast<std::size_t>(e),"retail event allocator fills E unique generation handles");
 check(reinterpret_cast<with_int>(0x4faea0)(events,nullptr,address(type))==0&&word(events,l.events.live)==static_cast<DWORD>(e),"retail event allocator refuses E+1");
 good=true;for(int i=0;i<e;++i)good&=reinterpret_cast<with_int>(0x4faac0)(events,nullptr,handles[i])==address(events)+i*0x54;
 check(good,"retail event resolution validates every dynamic handle");
 for(auto h:handles)reinterpret_cast<with_int>(0x4fad20)(events,nullptr,h);
 good=word(events,l.events.live)==0&&clear(events,l.events.used,l.events.words)&&clear(events,l.events.built,l.events.words);
 for(int i=0;i<e;++i){auto h=reinterpret_cast<with_int>(0x4faea0)(events,nullptr,address(type));good&=h!=handles[i]&&(h&mask)==static_cast<DWORD>(i)&&reinterpret_cast<with_int>(0x4faac0)(events,nullptr,handles[i])==0;}
 check(good,"retail event release/reuse advances generations and rejects stale handles");
 reinterpret_cast<method>(0x4ff860)(manager,nullptr);
 check(word(manager,l.count)==0&&clear(manager,l.bitmap,l.bitmap_words)&&clear(manager,style_pool_offset+l.node_bitmap,l.bitmap_words),"retail unload-all clears all style words");
 check(word(events,l.events.live)==static_cast<DWORD>(e),"style unload preserves event part");
 reinterpret_cast<method>(0x4ffc60)(manager,nullptr);
 check(clear(events,l.events.built,l.events.words)&&canary(backing,l.manager_size),"retail manager destructor clears events and preserves both outer canaries");
 if(n>32||e!=1064)check(style_refused==1&&event_refused==1,"production refusal caves count both capacity refusals");
 std::printf("MATRIX styles=%d events=%d size=0x%lx\n",n,e,l.manager_size);
}
static void negative_control(const bytes& retail,void* cave)
{
 load(retail);capacity=33;const auto l=manager_layout(33,1064);check(install(l,cave,true),"negative control installs same layout with retail one-word clear");
 bytes b(33*style_record_size+8,0);auto* p=b.data();word(p,32*style_record_size)=address(vtable);word(p,33*style_record_size+4)=1;destroyed.clear();
 reinterpret_cast<method>(0x4fe9c0)(p,nullptr);reinterpret_cast<method>(0x4fe9c0)(p,nullptr);
 check(destroyed[address(p)+32*style_record_size]==2&&word(p,33*style_record_size+4)==1,"NEGATIVE CONTROL: retail clear double-destroys slot 32");
}
static void refusals(const bytes& retail,void* cave)
{
 const auto l=manager_layout(64,4096);
 for(int mode=0;mode<5;++mode)
 {
  load(retail);auto p=style_manager_plan::make(l,address(reinterpret_cast<void*>(&ring_construct)),address(reinterpret_cast<void*>(&ring_push)),address(cave),address(&style_refused),address(&event_refused));
  p.transaction.guards.push_back({style_manager_pointer,{0,0,0,0},"manager absent"});
  if(mode==0)word(ptr(style_manager_pointer))=1;
  if(mode==1)*static_cast<std::uint8_t*>(ptr(0x4faeb9))^=1;
  if(mode==2)p.transaction.guards.push_back({0x4ffb8a,{0x90},"overlapping foreign guard"});
  const bytes before(union_space,union_space+0x300000);bool published=false;p.transaction.publish=[&]{published=true;};
  patch_transaction::transaction t;t.add(std::move(p.transaction));patch_transaction::hooks h;
  if(mode==3)h.protect=[](void*,std::size_t,DWORD,DWORD*){return false;};
  if(mode==4)h.flush=[](void*,std::size_t){return false;};
  auto result=t.commit(mode>=3?&h:nullptr);
  check(result!=patch_transaction::outcome::committed&&!published&&std::equal(before.begin(),before.end(),union_space),"install refusal/rollback leaves every guarded code byte unchanged and unpublished");
 }
 for(const auto& pair:std::array<std::pair<const char*,const char*>,5>{{{"128","4096"},{"64","8193"},{"18","4096"},{"64","1063"},{"bad","4096"}}})
  check(!style_manager_plan::choose(pair.first,pair.second).valid,"shared request rejects invalid key before writes");
}

static void agreement_tests()
{
 using namespace gameplay_capabilities;
 for(int n:{32,33,64,127})
 {
  auto wanted=limits_agreement_rules::stock;wanted[3]=n;
  record a;
  limits_agreement_rules::register_rules(wanted,wanted,[&](auto id,auto rev,auto v,bool ok){a.add(id,static_cast<std::uint16_t>(rev),v,ok);});
  check(a.count()==2&&a.rule(1).revision==(n>32?2:1)&&a.rule(1).values[0]==static_cast<DWORD>(n),"rule 35 uses revision 1 through 32, revision 2 above 32");
  record old;old.add(fight_styles_id,1,{32,0,0,0},true);old.finalize();
  a.add(style_events_id,1,{4096,0,0,0},true);a.finalize();
  difference ab,ba;
  check(a.compare(old.bytes(),&ab)!=result::match&&old.compare(a.bytes(),&ba)!=result::match,"style/event agreement mismatch refused both directions");
  char text[512]{};old.describe(ba.buckets,ba.local_only,true,text,sizeof text);
  check(std::strstr(text,"StyleEvents")!=nullptr,"disabled peer names StyleEvents in refusal");
 }
 for(bool has_style:{false,true})
 {
  auto wanted=limits_agreement_rules::stock;if(has_style)wanted[3]=64;
  unsigned unverified=0;
  limits_agreement_rules::register_rules(wanted,limits_agreement_rules::stock,[&](auto id,auto,auto,bool ok){if(id==fight_styles_id&&!ok)++unverified;},false);
  check(unverified==1,"shared failure registers FightStyles unverified even for event-only request");
  limits_agreement_rules::register_events(1064,1064,false,[&](auto id,auto,auto,bool ok){if(id==style_events_id&&!ok)++unverified;});
  check(unverified==2,"shared failure also registers StyleEvents unverified for style-only request");
 }
 record baseline,off;
 limits_agreement_rules::register_rules(limits_agreement_rules::stock,limits_agreement_rules::stock,[&](auto id,auto rev,auto v,bool ok){off.add(id,static_cast<std::uint16_t>(rev),v,ok);});
 baseline.finalize();off.finalize();
 check(std::equal(baseline.bytes().begin(),baseline.bytes().end(),off.bytes().begin()),"both absent keys preserve agreement bytes");
}
static int dump_plan(int n,int e,const char* path)
{
 auto l=manager_layout(n,e);
 auto p=style_manager_plan::make(l,0x12340000,0x12340100,0x23450000,0x34560000,0x34560004);
 FILE* f=std::fopen(path,"wb");if(!f)return 2;
 for(const auto& w:p.transaction.writes){std::fprintf(f,"W %08lx ",w.va);for(auto b:w.bytes)std::fprintf(f,"%02x",b);std::fprintf(f,"\n");}
 for(const auto& g:p.transaction.guards){std::fprintf(f,"G %08lx ",g.va);for(auto b:g.bytes)std::fprintf(f,"%02x",b);std::fprintf(f,"\n");}
 for(auto field:{style_field::node_ring,style_field::node_ring_write,style_field::node_ring_read,style_field::node_ring_count,style_field::node_bitmap,style_field::node_count,style_field::count_or_styles_via_map,style_field::styles,style_field::bitmap,style_field::iterator_index,style_field::iterator_owner,style_field::auxiliary,style_field::manager_size})std::fprintf(f,"S %lx\n",value_of(field,l));
 for(auto field:{event_field::built,event_field::ring,event_field::write,event_field::read,event_field::free,event_field::used,event_field::live,event_field::ids,event_field::mask,event_field::shift,event_field::words})std::fprintf(f,"E %lx\n",value_of(field,l.events));
 std::fclose(f);return 0;
}
int main(int argc,char** argv)
{
 if(argc==5&&std::strcmp(argv[1],"--plan")==0)return dump_plan(std::atoi(argv[2]),std::atoi(argv[3]),argv[4]);
 const auto* path=argc>1?argv[1]:std::getenv("XML2_FIX_TEST_EXE");if(!path){std::puts("FAIL XML2_FIX_TEST_EXE required");return 2;}
 const auto retail=read(path);if(retail.size()<0x400)return 2;
 void* cave=VirtualAlloc(nullptr,6*128,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);
 for(int n:{19,32,33,63,64,65,127})for(int e:{1064,1065,4096})lifecycle(retail,n,e,cave);
 lifecycle(retail,127,8192,cave);negative_control(retail,cave);refusals(retail,cave);agreement_tests();
 VirtualFree(cave,0,MEM_RELEASE);
 std::printf("style manager host: %u checks, %u failures\n",checks,failures);return failures?1:0;
}
