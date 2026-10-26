// Execute the owner's original input gate with synthetic dependencies. This
// complements live trials; no game code or data is embedded in this fixture.
int source_animation = 0;
float source_recoil_value(float recoil) { return recoil; }
int __fastcall source_category(void*, void*) { return 0; }
int __fastcall source_animation_getter(void*, void*) { return source_animation; }
float source_warmup = 0; bool source_continuous = false;
float __fastcall source_no_warmup(void*, void*) { return source_warmup; }
bool __fastcall source_not_continuous(void*, void*) { return source_continuous; }
void* const source_charge_effect = reinterpret_cast<void*>(0x5eed);
void* __fastcall source_charge(void*, void*) { return source_charge_effect; }
unsigned source_warm_starts = 0;
void __fastcall source_start_warmup(void*, void*, void* effect)
{ ++source_warm_starts; check(effect == source_charge_effect, "original warm-up start receives the weapon's charge effect"); }
std::array<void*, 64> source_actor_vtable{};
void __fastcall source_request(void*, void*, void*, int animation, float duration, u32 flags, void*, void*)
{ ++requests; requested_animation = animation; requested_flags = flags; check(duration == -1, "original native duration"); }

// Frame-by-frame: the executed original gate (hash-bound, synthetic dependencies) and the
// restored gate run the same held-fire timeline for every weapon family at 30, 49 and 60 fps,
// with the semi-auto entry arming the recoil deadline on each power_attack (0xe14d0 on the
// original side, the production entry callback on the port side) and a fire release mid-way.
// Decisions, play flags, warm-up starts and both deadlines must agree bit for bit.
struct family { const char* name; const char* row; float recoil, warmup; bool continuous, pistol = false; };
constexpr family families[] = {
 {"discrete laser", "test_guard|3e2e147b|44098000|3fc00000|00000000|00000000|00000000|00000000|00000000", .17f, 0, false},
 {"nullifier warm-up per shot", "test_guard|3f333333|44098000|3f800000|00000001|3f000000|00000003|00000000|00000000", .7f, .5f, false},
 {"knockback warm-up per shot", "test_guard|3f4ccccd|44098000|3f800000|00000001|3f000000|00000002|00000000|00000000", .8f, .5f, false},
 {"lightning continuous", "test_guard|00000000|44098000|40200000|00000001|3f19999a|00000013|00000000|00000000", 0, .6f, true},
 {"flame continuous", "test_guard|3cf5c28f|43898000|40600000|00000000|3f800000|00000011|00000000|00000000", .03f, 1.f, true},
 // Default-handler primary: no semi-auto entry arms the recoil deadline on either side.
 {"pistol default handler", "test_guard|3e4ccccd|44098000|3fc00000|00000000|00000000|00000040|00000000|00000000", .2f, 0, false, true},
};
void timeline_comparison(void* code, std::byte* source_actor, std::byte* input, void* pc_actor)
{
 using namespace npc_semi_auto;
 const auto saved = weapons;
 for (const auto& f : families) for (const int fps : {30, 49, 60}) {
  const auto row = npc_semi_auto_rules::parse(f.row);
  check(row && std::bit_cast<float>(row->recoil_bits) == f.recoil && std::bit_cast<float>(row->warmup_bits) == f.warmup,
        "family row parses to its source timing");
  if (!row) continue;
  weapons = {*row}; source_warmup = f.warmup; source_continuous = f.continuous;
  field<float>(source_actor, 0x498) = 0; field<float>(source_actor, 0x49c) = 0;
  field<unsigned char>(source_actor, 0x54c) = 0; field<unsigned char>(source_actor, 0x336) = 0;
  field<void*>(pc_actor, 0x378) = move.data(); field<int>(move.data(), 0xbc) = 115;
  name_function = reinterpret_cast<resolve_name_t>(f.pistol ? &fixture_named_pistol : &fixture_named);
  if (f.pistol) {
   field<void*>(pistol_move.data(), 0xc8) = default_object.data(); field<int>(pistol_move.data(), 0xbc) = 115;
   field<void*>(pc_actor, 0x378) = pistol_move.data();
  }
  field<unsigned char>(pc_actor, 0x764) = 0; field<unsigned char>(pc_actor, 0x3d9) = 0;
  const u32 token = field<u32>(pc_actor, 0x1c);
  states[timer_slot(token)] = {};
  source_animation = 0x86;
  unsigned shots = 0, starts = 0, port_starts = 0; bool agree = true; int last_shot = -1;
  int min_gap = 1 << 30, max_gap = 0;
  float previous_warm = 0;
  for (int tick = 0; tick <= fps * 12; ++tick) {
   fixture_time = 100.f + float(tick) / fps;
   const bool held = !(tick >= fps * 5 && tick < fps * 5 + fps / 2); // release for half a second at 5 s
   field<u32>(input, 0) = held ? 0x90 : 0;
   requests = 0; requested_flags = 0; requested_name.clear(); source_warm_starts = 0;
   const bool original = reinterpret_cast<bool(__thiscall*)(void*, void*)>(code)(source_actor, input);
   const unsigned source_requests = requests; const u32 source_flags = requested_flags; const auto source_name = requested_name;
   if (source_requests && source_name == "power_attack" && !f.pistol) // 0xe14d0: entry + recoil
    field<float>(source_actor, 0x498) = fixture_time + source_recoil_value(f.recoil);
   starts += source_warm_starts;
   requests = 0; requested_flags = 0; requested_name.clear();
   const bool port = weapon_input(pc_actor, input); // its request fixture runs the production entry
   const auto* st = find(pc_actor, token);
   const float port_warm = st ? st->warm : 0.f, port_next = st ? st->next : 0.f;
   if (previous_warm == 0 && port_warm != 0) ++port_starts;
   previous_warm = port_warm;
   // The restored gate resolves power_attack by name before the warm-up (to check the converted
   // move); only names of actual requests are compared.
   agree &= original == port && source_requests == requests && source_flags == requested_flags &&
            (!source_requests || source_name == requested_name);
   agree &= std::bit_cast<u32>(field<float>(source_actor, 0x49c)) == std::bit_cast<u32>(port_warm);
   if (source_requests && source_name == "power_attack") {
    if (!f.pistol) agree &= std::bit_cast<u32>(field<float>(source_actor, 0x498)) == std::bit_cast<u32>(port_next);
    agree &= requested_flags == (f.continuous ? 1u : 9u);
    if (last_shot >= 0 && tick - last_shot > 0) { min_gap = std::min(min_gap, tick - last_shot); max_gap = std::max(max_gap, tick - last_shot); }
    last_shot = tick; ++shots;
   }
   if (!held) last_shot = -1;
  }
  check(agree, "executed original gate and restored gate agree frame by frame for every family");
  check(starts == port_starts, "warm-up starts agree");
  const float period = f.pistol ? 0.f : f.continuous ? f.recoil : f.recoil + f.warmup;
  const int frames = std::max(1, int(std::ceil(period * fps - 1e-3f)));
  check(shots > 0 && min_gap >= frames && max_gap <= frames + (f.warmup != 0 && !f.continuous ? 2 : 1), "shot gap is the recoil (+ per-shot warm-up) in whole frames");
  if (f.warmup != 0) check(f.continuous ? starts == 2 : starts >= shots, "continuous beams warm up once per hold, discrete ones before every shot");
  else check(starts == 0, "no warm-up without a source warm-up");
  std::printf("  %s @%d fps: %u shots, gaps %d..%d frames, %u warm-ups\n", f.name, fps, shots, min_gap, max_gap, starts);
 }
 weapons = saved; source_warmup = 0; source_continuous = false;
 name_function = reinterpret_cast<resolve_name_t>(&fixture_named);
 field<void*>(pc_actor, 0x378) = move.data();
}

