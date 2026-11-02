// 0.6 preview fix H (060q live failure 11): the Hub's focused row kept its light-blue hint on the amber bar and the help box
// always read HOST A GAME. Real text items (vtable 0x6a0244, setHidden 0x5bcdb0 = the bit) for the rows, their navy twins and
// the per-row help sets, driven through the real menu-level hide (0x5ade10 = find the item, call its vt+8); the CMenu's focused
// item (+0x324) moves; the installed Hub tick shows exactly the focused row's twin and help.
namespace hub_fixture {
std::map<std::string,std::array<unsigned char,0x100>> items;
void* item_of(const char* name) { auto f=items.find(name);return f==items.end() ? nullptr : f->second.data(); }
void run(const script_functions_rules::func_entry*) {
    using tick_t=void(__fastcall*)(void*,void*);
    const auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    scene_lookup=&item_of;
    absent_items={"party_hint_on","lc_help_party_title","lc_help_party_1","lc_help_party_2"}; // not authored (assets lookup fails)
    // The Party row is authored without a twin or help set (it is not focusable in the LC layout); the DLL must cope.
    const char* rows[]{"host","code","browse","other"};
    std::vector<std::string> names{"change_name","party","party_hint"};
    for(const auto* r:rows) for(const auto* suffix:{"","_hint","_hint_on"}) names.push_back(std::string(r)+suffix);
    for(const auto* r:rows) for(const auto* part:{"_title","_1","_2"}) names.push_back(std::string("lc_help_")+r+part);
    auto shown=[&](const std::string& n) { return !(items[n][0x54]&4); };
    auto authored=[&] {
        for(const auto& n:names) { auto& mem=items[n];mem.fill(0);put(mem.data(),0,0x6a0244); }
        for(const auto& n:names) if(n.ends_with("_hint_on") || (n.starts_with("lc_help_") && !n.starts_with("lc_help_host"))) items[n][0x54]|=4;
    };
    auto focus=[&](const char* n) { *reinterpret_cast<void**>(hub.data()+0x324)=n ? item_of(n) : nullptr;set_current(hub.data());tick(hub.data(),nullptr); };
    auto expect=[&](const char* twin,const char* help) {
        for(const auto* r:rows) {
            const bool on=twin && std::string_view(r)==twin;
            assert(shown(std::string(r)+"_hint")==!on && shown(std::string(r)+"_hint_on")==on);
            for(const auto* part:{"_title","_1","_2"}) assert(shown(std::string("lc_help_")+r+part)==(std::string_view(r)==help));
        }
        assert(shown("party_hint"));
    };
    authored();
    for(const auto* row:rows) { focus(row);expect(row,row); }
    focus("change_name");expect(nullptr,"other");   // focus elsewhere: no twin, the last row's help stays
    focus("party");expect(nullptr,"other");         // a row without twin / help set: plain hints, last help kept
    assert(texts["browse_hint_on"]==texts["browse_hint"] && !texts["browse_hint"].empty());
    // A new Hub visit (departure clears the binding cache; a new menu at the same address) with the focus not yet set
    // starts on the Host help, not on the previous visit's row.
    focus("browse");expect("browse","browse");
    (*reinterpret_cast<tick_t*>(0x69e438))(hub.data(),nullptr);authored(); // the installed base deactivation (departure)
    focus(nullptr);expect(nullptr,"host");
    focus("code");expect("code","code");
    std::cout<<"online menus r17-hub-focus PASS (the focused row's navy hint twin and help set follow the CMenu focus over four rows; "
        "Change name and the Party row keep the last help; a new visit starts on the Host help)\n";
}
}
// Fix H: the Party rail's Codex row runs `openmenu codex` (LC online_party.CODEX_COMMAND). Checked against the mapped retail
// image, not restated: the exe's own codex route is that literal (0x68cf9c), pushed by the script function codexMenu
// (0x49e250) only after the menu manager's vt+0x84 test (0x5d5170) - which refuses while a menu is current, so the old
// rail command `runscript codexMenu(0)` from the Party screen (a menu) could never open the codex (060q live failure 8).
namespace codex_fixture {
void run() {
    assert(std::string_view(reinterpret_cast<const char*>(0x68cf9c))=="openmenu codex");
    const auto bytes=patch_transaction::bytes_of("ff928400000084c07421e8a3a613008b10568bc8ff92d4010000e803e60b008b10689ccf68008bc8ff521833c0");
    assert(std::memcmp(reinterpret_cast<const void*>(0x49e26e),bytes.data(),bytes.size())==0); // vt+0x84 gate, then push 0x68cf9c
    assert(*reinterpret_cast<const std::uint32_t*>(0x6a236c+0x84)==0x5d5170);                  // the manager's gate
    // 0x5d5170: a current menu (vt+0x80 non-null with a name) jumps to the refusal; only no menu reaches the game-state test.
    const auto gate=patch_transaction::bytes_of("568bf18b06ff908000000085c0740f8b168bceff92800000008038007517");
    assert(std::memcmp(reinterpret_cast<const void*>(0x5d5170),gate.data(),gate.size())==0);
    std::cout<<"online menus r17-codex-route PASS (retail codex route literal 0x68cf9c; codexMenu pushes it only past the menu-open gate 0x5d5170)\n";
}
}
