// Included in the carry fixture namespace. Run the complete retail42B6C0
// probe, using real owned libIGMath operations; only world intersection and
// query allocation are external stand-ins. No game/world startup runs.
int probe_property=0,probe_intersections=0;
V probe_contact_position{};
V* __fastcall probe_center(void* who,void*,V* out){*out=get<V>(who,0x20);return out;}
void** __fastcall probe_acquire(void** out,void*,const u32*){*out=air_query.data();return out;}
void __fastcall probe_filter(void*,void*,int mode,int property){check(mode==2,"native movement probe intersects property sets");if(property==7||property==8)probe_property=property;}
bool __cdecl probe_intersection(u32 self,u32 owner,const V* start,const V*,void** contact,void** q){
 ++probe_intersections;
 check(self==0x1111&&*q==air_query.data(),"complete native probe supplies self and query reference to intersection");
 // Literal fixture: selected victim occupies properties7 AND8. A native
 // flag0 probe therefore still meets it unless its full owner token is
 // excluded. An independent solid ledge always blocks either owner choice.
 const bool victim_blocks=probe_property==7||(probe_property==8&&owner!=0x2222);
 if(victim_blocks||clearance_blocked){*contact=victim.data();probe_contact_position=*start;return true;}
 *contact=nullptr;return false;
}
const V* __fastcall probe_contact(void*,void*){return &probe_contact_position;}
void complete_native_clearance_test(){
 wchar_t system_path[MAX_PATH]{};GetSystemDirectoryW(system_path,MAX_PATH);
 const std::wstring system_input=std::wstring(system_path)+L"\\dinput.dll";
 const auto input_module=LoadLibraryW(system_input.c_str());
 check(input_module!=nullptr,"engine math dependency uses system dinput, not the production test DLL");
 wchar_t engine[MAX_PATH]{};const auto size=GetEnvironmentVariableW(L"XML2_FIX_TEST_GAME_DIR",engine,MAX_PATH);
 check(size>0&&size<MAX_PATH,"XML2_FIX_TEST_GAME_DIR supplies the owned engine DLL folder");if(!size||size>=MAX_PATH)return;
 std::wstring math_path=std::wstring(engine)+L"\\libIGMath.dll";std::replace(math_path.begin(),math_path.end(),L'/',L'\\');
 const auto math=LoadLibraryExW(math_path.c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
 if(!math)std::printf("native probe libIGMath load error %lu\n",GetLastError());
 check(math!=nullptr,"owned libIGMath available for complete probe ABI");if(!math)return;
 struct probe_import {u32 address;const char* name;void* saved;};
 probe_import imports[]{
  {0x67f9e0,"??0igVec3f@Math@Gap@@QAE@ABV012@@Z",nullptr},
  {0x67fa0c,"??GigVec3f@Math@Gap@@QBE?AV012@ABV012@@Z",nullptr},
  {0x67fa10,"??HigVec3f@Math@Gap@@QBE?AV012@ABV012@@Z",nullptr},
  {0x67fa2c,"??4igVec3f@Math@Gap@@QAEAAV012@ABV012@@Z",nullptr}};
 for(auto& i:imports){i.saved=get<void*>(ptr(i.address),0);const auto function=GetProcAddress(math,i.name);check(function!=nullptr,"retail probe math export resolves");put(ptr(i.address),0,function);}
 const auto old_center=actor_vt[0x24/4],old_filter=query_vt[0x40/4];
 native(0x42b6c0,0x178);jump(0x4ab670,&probe_acquire);jump(0x4ab7f0,&probe_intersection);jump(0x4a9620,&probe_contact);
 actor_vt[0x24/4]=reinterpret_cast<void*>(&probe_center);query_vt[0x40/4]=reinterpret_cast<void*>(&probe_filter);
 for(bool ledge:{false,true}){
  air_reset();sequence_mode=true;selected=move.data();clearance_blocked=ledge;probe_intersections=0;
  put(air_input.data(),0,0x100u);put(actor.data(),0x20,V{10,20,73});put(victim.data(),0x20,V{10,20,3});put(victim.data(),4,0x180u);put(actor.data(),0x178,0x3333u);
  unsigned before=0,after=0;__asm mov before,esp
  descent_probe(actor.data(),nullptr,air_input.data());
  __asm mov after,esp
  check(before==after&&probe_intersections==2&&probe_property==8,"native full probe retains ret20 ABI and selects property8 for flag0");
  check(carry_entries==(ledge?0:1)&&get<V>(actor.data(),0x20).z==(ledge?73.f:43.f)&&get<V>(victim.data(),0x20).z==3.f,"native probe lowers only carrier through contacted character and still blocks on a solid ledge");
  check(get<u32>(actor.data(),0x178)==0x3333u,"native probe always restores carrier owner token");
  callback<cleanup_t>(air_handler("ch_fly_guard_decide"),3)(nullptr,nullptr,actor.data());
 }
 sequence_mode=false;actor_vt[0x24/4]=old_center;query_vt[0x40/4]=old_filter;
 for(const auto& i:imports)put(ptr(i.address),0,i.saved);
 FreeLibrary(math);air_bind();
}
