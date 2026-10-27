// Compile the production call-site wrappers themselves in this test TU.
// Only their native services and unrelated module APIs are fixture dependencies.
#include "../src/forced_teams.cpp"
#include "../src/conversations.cpp"
#include <cstdio>
#include <stdexcept>
#include <filesystem>
extern "C" unsigned char fixture_space[0x700000];
namespace fixture {bool on=false;unsigned calls=0;std::string fault;void* object=nullptr;}
namespace ini {
    bool flag(const wchar_t*,const wchar_t*,bool){return fixture::on;}
    int number(const wchar_t*,const wchar_t*,int fallback){return fallback;}
    std::optional<std::string> text(const wchar_t*,const wchar_t*){return std::nullopt;}
}
namespace logger {
    void write(const char*,...){}
    void write_once(std::string_view,const char*,...){}
    const std::filesystem::path& module_dir(){static const std::filesystem::path p=".";return p;}
}
namespace entity_capacity {[[noreturn]] void fail_stop_lifecycle(const char*){ExitProcess(fixture::calls==1?77:91);}}
namespace solo_mode {bool installed(){return false;}void script_request(void*,bool,const char*){}}
namespace join_in_progress {void note_party_change(){}}
namespace xtract {int set_services(int){return 0;}}
namespace script_functions {int capacity(){return 320;}std::span<const script_functions_rules::func_entry> probes(){return {};}}
// 0.4.2 tree: forced_teams.cpp also asks W2's save layout for its guard bytes and W3's ScriptPools for the value makers;
// neither is installed in this fixture, so both answer as their inactive modules do (retail bytes, stock layout).
namespace save_limits {std::optional<std::string> expected(unsigned,std::string_view hex){return std::string(hex);}}
namespace script_pools {bool value_makers_match(){return true;}}
namespace {
using u32=std::uint32_t;
u32 addr(const void* p){return static_cast<u32>(reinterpret_cast<std::uintptr_t>(p));}
void put(u32 at,u32 v){std::memcpy(reinterpret_cast<void*>(at),&v,4);}
void jump(u32 at,const void* target){*reinterpret_cast<unsigned char*>(at)=0xe9;put(at+1,addr(target)-at-5);}
void fault(){++fixture::calls;if(fixture::fault=="cpp")throw std::runtime_error("real native wrapper fault");if(fixture::fault=="seh")RaiseException(0xe0420042,0,0,nullptr);}
void* __cdecl get_object(){return fixture::object;}
bool __fastcall native_bool(void*,void*,const char*){fault();return true;}
void __fastcall native_void(void*,void*){fault();}
void __fastcall native_pick(void*,void*,int index){if(index!=0)ExitProcess(92);fault();}
void* outer_work(forced_teams::game_engine&,void*){fault();return reinterpret_cast<void*>(0x1234);}
int child(const std::string& site,const std::string& mode,bool on){
    fixture::on=on;fixture::fault=mode;e4_observe::configure();
    DWORD old=0;if(!VirtualProtect(fixture_space,0x700000,PAGE_EXECUTE_READWRITE,&old))return 90;
    std::vector<unsigned char> object(0x23a00);fixture::object=mode=="probe"?reinterpret_cast<void*>(1):object.data();
    bool completed=false,value=false;
    if(site=="outer"){
        if(mode=="cpp")fixture::fault="cpp";else fixture::fault="ok";
        void* result=forced_teams::handler<&outer_work,'v'>(nullptr);
        return fixture::calls==1?(mode=="cpp"?(result==nullptr?42:93):(result==reinterpret_cast<void*>(0x1234)?0:93)):94;
    }
    if(site=="add"){
        using namespace forced_teams_rules;put(addr(object.data()),game_vtable);put(game_vtable+game_add_hero_slot,addr(reinterpret_cast<void*>(&native_bool)));
        jump(game_getter,reinterpret_cast<void*>(&get_object));completed=forced_teams::call_add_hero("hero",value);
    }else if(site=="console"||site=="queue"){
        using namespace forced_teams_rules;put(addr(object.data()),console_vtable);put(console_vtable+console_run_slot,addr(reinterpret_cast<void*>(&native_bool)));put(console_vtable+console_queue_slot,addr(reinterpret_cast<void*>(&native_bool)));
        jump(console_getter,reinterpret_cast<void*>(&get_object));completed=forced_teams::call_console(site=="queue"?console_queue_slot:console_run_slot,"command",value);
    }else if(site=="hud"){
        using namespace forced_teams_rules;put(addr(object.data()),hud_vtable);put(hud_vtable+hud_leave_slot,addr(reinterpret_cast<void*>(&native_void)));
        jump(hud_getter,reinterpret_cast<void*>(&get_object));completed=forced_teams::call_leave_hud();value=true;
    }else if(site=="pick"){
        std::array<u32,16> table{};table[conversations_rules::cs_pick_slot/4]=addr(reinterpret_cast<void*>(&native_pick));
        put(addr(object.data()),mode=="probe"?1:addr(table.data()));put(addr(object.data())+conversations_rules::cs_voice,conversations_rules::none_handle);
        completed=conversations::advance(object.data());value=true;
    }else return 95;
    if(mode=="probe")return !completed&&fixture::calls==0?42:96;
    if(mode=="ok")return completed&&value&&fixture::calls==1?0:97;
    return !completed&&fixture::calls==1?42:98;
}
}
int main(int argc,char** argv){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    if(argc==5)return child(argv[2],argv[3],std::string(argv[4])=="on");
    unsigned count=0,failed=0;
    for(const auto* site:{"add","console","queue","hud","pick","outer"})for(const auto* mode:{"seh","cpp","probe","ok"})for(bool on:{false,true}){
        if(std::string(site)=="outer"&&(std::string(mode)=="seh"||std::string(mode)=="probe"))continue;
        char command[4096]{};std::snprintf(command,sizeof(command),"\"%s\" child %s %s %s",argv[0],site,mode,on?"on":"off");
        DWORD expected=std::string(mode)=="ok"?0:std::string(mode)=="probe"||!on||std::string(site)=="queue"?42:77;
        STARTUPINFOA si{};si.cb=sizeof(si);PROCESS_INFORMATION pi{};
        if(!CreateProcessA(nullptr,command,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){++failed;++count;continue;}
        if(WaitForSingleObject(pi.hProcess,15000)!=WAIT_OBJECT_0){TerminateProcess(pi.hProcess,99);WaitForSingleObject(pi.hProcess,5000);}
        DWORD code=0;GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hThread);CloseHandle(pi.hProcess);++count;
        if(code!=expected){++failed;std::printf("FAIL real X1 %s %s ON=%u: %lu expected %lu\n",site,mode,on,code,expected);}
    }
    std::printf("real X1 call-site wrappers: %u checks, %u failures\n",count,failed);return failed?1:0;
}
