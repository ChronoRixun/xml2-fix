// Included in the mapped-retail constructor fixture's private namespace.
// All native code is loaded from the owner's PE at runtime; only environment
// edges (content caches, roster, UI, stream and state-list service) are faked.
struct price_row {unsigned type=0;descriptor item{};void* owner=nullptr;unsigned detail=0;};
static_assert(sizeof(price_row)==20);
constexpr unsigned health_index=first_economy+equipment_count,energy_index=health_index+1,pip_index=health_index+3;
std::array<descriptor,1024> cached_items{};
std::array<unsigned char,0x18f0> price_shop{};
std::array<unsigned char,0xc4> price_hero{};
std::array<unsigned char,0x610> price_game{};
std::array<void*,160> registry_vt{},gear_vt{},game_vt{};
void* registry_object[1]{registry_vt.data()};
void* gear_object[1]{gear_vt.data()};
descriptor equipped_item{};
int highest_level=0,credits=0,state_calls=0,last_state=0,removals=0;
bool state_saw_obtained=false;
descriptor state_item{};
void* __cdecl get_registry(){return registry_object;}
void* __cdecl get_price_game(){return price_game.data();}
int __fastcall roster_count(void*,void*){return 1;}
int __fastcall highest(void*,void*){return highest_level;}
unsigned __fastcall roster_handle(void*,void*,unsigned){return 0;}
void* __fastcall roster_hero(void*,void*,unsigned){return price_hero.data();}
descriptor* __fastcall gear_item(void*,void*,descriptor* out,unsigned){*out=equipped_item;return out;}
int __fastcall game_mode(void* self,void*){return read<int>(self,0x60c);}
descriptor* __fastcall pack_health(void*,void*,descriptor* out){*out=cached_items[health_index];return out;}
descriptor* __fastcall pack_energy(void*,void*,descriptor* out){*out=cached_items[energy_index];return out;}
std::uint16_t __fastcall price_lookup(void*,void*,const char* name){if(std::strcmp(name,"HEALTH_ITEM")==0)return health_index;if(std::strcmp(name,"ENERGY_ITEM")==0)return energy_index;return 0xffff;}
descriptor* __fastcall cached_item(void*,void*,descriptor* out,unsigned index){*out=cached_items[index];return out;}
void* definition(unsigned index){return manager.data()+item_capacity_rules::definitions+index*0x3c;}
void* instance(descriptor item){return native<void*(__fastcall*)(void*,void*)>(0x480f30)(&item,nullptr);}
descriptor allocate(unsigned index,int state=2){
 descriptor item{};
 if(index==legacy_definition)native<make_t>(0x47bb50)(manager.data(),nullptr,&item,definition(index),1,0,0,0,0);
 else native<descriptor*(__fastcall*)(void*,void*,descriptor*)>(0x47c990)(manager.data(),nullptr,&item);
 put(instance(item),4,definition(index));put(instance(item),0x2e,static_cast<unsigned char>(state));
 native<void(__fastcall*)(void*,void*)>(0x481170)(instance(item),nullptr);return item;
}
int value(descriptor item){return native<int(__fastcall*)(void*,void*,descriptor)>(0x480d10)(instance(item),nullptr,item);}
int sale(descriptor item){price_row row;row.item=item;return native<int(__fastcall*)(void*,void*)>(0x5a9cc0)(&row,nullptr);}
bool obtained(unsigned index){return (read<unsigned char>(definition(index),0x3b)&8)!=0;}
void set_obtained(unsigned index,bool value){native<void(__fastcall*)(void*,void*,int)>(0x47ac50)(definition(index),nullptr,value?1:0);}
void __fastcall credit(void*,void*,int amount){credits+=amount;}
void __fastcall feedback(void*,void*,int){}
bool __fastcall change_state(void*,void*,descriptor item,int state){
 ++state_calls;last_state=state;state_item=item;
 auto* d=read<void*>(instance(item),4);state_saw_obtained=(read<unsigned char>(d,0x3b)&8)!=0;
 put(instance(item),0x2e,static_cast<unsigned char>(state));return true;
}
void __fastcall remove_equipped(void*,void*,descriptor){++removals;equipped_item={};}
__declspec(naked) void transaction_end(){__asm {
 add esp,40h
 pop edi
 pop esi
 pop ebx
 pop ebp
 ret 12
}}
// Authored caller supplies the native transaction locals and nonvolatile state.
// The retail sale/buy blocks choose state 2/4/5 and run through the installed
// owner capture, equipped removal, credit/debit and shared call site.
__declspec(naked) void __fastcall transact(void*,void*,descriptor*,void*,int){__asm {
 push ebp
 push ebx
 push esi
 push edi
 sub esp,40h
 mov esi,ecx
 xor ebx,ebx
 mov eax,[esp+54h]
 mov edx,[eax]
 mov [esp+1ch],edx
 mov edx,[eax+4]
 mov [esp+20h],edx
 mov eax,[esp+58h]
 mov [esp+24h],eax
 mov [esp+18h],ebx
 cmp dword ptr[esp+5ch],0
 jne purchase
 push 05d3ee9h
 ret
 purchase:
 push 05d3ff0h
 ret
}}
// Supply the alternate initializer's finalization frame. Its real 0x481d13
// call and native epilogue execute, including the original ret 8.
__declspec(naked) void __fastcall alternate_finalize(void*,void*){__asm {
 pop eax
 push 0
 push 0
 push eax
 sub esp,2ch
 push ebp
 push esi
 push edi
 push ebx
 mov ebp,ecx
 push 0481d11h
 ret
}}
// Native deserialization still chooses the definition, clears cache validity,
// and calls the new entry. The stream and list payload are synthetic.
unsigned stream_calls=0;
void __fastcall stream_read(void*,void*,void* out,int size){
 std::memset(out,0,size);if(stream_calls++==0){const unsigned id=first_economy;std::memcpy(out,&id,size);}else std::memset(out,0xff,size);
}
void __fastcall read_list(void*,void*,void*,descriptor*){}
void* __fastcall definition_by_id(void*,void*,unsigned id){return definition(id);}
const char* __fastcall menu_text(void*,void*,unsigned){return "fixture sell";}
unsigned __cdecl format_text(const char*,...){return 123;}
void __cdecl present(unsigned,unsigned,unsigned,int,unsigned){}
void __fastcall no_ui(void*,void*){}
void __cdecl cookie(){}
const char* __fastcall pip_cost(void*,void*,int){return "1500";}
std::vector<price_row> build_sell(bool stash){
 put(price_shop.data(),0x18e8,static_cast<unsigned char>(stash?1:0));
 native<void(__fastcall*)(void*,void*)>(0x5d25e0)(price_shop.data(),nullptr);
 const auto count=read<unsigned>(at(0x8a6b1c));
 check(count<=100,"sell builder stays within 100-row array");
 const auto* begin=static_cast<const price_row*>(at(0x8a6348));std::vector<price_row> result{begin,begin+count};
 // Native row construction writes only the low byte of detail. The upper
 // three bytes are unused stack padding, not part of the row's semantics.
 for(auto& r:result)r.detail&=0xff;return result;
}
void price_checks(){
 jump(0x44b8f0,&get_registry);jump(0x46dce0,&get_price_game);
 registry_vt[0x48/4]=reinterpret_cast<void*>(&roster_count);registry_vt[0x50/4]=reinterpret_cast<void*>(&highest);
 registry_vt[0x4c/4]=reinterpret_cast<void*>(&roster_handle);registry_vt[0x74/4]=reinterpret_cast<void*>(&roster_hero);
 gear_vt[0x5c/4]=reinterpret_cast<void*>(&gear_item);gear_vt[0x64/4]=reinterpret_cast<void*>(&remove_equipped);
 put(price_hero.data(),0xc0,gear_object);game_vt[0x268/4]=reinterpret_cast<void*>(&game_mode);put(price_game.data(),0,game_vt.data());
 put(at(0x688314),0x44,addr(&pack_health));put(at(0x688314),0x48,addr(&pack_energy));
 for(unsigned i=health_index;i<first_economy+economy_count;++i)cached_items[i]=allocate(i);
 const std::array<int,5> buys{100,325,550,775,1000},sales{25,81,137,193,250},bags{2000,8000,18000,32000,50000};
 for(unsigned g=0;g<5;++g){
  highest_level=int(g)*10;
  check(value(cached_items[health_index])==buys[g]&&value(cached_items[energy_index])==buys[g],"both pack prices G="+std::to_string(g+1));
  check(sale(cached_items[health_index])==sales[g]&&sale(cached_items[energy_index])==sales[g],"both pack sales G="+std::to_string(g+1));
  price_row bag;bag.type=1;check(native<int(__fastcall*)(void*,void*)>(0x5a9b00)(&bag,nullptr)==bags[g],"native bag price unchanged G="+std::to_string(g+1));
 }
 // Pricing uses the actual native x87 multiplier and ftol veneer.
 auto ordinary=allocate(first_economy+4),unique=allocate(first_economy),legacy=allocate(legacy_definition);
 const int legacy_value=value(legacy);
 check(legacy_value!=137&&legacy_value>0&&legacy_value%25==0,"legacy gear retains native affecter-derived and rounded value");
 for(const int cost:{0,1,2,3,4,99,100,101,1499,1500,1501}){
  put(instance(ordinary),0x28,cost);put(instance(unique),0x28,cost);put(instance(legacy),0x28,cost);
  check(sale(ordinary)==int(double(cost)*0.25)&&sale(unique)==int(double(cost)*double(0.33f))&&sale(legacy)==int(double(cost)*0.25),"native sale truncation and legacy fraction cost="+std::to_string(cost));
 }
 check(sale(unique)==495,"1501 unique truncates to 495");
 put(instance(unique),0x28,1500);check(sale(unique)==495,"1500 times exact XML1 float 0.33 is 495");
 put(definition(legacy_definition),0x3b,static_cast<unsigned char>(4));put(instance(legacy),0x28,1500);
 check(sale(legacy)==495,"legacy unique uses native value times XML1 unique fraction");put(definition(legacy_definition),0x3b,static_cast<unsigned char>(0));
 // A held pip uses its authored base value, not the equipment cache.
 auto* native_property=reinterpret_cast<unsigned char*>(at(0x688314))+0xac;
 const auto saved_property=read<unsigned>(native_property);
 // property_text is supplied below as a deliberately constant environment edge.
 put(native_property,0,addr(&pip_cost));
 check(sale(cached_items[pip_index])==375,"pip sale is 375 from authored cost 1500");put(native_property,0,saved_property);
 for(unsigned i=0;i<3;++i){
  auto d=allocate(first_economy+i);put(instance(d),0x28,9999);put(instance(d),0x30,static_cast<unsigned char>(0xa1));
  native<void(__fastcall*)(void*,void*)>(0x481170)(instance(d),nullptr);
  check(value(d)==(i?0:137)&&read<unsigned char>(instance(d),0x30)==0xa3,"definition-cost cache overrides affecters, absent/zero cost and preserves other flags");
 }
 check(value(allocate(first_economy+1))==0&&sale(allocate(first_economy+1))==0,"zero-cost reward remains sellable for zero");
 auto alternate=allocate(first_economy+4);put(instance(alternate),0x28,123456);
 alternate_finalize(instance(alternate),nullptr);
 check(value(alternate)==137&&(read<unsigned char>(instance(alternate),0x30)&2),"alternate initializer's native 0x481d13 call replaces even an already-valid economy cache");
 auto loaded=allocate(first_economy);put(instance(loaded),0x28,123456);
 jump(0x468940,&stream_read);jump(0x482ad0,&read_list);put(at(0x688314),0x18,addr(&definition_by_id));
 stream_calls=0;native<void(__fastcall*)(void*,void*,void*)>(0x482b40)(instance(loaded),nullptr,price_game.data());
 check(stream_calls==5&&value(loaded)==137&&(read<unsigned char>(instance(loaded),0x30)&2),"native deserializer clears validity then recomputes definition cost through 0x482bd3");
 for(int mode:{1,2})for(unsigned level:{0,1,7,50}){
  put(price_game.data(),0x60c,mode);put(at(0x784e00),0,static_cast<unsigned char>(level));
  price_row course;course.type=2;
  check(native<int(__fastcall*)(void*,void*)>(0x5a9b00)(&course,nullptr)==int(level*level*10),"course row uses raw level in game mode "+std::to_string(mode));
  check(native<int(__fastcall*)(void*,void*)>(0x46eae0)(at(0x784e00),nullptr)==int(level+(mode>1&&level?40:0)),"global course helper still adds 40 only in mode 2");
 }
 jump(0x5d22f0,&credit);jump(0x5a96a0,&feedback);jump(0x5d4133,&transaction_end);
 put(at(0x688314),0x104,addr(&change_state));
 auto test_transaction=[&](descriptor item,bool buy,bool stash,void* owner,bool initial,bool expected,int expected_state){
  const auto index=static_cast<unsigned>((static_cast<unsigned char*>(read<void*>(instance(item),4))-manager.data()-item_capacity_rules::definitions)/0x3c);
  set_obtained(index,initial);put(price_shop.data(),0x18e8,static_cast<unsigned char>(stash));credits=state_calls=removals=0;
  const int expected_credit=buy?-value(item):sale(item);
  equipped_item=owner?item:descriptor{};transact(price_shop.data(),nullptr,&item,owner,buy?1:0);
  check(state_calls==1&&last_state==expected_state&&state_item.handle==item.handle&&state_item.pool==item.pool,"native buy/sale routes exactly one shared state call");
  check(state_saw_obtained==initial&&obtained(index)==expected,"obtained bridge acts after state call, by state/stash/captured owner");
  check(removals==(owner&&!buy?1:0),"native equipped removal still executes once");
  check(credits==expected_credit&&read<unsigned char>(instance(item),0x2e)==expected_state,"native transaction forwards the priced credit/debit and state without extra charge");
 };
 test_transaction(unique,true,false,nullptr,false,true,2);
 test_transaction(unique,false,false,nullptr,true,false,4);
 test_transaction(unique,false,false,price_hero.data(),true,true,4);
 test_transaction(unique,false,true,nullptr,true,true,5);
 test_transaction(unique,true,true,nullptr,false,false,2);
 test_transaction(unique,false,true,price_hero.data(),true,true,5);
 test_transaction(ordinary,true,false,nullptr,false,false,2);
 test_transaction(ordinary,false,false,nullptr,true,true,4);
 put(definition(legacy_definition),0x3b,static_cast<unsigned char>(4));
 test_transaction(legacy,true,false,nullptr,false,false,2);
 test_transaction(legacy,false,false,nullptr,true,true,4);
 put(definition(legacy_definition),0x3b,static_cast<unsigned char>(0));
 // An equipped sale followed by an inventory sale must not reuse the owner.
 test_transaction(unique,false,false,nullptr,true,false,4);
 // Actual sell builder keeps both native equipment walks and presentation.
 put(at(0x688314),0x78,addr(&price_lookup));put(at(0x688314),0x12c,addr(&cached_item));
 jump(0x5adc10,&menu_text);jump(0x67234e,&format_text);jump(0x5aacf0,&present);jump(0x5d2130,&no_ui);jump(0x672161,&cookie);jump(0x5aa240,&no_ui);
 auto carried_zero=allocate(first_economy+1,2),equipped=allocate(first_economy+4,1),vendor=allocate(first_economy+5,3);
 equipped_item=equipped;
 put(instance(carried_zero),0x20,equipped);put(instance(equipped),0x20,vendor);put(instance(vendor),0x20,descriptor{});
 put(manager.data(),0x7764,carried_zero);
 for(unsigned i=health_index;i<first_economy+economy_count;++i)put(definition(i),0x39,static_cast<unsigned char>(0));
 put(definition(health_index),0x39,static_cast<unsigned char>(1));put(definition(pip_index),0x39,static_cast<unsigned char>(2));
 put(definition(health_index+2),0x39,static_cast<unsigned char>(3)); // held, not sellable
 const auto stash_rows=build_sell(true);
 check(stash_rows.size()==2&&stash_rows[0].item.handle==carried_zero.handle&&stash_rows[1].item.handle==equipped.handle&&stash_rows[1].owner==price_hero.data(),"stash builder native carried/equipped only, zero cost retained, owner retained");
 const auto forge=build_sell(false),healer=build_sell(false);
 // Vendor routing (step 5) uses this same entry without a vendor argument.
 check(forge.size()==4&&std::memcmp(forge.data(),healer.data(),forge.size()*sizeof(price_row))==0,"shared Forge/Healer sell entry produces identical carried/equipped/held rows");
 check(forge.size()==4&&forge[0].item.handle==carried_zero.handle&&forge[1].item.handle==equipped.handle&&forge[2].item.handle==cached_items[health_index].handle&&forge[3].item.handle==cached_items[pip_index].handle,"zero-cost equipment, positive health and sellable pip retained; empty energy and unflagged plain omitted");
 const auto stash_again=build_sell(true);check(stash_rows.size()==stash_again.size()&&std::memcmp(stash_rows.data(),stash_again.data(),stash_rows.size()*sizeof(price_row))==0,"stash row fields identical before/after non-stash builds (unused padding normalized)");
 // Fill with 99 native equipment rows, then append at most one plain row.
 descriptor head{},previous{};
 for(unsigned n=0;n<99;++n){auto item=allocate(first_economy+1);if(!head.pool)head=item;if(previous.pool)put(instance(previous),0x20,item);previous=item;}
 put(manager.data(),0x7764,head);put(at(0x8a6b18),0,0x1234abcdu);put(at(0x8a6b20),0,0x5678abcdu);
 const auto full=build_sell(false);
 check(full.size()==100&&full.back().item.handle==cached_items[health_index].handle&&read<unsigned>(at(0x8a6b18))==0x1234abcd&&read<unsigned>(at(0x8a6b20))==0x5678abcd,"shared row capacity admits final health row and refuses further appends without overwriting sentinels");
 // Compare current agreement to an otherwise identical peer missing part 2.
 auto values=gameplay_agreement::rules.rule(1).values;values[0]^=2;
 gameplay_capabilities::record peer;
 for(unsigned i=1;i<gameplay_agreement::rules.count();++i){const auto& r=gameplay_agreement::rules.rule(i);peer.add(r.id,r.revision,r.id==52?values:r.values,true);}
 gameplay_agreement::rules.finalize();peer.finalize();gameplay_capabilities::difference diff;
 check(gameplay_agreement::rules.compare(peer.bytes(),&diff)!=gameplay_capabilities::result::match&&diff.buckets==(std::uint64_t{1}<<51),"otherwise-identical peer with different sales parts is refused, only rule52 differs");
 char reason[128];gameplay_agreement::rules.describe(diff.buckets,0,true,reason,sizeof reason);
 check(std::strstr(reason,"52 Xml1Economy")!=nullptr,"parts mismatch explicitly names rule 52 Xml1Economy");
}
