// Native PC text-entry code in the reserved PE fixture. No game/window/input.
namespace text_fixture {
std::array<unsigned char,0x1e20> editor{};
unsigned closed=0;
unsigned sounds=0;
void* cancel_return=nullptr;
void __fastcall sound(void*,void*,int) { ++sounds; }
int __fastcall close_editor(void*,void*) { ++closed;if(cancel_return) set_current(cancel_return);return 2; }
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
        std::filesystem::rename(path,missing);expect("lc_online_unavailable");std::filesystem::rename(missing,path);
    }
    for(const auto* bad:{"","lc-online-skin/text_entry/2\n","lc-online-skin/text_entry/1\nextra"}) { cap(bad);expect("lc_online_unavailable"); }
    cap("lc-online-skin/text_entry/1\n");
    put(child.data(),0,0x69efa4);std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"online");expect("lc_text_entry");
    put(child.data(),0,0x69f8b4);std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"games_list");expect("lc_text_entry");
    put(child.data(),0,0x69f724);std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_player_game_options");expect("lc_text_entry");
    set_current(main_menu.data());expect("lc_online_unavailable");
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
        editor.fill(0);editor[4]=1;put(editor.data(),0,0x69f06c);std::strcpy(reinterpret_cast<char*>(editor.data()+0xc),"lc_text_entry");
        put(editor.data(),0x1d10,limit);std::strcpy(reinterpret_cast<char*>(editor.data()+0x1c10),seed);
        std::strcpy(reinterpret_cast<char*>(editor.data()+0x1d14),seed);
        std::strcpy(reinterpret_cast<char*>(0xa3e168+0x200),seed);
        *reinterpret_cast<unsigned char*>(0xa3e168+0x300)=0;put(reinterpret_cast<void*>(0xa3e168),0x304,tag);
        *reinterpret_cast<unsigned char*>(0xa0a061)=0;mask=0;set_current(editor.data());activate(editor.data(),nullptr,0);
    };
    const auto text=[&]() { return std::string(reinterpret_cast<char*>(editor.data()+0x1c10)); };
    const auto key=[&](int k) { command(k);tick(editor.data(),nullptr); };
    const auto typed=[&](unsigned char c) { *reinterpret_cast<unsigned char*>(0xa0a061)=c;tick(editor.data(),nullptr); };
    online_text_entry_rules::inbox inbox;
    for(int i=0;i<70;++i) inbox.push_back(i);assert(inbox.size()==64);
    inbox.push_front(99);assert(inbox.front()==99 && inbox.size()==64);inbox.pop_front();
    assert(inbox.front()==0);inbox.clear();assert(inbox.empty());
    for(unsigned scan=0;scan<256;++scan) {
        const bool control=scan==1 || scan==0x0e || scan==0x1c || scan==0x9c || scan==0xcb || scan==0xcd || scan==0xc8 || scan==0xd0;
        assert(bool(online_text_entry_rules::key(scan))==control);
    }
    for(unsigned c:{0u,8u,13u,27u,31u,127u}) assert(!online_text_entry_rules::character(c));
    for(unsigned c:{0xe9u,0x20acu,0x4e2du,0xd83du,0xde00u,0x1f600u})
        assert(online_text_entry_rules::character(c)==online_text_entry_rules::unsupported);
    for(int count:{5,11,36}) {
        std::array<bool,41> reached{};reached[0]=true;
        for(int pass=0;pass<41;++pass) for(int i=0;i<41;++i) if(reached[i])
            for(int d:{online_text_entry_rules::left,online_text_entry_rules::right,online_text_entry_rules::up,online_text_entry_rules::down}) {
                const int target=online_text_entry_rules::move(i,d,count,count==11);assert(online_text_entry_rules::enabled(target,count,count==11));reached[target]=true;
            }
        for(int i=0;i<41;++i) if(online_text_entry_rules::enabled(i,count,count==11)) assert(reached[i]);
    }
    // Give every key its own actual item pointer, just as the native menu does.
    redirect(0x5adf40,focus_stub);
    // Controls use production DI delivery; printable text uses the public WM_CHAR sink.
    const auto press=[&](unsigned scan) {
        std::array<unsigned char,256> state{};online_menus::keyboard(state.data());
        state[scan]=128;online_menus::keyboard(state.data());assert(state[scan]==0);
        tick(editor.data(),nullptr);
    };
    const auto chars=[&](std::u16string_view value) {
        for(unsigned c:value) assert(online_menus::text_entry_character(c));
        for(unsigned i=0;i<value.size();++i) tick(editor.data(),nullptr);
    };
    const auto physical=[&](unsigned scan,unsigned character) { press(scan);chars(std::u16string(1,char16_t(character))); };
    // Regression: the IP caller seeds an empty buffer. Cancellation must not
    // enter Done's non-whitespace validator or publish a searchable address.
    // Exercise the actual installed input routes and the retail return handler.
    for(int route=0;route<5;++route) for(const char* seed:{"","203.0.113.8"}) for(const auto* draft:{u"",u"12.3"}) {
        if(mode.starts_with("r31-cancel-") && route!=std::stoi(mode.substr(11))) continue;
        std::cout<<"IP Cancel route "<<route<<" seed "<<(seed[0]?"nonempty":"empty")<<"\n";
        begin(3,15,seed);chars(draft);
        std::strcpy(reinterpret_cast<char*>(0xa3bc1c),"198.51.100.7");
        cancel_return=child.data();const auto before_close=closed;
        if(route==0) press(1); // physical Escape
        if(route==1) { mask=0x200000;tick(editor.data(),nullptr);mask=0; } // pad B
        if(route==2) { // focused Cancel + physical Enter
            std::array<unsigned char,256> state{};online_menus::keyboard(state.data());
            auto* cancel=item_stub(editor.data(),nullptr,"lc_text_cancel");
            put(editor.data(),0x324,unsigned(reinterpret_cast<std::uintptr_t>(cancel)));
            press(0x1c);
        }
        if(route==3) key(40); // the exact mouse usecmd lcOnlineIntent(140)
        if(route==4) typed(27); // native character lane
        assert(closed==before_close+1 && current_menu(nullptr,nullptr)==cancel_return);
        assert(text()==seed && *reinterpret_cast<unsigned char*>(0xa3e168+0x300)==0);
        assert(*reinterpret_cast<int*>(0xa3e168+0x304)==4);
        redirect(0x5ad080,tick_stub);
        reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5cf040)(child.data(),nullptr);
        assert(std::string(reinterpret_cast<char*>(0xa3bc1c))=="198.51.100.7");
        cancel_return=nullptr;
        std::cout<<"PASS IP Cancel route "<<route<<" seed "<<(seed[0]?"nonempty":"empty")<<": one close, no result, saved address unchanged\n";
    }
    if(mode.starts_with("r31-cancel-")) return;
    // Simulated TranslateMessage WM_CHAR streams: same physical keys, active
    // US (0409), French AZERTY (040c), German QWERTZ (0407) output.
    for(const auto& layout:{std::pair{0x0409,u"aqwzym"},std::pair{0x040c,u"qazwym"},std::pair{0x0407,u"aqwyzm"}}) {
        begin(0,31);
        std::string expected;
        for(unsigned i=0;i<6;++i) {
            physical(std::array<unsigned,6>{0x1e,0x10,0x11,0x2c,0x15,(layout.first==0x040c ? 0x27u : 0x32u)}[i],layout.second[i]);
            expected.push_back(static_cast<char>(layout.second[i]));
        }
        assert(text()==expected);
        assert(text().size()==6); // DI never duplicates WM_CHAR, including AZERTY M (scan 27).
        (void)layout.first;
    }
    // Correct QWERTZ Y/Z and shifted/caps-translated text arrive verbatim.
    begin(0,31);physical(0x15,'z');physical(0x2c,'y');chars(u"AZ");assert(text()=="zyAZ");
    // AltGr is not filtered by the DI modifier guard. Allowed '<' survives;
    // unsupported '@' / euro are visibly refused by native charset / Unicode guard.
    begin(0,31);std::array<unsigned char,256> modifiers{};online_menus::keyboard(modifiers.data());
    modifiers[0x1d]=modifiers[0xb8]=modifiers[0x10]=128;online_menus::keyboard(modifiers.data());
    chars(u"<");assert(text()=="<");chars(u"@\u20ac");assert(text()=="<" && texts["desctext4"]=="Unsupported");
    // A dead-key press produces no WM_CHAR until composition. Composed e-acute
    // and IME/surrogate output must never be narrowed to printable low bytes.
    begin(0,31);press(0x1a);assert(text().empty());chars(u"\u00e9\u4e61\xd83d\xde00");
    assert(text().empty() && texts["desctext4"]=="Unsupported");
    begin(0,31);press(0x1a);chars(u"a");assert(text()=="a"); // dead key yielding supported output
    // Both message/poll orders, repeats, and WM_CHAR copies of DI controls.
    begin(0,31);chars(u"a");press(0x1e);assert(text()=="a");chars(u"aa");assert(text()=="aaa");
    press(0x0e);chars(u"\b");assert(text()=="aa");press(0xcd);press(0x1c);chars(u"\r");assert(text()=="aab");
    // Main row (Shift supplies AZERTY digits) and NumLock numpad streams share
    // tag 3. A translated comma is refused, never silently changed to a dot.
    for(unsigned scan:{2u,0x52u}) {
        begin(3,15);physical(scan,'1');physical(0x34,'.');physical(scan,'2');assert(text()=="1.2");
        chars(u",");assert(text()=="1.2" && texts["desctext4"]=="Unsupported");
    }
    begin(3,15);assert(online_menus::text_entry_character(',',0x53));tick(editor.data(),nullptr);assert(text()==".");
    begin(0,31);assert(online_menus::text_entry_character(',',0x53));tick(editor.data(),nullptr);assert(text()==",");
    begin(3,15);physical(2,'1');physical(0x34,'.');physical(3,'2');assert(text()=="1.2");press(0x0e);assert(text()=="1.");
    press(0xcd);press(0x1c);assert(text()=="1.2"); // focus 1 -> 2, Enter types 2
    mask=0x1000;tick(editor.data(),nullptr);mask=0;tick(editor.data(),nullptr); // D-pad Down
    const auto pad_before=text();mask=0x10;tick(editor.data(),nullptr);mask=0;tick(editor.data(),nullptr);
    assert(text().size()==pad_before.size()+1); // A presses the focused key, never Done
    press(1);assert(text().empty());
    begin(0,31);physical(0x1e,'a');physical(0x30,'b');assert(text()=="ab");
    mask=0x200000;tick(editor.data(),nullptr);mask=0;assert(text().empty());
    const auto pad=[&](unsigned value) {mask=value;tick(editor.data(),nullptr);mask=0;tick(editor.data(),nullptr);};
    const auto action_mask=[](int action) {
        const auto native_action=reinterpret_cast<int(__cdecl*)(int)>(0x619c40);
        unsigned out=0;for(unsigned code=0;code<32;++code) {
            assert(native_action(int(code))==pad_prompts_rules::actions[code]);
            if(native_action(int(code))==action) out|=1u<<code;
        }
        return out;
    };
    // The console layout binds D-pad to native actions 12..15, stick to 0..3.
    for(const auto actions:{std::array<int,4>{15,14,13,12},std::array<int,4>{3,2,1,0}}) {
        begin(3,15);pad(action_mask(actions[0]));assert(focused=="text_inputkeys02");
        pad(action_mask(actions[1]));assert(focused=="text_inputkeys01");
        pad(action_mask(actions[2]));assert(focused=="text_inputkeys10");
        pad(action_mask(actions[3]));assert(focused=="text_inputkeys01");
    }
    begin(0,31);command(0);mask=0x10;tick(editor.data(),nullptr);mask=0;tick(editor.data(),nullptr);assert(text()=="a");
    // Enter/A accept Done only while that actual item owns focus.
    focus_stub(editor.data(),nullptr,"text_inputkeys40",true);const auto enter_done=closed;
    press(0x1c);assert(closed==enter_done+1);
    begin(0,31);key(0);focus_stub(editor.data(),nullptr,"text_inputkeys40",true);
    const auto pad_done=closed;pad(0x10);assert(closed==pad_done+1);
    // Queued text belongs to this activation, even at a reused menu address.
    begin(0,31);std::array<unsigned char,256> queued_keys{};online_menus::keyboard(queued_keys.data());
    queued_keys[0x1e]=128;online_menus::keyboard(queued_keys.data());assert(online_menus::text_entry_character('a'));deactivate(editor.data(),nullptr);
    begin(0,31);tick(editor.data(),nullptr);assert(text().empty());
    begin(0,31);chars(u"0123456789012345678901234567890");assert(text().size()==31);
    chars(u"a");assert(text().size()==31 && texts["desctext4"]=="Text limit");
    // Other ways inherits the Hub's longer name, but its editor owns 15 bytes.
    begin(0,15,"abcdefghijklmnopqrstuvwx");assert(text()=="abcdefghijklmno");
    chars(u"z");assert(text()=="abcdefghijklmno" && texts["desctext4"]=="Text limit");
    typed('z');assert(text().size()==15);key(0);assert(text().size()==15);
    press(0x0e);chars(u"z");assert(text()=="abcdefghijklmnz");
    // A boundary may bisect a UTF-8 sequence in the inherited seed.
    begin(0,15,"abcdefghijklm\xc3\xa9XYZ");assert(text()=="abcdefghijklm\xc3\xa9");
    begin(0,14,"abcdefghijklm\xc3\xa9XYZ");assert(text()=="abcdefghijklm");
    begin(0,15,"abcdefghijkl\xe2\x82\xacXYZ");assert(text()=="abcdefghijkl\xe2\x82\xac");
    begin(0,14,"abcdefghijkl\xe2\x82\xacXYZ");assert(text()=="abcdefghijkl");
    begin(0,15,"abcdefghijk\xf0\x9f\x98\x80XYZ");assert(text()=="abcdefghijk\xf0\x9f\x98\x80");
    begin(0,14,"abcdefghijk\xf0\x9f\x98\x80XYZ");assert(text()=="abcdefghijk");
    begin(0,15,"abcdefghijklmnopqrstuvwx");key(40);
    assert(text()=="abcdefghijklmnopqrstuvwx"); // Cancel retains the caller's seed.
    begin(0,31);assert(online_menus::text_entry_character('a'));online_menus::keyboard(nullptr);tick(editor.data(),nullptr);assert(text().empty());
    begin(0,31);for(int i=0;i<70;++i) assert(online_menus::text_entry_character('a'));
    tick(editor.data(),nullptr);assert(text()=="a" && texts["desctext4"]=="Input full");
    begin(0,31);chars(u"\u00e9a");assert(text()=="a" && texts["desctext4"]=="Unsupported");
    ticks+=3000;tick(editor.data(),nullptr);assert(texts["desctext4"].empty());
    // A foreign request cannot enqueue characters into the current activation.
    begin(0,31);put(reinterpret_cast<void*>(0xa3e168),0x304,3);
    assert(online_menus::text_entry_character('a'));put(reinterpret_cast<void*>(0xa3e168),0x304,0);
    tick(editor.data(),nullptr);assert(text().empty());
    std::cout<<"PASS WM_CHAR US 0409 / AZERTY 040c / QWERTZ 0407, AltGr, dead key, IME refusal, controls, repeat, no DI duplicates, numpad/main-row IP, visible refusal, queue bounds and 31-byte limit\n";
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
    for(int i=1;i<=40;++i) {
        char name[32]{};std::snprintf(name,sizeof(name),"lc_text_key_bg%02d",i);
        assert(visibility[name]==!(i<=11 || i==37 || i==40));
    }
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
        const int expected=std::string_view(name)=="text_entry" ? 2 : std::string_view(name)=="team" ? 3 : 1;
        context=-1;reinterpret_cast<void(__cdecl*)()>(0x61c300)();assert(context==expected);
        put(reinterpret_cast<void*>(0xa6adb4),0,5);context=-1;reinterpret_cast<void(__cdecl*)()>(0x61c3b0)();assert(context==expected);
    }
    std::cout<<"PASS both native input context selectors: retail editor 2; LC editor 1 with pad bindings; Team and ordinary menu\n";
}
}
