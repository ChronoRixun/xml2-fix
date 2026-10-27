// Production room worker + game-thread lifecycle, with invented pinned TLS and
// broker I/O. The TLS suite checks real certificate trust/protocol selection;
// its optional --live handshake/record route is a separately scheduled gate.
// No game, external broker, profile, key, registry or desktop access.
#include <winsock2.h>
#include <ws2tcpip.h>
#include "broker_tls.hpp"
#include "online_room.hpp"
#include "online_status.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <iostream>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <cstring>

namespace fake {
std::mutex lock;
std::string input, output, mode;
std::vector<std::string> requests;
std::atomic<DWORD> clock{10000};
std::atomic<bool> closed{false}, hold{false};
std::atomic<bool> handshake_entered{false}, hold_handshake{false};
bool test_mode=false,transport_busy=false;
int pins=0,connections=0;
int native_sends=0,native_receives=0;
int WSAAPI sendto(SOCKET,const char*,int size,int,const sockaddr*,int) { ++native_sends;return size; }
int WSAAPI recvfrom(SOCKET,char*,int,int,sockaddr*,int*) { ++native_receives;return 17; }
void reply(const std::string& line) { std::lock_guard g(lock);input+=line+"\n"; }
DWORD WINAPI now() { return clock.load(); }
int WSAAPI address(PCSTR,PCSTR,const ADDRINFOA*,PADDRINFOA* result) {
    static sockaddr_in addr{};static addrinfo info{};info.ai_addr=reinterpret_cast<sockaddr*>(&addr);info.ai_addrlen=sizeof(addr);*result=&info;return 0;
}

void WSAAPI free_address(PADDRINFOA) {}
SOCKET WSAAPI socket(int,int,int) { ++connections;closed=false;return 123; }
int WSAAPI connect(SOCKET,const sockaddr*,int) { return 0; }
int WSAAPI socket_option(SOCKET,int,int,char* out,int*) { *reinterpret_cast<int*>(out)=0;return 0; }
int WSAAPI ioctl(SOCKET,long,u_long*) { return 0; }
int WSAAPI close(SOCKET) { closed=true;return 0; }
int WSAAPI shutdown(SOCKET,int) { closed=true;return 0; }
int WSAAPI select(int,fd_set* r,fd_set*,fd_set* e,const timeval*) {
    if(e) FD_ZERO(e);
    if(!r) return 1;
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
    std::lock_guard g(lock);
    return !hold && (!input.empty() || closed) ? 1 : 0;
}
int WSAAPI receive(SOCKET,char* data,int length,int) {
    std::lock_guard g(lock);
    if(closed) return 0;
    const auto count=std::min<std::size_t>(input.size(),std::size_t(length));
    std::memcpy(data,input.data(),count);input.erase(0,count);return int(count);
}
int WSAAPI send(SOCKET,const char* data,int size,int) {
    output.append(data,size);
    for(;;) {
        auto end=output.find('\n');if(end==std::string::npos) break;
        auto row=output.substr(0,end);output.erase(0,end+1);
        requests.push_back(row);
        auto msg=online_room_core::parse(row);auto op=msg.text("op");
        if(op=="auth") reply("{\"op\":\"auth_ok\",\"identity\":\"legends:0000000000000000\",\"ca\":\"fixture-ca\"}");
        if(mode=="timeout") continue;
        if(op=="list_rooms") {
            if(mode=="malformed") reply("{\"op\":\"list_rooms_ok\",\"rooms\":[}");
            else if(mode=="oversize") reply(std::string(16384,'x'));
            else if(mode=="duplicate") reply("{\"op\":\"list_rooms_ok\",\"rooms\":[{\"code\":\"ABCDEFGH\"},{\"code\":\"ABCDEFGH\"}]}");
            else if(mode=="error") reply("{\"op\":\"error\",\"in_reply_to\":\"list_rooms\",\"code\":\"rate_limited\"}");
            else if(mode=="empty") reply("{\"op\":\"list_rooms_ok\",\"rooms\":[]}");
            else reply("{\"op\":\"list_rooms_ok\",\"rooms\":[{\"code\":\"ABCDEFGH\",\"name\":\"Remote % ^ \\\\ name\"}]}");
        }
        if(op=="host") reply("{\"op\":\"host_ok\",\"code\":\"ABCDEFGH\"}");
        if(op=="resolve") {
            if(mode=="no_such_room" || mode=="room_full") reply("{\"op\":\"error\",\"in_reply_to\":\"resolve\",\"code\":"+online_room_core::quote(mode)+"}");
            else if(mode=="missing_fields") reply("{\"op\":\"resolve_ok\",\"code\":\"ABCDEFGH\",\"host\":\"legends:1111111111111111\",\"members\":[\"legends:1111111111111111\",\"legends:0000000000000000\"]}");
            else if(mode=="duplicate_members") reply("{\"op\":\"resolve_ok\",\"code\":\"ABCDEFGH\",\"host\":\"legends:1111111111111111\",\"members\":[\"legends:1111111111111111\",\"legends:0000000000000000\",\"legends:0000000000000000\"]}");
            else reply("{\"op\":\"resolve_ok\",\"code\":"+online_room_core::quote(msg.text("code"))+",\"host\":\"legends:1111111111111111\",\"members\":[\"legends:1111111111111111\",\"legends:0000000000000000\"],\"room\":{\"version_token\":\""+(mode=="mismatch" ? std::string("OTHER") : "FIXTURE")+"\"}}");
        }
    }
    return size;
}
}

