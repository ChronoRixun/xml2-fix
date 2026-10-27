// Production room worker + game-thread lifecycle, with mock TLS outcomes and
// broker I/O. This mock does NOT verify a cryptographic pin. The separate TLS
// suite checks real certificate trust/protocol selection;
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
int pins=0,connections=0,cert_replies=0,installs=0;
std::string cert_identity;
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
        if(op=="cert") { // like the real broker: one cert_ok per cert request, i.e. after auth_ok on every connection that has no cached certificate
            ++cert_replies;
            reply("{\"op\":\"cert_ok\",\"cert_pem\":\"-----BEGIN STEAMDATAGRAM CERT-----\\nQUJD\\n-----END STEAMDATAGRAM CERT-----\\n\",\"gns_identity\":"+online_room_core::quote(cert_identity)+",\"expires\":4102444800}");
        }
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

namespace fake {
// Real GNS refuses every certificate the project CA did not sign, so the fixture installs through the room's own seam:
// it takes any certificate whose first byte is not 0 ("QUJD" -> "ABC") and refuses "AAAA" (000) like GNS refuses a foreign one.
bool install(const void* data,int size,SteamNetworkingErrMsg& error) {
    ++installs;
    if(size>0 && static_cast<const unsigned char*>(data)[0]==0) { std::strcpy(error,"fixture: not signed by the project CA");return false; }
    return true;
}
}
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
    room::set_certificate=&fake::install;room::cert_ready=false;room::cert_identity.clear();
    fake::installs=fake::cert_replies=0;fake::cert_identity="str:legends:0000000000000000";
    fake::mode=mode;fake::hold=false;fake::hold_handshake=false;fake::handshake_entered=false;fake::requests.clear();fake::input.clear();fake::output.clear();
    fake::test_mode=fake::transport_busy=false;
}
void ready_list(const std::string& mode) { reset(mode);assert(room::list());pump_until([] { return !room::client.pending(); }); }
void enqueue(const std::string& line, b::ticket ticket) {
    std::lock_guard g(room::lock);
    room::inbox.push_back({ticket,room::connection_generation.load(),line});room::inbox_bytes+=line.size();
}
void identity_lifecycle_test() {
    struct adapter {
        int resets=0,closes=0;
        bool get_identity(SteamNetworkingIdentity& out) { return SteamNetworkingSockets()->GetIdentity(&out); }
        void reset_identity(const SteamNetworkingIdentity& value) { ++resets;SteamNetworkingSockets()->ResetIdentity(&value); }
        void close_listener() { ++closes; }
    } io;
    gns_transport_core::room_identity_lifecycle<SteamNetworkingIdentity> policy;
    SteamNetworkingIdentity native,remote,current;
    assert(io.get_identity(native));remote.Clear();remote.ParseString("str:legends:1111111111111111");
    assert(policy.remember_native(io) && policy.synchronize(false,nullptr,true,false,io) && io.resets==0);
    assert(!policy.synchronize(true,&remote,false,false,io) && io.resets==0 && io.closes==0);
    assert(policy.synchronize(true,&remote,true,false,io) && io.resets==1 && io.get_identity(current) && current==remote);
    assert(!policy.synchronize(false,nullptr,false,false,io) && io.resets==1); // a new live link prevents reset
    assert(!policy.synchronize(false,nullptr,true,true,io) && io.resets==1); // committed room owns its context
    assert(policy.synchronize(false,nullptr,true,false,io) && io.resets==2 && io.get_identity(current) && current==native);
    assert(!gns_transport_core::room_ui_pump_needed(false,false) && gns_transport_core::room_ui_pump_needed(false,true) && gns_transport_core::room_ui_pump_needed(true,false));
    std::cout<<"PASS native identity handoff: actual GNS native -> room -> native; busy/committed context untouched; fresh-Hub initialization gate\n";
}
void live_room_survival(const std::string& fault) {
    reset("success");assert(room::host("Game",false));pump_until([] { return room::view().state==b::phase::room; });
    const auto ticket=room::view().generation;const auto address=room::own_virtual();const auto code=room::view().code;
    HSteamNetConnection local,peer;assert(room::sockets()->CreateSocketPair(&local,&peer,false,nullptr,nullptr));
    const std::string id="legends:1111111111111111";assert(room::members.join(id));
    auto member=std::make_unique<room::tunnel>();member->id=id;member->address=room::members.address(id);
    member->connection=local;member->connected=member->transport_bound=true;member->reported=fake::clock-10001;
    member->waiting.push_back({fake::clock,5165,5165,{1,2,3}});member->waiting_bytes=3;
    auto* tunnel=member.get();room::tunnels.push_back(std::move(member));
    const unsigned char keepalive=3;assert(room::sockets()->SendMessageToConnection(peer,&keepalive,1,k_nSteamNetworkingSend_Reliable,nullptr)==k_EResultOK);
    WSADATA wsa{};assert(WSAStartup(MAKEWORD(2,2),&wsa)==0);
    SOCKET udp=::socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);sockaddr_in bound{};bound.sin_family=AF_INET;bound.sin_addr.s_addr=htonl(INADDR_LOOPBACK);
    assert(::bind(udp,reinterpret_cast<sockaddr*>(&bound),sizeof(bound))==0);int length=sizeof(bound);assert(getsockname(udp,reinterpret_cast<sockaddr*>(&bound),&length)==0);
    std::vector<std::uint8_t> original(6);std::memcpy(original.data(),&bound.sin_addr.s_addr,4);const auto port=ntohs(bound.sin_port);std::memcpy(original.data()+4,&port,2);
    fake::hold=true;
    if(fault=="overflow") {
        room::worker_generation=room::connection_generation.load();room::worker_ticket=ticket;
        for(unsigned i=0;i<257;++i) room::arrived("{}");
    } else if(fault=="bad_cert") { room::cert_ready=false;room::cert_identity.clear();enqueue(R"({"op":"cert_ok","cert_pem":"bad"})",ticket); }
    else if(fault=="refused_cert") { room::cert_ready=false;room::cert_identity.clear();enqueue("{\"op\":\"cert_ok\",\"cert_pem\":\"-----BEGIN STEAMDATAGRAM CERT-----\\nAAAA\\n-----END STEAMDATAGRAM CERT-----\\n\"}",ticket); }
    else if(fault=="cert_error") enqueue(R"({"op":"error","in_reply_to":"cert","code":"quota_exceeded"})",ticket);
    else if(fault=="closed") enqueue(R"({"op":"_closed","kind":"broker_unreachable","msg":"invented loss"})",ticket);
    const auto original_id=room::own_id;
    if(fault=="identity") room::own_id="legends:2222222222222222";
    room::ui_tick();room::tick();room::own_id=original_id;
    pump_until([&] { return tunnel->waiting.empty() && !tunnel->game.empty() && room::worker_done.load(); });
    assert(room::view().state==b::phase::room && room::view().generation==ticket && room::view().code==code && !room::view().authenticated);
    assert(room::own_virtual()==address && address && room::members.contains(id) && room::owns(local) && tunnel->connected && tunnel->transport_bound);
    assert(tunnel->datagrams_out==1 && tunnel->waiting_bytes==0 && !room::room_ready);
    SteamNetworkingMessage_t* received=nullptr;assert(room::sockets()->ReceiveMessagesOnConnection(peer,&received,1)==1);
    auto datagram=gns_transport_core::parse(static_cast<const std::uint8_t*>(received->m_pData),received->m_cbSize);
    assert(datagram.valid && datagram.k==gns_transport_core::kind::datagram && datagram.length==3);received->Release();
    auto rewritten=original;room::present_virtual(udp,rewritten);const auto virtual_ip=htonl(address);
    assert(std::memcmp(rewritten.data(),&virtual_ip,4)==0 && std::memcmp(rewritten.data(),original.data(),4)!=0);
    assert(!room::retry() && !room::list() && !room::resolve("ABCDEFGH") && !room::host("Replacement",false));
    room::cancel();assert(room::view().state==b::phase::room && room::own_virtual()==address);
    closesocket(udp);WSACleanup();room::leave();pump_until([] { return room::worker_done.load(); });room::sockets()->CloseConnection(peer,0,"fixture complete",false);
    std::cout<<"PASS live-room real ui_tick/tick "<<fault<<": address/room/roster retained, real GNS flush+poll, virtual rewrite, retry refused\n";
}

