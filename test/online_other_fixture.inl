// Native Other ways routing and real LAN list update/admission, fake services.
namespace other_fixture {
std::array<unsigned,64> list_vt{};unsigned* listing=list_vt.data();
std::array<unsigned char,0x2000> names{};
std::vector<std::string> rows;
std::vector<bool> row_enabled;
std::vector<int> localized;
int selected=0,admissions=0;
void* admitted=nullptr;
void* __cdecl name_table() { return names.data(); }
const char* __fastcall locale(void*,void*,int id) { localized.push_back(id);return "Synthetic state"; }
int __fastcall selection(void*,void*) { return selected; }
void __fastcall clear(void*,void*) { rows.clear();row_enabled.clear(); }
int __fastcall append(void*,void*,const char* value,int) { rows.emplace_back(value);row_enabled.push_back(true);return int(rows.size()-1); }
void __fastcall enable_row(void*,void*,int row,int enabled) { assert(row>=0 && unsigned(row)<row_enabled.size());row_enabled[row]=enabled!=0; }
void __fastcall select_row(void*,void*,int row) { selected=row; }
void __fastcall refresh(void*,void*) {}
int __fastcall count(void*,void*) { return int(rows.size()); }
void __fastcall address_clear(void* address,void*) { std::memset(address,0,12); }
void __fastcall address_copy(void* address,void*,void*) { std::memset(address,0,12); }
bool __fastcall address_diff(void*,void*,void*) { return false; }
void __fastcall admit(void*,void*,void* record) { ++admissions;admitted=record; }
bool __fastcall accept_only(void*,void*,unsigned input,int) { return (input&0x10)!=0; }
int __fastcall preference(void*,void*) { return 2; }
unsigned preference_sets=0;
void __fastcall set_preference(void*,void*,int value) { assert(value==2);++preference_sets; }
void run(const script_functions_rules::func_entry* entry) {
    using request_t=void(__fastcall*)(void*,void*,const char*,const char*);
    const auto request=*reinterpret_cast<request_t*>(0x6a23d8);
    for(const auto* native:{"online","campaign_lobby","games_list","player_game_options"}) {
        const std::string variant=std::string(native)=="online" ? "lc_other_online" : "lc_"+std::string(native);
        std::vector<std::string> files;
        for(const auto* ext:{".XMLB",".engb",".IGB"}) files.push_back("UI/menus/"+variant+ext);
        files.insert(files.end(),{"Textures/menus/"+variant+".IGB","Packages/generated/maps/package/menus/"+variant+".PKGB",
            "UI/models/lc_o_backdrop_"+variant+".IGB","UI/models/lc_o_field.IGB"});
        const bool controls=std::string_view(native)!="games_list";
        if(controls) files.push_back("UI/models/lc_o_focus_0_-400_4144_0.IGB");
        if(std::string_view(native)=="online" || std::string_view(native)=="player_game_options") files.push_back("UI/models/lc_o_focus_0_-224_4144_0.IGB");
        if(std::string_view(native)=="online" || std::string_view(native)=="games_list")
            for(const auto* model:{"focus","up","down"}) files.push_back("UI/models/lc_o_picker_"+std::string(model)+".IGB");
        for(const auto& file:files) { const auto path=root/file;std::filesystem::create_directories(path.parent_path());std::ofstream(path)<<"synthetic"; }
        const auto marker=root/("Data/lc_online_skin_"+std::string(native)+".cap");
        const auto capability="lc-online-skin/"+std::string(native)+"/1\n";
        std::ofstream(marker,std::ios::binary)<<capability;
        set_current(hub.data());
        const auto expect=[&](const std::string& want) { calls.clear();request(mgr.data(),nullptr,native,"synthetic arguments");assert(calls.back()=="request:"+want); };
        expect(variant);
        for(const auto& file:files) {
            const auto path=root/file;const auto missing=path.wstring()+L".missing";
            std::filesystem::rename(path,missing);expect("lc_online_unavailable");std::filesystem::rename(missing,path);
        }
        std::ofstream(marker,std::ios::binary|std::ios::trunc)<<capability<<"extra";expect("lc_online_unavailable");
        std::ofstream(marker,std::ios::binary|std::ios::trunc)<<capability;
    }
    // The actual Hub Other action bypasses main-menu interception but must use
    // the same optional scene selector as native manager/console requests.
    set_current(hub.data());int_value[1]=5;reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(entry->handler))(nullptr);
    (*reinterpret_cast<void(__fastcall**)(void*,void*)>(0x69e454))(hub.data(),nullptr);
    assert(calls.back()=="request:lc_other_online");
    std::cout<<"PASS all Other ways scene gates, missing resources, invalid markers and Hub action\n";
    native_fixture::restore_services();
    // The native Host/Join chooser follows identifiers from the real focus
    // object and requests the two actual PC pages; no broker is involved.
    redirect(0x602200,name_table);put(names.data(),8,16);put(text_item.data(),8,1);
    put(child.data(),0,0x69f404);std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_campaign_lobby");set_current(child.data());
    put(child.data(),0x324,unsigned(reinterpret_cast<std::uintptr_t>(text_item.data())));child[0x195c]=1;put(child.data(),0x1968,0);mask=0x10;
    std::strcpy(reinterpret_cast<char*>(names.data()+0x1018),"text_hostgame");
    reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5b09f0)(child.data(),nullptr);assert(calls.back()=="request:lc_game_options");
    std::strcpy(reinterpret_cast<char*>(names.data()+0x1018),"text_joingame");
    reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5b09f0)(child.data(),nullptr);assert(calls.back()=="request:lc_player_game_options");
    std::cout<<"PASS real native campaign choice Host/Join effects and intermediate PC page\n";
    // The native list updater renders searching -> empty -> rows, filtering
    // incompatible records with its own joinability flag. The real tick calls
    // native admission only for a selectable record.
    locale_vt[2]=unsigned(reinterpret_cast<std::uintptr_t>(&locale));
    list_vt[0xc0/4]=unsigned(reinterpret_cast<std::uintptr_t>(&clear));list_vt[0xc4/4]=unsigned(reinterpret_cast<std::uintptr_t>(&refresh));
    list_vt[0xc8/4]=unsigned(reinterpret_cast<std::uintptr_t>(&selection));list_vt[0xd0/4]=unsigned(reinterpret_cast<std::uintptr_t>(&select_row));
    list_vt[0x84/4]=unsigned(reinterpret_cast<std::uintptr_t>(&append));list_vt[0x8c/4]=unsigned(reinterpret_cast<std::uintptr_t>(&enable_row));
    redirect(0x6155e0,address_clear);redirect(0x615600,address_copy);redirect(0x615650,address_diff);redirect(0x5bf750,count);redirect(0x60a0a0,admit);
    redirect(0x6720ec,reinterpret_cast<const void*>(&std::sprintf));
    std::array<unsigned char,16> table{};std::array<std::array<unsigned char,0x18>,3> nodes{};
    std::array<std::array<unsigned char,0x44>,3> records{};
    child.fill(0);put(child.data(),0,0x69f8b4);std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_games_list");set_current(child.data());
    put(child.data(),0x1960,unsigned(reinterpret_cast<std::uintptr_t>(&listing)));
    put(child.data(),0x1964,unsigned(reinterpret_cast<std::uintptr_t>(table.data())));
    mask=0;reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5b9900)(child.data(),nullptr);
    assert(rows.size()==1 && localized.back()==0xfc5);
    put(child.data(),0x196c,5);reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5b9900)(child.data(),nullptr);
    assert(rows.size()==1 && !row_enabled[0] && localized.back()==0xfe4);
    mask=0x10;reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5b9800)(child.data(),nullptr);assert(admissions==0);
    for(unsigned i=0;i<3;++i) {
        put(nodes[i].data(),8,i<2 ? unsigned(reinterpret_cast<std::uintptr_t>(nodes[i+1].data())) : 0);
        put(nodes[i].data(),0x10,unsigned(reinterpret_cast<std::uintptr_t>(records[i].data())));
        std::strcpy(reinterpret_cast<char*>(records[i].data()+0x12),i==0 ? "Synthetic open" : i==1 ? "Synthetic full" : "Synthetic incompatible");
        records[i][0x11]=i<2;records[i][0x38]=i==1 ? 4 : 1;records[i][0x39]=4;records[i][0x3b]=1;
    }
    put(table.data(),0,unsigned(reinterpret_cast<std::uintptr_t>(nodes[0].data())));put(table.data(),0xc,3);
    selected=0;mask=0;reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5b9900)(child.data(),nullptr);
    assert(rows.size()==2 && child[0x1968] && row_enabled[0] && row_enabled[1]);
    mask=0x10;reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5b9800)(child.data(),nullptr);
    assert(admissions==1 && admitted==records[0].data());
    selected=2;reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5b9800)(child.data(),nullptr);assert(admissions==1);
    std::cout<<"PASS real native LAN searching/empty/open/full/incompatible rows and admission effect\n";
    mask=0;put(child.data(),0,0x69f724);std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_player_game_options");set_current(child.data());
    redirect(0x5cef40,refresh);redirect(*reinterpret_cast<unsigned*>(0x6a236c+0x160),accept_only);
    mask=0x1000000;reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5cf230)(child.data(),nullptr);mask=0;
    assert(native_tag==3 && native_max==15 && calls.back()=="request:lc_text_entry"); // no optional editor assets in this mode
    redirect(0x607450,preference);redirect(0x607430,set_preference);
    result("203.0.113.9",3);calls.clear();const auto consumed=consumes;
    reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5cf040)(child.data(),nullptr);
    assert(consumes==consumed+1 && preference_sets==1 && std::string(reinterpret_cast<char*>(0xa3bc1c))=="203.0.113.9");
    assert(calls==std::vector<std::string>({"queue:gamesearch","queue:openmenu games_list"}));
    native_fixture::console_request("games_list");assert(calls.back()=="request:lc_games_list");
    native_ready=false;calls.clear();reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5cf040)(child.data(),nullptr);assert(calls.empty());
    result("Foreign",1);reinterpret_cast<void(__fastcall*)(void*,void*)>(0x5cf040)(child.data(),nullptr);assert(calls.empty() && consumes==consumed+1);
    std::cout<<"PASS real direct-IP tag/limit, address/selection/search effects, browser return, Cancel and foreign-result refusal\n";
}
}
