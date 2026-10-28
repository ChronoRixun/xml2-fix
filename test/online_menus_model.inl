// 0.6 preview fix I (060q live failures 3, 4, 9 and the Browse / Host / lobby scenes that never drew): a MENU_ITEM_MODEL that starts
// hidden never gets its model. Runs the RETAIL model-item code of the mapped XMen2.exe - 0x5c4c50 (vt+0x3c, install a model),
// 0x5bbdd0 (vt+0x38, load by name and install), 0x5c4b90 (setHidden) and the hidden-state query 0x5bd8f0 - over a fake item and a
// fake resource manager: only the engine's services under it are stubs (the manager 0x5d8920 and its model loader vt+0x260 / clock
// vt+0x24, the scene-graph assign 0x5bc3f0, the reference release 0x575980), and the stubs record what the retail code asked for.
// Why the earlier host fixtures passed: they replace 0x5ade10 (setItemHidden) altogether, so the engine's model path never ran.
namespace model_fixture {
std::array<unsigned char,0x1a0> item{};
std::array<unsigned,0x100> manager_vt{};
std::array<unsigned,2> manager_object{};
struct assign { void* model; void** stash; };
std::vector<assign> assigns;
std::vector<std::string> loads;
std::vector<void*> releases;
void* requested=reinterpret_cast<void*>(0x1111);
void* stand_in=reinterpret_cast<void*>(0x2222);
void* __cdecl get_manager() { return manager_object.data(); }
void __fastcall load_model(void*,void*,void** out,const char* name,int) {
    loads.emplace_back(name);*out=std::string_view(name).ends_with("m_invis.igb") ? stand_in : requested;
}
float __fastcall clock_stub(void*,void*) { return 0.f; }
bool __fastcall assign_stub(void*,void*,void** model,void** stash) { assigns.push_back({model ? *model : nullptr,stash});return true; }
void __cdecl release_stub(void** ref) { releases.push_back(*ref); }
void reset(bool hidden) {
    item.fill(0);put(item.data(),0,0x6a0304); // MENU_ITEM_MODEL's vtable
    item[0x54]=hidden ? 4 : 0;               // +0x54 bit 2 = hidden (the XML parse reads the `hide` attribute into it first)
    assigns.clear();loads.clear();releases.clear();
}
using install_t=bool(__thiscall*)(void*,void**,void**);   // vt+0x3c = 0x5c4c50(this, model ref, stash slot)
using by_name_t=void(__thiscall*)(void*,const char*,void**); // vt+0x38 = 0x5bbdd0(this, name, stash slot)
using set_hidden_t=void(__thiscall*)(void*,bool);          // vt+0x08 = 0x5c4b90
void run() {
    manager_vt[0x260/4]=unsigned(reinterpret_cast<std::uintptr_t>(&load_model));
    manager_vt[0x24/4]=unsigned(reinterpret_cast<std::uintptr_t>(&clock_stub));
    manager_object[0]=unsigned(reinterpret_cast<std::uintptr_t>(manager_vt.data()));
    redirect(0x5d8920,reinterpret_cast<const void*>(&get_manager));
    redirect(0x5bc3f0,reinterpret_cast<const void*>(&assign_stub));
    redirect(0x575980,reinterpret_cast<const void*>(&release_stub));
    const auto vt=reinterpret_cast<const unsigned*>(0x6a0304);
    assert(vt[1]==0x5bd8f0 && vt[2]==0x5c4b90 && vt[0xe]==0x5bbdd0 && vt[0xf]==0x5c4c50 && vt[0x11]==0x5c5080);
    const auto install=reinterpret_cast<install_t>(vt[0xf]);
    const auto by_name=reinterpret_cast<by_name_t>(vt[0xe]);
    const auto set_hidden=reinterpret_cast<set_hidden_t>(vt[2]);
    const auto m_invis=*reinterpret_cast<const char* const*>(0x6e5a08);
    assert(std::string_view(m_invis)=="ui/models/m_invis.igb");

    // 1. The XML parse of an item authored VISIBLE: the requested model is installed as it is (nothing released).
    reset(false);by_name(item.data(),"ui/models/lc_o_lc_online_row_status",nullptr);
    assert(loads.size()==1 && loads[0]=="ui/models/lc_o_lc_online_row_status" && assigns.size()==1 && assigns[0].model==requested && !assigns[0].stash && releases.empty());

    // 2. The same parse of an item authored hide="true" (the bit is set before the model is installed): the retail code asks the
    //    loader for the INVISIBLE stand-in, installs that, and RELEASES the requested model - the item has no real model for good.
    reset(true);by_name(item.data(),"ui/models/lc_o_lc_online_row_status",nullptr);
    assert(loads.size()==2 && loads[0]=="ui/models/lc_o_lc_online_row_status" && loads[1]=="ui/models/m_invis.igb");
    assert(assigns.size()==1 && assigns[0].model==stand_in && !assigns[0].stash);
    assert(releases.size()==1 && releases[0]==requested);
    assert(*reinterpret_cast<void**>(item.data()+0x94)==nullptr); // nothing is stashed: there is nothing to give back

    // 3. DLL un-hides it (setHidden(false), 0x5c4b90): the retail code only clears the bit and asks the install routine to restore
    //    the stash (+0x94) - which is empty - so nothing real is ever installed (the live 060q captures: the scene never draws).
    assigns.clear();loads.clear();releases.clear();
    set_hidden(item.data(),false);
    assert(!(item[0x54]&4) && assigns.size()==1 && assigns[0].model==nullptr && assigns[0].stash==nullptr && loads.empty() && releases.empty());

    // 4. An item authored visible and hidden by the DLL: the hide hands the install routine the stash slot (+0x94) to keep the
    //    real model in, the un-hide restores from it (the Host visibility pair; PRIVATE came back in the live round, PUBLIC did not).
    reset(false);by_name(item.data(),"ui/models/lc_o_lc_host_scene",nullptr);assigns.clear();loads.clear();releases.clear();
    set_hidden(item.data(),true);
    assert((item[0x54]&4) && loads.size()==2 && loads[0]=="ui/models/m_invis.igb" && loads[1]=="ui/models/m_invis.igb"); // 0x5bbdd0 loads the stand-in, 0x5c4c50 (hidden) loads and installs its own
    assert(assigns.size()==1 && assigns[0].model==stand_in && assigns[0].stash==reinterpret_cast<void**>(item.data()+0x94) && releases.size()==1 && releases[0]==stand_in);
    std::cout<<"online menus r16-model-start PASS retail 0x5c4c50 / 0x5bbdd0 / 0x5c4b90: a model authored hide=\"true\" is installed as the invisible stand-in with the requested model released and no stash to restore; one authored visible stashes on hide\n";
}
}
