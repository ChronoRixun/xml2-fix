// Production display hooks, fake COM objects only. No window, registry or game entry point.
#include "../src/centered_ui_trace.hpp"
#ifdef CENTERED_UI_BASELINE_DISPLAY
#include CENTERED_UI_BASELINE_DISPLAY
#elif defined(REMASTERED_BASELINE_DEVICE)
#include "../../baseline-source/display.cpp"
#else
#include "../src/display.cpp"
#endif
#include <cassert>
#include <iostream>

namespace texture_filtering {
void configure(const remastered_rules::settings&) {}
void device_ready(void*,std::uint64_t) {} void device_unavailable(void*) {} void device_recovered(void*) {}
}
namespace ini {
std::optional<std::string> text(const wchar_t*, const wchar_t*) { return {}; }
int number(const wchar_t*, const wchar_t*, int fallback) { return fallback; }
bool flag(const wchar_t*, const wchar_t*, bool fallback) { return fallback; }
}
namespace logger { void write(const char*,...) {} void write_once(std::string_view,const char*,...) {} }
bool test_isolation=false;
std::vector<int> present_events;
namespace no_activate {
bool enabled() { return test_isolation; }
void presentation(d3d8::present_parameters& pp,const char*) {
    if(test_isolation) {pp.windowed=TRUE;pp.fullscreen_refresh_rate=0;pp.fullscreen_presentation_interval=0;}
}
}
// This fixture exercises COM hooks; no online text editor is open.
namespace online_menus {
bool text_entry_active() { return false; }
bool text_entry_character(unsigned,unsigned) { return false; }
bool installed() { return false; }
}
namespace options_menu { void install(HMODULE) {} bool installed() { return false; } }
namespace frame_rate {
void install(HMODULE,const frame_rate_rules::cap&,unsigned,bool) {}
void retarget(const frame_rate_rules::cap&,bool) {} bool paces() { return false; }
void on_present() { present_events.push_back(5); }
}
namespace display_settings { void install(HMODULE,bool) {} int requested_fsaa_index() { return 4; } }
namespace resolution_list { std::size_t install(HMODULE,display_rules::resolution_list) { return 20; } }
namespace pool_counters { bool enabled() { return false; } void sample() {} }
namespace script_pools { void sample() {} }
namespace e4_observe { bool enabled() { return false; } void sample() {} }
namespace iat_hook { void* hook(HMODULE,const char*,const char*,WORD,void*) { return nullptr; } }

