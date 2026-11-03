// Exercise the production pacer on invented retail patch storage. No game,
// device, socket, settings file, registry or desktop access.
#include "frame_rate.hpp"
#include "online_tick.hpp"
#include "ini.hpp"
#include <cstdio>
#include <cstring>
#include <cstdarg>
#include <cwchar>
#include <vector>

namespace logger { void write(const char*,...) {} }
namespace ini {
    std::string tick="60";
    std::optional<std::string> text(const wchar_t*,const wchar_t* key) {
        if(std::wcscmp(key,L"TickRate")==0)return tick;
        if(std::wcscmp(key,L"TickWait")==0)return "spin";
        return {};
    }
}
namespace online_menus { bool installed() { return true; } }
namespace {
unsigned passed=0, failed=0;
void check(bool ok,const char* text) { std::printf("%s %s\n",ok ? "PASS" : "FAIL",text);ok ? ++passed : ++failed; }
}
int main() {
    using namespace frame_rate_rules;
    std::vector<std::uint8_t> image(0x4000);
    std::memcpy(image.data()+stock_cap_patch.rva,stock_cap_patch.expected.data(),stock_cap_patch.expected.size());
    frame_rate::install(reinterpret_cast<HMODULE>(image.data()),{cap::kind::fixed,60},30,false);
    online_tick::install(reinterpret_cast<HMODULE>(image.data()));
    check(online_tick::linked() && online_tick::verified() && online_tick::revision()==2,"production online installer requires verified linked pacing");
    check(matches_patched(online_wait_patch,image.data()+online_wait_patch.rva),"TickWait=spin cannot leave a lower native guest limiter active");
    std::memcpy(image.data()+online_wait_patch.rva+online_wait_patch.offset,online_wait_patch.expected.data()+online_wait_patch.offset,4);
    ini::tick="30";online_tick::install(reinterpret_cast<HMODULE>(image.data()));
    check(online_tick::verified() && online_tick::rate()==30 && frame_rate::host_rate()==30,"production TickRate 30 is this PC's hosting ceiling");
    for (unsigned host : {30u,60u}) {
        const unsigned local=host==30 ? 60 : 30;
        frame_rate::retarget({cap::kind::fixed,local},false);
        const auto before=frame_rate::describe();
        check(frame_rate::local_rate()==local,"production local setting recorded");
        check(frame_rate::adopt_session_rate(host) && frame_rate::session_rate()==host,"production guest adopts the host's rate before a shared frame");
        check(online_tick::rate()==host && online_tick::frames_for(64)==64*host/30,"lost-input time coverage follows the adopted session rate");
        check(frame_rate::describe().find("host's session rate "+std::to_string(host))!=std::string::npos,"production diagnostic names the session override");
        check(frame_rate::local_rate()==local,"production override preserves the guest's local option");
        for(unsigned i=0;i<4;++i)frame_rate::on_present();
        check(frame_rate::session_rate()==host,"late join keeps the override while native shared frames are pending");
        check(!frame_rate::adopt_session_rate(local) && frame_rate::session_rate()==host,"production conflicting mesh/settings message cannot change a running session");
        frame_rate::clear_session_rate();frame_rate::on_present();
        check(frame_rate::session_rate()==0 && frame_rate::local_rate()==local && frame_rate::describe()==before,"production leave restores exactly the previous local setting");
    }
    frame_rate::retarget({cap::kind::stock,0},false);
    const auto stock=frame_rate::describe();
    check(frame_rate::adopt_session_rate(30),"stock/absent option can enter a 30 fps session");
    frame_rate::clear_session_rate();
    check(frame_rate::adopt_session_rate(30),"native direct session can own a target without a room");
    frame_rate::clear_session_rate(true);
    check(frame_rate::session_rate()==30,"canceling an unrelated broker UI cannot clear a native direct session's target");
    frame_rate::clear_session_rate();
    check(frame_rate::adopt_session_rate(60,true),"room session owns its target across native lifecycle changes");
    frame_rate::clear_session_rate(true);
    check(frame_rate::session_rate()==0,"room teardown clears only its own override");
    check(frame_rate::describe()==stock && frame_rate::local_rate()==60,"absent FrameRate remains absent/stock on leave");
    frame_rate::retarget({cap::kind::fixed,60},true); // local 30 Hz VSync clamp
    check(frame_rate::adopt_session_rate(60),"windowed VSync does not reject the host's target");
    frame_rate::clear_session_rate();
    image[online_wait_patch.rva]^=1;
    online_tick::install(reinterpret_cast<HMODULE>(image.data()));
    check(online_tick::requested() && !online_tick::verified(),"a refused online timer patch closes agreement rather than mixing hidden spin rates");
    std::printf("Linked frame rate production pacer: %u passed, %u failed\n",passed,failed);
    return failed ? 1 : 0;
}
