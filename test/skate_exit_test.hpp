// Included by shared_handlers_test.cpp with its synthetic fixture helpers.
// Removing either production continuation hook must fail on physical state.
u32 continuation_site=0,continuation_before=0,continuation_after=0;
u32 continuation_regs[5]{};
void* continuation_actor=nullptr;
void* continuation_handler=nullptr;
void* continuation_result=nullptr;
unsigned continuation_requests=0,continuation_exits=0;
float continuation_fp=0,continuation_sse=0,continuation_seed=1.25f;
void* __fastcall continuation_resolve(void* self,void*,u32 id) {
 check(self==continuation_actor && id==0x1e,"both skating paths resolve samepowerhold with actor this");
 ++continuation_requests;return continuation_result;
}
void __fastcall continuation_refresh(void*,void*) { ++continuation_exits; }
__declspec(naked) void drive_continuation() {
 __asm {
  pushad
  mov continuation_before,esp
  mov ebp,0x11223344
  mov ebx,0x22334455
  mov esi,continuation_actor
  mov edi,continuation_handler
  mov ecx,esi
  fld1
  movss xmm0,continuation_seed
  push 0x1234
  push 0x5678
  push offset returned
  push ebx
  push 0x33445566 // caller's ESI differs from the actor held in live ESI
  push 0x44556677 // caller's EDI differs from the handler held in live EDI
  jmp dword ptr [continuation_site]
 returned:
  mov continuation_after,esp
  mov continuation_regs[0],eax
  mov continuation_regs[4],ebx
  mov continuation_regs[8],esi
  mov continuation_regs[12],edi
  mov continuation_regs[16],ebp
  fstp continuation_fp
  movss continuation_sse,xmm0
  popad
  ret
 }
}
void skating_continuation_tests() {
 std::array<void*,4> handler_vt{};
 handler_vt[3]=reinterpret_cast<void*>(0x4ecfb0);
 void** handler=handler_vt.data();
 void** other=handler_vt.data(); // distinct object, even with the same vtable
 std::array<unsigned char,0x100> next{};
 continuation_actor=actor.data();continuation_handler=&handler;
 jump(0x427c80,&continuation_resolve);jump(0x45fe40,&continuation_refresh);
 for(const u32 site:{0x4eca0au,0x4eca77u})for(const int outcome:{0,1,2}) {
  reset_actor();field<u32>(actor.data(),4)=0xa5;
  actor[0x765]=0x14;field<float>(actor.data(),0x278)=.5f;
  field<float>(actor.data(),0x434)=.75f;field<float>(actor.data(),0x798)=9.f;
  field<void*>(actor.data(),0x3bc)=reinterpret_cast<void*>(0x12345678);
  field<void*>(next.data(),0xc8)=outcome==1?static_cast<void*>(&handler):static_cast<void*>(&other);
  continuation_result=outcome==0?nullptr:next.data();
  auto expected=actor;
  field<void*>(expected.data(),0x3bc)=continuation_result;
  if(outcome!=1) {
   field<u32>(expected.data(),4)=0xa1;expected[0x765]=0x10;
   field<float>(expected.data(),0x278)=1.f;field<float>(expected.data(),0x798)=0.f;
  }
  continuation_requests=continuation_exits=0;continuation_site=site;drive_continuation();
  check(actor==expected,"leaving skating clears traversal state via native exit; self-loop keeps it exactly");
  check(continuation_exits==(outcome==1?0u:1u),"exit runs once for null/other handler and never for self-loop");
  check(continuation_requests==1 && field<void*>(actor.data(),0x3bc)==continuation_result,"resolved next move survives exit dispatch");
  check(continuation_before==continuation_after && (continuation_regs[0]&255)==1,"both continuation tails retain ret 8 and true AL");
  check(continuation_regs[1]==0x22334455 && continuation_regs[2]==0x33445566 &&
        continuation_regs[3]==0x44556677 && continuation_regs[4]==0x11223344,
        "continuation preserves EBX/ESI/EDI/EBP");
  check(continuation_fp==1.f && continuation_sse==1.25f,"continuation retains live x87/SSE values");
 }
 jump(0x427c80,&resolve_action);jump(0x45fe40,&refresh);
}