std::string cert_ok_line(const std::string& identity, const char* body) {
    return "{\"op\":\"cert_ok\",\"cert_pem\":\"-----BEGIN STEAMDATAGRAM CERT-----\\n"+std::string(body)+"\\n-----END STEAMDATAGRAM CERT-----\\n\",\"gns_identity\":"+online_room_core::quote(identity)+"}";
}
// Review R1: every new broker connection delivers a cert_ok right after auth_ok. After an attempt failed, Retry / Refresh / Done
// open a new connection whose cert_ok meets the certificate the earlier attempt installed; that must not fail the attempt.
void certificate_after_failure_test(const std::string& fault, const std::string& next) {
    const bool on_resolve=fault=="no_such_room" || fault=="room_full" || fault=="version_mismatch";
    reset(fault=="rate_limited" ? "error" : fault=="timeout" ? "timeout" : fault=="version_mismatch" ? "mismatch" : fault=="idle_disconnect" ? "success" : fault);
    assert(on_resolve ? room::resolve("ABCDEFGH") : room::list());
    if(fault=="timeout") { pump_until([] { return room::view().state==b::phase::listing && room::cert_ready; });fake::clock+=10000;room::ui_tick(); }
    else pump_until([&] { return !room::client.pending(); });
    if(fault=="idle_disconnect") {
        assert(room::view().state==b::phase::idle && room::view().list_known);
        fake::closed=true;pump_until([] { return room::worker_done.load(); }); // the broker dropped the idle connection
    } else assert(room::view().state==b::phase::retry_wait);
    for(int frame=0;frame<5;++frame) { room::ui_tick();room::tick(); } // the game's next frames close the failed attempt's worker
    assert(room::cert_ready && fake::installs==1 && fake::cert_replies==1 && room::cert_identity==fake::cert_identity);
    const auto connections=fake::connections;
    fake::mode="success";fake::clock+=200000;
    const bool opens_room=next=="done" || (next=="retry" && on_resolve);
    if(next=="retry") assert(room::retry());
    else if(next=="refresh") assert(room::list());
    else assert(room::resolve("ABCDEFGH"));
    if(opens_room) pump_until([] { return room::view().state==b::phase::room; });
    else pump_until([] { return !room::client.pending(); });
    assert(room::view().error.empty() && room::view().state==(opens_room ? b::phase::room : b::phase::idle));
    assert(fake::connections>connections && fake::cert_replies==2 && fake::installs==1 && room::cert_ready);
    if(opens_room) assert(room::view().code=="ABCDEFGH" && room::view().authenticated); else assert(room::view().rows.size()==1);
    room::leave();pump_until([] { return room::worker_done.load(); });
    std::cout<<"PASS certificate kept across attempts: "<<fault<<" then "<<next<<" on a new connection (cert_ok repeated, ignored, not reinstalled)\n";
}
// A repeat for the same identity is ignored outside a room; any other identity is validated exactly like a first certificate.
void certificate_identity_outside_room_test() {
    reset("timeout");assert(room::list());pump_until([] { return room::view().state==b::phase::listing && room::cert_ready; });
    assert(fake::installs==1);
    auto ticket=room::view().generation;
    enqueue(cert_ok_line("str:legends:0000000000000000","AAAA"),ticket);room::tick();
    assert(room::view().state==b::phase::listing && room::view().error.empty() && fake::installs==1); // same identity: not installed again, not fatal
    enqueue(cert_ok_line("str:legends:2222222222222222","AAAA"),ticket);room::tick();
    assert(fake::installs==2 && room::view().state==b::phase::retry_wait && room::view().error=="invalid_certificate"); // other identity: checked, refused
    assert(room::cert_identity=="str:legends:0000000000000000");
    reset("timeout");assert(room::list());pump_until([] { return room::view().state==b::phase::listing && room::cert_ready; });
    ticket=room::view().generation;
    enqueue(cert_ok_line("str:legends:2222222222222222","QUJD"),ticket);room::tick();
    assert(fake::installs==2 && room::view().state==b::phase::listing && room::cert_identity=="str:legends:2222222222222222"); // other identity: checked, accepted
    enqueue(cert_ok_line("str:legends:2222222222222222","AAAA"),ticket);room::tick();
    assert(fake::installs==2 && room::view().state==b::phase::listing && room::view().error.empty());
    enqueue("{\"op\":\"cert_ok\",\"cert_pem\":\"bad\"}",ticket);room::tick(); // names no identity = this PC's own, which is not the installed one here
    assert(fake::installs==2 && room::view().state==b::phase::retry_wait && room::view().error=="invalid_certificate");
    room::leave();pump_until([] { return room::worker_done.load(); });
    std::cout<<"PASS certificate identity outside a room: same identity ignored, other identity checked (refused and accepted)\n";
}
// In a room a repeat cert_ok, whoever it names and whatever it carries, is ignored and the broker connection stays up.
void certificate_repeat_in_room_test() {
    reset("success");assert(room::host("Game",false));pump_until([] { return room::view().state==b::phase::room; });
    assert(room::cert_ready && fake::installs==1);
    const auto ticket=room::view().generation;const auto connections=fake::connections;
    for(const auto& line:{cert_ok_line("str:legends:0000000000000000","QUJD"),cert_ok_line("str:legends:0000000000000000","AAAA"),cert_ok_line("str:legends:2222222222222222","QUJD"),std::string("{\"op\":\"cert_ok\",\"cert_pem\":\"bad\"}")}) {
        enqueue(line,ticket);room::ui_tick();room::tick();
        assert(room::view().state==b::phase::room && room::view().generation==ticket && room::view().authenticated && room::view().error.empty());
        assert(!room::worker_done.load() && fake::installs==1 && room::cert_identity=="str:legends:0000000000000000" && fake::connections==connections);
    }
    room::leave();pump_until([] { return room::worker_done.load(); });
    std::cout<<"PASS certificate repeat inside a room: ignored, broker connection and room kept\n";
}
}

