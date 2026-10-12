// Load the production DLL against a synthetic Win32 image, never a game or real save.
// A rejected guard must fail DLL initialization before the simulated game can start.
#include <winsock2.h>
#include <Windows.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <share.h>
#include <string>
#include <vector>
#include "save_format_rules.hpp"
#include "save_core_rules.hpp"
#include "game_version_rules.hpp"

// Keep every guarded address inside this fixture's image. The bytes are generated
// from public interoperability signatures, not copied from a game executable.
volatile unsigned char fixture_space[0x800000];
auto* volatile socket_import = &socket; // exercise the real LC network startup gate
struct notice_context
{
 DWORD pid=0;
 bool seen=false, informative=false;
 bool capacity_seen=false; // the non-fatal entity-capacity fallback notice (case -14 only)
};
BOOL CALLBACK read_notice_text(HWND window, LPARAM parameter)
{
 auto& context=*reinterpret_cast<notice_context*>(parameter);
 char text[2048]{};DWORD_PTR result=0;
 SendMessageTimeoutA(window,WM_GETTEXT,sizeof text,reinterpret_cast<LPARAM>(text),SMTO_ABORTIFHUNG,100,&result);
 if(std::strstr(text,"not started") && std::strstr(text,"LC")) context.informative=true;
 return TRUE;
}
BOOL CALLBACK close_owned_notice(HWND window, LPARAM parameter)
{
 auto& context=*reinterpret_cast<notice_context*>(parameter);
 DWORD pid=0;GetWindowThreadProcessId(window,&pid);
 char title[256]{};GetWindowTextA(window,title,sizeof title);
 if(pid==context.pid && std::strcmp(title,"XML2 Fix: entity capacity unavailable")==0)
 {
  context.capacity_seen=true;
  const auto button=GetDlgItem(window,IDOK);DWORD_PTR result=0;
  SendMessageTimeoutW(button,BM_CLICK,0,0,SMTO_ABORTIFHUNG,250,&result);
  PostMessageW(window,WM_COMMAND,MAKEWPARAM(IDOK,BN_CLICKED),reinterpret_cast<LPARAM>(button));
  PostMessageW(window,WM_CLOSE,0,0);
  return TRUE;
 }
 if(pid==context.pid && std::strcmp(title,"XML2 Fix cannot start")==0)
 {
  context.seen=true;
  EnumChildWindows(window,read_notice_text,parameter);
  if(!context.informative) return TRUE; // wait until the dialog has initialized its text
  const auto button=GetDlgItem(window,IDOK);DWORD_PTR result=0;
  SendMessageTimeoutW(button,BM_CLICK,0,0,SMTO_ABORTIFHUNG,250,&result);
  PostMessageW(window,WM_COMMAND,MAKEWPARAM(IDOK,BN_CLICKED),reinterpret_cast<LPARAM>(button));
  PostMessageW(window,WM_CLOSE,0,0);
 }
 return TRUE;
}
std::vector<limits_rules::guard> save_guards()
{
 std::vector<limits_rules::guard> out(save_format_rules::guards.begin(),save_format_rules::guards.end());
 for(const auto& site:save_format_rules::flag_sites) out.push_back({site.va,site.hex,site.what});
 out.insert(out.end(),save_core_rules::guards.begin(),save_core_rules::guards.end());
 out.push_back(save_core_rules::ordinal_write_guard); return out;
}
void seed(const limits_rules::guard& guard)
{
 const auto size=limits_rules::hex_size(guard.hex);
 auto* address=reinterpret_cast<unsigned char*>(guard.va);
 DWORD before=0; if(!VirtualProtect(address,size,PAGE_EXECUTE_READWRITE,&before)) std::exit(90);
 for(std::size_t i=0;i<size;++i) address[i]=limits_rules::hex_byte(guard.hex,i);
 DWORD ignored=0; if(!VirtualProtect(address,size,before,&ignored)) std::exit(90);
}
int child(int mutation)
{
 fixture_space[0]=1; // keep the synthetic image padding in the linked executable
 if(reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr))!=0x400000) return 91;
 const auto guards=save_guards();
 const bool fault=mutation<=-4 && mutation!=-13 && mutation!=-14; // -14: entity capacity on a non-retail image
 if(mutation==-13) SetEnvironmentVariableA("XML2_SAVE_STARTUP_FAULT","0");
 if(fault) {DeleteFileA("startup-fault-hit.txt");SetEnvironmentVariableA("XML2_SAVE_STARTUP_FAULT",std::to_string(mutation).c_str());}
 for(const auto& g:guards) seed(g);
 for(const auto& g:game_version_rules::guards) seed(g);
 if(const auto* mismatch=save_format_rules::first_mismatch(reinterpret_cast<const unsigned char*>(0x400000)))
 { std::printf("fixture mismatch %lx %s\n",mismatch->va,mismatch->what);return 92; }
 if(mutation>=0)
 {
  const auto& g=guards.at(static_cast<std::size_t>(mutation));
  auto* byte=reinterpret_cast<unsigned char*>(g.va);DWORD before=0;
  if(!VirtualProtect(byte,1,PAGE_EXECUTE_READWRITE,&before)) return 93;
  *byte^=1;DWORD ignored=0;if(!VirtualProtect(byte,1,before,&ignored)) return 94;
 }
 const char* format=mutation==-2 ? "off" : mutation==-3 ? "broken" : "lc";
 WritePrivateProfileStringA("Save","Format",format,".\\xml2-fix.ini");
 WritePrivateProfileStringA("Online","GameVersion","X1.0",".\\xml2-fix.ini");
 WritePrivateProfileStringA("Discord","Enabled","0",".\\xml2-fix.ini");
 // -14: [Limits] EntityCapacity=1 where the executable's entity code is not the ledger's (this synthetic image): the
 // capacity install must fall back to stock at preparation and the LC seam must still install (plan step 6).
 WritePrivateProfileStringA("Limits","EntityCapacity",mutation==-14?"1":"0",".\\xml2-fix.ini");
 SetLastError(0); const auto module=LoadLibraryW((fault || mutation==-13)?L".\\save-startup-faults.dll":L".\\dinput.dll"); const auto error=GetLastError();
 const bool expected_refusal=mutation>=0 || (mutation<=-3 && mutation!=-13 && mutation!=-14);
 if(fault)
 {
  FILE* marker=nullptr;fopen_s(&marker,"startup-fault-hit.txt","r");
  if(!marker) {std::printf("FAIL case %d: fault was not exercised\n",mutation);return 1;}
  char evidence[256]{};std::fgets(evidence,sizeof evidence,marker);std::fclose(marker);
  std::printf("fault case %d: %s",mutation,evidence);
  if(mutation==-12 && !std::strstr(evidence,"flushed=3")) return 1;
  unsigned changed=0;
  for(const auto& g:guards)
   if(!limits_rules::matches(reinterpret_cast<const unsigned char*>(g.va),g.hex)) ++changed;
  // The startup transaction rolls every written byte back to the expected bytes after a flush, readback or
  // protection-restore failure (plan section 7), so no fault case may leave a changed guard site behind.
  std::printf("fault case %d: changed guard sites=%u\n",mutation,changed);
  if(changed!=0) return 1;
 }
 if(expected_refusal)
 {
  if(module)
  {
   std::printf("FAIL case %d: DLL loaded; simulated retail startup/save path is still active\n",mutation);
   return 1;
  }
  if(error!=ERROR_DLL_INIT_FAILED)
  {
   std::printf("FAIL case %d: loader did not report DLL initialization refusal (error=%lu)\n",mutation,error);
   return 1;
  }
  std::printf("PASS case %d: loader refused startup, error=%lu\n",mutation,error);
  // A statically imported game would stop in the loader. This dynamic fixture
  // must not resume CRT teardown through any earlier IAT hooks in the rejected DLL.
  TerminateProcess(GetCurrentProcess(),0);
 }
 else
 {
  if(!module)
  {
   std::printf("FAIL valid/off case %d: error=%lu\n",mutation,error);return 1;
  }
  if(mutation==-14)
  {
   FILE* log=_fsopen("xml2-fix.log","rb",_SH_DENYNO); /* the DLL keeps its log open */std::string text;
   if(log){char buffer[4096];std::size_t n=0;while((n=std::fread(buffer,1,sizeof buffer,log))>0)text.append(buffer,n);std::fclose(log);}
   const auto from=text.rfind("XML2 Fix ");const auto tail=from==std::string::npos?text:text.substr(from);
   const bool fell_back=tail.find("EntityCapacity falls back to stock capacities")!=std::string::npos;
   const bool lc=tail.find("save format: LC")!=std::string::npos && tail.find("startup transaction: committed")!=std::string::npos;
   const bool told=tail.find("startup: entity capacity notice closed")!=std::string::npos;
   if(!fell_back || !lc || !told){std::printf("FAIL case -14: capacity fallback %d, LC seam committed %d, notice %d\n",fell_back,lc,told);return 1;}
   std::printf("PASS case -14: capacity-only mismatch falls back to stock with its notice; the LC seam installs in the committed transaction\n");
   return 0;
  }
  std::printf("PASS case %d: valid or disabled LC startup allowed\n",mutation);
 }
 // No native engine methods, network calls or save-file operations are run.
 return 0;
}
int main(int argc,char** argv)
{
 std::setvbuf(stdout,nullptr,_IONBF,0);
 if(argc==3 && std::strcmp(argv[1],"--case")==0) return child(std::atoi(argv[2]));
 char exe[MAX_PATH]{};GetModuleFileNameA(nullptr,exe,sizeof exe);
 // Keep refusal dialogs off the user's desktop; only the owning test parent observes/closes them.
 const auto desktop_name=L"xml2-save-startup-"+std::to_wstring(GetCurrentProcessId());
 const auto desktop=CreateDesktopW(desktop_name.c_str(),nullptr,nullptr,0,GENERIC_ALL,nullptr);
 if(!desktop) {std::printf("cannot create isolated test desktop: %lu\n",GetLastError());return 95;}
 const auto original_desktop=GetThreadDesktop(GetCurrentThreadId());
 if(!SetThreadDesktop(desktop)){CloseDesktop(desktop);std::printf("cannot attach test desktop: %lu\n",GetLastError());return 97;}
 int failed=0,cases=0;
 const bool single=argc==3 && std::strcmp(argv[1],"--single")==0;
 const int first=single?std::atoi(argv[2]):-14;
 const int end=single?first+1:static_cast<int>(save_guards().size());
 for(int i=first;i<end;++i)
 {
  std::wstring path;for(const char* c=exe;*c;++c)path.push_back(static_cast<unsigned char>(*c));
  auto command=L"\""+path+L"\" --case "+std::to_wstring(i);
  STARTUPINFOW startup{};startup.cb=sizeof startup;
  startup.lpDesktop=const_cast<wchar_t*>(desktop_name.c_str());
  startup.dwFlags=STARTF_USESTDHANDLES|STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
  startup.hStdInput=GetStdHandle(STD_INPUT_HANDLE);startup.hStdOutput=GetStdHandle(STD_OUTPUT_HANDLE);startup.hStdError=GetStdHandle(STD_ERROR_HANDLE);
  PROCESS_INFORMATION process{};++cases;
  if(!CreateProcessW(path.c_str(),command.data(),nullptr,nullptr,TRUE,0,nullptr,nullptr,&startup,&process))
  {++failed;std::printf("case %d CreateProcess failed: %lu\n",i,GetLastError());continue;}
  notice_context notice;notice.pid=process.dwProcessId;
  const auto deadline=GetTickCount64()+15000;DWORD exit=1;
  while(WaitForSingleObject(process.hProcess,25)==WAIT_TIMEOUT && GetTickCount64()<deadline)
   EnumDesktopWindows(desktop,close_owned_notice,reinterpret_cast<LPARAM>(&notice));
  if(WaitForSingleObject(process.hProcess,0)==WAIT_OBJECT_0)GetExitCodeProcess(process.hProcess,&exit);
  else {TerminateProcess(process.hProcess,96);WaitForSingleObject(process.hProcess,5000);std::printf("case %d timed out (notice=%d text=%d)\n",i,notice.seen,notice.informative);}
  const bool must_refuse=i>=0 || (i<=-3 && i!=-13 && i!=-14);
  if(must_refuse && (!notice.seen || !notice.informative))
  {exit=1;std::printf("FAIL case %d: missing clear LC refusal notice (seen=%d informative=%d)\n",i,notice.seen,notice.informative);}
  if(!must_refuse && notice.seen){exit=1;std::printf("FAIL case %d: unexpected refusal notice\n",i);}
  // A capacity-only fallback starts the game and tells the player once; no other case shows that notice.
  if((i==-14)!=notice.capacity_seen){exit=1;std::printf("FAIL case %d: entity capacity fallback notice %s\n",i,notice.capacity_seen?"shown unexpectedly":"missing");}
  CloseHandle(process.hThread);CloseHandle(process.hProcess);if(exit!=0)++failed;
 }
 SetThreadDesktop(original_desktop);
 CloseDesktop(desktop);
 std::printf("save startup: %d cases, %d failures\n",cases,failed);return failed?1:0;
}
