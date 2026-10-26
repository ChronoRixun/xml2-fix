bool __cdecl noisy_pair(void* first, void* second)
{
 check(first == fixture_ai, "weapon bridge first argument");
 check(second == reinterpret_cast<void*>(std::uintptr_t(fixture_slot)), "weapon bridge second argument");
 __asm { fninit }
 __asm { fld1 }
 __asm { xorps xmm0, xmm0 }
 return forced_result != 0;
}
bool __cdecl noisy_one(void* first)
{
 check(first == fixture_ai, "weapon bridge AI argument");
 __asm { fninit }
 __asm { xorps xmm0, xmm0 }
 return forced_result != 0;
}
void __cdecl noisy_prepare(void* first) { noisy_one(first); }
bool __cdecl noisy_refresh(void* first, void* const* chains)
{ check(chains == fixture_chains_expected,"refresh bridge forwards its original resolved-action table"); return noisy_one(first); }
__declspec(naked) void fake_movement() { __asm { ret 4 } }
// The movement wrapper consumes one argument on either continuation.
u32 input_stack_before = 0, input_stack_after = 0;
__declspec(naked) void input_consumed_return()
{
 __asm {
  mov chosen_path, 1
  mov input_stack_after, esp
  pop edi
  pop esi
  pop ebx
  pop ebp
  ret
 }
}
__declspec(naked) void invoke_input_stack(void*, void*)
{
 __asm {
  push ebp
  push ebx
  push esi
  push edi
  mov input_stack_before, esp
  mov esi, [esp + 20]
  mov edi, [esp + 24]
  mov ecx, esi
  push edi
  call dword ptr [bridge]
  mov chosen_path, 0
  mov input_stack_after, esp
  pop edi
  pop esi
  pop ebx
  pop ebp
  ret
 }
}
int press_seen=0;
bool __cdecl noisy_press(int native,void* brain,void* input)
{
 press_seen=(native==0x78)+(u32(reinterpret_cast<std::uintptr_t>(input))==0x22334455)+(brain==fixture_ai);
 volatile double sink=1.25;for(int i=0;i!=3;++i)sink=sink*1.5;
 return forced_result!=0;
}
// Line-of-sight bridge (0x519e3a): EBP (brain) reaches the policy, every register, the flags
// and the FPU state survive, and the two displaced loads (EBX = [esp+0x70], EDX = [ebp])
// run before the native continuation.
u32 sight_ebx = 0, sight_edx = 0, sight_expected_ebx = 0;
std::array<std::uint32_t,9> sight_before{}, sight_after{};
void* sight_brain = nullptr;
void __cdecl noisy_sight(void* brain)
{
 check(brain == sight_brain, "sight bridge passes the brain in EBP");
 __asm { fninit }
 __asm { xorps xmm0, xmm0 }
}
__declspec(naked) void path_sight()
{
 __asm {
  mov sight_ebx, ebx
  mov sight_edx, edx
  push eax
  mov eax, [esp + 0x74]
  mov sight_expected_ebx, eax
  pop eax
  mov chosen_path, 1
  ret
 }
}
__declspec(naked) void invoke_sight(void*)
{
 __asm {
  push ebp
  push ebx
  push esi
  push edi
  mov ebp, [esp + 20]
  mov eax, 12345678h
  mov ebx, 22334455h
  mov ecx, 34567890h
  mov edx, 45678901h
  mov esi, 56789012h
  mov edi, 67890123h
  cld
  stc
  pushfd
  pushad
  mov esi, esp
  lea edi, sight_before
  mov ecx, 9
  rep movsd
  popad
  popfd
  fxsave fp_before
  call dword ptr [bridge]
  fxsave fp_after
  pushfd
  pushad
  mov esi, esp
  lea edi, sight_after
  mov ecx, 9
  rep movsd
  popad
  popfd
  pop edi
  pop esi
  pop ebx
  pop ebp
  ret
 }
}
void sight_bridge_cases()
{
 using namespace npc_semi_auto;
 std::array<std::byte,0x60> brain{};
 field<u32>(brain.data(),0)=0x6950f4;sight_brain=brain.data();
 sight_function=&noisy_sight;sight_resume=u32(reinterpret_cast<std::uintptr_t>(&path_sight));
 bridge=reinterpret_cast<void*>(&sight_hook);chosen_path=-1;
 __asm { fninit }
 __asm { fld1 }
 __asm { pcmpeqd xmm0, xmm0 }
 invoke_sight(brain.data());
 auto expected=sight_before;expected[4]=sight_expected_ebx;expected[5]=0x6950f4;
 check(chosen_path==1,"sight bridge resumes the native update");
 check(sight_ebx==sight_expected_ebx && sight_edx==0x6950f4,"sight bridge performs the displaced loads");
 check(sight_after==expected,"sight bridge preserves registers and flags except the displaced loads");
 check(fp_before==fp_after,"sight bridge preserves x87/SSE state");
 __asm { fninit }
}
void weapon_bridge_cases()
{
 sight_bridge_cases();
 using namespace npc_semi_auto;
 std::array<std::byte,0x800> ai{}, actor{}, brain{}, cursor{};
 field<void*>(ai.data(),0x10)=actor.data();field<void*>(brain.data(),4)=ai.data();
 field<void*>(cursor.data(),0x10)=actor.data();field<u32>(actor.data(),4)=0x12345678;
 field<u32>(ai.data(),0x4c4)=0x34567890;
 init_resume=u32(reinterpret_cast<std::uintptr_t>(&path_native));
 refresh_resume=init_resume;decision_resume=init_resume;
 refresh_done=u32(reinterpret_cast<std::uintptr_t>(&path_allow));decision_done=refresh_done;
 prepare_function=&noisy_prepare;preserve_function=&noisy_refresh;decision_function=&noisy_pair;input_function=&noisy_pair;
 for(unsigned which=0;which!=3;++which)for(int result:{0,1}) {
  forced_result=result;
  if(which==0){bridge=reinterpret_cast<void*>(&init_hook);fixture_ai=ai.data();}
  if(which==1){bridge=reinterpret_cast<void*>(&refresh_hook);fixture_ai=cursor.data();}
  if(which==2){bridge=reinterpret_cast<void*>(&decision_hook);fixture_ai=brain.data();fixture_slot=0x22334455;}
  __asm { fninit }
  __asm { fld1 }
  __asm { pcmpeqd xmm0, xmm0 }
  invoke(ai.data(),u32(reinterpret_cast<std::uintptr_t>(brain.data())),cursor.data());
  auto expected=before;
  if(which==1 && !result)expected[6]=u32(reinterpret_cast<std::uintptr_t>(actor.data()));
  if(which==2 && !result){expected[6]=u32(reinterpret_cast<std::uintptr_t>(ai.data()));expected[7]=u32(reinterpret_cast<std::uintptr_t>(actor.data()));}
  check(after==expected,"weapon bridge preserves registers except displaced native loads");
  check(fp_before==fp_after,"weapon bridge preserves x87/SSE state");
  check(chosen_path==(which==0 || !result?-1:1),"weapon bridge continuation");
  __asm { fninit }
 }
 // Power-decision result bridge: AL, brain (EBP) and input (EBX) reach the policy,
 // every register and the FPU state survive, and the continuation follows the native
 // brain+8 test exactly as 0x519feb does.
 power_false=u32(reinterpret_cast<std::uintptr_t>(&path_native));power_true=u32(reinterpret_cast<std::uintptr_t>(&path_allow));
 press_function=&noisy_press;bridge=reinterpret_cast<void*>(&power_hook);fixture_ai=brain.data();
 for(int brain_state:{-1,3})for(int result:{0,1}) {
  forced_result=result;field<int>(brain.data(),8)=brain_state;
  __asm { fninit }
  __asm { fld1 }
  __asm { pcmpeqd xmm0, xmm0 }
  invoke(ai.data(),u32(reinterpret_cast<std::uintptr_t>(brain.data())),cursor.data());
  auto expected=before; if(result)expected[8]=after[8];
  check(after==expected,"power bridge preserves registers");
  check(fp_before==fp_after,"power bridge preserves x87/SSE state");
  check(press_seen==3,"power bridge passes AL, brain and input");
  check(chosen_path==(result && brain_state==-1?1:-1),"power bridge continuation follows the native brain test");
  __asm { fninit }
 }
 native_movement=u32(reinterpret_cast<std::uintptr_t>(&fake_movement));
 input_done=u32(reinterpret_cast<std::uintptr_t>(&input_consumed_return));
 bridge=reinterpret_cast<void*>(&input_hook);fixture_ai=actor.data();fixture_slot=u32(reinterpret_cast<std::uintptr_t>(cursor.data()));
 for(int result:{0,1}) {
  forced_result=result;
  invoke_input_stack(actor.data(),cursor.data());
  check(chosen_path==result && input_stack_before==input_stack_after,"movement-call wrapper restores caller stack on both input paths");
 }
}
