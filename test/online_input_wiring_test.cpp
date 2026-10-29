// Compile the production translation unit in this fixture TU, so both real
// keyboard delivery branches can run with fake COM devices and no pipe worker.
#define DIRECTINPUT_VERSION 0x0800
#include <Windows.h>
#include <dinput.h>
#include <array>
#include <cassert>
#include <cstdio>
HWND WINAPI fixture_foreground();
DWORD WINAPI fixture_window_process(HWND,LPDWORD);
#define GetForegroundWindow fixture_foreground
#define GetWindowThreadProcessId fixture_window_process
#include "../src/test_input.cpp"
#undef GetForegroundWindow
#undef GetWindowThreadProcessId

namespace {
bool menus=false,foreground=true,isolated=false;
int physical_reads=0; DWORD cooperative_flags=0;
int delivered=0,resets=0,mode_keys=0,samples=0,pumps=0,lost=0,watch=0;
HRESULT read_result=DI_OK;
std::array<unsigned char,256> physical{},observed{};
std::array<void*,20> di_vt{},keyboard_vt{};
void** di_object=di_vt.data();
void** keyboard_object=keyboard_vt.data();
void** other_object=keyboard_vt.data();
HRESULT STDMETHODCALLTYPE native_state(void*,DWORD size,LPVOID data) {
    ++physical_reads;
    if(data) std::memcpy(data,physical.data(),std::min<DWORD>(size,DWORD(physical.size())));
    return read_result;
}
HRESULT STDMETHODCALLTYPE native_level(void*,HWND,DWORD flags) { cooperative_flags=flags;return DI_OK; }
HRESULT STDMETHODCALLTYPE native_create(void*,REFGUID guid,void** device,LPUNKNOWN) {
    *device=IsEqualGUID(guid,GUID_SysKeyboard) ? &keyboard_object : &other_object;return DI_OK;
}
}
HWND WINAPI fixture_foreground() { return reinterpret_cast<HWND>(1); }
DWORD WINAPI fixture_window_process(HWND,LPDWORD process) { *process=GetCurrentProcessId()+(foreground ? 0 : 1);return GetCurrentThreadId(); }
namespace online_menus {
bool installed() { return menus; }
void keyboard(unsigned char* state) {
    if(!state) { ++resets;return; }
    ++delivered;std::copy_n(state,observed.size(),observed.begin());state[0x1c]=0;
}
}
namespace logger { void write(const char*,...) {} void write_once(std::string_view,const char*,...) {} }
namespace test_mode {
bool hotkeys_enabled() { return false; }
void keyboard(const unsigned char*) { ++mode_keys; }
void input_lost() { ++lost; }
void watch_window(HWND) { ++watch; }
bool console_frame_hook_matches() { return false; }
}
namespace test_state { void sample() { ++samples; } }
// NoActivate starts off so Menus-only installation is tested, then varies in the agent matrix.
namespace no_activate { bool enabled() { return isolated; } }
namespace save_format { void pump() { ++pumps; } }
// The included TU also defines the automation service. These dependencies are
// deliberately unreachable here; an accidental install/command/capture fails.
#define UNUSED_TEXT(ns,fn) namespace ns { std::string fn() { assert(false);return {}; } }
UNUSED_TEXT(anim_enums,status)
UNUSED_TEXT(pool_counters,json)
UNUSED_TEXT(e4_observe,json)
UNUSED_TEXT(test_mode,status)
UNUSED_TEXT(balance,status)
UNUSED_TEXT(frame_rate,describe)
UNUSED_TEXT(limits,status)
UNUSED_TEXT(model_pools,status)
UNUSED_TEXT(break_rule,status)
UNUSED_TEXT(placement_crush,status)
UNUSED_TEXT(world_impact,status)
UNUSED_TEXT(double_heal,status)
UNUSED_TEXT(world_backfaces,status)
UNUSED_TEXT(combo_heads,status)
UNUSED_TEXT(pack_amount,status)
UNUSED_TEXT(xml1_cling,status)
UNUSED_TEXT(xml1_rogue_decide,status)
UNUSED_TEXT(xml1_grab_carry,status)
UNUSED_TEXT(lift_rule,status)
UNUSED_TEXT(geometry_sharing,status)
UNUSED_TEXT(ladder_paths,status)
UNUSED_TEXT(pad_input,status)
UNUSED_TEXT(save_format,status)
UNUSED_TEXT(entity_capacity,status)
UNUSED_TEXT(address_space,status)
UNUSED_TEXT(native_save_layout,status)
UNUSED_TEXT(xml1_economy,status)
UNUSED_TEXT(xml1_economy::roller,recent_rolls)
#undef UNUSED_TEXT
#define UNUSED_PIPE(ns,fn) namespace ns { std::string fn(const std::string&) { assert(false);return {}; } }
UNUSED_PIPE(test_state,reply)
UNUSED_PIPE(save_format,pipe)
UNUSED_PIPE(address_space,pipe)
UNUSED_PIPE(xml1_economy::digest,pipe)
#undef UNUSED_PIPE
namespace ini {
std::optional<std::string> text(const wchar_t*,const wchar_t*) { assert(false);return {}; }
bool flag(const wchar_t*,const wchar_t*,bool) { assert(false);return false; }
}
namespace frame_rate { unsigned measured_fps_x10() { assert(false);return 0; } }
namespace display {
void set_frame_hook(void(*)(void*)) { assert(false); }
void disable_multisampling(const char*) { assert(false); }
void show_without_focus(const char*) { assert(false); }
void enable_test_pointer() { assert(false); }
std::string test_pointer(UINT,int,int) { assert(false);return {}; }
std::string test_char(unsigned) { assert(false);return {}; }
}
namespace frame_capture {
std::string read_back_buffer(void*,frame&) { assert(false);return {}; }
std::string save(const frame&,const std::filesystem::path&) { assert(false);return {}; }
}
namespace pad_input {
void enable() { assert(false); }
void release_all() { assert(false); }
std::string handle(const pad_input_rules::pad_command&) { assert(false);return {}; }
}
namespace entity_capacity { bool retire_slot(std::uint32_t) { assert(false);return false; } }
namespace xml1_economy::digest { void enable() { assert(false); } }