int main() {
    std::cout<<std::unitbuf;
    SteamNetworkingIdentity id;id.Clear();id.ParseString("str:legends:0000000000000000");SteamNetworkingErrMsg error{};
    assert(GameNetworkingSockets_Init(nullptr,error));
    identity_lifecycle_test();
    GameNetworkingSockets_Kill();
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
    reset("timeout");assert(room::list());pump_until([] { return room::view().state==b::phase::listing; });
    auto current_ticket=room::view().generation,stale_ticket=current_ticket;--stale_ticket.request;
    enqueue(R"({"op":"error","in_reply_to":"cert","code":"quota_exceeded"})",stale_ticket);room::tick();
    assert(room::view().state==b::phase::listing && room::view().error.empty());
    enqueue(R"({"op":"error","in_reply_to":"cert","code":"quota_exceeded"})",current_ticket);room::tick();
    assert(room::view().state==b::phase::retry_wait && room::view().error=="quota_exceeded");
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
    for(const auto* fault:{"closed","overflow","bad_cert","refused_cert","cert_error","identity"}) live_room_survival(fault);
    for(const char* fault:{"rate_limited","no_such_room","room_full","version_mismatch","timeout","idle_disconnect"}) for(const char* next:{"retry","refresh","done"})
        if(!(std::string(fault)=="idle_disconnect" && std::string(next)=="retry")) certificate_after_failure_test(fault,next);
    certificate_identity_outside_room_test();certificate_repeat_in_room_test();
    reset("success");assert(room::list());pump_until([] { return room::view().state==b::phase::listing; });
    room::worker_generation=room::connection_generation.load();room::worker_ticket=room::view().generation;
    { std::lock_guard g(room::lock);for(unsigned i=0;i<256;++i) room::inbox.push_back({room::worker_ticket,room::worker_generation,"{}"}); }
    room::arrived("{}");room::ui_tick();assert(room::view().error=="inbox_overflow");
    room::leave();pump_until([] { return room::worker_done.load(); });GameNetworkingSockets_Kill();
    std::cout<<"PASS production broker worker: mock TLS outcomes (not cryptographic pin verification), auth/list/host/resolve/reentry, malformed/oversize/duplicate/error/timeout/mismatch, limiter, cancel and stale inbox\n";
}
