// 0.6 preview fix H: hidden-authored model scenes (online_menus_models_rules.hpp; 060q live failures 5 / 6).
// The Host screen's scenes are REAL model items of the mapped XMen2.exe image (vtable 0x6a0304): their setHidden
// (vt+8 = 0x5c4b90), shown test (vt+4 = 0x5bd8f0), load (vt+0x38 = 0x5bbdd0) and hidden-aware load (vt+0x3c = 0x5c4c50) run
// as shipped. The menu-level hide 0x5ade10 is "find the item, call its vt+8" (stubbed as exactly that); the XMLB read is
// what 0x5bc7a0 + 0x5c5080 do: `hide` sets +0x54 bit 2, then vt+0x38(model, 0). Only the leaves are stubs: the string
// intern (manager vt+0x260 = 0x5d5a30), the handle release 0x575980 and the resource load 0x5bc3f0 (it records which model
// the item now draws and hands back the previous one into the save slot, as the retail one does for 0x5c4b90's +0x94).
// Earlier fixtures (r5b-views) recorded only the hidden bit of a fake item, so a scene the engine can never draw passed.
namespace models_fixture {
std::deque<std::string> pool;          // interned names: a handle is the address of the pooled string's characters
std::map<void*,std::string> drawn;     // item -> the model it draws now
std::vector<std::string> released;
std::map<std::string,std::array<unsigned char,0x100>> scene_items;
const char* handle_name(void* h) { return h ? static_cast<const char*>(h) : ""; }
void __fastcall intern(void*,void*,void** out,const char* name,int) { pool.emplace_back(name ? name : "");*out=pool.back().data(); }
void __cdecl release(void** h) { released.emplace_back(handle_name(*h));*h=nullptr; }
bool __fastcall load(void* item,void*,void** name,void** save) {
    if(!*name) return false;                                   // retail 0x5bc3f0: cmp [name],0 / je -> nothing loads
    auto previous=drawn.count(item) ? drawn[item] : std::string();
    if(save) { pool.push_back(previous);*save=pool.back().data(); }
    drawn[item]=handle_name(*name);return true;
}
constexpr const char* invis="ui/models/m_invis.igb";
constexpr std::pair<const char*,bool> scenes[]{  // name, authored hidden
    {"lc_host_settings_unknown",true},{"lc_host_drop_unknown",true},{"lc_host_drop_on",true},{"lc_host_drop_off",true},
    {"lc_host_private",false},{"lc_host_public",true}};
std::string model_of(const char* name) { return std::string("ui/models/lc_o_lc_host_")+name; }
void* scene(const char* name) { auto f=scene_items.find(name);return f==scene_items.end() ? nullptr : f->second.data(); }
using vt_load=void(__thiscall*)(void*,const char*,void*);
void xmlb_read(void* item,const char* model,bool hide) {
    if(hide) static_cast<unsigned char*>(item)[0x54]|=4;  // 0x5bcb96: or byte [esi+54],4 (before the model is read)
    const auto slot=(*static_cast<std::uintptr_t**>(item))[0x38/4];
    assert(slot==0x5bbdd0);
    reinterpret_cast<vt_load>(slot)(item,model,nullptr);    // 0x5c50ae: call [eax+38](model, 0)
}
bool shown_bit(void* item) { return !(static_cast<unsigned char*>(item)[0x54]&4); }
std::string table_text() {
    std::string t=std::string(online_menus_models::contract)+"\n";
    for(const auto& [name,hidden]:scenes) if(hidden) t+=std::string("lc_host ")+name+" "+model_of(name)+"\n";
    return t;
}
void run(const script_functions_rules::func_entry* registration,const std::string& variant) {
    using tick_t=void(__fastcall*)(void*,void*);
    const auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    auto post=[&](intent value) { int_value[1]=unsigned(value);reinterpret_cast<void*(__cdecl*)(void*)>(const_cast<void*>(registration->handler))(nullptr); };
    redirect(0x5d5a30,reinterpret_cast<const void*>(&intern));
    redirect(0x575980,reinterpret_cast<const void*>(&release));
    redirect(0x5bc3f0,reinterpret_cast<const void*>(&load));
    scene_lookup=&scene;
    for(const auto& [name,hidden]:scenes) {
        auto& mem=scene_items[name];put(mem.data(),0,0x6a0304);
        xmlb_read(mem.data(),model_of(name).c_str(),hidden);
        // The engine's own load: a hidden-authored scene draws m_invis and its authored name is released (0x5c4c94).
        assert(drawn[mem.data()]==(hidden ? std::string(invis) : model_of(name)));
        if(hidden) assert(std::find(released.begin(),released.end(),model_of(name))!=released.end());
    }
    {   // The retail defect itself: showing a hidden-authored scene through the engine's setHidden restores nothing.
        std::array<unsigned char,0x100> probe{};put(probe.data(),0,0x6a0304);
        xmlb_read(probe.data(),"ui/models/lc_o_probe",true);
        reinterpret_cast<void(__thiscall*)(void*,bool)>(0x5c4b90)(probe.data(),false);
        assert(shown_bit(probe.data()) && drawn[probe.data()]==invis);
    }
    if(variant=="r17-models") std::ofstream(root/"Data/lc_online_models.txt",std::ios::binary)<<table_text();
    if(variant=="r17-models-refused") std::ofstream(root/"Data/lc_online_models.txt",std::ios::binary)<<table_text()+"lc_host bad ../x\n";
    ++asset_generation;
    std::strcpy(reinterpret_cast<char*>(child.data()+0xc),"lc_host");child[4]=1;set_current(child.data());
    tick(child.data(),nullptr);menu_auth=true;tick(child.data(),nullptr);tick(child.data(),nullptr);
    auto* unknown=scene("lc_host_settings_unknown");auto* drop=scene("lc_host_drop_unknown");
    auto* priv=scene("lc_host_private");auto* pub=scene("lc_host_public");
    // The DLL asked for the "SET IN GAME OPTIONS" and "--" scenes (bit clear) in every variant.
    assert(shown_bit(unknown) && shown_bit(drop) && !shown_bit(scene("lc_host_drop_on")) && shown_bit(priv) && !shown_bit(pub));
    if(variant!="r17-models") {
        // No (or a refused) table: exactly the 060q screen - asked to show, still drawing m_invis; the log says why once.
        assert(drawn[unknown]==invis && drawn[drop]==invis && drawn[priv]==model_of("lc_host_private"));
        const auto key=variant=="r17-models-absent" ? "online:models-absent" : "online:models-refused";
        assert(once_logs.count(key) && once_logs.count("online:models-miss:lc_host:lc_host_settings_unknown"));
        std::cout<<"online menus "<<variant<<" PASS (no usable table: hidden-authored scenes stay m_invis, as live 060q; logged)\n";return;
    }
    assert(drawn[unknown]==model_of("lc_host_settings_unknown") && drawn[drop]==model_of("lc_host_drop_unknown"));
    assert(drawn[scene("lc_host_drop_on")]==invis && drawn[priv]==model_of("lc_host_private") && drawn[pub]==invis);
    assert(once_logs.count("online:models") && once_logs["online:models"].find("from lc-online-models/1 (5 entries)")!=std::string::npos);
    // PUBLIC: the public scene (chips + YOUR ROOM badge) appears from the table, the private one hides into its +0x94 save.
    unsigned loads=0;
    for(int round=0;round<3;++round) {
        post(intent::visibility);tick(child.data(),nullptr);
        assert(shown_bit(pub) && !shown_bit(priv) && drawn[pub]==model_of("lc_host_public") && drawn[priv]==invis);
        post(intent::visibility);tick(child.data(),nullptr);
        assert(shown_bit(priv) && !shown_bit(pub) && drawn[priv]==model_of("lc_host_private") && drawn[pub]==invis);
        ++loads;
    }
    // After the first show the engine's own save / restore carries the scene: the table is used once per item.
    unsigned interned_public=0;for(const auto& s:pool) if(s==model_of("lc_host_public")) ++interned_public;
    assert(interned_public>=1);
    std::cout<<"online menus "<<variant<<" PASS (hidden-authored Host scenes drawn from lc-online-models/1 through the retail model item code; "
        <<loads<<" PUBLIC / PRIVATE round trips)\n";
}

// 060q live (failure 11): the Hub's focused row kept its light-blue hint on the amber bar and the help box always read HOST
// A GAME. Real text items (vtable 0x6a0244, setHidden 0x5bcdb0 = the bit) for the rows, their navy twins and the per-row
// help sets; the CMenu's focused item (+0x324) moves; the installed Hub tick shows exactly the focused row's twin and help.
void hub_focus_run(const script_functions_rules::func_entry*) {
    using tick_t=void(__fastcall*)(void*,void*);
    const auto tick=*reinterpret_cast<tick_t*>(0x69e454);
    scene_lookup=&scene;
    const char* rows[]{"host","code","browse","other"};
    std::vector<std::string> names{"change_name"};
    for(const auto* r:rows) for(const auto* suffix:{"","_hint","_hint_on"}) names.push_back(std::string(r)+suffix);
    for(const auto* r:rows) for(const auto* part:{"_title","_1","_2"}) names.push_back(std::string("lc_help_")+r+part);
    for(const auto& n:names) { auto& mem=scene_items[n];put(mem.data(),0,0x6a0244); }
    auto shown=[&](const std::string& n) { return !(scene_items[n][0x54]&4); };
    auto focus=[&](const char* n) { *reinterpret_cast<void**>(hub.data()+0x324)=scene(n);set_current(hub.data());tick(hub.data(),nullptr); };
    // The authored start: the twins and every help set but the Host row's hidden.
    for(const auto& n:names) if(n.ends_with("_hint_on") || (n.starts_with("lc_help_") && !n.starts_with("lc_help_host"))) scene_items[n][0x54]|=4;
    for(const auto* row:{"host","code","browse","other"}) {
        focus(row);
        for(const auto* r:rows) {
            const bool on=std::string_view(r)==row;
            assert(shown(std::string(r)+"_hint")==!on && shown(std::string(r)+"_hint_on")==on);
            for(const auto* part:{"_title","_1","_2"}) assert(shown(std::string("lc_help_")+r+part)==on);
        }
    }
    // Focus on Change name: no row twin, the last row's help stays.
    focus("change_name");
    for(const auto* r:rows) assert(shown(std::string(r)+"_hint") && !shown(std::string(r)+"_hint_on"));
    assert(shown("lc_help_other_title") && !shown("lc_help_host_title"));
    assert(texts["browse_hint_on"]==texts["browse_hint"] && !texts["browse_hint"].empty());
    std::cout<<"online menus r17-hub-focus PASS (the focused row's navy hint twin and help set follow the CMenu focus; Change name keeps the last help)\n";
}
}
