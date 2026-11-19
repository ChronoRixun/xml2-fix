#ifdef REMASTERED_BASELINE_OPTIONS
#include "../../baseline-source/options_menu.cpp"
#else
#include "../src/options_menu.cpp"
#endif
#include <cassert>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>

namespace {
bool enabled=true,copying=false;
display::applied_state observed;
display_rules::options display_options;
std::filesystem::path directory;
std::vector<std::unique_ptr<unsigned char[]>> allocations;
std::map<int,void*> items;
std::map<void*,std::string> labels;
std::array<void*,32> widget_vt{};
unsigned writes=0,draws=0,navigation=0,saves=0,loads=0;
DWORD ui_thread=GetCurrentThreadId();
void* __cdecl allocate(std::size_t n) {
    auto data=std::make_unique<unsigned char[]>(n);void* ptr=data.get();allocations.push_back(std::move(data));return ptr;
}
void* __fastcall ctor(void* item,void*,void*,int id) { *static_cast<void***>(item)=widget_vt.data();items[id]=item;return item; }
void* __fastcall cycle_ctor(void* item,void* dummy,void* panel,int id,const char*) {return ctor(item,dummy,panel,id);}
void __fastcall rect(void* item,void*,int x,int y,int w,int h) {
    const int values[]={x,y,w,h};std::memcpy(static_cast<char*>(item)+0x14,values,sizeof(values));
}
void __fastcall set_text(void* item,void*,const char* text) {assert(!copying && GetCurrentThreadId()==ui_thread);labels[item]=text;++writes;}
void __fastcall dword(void*,void*,DWORD) {}
void __fastcall integer(void*,void*,int) {}
void __fastcall option(void*,void*,int,const char*) {}
void __fastcall option_rect(void*,void*,int,int,int,int,int) {}
void __fastcall selection(void* item,void*,int selected) {std::memcpy(static_cast<char*>(item)+0x1e0,&selected,4);}
void __fastcall nav(void*,void*,options_menu_rules::nav_record*) {++navigation;}
void* __fastcall find(void*,void*,int id) {auto it=items.find(id);return it==items.end() ? nullptr : it->second;}
void __fastcall anim(void*,void*,int) {}
bool __fastcall native_draw(void*,void*) {++draws;return true;}
void __cdecl save() {++saves;} void __cdecl load() {++loads;}
void redirect(unsigned rva,const void* target) {
    auto* at=options_menu::base+rva;at[0]=0xe9;
    auto delta=options_menu_rules::rel32(reinterpret_cast<std::uintptr_t>(at),reinterpret_cast<std::uintptr_t>(target));
    std::memcpy(at+1,&delta,4);
}
void prepare_stubs() {
    using namespace options_menu_rules;
    widget_vt[game::vtable_set_text/4]=reinterpret_cast<void*>(&set_text);
    widget_vt[game::vtable_set_colour/4]=reinterpret_cast<void*>(&dword);
    widget_vt[game::vtable_set_style/4]=reinterpret_cast<void*>(&integer);
    widget_vt[game::vtable_set_visible/4]=reinterpret_cast<void*>(&integer);
    redirect(game::operator_new,allocate);redirect(game::label_ctor,ctor);redirect(game::cycle_ctor,cycle_ctor);
    redirect(game::set_rect,rect);redirect(game::add_option,option);redirect(game::option_rect,option_rect);
    redirect(game::set_selection,selection);redirect(game::register_nav,nav);redirect(game::find_item,find);
    redirect(game::set_all_anim,anim);redirect(game::save_settings,save);redirect(game::load_settings,load);
    redirect(game::load_defaults,load);redirect(0x2223a0,native_draw);
}
void open(bool collision=false) {
    using namespace options_menu_rules;
    items.clear();labels.clear();navigation=0;
    void* panel=allocate(0x28600);
    *reinterpret_cast<void**>(options_menu::base+game::panel_pointer)=panel;
    ctor(allocate(game::cycle_size),nullptr,panel,fsaa_item_id);
    ctor(allocate(game::cycle_size),nullptr,panel,accept_item_id);
    if(collision) {void* item=allocate(game::label_size);ctor(item,nullptr,panel,0x4e);labels[item]="foreign";}
    options_menu::finish_panel(panel,nullptr,0);
    assert(navigation==4);
}
void render() {
    auto* call=options_menu::base+0x21939a;
    std::int32_t rel=0;std::memcpy(&rel,call+1,4);
    const auto fn=reinterpret_cast<bool(__fastcall*)(void*,void*)>(call+5+rel);
    assert(fn(options_menu::current_panel(),nullptr));
}
}
namespace logger { const std::filesystem::path& module_dir() {return directory;} void write(const char*,...) {} }
namespace ini { std::optional<std::string> text(const wchar_t*,const wchar_t*) {return {};} }
namespace display {
bool remastered_enabled() {return enabled;}
applied_state applied_snapshot() {copying=true;auto value=observed;copying=false;return value;}
const display_rules::options& current_options() {return display_options;}
display_rules::options read_ini_options() {return display_options;}
unsigned desktop_refresh_rate() {return 60;}
void set_frame_rate(const frame_rate_rules::cap&) {} bool set_vsync(std::optional<bool>) {return true;}
void set_run_in_background(bool) {}
void set_resolution(unsigned width,unsigned height) { display_options.width=int(width);display_options.height=int(height); }
}
int main(int argc,char** argv) {
    assert(argc>=2);const std::string mode=argc>2 ? argv[2] : "all";enabled=mode!="off";
    std::ifstream input(argv[1],std::ios::binary);std::vector<unsigned char> file((std::istreambuf_iterator<char>(input)),{});
    assert(file.size()>0x1000);
    auto u16=[&](std::size_t at){std::uint16_t n;std::memcpy(&n,file.data()+at,2);return n;};
    auto u32=[&](std::size_t at){std::uint32_t n;std::memcpy(&n,file.data()+at,4);return n;};
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,0x700000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE));assert(memory);
    const auto pe=u32(0x3c),sections=pe+24+u16(pe+20);
    for(unsigned i=0;i<u16(pe+6);++i) {const auto row=sections+40*i;std::memcpy(memory+u32(row+12),file.data()+u32(row+20),u32(row+16));}
    const auto before=std::vector<unsigned char>(memory,memory+0x700000);
    if(mode.starts_with("guard")) memory[0x219390+std::stoul(mode.substr(5))]^=1;
    options_menu::install(reinterpret_cast<HMODULE>(memory));
    if(mode.starts_with("guard")) {
        assert(!options_menu::installed());
        for(const auto* site:options_menu_rules::call_sites) assert(std::memcmp(memory+site->rva,before.data()+site->rva,5)==0);
        std::cout<<"PASS atomic MSAA "<<mode<<"\n";return 0;
    }
    assert(options_menu::installed());prepare_stubs();
    std::memcpy(memory+options_menu_rules::game::resolution_setting,"640x480",8);
    observed.device_generation=1;observed.observation_revision=1;observed.validity=display::observation_validity::ready;
    observed.source=display::observation_source::backbuffer_desc;observed.observed_backbuffer.multi_sample_type=4;
    observed.requested_profile_samples=4;
    open(mode=="collision");
    if(mode=="off") {
        assert(!items.contains(0x4e) && std::memcmp(memory+0x21939a,before.data()+0x21939a,5)==0);
    } else if(mode=="collision") {render();assert(labels[items[0x4e]]=="foreign");}
    else {
        assert(items.contains(0x4e));void* label=items[0x4e];
        assert(labels[label]=="MSAA: 4x (Remastered)");
        assert(*reinterpret_cast<int*>(static_cast<char*>(label)+0x18)==299);
        const auto unchanged=writes;render();assert(writes==unchanged && draws==1);
        // Pending FSAA, Cancel and Revert cannot claim a different applied surface.
        selection(items[6],nullptr,0);render();assert(labels[label]=="MSAA: 4x (Remastered)");
        options_menu::on_cancel();options_menu::on_revert();render();assert(labels[label]=="MSAA: 4x (Remastered)");
        // Accept must retain beta.2 resolution persistence while the new label
        // continues to describe the applied surface, not the pending choice.
        directory=std::filesystem::current_path()/"remastered-options-profile";
        std::filesystem::create_directories(directory);
        std::memcpy(memory+options_menu_rules::game::resolution_setting,"1920x1080",10);
        options_menu::on_save();assert(saves==1 && loads==2);
        assert(display_options.width==1920 && display_options.height==1080);
        const auto saved_ini=directory/L"xml2-fix.ini";
        assert(GetPrivateProfileIntW(L"Display",L"Width",0,saved_ini.c_str())==1920);
        assert(GetPrivateProfileIntW(L"Display",L"Height",0,saved_ini.c_str())==1080);
        render();assert(labels[label]=="MSAA: 4x (Remastered)");
        ++observed.observation_revision;observed.validity=display::observation_validity::lost;
        render();assert(labels[label]=="MSAA: unavailable");
        *reinterpret_cast<void**>(memory+options_menu_rules::game::panel_pointer)=nullptr;
        const auto closed_writes=writes; // stale panel marker must never touch its destroyed widget
        observed.observation_revision++; // reopening initializes from the latest snapshot
        open();assert(items[0x4e]!=label && labels[items[0x4e]]=="MSAA: unavailable");
        assert(writes>closed_writes);
    }
    std::cout<<"PASS options "<<mode<<": label/pending/close/reopen/return/navigation/UI-thread\n";
    VirtualFree(memory,0,MEM_RELEASE);
}
