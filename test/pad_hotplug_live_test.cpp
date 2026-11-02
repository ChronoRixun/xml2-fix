// The exact live failure: DI7 sees system mouse/keyboard at startup; discovery
// later sees a Bluetooth Xbox pad, but the separate gameplay input stays empty.
#define PAD_HOTPLUG_LIVE_FIXTURE
#include <Windows.h>
namespace
{
    bool fixture_has_focus = true;
    HWND fixture_foreground() { return fixture_has_focus ? reinterpret_cast<HWND>(1) : nullptr; }
    DWORD fixture_window_thread(HWND, DWORD* pid) { *pid = GetCurrentProcessId(); return 1; }
}
#define GetForegroundWindow fixture_foreground
#define GetWindowThreadProcessId fixture_window_thread
#include "../src/gamepad_fix.cpp"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId
#define main legacy_hotplug_fixture_main
#include "pad_hotplug_test.cpp"
#undef main
#include <thread>

namespace logger { void write_once(std::string_view, const char*, ...) {} }
namespace xinput_pad
{
    unsigned fixture_mask = 0, fixture_pressed = 0;
    bool starting() { return false; }
    std::vector<int> connected_indices()
    {
        std::vector<int> out;
        for (int i=0;i<4;++i) if (fixture_mask & (1u<<i)) out.push_back(i);
        return out;
    }
    bool read_raw(int i, raw_state& out)
    {
        out = {}; if (i<0 || i>=4 || !(fixture_mask & (1u<<i))) return false;
        out.buttons = (fixture_pressed & (1u<<i)) ? 0x4000 : 0; // X -> Dual Action button 1
        return true;
    }
}
namespace pad_input { void on_read(int, xinput_pad::raw_state&, bool) {} }
namespace virtual_pad
{
    int count() { return 0; }
    bool is_virtual(REFGUID) { return false; }
    HRESULT create(REFGUID, const IID*, bool, bool, void**) { return E_NOTIMPL; }
    bool enumerate(bool, bool, void*, void*) { return true; }
}
namespace
{
    pad keyboard{pad_table, 0}, keyboard2{pad_table, 0}, mouse{pad_table, 0};
    unsigned fixture_run = 0;
    unsigned fixture_enumerations = 0;
    unsigned driver_calls_on_frame = 0;
    DWORD frame_thread = GetCurrentThreadId();
    controller_scan_cache::cache* fixture_cache = nullptr;
    std::array<unsigned char,0x13000> discovery{};
    input discovery_di{input_table};
    std::array<std::array<unsigned char,0xa00>,20> configs{};
    HRESULT STDMETHODCALLTYPE live_create(void* self, REFGUID id, void** out, void* outer)
    {
        if (id.Data1!=5) return create(self,id,out,outer);
        ++static_cast<input*>(self)->creates;++pads[0].refs;*out=&pads[0];
        auto& record=gamepad_fix::device_records[&pads[0]];record.ordinal=4;
        record.instance_key.assign(reinterpret_cast<const char*>(&id),sizeof(id));
        return DI_OK;
    }
    HRESULT STDMETHODCALLTYPE live_enumerate(void* self, DWORD type, LPDIENUMDEVICESCALLBACKA cb, void* user, DWORD)
    {
        ++fixture_enumerations;
        if(fixture_cache && GetCurrentThreadId()==frame_thread) ++driver_calls_on_frame;
        check(type==4,"native gameplay/discovery query lists controllers only, not system mouse/keyboard");
        for(auto id:static_cast<input*>(self)->attached)
        {
            DIDEVICEINSTANCEA item{};item.dwSize=sizeof(item);item.guidInstance=guid(id);
            item.guidProduct.Data1=0x0b22045e;item.dwDevType=0x10215;
            std::snprintf(item.tszInstanceName,MAX_PATH,"Invented Bluetooth Xbox %u",id);
            if(cb(&item,user)==DIENUM_STOP) break;
        }
        return DI_OK;
    }
    ULONG STDMETHODCALLTYPE input_ref(void*) { return 1; }
    HRESULT STDMETHODCALLTYPE cached_enumerate(void* self, DWORD type, LPDIENUMDEVICESCALLBACKA cb, void* user, DWORD flags)
    {
        const auto list=fixture_cache->request({self,8,false,type,flags},
            reinterpret_cast<controller_scan_cache::enumerate_t>(&live_enumerate),GetTickCount64());
        check(list!=nullptr,"primary input replays a published complete cache list");
        for(const auto& item:list->devices)
            if(cb(reinterpret_cast<LPCDIDEVICEINSTANCEA>(item.bytes.data()),user)==DIENUM_STOP) break;
        return list->result;
    }
    void setup_live()
    {
        pad_table[2]=reinterpret_cast<void*>(&release); pad_table[4]=reinterpret_cast<void*>(&objects);
        pad_table[7]=reinterpret_cast<void*>(&poll); pad_table[9]=reinterpret_cast<void*>(&gamepad_fix::get_device_state);
        pad_table[11]=reinterpret_cast<void*>(&format); pad_table[13]=reinterpret_cast<void*>(&coop); pad_table[25]=reinterpret_cast<void*>(&poll);
        input_table[3]=reinterpret_cast<void*>(&live_create); input_table[4]=reinterpret_cast<void*>(&live_enumerate);
        gamepad_fix::vtables[pad_table].originals[9]=reinterpret_cast<void*>(&state);
        reset_manager();
        auto* key=(++fixture_run%2) ? &keyboard : &keyboard2;
        field<void*>(manager.data(),4)=key; field<void*>(manager.data(),8)=&mouse;
        discovery.fill(0); discovery_di.attached.clear(); discovery_di.creates=0;
        field<void*>(discovery.data(),4)=&keyboard; field<void*>(discovery.data(),8)=&mouse;
        field<void*>(discovery.data(),0x27e4)=&discovery_di;
        *reinterpret_cast<void**>(at(0xa6abfc))=manager.data();
        *reinterpret_cast<void**>(at(0xa6a7e4))=discovery.data();
        for(unsigned i=0;i<4;++i)
        {
            // The saved Xbox instance is absent at launch (registry GUID loaded
            // by 626E80, before the startup scan at 61BC73).
            field<GUID>(manager.data(),0x27e8+i*16)=guid(i+1);
            std::snprintf(reinterpret_cast<char*>(at(0xa6ac08+i*0x25)),0x25,"%08x-0000-0000-0000-000000000000",i+1);
            gamepad_fix::device_records[&pads[i]]={};
            auto& r=gamepad_fix::device_records[&pads[i]];r.spoofed=true;r.ordinal=i;r.axes_loaded=true;
            const auto id=guid(i+1);r.instance_key.assign(reinterpret_cast<const char*>(&id),sizeof(id));
        }
        for(unsigned row=0;row<5;++row) for(unsigned player=0;player<4;++player)
        {
            auto& c=configs[row*4+player];c.fill(0);
            *reinterpret_cast<void**>(at(0xa68f40+(row*4+player)*4))=c.data();
            auto* map=c.data()+0x18;field<unsigned>(map,0)=42;
            for(unsigned action=0;action<42;++action)
            {
                field<unsigned>(map,4+action*48)=player+3;
                field<unsigned>(map,8+action*48)=21+action%12;
                if(player==0) { field<unsigned>(map,16+action*48)=1;field<unsigned>(map,20+action*48)=17; }
            }
            field<unsigned>(map,0x964)=player==0 ? 0 : player+3; // exercise stale active-device metadata too
        }
        xinput_pad::fixture_mask=xinput_pad::fixture_pressed=0;
        fixture_has_focus=true;
        gamepad_fix::device_records[&keyboard]={}; gamepad_fix::device_records[&mouse]={};
    }
    void frame()
    {
        BYTE keys[256]{};
        check(SUCCEEDED(gamepad_fix::get_device_state(field<void*>(manager.data(),4),sizeof(keys),keys)),"main DI8 keyboard polled");
        read_frame();
    }
    bool player_down(unsigned player, unsigned row=0)
    {
        const auto d=field<unsigned>(configs[row*4+player].data()+0x18,4);
        return d>=3 && d<13 && manager[0x4f0+(d-3)*0x110+DIJOFS_BUTTON(0)]==0x80;
    }
    void maps_match(const std::array<unsigned,4>& expected)
    {
        for(unsigned row=0;row<5;++row) for(unsigned p=0;p<4;++p)
        {
            auto* map=configs[row*4+p].data()+0x18;
            check(field<unsigned>(map,0x964)==expected[p],"all five rows have rebuilt active pad metadata");
            for(unsigned a=0;a<42;++a)
            {
                check(field<unsigned>(map,4+a*48)==expected[p],"actual device index propagated to every live action row");
                check(field<unsigned>(map,8+a*48)==21+a%12,"custom control IDs preserved");
                if(p==0) check(field<unsigned>(map,16+a*48)==1 && field<unsigned>(map,20+a*48)==17,"keyboard bindings preserved");
            }
        }
    }
    bool logged(const char* text)
    {
        for(const auto& line:logger::lines) if(line.find(text)!=std::string::npos) return true;
        return false;
    }
    BOOL CALLBACK collect_system(LPCDIDEVICEINSTANCEW info, void* p)
    {
        static_cast<std::vector<DWORD>*>(p)->push_back(info->dwDevType);return DIENUM_CONTINUE;
    }
    void system_startup()
    {
        std::vector<DWORD> seen;
        gamepad_fix::enum_context ctx{collect_system,&seen,false};
        for(auto pair: {std::pair{0x6f1d2b60u,0x102u},std::pair{0x6f1d2b61u,0x403u}})
        {
            DIDEVICEINSTANCEA item{};item.dwSize=sizeof(item);item.guidProduct.Data1=pair.first;item.dwDevType=pair.second;
            gamepad_fix::enum_callback(reinterpret_cast<LPCDIDEVICEINSTANCEW>(&item),&ctx);
        }
        check(seen==std::vector<DWORD>{0x102,0x403},"DI7 startup is exactly system mouse + keyboard");
    }
}
int main(int argc,char** argv)
{
    if(argc<2) return 2;
    const bool on=configure_latepads(argc,argv);
    load(argv[1]);at(0x62887d)[0]=0xc3;jump(0x672170,reinterpret_cast<const void*>(&copy));
    setup_live();system_startup();scan();
    check(!device(0),"gameplay has no startup pad");
    pad_hotplug::install(reinterpret_cast<HMODULE>(0x400000));
    if (!on)
    {
        check(at(0x61862a)[0]==0x39,"LatePads=0 leaves Options bridge absent");
        const auto primary_before=manager, discovery_before=discovery;
        const auto maps_before=configs;
        const auto scans_before=fixture_enumerations;
        const auto records_before=gamepad_fix::source_slots;
        di.attached=discovery_di.attached={1};xinput_pad::fixture_mask=xinput_pad::fixture_pressed=1;
        // A disabled callback must not dereference even an invalid device, or
        // touch the primary/discovery input, any of the five maps or XInput leases.
        for(unsigned i=0;i<3;++i)
        {
            pad_hotplug::on_keyboard_read(reinterpret_cast<void*>(1),256);
            pad_hotplug::on_keyboard_read(field<void*>(manager.data(),4),256);
            Sleep(510);
        }
        check(manager==primary_before && discovery==discovery_before,"OFF callback makes no CInput/state/GUID writes");
        check(configs==maps_before,"OFF callback makes no five-row binding/action/metadata writes");
        check(fixture_enumerations==scans_before,"OFF callback does not scan");
        check(gamepad_fix::source_slots==records_before && !gamepad_fix::topology_known,"OFF callback leaves XInput topology/leases unchanged");
        frame();
        check(!device(0) && !player_down(0) && configs==maps_before,"OFF production keyboard hook retains stock missing primary pad and saved maps");
        std::printf("pad hotplug live OFF: %u checks PASS (no refresh, map, state or XInput topology writes)\n",checks);
        return 0;
    }
    frame();
    // Only the discovery manager gets the recurring scan in the unmodified game.
    di.attached=discovery_di.attached={1};xinput_pad::fixture_mask=xinput_pad::fixture_pressed=1;
    using scan_t=void(__thiscall*)(void*,bool);
    reinterpret_cast<scan_t>(0x628e20)(discovery.data(),true);
    check(field<pad*>(discovery.data(),0xc)==&pads[0],"late Bluetooth pad discovered independently");
    Sleep(1050);frame();
    check(device(0)!=nullptr,"late arrival must reach gameplay manager, not just discovery manager");
    DIJOYSTATE2 synthesized{};
    check(SUCCEEDED(gamepad_fix::get_device_state(&pads[0],sizeof(synthesized),&synthesized)) && synthesized.rgbButtons[0]==0x80,
          "late pad state goes through production XInput -> Logitech synthesis");
    const auto* play_map=configs[0].data()+0x18;
    const auto dev=field<unsigned>(const_cast<unsigned char*>(play_map),4);
    check(dev>=3 && dev<13 && manager[0x4f0+(dev-3)*0x110+DIJOFS_BUTTON(0)]==0x80,"player 1's actual binding reads late XInput pad");
    check(field<unsigned>(const_cast<unsigned char*>(play_map),0x964)==dev,"player 1 active pad device is rebuilt");
    di.attached=discovery_di.attached={1,2};xinput_pad::fixture_mask=3;xinput_pad::fixture_pressed=2;
    Sleep(550);frame();
    check(!player_down(0) && player_down(1),"second late pad reaches only player 2 through XInput synthesis");
    for(unsigned row=0;row<5;++row) field<unsigned>(configs[row*4+1].data()+0x18,12)=0x3f800000; // held action value
    di.attached=discovery_di.attached={2};xinput_pad::fixture_mask=xinput_pad::fixture_pressed=2;
    Sleep(550);frame();
    check(!player_down(0) && player_down(1),"removing pad 1 cannot silence or steal still-connected pad 2");
    check(manager[0xf90+DIJOFS_BUTTON(0)]==0x80,"compacting a still-held pad must not manufacture a new button-down edge");
    for(unsigned row=0;row<5;++row) check(field<unsigned>(configs[row*4+1].data()+0x18,12)==0x3f800000,"same physical pad retains its held action value across index changes");
    maps_match({4,3,5,6});
    di.attached=discovery_di.attached={2,1};xinput_pad::fixture_mask=3;xinput_pad::fixture_pressed=1;
    Sleep(550);frame();
    check(player_down(0) && !player_down(1),"re-added pad 1 uses its actual compacted index without stealing player 2");
    maps_match({4,3,5,6});
    fixture_has_focus=false;frame();check(!player_down(0) && !player_down(1),"unfocused gameplay receives neutral XInput state");
    fixture_has_focus=true;frame();check(player_down(0),"physical input resumes when focus returns");
    for(unsigned row=0;row<5;++row) field<unsigned>(configs[row*4].data()+0x18,12)=0x3f800000;
    di.attached=discovery_di.attached={2};xinput_pad::fixture_mask=xinput_pad::fixture_pressed=2;Sleep(550);frame();
    for(unsigned row=0;row<5;++row) check(field<unsigned>(configs[row*4].data()+0x18,12)==0,"disconnection releases cached actions even when the reserved number stays unchanged");
    di.attached=discovery_di.attached={2,5};xinput_pad::fixture_mask=3;xinput_pad::fixture_pressed=1;Sleep(550);frame();
    check(player_down(0) && !player_down(1),"changed Bluetooth GUID reclaims a missing player and a free physical XInput slot");
    maps_match({4,3,5,6});
    check(logged("startup map player 1 saved slot 1"),"startup saved-slot/binding diagnostics are present");
    check(logged("arrival bound player 1") && logged("actual device index 1, binding device 4"),"arrival logs name player and actual device index");

    // Primary input exists at launch, rather than relying on any Options handler.
    setup_live();di.attached={1};xinput_pad::fixture_mask=xinput_pad::fixture_pressed=1;scan();frame();
    check(player_down(0),"pad present at launch still works");maps_match({3,4,5,6});
    // Saved pad 2 alone: native compaction moves it from slot 1 to slot 0.
    setup_live();di.attached={2};xinput_pad::fixture_mask=xinput_pad::fixture_pressed=2;scan();frame();
    check(!player_down(0) && player_down(1),"saved pad 2 present at launch keeps player 2 after native compaction");
    maps_match({4,3,5,6});
    // A sparse saved slot must be rebound on arrival to its actual index,
    // rather than assuming the saved binding number is the physical ordinal.
    setup_live();
    for(unsigned i=0;i<4;++i)
    {
        field<GUID>(manager.data(),0x27e8+i*16)=guid(i==3?1:i==0?0:i+1);
        std::snprintf(reinterpret_cast<char*>(at(0xa6ac08+i*0x25)),0x25,"%08x-0000-0000-0000-000000000000",i==3?1:i==0?0:i+1);
    }
    for(unsigned row=0;row<5;++row) for(unsigned a=0;a<42;++a)
    {
        field<unsigned>(configs[row*4].data()+0x18,4+a*48)=6;
        field<unsigned>(configs[row*4+3].data()+0x18,4+a*48)=0;
    }
    scan();frame();
    check(field<unsigned>(configs[0].data()+0x18,4)==6,"unoccupied saved binding is kept until an actual arrival");
    di.attached={1};xinput_pad::fixture_mask=xinput_pad::fixture_pressed=1;Sleep(550);frame();
    check(player_down(0) && field<unsigned>(configs[0].data()+0x18,4)==3,"saved slot 4 binds on arrival to actual device 3");
    maps_match({3,4,5,0});
    check(logged("arrival bound player 1 saved slot 4") && logged("binding device 3 (was 6)"),"arrival log proves actual-index rebinding of a sparse saved slot");
    // Compose primary-input routing with actual asynchronous cache publication.
    // Driver enumeration happens only on the owned worker, and the frame thread
    // runs the real retail callbacks against the complete published snapshots.
    setup_live();
    {
        controller_scan_cache::cache cache;fixture_cache=&cache;driver_calls_on_frame=0;
        input_table[1]=reinterpret_cast<void*>(&input_ref);input_table[2]=reinterpret_cast<void*>(&input_ref);
        const controller_scan_cache::key key{&di,8,false,4,1};
        cache.request(key,reinterpret_cast<controller_scan_cache::enumerate_t>(&live_enumerate),GetTickCount64());
        std::thread initial([&]{cache.tick(GetTickCount64(),controller_scan_cache::topology{});});initial.join();
        input_table[4]=reinterpret_cast<void*>(&cached_enumerate);
        scan();frame();check(!device(0),"cached primary startup list is empty");
        di.attached={1};xinput_pad::fixture_mask=xinput_pad::fixture_pressed=1;
        std::thread arrival([&]{cache.tick(GetTickCount64()+250,controller_scan_cache::topology{{1,RIM_TYPEHID,L"invented-bt-pad"}});});arrival.join();
        Sleep(550);frame();
        check(player_down(0),"async cache publication feeds the primary arrival and XInput path");
        check(driver_calls_on_frame==0 && cache.counts()[0]==2,"no real enumeration on frame thread with cached primary refresh");
        input_table[4]=reinterpret_cast<void*>(&live_enumerate);fixture_cache=nullptr;
    }
    for(const auto bad_count:{51u,64u,UINT32_MAX})
    {
        auto* map=configs[0].data()+0x18;field<unsigned>(map,0)=bad_count;
        const auto before=fixture_enumerations;Sleep(550);
        BYTE keys[256]{};gamepad_fix::get_device_state(field<void*>(manager.data(),4),sizeof(keys),keys);
        check(fixture_enumerations==before,"malformed action count must be refused before scanning fixed-capacity maps");
        field<unsigned>(map,0)=42;
    }
    gamepad_fix::topology_known=false;gamepad_fix::source_slots.clear();gamepad_fix::attached_instances.clear();
    gamepad_fix::device_record first{},replacement{};first.ordinal=replacement.ordinal=0;
    const auto first_id=guid(900),replacement_id=guid(901);
    first.instance_key.assign(reinterpret_cast<const char*>(&first_id),sizeof(first_id));
    replacement.instance_key.assign(reinterpret_cast<const char*>(&replacement_id),sizeof(replacement_id));
    check(gamepad_fix::source_for(first,{0})==0,"unvalidated topology preserves legacy source selection");
    check(gamepad_fix::source_for(replacement,{0})==0,"no stable lease is retained before gameplay topology is known");
    gamepad_fix::source_slots.clear();gamepad_fix::update_attached(&first_id,1);
    check(gamepad_fix::source_for(first,{0})==0,"known physical GUID leases its first XInput index");
    gamepad_fix::update_attached(&first_id,1); // fast reconnect missed by DirectInput topology snapshots
    check(gamepad_fix::source_for(first,{1})==1,"same GUID reconnecting at another XInput index recovers without a sampled GUID removal");
    for(const auto& line:logger::lines) if(line.starts_with("input:") || line.starts_with("dinput: physical")) std::puts(line.c_str());
    std::printf("live startup hotplug: %u checks PASS (DI7 mouse/keyboard, separate managers, late Bluetooth XInput)\n",checks);
}