int main() {
    di_vt[3]=reinterpret_cast<void*>(&native_create);keyboard_vt[9]=reinterpret_cast<void*>(&native_state);keyboard_vt[13]=reinterpret_cast<void*>(&native_level);
    test_input::hook_direct_input8(&di_object);assert(di_vt[3]==reinterpret_cast<void*>(&native_create)); // Menus OFF, no pipe/hotkeys
    menus=true;test_input::hook_direct_input8(&di_object);assert(di_vt[3]!=reinterpret_cast<void*>(&native_create));
    auto create=reinterpret_cast<test_input::create_device_t>(di_vt[3]);void* keyboard=nullptr;
    assert(SUCCEEDED(create(&di_object,GUID_SysKeyboard,&keyboard,nullptr)) && keyboard==&keyboard_object);
    assert(keyboard_vt[9]!=reinterpret_cast<void*>(&native_state));
    auto read=reinterpret_cast<test_input::get_device_state_t>(keyboard_vt[9]);std::array<unsigned char,256> state{};
    physical[0x1e]=physical[0x1c]=128;
    assert(SUCCEEDED(read(keyboard,256,state.data())) && delivered==1 && observed[0x1e]==128 && !state[0x1c]);
    foreground=false;read(keyboard,256,state.data());assert(delivered==2 && observed[0x1e]==0);
    read_result=DIERR_INPUTLOST;read(keyboard,256,state.data());assert(resets==1 && lost==1 && delivered==2);read_result=DI_OK;
    read(keyboard,128,state.data());read(&other_object,256,state.data());read(keyboard,256,nullptr);assert(delivered==2 && resets==1);
    auto level=reinterpret_cast<test_input::set_cooperative_level_t>(keyboard_vt[13]);level(keyboard,reinterpret_cast<HWND>(1),DISCL_FOREGROUND);assert(watch==1);
    // Actual COM hooks: NoActivate agent copies deliver only pipe keys, with no physical read,
    // whether foreground or background, even when the native device is unacquired.
    test_input::read_event=CreateEventW(nullptr,FALSE,FALSE,nullptr);assert(test_input::read_event);
    for (const bool pipe : {false,true}) for (const bool noactivate : {false,true}) for (const bool focus : {false,true}) {
        test_input::enabled=pipe;isolated=noactivate;foreground=focus;
        test_input::keys.release_all();test_input::keys.press(0x30,GetTickCount64()+10000);
        const bool synthetic=noactivate || (pipe&&!focus);
        physical_reads=0;read_result=synthetic ? DIERR_NOTACQUIRED : DI_OK;
        assert(SUCCEEDED(read(keyboard,256,state.data())));
        assert(physical_reads==(synthetic?0:1));
        assert(observed[0x1e]==((!noactivate&&focus)?128:0));
        assert(observed[0x30]==(pipe?128:0));
        if(pipe) assert(WaitForSingleObject(test_input::read_event,0)==WAIT_OBJECT_0);
        constexpr DWORD flags=DISCL_FOREGROUND|DISCL_NONEXCLUSIVE|DISCL_NOWINKEY;
        level(keyboard,reinterpret_cast<HWND>(1),flags);
        assert(cooperative_flags==flags && !(cooperative_flags&DISCL_BACKGROUND));
        // Unrelated device, size and null calls still chain to the native device.
        read(keyboard,128,state.data());read(&other_object,256,state.data());read(keyboard,256,nullptr);
        assert(physical_reads==(synthetic?3:4));
    }
    // Focused player kit + pipe retains native failure/reacquisition behavior too.
    test_input::enabled=true;isolated=false;foreground=true;read_result=DIERR_INPUTLOST;
    physical_reads=0;const auto previous_lost=lost;
    assert(read(keyboard,256,state.data())==DIERR_INPUTLOST && physical_reads==1 && lost==previous_lost+1);
    test_input::enabled=false;isolated=false;test_input::keys.release_all();CloseHandle(test_input::read_event);test_input::read_event=nullptr;
    std::puts("DirectInput production wiring PASS: real COM chain; 8 pipe/noactivate/focus cases; no NoActivate physical reads; original flags; unfocused pipe survives unacquired device; player keyboard including focused pipe and unrelated reads preserved");
}
