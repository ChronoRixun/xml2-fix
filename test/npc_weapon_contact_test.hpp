// Execute owned event-ID construction, beam attack copies and all three native
// duplicate predicates. Geometry/query services and vulnerable HP sink are
// synthetic; no game process, serialized fixtures or extracted code is shipped.
void* contact_id_code = nullptr;
void* contact_filter_code = nullptr;
void* character_filter_code = nullptr;
void* physical_filter_code = nullptr;
void* remember_id_code = nullptr;
__declspec(naked) void contact_generate_id(void*, void*, void*)
{
 __asm {
  push ebx
  push esi
  mov ecx, [esp + 12]
  mov ebx, [esp + 16]
  mov esi, [esp + 20]
  call dword ptr [contact_id_code]
  pop esi
  pop ebx
  ret
 }
}
__declspec(naked) bool contact_native_eligible(void*, void*)
{
 __asm {
  push esi
  push edi
  push ebx
  push ebp
  mov edi, [esp + 20]
  mov esi, [esp + 24]
  call dword ptr [contact_filter_code]
  test al, al
  jz contact_refused
  mov ebp, edi
  mov ebx, esi
  call dword ptr [character_filter_code]
  test al, al
  jz contact_refused
  // The physical routine's local ID is at ESP+0x4c on entry to its predicate.
  mov eax, [esi + 0x28]
  mov esi, edi
  sub esp, 0x4c
  mov [esp + 0x48], eax
  call dword ptr [physical_filter_code]
  test al, al
  jz physical_refused
  call dword ptr [remember_id_code]
  mov al, 1
 physical_refused:
  add esp, 0x4c
 contact_refused:
  pop ebp
  pop ebx
  pop edi
  pop esi
  ret
 }
}
std::array<std::array<std::byte, 0x400>, 2> contact_victims{};
unsigned contact_count = 1, contact_target = 0, contact_queries = 0, contact_deliveries = 0;
unsigned char contact_material = 0x10;
npc_semi_auto::point contact_position{10, 60, 30}, contact_normal{};
void contact_damage(void* victim, void* attack)
{
 using namespace npc_semi_auto;
 if (!contact_native_eligible(victim, attack)) return;
 // Deliberately synthetic HP arithmetic after the real native refusal paths.
 field<float>(victim, 0x100) -= field<float>(attack, 4);
 ++contact_deliveries;
}
bool __fastcall contact_manager_hit(void*, void*, void*, void* victim, void* attack, u32)
{ contact_damage(victim, attack); return true; }
void __cdecl contact_bullet_hit(void* victim, void* attack) { contact_damage(victim, attack); }
bool __cdecl contact_physent(void*, u32) { return true; }
npc_semi_auto::bullet_contact contact_bullet_query(void*, const npc_semi_auto::bullet_segment&)
{
 ++contact_queries;
 return {contact_victims[contact_target].data(), contact_position, contact_normal, contact_count != 0};
}
unsigned __fastcall contact_size(void*, void*) { return contact_count; }
void* __fastcall contact_entity(void*, void*, unsigned index) { return contact_victims[(index+contact_target)%2].data(); }
void __fastcall contact_record(void*, void*, u32* out, unsigned index) { *out = index; }
void __fastcall contact_filter(void*, void*, u32, u32) {}
void __fastcall contact_sweep(void*, void*, void*, void*, void*) { ++contact_queries; }
void* __fastcall contact_enum(void* self, void*, void*) { return self; }
void* contact_results = nullptr;
void* __cdecl contact_collect(void*, void*) { return contact_results; }
void __fastcall contact_query(void** self, void*, void*, void*) { *self = contact_results; }
npc_semi_auto::point* __fastcall contact_get_position(void*, void*) { return &contact_position; }
npc_semi_auto::point* __fastcall contact_get_normal(void*, void*) { return &contact_normal; }
unsigned char* __fastcall contact_get_material(void*, void*) { return &contact_material; }
void* contact_manager = nullptr;
std::array<std::byte, 0x6c> contact_attack{};
std::array<std::byte, 0x50> contact_metadata{};
bool __fastcall contact_native_event(void*, void*, void* context)
{
 using namespace npc_semi_auto;
 auto* actor = field<void*>(context, 0);
 std::array<std::byte, 0x20> id_context{};
 field<void*>(id_context.data(), 0x14) = contact_metadata.data();
 contact_generate_id(actor, id_context.data(), contact_attack.data());
 check(field<u32>(contact_attack.data(), 0x28) == field<u32>(actor, 0x38c),
       "native event creates move-base plus the same zero Index on every invocation");
 const auto original_attack = contact_attack;
 std::array<std::byte, 0x270> frame{};
 field<void*>(frame.data(), 0x20c) = contact_attack.data();
 field<point>(frame.data(), 0x44) = field<point>(frame.data(), 0x110) = {10,20,30};
 field<point>(frame.data(), 0x50) = {0,1,0};
 field<float>(frame.data(), 0x214) = 550;
 field<unsigned char>(frame.data(), 0x218) = 1; // synthetic query has its filters
 field<unsigned char>(frame.data(), 0x238) = 1; // no synthetic muzzle obstruction
 check(safe_trace_fragment(actor, frame.data()), "admitted/rejected owned beam contact fragment executes");
 if (current_bullet->row) {
  check(contact_attack == original_attack, "marked native call preserves every caller-owned attack byte");
  auto expected = original_attack; field<u32>(expected.data(), 0x28) = 0;
  // Native endpoint math writes only geometry in the private call's copy.
  const auto* copy = current_bullet->attack.data();
  check(!std::memcmp(expected.data(), copy, 0x40) && !std::memcmp(expected.data()+0x64, copy+0x64, 8),
        "private native attack retains damage/type/modifiers and flags with only ID zero before geometry");
 }
 return true;
}
void native_weapon_contact_cases(const char* pc_path)
{
 using namespace npc_semi_auto;
 // Reuse the owned-input loader; only the supplied image contributes bytes.
 bullet_owned_code fixture(pc_path);
 constexpr u32 first = 0x452225, end = 0x452e63;
 const auto load = [&](u32 va, u32 size, unsigned at) { return fixture.copy_pc(pc_path, va, size, at); };
 const bool loaded = load(first, end-first, 0) && load(0x4350d0, 0xb9, 0x1000) &&
  load(0x4353b0, 0x86, 0x1200) && load(0x4dc30f, 0x10, 0x1400) &&
  load(0x4532d6, 0x16, 0x1500) && load(0x4293b6, 0x13, 0x1600) &&
  load(0x498030, 0x14, 0x1700) && load(0x498740, 0xe, 0x1800);
 check(loaded, "owned event ID, beam copy, contact/character/physical predicates and ID remember readable");
 if (!loaded) return;
 const auto branch = [&](u32 va, const void* target, unsigned length = 5) {
  auto bytes = stun_rules::branch(u32(reinterpret_cast<std::uintptr_t>(fixture.code + va-first)), target, false, length);
  std::memcpy(fixture.code + va-first, bytes.data(), bytes.size());
 };
 // Success/failure return stubs are synthetic; the actual comparisons and
 // conditional branches come from retail. Rebase only their external exits.
 fixture.code[0x1900]=0xb0; fixture.code[0x1901]=1; fixture.code[0x1902]=0xc3;
 fixture.code[0x1910]=0x32; fixture.code[0x1911]=0xc0; fixture.code[0x1912]=0xc3;
 const auto success = fixture.code+0x1900, failure = fixture.code+0x1910;
 const auto exit = [&](unsigned at, const void* target) {
  auto bytes = stun_rules::branch(u32(reinterpret_cast<std::uintptr_t>(fixture.code+at)),target,false,5);
  std::memcpy(fixture.code+at,bytes.data(),5);
 };
 exit(0x1516,success); fixture.rel(0x1511,failure);
 exit(0x1613,success); fixture.rel(0x160d,failure,2,6);
 exit(0x1714,success); fixture.rel(0x170e,failure,2,6);
 fixture.code[0x1400+0x10]=0xc3; fixture.code[0x1800+0xe]=0xc3;
 fixture.rel(0x1209,fixture.code+0x1000); // native extended attack copy's base copy
 contact_id_code=fixture.code+0x1400; contact_filter_code=fixture.code+0x1500;
 character_filter_code=fixture.code+0x1600; physical_filter_code=fixture.code+0x1700;
 remember_id_code=fixture.code+0x1800;
 auto mul=&fragment_mul; auto add=&fragment_add; auto sub=&fragment_sub;
 auto copy=&fragment_copy; auto length=&fragment_length; auto init=&fragment_copy; float threshold=1;
 for (const auto [old,replacement] : {std::pair<u32,const void*>{0x67f9f8,&mul},{0x67fa10,&add},
      {0x67fa0c,&sub},{0x67fa2c,&copy},{0x67fa58,&length},{0x682a2c,&threshold},{0x67f8e0,&init}})
  fixture.replace(0,end-first,old,replacement);
 for (const auto [va,target] : {std::pair<u32,const void*>{0x45237d,&contact_query},
      {0x4523e4,&contact_enum},{0x4523ef,&contact_collect},{0x45245d,&contact_get_position},
      {0x452474,&contact_get_material},{0x4524bd,&contact_get_position},{0x4524d4,&contact_get_normal}})
  fixture.rel(va-first,target);
 fixture.rel(0x4524a9-first,fixture.code+0x1200);
 branch(0x452225,&bullet_muzzle_hook,7); branch(0x452350,&bullet_collision_hook);
 branch(0x452e5c,&weapon_presentation_hook,7); exit(end-first,&fragment_done);
 // After native virtual contact dispatch, omit unrelated impact/sound services.
 // Retain the native piercing enumeration loop and ordinary endpoint tail.
 branch(0x45250c,fixture.code+0x45273b-first);
 branch(0x452750,fixture.code+0x4528a9-first);
 FlushInstructionCache(GetCurrentProcess(),fixture.code,0x5000);
 std::array<void*,0x78/4> query_methods{};
 query_methods[0]=reinterpret_cast<void*>(&contact_size);
 query_methods[1]=reinterpret_cast<void*>(&contact_entity);
 query_methods[2]=reinterpret_cast<void*>(&contact_record);
 query_methods[0x40/4]=reinterpret_cast<void*>(&contact_filter);
 query_methods[0x74/4]=reinterpret_cast<void*>(&contact_sweep);
 void* query=query_methods.data(); contact_results=&query;
 std::array<void*,0x198/4> victim_methods{};
 victim_methods[0x194/4]=reinterpret_cast<void*>(&contact_get_material);
 std::array<void*,0x68/4> manager_methods{};
 manager_methods[0x64/4]=reinterpret_cast<void*>(&contact_manager_hit);
 void* manager=manager_methods.data(); contact_manager=&manager;
 const auto mr=muzzle_resume,ms=muzzle_skipped,cr=collision_resume,cs=collision_skipped,pr=presentation_resume;
 const auto address=[&](u32 va) {return u32(reinterpret_cast<std::uintptr_t>(fixture.code+va-first));};
 muzzle_resume=address(0x45222c); muzzle_skipped=address(0x45230b);
 collision_resume=address(0x452355); collision_skipped=address(0x4528a9); presentation_resume=address(end);
 const auto old_fragment=trace_fragment; const auto old_manager=trace_fragment_manager;
 trace_fragment=fixture.code; trace_fragment_manager=contact_manager;
 const auto old_weapons=weapons; const auto old_applied=applied; const auto old_event=event_function;
 const auto old_clock=clock_function; const auto old_frame=bullet_frame_function;
 const auto old_query=bullet_contact_function; const auto old_hit=bullet_take_hit_function;
 const auto old_class=bullet_class_function; const auto old_random=random_function;
 const auto old_states=trace_states; const auto old_presentations=beam_presentations;
 event_function=reinterpret_cast<event_t>(&contact_native_event); clock_function=&trace_clock;
 bullet_frame_function=&trace_frame; bullet_contact_function=&contact_bullet_query;
 bullet_take_hit_function=&contact_bullet_hit; bullet_class_function=&contact_physent; random_function=&trace_random;
 std::array<std::byte,0x800> actor{}; std::array<std::byte,0x500> stats{};
 std::memcpy(stats.data()+0x150,"traceguard",11); field<void*>(actor.data(),0x35c)=stats.data();
 field<u32>(actor.data(),0x1c)=0x123; field<u32>(actor.data(),0x38c)=19;
 field<point>(actor.data(),0x3ac)={560,20,66};
 std::array<void*,6> context{}; context[0]=actor.data();
 std::array<std::byte,0x50> event{}; field<void*>(event.data(),0x14)=contact_metadata.data();
 const auto reset=[&] {
  trace_states={}; beam_presentations={}; contact_queries=contact_deliveries=contact_target=0;
  contact_attack.fill(std::byte{0x42}); field<float>(contact_attack.data(),4)=35;
  for(auto& v:contact_victims) {v={};field<void*>(v.data(),0)=victim_methods.data();field<float>(v.data(),0x100)=500;}
 };
 const auto hp=[&](unsigned i=0) {return field<float>(contact_victims[i].data(),0x100);};
 const auto fire=[&](float time) {fixture_time=time;check(bullet_event(event.data(),nullptr,context.data()),"weapon event returns normally");};
 for(const auto kind:{type_beam,type_flame,type_bullet}) {
  weapons={*parse("traceguard|3e23d70a|44098000|3fc00000|00000000|00000000|00000013|3f000000|00000000")};
  weapons[0].flags=kind; applied=true; contact_count=kind==type_flame?2:1;
  field<u32>(event.data(),0x3c)=kind==type_bullet?bullet_marker:nonbullet_marker;
  reset(); fire(.5f); check(hp()==465,"first admitted contact changes HP with native nonzero move ID construction");
  fire(.6f);check(hp()==465&&contact_queries==1,"rejected trace neither queries nor changes HP");
  fire(1.f);check(hp()==430&&contact_queries==2,"same tagged event in same move hits same victim twice through native duplicate checks");
  if(kind==type_flame)check(hp(1)==430&&contact_deliveries==4,"both piercing victims take both admitted flame hits");
  check(field<u32>(contact_victims[0].data(),0x228)==0,"ID zero never overwrites victim remembered IDs");
  field<u32>(contact_victims[0].data(),0x228)=777;fire(1.5f);
  check(hp()==395&&field<u32>(contact_victims[0].data(),0x228)==777,"marked contact bypasses remembered ID without clearing it");
  reset();contact_count=0;fire(.5f);contact_count=1;fire(.6f);
  check(hp()==500&&contact_queries==1,"admitted miss spends admission, subsequent contact is rejected");
  field<u32>(actor.data(),0x38c)=38;contact_target=1;fire(.7f);
  check(hp()==500&&contact_queries==1,"other move and target do not reset actor-wide admission");
  fire(1.f);check(hp()==500&&hp(1)==465,"later admission in another move/target still delivers");
  field<u32>(actor.data(),0x38c)=19;
 }
 for(unsigned control=0;control!=3;++control) {
  reset();contact_count=1;weapons[0].flags=type_beam;
  applied=control!=1;field<u32>(event.data(),0x3c)=control==0?0:nonbullet_marker;
  field<unsigned char>(event.data(),0x45)=control==2?1:0;
  fire(.5f);fire(1.f);
  check(hp()==465&&field<u32>(contact_victims[0].data(),0x228)==19,
        "unmarked/OFF/split controls retain native move-ID duplicate rejection");
 }
 weapons=old_weapons;applied=old_applied;event_function=old_event;clock_function=old_clock;
 bullet_frame_function=old_frame;bullet_contact_function=old_query;bullet_take_hit_function=old_hit;
 bullet_class_function=old_class;random_function=old_random;trace_states=old_states;beam_presentations=old_presentations;
 trace_fragment=old_fragment;trace_fragment_manager=old_manager;
 muzzle_resume=mr;muzzle_skipped=ms;collision_resume=cr;collision_skipped=cs;presentation_resume=pr;
 std::printf("  native weapon contacts: beam/flame/bullet same-move HP, piercing, miss, rejected and retail controls\n");
}