void original_input_comparison(const char* path, void* pc_actor)
{
 using namespace npc_semi_auto;
 std::ifstream file(path, std::ios::binary);
 std::vector<unsigned char> raw((std::istreambuf_iterator<char>(file)), {});
 const auto word = [&raw](std::size_t at) { u32 value = 0; if (at+4 <= raw.size()) std::memcpy(&value,raw.data()+at,4); return value; };
 if (raw.size()<0x124 || std::memcmp(raw.data(),"XBEH",4)) {check(false,"input comparison XBE identity");return;}
 constexpr u32 address=0x42f60, size=0x205;
 const u32 image=word(0x104), sections=word(0x11c), table=word(0x120);
 if(table<image || sections>64) {check(false,"input comparison sections");return;}
 std::vector<unsigned char> body;
 for(u32 i=0;i<sections;++i) {
  const std::size_t s=table-image+0x38*i;
  const u32 va=word(s+4), offset=word(s+12), bytes=word(s+16);
  if(address<va || address-va+size>bytes || std::uint64_t(offset)+address-va+size>raw.size())continue;
  body.assign(raw.begin()+offset+address-va,raw.begin()+offset+address-va+size);break;
 }
 std::array<std::uint8_t,32> hash{};
 const char* expected="9e2f83a8a5d8bcd2bd4307a4c02610be31f97f5e06817f01b431ce326584cd9e";
 for(unsigned i=0;i<32;++i)hash[i]=limits_rules::hex_byte(expected,i);
 if(body.size()!=size || sha256_fips::digest(body.data(),body.size())!=hash) {check(false,"original input full identity");return;}
 auto* code=static_cast<unsigned char*>(VirtualAlloc(nullptr,size,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
 if(!code) {check(false,"original input allocation");return;}
 bool valid=true;
 for(const auto [offset,target,replacement]: {
  std::tuple<u32,u32,const void*>{0x13,0xbd520,&source_get_manager},
  {0x92,0x27ef0,&fixture_intern},{0x9e,0x38210,&fixture_named},{0xb3,0x40df0,&source_request},
  {0xb8,0x66ab0,&fixture_clock},{0xd0,0x66ab0,&fixture_clock},{0x122,0x66ab0,&fixture_clock},
  {0x157,0x66ab0,&fixture_clock},{0x178,0x27ef0,&fixture_intern},{0x184,0x38210,&fixture_named},
  {0x1d1,0x40df0,&source_request}}) {
  u32 relative=0;std::memcpy(&relative,body.data()+offset+1,4);
  valid &= body[offset]==0xe8 && address+offset+5+relative==target;
  relative=u32(reinterpret_cast<std::uintptr_t>(replacement))-u32(reinterpret_cast<std::uintptr_t>(code))-offset-5;
  std::memcpy(body.data()+offset+1,&relative,4);
 }
 // The original's own idle-transition delay, read from the owner's XBE (not substituted).
 float transition=-1;
 for(u32 i=0;i<sections;++i) {
  const std::size_t s=table-image+0x38*i;
  const u32 va=word(s+4), offset=word(s+12), bytes=word(s+16);
  if(0x3ca9e0<va || 0x3ca9e0-va+4>bytes || std::uint64_t(offset)+0x3ca9e0-va+4>raw.size())continue;
  std::memcpy(&transition,raw.data()+offset+0x3ca9e0-va,4);break;
 }
 check(transition==npc_semi_auto_rules::transition_delay,"restored idle transition delay equals the original constant");
 const float zero=0;
 for(const auto [offset,source,replacement]: {
  std::tuple<u32,u32,const void*>{0xf6,0x3c6de4,&zero},{0x105,0x3c6de4,&zero},{0x1e1,0x3c6de4,&zero},
  {0xbf,0x3ca9e0,&transition},{0x8a,0x3c974c,"idle"},{0x170,0x3c8cd8,"power_attack"}}) {
  u32 original=0;std::memcpy(&original,body.data()+offset,4);valid &= original==source;
  const u32 pointer=u32(reinterpret_cast<std::uintptr_t>(replacement));std::memcpy(body.data()+offset,&pointer,4);
 }
 check(valid,"original input dependency and constant references");
 if(valid) {
  std::memcpy(code,body.data(),size);DWORD old=0;
  valid=VirtualProtect(code,size,PAGE_EXECUTE_READ,&old)&&FlushInstructionCache(GetCurrentProcess(),code,size);
 }
 if(valid) {
  source_weapon_vtable[12]=reinterpret_cast<void*>(&source_category);
  source_prototype_vtable[2]=reinterpret_cast<void*>(&source_no_warmup);
  source_prototype_vtable[11]=reinterpret_cast<void*>(&source_not_continuous);
  source_prototype_vtable[13]=reinterpret_cast<void*>(&source_charge);
  source_actor_vtable[0x84/4]=reinterpret_cast<void*>(&source_start_warmup);
  std::array<void*,51> move_vtable{};move_vtable[50]=reinterpret_cast<void*>(&source_animation_getter);
  source_object source_move{move_vtable.data()};
  std::array<std::byte,0x600> actor{};std::array<std::byte,0x28> input{};
  field<void*>(actor.data(),0)=source_actor_vtable.data();
  field<u32>(actor.data(),0x3e0)=123;field<void*>(actor.data(),0x2f4)=&source_move;
  for(const unsigned held: {0u,0x10u,0x80u,0x90u})
   for(const unsigned locked: {0u,1u})
    for(const unsigned crouched: {0u,1u})
     for(const int animation: {0,21,0x86,0x8c,9})
      for(const float deadline: {99.f,100.f,101.f}) {
       fixture_time=100;source_animation=animation;requests=0;requested_animation=0;requested_flags=0;requested_name.clear();
       field<u32>(input.data(),0)=held;field<unsigned char>(actor.data(),0x54c)=static_cast<unsigned char>(locked);
       field<unsigned char>(actor.data(),0x336)=static_cast<unsigned char>(crouched);
       field<float>(actor.data(),0x498)=deadline;
       const bool original=reinterpret_cast<bool(__thiscall*)(void*,void*)>(code)(actor.data(),input.data());
       const unsigned source_requests=requests;const int source_result_animation=requested_animation;
       const u32 source_flags=requested_flags;const auto source_name=requested_name;
       requests=0;requested_animation=0;requested_flags=0;requested_name.clear();
       field<void*>(pc_actor,0x378)=move.data();field<int>(move.data(),0xbc)=animation==0x86?115:animation==0x8c?127:animation;
       field<unsigned char>(pc_actor,0x764)=static_cast<unsigned char>(locked);
       field<unsigned char>(pc_actor,0x3d9)=crouched?0x40:0;
       const u32 token=field<u32>(pc_actor,0x1c);
       states[timer_slot(token)]={reinterpret_cast<std::uintptr_t>(pc_actor),token,90.f,deadline};
       const bool port=weapon_input(pc_actor,input.data());
       check(original==port && source_requests==requests && source_flags==requested_flags && source_name==requested_name,
             "executed original and restored input decisions agree");
       if(requests)check((source_result_animation==0x86?115:source_result_animation==0x8c?127:source_result_animation)==requested_animation,
                         "requested animation maps to the same original animation");
       if(source_requests && source_name=="idle")check(field<float>(actor.data(),0x498)==find(pc_actor,token)->next,
                                                       "original and restored transition deadlines agree");
      }
  timeline_comparison(code,actor.data(),input.data(),pc_actor);
 }
 VirtualFree(code,0,MEM_RELEASE);
}
