// Actual CMenu formatted DATA setter, CText setter/getter and frontend pool.
namespace data_text_fixture {
online_browser_core::client_view snapshot;
online_menus::broker_api api;
std::array<std::array<unsigned char,0xb0>,3> fields{};
std::array<unsigned,3> names{};
std::atomic<unsigned> phase{0};
DWORD WINAPI watch(void*) {
    using namespace frontend_strings_fixture;
    if(WaitForSingleObject(done,30000)==WAIT_OBJECT_0) return 0;
    assert(SuspendThread(main_thread)!=DWORD(-1));CONTEXT ctx{};ctx.ContextFlags=CONTEXT_FULL;
    assert(GetThreadContext(main_thread,&ctx));
    std::cout<<"DATA stalled phase="<<phase.load()<<" eip="<<std::hex<<ctx.Eip<<" eax="<<ctx.Eax<<" ecx="<<ctx.Ecx<<" edx="<<ctx.Edx<<"\n";
    ExitProcess(94);
}
bool available() { return true; }
const online_browser_core::client_view& view() { return snapshot; }
void noop() {}
bool yes() { return true; }
bool no() { return false; }
std::string version() { return "FIXTURE"; }
void prepare() {
    snapshot={};snapshot.authenticated=snapshot.list_known=true;
    snapshot.rows.push_back({});auto& room=snapshot.rows[0];room.code="AAAAAAAB";room.name="First room";room.players=1;room.version="FIXTURE";
    api.available=available;api.view=view;api.tick=noop;api.list=yes;api.version=version;api.refresh_due=no;
}
void* __fastcall item(void*,void*,const char* name) {
    if(std::strcmp(name,"room_0_0")==0) return fields[0].data();
    if(std::strcmp(name,"code_value")==0) return fields[1].data();
    return nullptr;
}
void __fastcall focus(void* menu,void*,const char* name,bool) {
    // This DATA test has no parsed onfocus registry. Focus event execution is
    // covered independently by the real-setter fixed-clock Retry fixture.
    put(menu,0x324,unsigned(reinterpret_cast<std::uintptr_t>(item(menu,nullptr,name))));
}
unsigned __fastcall index(void*,void*,const unsigned* key,unsigned) {
    for(unsigned i=0;i<names.size();++i) if(*key==names[i]) return i;
    return 0x3fffffff;
}
int __cdecl format(char* out,unsigned count,const char* fmt,va_list args) { return std::vsnprintf(out,count,fmt,args); }
void* __cdecl cast(void* value,int,void*,void*,int) { return value; }
const char* __cdecl find_char(const char* text,int ch) { return std::strchr(text,ch); }
const char* shown(unsigned i) {
    return reinterpret_cast<const char*(__fastcall*)(void*,void*)>((*reinterpret_cast<unsigned**>(fields[i].data()))[0x7c/4])(fields[i].data(),nullptr);
}
unsigned occupied() {
    unsigned count=0;for(unsigned i=0;i<1024;++i) count+=*reinterpret_cast<unsigned*>(frontend_strings_fixture::pool.data()+4+i*4)!=0;
    return count;
}
void run(const script_functions_rules::func_entry* registration) {
    using namespace frontend_strings_fixture;
    assert(DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&main_thread,0,FALSE,DUPLICATE_SAME_ACCESS));
    done=CreateEventW(nullptr,TRUE,FALSE,nullptr);assert(done);HANDLE watcher=CreateThread(nullptr,0,watch,nullptr,0,nullptr);assert(watcher);
    reset();redirect(0x602200,get_pool);
    names={token("room_0_0"),token("code_value"),token("native_pressure")};
    for(unsigned i=0;i<fields.size();++i) {
        put(fields[i].data(),0,0x6a10cc);put(fields[i].data(),4,unsigned(reinterpret_cast<std::uintptr_t>(child.data())));put(fields[i].data(),8,names[i]);
        put(child.data(),0x160c+i*4,unsigned(reinterpret_cast<std::uintptr_t>(fields[i].data())));
    }
    put(child.data(),0x828,3);set_current(child.data());
    restore_retail(0x5adcf0,0xd0);
    redirect(0x5adc10,item);redirect(0x5adf40,focus);redirect(0x5ab7d0,index);redirect(0x560080,format);redirect(0x67234e,cast);redirect(0x672161,anchor_fixture::noop);
    redirect(0x6723da,find_char);
    const auto tick=*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69e454);
    std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_browse");tick(child.data(),nullptr);
    assert(std::string(shown(0))=="First room");
    const char* room_pointer=shown(0);
    const auto native_set=reinterpret_cast<void(__fastcall*)(void*,void*,const char*)>(0x5c6850);
    native_set(fields[2].data(),nullptr,"$MENU_BACK Leave");assert(std::string(shown(2))=="$MENU_BACK Leave");
    const auto prompt_slots=occupied();put(fields[2].data(),0xa0,1);*reinterpret_cast<unsigned*>(0x8add9c)|=2;
    native_set(fields[2].data(),nullptr,"native gamevar");assert(std::string(shown(2))=="native gamevar" && occupied()==prompt_slots);
    put(fields[2].data(),0xa0,0);
    for(unsigned i=0;i<1200;++i) { const auto value="Native "+std::to_string(i);native_set(fields[2].data(),nullptr,value.c_str()); }
    assert(occupied()==1024);
    assert(once_logs.contains("online:frontend-strings-table full"));
    std::cout<<once_logs.at("online:frontend-strings-table full")<<"\n";
    snapshot.rows[0].name="Fresh room at full";tick(child.data(),nullptr);
    assert(std::string(shown(0))==snapshot.rows[0].name);
    const auto cached_room=snapshot.rows[0];snapshot.rows.clear();tick(child.data(),nullptr);
    assert(!shown(0)[0] && !*reinterpret_cast<unsigned*>(fields[0].data()+0x9c));
    snapshot.rows.push_back(cached_room);tick(child.data(),nullptr);
    phase=1;
    const auto base=occupied();
    for(unsigned i=0;i<2200;++i) {
        phase=100+i;
        snapshot.rows[0].name="Room "+std::to_string(i);tick(child.data(),nullptr);
        assert(std::string(shown(0))==snapshot.rows[0].name);
    }
    // Code's hidden aggregate is DATA too. Change it through the production
    // intent/input path; only its eight one-character cells are interned.
    const auto post=[&](unsigned value) {
        int_value[1]=value;reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr);tick(child.data(),nullptr);
    };
    std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_code");tick(child.data(),nullptr);
    phase=2;
    const char* code_pointer=shown(1);
    for(unsigned i=0;i<1200;++i) {
        phase=10000+i;
        post(unsigned(intent::clear));std::string expected;
        for(unsigned j=0;j<8;++j) { const unsigned symbol=j<4 ? (i>>(j*3))&7 : 0;expected+=char('A'+symbol);post(32+symbol); }
        expected.insert(4,"-");assert(std::string(shown(1))==expected);
    }
    assert(occupied()==base && shown(0)==room_pointer && shown(1)==code_pointer);
    // Fill every native slot through the real CText setter, then bind a new
    // room name through the real CMenu DATA setter and the production bridge.
    assert(occupied()==1024);
    std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_browse");snapshot.rows[0].name="Fresh room at full";tick(child.data(),nullptr);
    assert(std::string(shown(0))==snapshot.rows[0].name);
    std::cout<<"DATA token actual="<<*reinterpret_cast<unsigned*>(fields[0].data()+0x9c)<<" name="<<names[0]<<" stock="<<reinterpret_cast<const char*(__fastcall*)(void*,void*)>(0x5c67f0)(fields[0].data(),nullptr)<<"\n";
    assert(*reinterpret_cast<unsigned*>(fields[0].data()+0x9c)==names[0]);
    const auto close=*reinterpret_cast<unsigned(__fastcall**)(void*,void*)>(0x69e464);close(child.data(),nullptr);
    assert(std::string(shown(0))=="room_0_0"); // buffer was released at owner close, native fallback remains
    SetEvent(done);assert(WaitForSingleObject(watcher,3000)==WAIT_OBJECT_0);CloseHandle(watcher);CloseHandle(done);CloseHandle(main_thread);
    std::cout<<"DATA lifetime PASS: 2200 room names + 1200 codes, stable buffers/slot count; real setter exhausts 1024 slots; fresh room still shown; close releases ownership\n";
}
}
