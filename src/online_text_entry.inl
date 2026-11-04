// LC uses menu context 1 (normal pad bindings) plus Windows-translated text.
// Pad/mouse feed that same lane: native tags, charset, limits and results win.
namespace text_entry_ui {
constexpr int first=100,last=140;
struct state { void* owner=nullptr;int page=0,pending=-1,tag=4,focus=0;unsigned held=0;const char* notice=nullptr;DWORD notice_at=0;int attempted_length=-1; } edit;
online_text_entry_rules::inbox input;
void reset_input() { input.clear();edit.pending=-1;edit.held=0;edit.notice=nullptr; }
bool live(void* menu) {
    __try {
        return ready && menu && *static_cast<unsigned*>(menu)==0x69f06c &&
            !std::strcmp(static_cast<const char*>(menu)+0xc,"lc_text_entry");
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool keyboard(void* menu,unsigned char* state,const std::array<bool,256>& edges) {
    if(!live(menu)) return false;
    if(edit.owner!=menu) return true;
    if(keyboard_note) for(unsigned scan=0;scan<256;++scan) if(edges[scan]) { keyboard_note();break; }
    const bool control=(state[0x1d]|state[0x9d]|state[0x38]|state[0xb8])&0x80;
    for(unsigned scan=0;scan<256;++scan) if(const int value=online_text_entry_rules::key(scan)) {
        if(!control && edges[scan]) {
            if(value==27) { input.clear();edit.pending=40; }
            else input.push_back(value);
        }
    }
    // Normal menu bindings include printable/numpad keys. None may also run a
    // grid action; WM_CHAR alone supplies their layout-derived characters.
    std::memset(state,0,256);
    return true;
}
const char* characters() {
    if(edit.tag==3) return "1234567890.";
    if(edit.page==1) return "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    if(edit.page==2) return "<>,.-";
    return "abcdefghijklmnopqrstuvwxyz0123456789";
}
int request_tag() {
    __try { return at<int(__thiscall*)(void*)>(0x60b9a0)(at<getter>(0x60c7e0)()); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 4; }
}
bool capture(void* menu,int action) {
    if(action<first || action>last) return false;
    // Consume exact constants even after their source closes; no delayed FIFO
    // command can type into a later editor at the same cached object address.
    if(live(menu) && edit.owner==menu && request_tag()==edit.tag && edit.pending<0)
        edit.pending=action-first;
    return true;
}
void bind(void* menu) {
    const auto* keys=characters();const auto count=std::strlen(keys);
    for(int i=0;i<40;++i) {
        char key[32]{},value[16]{};std::snprintf(key,sizeof(key),"text_inputkeys%02d",i+1);
        bool enabled=true;
        if(i<36) {
            enabled=unsigned(i)<count;
            if(enabled) { value[0]=keys[i];value[1]=0; }
        } else {
            const char* label=i==36 ? "Backspace" : i==37 ? "Space" : i==38 ?
                (edit.page==0 ? "ABC" : edit.page==1 ? "Symbols" : "abc") : "Done";
            std::snprintf(value,sizeof(value),"%s",label);
            enabled=edit.tag!=3 || i==36 || i==39;
        }
        // These controls use the native mouse handler, which checks their
        // interned +0x9c token as well as the getter. Publish through retail's
        // setter; a DATA-only owned-text binding would leave blank keys inert.
        bindings.scope(menu);
        const auto previous=bindings.text.find(key);
        if(previous==bindings.text.end() || previous->second!=value) {
            at<set_text_fn>(0x5adcf0)(menu,key,"%s",value);
            bindings.text.insert_or_assign(key,value);
        }
        enable(menu,key,enabled);hide(menu,key,!enabled);
        std::snprintf(key,sizeof(key),"lc_text_key_bg%02d",i+1);
        hide(menu,key,!enabled);
    }
}
void focus(void* menu,int key) {
    char name[32]{};
    if(key==40) std::snprintf(name,sizeof(name),"lc_text_cancel");
    else std::snprintf(name,sizeof(name),"text_inputkeys%02d",key+1);
    lobby_focus(menu,name);edit.focus=key;
}
void navigate(void* menu,int direction) {
    focus(menu,online_text_entry_rules::move(edit.focus,direction,int(std::strlen(characters())),edit.tag==3));
}
void sync_focus(void* menu) {
    __try {
        auto* selected=*reinterpret_cast<void**>(static_cast<char*>(menu)+0x324);
        for(int i=0;i<=40;++i) {
            char name[32]{};
            if(i==40) std::snprintf(name,sizeof(name),"lc_text_cancel");
            else std::snprintf(name,sizeof(name),"text_inputkeys%02d",i+1);
            if(selected && selected==item(menu,name)) { edit.focus=i;break; }
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
}
bool native_tick(void* menu) {
    __try { at<tick_fn>(0x5e72f0)(menu);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void notify(const char* message) { edit.notice=message;edit.notice_at=now_ms(); }
void character(unsigned value,unsigned scan) {
    if(const int translated=online_text_entry_rules::character(value,scan,edit.tag==3))
        if(!input.push_back(translated)) notify("Input full");
}
int native_length(void* menu) {
    __try {
        const auto* text=static_cast<const char*>(menu)+0x1c10;
        const auto* end=static_cast<const char*>(std::memchr(text,0,256));
        return end ? int(end-text) : -1;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int native_limit(void* menu) {
    __try { return *reinterpret_cast<int*>(static_cast<char*>(menu)+0x1d10); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
void truncate_seed(void* menu) {
    const int length=native_length(menu),limit=std::clamp(native_limit(menu),0,255);
    if(length<=limit) return;
    auto* buffer=static_cast<char*>(menu)+0x1c10;
    const auto clipped=online_status_core::utf8_clean(std::string(buffer,length),std::size_t(limit));
    std::memcpy(buffer,clipped.c_str(),clipped.size()+1);
    at<void(__thiscall*)(void*,bool)>(0x5e6fc0)(menu,false);
    // The native cancellation snapshot stays intact: Cancel restores the
    // caller's original name, whereas Done accepts the shortened edit buffer.
}
bool inject(unsigned char character) {
    __try {
        auto* request=at<getter>(0x60c7e0)();
        if(at<bool(__thiscall*)(void*)>(0x60b960)(request)) return true;
        auto& input=*at<unsigned char*>(0xa0a061);
        if(input) return false;
        input=character;
        if(character>=32) edit.attempted_length=native_length(edit.owner);
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
}
void __fastcall tick(void* menu,void*) {
    if(!live(menu)) { at<tick_fn>(0x5e72f0)(menu);return; }
    if(!input_thread("text entry",false)) return;
    bool native_called=false;
    try {
        if(edit.owner!=menu) { edit={menu,0,-1,request_tag()};input.clear(); }
        truncate_seed(menu);
        edit.attempted_length=-1;
        sync_focus(menu);
        const auto raw=at<unsigned(__thiscall*)(void*)>(0x5d4b00)(manager());
        const auto edge=raw&~edit.held;edit.held=raw;
        const int pad_action=online_text_entry_rules::pad(edge);
        if(pad_action==27) { input.clear();edit.pending=40; }
        else if(pad_action && (pad_action!=online_text_entry_rules::accept || edit.pending<0)) input.push_back(pad_action);
        if(edit.pending<0 && !input.empty()) {
            const int value=input.front();input.pop_front();
            if(value==online_text_entry_rules::accept) edit.pending=edit.focus;
            else if(value==online_text_entry_rules::unsupported) notify("Unsupported");
            else if(value>=online_text_entry_rules::left) navigate(menu,value);
            else if(!inject(static_cast<unsigned char>(value))) input.push_front(value);
        }
        if(edit.pending>=0) {
            const int action=edit.pending;
            if(request_tag()!=edit.tag || current()!=menu) edit.pending=-1;
            else if(action==38 && edit.tag!=3) { edit.page=(edit.page+1)%3;edit.pending=-1; }
            else {
                const auto* keys=characters();unsigned char character=0;
                if(action<36 && unsigned(action)<std::strlen(keys)) character=keys[action];
                if(action==36) character=8;
                if(action==37 && edit.tag!=3) character=32;
                if(action==39) character=13;
                if(action==40) character=27;
                if(!character || inject(character)) edit.pending=-1;
            }
        }
        bind(menu);
        if(!online_text_entry_rules::enabled(edit.focus,int(std::strlen(characters())),edit.tag==3)) focus(menu,0);
        // Retail rejects length==limit only. Guard >= as well, including the
        // native character lane used by tests/other input producers.
        auto& character=*at<unsigned char*>(0xa0a061);
        if(character>=32 && native_length(menu)>=native_limit(menu)) { character=0;notify("Text limit"); }
        text_entry_ticking=true;
        native_called=true;const bool ok=native_tick(menu);text_entry_ticking=false;
        if(!ok) {edit={};input.clear();}
        else if(current()==menu) {
            if(edit.attempted_length>=0) {
                if(native_length(menu)<=edit.attempted_length)
                    notify(edit.attempted_length>=native_limit(menu) ? "Text limit" : "Unsupported");
            }
            if(edit.notice && DWORD(now_ms()-edit.notice_at)>=3000) edit.notice=nullptr;
            const bool pad=pad_probe && pad_probe(menu_player);
            const char* prompts[]{pad ? "B Cancel" : "Esc Cancel","Backspace Delete",pad ? "D-pad Move" : "Arrows Move",edit.notice ? edit.notice : "",pad ? "A Select" : "Enter Select"};
            for(unsigned i=0;i<5;++i) {
                const auto key="desctext"+std::to_string(i+1);bindings.text.erase(key);
                bind_text(menu,key.c_str(),prompts[i]);
            }
        }
    } catch(...) { text_entry_ticking=false;edit={};input.clear();if(!native_called) native_tick(menu); }
}
void __fastcall activate(void* menu,void*,int argument) {
    input_thread("text entry activation",false);
    edit={};input.clear();bindings.clear();
    at<void(__thiscall*)(void*,int)>(0x5e6ec0)(menu,argument);
    if(live(menu)) {
        try { edit={menu,0,-1,request_tag()};truncate_seed(menu);bind(menu);
            edit.held=at<unsigned(__thiscall*)(void*)>(0x5d4b00)(manager());focus(menu,0); }
        catch(...) { edit={}; } // native keyboard/result service remains available
    }
}
void __fastcall deactivate(void* menu,void*) {
    if(edit.owner==menu) { edit={};input.clear(); }
    at<tick_fn>(0x5af410)(menu);
}
// Keep the native name selectors intact: text_entry remains context 2;
// lc_text_entry is an ordinary menu (context 1), with the normal pad map.
void contribute(patch_transaction::contribution& c) {
    c.writes.push_back(patch_transaction::relocation(0x69f07c,"",false,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&activate)),0,"online text keyboard activation"));
    c.writes.push_back(patch_transaction::relocation(0x69f080,"",false,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&deactivate)),0,"online text keyboard retirement"));
    c.writes.push_back(patch_transaction::relocation(0x69f0a4,"",false,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&tick)),0,"online text keyboard native character lane"));
}
}
