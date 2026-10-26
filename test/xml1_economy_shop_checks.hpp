// Mapped-retail shop tests. Only environment services are supplied here;
// the installed entry points, pools, list links, rows and tab cycle execute.
unsigned script_queues=0;
const char* script_text="equip";
std::array<void*,64> shop_ui_vt{},script_value_vt{},queue_vt{};
void* shop_ui[1]{shop_ui_vt.data()};
void* script_value[1]{script_value_vt.data()};
void* queue_object[1]{queue_vt.data()};
unsigned __cdecl shop_format(const char*,...){return 0;}
std::vector<std::pair<unsigned,int>> tab_labels;
unsigned service_calls=0,price_calls=0,course_calls=0;
void __fastcall tab_label(void*,void*,unsigned text,int selected){tab_labels.emplace_back(text,selected);}
int __fastcall navigate_input(void*,void*,void*){return 0;}
void __fastcall ui_notify(void*,void*,int){}
void __fastcall ui_focus(void*,void*,unsigned,int){}
void __fastcall training_service(void*,void*,int){++service_calls;}
int __fastcall training_price(void*,void*){++price_calls;return 100;}
void __fastcall courses(void*,void*){++course_calls;price_row r;r.type=2;r.detail=7;native<int(__cdecl*)(const price_row*)>(0x5aa4f0)(&r);}
void open_tabs(bool stash){
 put(price_shop.data(),0x18e8,static_cast<unsigned char>(stash));
 const auto target=0x5d3a39+read<unsigned>(at(0x5d3a35));
 native<int(__fastcall*)(void*,void*,int)>(target)(price_shop.data(),nullptr,0);
}
__declspec(naked) void __fastcall setup_labels(void*,void*){__asm {
 push esi
 push edi
 mov esi,ecx
 xor edi,edi
 call label_segment
 pop edi
 pop esi
 ret
 label_segment:
 push 05d3b10h
 ret
}}
__declspec(naked) void shop_transaction_end(){__asm {
 add esp,40h
 pop edi
 pop esi
 pop ebx
 pop ebp
 ret 4
}}
__declspec(naked) void __fastcall stale_purchase(void*,void*,unsigned){__asm {
 push ebp
 push ebx
 push esi
 push edi
 sub esp,40h
 mov esi,ecx
 xor ebx,ebx
 mov eax,[esp+54h]
 mov [esp+18h],eax
 mov [esp+28h],ebx
 push 05d3ff0h
 ret
}}
const char* __fastcall value_text(void*,void*){return script_text;}
void* __cdecl get_shop_ui(){return shop_ui;}
void* __cdecl get_queue(){return queue_object;}
void __fastcall queue_command(void*,void*,const char* command){if(std::strcmp(command,"openmenu shop")==0)++script_queues;}
void choose_vendor(const char* text){
 script_text=text;std::array<unsigned,8> args{};args[0]=addr(script_value);args[7]=1;
 native<void*(__cdecl*)(void*)>(0x49e640)(args.data());
}
void refill(){native<void(__fastcall*)(void*,void*,int)>(0x46a1a0)(price_game.data(),nullptr,0);}
void list_add(descriptor d,int state){native<bool(__fastcall*)(void*,void*,descriptor,int,int)>(0x47df00)(manager.data(),nullptr,d,1,state);}
std::vector<descriptor> stock_items(int state=3){
 std::vector<descriptor> result;auto d=read<descriptor>(manager.data(),0x7764);
 for(unsigned i=0;d.pool&&i<1024;++i){auto* p=instance(d);if(read<unsigned char>(p,0x2e)==state)result.push_back(d);d=read<descriptor>(p,0x20);}
 return result;
}
std::vector<price_row> build_buy(bool stash=false){
 put(price_shop.data(),0x18e8,static_cast<unsigned char>(stash));
 native<void(__fastcall*)(void*,void*)>(0x5d2a00)(price_shop.data(),nullptr);
 const auto n=read<unsigned>(at(0x8a6b1c));check(n<=100,"buy list stays within 100 rows");
 auto* p=static_cast<price_row*>(at(0x8a6348));return {p,p+n};
}
void shop_checks(){
 check(read<unsigned char>(at(0x46a1a0))==0xe9,"Forge replaces refill entry before all native top-up RNG");
 auto rare=allocate(first_economy+4);
 check(native<int(__fastcall*)(void*,void*)>(0x480e20)(instance(rare),nullptr)==11,"T-STYLE frequency-2 nonunique uses palette 11");
 if(read<unsigned char>(at(0x46a1a0))!=0xe9)return; // RED baseline must not enter unprovided game services.
 jump(0x44b8f0,&get_registry);jump(0x449960,&highest);
 jump(0x5d8920,&get_shop_ui);jump(0x55c890,&get_queue);
 shop_ui_vt[0x84/4]=reinterpret_cast<void*>(&yes);script_value_vt[0x14/4]=reinterpret_cast<void*>(&value_text);
 queue_vt[0x18/4]=reinterpret_cast<void*>(&queue_command);
 jump(0x5adc10,&menu_text);jump(0x67234e,&shop_format);jump(0x5aacf0,&present);
 jump(0x5d2060,&no_ui);jump(0x5aa240,&no_ui);
 put(at(0x688314),0x78,addr(&price_lookup));put(at(0x688314),0x12c,addr(&cached_item));
 for(unsigned i=health_index;i<health_index+2;++i)cached_items[i]=allocate(i);
 put(definition(legacy_definition),0x3b,static_cast<unsigned char>(12)); // old unique, already obtained
 auto old=allocate(legacy_definition,3),sold=allocate(legacy_definition,4),carried=allocate(first_economy+6,2),stash=allocate(first_economy+7,5);
 list_add(old,3);list_add(sold,4);list_add(carried,2);list_add(stash,5);
 put(at(0x7ad8b4),0,0u);put(at(0x7ad8b0),0,67890u);
 choose_vendor("EqUiP");
 const std::array<unsigned,5> counts{18,37,52,69,85};
 for(unsigned g=1;g<=5;++g){
  highest_level=int(g-1)*10;refill();const auto a=stock_items();
  check(a.size()==counts[g-1]&&stock_items(4).empty(),"T-FORGE catalog G="+std::to_string(g)+" removes old state-3/4 stock");
  auto rows=build_buy();bool ordered=rows.size()==a.size();unsigned previous=0;
  for(const auto& r:rows){auto* d=read<void*>(instance(r.item),4);const auto index=unsigned((static_cast<unsigned char*>(d)-manager.data()-item_capacity_rules::definitions)/0x3c);ordered&=r.type==0&&index>previous;previous=index;}
  check(ordered,"Forge buy rows are instances in definition order G="+std::to_string(g));
  const auto live_i=read<unsigned>(native<void*(__cdecl*)()>(0x483610)(),item_capacity_rules::instance_count_offset(true));
  const auto live_a=read<unsigned>(native<void*(__cdecl*)()>(0x45fb00)(),item_capacity_rules::attached_count_offset(true));
  refill();const auto b=stock_items();bool identical=a.size()==b.size();for(unsigned n=0;n<a.size()&&n<b.size();++n)identical&=a[n].handle==b[n].handle&&a[n].pool==b[n].pool;
  check(identical&&live_i==read<unsigned>(native<void*(__cdecl*)()>(0x483610)(),item_capacity_rules::instance_count_offset(true))&&live_a==read<unsigned>(native<void*(__cdecl*)()>(0x45fb00)(),item_capacity_rules::attached_count_offset(true)),"second open is idempotent, descriptors and both budgets unchanged");
 }
 check(read<unsigned>(native<void*(__cdecl*)()>(0x483610)(),item_capacity_rules::instance_count_offset(true))==90&&read<unsigned>(native<void*(__cdecl*)()>(0x45fb00)(),item_capacity_rules::attached_count_offset(true))==255,"85 vendor instances plus five fixture units, exactly 255 own records: no stock/pool leaks");
 check(obtained(legacy_definition),"removing out-of-catalog legacy unique stock preserves its obtained flag");
 put(definition(legacy_definition),0x3b,static_cast<unsigned char>(0));
 highest_level=0;refill();check(stock_items().size()==18,"lower fresh roster G removes stock above the catalog");
 highest_level=40;refill();check(stock_items().size()==85,"raised fresh roster G restores missing catalog rows");
 check(read<unsigned char>(instance(carried),0x2e)==2&&read<unsigned char>(instance(stash),0x2e)==5,"reconcile preserves carried and stash items");
 set_obtained(first_economy,true);refill();check(obtained(first_economy)&&stock_items().size()==84,"obtained unique removed without clearing its obtained flag");
 set_obtained(first_economy,false);refill();check(stock_items().size()==85,"cleared unique reappears");
 auto duplicate=allocate(first_economy+4,3);list_add(duplicate,3);refill();check(stock_items().size()==85,"duplicate catalog stock removed");
 // Buying a non-unique leaves state 2; the next reconcile restores its row.
 const auto bought=build_buy()[4].item;put(instance(bought),0x2e,static_cast<unsigned char>(2));refill();
 check(stock_items().size()==85&&read<unsigned char>(instance(bought),0x2e)==2,"nonunique stock reappears after purchase, owned unit retained");
 check(read<unsigned>(at(0x7ad8b4))==0&&read<unsigned>(at(0x7ad8b0))==67890&&xml1_economy::status().find("; gen 0;")!=std::string::npos,"reconcile and rows use zero draws on either stream; native generator tripwire stays zero");
 // Empty native course source: Healer still has exactly packs plus Grab Bag.
 jump(0x5d2710,&no_ui);
 for(const char* name:{"ITEM","unknown","","item"}){
  choose_vendor(name);refill();const auto rows=build_buy();
  check(stock_items().empty()&&rows.size()==3&&rows[0].type==0&&rows[0].item.handle==cached_items[health_index].handle&&rows[1].type==0&&rows[2].type==1,"Healer/default has no equipment stock and only synthetic pack/bag rows");
 }
 check(script_queues==5,"vendor text captured without changing native openmenu shop queue count");
 jump(0x5d2710,&courses);const auto healer_courses=build_buy();
 check(course_calls==1&&healer_courses.size()==4&&healer_courses.back().type==2&&healer_courses.back().detail==7,"Healer retains native eligible-course builder and rows");
 auto unique=allocate(first_economy),legacy=allocate(legacy_definition);
 check(native<int(__fastcall*)(void*,void*)>(0x480e20)(instance(unique),nullptr)==12,"T-STYLE unique takes precedence over frequency 2");
 check(native<int(__fastcall*)(void*,void*)>(0x480e20)(instance(legacy),nullptr)==0x24,"T-STYLE legacy plain equipment retains native style");
 put(definition(legacy_definition),0x3b,static_cast<unsigned char>(4));
 check(native<int(__fastcall*)(void*,void*)>(0x480e20)(instance(legacy),nullptr)==12,"T-STYLE legacy unique retains native priority");
 put(definition(legacy_definition),0x3b,static_cast<unsigned char>(0));
 auto plain=allocate(first_economy+5);
 check(native<int(__fastcall*)(void*,void*)>(0x480e20)(instance(plain),nullptr)==0x24,"T-STYLE economy frequency-3 equipment retains native plain style");
 for(auto item:{legacy,plain}){
  put(instance(item),0x2c,static_cast<unsigned char>(0));
  check(native<int(__fastcall*)(void*,void*)>(0x480e20)(instance(item),nullptr)==11,"T-STYLE other legacy/economy special-name styling stays native");
  put(instance(item),0x2c,static_cast<unsigned char>(0xff));put(instance(item),8,descriptor{1,&node_pool});
  check(native<int(__fastcall*)(void*,void*)>(0x480e20)(instance(item),nullptr)==0x23,"T-STYLE other legacy/economy affix styling stays native");
  put(instance(item),8,descriptor{});
 }
 // Native stash buy builder ignores vendor mode and still lists only state 5.
 const auto stash_rows=build_buy(true);choose_vendor("equip");const auto stash_again=build_buy(true);
 check(stash_rows.size()==1&&stash_again.size()==1&&stash_rows[0].item.handle==stash.handle&&stash_again[0].item.handle==stash.handle,"stash buy list unchanged across both vendor modes");
 // Switch/navigation bodies and setup loop execute, with only UI endpoints stubbed.
 jump(0x5adf80,&tab_label);jump(0x5adf40,&ui_focus);jump(0x5d30d0,&no_ui);
 jump(0x5d25e0,&no_ui);jump(0x5aafd0,&navigate_input);
 shop_ui_vt[0xe8/4]=reinterpret_cast<void*>(&ui_notify);
 jump(0x5d3b49,&no_ui); // end the isolated native tab-label loop
 for(bool is_stash:{false,true}){
  open_tabs(is_stash);
  check((read<unsigned char>(price_shop.data(),0x18e8)&1)==unsigned(is_stash),"two-tab state never changes the native stash bit");
  for(int tab:{0,1})for(signed char direction:std::array<signed char,2>{-1,1}){
   put(price_shop.data(),0x18d8,tab);
   native<int(__fastcall*)(void*,void*,void*)>(0x5d36a0)(price_shop.data(),nullptr,&direction);
   check(read<int>(price_shop.data(),0x18d8)==1-tab,"T-TAB both directions from tab "+std::to_string(tab)+(is_stash?" stash":" shop"));
  }
  if(!is_stash){native<int(__fastcall*)(void*,void*,int)>(0x5d35d0)(price_shop.data(),nullptr,2);check(read<int>(price_shop.data(),0x18d8)==0,"direct index 2 is clamped to buy");}
  tab_labels.clear();setup_labels(price_shop.data(),nullptr);
  const auto* table=static_cast<const unsigned*>(at(is_stash?0x6e6fe4:0x6e6fd8));
  check(tab_labels.size()==3&&tab_labels[0].first==table[0]&&tab_labels[1].first==table[1]&&tab_labels[2].first==0x681968,"T-TAB setup preserves first two labels, third is native empty string");
 }
 open_tabs(false);put(at(0x8a6b1c),0,3u);
 native<void(__fastcall*)(void*,void*)>(0x5d2b70)(price_shop.data(),nullptr);
 check(read<unsigned>(at(0x8a6b1c))==0,"Training builder clears prior rows and adds nothing");
 jump(0x5d2d30,&training_service);jump(0x5d2290,&training_service);jump(0x5d2810,&training_service);
 jump(0x5a9b00,&training_price);jump(0x5d22f0,&credit);jump(0x5a96a0,&feedback);jump(0x5d4133,&shop_transaction_end);
 for(bool is_stash:{false,true}){
  put(price_shop.data(),0x18e8,static_cast<unsigned char>(is_stash));
  service_calls=price_calls=0;credits=0;
  for(unsigned site:{0x5d4104u,0x5d4119u,0x5d412eu}){const auto target=site+5+read<unsigned>(at(site+1));native<void(__fastcall*)(void*,void*,int)>(target)(price_shop.data(),nullptr,7);}
  check(service_calls==(is_stash?3u:0u),"all three direct Training dispatches refuse only normal shop mode");
  service_calls=0;
  for(unsigned type:{3u,4u,5u})stale_purchase(price_shop.data(),nullptr,type);
  check(service_calls==(is_stash?3u:0u)&&price_calls==(is_stash?3u:0u)&&credits==(is_stash?-300:0),"stale Training rows refused before price/debit/services; stash path unchanged");
 }
 // Both advertised parts must participate in the real agreement record.
 for(unsigned mask:{4u,16u}){
  gameplay_capabilities::record peer;for(unsigned i=1;i<gameplay_agreement::rules.count();++i){auto r=gameplay_agreement::rules.rule(i);if(r.id==52)r.values[0]^=mask;peer.add(r.id,r.revision,r.values,true);}
  peer.finalize();gameplay_agreement::rules.finalize();gameplay_capabilities::difference diff;
  const auto result=gameplay_agreement::rules.compare(peer.bytes(),&diff);char reason[128];gameplay_agreement::rules.describe(diff.buckets,0,true,reason,sizeof reason);
  check(result!=gameplay_capabilities::result::match&&diff.buckets==(std::uint64_t{1}<<51)&&std::strstr(reason,"52 Xml1Economy"),"peer missing part "+std::to_string(mask)+" is refused naming only rule 52");
 }
 // The real constructor preflight, reached from Forge, refuses before any
 // unsafe allocator call when either installed W2 pool lacks capacity.
 choose_vendor("item");refill();choose_vendor("equip");
 auto* ip=native<void*(__cdecl*)()>(0x483610)();auto* ap=native<void*(__cdecl*)()>(0x45fb00)();
 const auto owned_head=read<descriptor>(manager.data(),0x7764);put(manager.data(),0x7764,descriptor{});
 const auto baseline_instances=read<unsigned>(ip,item_capacity_rules::instance_count_offset(true)),baseline_attached=read<unsigned>(ap,item_capacity_rules::attached_count_offset(true));
 refill();choose_vendor("item");refill();
 check(!read<descriptor>(manager.data(),0x7764).pool&&read<unsigned>(ip,item_capacity_rules::instance_count_offset(true))==baseline_instances&&read<unsigned>(ap,item_capacity_rules::attached_count_offset(true))==baseline_attached,"all-stock list can be reconciled to empty, including the final head; both pool counts restored");
 put(manager.data(),0x7764,owned_head);choose_vendor("equip");
 std::array<void*,80> manager_methods;std::memcpy(manager_methods.data(),at(0x688314),sizeof manager_methods);
 put(manager.data(),0,addr(manager_methods.data()));manager_methods[0xe4/4]=reinterpret_cast<void*>(&allocation_spy);
 for(bool attached_full:{false,true}){
  auto* pool=attached_full?ap:ip;const unsigned off=attached_full?item_capacity_rules::attached_count_offset(true):item_capacity_rules::instance_count_offset(true);
  const auto count=read<unsigned>(pool,off);put(pool,off,attached_full?4096u:1024u);allocation_calls=0;
  const std::vector<unsigned char> before_i(static_cast<unsigned char*>(ip),static_cast<unsigned char*>(ip)+item_capacity_rules::instances.size),before_a(static_cast<unsigned char*>(ap),static_cast<unsigned char*>(ap)+item_capacity_rules::attached.size);
  refill();
  check(stock_items().empty()&&allocation_calls==0&&std::memcmp(ip,before_i.data(),before_i.size())==0&&std::memcmp(ap,before_a.data(),before_a.size())==0,"Forge pool exhaustion refuses every missing catalog unit without partial allocation");
  put(pool,off,count);
 }
 put(manager.data(),0,0x688314u);
}