#include "../src/online_room.cpp"

namespace broker_tls {
client::~client()=default;
bool client::handshake(SOCKET,const std::string&,const std::string& pin,std::string& error) {
    ++fake::pins;
    fake::handshake_entered=true;
    while(fake::hold_handshake && !fake::closed) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    if(fake::closed) { error="canceled handshake";return false; }
    if(pin!="fixture-pin") { error="broker certificate SPKI pin does not match";return false; }
    fake::reply("{\"op\":\"challenge\",\"v\":2,\"nonce\":\"AAAAAAAAAAAAAAAAAAAAAA==\"}");return true;
}
bool client::encrypt(const std::string& plain,std::vector<std::uint8_t>& wire,std::string&) { wire.assign(plain.begin(),plain.end());return true; }
bool client::decrypt(const std::uint8_t* data,std::size_t size,std::string& plain,std::string&) { if(size) plain.append(reinterpret_cast<const char*>(data),size);return true; }
}
namespace ini { std::optional<std::string> text(const wchar_t*,const wchar_t*) { return {}; } int number(const wchar_t*,const wchar_t*,int n) { return n; } }
namespace logger {
void write(const char*,...) {} void write_once(std::string_view,const char*,...) {} void write_capped(std::string_view,const char*,...) {}
}
namespace online_menus { bool installed() { return true; } }
namespace gns_transport { bool installed() { return true; } bool room_can_start() { return !fake::transport_busy; } }
namespace receive_validation { bool installed() { return true; } }
namespace save_format { bool campaign_load_checked() { return true; } }
namespace join_in_progress { bool installed() { return false; } }
namespace game_version { bool test_mode_active() { return fake::test_mode; } const char* test_mode_reason() { return "fixture"; } void refuse_startup() {} }
namespace online_status {
void begin(online_status_core::role) {} bool active() { return true; }
void set(online_status_core::state,const std::string&,int,online_status_core::error,const std::string&) {}
void error(online_status_core::error,const std::string&) {} void players(int) {} void room(bool,const std::string&) {}
online_status_core::state current() { return online_status_core::state::connecting; }
void end() {} bool remove_launch_keys(const std::wstring&) { return true; } std::wstring game_ini() { return {}; }
}

