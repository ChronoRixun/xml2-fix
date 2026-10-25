// Execute the owner's original input gate with synthetic dependencies. This
// complements live trials; no game code or data is embedded in this fixture.
int source_animation = 0;
int __fastcall source_category(void*, void*) { return 0; }
int __fastcall source_animation_getter(void*, void*) { return source_animation; }
float __fastcall source_no_warmup(void*, void*) { return 0; }
bool __fastcall source_not_continuous(void*, void*) { return false; }
void __fastcall source_request(void*, void*, void*, int animation, float duration, u32 flags, void*, void*)
{ ++requests; requested_animation = animation; requested_flags = flags; check(duration == -1, "original native duration"); }

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
 const float zero=0, transition=.05f;
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
  std::array<void*,51> move_vtable{};move_vtable[50]=reinterpret_cast<void*>(&source_animation_getter);
  source_object source_move{move_vtable.data()};
  std::array<std::byte,0x600> actor{};std::array<std::byte,0x28> input{};
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
 }
 VirtualFree(code,0,MEM_RELEASE);
}
