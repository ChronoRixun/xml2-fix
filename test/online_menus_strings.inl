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
unsigned clears=0;
unsigned gains=0;
void* find(const char* name) {
    if(std::strcmp(name,"lc_focus")==0) return focus_model.data();
    if(std::strcmp(name,"retry")==0) return action.data();
    return nullptr;
}
void __fastcall set_model(void* item,void*,const char* model,void*) {
    assert(item==focus_model.data() && std::strcmp(model,"ui/models/model_hide")==0);
    ++clears;put(focus_model.data(),0x98,2);
}
void __fastcall select(void* menu,void*,void* target) {
    assert(menu==child.data());
    put(menu,0x324,unsigned(reinterpret_cast<std::uintptr_t>(target)));
    if(target) {
        assert(target==action.data());action[0x54]|=1;
        put(focus_model.data(),0x98,1);++gains;
    } else action[0x54]&=~1;
}
void retry() {
    const auto tick=*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69e454);
    std::strcpy(reinterpret_cast<char*>(child.data()+0xc),mode=="r20-retry-code" ? "lc_code" : "lc_browse");
    set_current(child.data());scene_lookup=find;
    put(action.data(),0,0x6a0244);put(focus_model.data(),0,0x6a0304);
    redirect(0x5bbdd0,set_model);
    redirect(0x5aaf80,select);
    for(unsigned reason=0;reason<4;++reason) {
        action[0x54]=9;put(child.data(),0x324,unsigned(reinterpret_cast<std::uintptr_t>(action.data())));
        put(focus_model.data(),0x98,1);const auto start=clears;
        tick(child.data(),nullptr);assert(clears==start); // focused Retry retains amber
        if(reason==0) put(child.data(),0x324,0);        // native mouse-out
        if(reason==1) action[0x54]=8;                   // RIGHT removes native focus
        if(reason==2) action[0x54]=13;                  // broker hides the Retry item
        if(reason==3) action[0x54]=1;                   // disabled during backoff
        tick(child.data(),nullptr);assert(clears==start+1);
        tick(child.data(),nullptr);assert(clears==start+1); // no per-frame resource reload
        if(reason>=2) {
            action[0x54]=9; // shown/enabled again, same focus, no native gain
            const auto before=gains;
            tick(child.data(),nullptr);assert(gains==before+1 && *reinterpret_cast<unsigned*>(focus_model.data()+0x98)==1);
            tick(child.data(),nullptr);assert(gains==before+1); // no repeated focus refresh
        }
    }
    scene_lookup=nullptr;
    std::cout<<"Retry focus lifecycle PASS mouse-out / RIGHT / hidden / disabled / shown or enabled again; only focused actions retain amber\n";
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