namespace {
namespace room=online_room;
namespace b=online_browser_core;
void pump_until(const std::function<bool()>& predicate) {
    const auto until=std::chrono::steady_clock::now()+std::chrono::seconds(3);
    do { room::ui_tick();room::tick();if(predicate()) return;std::this_thread::sleep_for(std::chrono::milliseconds(1)); } while(std::chrono::steady_clock::now()<until);
    assert(predicate());
}
void reset(const std::string& mode) {
    room::leave();pump_until([] { return room::worker_done.load(); });
    room::client=b::client_state{};room::configured=room::menu_client=true;room::wanted=false;
    room::broker_address="invented.test:10020";room::broker_pin="fixture-pin";
    room::own_id="legends:0000000000000000";room::own_version_token="FIXTURE";room::key_loaded=true;
    room::metadata={};room::metadata.version_token="FIXTURE";
    room::room_clock=&fake::now;
    room::io={fake::address,fake::free_address,fake::socket,fake::connect,fake::socket_option,fake::ioctl,fake::close,fake::shutdown,fake::select,fake::receive,fake::send};
    room::cert_cache.clear();room::turn_cache.clear();fake::clock+=100000;
    fake::mode=mode;fake::hold=false;fake::hold_handshake=false;fake::handshake_entered=false;fake::requests.clear();fake::input.clear();fake::output.clear();
    fake::test_mode=fake::transport_busy=false;
}
void ready_list(const std::string& mode) { reset(mode);assert(room::list());pump_until([] { return !room::client.pending(); }); }
}

