// Included after prices checks in the owned-retail mapped execution fixture.
// Real constructor, native inventory addition, obtained setter, buy-price and
// debit-to-dispatch block execute. Script/UI/roster/content are fixture edges.
std::vector<xml1_grab_bag_rules::course> bag_courses;
std::array<std::array<unsigned char,0x50>,16> bag_course_memory{};
std::vector<double> bag_script;
unsigned bag_draws=0,bag_debits=0,bag_notifications=0,bag_script_adds=0,bag_script_runs=0;
unsigned bag_course_notifications=0;
unsigned short bag_cw=0;
unsigned bag_mxcsr=0;
bool bag_controls=true;
std::string bag_command;
descriptor bag_awarded{};
void check_bag_controls(){unsigned short current=0;__asm fnstcw current
 bag_controls&=current==bag_cw&&_mm_getcsr()==bag_mxcsr;
}
double __cdecl bag_random(char lockstep){
 unsigned short current=0;__asm fnstcw current
 check(lockstep==1&&(current&0x0f00)==0x0200&&(_mm_getcsr()&0x6000)==0,"bag random uses lockstep stream with 53-bit/RN precision");
 if(bag_draws>=bag_script.size()){check(false,"bag script exhausted");std::exit(3);}return bag_script[bag_draws++];
}
unsigned char __cdecl bag_course_count(){check_bag_controls();return static_cast<unsigned char>(bag_courses.size());}
void* __cdecl bag_course_get(unsigned char index){check_bag_controls();return bag_course_memory.at(index).data();}
void __fastcall bag_credit(void*,void*,int amount){check_bag_controls();++bag_debits;credits+=amount;}
struct bag_position {float x,y,z;};
void __fastcall bag_notify(void*,void*,descriptor item,int kind,bag_position pos,int show){
 check_bag_controls();++bag_notifications;
 bag_awarded=item;
 check(item.pool&&kind==1&&pos.x==0&&pos.y==0&&pos.z==0&&show==1,"bag native notification ABI");
}
void* bag_script_vt[16]{};
void* bag_script_object[1]{bag_script_vt};
void* __cdecl bag_script_manager(){check_bag_controls();return bag_script_object;}
void __fastcall bag_add_script(void*,void*,const char* command){check_bag_controls();++bag_script_adds;bag_command=command;}
void __fastcall bag_run_script(void*,void*,const char* command,int mode){
 check_bag_controls();++bag_script_runs;check(bag_command==command&&mode==1,"course uses native script submit and execute route");
 check(std::strcmp(command,"loadDangerRoomCourse('item0003')")==0,"fixture dispatch recognizes course command");
 native<int(__cdecl*)(void*)>(0x4a72e0)(nullptr);
}
void* bag_value_vt[8]{};void* bag_value[1]{bag_value_vt};
void* bag_text_vt[8]{};void* bag_text[1]{bag_text_vt};
void* bag_hud_vt[8]{};void* bag_hud[1]{bag_hud_vt};
void* __fastcall bag_argument(void*,void*,int index){check(index==0,"native course handler argument zero");return bag_value;}
const char* __fastcall bag_argument_text(void*,void*){return "item0003";}
char* __cdecl bag_copy(char* out,const char* value,unsigned count){return std::strncpy(out,value,count);}
char* __cdecl bag_token(char* value,const char* delimiters){return std::strtok(value,delimiters);}
int __cdecl bag_length(const char* text){return int(std::strlen(text));}
unsigned __fastcall bag_intern(void*,void*,const char* text,int length,int mode){check(std::strcmp(text,"item0003")==0&&length==9&&mode==0,"native course handler interns identifier");return 4;}
unsigned char __cdecl bag_course_lookup(const unsigned* key){check(*key==4,"native course handler looks up identifier");return 3;}
void* __cdecl bag_get_text(){return bag_text;}
void* __cdecl bag_get_hud(){return bag_hud;}
const char* __fastcall bag_localized(void*,void*,unsigned key){check(key==0x298,"native course handler notification localization");return "course unlocked";}
int __cdecl bag_format(char* out,unsigned size,const char* format,...){va_list args;va_start(args,format);const int n=std::vsnprintf(out,size,format,args);va_end(args);return n;}
void __fastcall bag_course_notice(void*,void*,const char* text,float duration,int kind){
 check_bag_controls();++bag_course_notifications;
 check(std::strstr(text,"course unlocked")&&duration==-1.f&&kind==0,"native course handler sends notification");
}
void __fastcall bag_ui_switch(void*,void*,int tab){check_bag_controls();check(tab==1,"bag switches native shop tab");}
void __fastcall bag_ui_select(void*,void*,unsigned,descriptor*){check_bag_controls();}
void __fastcall bag_ui_refresh(void*,void*,const void*){check_bag_controls();}
void __fastcall bag_row_state(void*,void*,int){} // row type 1 carries no descriptor
// Unlike the prices fixture, seed row type 1 and preserve retail buy dispatch.
__declspec(naked) void __fastcall bag_buy(void*,void*){__asm {
 push ebp
 push ebx
 push esi
 push edi
 sub esp,40h
 mov esi,ecx
 xor ebx,ebx
 mov dword ptr[esp+18h],1
 push 05d3ff0h
 ret
}}
__declspec(naked) void bag_end(){__asm {
 add esp,40h
 pop edi
 pop esi
 pop ebx
 pop ebp
 ret
}}
void bag_checks(){
 auto local=gameplay_agreement::rules;
 check(local.finalize(),"installed rule52 finalizes");
 auto values=local.rule(1).values;
 check(values[0]==127,"merged rule52 parts 127 = 1|2|4|8|16|32|64 (plan A full set)");
 gameplay_capabilities::record same_peer;
 same_peer.add(52,1,values,true);same_peer.finalize();
 check(local.compare(same_peer.bytes())==gameplay_capabilities::result::match,"matching step6 peer agrees");
 values[0]^=8;gameplay_capabilities::record old_peer;old_peer.add(52,1,values,true);old_peer.finalize();
 gameplay_capabilities::difference difference;
 check(local.compare(old_peer.bytes(),&difference)!=gameplay_capabilities::result::match,"peer without bag part refused");
 char refusal[128]{};local.describe(difference.buckets,0,true,refusal,sizeof refusal);
 check(std::strstr(refusal,"52 Xml1Economy")!=nullptr,"parts mismatch names rule52 Xml1Economy");

 check(read<unsigned char>(at(0x5d3720))==0xe9,"whole bag reward entry replaced before native vt+138 generator");
 if(read<unsigned char>(at(0x5d3720))!=0xe9)return;
 jump(0x44b8f0,&get_registry);
 registry_vt[0x50/4]=reinterpret_cast<void*>(&highest);
 jump(0x560190,&bag_random);jump(0x4c8bf0,&bag_course_count);jump(0x4c9c10,&bag_course_get);
 jump(0x4a1670,&bag_script_manager);jump(0x5d22f0,&bag_credit);jump(0x5a96a0,&bag_row_state);
 jump(0x5d35d0,&bag_ui_switch);jump(0x5d2460,&bag_ui_select);jump(0x5d4133,&bag_end);
 bag_script_vt[0xc/4]=reinterpret_cast<void*>(&bag_add_script);bag_script_vt[0x3c/4]=reinterpret_cast<void*>(&bag_run_script);
 bag_value_vt[0x14/4]=reinterpret_cast<void*>(&bag_argument_text);
 bag_text_vt[0x8/4]=reinterpret_cast<void*>(&bag_localized);bag_hud_vt[0xc/4]=reinterpret_cast<void*>(&bag_course_notice);
 jump(0x4d5830,&bag_argument);jump(0x672170,&bag_copy);jump(0x672204,&bag_token);
 jump(0x602120,&bag_length);jump(0x41a320,&bag_intern);jump(0x4d17b0,&bag_course_lookup);
 jump(0x4bdb30,&bag_get_text);jump(0x5604f0,&bag_format);jump(0x5eb300,&bag_get_hud);jump(0x672161,&cookie);
 put(at(0x688314),0x140,addr(&bag_notify));
 std::array<void*,32> shop_vt{};shop_vt[0x40/4]=reinterpret_cast<void*>(&bag_ui_refresh);put(price_shop.data(),0,shop_vt.data());
 // Two unique definitions per pool for each G; obtained subsets force retry/empty.
 // The resolved sidecar used by this fixture mirrors these definitions.
 for(unsigned i=0;i<equipment_count;++i)set_obtained(first_economy+i,false);
 for(unsigned i=0;i<20;++i)put(definition(first_economy+i),0x3b,static_cast<unsigned char>(4));
 unsigned short old_cw=0;
 __asm fnstcw old_cw
 const auto old_mxcsr=_mm_getcsr();
 for(unsigned precision:{0u,0x200u})for(unsigned group=1;group<=5;++group)for(unsigned outcome=0;outcome<6;++outcome){
  const unsigned short chosen=static_cast<unsigned short>((old_cw&~0xf00u)|precision|0x400u);__asm fldcw chosen
  _mm_setcsr((old_mxcsr&~0x6000u)|0x4000u);bag_cw=chosen;bag_mxcsr=_mm_getcsr();
  bag_controls=true;bag_draws=bag_debits=bag_notifications=bag_script_adds=bag_script_runs=0;credits=0;bag_command.clear();
  bag_course_notifications=0;
  bag_awarded={};
  const auto unsynchronized=read<unsigned>(at(0x7ad8b0));
  const auto native_draws=read<unsigned>(at(0x7ad8b4));
  highest_level=group==1?8:int(group-1)*10;
  const unsigned start=first_economy+(group-1)*4;
  for(unsigned i=0;i<20;++i)set_obtained(first_economy+i,false);
  if(outcome>=3)for(unsigned i=0;i<(outcome==3?2u:4u);++i)set_obtained(start+i,true);
  bag_courses.clear();
  const bool course_outcome=outcome==2||outcome==5;
  if(course_outcome)bag_courses={{1,1,1},{static_cast<unsigned char>(highest_level-5),1,0},{static_cast<unsigned char>(highest_level-6),0,0},{static_cast<unsigned char>(highest_level-6),0x100,0}};
  for(unsigned i=0;i<bag_courses.size();++i){auto* c=bag_course_memory[i].data();put(c,0,bag_courses[i].level);put(c,0x4c,bag_courses[i].available);put(c,0x4f,bag_courses[i].flags);put(c,0x44,i+1);}
  if(outcome==0)bag_script={0,.9,.9,.9};
  if(outcome==1)bag_script={.9,.9,.9,.9};
  if(outcome==2)bag_script={.9,.9,.9,.05};
  if(outcome==3)bag_script={0,.9,.9,.9,.9};
  if(outcome==4)bag_script={.9,.9,.9};
  if(outcome==5)bag_script={.9,.9,.05};
  // For course strings use the real intern getter on the invented name table.
  bag_buy(price_shop.data(),nullptr);
  check(bag_draws==bag_script.size(),"buy-to-bag exact selection draw count");
  const int G=std::min(5,highest_level/10+1);
  check(bag_debits==1&&credits==-2000*G*G,"native 5d3ff0 -> 5d40a3 charges exactly one 2000G^2 debit, no refund");
  check(bag_controls,"x87 and MXCSR restored before all native environment calls");
  check(bag_script_adds==unsigned(course_outcome)&&bag_script_runs==unsigned(course_outcome),"equipment/retry/empty outcomes vs script course route");
  check(bag_course_notifications==unsigned(course_outcome),"native handler notifies exactly on course outcome");
  if(course_outcome)check((read<unsigned char>(bag_course_memory[3].data(),0x4f)&1)!=0,"native 4a739a unlocks selected course");
  if(course_outcome)check(bag_command=="loadDangerRoomCourse('item0003')","first eligible raw course uses identifier +44");
  check(bag_notifications==(outcome==0||outcome==1||outcome==3),"native inventory notification only for equipment");
  if(bag_awarded.pool){
   const unsigned index=start+(outcome==0?0:2);
   check(read<void*>(instance(bag_awarded),4)==definition(index)&&read<unsigned char>(instance(bag_awarded),0x2e)==2&&obtained(index),"selected definition constructed, native inventory state2 and unique obtained set");
  }
  check(read<unsigned>(at(0x7ad8b0))==unsynchronized&&read<unsigned>(at(0x7ad8b4))==native_draws,"zero construction draws and unsynchronized stream unchanged");
  if(outcome==0)check(obtained(start),"awarded unique sets obtained through native 47ac50(1)");
 }
 __asm fldcw old_cw
 _mm_setcsr(old_mxcsr);
}
