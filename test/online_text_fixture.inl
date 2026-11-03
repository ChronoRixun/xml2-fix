// Native PC text-entry code in the reserved PE fixture. No game/window/input.
namespace text_fixture {
std::array<unsigned char,0x1e20> editor{};
unsigned closed=0;
unsigned sounds=0;
void __fastcall sound(void*,void*,int) { ++sounds; }
int __fastcall close_editor(void*,void*) { ++closed;return 2; }
void __fastcall activate_base(void*,void*,int) {}
void __cdecl cookie(unsigned) {}
const char* __cdecl find_character(const char* text,int c) { return std::strchr(text,c); }
int __cdecl whitespace(int c) { return std::isspace(static_cast<unsigned char>(c)); }
int context=-1;
void __cdecl context_set(int value) { context=value; }
bool __fastcall menu_open(void*,void*) { return true; }
bool __fastcall not_busy(void*,void*) { return false; }
bool __fastcall key_up(void*,void*,int) { return false; }
void* __fastcall current_menu(void*,void*) { return *reinterpret_cast<void**>(mgr.data()+0x86090); }
std::array<unsigned,32> dialog_vt{};unsigned* dialog_object=dialog_vt.data();
void* __cdecl dialog_get() { return &dialog_object; }
void run(const script_functions_rules::func_entry* entry) {
    AddVectoredExceptionHandler(1,[](EXCEPTION_POINTERS* error)->LONG {
        if(error->ExceptionRecord->ExceptionCode==EXCEPTION_ACCESS_VIOLATION) {
            std::cerr<<"text fixture fault eip="<<std::hex<<error->ContextRecord->Eip<<" address="<<error->ExceptionRecord->ExceptionInformation[1]<<" stack ";
            const auto* stack=reinterpret_cast<unsigned*>(error->ContextRecord->Esp);
            for(int i=0;i<12;++i) std::cerr<<stack[i]<<' ';
            std::cerr<<std::dec<<"\n";ExitProcess(EXCEPTION_ACCESS_VIOLATION);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    });
    using request_t=void(__fastcall*)(void*,void*,const char*,const char*);
    const auto request=*reinterpret_cast<request_t*>(0x6a23d8);
    constexpr const char* files[]{"UI/menus/lc_text_entry.XMLB","UI/menus/lc_text_entry.engb","UI/menus/lc_text_entry.IGB",
        "Textures/menus/lc_text_entry.IGB","Packages/generated/maps/package/menus/lc_text_entry.PKGB",
        "UI/models/lc_o_backdrop_lc_text_entry.IGB","UI/models/lc_o_field.IGB",
        "UI/models/lc_o_focus_-200_-208_200_0.IGB","UI/models/lc_o_focus_-488_-224_488_0.IGB",
        "UI/models/lc_o_focus_-528_-224_528_0.IGB"};
    for(const auto* file:files) {
        const auto path=root/file;std::filesystem::create_directories(path.parent_path());std::ofstream(path)<<"synthetic";
    }
    auto marker=root/"Data/lc_online_skin_text_entry.cap";
    auto cap=[&](const char* text) { std::ofstream(marker,std::ios::binary|std::ios::trunc)<<text; };
    cap("lc-online-skin/text_entry/1\n");
    put(child.data(),0,0x69f65c);std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"game_options");set_current(child.data());
    auto expect=[&](const char* destination) { calls.clear();request(mgr.data(),nullptr,"text_entry",nullptr);assert(calls.back()==std::string("request:")+destination); };
    expect("lc_text_entry");
    for(const auto* file:files) {
        const auto path=root/file;const auto missing=path.wstring()+L".missing";
        std::filesystem::rename(path,missing);expect("text_entry");std::filesystem::rename(missing,path);
    }
    for(const auto* bad:{"","lc-online-skin/text_entry/2\n","lc-online-skin/text_entry/1\nextra"}) { cap(bad);expect("text_entry"); }
    cap("lc-online-skin/text_entry/1\n");
    put(child.data(),0,0x69efa4);std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"online");expect("lc_text_entry");
    put(child.data(),0,0x69f8b4);std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"games_list");expect("lc_text_entry");
    put(child.data(),0,0x69f724);std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_player_game_options");expect("lc_text_entry");
    set_current(main_menu.data());expect("text_entry");
    std::cout<<"PASS text entry scope and all 10 missing-resource / marker fallback cases\n";
    for(unsigned va:{0x60b960u,0x60b9a0u,0x60c730u,0x60c780u}) restore_retail(va,5);
    redirect(0x5accc0,activate_base);redirect(0x5af410,deactivate_stub);
    redirect(0x5b9fe0,close_editor);redirect(0x672161,cookie);
    redirect(*reinterpret_cast<unsigned*>(0x6a236c+0xe8),sound);
    redirect(0x6723da,find_character);redirect(0x6723a4,whitespace);
    const auto tick=*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69f0a4);
    const auto activate=*reinterpret_cast<void(__fastcall**)(void*,void*,int)>(0x69f07c);
    const auto deactivate=*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69f080);
    const auto command=[&](int key) {
        int_value[1]=unsigned(100+key);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(entry->handler))(nullptr);
    };
    const auto begin=[&](int tag,int limit,const char* seed="") {
        editor.fill(0);put(editor.data(),0,0x69f06c);std::strcpy(reinterpret_cast<char*>(editor.data()+0xc),"lc_text_entry");
        put(editor.data(),0x1d10,limit);std::strcpy(reinterpret_cast<char*>(editor.data()+0x1c10),seed);
        std::strcpy(reinterpret_cast<char*>(editor.data()+0x1d14),seed);
        std::strcpy(reinterpret_cast<char*>(0xa3e168+0x200),seed);
        *reinterpret_cast<unsigned char*>(0xa3e168+0x300)=0;put(reinterpret_cast<void*>(0xa3e168),0x304,tag);
        *reinterpret_cast<unsigned char*>(0xa0a061)=0;mask=0;set_current(editor.data());activate(editor.data(),nullptr,0);
    };
    const auto text=[&]() { return std::string(reinterpret_cast<char*>(editor.data()+0x1c10)); };
    const auto key=[&](int k) { command(k);tick(editor.data(),nullptr); };
    const auto typed=[&](unsigned char c) { *reinterpret_cast<unsigned char*>(0xa0a061)=c;tick(editor.data(),nullptr); };
    begin(0,31);assert(texts["text_inputkeys01"]=="a");key(0);assert(text()=="a");
    key(38);assert(texts["text_inputkeys01"]=="A");key(0);assert(text()=="aA");
    key(38);assert(texts["text_inputkeys01"]=="<" && !enabled_items["text_inputkeys06"]);key(0);key(37);assert(text()=="aA< ");
    key(36);assert(text()=="aA<");typed('!');assert(text()=="aA<");
    key(38);for(int i=0;i<40;++i) key(1);assert(text().size()==31);
    const auto before=closed;key(39);assert(closed==before+1);
    assert(*reinterpret_cast<unsigned char*>(0xa3e168+0x300)==1 && *reinterpret_cast<int*>(0xa3e168+0x304)==0);
    assert(std::string(reinterpret_cast<char*>(0xa3e168+0xc0))==text());
    begin(1,31);const auto empty_closed=closed;key(39);assert(closed==empty_closed);key(37);key(39);assert(closed==empty_closed);
    typed('N');key(39);assert(closed==empty_closed+1 && *reinterpret_cast<int*>(0xa3e168+0x304)==1);
    begin(3,15);assert(texts["text_inputkeys01"]=="1" && !enabled_items["text_inputkeys12"] && !enabled_items["text_inputkeys38"]);
    typed('A');assert(text().empty());key(0);key(10);key(1);assert(text()=="1.2");key(38);assert(texts["text_inputkeys01"]=="1");
    for(int i=0;i<20;++i) key(0);assert(text().size()==15);key(39);assert(*reinterpret_cast<int*>(0xa3e168+0x304)==3);
    begin(0,31,"Seed");key(0);typed(27);assert(text()=="Seed");
    begin(0,31,"Seed");key(0);key(40);assert(text()=="Seed");
    begin(0,31,"Back");key(0);mask=0x200000;tick(editor.data(),nullptr);mask=0;assert(text()=="Back");
    begin(0,31);command(0);typed('Z');assert(text()=="Z");tick(editor.data(),nullptr);assert(text()=="Za");
    begin(0,31);command(0);deactivate(editor.data(),nullptr);begin(0,31);tick(editor.data(),nullptr);assert(text().empty());
    std::cout<<"PASS native text entry keyboard, pad/mouse command effects, pages, charset, limits, Done/Cancel tags and retired input\n";
    redirect(0x619bd0,context_set);redirect(*reinterpret_cast<unsigned*>(0x6a236c+0x204),menu_open);
    redirect(*reinterpret_cast<unsigned*>(0x6a236c+0x210),current_menu);
    dialog_vt[0x78/4]=unsigned(reinterpret_cast<std::uintptr_t>(&not_busy));redirect(0x5eb300,dialog_get);
    redirect(0x6285c0,tick_stub);redirect(0x6270e0,key_up);
    *reinterpret_cast<unsigned char*>(0xa53ee8)=0;put(reinterpret_cast<void*>(0xa6abfc),0,1);
    for(const auto* name:{"text_entry","lc_text_entry","lc_online","team"}) {
        std::strcpy(reinterpret_cast<char*>(editor.data()+0xc),name);set_current(editor.data());
        const int expected=std::string_view(name)=="text_entry" || std::string_view(name)=="lc_text_entry" ? 2 : std::string_view(name)=="team" ? 3 : 1;
        context=-1;reinterpret_cast<void(__cdecl*)()>(0x61c300)();assert(context==expected);
        put(reinterpret_cast<void*>(0xa6adb4),0,5);context=-1;reinterpret_cast<void(__cdecl*)()>(0x61c3b0)();assert(context==expected);
    }
    std::cout<<"PASS both native keyboard context selectors: retail/LC editors, Team and ordinary menu\n";
}
}
