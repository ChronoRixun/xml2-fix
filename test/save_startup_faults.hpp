// Test-only wrappers, compiled solely into the opt-in fault DLL, never dinput.dll.
#pragma once
namespace startup_faults
{
 inline int mode() { const auto* value=std::getenv("XML2_SAVE_STARTUP_FAULT"); return value?std::atoi(value):0; }
 inline unsigned writable=0, restored=0, flushed=0;
 inline void* stubs=nullptr;
 inline bool rollback=false;
 inline bool fail(const char* operation)
 {
  FILE* file=nullptr; fopen_s(&file,"startup-fault-hit.txt","w");
  if(file) {std::fprintf(file,"%s writable=%u restored=%u flushed=%u\n",operation,writable,restored,flushed);std::fclose(file);}
  SetLastError(ERROR_ACCESS_DENIED);return false;
 }
 inline LPVOID WINAPI allocate(LPVOID address,SIZE_T size,DWORD type,DWORD protection)
 {
  if(mode()==-4) {fail("allocation");return nullptr;}
  stubs=::VirtualAlloc(address,size,type,protection);return stubs;
 }
 inline BOOL WINAPI protect(LPVOID address,SIZE_T size,DWORD protection,PDWORD old)
 {
  if(address==stubs && mode()==-5) return fail("stub-protection");
  if(address!=stubs)
  {
   if(!rollback && flushed==0)
   {
    ++writable;
    if((mode()==-6 && writable==1) || ((mode()==-7 || mode()==-8) && writable==3)) {rollback=true;return fail("make-writable");}
   }
   else
   {
    ++restored;
    if((mode()==-8 || mode()==-9) && restored==1) return fail("restore-protection");
   }
  }
  return ::VirtualProtect(address,size,protection,old);
 }
 inline BOOL WINAPI flush(HANDLE process,LPCVOID address,SIZE_T size)
 {
  if(address==stubs && mode()==-10) return fail("stub-cache-flush");
  if(address!=stubs)
  {
   ++flushed;
   if((mode()==-11 && flushed==1) || (mode()==-12 && flushed==3)) return fail("patch-cache-flush");
  }
  return ::FlushInstructionCache(process,address,size);
 }
}
#define VirtualAlloc startup_faults::allocate
#define VirtualProtect startup_faults::protect
#define FlushInstructionCache startup_faults::flush
