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
void* lock_actor=nullptr;
bool __cdecl noisy_lock(void* actor)
{
 check(actor==lock_actor,"lock bridge passes the actor in EAX");
 volatile double sink=1.25;for(int i=0;i!=3;++i)sink=sink*1.5;
 return forced_result!=0;
}
std::array<std::uint32_t,9> lock_before{},lock_after{};
__declspec(naked) void invoke_lock(void*)
{
 __asm {
  push ebp
  push ebx
  push esi
  push edi
  mov eax, [esp + 20]
  mov ebx, 22334455h
  mov ecx, 34567890h
  mov edx, 45678901h
  mov esi, 56789012h
  mov edi, 67890123h
  mov ebp, 78901234h
  pushad
  mov esi, esp
  lea edi, lock_before
  mov ecx, 8
  rep movsd
  popad
  fxsave fp_before
  call dword ptr [bridge]
  fxsave fp_after
  pushad
  mov esi, esp
  lea edi, lock_after
  mov ecx, 8
  rep movsd
  popad
  pop edi
  pop esi
  pop ebx
  pop ebp
  ret
 }
}
void weapon_bridge_cases()
{
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
 // Input-lock exit bridge: listed actors always resume the update, others keep the
 // native 0x3d9 bit test; registers and FPU state survive.
 lock_resume=u32(reinterpret_cast<std::uintptr_t>(&path_allow));lock_return=u32(reinterpret_cast<std::uintptr_t>(&path_deny));
 lock_function=&noisy_lock;bridge=reinterpret_cast<void*>(&lock_hook);
 for(int flagged:{0,1})for(int result:{0,1}) {
  forced_result=result;field<unsigned char>(actor.data(),0x3d9)=flagged?0x10:0;lock_actor=actor.data();
  __asm { fninit }
  __asm { fld1 }
  __asm { pcmpeqd xmm0, xmm0 }
  invoke_lock(actor.data());
  check(lock_after==lock_before,"lock bridge preserves registers");
  check(fp_before==fp_after,"lock bridge preserves x87/SSE state");
  check(chosen_path==(result || !flagged?1:0),"lock bridge continuation");
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
