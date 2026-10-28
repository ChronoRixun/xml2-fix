#include "e4_observe.hpp"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <stdexcept>

namespace { bool opt_in=false;unsigned logger_fault=0; }
namespace ini { bool flag(const wchar_t*,const wchar_t*,bool) { return opt_in; } }
namespace logger { void write(const char*,...) {if(logger_fault==1)throw std::runtime_error("diagnostic logging fault");if(logger_fault==2)RaiseException(0xe0420043,0,0,nullptr);} }
namespace entity_capacity {
    [[noreturn]] void fail_stop_lifecycle(const char*) { ExitProcess(77); }
}
namespace {
    constexpr DWORD injected=0xe0420042;
    void fault() { RaiseException(injected,0,0,nullptr); }
    // A stand-in for the original broad recovery scope at each audited site.
    // Its data read must still recover, including when opt-in is active.
    bool guarded(unsigned kind, bool data_fault) {
        __try {
            if(data_fault) fault();
            if(kind==5){fault();return false;} // audited SAFE queue-insertion class
            switch(kind) {
            case 0: return e4_observe::native_call("bool native",[]{ fault(); return true; });
            case 1: e4_observe::native_call("void native",[]{ fault(); }); break;
            case 2: return e4_observe::native_call("pointer native",[]{ fault(); return static_cast<void*>(nullptr); })!=nullptr;
            case 3: return e4_observe::native_call("integer native",[]{ fault(); return 1; })!=0;
            default: e4_observe::native_call("compound admission",[]{ fault(); }); break;
            }
            return true;
        } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
    }
    void cpp_native() { throw std::runtime_error("injected"); }
    int cpp_handler(bool outer=false) {
        try { if(outer) cpp_native(); else e4_observe::native_call("C++ native",[]{cpp_native();}); }
        catch(...) { if(e4_observe::enabled()) e4_observe::fatal("outer C++ barrier"); return 42; }
        return 0;
    }
    DWORD child(const char* exe,unsigned kind,const char* mode) {
        char command[2048]{};
        std::snprintf(command,sizeof(command),"\"%s\" child %u %s",exe,kind,mode);
        STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
        if(!CreateProcessA(nullptr,command,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)) return 255;
        WaitForSingleObject(pi.hProcess,15000);DWORD code=255;GetExitCodeProcess(pi.hProcess,&code);
        if(code==STILL_ACTIVE){TerminateProcess(pi.hProcess,254);WaitForSingleObject(pi.hProcess,15000);code=254;}
        CloseHandle(pi.hThread);CloseHandle(pi.hProcess);return code;
    }
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc==5) {
        opt_in=std::string(argv[4])!="off";e4_observe::configure();
        unsigned kind=std::strtoul(argv[2],nullptr,10);
        if(std::string(argv[3])=="cpp") return cpp_handler();
        if(std::string(argv[3])=="outer") return cpp_handler(true);
        if(std::string(argv[3])=="log")logger_fault=1;
        if(std::string(argv[3])=="logseh")logger_fault=2;
        return guarded(kind,std::string(argv[3])=="probe")?1:42;
    }
    unsigned checks=0,failed=0;
    for(unsigned kind=0;kind<5;++kind) {
        for(const auto& mode:{"native on","native off","probe on","probe off"}) {
            const DWORD expected=std::string(mode)=="native on"?77:42;
            auto result=child(argv[0],kind,mode);++checks;
            if(result!=expected){++failed;std::printf("FAIL class %u %s: %lu != %lu\n",kind,mode,result,expected);}
        }
    }
    for(const auto& mode:{"queue on","queue off"}){++checks;if(child(argv[0],5,mode)!=42)++failed;}
    for(const auto& mode:{"cpp on","cpp off","outer on","outer off","log on","log off","logseh on","logseh off"}) {
        DWORD expected=std::string(mode).ends_with(" on")?77:42;
        ++checks;if(child(argv[0],0,mode)!=expected)++failed;
    }
    std::printf("X1 native fault/probe/C++: %u checks, %u failures\n",checks,failed);
    return failed?1:0;
}
