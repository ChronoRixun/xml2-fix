// Real retail frontend string constructor/interner, with invented strings only.
namespace frontend_strings_fixture {
std::array<unsigned char,0x11008> pool{};
void* __cdecl get_pool() { return pool.data(); }
void reset() { pool.fill(0);put(pool.data(),4,1);put(pool.data(),0x1004,2); }
unsigned token(const std::string& value) {
    unsigned id=0;
    reinterpret_cast<void(__fastcall*)(void*,void*,const char*)>(0x5ab780)(&id,nullptr,value.c_str());
    return id;
}
const char* value(unsigned id) { return reinterpret_cast<const char*>(pool.data()+0x1008+*reinterpret_cast<unsigned*>(pool.data()+4+(id&0xffffff)*4)); }
std::vector<std::string> menu_names() {
    std::vector<std::string> names;
    if(const char* path=std::getenv("XML2_MENU_STRINGS")) {
        std::ifstream in(path);assert(in);
        for(std::string line;std::getline(in,line);) if(!line.empty()) names.push_back(line);
    } else {
        // Standalone native suite: invented names only. The paired LC gate
        // supplies the serialized production Code names through the env var.
        for(unsigned i=0;i<80;++i) {
            names.push_back("fixture_item_"+std::to_string(i));
            names.push_back("ui/models/lc_o_fixture_focus_"+std::to_string(i));
        }
    }
    assert(names.size()>100);return names;
}
std::vector<unsigned> retail_ids;
std::array<unsigned char,0x2c08> retail_pool{};
std::string parity_name(unsigned i) { return "native_"+std::to_string(i)+std::string(i%37,'x'); }
void parity_before() {
    reset();put(pool.data(),0,0x12);
    const auto add=reinterpret_cast<unsigned(__fastcall*)(void*,void*,const char*,unsigned,unsigned)>(0x41a460);
    for(unsigned i=0;i<100;++i) { const auto s=parity_name(i);retail_ids.push_back(add(pool.data(),nullptr,s.c_str(),unsigned(s.size()+1),2)); }
    std::copy_n(pool.begin(),retail_pool.size(),retail_pool.begin());
}
void parity_after() {
    reset();put(pool.data(),0,0x12);
    const auto add=reinterpret_cast<unsigned(__fastcall*)(void*,void*,const char*,unsigned,unsigned)>(0x41a460);
    for(unsigned repeat=0;repeat<2;++repeat) for(unsigned i=0;i<100;++i) {
        const auto s=parity_name(i);assert(retail_ids[i]==add(pool.data(),nullptr,s.c_str(),unsigned(s.size()+1),2));
    }
    assert(std::equal(retail_pool.begin(),retail_pool.end(),pool.begin()));
    std::cout<<"Frontend string parity PASS: retail hash, collisions, generation/slot handles and complete original backing identical\n";
}
unsigned requested_size=0;
void* __cdecl allocate(unsigned size,unsigned owner,unsigned line) { requested_size=size;assert(owner==14 && line==36);return pool.data(); }
int __cdecl at_exit(void(*)()) { return 0; }
void allocation() {
    pool.fill(0xcc);*reinterpret_cast<unsigned*>(0xa0a81c)=0;
    redirect(0x5605e0,allocate);redirect(0x401d50,at_exit);
    assert(reinterpret_cast<void*(__cdecl*)()>(0x602200)()==pool.data());
    assert(requested_size==pool.size() && *reinterpret_cast<unsigned*>(pool.data()+0x1004)==2);
    assert(std::string(value(token("first-name")))=="first-name");
    std::cout<<"Frontend string native allocation PASS: "<<requested_size<<" bytes, native initialization and first insertion\n";
}
void run() {
    reset();redirect(0x602200,get_pool);
    // Fill the original 7 KiB arena with names representative of previously
    // opened native pages, then open Code using the actual authored strings.
    for(unsigned i=0;i<80;++i) token("prior_menu_"+std::to_string(i)+std::string(40,'p'));
    const auto names=menu_names();
    std::map<std::string,unsigned> ids;
    for(unsigned visit=0;visit<12;++visit) {
        for(const auto& name:names) {
            const auto id=token(name);
            if(!id || name!=value(id)) {
                std::cout<<"frontend string corruption: visit="<<visit<<" used="<<*reinterpret_cast<unsigned*>(pool.data()+0x1004)<<" name="<<name<<"\n";
                assert(false);
            }
            if(visit) assert(ids.at(name)==id);else ids.emplace(name,id);
        }
        // These are the first visible DATA bindings in a one-room Browse.
        for(const auto* text:{"1 public game","Only room","1/4","1 last known game"}) assert(std::string(value(token(text)))==text);
    }
    std::cout<<"Code open/close string lifecycle PASS: 12 visits, "<<names.size()<<" authored names, used="<<*reinterpret_cast<unsigned*>(pool.data()+0x1004)<<" bytes; stable handles and Browse DATA roundtrips\n";
}
HANDLE main_thread=nullptr,done=nullptr;
DWORD WINAPI spin_watch(void*) {
    if(WaitForSingleObject(done,1200)==WAIT_OBJECT_0) return 0;
    assert(SuspendThread(main_thread)!=DWORD(-1));CONTEXT ctx{};ctx.ContextFlags=CONTEXT_CONTROL;
    assert(GetThreadContext(main_thread,&ctx));
    std::cout<<"frontend probe spin at "<<std::hex<<ctx.Eip<<" (retail loop 419db4..419de0)\n";
    ExitProcess(95);
}
void full() {
    reset();redirect(0x602200,get_pool);
    // Occupied slots with no matching string: retail's wrap has no stop.
    for(unsigned i=0;i<1024;++i) put(pool.data(),4+i*4,1);
    assert(DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&main_thread,0,FALSE,DUPLICATE_SAME_ACCESS));
    done=CreateEventW(nullptr,TRUE,FALSE,nullptr);assert(done);
    HANDLE watcher=CreateThread(nullptr,0,spin_watch,nullptr,0,nullptr);assert(watcher);
    const auto snapshot=pool;
    assert(token("missing-name")==0);
    assert(pool==snapshot); // refusal publishes nothing, including arena usage
    SetEvent(done);assert(WaitForSingleObject(watcher,3000)==WAIT_OBJECT_0);
    CloseHandle(watcher);CloseHandle(done);CloseHandle(main_thread);
    std::cout<<"frontend full-table refusal PASS: bounded, no publication\n";
}
void arena_full() {
    reset();redirect(0x602200,get_pool);put(pool.data(),0x1004,0xfff0);
    const auto snapshot=pool;
    assert(token(std::string(32,'z'))==0 && pool==snapshot);
    std::cout<<"frontend full-arena refusal PASS: no offset or usage published\n";
}
void code_spin() {
    reset();redirect(0x602200,get_pool);
    for(unsigned i=0;i<80;++i) token("prior_menu_"+std::to_string(i)+std::string(40,'p'));
    const auto names=menu_names();
    assert(DuplicateHandle(GetCurrentProcess(),GetCurrentThread(),GetCurrentProcess(),&main_thread,0,FALSE,DUPLICATE_SAME_ACCESS));
    done=CreateEventW(nullptr,TRUE,FALSE,nullptr);assert(done);
    HANDLE watcher=CreateThread(nullptr,0,spin_watch,nullptr,0,nullptr);assert(watcher);
    for(unsigned visit=0;visit<80;++visit) {
        for(const auto& name:names) token(name);
        unsigned occupied=0;for(unsigned slot=0;slot<1024;++slot) occupied+=*reinterpret_cast<unsigned*>(pool.data()+4+slot*4)!=0;
        std::cout<<"Code strings visit="<<visit<<" used="<<*reinterpret_cast<unsigned*>(pool.data()+0x1004)<<" occupied="<<occupied<<"\n";
    }
    SetEvent(done);assert(WaitForSingleObject(watcher,3000)==WAIT_OBJECT_0);
    CloseHandle(watcher);CloseHandle(done);CloseHandle(main_thread);
    std::cout<<"Code authored-name probe lifecycle PASS, 80 visits without spin\n";
}
std::array<unsigned char,0xb0> action{},focus_model{};
std::array<unsigned,64> action_vt{};
std::array<unsigned,2> amber_model{},hidden_model{};
unsigned clears=0,gains=0;
void* find(const char* name) {
    if(std::strcmp(name,"lc_focus")==0) return focus_model.data();
    if(std::strcmp(name,"retry")==0) return action.data();
    return nullptr;
}
float __fastcall fixed_model_clock(void*,void*) { return 42.125f; }
void __fastcall load_focus_model(void*,void*,void** out,const char* name,int) {
    const bool hidden=std::strstr(name,"model_hide") || std::strstr(name,"m_invis");
    *out=hidden ? hidden_model.data() : amber_model.data();if(hidden) ++clears;
}
bool __fastcall attach_focus_model(void* owner,void*,const char*,void*,void** old) {
    assert(owner==child.data());*old=*reinterpret_cast<void**>(focus_model.data()+0x58);return true;
}
void __cdecl release_focus_model(void**) {}
void by_name(const char* name) {
    reinterpret_cast<void(__fastcall*)(void*,void*,const char*,void*)>(0x5bbdd0)(focus_model.data(),nullptr,name,nullptr);
}
void __fastcall focus_event(void* item,void*,bool gain) {
    assert(item==action.data());
    if(gain) { action[0x54]|=1;++gains;by_name("ui/models/lc_o_fixture_amber"); }
    else action[0x54]&=~1;
}
void select(void* target) { reinterpret_cast<void(__fastcall*)(void*,void*,void*)>(0x5aaf80)(child.data(),nullptr,target); }
void retry() {
    const auto tick=*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69e454);
    std::strcpy(reinterpret_cast<char*>(child.data()+0xc),mode=="r20-retry-code" ? "lc_code" : "lc_browse");
    set_current(child.data());scene_lookup=find;
    std::copy_n(reinterpret_cast<unsigned*>(0x6a10cc),action_vt.size(),action_vt.begin());
    action_vt[0x4c/4]=unsigned(reinterpret_cast<std::uintptr_t>(focus_event));
    put(action.data(),0,unsigned(reinterpret_cast<std::uintptr_t>(action_vt.data())));
    put(focus_model.data(),0,0x6a0304);put(focus_model.data(),4,unsigned(reinterpret_cast<std::uintptr_t>(child.data())));
    // Only external resource/clock/layout services are invented. Execute the
    // retail 5bbdd0 -> 5c4c50 -> 5bc3f0 setters and 5aaf80 focus transitions.
    put(reinterpret_cast<void*>(0x6a236c),0x24,unsigned(reinterpret_cast<std::uintptr_t>(fixed_model_clock)));
    put(reinterpret_cast<void*>(0x6a236c),0x260,unsigned(reinterpret_cast<std::uintptr_t>(load_focus_model)));
    redirect(0x5ab720,attach_focus_model);redirect(0x575980,release_focus_model);
    redirect(0x5bc530,anchor_fixture::noop);
    redirect(*reinterpret_cast<unsigned*>(0x69e41c+0x68),anchor_fixture::noop);
    const auto model=[] { return *reinterpret_cast<void**>(focus_model.data()+0x58); };
    const auto time=[] { return *reinterpret_cast<float*>(focus_model.data()+0x98); };
    for(unsigned reason=0;reason<4;++reason) {
        action[0x54]=8;select(action.data());const auto start=clears;
        tick(child.data(),nullptr);assert(clears==start && model()==amber_model.data());
        if(reason<2) select(nullptr);
        if(reason==2) action[0x54]=13;
        if(reason==3) action[0x54]=1;
        tick(child.data(),nullptr);assert(clears==start+1 && model()==hidden_model.data());
        tick(child.data(),nullptr);assert(clears==start+1);
        // A real gain AND loss between cleanup ticks changes +58, while the
        // retail timestamp at +98 stays exactly the same under this clock.
        action[0x54]=8;select(nullptr);select(action.data());select(nullptr);
        assert(model()==amber_model.data() && time()==42.125f);
        tick(child.data(),nullptr);
        assert(model()==hidden_model.data() && clears==start+2 && time()==42.125f);
        if(reason>=2) {
            action[0x54]=9;put(child.data(),0x324,unsigned(reinterpret_cast<std::uintptr_t>(action.data())));
            const auto before=gains;tick(child.data(),nullptr);
            assert(gains==before+1 && model()==amber_model.data());
            tick(child.data(),nullptr);assert(gains==before+1);
        }
        select(nullptr);
    }
    scene_lookup=nullptr;
    std::cout<<"Retry retail lifecycle PASS: fixed clock, +58 model identity, gain/loss between ticks, hidden/disabled recovery\n";
}
void open_close() {
    const auto tick=*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69e454);
    const auto close=*reinterpret_cast<unsigned(__fastcall**)(void*,void*)>(0x69e464);
    for(unsigned visit=0;visit<12;++visit) {
        std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_code");child[4]=1;set_current(child.data());
        tick(child.data(),nullptr);assert(texts["code_value"]=="____-____");
        std::array<unsigned char,256> keys{};online_menus::keyboard(keys.data());
        keys[0x1e]=128;online_menus::keyboard(keys.data());tick(child.data(),nullptr);
        assert(texts["code_value"]=="A___-____");
        close(child.data(),nullptr);set_current(hub.data());tick(hub.data(),nullptr);
    }
    std::cout<<"Code open/close input lifecycle PASS: 12 visits, keyboard reads continue and edit state resets\n";
}
}