namespace {
int resets=0;
struct fake_surface { void** vtable; d3d8::surface_desc desc{}; int refs=0; HRESULT hr=S_OK; };
struct fake_device { void** vtable; fake_surface back,depth; HRESULT back_hr=S_OK,depth_hr=S_OK; unsigned back_queries=0; };
ULONG STDMETHODCALLTYPE release_surface(void* p) { auto& s=*static_cast<fake_surface*>(p); assert(s.refs==1); return --s.refs; }
HRESULT STDMETHODCALLTYPE get_desc(void* p,d3d8::surface_desc* out) { auto& s=*static_cast<fake_surface*>(p); *out=s.desc; return s.hr; }
HRESULT STDMETHODCALLTYPE get_back(void* p,UINT index,DWORD type,void** out) {
    auto& d=*static_cast<fake_device*>(p); assert(index==0 && type==0);++d.back_queries;
    *out=&d.back; ++d.back.refs; return d.back_hr;
}
HRESULT STDMETHODCALLTYPE get_depth(void* p,void** out) { auto& d=*static_cast<fake_device*>(p); *out=&d.depth; ++d.depth.refs; return d.depth_hr; }
HRESULT STDMETHODCALLTYPE reset_device(void* p,d3d8::present_parameters* pp) {
    auto& d=*static_cast<fake_device*>(p); assert(d.back.refs==0 && d.depth.refs==0);
    ++resets; pp->back_buffer_width=800; pp->back_buffer_height=600;
    return S_OK;
}
HRESULT STDMETHODCALLTYPE cooperative(void*) { return S_OK; }
#ifndef REMASTERED_BASELINE_DEVICE
#ifndef CENTERED_UI_BASELINE_DISPLAY
HRESULT present_result=S_OK;
HRESULT STDMETHODCALLTYPE native_present(void*,const RECT*,const RECT*,HWND,const void*) {
    present_events.push_back(4);return present_result;
}
void present_composition_tests() {
    using namespace display;
    // Both beta.2 overlays precede capture; linked pacing runs once, only after
    // the original Present succeeds. Remastered must preserve this ordering.
    std::array<void*,16> vt{};
    vt[15]=reinterpret_cast<void*>(&native_present);
    void** object=vt.data();device=&object;opts={};stock_fallback=false;
    observations.clear();com_originals.clear();present_hooked=false;
    overlay_hook=+[](void*) {present_events.push_back(1);};
    network_overlay_hook=+[](void*) {present_events.push_back(2);};
    frame_hook=+[](void*) {present_events.push_back(3);};
    for(bool enabled:{false,true}) {
        profile.enabled=enabled;
        hook_present("combined beta.2/Remastered fixture");
        assert(present_hooked);
        for(HRESULT result:{S_OK,d3d8::err_device_lost}) {
            present_result=result;present_events.clear();
            assert(d3d8::method<d3d8::present_t>(device,15)(device,nullptr,nullptr,nullptr,nullptr)==result);
            const std::vector<int> wanted=result==S_OK ? std::vector<int>{1,2,3,4,5} : std::vector<int>{1,2,3,4};
            assert(present_events==wanted);
        }
    }
    overlay_hook=nullptr;network_overlay_hook=nullptr;frame_hook=nullptr;
    device=nullptr;present_hooked=false;com_originals.clear();profile={};
}
#endif
#endif
void stock_observation_regression() {
    using namespace display;
    std::array<void*,9> surface_vt{};surface_vt[2]=reinterpret_cast<void*>(&release_surface);
    std::array<void*,34> vt{};vt[16]=reinterpret_cast<void*>(&get_back);
    fake_device d{vt.data(),{surface_vt.data()},{surface_vt.data()}};d.back_hr=E_FAIL;
    stock_fallback=true;real_reset=&reset_device;d3d8::present_parameters pp{};
    assert(hooked_reset(&d,&pp)==S_OK);
    assert(d.back_queries==1 && d.back.refs==0); // real stock-fallback reset must be observed
}
#ifndef REMASTERED_BASELINE_DEVICE
void observation_tests() {
    using namespace display;
    std::array<void*,9> surface_vt{};
    surface_vt[2]=reinterpret_cast<void*>(&release_surface);surface_vt[8]=reinterpret_cast<void*>(&get_desc);
    std::array<void*,34> vt{};vt[16]=reinterpret_cast<void*>(&get_back);vt[33]=reinterpret_cast<void*>(&get_depth);
    fake_device d{vt.data(),{surface_vt.data()},{surface_vt.data()}};
    applied_state state; state.device_generation=1;state.successful_pp.multi_sample_type=8;
    state.successful_pp.enable_auto_depth_stencil=TRUE;
    d.back.desc={22,1,0,0,0,4,1920,1080};d.depth.desc=d.back.desc;
    observe_applied(&d,state);
    assert(state.validity==observation_validity::ready && state.source==observation_source::backbuffer_desc);
    assert(state.observed_backbuffer.multi_sample_type==4 && state.depth_matches);
    assert(msaa_status(state)=="MSAA applied: 4x");assert(d.back.refs==0 && d.depth.refs==0);
    d.depth.desc.multi_sample_type=2;observe_applied(&d,state);assert(!state.depth_matches);
    d.back.hr=E_FAIL;observe_applied(&d,state);assert(msaa_status(state)=="MSAA: unknown");assert(d.back.refs==0 && d.depth.refs==0);
    d.back.hr=S_OK;d.back_hr=E_FAIL;observe_applied(&d,state);assert(msaa_status(state)=="MSAA: unknown");assert(d.back.refs==0);
    d.back_hr=S_OK;observe_applied(&d,state);
    state.requested_profile_samples=8;assert(msaa_status(state)=="MSAA: 4x (Remastered)");
    state.observed_backbuffer.multi_sample_type=0;assert(msaa_status(state)=="MSAA: Off (requested 8x)");
    state.capture_constraint=true;assert(msaa_status(state)=="MSAA: Off (capture)");
    for(unsigned n=0;n<=17;++n) {
        state.observed_backbuffer.multi_sample_type=n;
        const auto text=msaa_status(state);assert(text.size()<=25);
        if(n==1 || n==17) assert(text=="MSAA: unknown");
    }
    state.validity=observation_validity::lost;assert(msaa_status(state)=="MSAA: unavailable");
    // Every actual successful reset, including stock fallback, reobserves and releases surfaces.
    stock_fallback=true;real_reset=&reset_device;d3d=nullptr;profile={};
    d3d8::present_parameters pp{};pp.enable_auto_depth_stencil=TRUE;
    assert(hooked_reset(&d,&pp)==S_OK);const auto first=applied_snapshot();
    assert(first.successful_pp.back_buffer_width==800 && first.observed_backbuffer.width==1920);
    assert(hooked_reset(&d,&pp)==S_OK);assert(applied_snapshot().device_generation>first.device_generation);
    const auto generation=applied_snapshot().device_generation;
    publish_failure(&d,d3d8::err_device_lost);assert(applied_snapshot().device_generation==generation);
    assert(msaa_status(applied_snapshot())=="MSAA: unavailable");
    d.back_hr=S_OK;recover_observation(&d);
    assert(applied_snapshot().validity==observation_validity::ready && applied_snapshot().device_generation==generation);
    // A synthetic unfocused pause must never mark an observed surface lost.
    stock_fallback=false;real_test_cooperative_level=&cooperative;
    opts.window_mode=display_rules::mode::windowed;opts.run_in_background=false;
    const auto revision=applied_snapshot().observation_revision;
    assert(hooked_test_cooperative_level(&d)==d3d8::err_device_lost && applied_snapshot().observation_revision==revision);
    opts.run_in_background=true;
    // The unchanged-reset shortcut keeps generation and never queries a new surface.
    stock_fallback=false;opts.window_mode=display_rules::mode::windowed;have_applied=true;
    applied=pp;real_test_cooperative_level=&cooperative;
    d.back_hr=E_FAIL;const int before=resets;const auto queries=d.back_queries;
    assert(hooked_reset(&d,&pp)==S_OK && resets==before && applied_snapshot().device_generation==generation && d.back_queries==queries);
}
void policy_tests() {
    using namespace remastered_rules;
    for(int i=0;i<=4;++i) assert(saved_samples(i)==(i==0 ? 0 : 2*i));
    assert(!choice("4junk",{0,2,4,6,8}) && !choice("+4",{0,2,4,6,8}) && !choice("04",{0,2,4,6,8}));
    d3d8::present_parameters pp{};pp.back_buffer_format=22;pp.auto_depth_stencil_format=75;
    pp.enable_auto_depth_stencil=TRUE;pp.swap_effect=1;pp.flags=0x81;
    for(unsigned n:{2u,4u,6u,8u}) {
        pp.multi_sample_type=n;pp.flags=0x81;std::string why;
        constrain_msaa(pp,false,[](auto,auto,auto){return true;},[](auto,auto){return true;},why);
        assert(pp.multi_sample_type==n && pp.flags==0x80);
    }
    pp.multi_sample_type=8;std::string why;
    constrain_msaa(pp,false,[](auto f,auto,auto n){return f==22 || n<=2;},[](auto,auto){return true;},why);
    assert(pp.multi_sample_type==2 && why=="sample-fallback");
    pp.multi_sample_type=8;constrain_msaa(pp,false,[](auto,auto,auto){return true;},[](auto,auto){return false;},why);
    assert(pp.multi_sample_type==0 && why=="depth-mismatch");
    pp.multi_sample_type=8;pp.swap_effect=3;pp.flags=0x81;
    constrain_msaa(pp,false,[](auto,auto,auto){assert(false);return true;},[](auto,auto){return true;},why);
    assert(pp.multi_sample_type==0 && pp.swap_effect==3 && pp.flags==0x81);
    // Capture remains hard across engine fallback; failed API mutation cannot leak to next candidate.
    using namespace display;
    profile.enabled=true;profile.msaa_samples=4;no_multisampling="capture";
    pp.swap_effect=1;pp.multi_sample_type=8;pp.back_buffer_width=640;
    auto wanted=pp; wanted.back_buffer_width=800;
    d3d8::present_parameters final{};int calls=0;
    auto hr=profile_fallbacks([&](auto& attempt) {
        assert(attempt.multi_sample_type==0 && attempt.flags==0x81);
        assert(attempt.back_buffer_width==(calls==0 ? 800u : 640u));
        attempt.back_buffer_width=999;
        return ++calls==2 ? S_OK : E_FAIL;
    },nullptr,wanted,pp,nullptr,false,final,why);
    assert(hr==S_OK && calls==2 && wanted.back_buffer_width==999 && final.back_buffer_width==640);
    calls=0;wanted=pp;
    hr=profile_fallbacks([&](auto&) {return ++calls==2 ? d3d8::err_device_lost : E_FAIL;},nullptr,wanted,pp,nullptr,true,final,why);
    assert(hr==d3d8::err_device_lost && calls==2);
    profile={};no_multisampling=nullptr;
}
void vtable_tests() {
    using namespace display;
    std::array<void*,16> v1{},v2{};
    auto fn1=+[](void*,d3d8::present_parameters*)->HRESULT{return S_OK;};
    auto fn2=+[](void*,d3d8::present_parameters*)->HRESULT{return E_FAIL;};
    v1[14]=reinterpret_cast<void*>(fn1);v2[14]=reinterpret_cast<void*>(fn2);
    void** object1=v1.data();void** object2=v2.data();
    d3d8::reset_t original=nullptr;
    assert(hook_slot(&object1,14,&hooked_reset,original));
    assert(hook_slot(&object2,14,&hooked_reset,original));
    assert(reinterpret_cast<void*>(original_for(&object1,14,original))==reinterpret_cast<void*>(fn1));
    assert(reinterpret_cast<void*>(original_for(&object2,14,original))==reinterpret_cast<void*>(fn2));
    v1[14]=reinterpret_cast<void*>(fn2);assert(!hook_slot(&object1,14,&hooked_reset,original));
    com_originals.clear();
}
fake_device* created=nullptr;
unsigned create_calls=0,fail_creates=0,support_queries=0;
void centered_trace_tests() {
    centered_ui_trace::configure(true,true,true);
    observation_tests();
    const auto history=centered_ui_trace::snapshot();
    bool ready=false,unavailable=false;
    for(unsigned i=0;i<history.used;++i) {
        const auto& e=history.events[i];
        if(e.transform_known) {
            ready=true;assert(e.canvas.width==1920 && e.canvas.height==1080 && e.canvas.ox==240);
            assert(e.generation==e.canvas.generation && e.generation!=0);
        } else unavailable=true;
    }
    assert(ready && unavailable);
    centered_ui_trace::configure(false,true,true);
}
bool expected_windowed=true;int adapter_query_mode=0;
std::vector<d3d8::present_parameters> create_attempts;
HRESULT STDMETHODCALLTYPE sample_support(void*,UINT which,DWORD type,DWORD format,BOOL windowed,DWORD n) {
    assert(which==2 && type==1 && (windowed!=FALSE)==expected_windowed);++support_queries;
    return (format==22 || format==75) && n<=4 ? S_OK : E_FAIL;
}
HRESULT STDMETHODCALLTYPE display_mode(void*,UINT,d3d8::display_mode* mode) {
    *mode={1024,768,60,adapter_query_mode==2 ? 0u : expected_windowed ? 21u : 22u};
    return adapter_query_mode==1 ? E_FAIL : S_OK;
}
HRESULT STDMETHODCALLTYPE device_type_ok(void*,UINT,DWORD,DWORD,DWORD,BOOL) {return S_OK;}
HRESULT STDMETHODCALLTYPE depth_match(void*,UINT which,DWORD type,DWORD adapter_format,DWORD color,DWORD depth) {
    assert(which==2 && type==1 && adapter_format==(expected_windowed ? 21u : 22u) && color==22 && depth==75);return S_OK;
}
HRESULT STDMETHODCALLTYPE create_native(void*,UINT which,DWORD type,HWND,DWORD behaviour,d3d8::present_parameters* pp,void** out) {
    assert(which==2 && type==1 && (behaviour&2));create_attempts.push_back(*pp);++create_calls;
    if(create_calls<=fail_creates) {pp->flags=0xdead;pp->back_buffer_format=0xbeef;return E_FAIL;}
    pp->back_buffer_width=800;pp->back_buffer_height=600;*out=created;return S_OK;
}
void create_tests() {
    using namespace display;
    std::array<void*,9> surface_vt{};surface_vt[2]=reinterpret_cast<void*>(&release_surface);surface_vt[8]=reinterpret_cast<void*>(&get_desc);
    // Distinct vtables exercise each create fallback and each successful reset entry.
    for(unsigned failures=0;failures<4;++failures) {
        std::array<void*,74> vt{};vt[16]=reinterpret_cast<void*>(&get_back);vt[33]=reinterpret_cast<void*>(&get_depth);
        vt[14]=reinterpret_cast<void*>(&reset_device);vt[3]=reinterpret_cast<void*>(&cooperative);
        fake_device target{vt.data(),{surface_vt.data()},{surface_vt.data()}};
        target.back.desc={22,1,0,0,0,2,800,600};target.depth.desc=target.back.desc;
        std::array<void*,16> d3d_vt{};d3d_vt[11]=reinterpret_cast<void*>(&sample_support);d3d_vt[12]=reinterpret_cast<void*>(&depth_match);
        d3d_vt[8]=reinterpret_cast<void*>(&display_mode);d3d_vt[9]=reinterpret_cast<void*>(&device_type_ok);
        void** d3d_object=d3d_vt.data();created=&target;d3d=&d3d_object;
        com_originals.clear();observations.clear();opts={};stock_fallback=false;no_multisampling=nullptr;
        profile.enabled=true;profile.msaa_samples=8;test_isolation=failures<3;expected_windowed=test_isolation;desktop.format=22;
        if(failures==3) {opts.window_mode=display_rules::mode::fullscreen;raw_modes_adapter=2;raw_modes={{640,480,60,22},{1024,768,60,22}};}
        create_calls=0;fail_creates=failures;create_attempts.clear();real_create_device=&create_native;
        d3d8::present_parameters pp{};pp.back_buffer_format=22;pp.auto_depth_stencil_format=75;
        pp.enable_auto_depth_stencil=TRUE;pp.swap_effect=1;pp.flags=0x81;pp.multi_sample_type=8;
        if(failures==3) {pp.back_buffer_width=640;pp.back_buffer_height=480;}
        void* out=nullptr;
        assert(hooked_create_device(&d3d_object,2,1,nullptr,0,&pp,&out)==S_OK && out==&target);
        const auto snapshot=applied_snapshot();
        assert(snapshot.requested_saved_index==4 && snapshot.requested_profile_samples==8u);
        assert(!snapshot.engine_pp_before_constraints.windowed && snapshot.effective_windowed==expected_windowed);
        assert(snapshot.observed_backbuffer.multi_sample_type==2 && snapshot.successful_pp.back_buffer_width==800);
        assert(create_attempts.size()==failures+1 && support_queries);
        assert(create_attempts[0].multi_sample_type==4 && create_attempts[0].flags==0x80);
        if(failures) assert(create_attempts[1].multi_sample_type==0 && create_attempts[1].flags==0x81 && create_attempts[1].back_buffer_format==22);
        if(failures>=2) assert(create_attempts[2].multi_sample_type==4 && create_attempts[2].flags==0x80);
        if(failures==3) assert(create_attempts[3].multi_sample_type==0 && create_attempts[3].back_buffer_width==1024 && create_attempts[3].flags==0x81);
        assert(target.back.refs==0 && target.depth.refs==0);
        const auto generation=snapshot.device_generation;
        // A different current device/adapter must not change this device's reset policy.
        adapter=7;device_type=9;d3d=nullptr;
        // Actual successful Reset updates the same device, no retained surfaces at entry.
        assert(hooked_reset(&target,&pp)==S_OK);
        assert(applied_snapshot().device_generation>generation);
        assert(adapter==7 && device_type==9 && d3d==nullptr);
        assert(target.back.refs==0 && target.depth.refs==0);
        if(failures==0) {
            scoped_reset_context selected(&target);
            for(int failure:{1,2}) {
                auto candidate=pp;candidate.multi_sample_type=4;std::string why;adapter_query_mode=failure;
                sanitize_attempt(d3d,candidate,why);
                assert(candidate.multi_sample_type==0 && why=="adapter-format-query-failed");
            }
            adapter_query_mode=0;
        }
        com_originals.clear();
    }
    profile={};test_isolation=false;d3d=nullptr;
}
#endif
}
LONG WINAPI fake_exception(EXCEPTION_POINTERS* e) {
    std::fprintf(stderr,"fake-device exception %08lX at %p\n",e->ExceptionRecord->ExceptionCode,e->ExceptionRecord->ExceptionAddress);
    return EXCEPTION_EXECUTE_HANDLER;
}
int main(int argc,char** argv) {
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX);
    std::setvbuf(stdout,nullptr,_IONBF,0);
    SetUnhandledExceptionFilter(&fake_exception);
    using namespace display;
    const std::string mode=argc>1 ? argv[1] : "all";
    d3d8::present_parameters pp{}; pp.multi_sample_type=4;
#ifdef REMASTERED_BASELINE_DEVICE
    stock_observation_regression();
#else
    if(mode=="all") {
#ifndef CENTERED_UI_BASELINE_DISPLAY
        present_composition_tests();
#endif
        stock_observation_regression();std::puts("policy");policy_tests();std::puts("observations");observation_tests();std::puts("vtables");vtable_tests();std::puts("creates");create_tests();centered_trace_tests();
    }
    else if(mode=="centered-trace") { centered_trace_tests(); }
    else if(mode=="stock-reset") { stock_observation_regression();observation_tests(); }
    else if(mode=="second-lost") {
        int calls=0;auto wanted=pp;auto engine=pp;engine.back_buffer_width=640;
        profile.enabled=true;no_multisampling="capture";
        d3d8::present_parameters final{};std::string why;
        const HRESULT result=profile_fallbacks([&](auto&) {return ++calls==1 ? E_FAIL : d3d8::err_device_lost;},
            nullptr,wanted,engine,nullptr,true,final,why);
        assert(result==d3d8::err_device_lost && calls==2);
    } else { assert(false); }
#endif
    std::cout<<"PASS "<<mode<<"\n";
}
