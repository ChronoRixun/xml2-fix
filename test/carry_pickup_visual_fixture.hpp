// The live pickup's first two seconds used no hand attachment. Unlike the old
// sequence fixture, execute CCEAtkVictim with targetdistance=1/offset=-40, then
// ask the retail bone/world-transform path where the rendered hands are.
void* __fastcall visual_handle(void* who,void*,u32 token){put(who,0,token);return who;}
bool __fastcall visual_probe(void*,void*,const V*,const V* delta,int,V* shift,void** contact){*shift=*delta;*contact=nullptr;return false;}
int visual_bone_reads=0;
float visual_pose_phase=0;
bool __fastcall visual_bone(void* who,void*,int id,Matrix* out){
 ++visual_bone_reads;const bool ok=world_bone(who,nullptr,id,out);
 // The renderer boundary supplies independently sampled pickup poses. An
 // early victim pose reaches below its root, then lifts while both turn.
 const bool carrier=who==world_skeletons[0].data();
 out->f[12]+=std::sin(visual_pose_phase)*(carrier?3.f:-8.f);
 out->f[13]+=std::cos(visual_pose_phase)*(carrier?2.f:7.f);
 if(!carrier)out->f[14]-=60.f*(std::max)(0.f,1.f-visual_pose_phase);
 return ok;
}
void __fastcall visual_world_matrix(void* who,void*,Matrix* out,int zero){
 check(zero==0,"pickup renderer supplies its committed world transform");
 void* actor_=who==world_objects[0].data()?actor.data():victim.data();
 const auto p=get<V>(actor_,0x20);matrix_translation(out,nullptr,&p);
 // Model world matrices follow +1f4, not the angle setter's blended +2c.
 const float yaw=get<V>(actor_,0x1f4).z;
 out->f[0]=out->f[5]=std::cos(yaw);out->f[1]=std::sin(yaw);out->f[4]=-std::sin(yaw);
}
void pickup_visual_test(){
 wchar_t folder[MAX_PATH]{};GetEnvironmentVariableW(L"XML2_FIX_TEST_GAME_DIR",folder,MAX_PATH);
 const auto math=LoadLibraryExW((std::wstring(folder)+L"\\libIGMath.dll").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
 check(math!=nullptr,"pickup fixture loads owned renderer math");if(!math)return;
 const auto crt=LoadLibraryExW((std::wstring(folder)+L"\\MSVCR71.dll").c_str(),nullptr,LOAD_WITH_ALTERED_SEARCH_PATH);
 check(crt!=nullptr,"pickup fixture loads owned angle-wrap runtime");if(!crt){FreeLibrary(math);return;}
 struct saved_import{u32 address;void* value;};std::vector<saved_import> saved;
 const auto wrap=GetProcAddress(crt,"_CIfmod");check(wrap!=nullptr,"native angle-wrap import resolves");
 saved.push_back({0x67f198,get<void*>(ptr(0x67f198),0)});put(ptr(0x67f198),0,wrap);native(0x672336,6);
 const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(retail_bytes.data()+get<u32>(retail_bytes.data(),0x3c));
 auto raw=[&](u32 rva)->const unsigned char*{const auto* sec=IMAGE_FIRST_SECTION(nt);for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i)if(rva>=sec[i].VirtualAddress&&rva<sec[i].VirtualAddress+sec[i].SizeOfRawData)return retail_bytes.data()+sec[i].PointerToRawData+rva-sec[i].VirtualAddress;return nullptr;};
 auto imports=reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(raw(nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress));
 for(;imports->Name;++imports){
  if(_stricmp(reinterpret_cast<const char*>(raw(imports->Name)),"libIGMath.dll"))continue;
  auto names=reinterpret_cast<const u32*>(raw(imports->OriginalFirstThunk));
  for(unsigned i=0;names[i];++i){
   const auto f=GetProcAddress(math,reinterpret_cast<const char*>(raw(names[i])+2));
   check(f!=nullptr,"pickup native math import resolves");const u32 address=0x400000+imports->FirstThunk+i*4;
   saved.push_back({address,get<void*>(ptr(address),0)});put(ptr(address),0,f);
  }
 }
 const auto old188=actor_vt[0x188/4],old30=move_vt[0x30/4],olde8=move_vt[0xe8/4],old28=move_vt[0x28/4];
 const auto old130=actor_vt[0x130/4],old158=actor_vt[0x158/4],old34=actor_vt[0x34/4],old150=actor_vt[0x150/4];
 actor_vt[0x188/4]=reinterpret_cast<void*>(&world_model);move_vt[0x30/4]=ptr(0x500640);move_vt[0xe8/4]=ptr(0x500840);
 move_vt[0x28/4]=reinterpret_cast<void*>(&no_powerup);
 actor_vt[0x130/4]=ptr(0x42d450);actor_vt[0x158/4]=ptr(0x41c430);actor_vt[0x34/4]=ptr(0x41c400);actor_vt[0x150/4]=reinterpret_cast<void*>(&air_position);
 for(int i=0;i<2;++i){put(world_models[i].data(),0,world_renders[i].data());put(world_renders[i].data(),0,world_render_vt.data());put(world_skeletons[i].data(),0,world_skeleton_vt.data());put(world_objects[i].data(),0,world_object_vt.data());}
 world_render_vt[5]=reinterpret_cast<void*>(&world_skeleton);world_render_vt[2]=reinterpret_cast<void*>(&world_object);world_skeleton_vt[25]=reinterpret_cast<void*>(&visual_bone);world_object_vt[4]=reinterpret_cast<void*>(&visual_world_matrix);
 for(auto range:std::array<std::pair<u32,unsigned>,12>{{{0x41d620,0x5f},{0x569370,0x4b},{0x5692e0,0x90},{0x568e90,0x448},{0x500640,7},{0x500840,7},{0x42d450,0xfb},{0x41c430,0x33},{0x41c400,0x2b},{0x4daf00,0x430},{0x4f9120,0x5c},{0x680030,4}}})native(range.first,range.second);
 native(0x560700,0x3a0);native(0x690818,4);native(0x69af54,4);native(0x68629c,4);native(0x6e0878,4);native(0x6908b4,4);native(0x682a2c,4);native(0x684070,4);jump(0x4a9570,&visual_handle);jump(0x42b6c0,&visual_probe);
 native(0x4f5a30,0x70);native(0x4f5cf0,0x187);
 const auto enter=reinterpret_cast<void(__thiscall*)(void*,void*,void*,void*)>(0x4f5a30);
 const auto tick=reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(0x4f5cf0);
 // A baseline without the registration guard leaves this fixture word empty.
 if(!get<void*>(ptr(0x692318),0))native(0x692318,4);
 const auto execute=reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(get<void*>(ptr(0x692318),0));
 auto* pickup=air_handler("ch_air_grab_pickup"),*vh=air_handler("ch_air_grab_victim");
 auto aligned=[&](){
  V a5{},a6{},v5{},v6{};auto bone=reinterpret_cast<bool(__thiscall*)(void*,int,V*,bool,bool)>(0x41d620);
  const bool ok=bone(actor.data(),5,&a5,false,false)&&bone(actor.data(),6,&a6,false,false)&&bone(victim.data(),5,&v5,false,false)&&bone(victim.data(),6,&v6,false,false);
  return ok&&std::fabs(a5.x+a6.x-v5.x-v6.x)<.001f&&std::fabs(a5.y+a6.y-v5.y-v6.y)<.001f&&std::fabs(a5.z+a6.z-v5.z-v6.z)<.001f;
 };
 for(bool ally:{false,true})for(bool victim_first:{false,true}){
  air_reset();sequence_mode=event_mode=true;visual_pose_phase=0;victim_team=ally?actor_team:2;put(idle.data(),0,move_vt.data());
  put(actor.data(),4,0x10080u);put(victim.data(),4,0x10080u);
  // 082f: carrier lowered to z44, victim on z4; no prior pair.
  put(actor.data(),0x20,V{760,650,44});put(victim.data(),0x20,V{759.948f,649.830f,4});put(actor.data(),0x178,0u);put(victim.data(),0x178,0u);
  put(actor.data(),0x1f4,V{0,0,1});put(victim.data(),0x1f4,V{0,0,-2});
  put(other.data(),0,move_vt.data());put(other.data(),0xc8,pickup);put<unsigned char>(other.data(),0x146,0);put(actor.data(),0x378,other.data());put(air_context.data(),4,other.data());
  put(move.data(),0xc8,vh);put(move.data(),0xbc,anim_enums::extension_id("ea_flyingcarry_pickup_a"));
  put<unsigned char>(move.data(),0x146,0);
  enter(other.data(),actor.data(),air_input.data(),air_context.data());
  check(get<u32>(actor.data(),0x178)==0x2222&&get<u32>(victim.data(),0x178)==0x1111,"pickup entry owns the pair before any victim callback");
  check(aligned(),"pickup entry aligns real animated world hands before first frame");
  tick(move.data(),victim.data(),air_input.data());
  check(air_starts==0,"victim callback before time-zero event cannot release pickup as no-carrier");
  // Exact authored values; old fixtures used -1 and skipped all placement.
  std::array<unsigned char,0x24> event{};reinterpret_cast<void*(__thiscall*)(void*)>(0x4f9120)(event.data());put(event.data(),0x18,1.f);put(event.data(),0x1c,-40.f);put<unsigned char>(event.data(),0x20,0x25);
  unsigned before=0,after=0;__asm mov before,esp
  execute(event.data(),actor.data(),nullptr);
  __asm mov after,esp
  check(before==after,"authored pickup event keeps ret8 ABI");
  check(aligned(),"native time-zero distance1/offset-40 placement cannot overwrite hand alignment");
  for(int frame=0;frame<70;++frame){
   visual_pose_phase=frame*.05f;
   put(actor.data(),0x20,V{760.f+frame,650.f+frame/2.f,44.f+frame});
   put(actor.data(),0x2c,V{0,0,frame*.05f});put(actor.data(),0x1f4,V{0,0,frame*.05f});
   if(frame==1)reinterpret_cast<void(__thiscall*)(void*)>(0x42c270)(victim.data());
   if(victim_first)tick(move.data(),victim.data(),air_input.data());
   tick(other.data(),actor.data(),air_input.data());
   if(!victim_first)tick(move.data(),victim.data(),air_input.data());
   check(aligned()&&(get<u32>(victim.data(),4)&7)==7&&(get<unsigned char>(victim.data(),0x764)&0x10)&&air_starts==0,"each pickup frame follows world hands in XYZ without a physics/reset gap");
  }
  // This path changes pose/ownership only, including for invisible characters.
  check((get<u32>(victim.data(),4)&0x10080)==0x10080,"pickup preserves unrelated physical/model flags");
  sequence_mode=false;
 }
 // The registration is shared by every ground/TK victim event too. Those
 // events must still execute the original placement, without querying bones.
 air_reset();sequence_mode=event_mode=true;put(actor.data(),4,0x10080u);put(victim.data(),4,0x10087u);
 put(other.data(),0,move_vt.data());put(other.data(),0xc8,air_handler("ch_air_grab_pickup_idle"));put(actor.data(),0x378,other.data());
 std::array<unsigned char,0x24> unrelated{};reinterpret_cast<void*(__thiscall*)(void*)>(0x4f9120)(unrelated.data());put(unrelated.data(),0x18,1.f);put(unrelated.data(),0x1c,-40.f);put<unsigned char>(unrelated.data(),0x20,0x25);
 const int before_bones=visual_bone_reads;
 const auto initial_actor=actor,initial_victim=victim;
 reinterpret_cast<void(__thiscall*)(void*,void*,void*)>(0x4daf00)(unrelated.data(),actor.data(),nullptr);
 const auto expected_actor=actor,expected_victim=victim;
 actor=initial_actor;victim=initial_victim;
 execute(unrelated.data(),actor.data(),nullptr);
 check(visual_bone_reads==before_bones&&actor==expected_actor&&victim==expected_victim,"unrelated native victim event is byte-identical to direct retail execution and never queries hands");
 sequence_mode=event_mode=false;
 for(auto i:saved)put(ptr(i.address),0,i.value);FreeLibrary(crt);FreeLibrary(math);
 actor_vt[0x188/4]=old188;actor_vt[0x130/4]=old130;actor_vt[0x158/4]=old158;actor_vt[0x34/4]=old34;actor_vt[0x150/4]=old150;move_vt[0x30/4]=old30;move_vt[0xe8/4]=olde8;move_vt[0x28/4]=old28;
 world_skeleton_vt[25]=reinterpret_cast<void*>(&world_bone);world_object_vt[4]=reinterpret_cast<void*>(&world_matrix);
 air_bind();
}
