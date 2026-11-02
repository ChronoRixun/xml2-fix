// Opt-in fault-DLL hooks only; never compiled into production dinput.dll.
#pragma once
#include <cstdlib>
#include <cstdio>
#include <cstring>
namespace w2_startup_faults {
static inline void snapshot(const char* name){
 const auto* folder=std::getenv("XML2_W2_SNAPSHOT_DIR");if(!folder)return;
 char path[MAX_PATH]{};std::snprintf(path,sizeof path,"%s/%s.bin",folder,name);FILE* file=nullptr;fopen_s(&file,path,"wb");
 if(file){std::fwrite(reinterpret_cast<const void*>(0x401000),1,0x700000,file);std::fclose(file);}
}
static inline bool selected(const char* name){const auto* value=std::getenv("XML2_W2_STARTUP_FAULT");return value&&std::strcmp(value,name)==0;}
static inline bool hit(const char* operation){FILE* f=nullptr;fopen_s(&f,"w2-fault-hit.txt","w");if(f){std::fputs(operation,f);std::fclose(f);}SetLastError(ERROR_ACCESS_DENIED);return false;}
static inline LPVOID WINAPI allocate(LPVOID a,SIZE_T n,DWORD t,DWORD p){if(selected("allocation")&&n==0x122190){hit("W2 save backing allocation");return nullptr;}return ::VirtualAlloc(a,n,t,p);}
static inline BOOL WINAPI protect(LPVOID a,SIZE_T n,DWORD p,PDWORD old){
 if(selected("protect")&&reinterpret_cast<std::uintptr_t>(a)==0x488000&&p==PAGE_EXECUTE_READWRITE)return hit("W2 transaction page made read-only by injected protection refusal");
 return ::VirtualProtect(a,n,p,old);
}
}
