// Observe real open/update/gate/routed-input/row/debit/state/obtained boundaries
// in the mapped retail image. Only UI, device, roster and script services differ.
std::array<void*,160> digest_menu_vt{},digest_input_vt{},digest_net_vt{},digest_cheat_vt{},digest_widget_vt{};
std::array<void*,16> digest_pad_vt{};
void* digest_menu[1]{digest_menu_vt.data()},*digest_input[1]{digest_input_vt.data()},*digest_net[1]{digest_net_vt.data()},*digest_cheat[1]{digest_cheat_vt.data()},*digest_widget[1]{digest_widget_vt.data()};
void* digest_pad[1]{digest_pad_vt.data()};
int digest_balance=10000,digest_selected=0,digest_player=1,digest_pad_player=0;
std::vector<int> digest_visible{0};
unsigned digest_edges=16;bool digest_blocked=false,digest_afford=true;
bool digest_alternate=false;
void* __cdecl digest_get_menu(){return digest_menu;}
void* __cdecl digest_get_input(){return digest_input;}
void* __cdecl digest_get_net(){return digest_net;}
void* __cdecl digest_get_cheat(){return digest_cheat;}
int __fastcall digest_money(void*,void*){return digest_balance;}
void __fastcall digest_credit(void* shop,void*,int amount){if(!(read<unsigned char>(shop,0x18e8)&1))digest_balance+=amount;}
bool __fastcall digest_gate(void*,void*){return digest_blocked;}
bool __fastcall digest_offline(void*,void*){return false;}
bool __fastcall digest_network_mode(void*,void*,int){return digest_alternate;}
bool __fastcall digest_remote(void*,void*,int){return true;}
int __fastcall digest_players(void*,void*){return 2;}
int __fastcall digest_local(void*,void*){return 0;}
void* __fastcall digest_get_pad(void*,void*,int i){digest_pad_player=i;return digest_pad;}
unsigned __fastcall digest_pad_edges(void*,void*){return digest_pad_player==digest_player?digest_edges:0;}
bool __fastcall digest_no_cheat(void*,void*,int){return false;}
bool __fastcall digest_affordable(void*,void*,int){return digest_afford;}
int __fastcall digest_selected_row(void*,void*){return digest_selected;}
int __fastcall digest_visible_count(void*,void*){return static_cast<int>(digest_visible.size());}
int __fastcall digest_row_index(void*,void*,int i){return digest_visible.at(i);}
void* __cdecl digest_find(void* focus,int,unsigned,unsigned,int){return focus==digest_widget?digest_widget:nullptr;}
void __fastcall digest_navigation(void*,void*,unsigned char* directions,void*){std::memset(directions,0,3);}
void __fastcall digest_switch(void* self,void*,int tab){put(self,0x18d8,tab);}
bool __fastcall digest_base_open(void*,void*,void*,void*){return true;}
void __fastcall digest_open_ui(void*,void*,int,int){}
double __cdecl digest_random(char synchronized){check(synchronized==1,"observed bag uses lockstep RNG");put(at(0x7ad8b4),0,read<unsigned>(at(0x7ad8b4))+1);return 0.05;}
void __fastcall digest_course_run(void*,void*,const char* text,int mode){check(mode==1&&std::strstr(text,"loadDangerRoomCourse("),"observed bag retains native script dispatch shape");}
// Finish the real open after its native initial-tab call and money refresh.
__declspec(naked) void digest_open_end(){__asm {
 pop edi
 mov al,1
 pop esi
 add esp,18h
 ret 8
}}
void digest_checks(){
 using namespace xml1_economy;
 check(digest::installed(),"digest observer participates in verified startup union");
 check(digest::pipe("events")=="{\"first\":0,\"last\":0,\"lost\":false,\"lines\":[]}","disabled diagnostics begin with empty evidence");
 jump(0x44b8f0,&get_registry);registry_vt[0x48/4]=reinterpret_cast<void*>(&roster_count);registry_vt[0x4c/4]=reinterpret_cast<void*>(&roster_handle);registry_vt[0x74/4]=reinterpret_cast<void*>(&roster_hero);
 jump(0x5d8920,&digest_get_menu);jump(0x551ed0,&digest_get_input);jump(0x48de40,&digest_get_net);jump(0x44cce0,&digest_get_cheat);
 jump(0x612be0,&digest_get_net);jump(0x610d20,&digest_offline);
 digest_menu_vt[0xdc/4]=reinterpret_cast<void*>(&digest_gate);
 digest_menu_vt[0x130/4]=at(0x5d4a00);digest_menu_vt[0x13c/4]=at(0x5d4b20);
 digest_menu_vt[0x148/4]=reinterpret_cast<void*>(&no_ui);digest_menu_vt[0xe8/4]=reinterpret_cast<void*>(&ui_notify);digest_menu_vt[0x1f8/4]=reinterpret_cast<void*>(&digest_navigation);
 digest_input_vt[0x70/4]=reinterpret_cast<void*>(&digest_players);digest_input_vt[0x60/4]=reinterpret_cast<void*>(&digest_local);digest_input_vt[0x4c/4]=reinterpret_cast<void*>(&digest_get_pad);
 digest_pad_vt[0x18/4]=reinterpret_cast<void*>(&digest_pad_edges);digest_net_vt[0x10/4]=reinterpret_cast<void*>(&digest_network_mode);digest_net_vt[0x24/4]=reinterpret_cast<void*>(&digest_remote);digest_cheat_vt[4/4]=reinterpret_cast<void*>(&digest_no_cheat);
 digest_widget_vt[0xc8/4]=reinterpret_cast<void*>(&digest_selected_row);digest_widget_vt[0x88/4]=reinterpret_cast<void*>(&digest_affordable);
 jump(0x5aaeb0,&no_ui);jump(0x5d23c0,&no_ui);jump(0x5bf750,&digest_visible_count);jump(0x5bfc20,&digest_row_index);jump(0x67234e,&digest_find);
 jump(0x672161,&cookie);jump(0x5d1fa0,&no_ui);jump(0x5d35d0,&digest_switch);jump(0x5d22f0,&digest_credit);jump(0x5a96a0,&feedback);
 put(at(0x688314),0x24,addr(&digest_money));put(at(0x688314),0x104,addr(&change_state));
 std::array<void*,32> self_vt{};self_vt[0x40/4]=reinterpret_cast<void*>(&bag_ui_refresh);put(price_shop.data(),0,self_vt.data());
 auto unique=allocate(first_economy,3);list_add(unique,3);price_row row;row.item=unique;put(at(0x8a6348),0,row);put(at(0x8a6b1c),0,1u);put(price_shop.data(),0x324,static_cast<void*>(digest_widget));put(price_shop.data(),0x18d8,0);
 auto update=native<void(__fastcall*)(void*,void*)>(0x5d3ba0);
 digest_blocked=true;update(price_shop.data(),nullptr);
 check(digest::pipe("events").find("\"last\":0")!=std::string::npos,"disabled observer forwards real update/gate without evidence or snapshots");
 digest::enable();digest_blocked=false;update(price_shop.data(),nullptr);
 auto lines=digest::pipe("events");
 check(lines.find("econ seq=1 player=1 gate=0 tab=0 list=buy row=0:item0160 owner=-1 result=buy money=9863")!=std::string::npos,"native 0x5d4a00 action player, gate, row, debit and successful purchase captured");
 check(obtained(first_economy)&&read<unsigned char>(instance(unique),0x2e)==2,"observer preserves actual obtained/state bridge");
 check(lines.find("error-")==std::string::npos&&lines.find(" rng=0")!=std::string::npos,"snapshot resolves all high definition indices and reads native draw counter without drawing");
 row.owner=price_hero.data();put(at(0x8a6348),0,row);put(price_shop.data(),0x18d8,1);gear_vt[0x64/4]=reinterpret_cast<void*>(&remove_equipped);put(price_hero.data(),0xc0,gear_object);update(price_shop.data(),nullptr);
 lines=digest::pipe("events 1");check(lines.find("player=1 gate=0 tab=1 list=sell row=0:item0160 owner=0 result=sell money=9908")!=std::string::npos,"equipped owner normalized before native removal and credited sale");
 check(obtained(first_economy)&&removals==1,"equipped sale retains obtained and removes equipment exactly once");
 row.owner=nullptr;put(at(0x8a6348),0,row);update(price_shop.data(),nullptr);check(!obtained(first_economy),"ordinary unique sale clears obtained under observation");
 put(price_shop.data(),0x18e8,static_cast<unsigned char>(1));update(price_shop.data(),nullptr);
 check(digest::pipe("events 3").find("result=deposit money=9953")!=std::string::npos&&last_state==5,"stash deposit emits despite zero currency change");
 put(price_shop.data(),0x18e8,static_cast<unsigned char>(0));digest_afford=false;update(price_shop.data(),nullptr);
 check(digest::pipe("events 4").find("result=refused money=9953")!=std::string::npos,"native refusal emits without claiming a transaction");digest_afford=true;
 digest_edges=0;update(price_shop.data(),nullptr);check(digest::pipe("events").find("\"last\":5")!=std::string::npos,"idle updates never advance economy sequence");
 digest_edges=16;digest_blocked=true;update(price_shop.data(),nullptr);check(digest::pipe("events").find("\"last\":5")!=std::string::npos,"blocked native gate cannot fabricate routed input");digest_blocked=false;
 check(digest::pipe("events rubbish").starts_with("error"),"invalid history cursor refused");
 check(digest::pipe("shop").find("\"active\":true")!=std::string::npos&&digest::pipe("shop").find("\"definition\":\"item0160\"")!=std::string::npos,"pipe exposes copied view without reading game memory on server thread");
 // Real native open prologue, base open, tab setup and return ABI; UI tail elided.
 jump(0x449960,&highest);jump(0x5adf80,&tab_label);jump(0x5adf40,&ui_focus);jump(0x5d30d0,&no_ui);jump(0x5d2a00,&no_ui);jump(0x5d2060,&no_ui);
 digest_menu_vt[0xe8/4]=reinterpret_cast<void*>(&ui_notify);
 jump(0x5af7b0,&digest_base_open);jump(0x5acf30,&digest_open_ui);jump(0x5d3a40,&digest_open_end);
 native<bool(__fastcall*)(void*,void*,void*,void*)>(0x5d39c0)(price_shop.data(),nullptr,nullptr,nullptr);
 check(digest::pipe("events 5").find("econ seq=6 player=-1 gate=-1 tab=0")!=std::string::npos&&digest::pipe("events 5").find("result=open")!=std::string::npos,"successful open emits after native setup, no invented player/gate");
 const auto s=xml1_economy::status();check(s.find("econ on; plan A; defs 132; digest ")==0&&s.find("; forge 0/G1; gen 0; refused 0")!=std::string::npos&&gameplay_agreement::rules.rule(1).values[0]==127,"complete status and full plan-A rule52 parts remain intact");
 jump(0x560190,&digest_random);jump(0x4c8bf0,&bag_course_count);jump(0x4c9c10,&bag_course_get);jump(0x4a1670,&bag_script_manager);jump(0x5d2460,&bag_ui_select);
 put(at(0x688314),0x140,addr(&bag_notify));registry_vt[0x50/4]=reinterpret_cast<void*>(&highest);
 bag_script_vt[0xc/4]=reinterpret_cast<void*>(&bag_add_script);bag_script_vt[0x3c/4]=reinterpret_cast<void*>(&digest_course_run);
 unsigned short cw=0;__asm fnstcw cw
 bag_cw=cw;bag_mxcsr=_mm_getcsr();
 row={};row.type=1;put(at(0x8a6348),0,row);put(price_shop.data(),0x18d8,0);highest_level=1;
 update(price_shop.data(),nullptr);
 check(digest::pipe("events 6").find("tab=0 list=buy row=0:bag owner=-1 result=bag-equipment money=7953")!=std::string::npos&&read<int>(price_shop.data(),0x18d8)==1,"real bag reward emits one outer event after debit, preserving pre-switch row/tab");
 highest_level=40;put(price_shop.data(),0x18d8,0);update(price_shop.data(),nullptr);
 check(digest::pipe("events 7").find("result=bag-empty money=-42047")!=std::string::npos,"real empty bag result captured, single native charge and no refund");
 bag_courses.push_back({1,1,0});put(bag_course_memory[0].data(),0,static_cast<unsigned char>(1));put(bag_course_memory[0].data(),0x4c,static_cast<unsigned short>(1));put(bag_course_memory[0].data(),0x44,4u);
 update(price_shop.data(),nullptr);
 check(digest::pipe("events 8").find("result=bag-course")!=std::string::npos,"real course bag dispatch is attributed to its captured purchase");
 digest_alternate=true;digest_player=0;digest_afford=false;update(price_shop.data(),nullptr);
 check(digest::pipe("events 9").find("econ seq=10 player=0 gate=0")!=std::string::npos,"alternate native 0x5d4b20 route captures its actual local action player");
 row.type=0;row.item=unique;row.owner=price_game.data();put(at(0x8a6348),0,row);update(price_shop.data(),nullptr);
 check(digest::pipe("events").find("\"last\":10")!=std::string::npos,"unresolvable captured owner skips evidence instead of hashing a process pointer");
 put(price_shop.data(),0x18e8,static_cast<unsigned char>(1));row.owner=nullptr;put(at(0x8a6348),0,row);
 auto second=allocate(first_economy+1,2),third=allocate(first_economy+2,2);
 price_row r1;r1.item=second;price_row r2;r2.item=third;put(at(0x8a6348),20,r1);put(at(0x8a6348),40,r2);put(at(0x8a6b1c),0,3u);
 auto reopen=[&]{native<bool(__fastcall*)(void*,void*,void*,void*)>(0x5d39c0)(price_shop.data(),nullptr,nullptr,nullptr);};
 auto stock_hash_after=[&](unsigned after){auto text=digest::pipe("events "+std::to_string(after));const auto p=text.find(" stock=");return p==std::string::npos?std::string():text.substr(p+7,16);};
 digest_visible={0,1,2};reopen();const auto all=stock_hash_after(10);
 digest_visible={0,2,1};reopen();check(all!=stock_hash_after(11),"nonselected visible row permutation changes stock hash");
 digest_visible={0,1};reopen();check(all!=stock_hash_after(12),"visible count changes stock hash with unchanged global rows and selection");
 r1.item={};put(at(0x8a6348),20,r1);reopen();check(digest::pipe("shop").find("row-identity")!=std::string::npos,"null equipment descriptor cannot become accepted type0 identity");
 r1.item=second;r1.item.handle^=0x80000000;put(at(0x8a6348),20,r1);reopen();check(digest::pipe("shop").find("row-identity")!=std::string::npos,"invalid equipment handle cannot collapse into a default row identity");
 r1.item=second;put(at(0x8a6348),20,r1);
 r1={};r1.type=2;r1.detail=0x11223300;put(at(0x8a6348),20,r1);reopen();const auto course_padding=stock_hash_after(13);
 r1.detail=0x88776600;put(at(0x8a6348),20,r1);reopen();check(course_padding==stock_hash_after(14),"course detail hashes only native byte index, never three stack padding bytes");
 auto bad=allocate(first_economy+3,3);list_add(bad,3);put(instance(bad),4,static_cast<unsigned char*>(definition(first_economy+3))+1);reopen();
 check(digest::pipe("shop").find("stock-identity")!=std::string::npos,"unresolvable stock definition invalidates evidence even outside the visible list");
 // Reproduce the real first-open shape: sell tab, no inventory, zero money,
 // and a focused widget POINTER. The old fixture's integer 1 concealed the AV.
 put(instance(bad),4,definition(first_economy+3));
 put(at(0x8a6b1c),0,0u);digest_visible.clear();digest_selected=-1;digest_balance=0;
 put(price_shop.data(),0x18d8,1);put(price_shop.data(),0x18e8,static_cast<unsigned char>(0));
 put(price_shop.data(),0x324,static_cast<void*>(digest_widget));
 const auto before_money=digest_balance;const auto before_state=last_state;
 const auto before_manager=manager;const auto before_draws=read<unsigned>(at(0x7ad8b4));
 digest::test_observe(price_shop.data());
 check(digest::pipe("shop").find("\"list\":\"sell\"")!=std::string::npos&&digest::pipe("shop").find("\"rows\":[]")!=std::string::npos,
       "real focused widget, sell-first empty open and zero money produce a complete snapshot without AV");
 check(digest::pipe("events").find("result=open money=0")!=std::string::npos,"empty zero-money open remains valid diagnostic evidence");
 const auto complete=digest::pipe("events");
 const auto skip=[&](const char* reason){
  digest::test_observe(price_shop.data());
  check(digest::pipe("events")==complete&&digest::pipe("shop").find(reason)!=std::string::npos,"malformed snapshot skips frame without publishing partial evidence");
 };
 const auto id=read<unsigned>(definition(0),8);
 put(definition(0),8,0u);skip("definition-name");
 put(definition(0),8,0x00ffffffu);skip("definition-name");
 put(definition(0),8,0x2000u);skip("definition-name");put(definition(0),8,id);
 const auto off=read<unsigned>(names.data(),4+id*4);
 put(names.data(),4+id*4,0xffffffffu);skip("definition-name");put(names.data(),4+id*4,off);
 const auto used=read<unsigned>(names.data(),0x8004);
 put(names.data(),0x8004,1u);skip("definition-name");
 put(names.data(),0x8004,0x14401u);skip("definition-name");put(names.data(),0x8004,used);
 const auto tail=names[0x8008+used-1];names[0x8008+used-1]='x';
 put(names.data(),4+id*4,used-1);skip("definition-name");
 names[0x8008+used-1]=tail;put(names.data(),4+id*4,off);
 put(price_shop.data(),0x324,static_cast<void*>(nullptr));skip("no-focus");
 put(price_shop.data(),0x324,static_cast<void*>(digest_widget));
 digest::test_observe(nullptr);check(digest::pipe("events")==complete&&digest::pipe("shop").find("null-shop")!=std::string::npos,"null shop is skipped without dereference");
 digest::test_observe(at(1));check(digest::pipe("events")==complete&&digest::pipe("shop").find("snapshot-fault")!=std::string::npos,"unreadable shop is fault-contained");
 check(digest::test_fault_cleanup(),"SEH containment unwinds diagnostic C++ owners instead of leaking captured resources");
 check(manager==before_manager&&digest_balance==before_money&&last_state==before_state&&read<unsigned>(at(0x7ad8b4))==before_draws,
       "empty/malformed observations preserve every manager byte, currency, state calls and native RNG counter");

 // Exercise the per-action describe/name path as well as full capture. The
 // native affordability refusal must still run with no diagnostic event.
 row={};row.item=unique;put(at(0x8a6348),0,row);put(at(0x8a6b1c),0,1u);
 digest_visible={0};digest_selected=0;digest_afford=false;put(price_shop.data(),0x18d8,0);
 const auto selected_name=read<unsigned>(definition(first_economy),8);
 put(definition(first_economy),8,0x00ffffffu);
 const auto refused_manager=manager;const auto refused_events=digest::pipe("events");
 update(price_shop.data(),nullptr);
 check(manager==refused_manager&&digest_balance==0&&digest::pipe("events")==refused_events,
       "out-of-range id in selected-row annotation is bounded and leaves native refusal/currency/state unchanged");
 put(definition(first_economy),8,selected_name);

}
