// PC retail retains the 40 keyboard widgets but only implements WM_CHAR.
// Pad/mouse feed that same lane: native tags, charset, limits and results win.
namespace text_entry_ui {
constexpr int first=100,last=140;
struct state { void* owner=nullptr;int page=0,pending=-1,tag=4; } edit;
bool live(void* menu) {
    __try {
        return ready && menu && *static_cast<unsigned*>(menu)==0x69f06c &&
            !std::strcmp(static_cast<const char*>(menu)+0xc,"lc_text_entry");
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
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
    }
}
bool native_tick(void* menu) {
    __try { at<tick_fn>(0x5e72f0)(menu);return true; }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool inject(unsigned char character) {
    __try {
        auto* request=at<getter>(0x60c7e0)();
        if(at<bool(__thiscall*)(void*)>(0x60b960)(request)) return true;
        auto& input=*at<unsigned char*>(0xa0a061);
        if(input) return false;
        input=character;return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return true; }
}
void __fastcall tick(void* menu,void*) {
    if(!live(menu)) { at<tick_fn>(0x5e72f0)(menu);return; }
    try {
        if(edit.owner!=menu) edit={menu,0,-1,request_tag()};
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
        if(!native_tick(menu)) edit={};
    } catch(...) { edit={};native_tick(menu); }
}
void __fastcall activate(void* menu,void*,int argument) {
    edit={};bindings.clear();
    at<void(__thiscall*)(void*,int)>(0x5e6ec0)(menu,argument);
    if(live(menu)) {
        try { edit={menu,0,-1,request_tag()};bind(menu); }
        catch(...) { edit={}; } // native keyboard/result service remains available
    }
}
void __fastcall deactivate(void* menu,void*) {
    if(edit.owner==menu) edit={};
    at<tick_fn>(0x5af410)(menu);
}
// The PC input-context selectors recognize text entry by name rather than by
// native class. Preserve their context 2 for both definitions (keyboard chars
// otherwise become ordinary menu bindings). Every other context stays native.
bool __cdecl editor_name(const char* name) {
    return name && (!std::strcmp(name,"text_entry") || !std::strcmp(name,"lc_text_entry"));
}
const unsigned context_one_yes=0x61c36c,context_one_no=0x61c379;
const unsigned context_two_yes=0x61c43b,context_two_no=0x61c43f;
__declspec(naked) void context_one() {
    __asm { pushad
        push eax
        call editor_name
        add esp,4
        test al,al
        jz other
        popad
        jmp dword ptr [context_one_yes]
    other:
        popad
        jmp dword ptr [context_one_no] }
}
__declspec(naked) void context_two() {
    __asm { pushad
        push eax
        call editor_name
        add esp,4
        test al,al
        jz other
        popad
        jmp dword ptr [context_two_yes]
    other:
        popad
        jmp dword ptr [context_two_no] }
}
void contribute(patch_transaction::contribution& c) {
    c.writes.push_back(patch_transaction::relocation(0x61c35a,"e9",true,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&context_one)),13,"text entry input context by both names"));
    c.writes.push_back(patch_transaction::relocation(0x61c429,"e9",true,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&context_two)),13,"text entry refreshed input context by both names"));
    c.writes.push_back(patch_transaction::relocation(0x69f07c,"",false,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&activate)),0,"online text keyboard activation"));
    c.writes.push_back(patch_transaction::relocation(0x69f080,"",false,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&deactivate)),0,"online text keyboard retirement"));
    c.writes.push_back(patch_transaction::relocation(0x69f0a4,"",false,
        static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(&tick)),0,"online text keyboard native character lane"));
}
}