int main() {
    SteamNetworkingIdentity id;id.Clear();id.ParseString("str:legends:0000000000000000");SteamNetworkingErrMsg error{};
    assert(GameNetworkingSockets_Init(&id,error));
    reset("success");room::real_sendto=&fake::sendto;room::real_recvfrom=&fake::recvfrom;
    assert(room::room_sendto(0,nullptr,17,0,nullptr,0)==17 && fake::native_sends==1);
    assert(room::room_recvfrom(0,nullptr,0,0,nullptr,nullptr)==17 && fake::native_receives==1);
    auto count=fake::connections;fake::test_mode=true;assert(!room::list() && room::view().error=="test_mode_on" && fake::connections==count);
    reset("success");fake::transport_busy=true;assert(!room::list() && room::view().error=="transport_identity_busy" && fake::connections==count);
    reset("success");room::preset=b::operation::resolve;room::launch_code="ABCDEFGH";room::configured_room_name="Preset";
    b::launch_preset preset;
    const auto connections=fake::connections;
    assert(room::take_preset(preset) && preset.code=="ABCDEFGH" && !room::take_preset(preset));
    assert(fake::connections==connections && !room::wanted && room::view().state==b::phase::idle);
    reset("success");room::menu_client=false;room::wanted=room::hosting=true;room::preset=b::operation::host;
    fake::clock+=600000;room::ui_tick();assert(room::client.pending());
    pump_until([] { return room::view().state==b::phase::room; }); // no install-time timeout
    const auto pins=fake::pins;ready_list("success");assert(room::view().rows.size()==1 && fake::pins==pins+1);
    assert(!room::post(std::string(room::broker_queue_cap+1,'x')));
    assert(!room::list());assert(!room::resolve("ABCDEFGH"));
    fake::clock+=2000;assert(room::resolve("ABCDEFGH"));pump_until([] { return room::view().state==b::phase::room; });
    assert(room::own_virtual()!=0 && room::view().code=="ABCDEFGH");
    auto prior=room::view().generation;room::leave();assert(room::view().generation.room>prior.room);
    pump_until([] { return room::worker_done.load(); });fake::clock+=2000;
    assert(room::host("  Friday night  ",true));pump_until([] { return room::view().state==b::phase::room; });assert(room::view().code=="ABCDEFGH");
    assert(!room::host("bad;name",true));
    ready_list("empty");assert(room::view().rows.empty());
    for(auto mode:{"malformed","oversize","duplicate","error"}) {
        ready_list(mode);assert(room::view().state==b::phase::retry_wait);assert(!room::retry());
    }
    reset("success");room::broker_pin="wrong-pin";assert(room::list());pump_until([] { return !room::client.pending(); });
    assert(room::view().error=="tls_pin_mismatch" && fake::requests.empty());
    reset("success");fake::hold_handshake=true;assert(room::list());pump_until([] { return fake::handshake_entered.load(); });
    room::cancel();pump_until([] { return room::worker_done.load(); });assert(room::view().state==b::phase::idle);
    reset("timeout");assert(room::list());pump_until([] { return room::view().state==b::phase::listing; });fake::clock+=10000;room::ui_tick();assert(room::view().error=="timeout");
    reset("mismatch");assert(room::resolve("ABCDEFGH"));pump_until([] { return !room::client.pending(); });assert(room::view().error=="version_mismatch" && !room::room_ready);
    for(const auto* mode:{"no_such_room","room_full","duplicate_members"}) {
        reset(mode);assert(room::resolve("ABCDEFGH"));pump_until([] { return !room::client.pending(); });
        assert(room::view().state==b::phase::retry_wait && !room::room_ready);
    }
    reset("missing_fields");assert(room::resolve("ABCDEFGH"));pump_until([] { return !room::client.pending(); });assert(room::view().state==b::phase::room && room::view().room_name.empty());
    for(bool during_auth:{true,false}) for(auto op:{b::operation::list,b::operation::host,b::operation::resolve}) {
        reset("timeout");
        fake::hold=during_auth;
        assert(op==b::operation::list ? room::list() : op==b::operation::host ? room::host("Game",false) : room::resolve("ABCDEFGH"));
        room::ui_tick();
        if(!during_auth) pump_until([] { return room::view().authenticated && room::client.pending(); });
        auto old=room::view().generation;room::cancel();pump_until([] { return room::worker_done.load(); });
        assert(room::view().generation!=old && room::view().state==b::phase::idle && !room::room_ready && !room::requested());
        // Late worker completion and prior-room inbox are ignored.
        { std::lock_guard g(room::lock);room::inbox.push_back({old,room::connection_generation.load()-1,"{\"op\":\"host_ok\",\"code\":\"ABCDEFGH\"}"}); }
        room::process_inbox();assert(room::view().state==b::phase::idle);
    }
    reset("success");assert(room::host("Game",false));pump_until([] { return room::view().state==b::phase::room; });
    auto member=std::make_unique<room::tunnel>();member->id="legends:1111111111111111";
    member->connection=42;member->connected=member->transport_bound=true;room::tunnels.push_back(std::move(member));
    room::handle(online_room_core::parse(R"({"op":"_closed","kind":"broker_unreachable","msg":"invented broker loss"})"));
    assert(room::view().state==b::phase::room && !room::view().authenticated && !room::room_ready && room::owns(42) && room::tunnels[0]->connected);
    assert(!room::list() && !room::retry()); // preserve existing gameplay, no replacement room
    reset("success");assert(room::list());pump_until([] { return room::view().state==b::phase::listing; });
    room::worker_generation=room::connection_generation.load();room::worker_ticket=room::view().generation;
    { std::lock_guard g(room::lock);for(unsigned i=0;i<256;++i) room::inbox.push_back({room::worker_ticket,room::worker_generation,"{}"}); }
    room::arrived("{}");room::ui_tick();assert(room::view().error=="inbox_overflow");
    room::leave();pump_until([] { return room::worker_done.load(); });GameNetworkingSockets_Kill();
    std::cout<<"PASS production broker worker: pinned TLS seam, auth/list/host/resolve/reentry, malformed/oversize/duplicate/error/timeout/pin/mismatch, limiter, cancel and stale inbox\n";
}
