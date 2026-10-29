// Production room worker + game-thread lifecycle, with mock TLS outcomes and
// broker I/O. This mock does NOT verify a cryptographic pin. The separate TLS
// suite checks real certificate trust/protocol selection;
// its optional --live handshake/record route is a separately scheduled gate.
// No game, external broker, profile, key, registry or desktop access.
// Production builds cannot reassign the room's certificate seam; only this fixture opts into it.
#define XML2_ONLINE_ROOM_TEST_SEAMS 1
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
std::mutex lock, requests_lock;
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
        { std::lock_guard g(requests_lock);requests.push_back(row); }
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
// The game-side reads of the transport (rule 64 glue tests set them): the native session facts and the delivery call.
namespace fake { gns_transport::party_facts party{}; bool party_ok=false,deliver_ok=true; std::vector<unsigned> delivered; }
namespace gns_transport { bool installed() { return true; } bool room_can_start() { return !fake::transport_busy; }
    bool party_snapshot(party_facts& out) { out = fake::party_ok ? fake::party : party_facts{}; return fake::party_ok; }
    bool deliver_removal(unsigned index) { fake::delivered.push_back(index); return fake::deliver_ok; } }
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
void attempt() {} void party(const std::string&,int,const std::vector<online_status_core::roster_row>&) {} void tick() {}
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
    room::set_certificate=&fake::install;room::cert_ready=false;room::cert_identity.clear();room::cert_expires=0;
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

std::string cert_ok_line(const std::string& identity, const char* body, long long expires=0) {
    return "{\"op\":\"cert_ok\",\"cert_pem\":\"-----BEGIN STEAMDATAGRAM CERT-----\\n"+std::string(body)+"\\n-----END STEAMDATAGRAM CERT-----\\n\",\"gns_identity\":"+online_room_core::quote(identity)+(expires>0 ? ",\"expires\":"+std::to_string(expires) : std::string())+"}";
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
// Review N-a: a same-identity repeat that expires LATER than the installed certificate replaces it outside a room; an equal,
// older or undated repeat is ignored; a refused renewal keeps the working certificate and the broker connection.
void certificate_renewal_test() {
    reset("timeout");assert(room::list());pump_until([] { return room::view().state==b::phase::listing && room::cert_ready; });
    assert(fake::installs==1 && room::cert_expires==4102444800LL);
    const std::string own="str:legends:0000000000000000";const auto ticket=room::view().generation;
    for(const long long expires:{4102444800LL,4102444799LL,0LL}) {
        enqueue(cert_ok_line(own,"QUJD",expires),ticket);room::tick();
        assert(fake::installs==1 && room::cert_expires==4102444800LL && room::view().state==b::phase::listing && room::view().error.empty()); // equal / older / undated: ignored
    }
    enqueue(cert_ok_line(own,"AAAA",4102444900LL),ticket);room::tick();
    assert(fake::installs==2 && room::cert_expires==4102444800LL && room::cert_identity==own && room::cert_ready); // later but refused: the installed certificate stays
    assert(room::view().state==b::phase::listing && room::view().error.empty());
    enqueue(cert_ok_line(own,"QUJD",4102445000LL),ticket);room::tick();
    assert(fake::installs==3 && room::cert_expires==4102445000LL && room::cert_identity==own && room::view().state==b::phase::listing && room::view().error.empty()); // later and accepted: replaced
    enqueue(cert_ok_line(own,"QUJD",4102445000LL),ticket);room::tick();
    assert(fake::installs==3); // now equal: ignored again
    room::leave();pump_until([] { return room::worker_done.load(); });
    std::cout<<"PASS certificate renewal: later expiry replaces the installed one, equal/older ignored, refused renewal keeps the working certificate\n";
}
// In a room a repeat cert_ok, whoever it names and whatever it carries, is ignored and the broker connection stays up.
void certificate_repeat_in_room_test() {
    reset("success");assert(room::host("Game",false));pump_until([] { return room::view().state==b::phase::room; });
    assert(room::cert_ready && fake::installs==1);
    const auto ticket=room::view().generation;const auto connections=fake::connections;
    for(const auto& line:{cert_ok_line("str:legends:0000000000000000","QUJD",4102444900LL),cert_ok_line("str:legends:0000000000000000","QUJD"),cert_ok_line("str:legends:0000000000000000","AAAA"),cert_ok_line("str:legends:2222222222222222","QUJD"),std::string("{\"op\":\"cert_ok\",\"cert_pem\":\"bad\"}")}) {
        enqueue(line,ticket);room::ui_tick();room::tick();
        assert(room::view().state==b::phase::room && room::view().generation==ticket && room::view().authenticated && room::view().error.empty());
        assert(!room::worker_done.load() && fake::installs==1 && room::cert_identity=="str:legends:0000000000000000" && fake::connections==connections);
    }
    room::leave();pump_until([] { return room::worker_done.load(); });
    std::cout<<"PASS certificate repeat inside a room: ignored, broker connection and room kept\n";
}
}

namespace {
void native_host_metadata_test() {
    reset("success");assert(!room::host_native("Native host",true,1,true));
    assert(room::host_native("Native host",true,3,true));
    pump_until([] { return room::view().state==b::phase::room; });
    const auto generation=room::view().generation;
    assert(room::native_posting && room::listed.max_players==3 && !room::listed.joinable && room::listed.game_mode=="danger_room");
    auto old=generation;--old.room;assert(!room::host_posted(old));
    assert(room::host_posted(generation) && !room::host_posted(generation));
    fake::clock+=501;room::ui_tick();room::tick();
    pump_until([] {
        std::lock_guard g(fake::requests_lock);
        for(const auto& line:fake::requests) {
            const auto message=online_room_core::parse(line);
            if(message.text("op")=="presence" && online_room_core::parse(message["metadata"].text)["joinable"].text=="true") return true;
        }
        return false;
    });
    room::leave();pump_until([] { return room::worker_done.load(); });
    bool hosted=false,opened=false;
    for(const auto& line:fake::requests) {
        const auto message=online_room_core::parse(line);
        if(message.text("op")=="host") {
            const auto metadata=online_room_core::parse(message["metadata"].text);
            assert(metadata.number("max_players")==3 && metadata["joinable"].text=="false" && metadata.text("game_mode")=="danger_room");hosted=true;
        }
        if(message.text("op")=="presence" && online_room_core::parse(message["metadata"].text)["joinable"].text=="true") opened=true;
    }
    assert(hosted && opened && !room::native_posting);
    std::cout<<"PASS native Host wire metadata: accepted name/max=3, initially unjoinable, current-generation activation publishes joinable; no extra v2 field\n";
}
void native_target_test() {
    reset("success");assert(room::resolve("ABCDEFGH"));pump_until([] { return room::view().state==b::phase::room && room::cert_ready; });
    const auto generation=room::view().generation;
    std::uint32_t address=123;assert(!room::admission_target(generation,address) && address==0);
    room::ice_ready=true;
    HSteamNetConnection local,peer;assert(room::sockets()->CreateSocketPair(&local,&peer,false,nullptr,nullptr));
    auto& tunnel=room::ensure_tunnel(room::host_id);tunnel.connection=local;tunnel.connected=false;
    assert(!room::admission_target(generation,address));
    SteamNetConnectionStatusChangedCallback_t status{};status.m_hConn=local;status.m_info.m_eState=k_ESteamNetworkingConnectionState_Connected;
    status.m_info.m_identityRemote.ParseString(online_room_core::gns_identity(room::host_id).c_str());
    assert(room::on_status(&status));assert(room::admission_target(generation,address) && address==room::members.address(room::host_id));
    auto old=generation;--old.room;assert(!room::admission_target(old,address) && address==0);
    room::cert_ready=false;assert(!room::admission_target(generation,address));room::cert_ready=true;
    room::ice_ready=false;assert(!room::admission_target(generation,address));room::ice_ready=true;
    const auto authenticated=room::client;room::client.disconnected("invented broker loss",fake::clock);
    assert(!room::admission_target(generation,address) && tunnel.connected);room::client=authenticated;
    tunnel.transport_bound=true;assert(room::master_list_entry(nullptr,nullptr)==nullptr); // still room-only after native hello
    char packet[32]{};packet[2]=0x18;assert(room::room_recvfrom(0,packet,sizeof(packet),0,nullptr,nullptr)==SOCKET_ERROR && WSAGetLastError()==WSAEWOULDBLOCK);
    packet[2]=0x19;assert(room::room_recvfrom(0,packet,sizeof(packet),0,nullptr,nullptr)==17);
    status.m_info.m_identityRemote.ParseString("str:legends:2222222222222222");assert(room::on_status(&status));
    assert(!room::admission_target(generation,address) && tunnel.connection==k_HSteamNetConnection_Invalid);
    room::arrivals.push_back({0x0a4d0001,5165,{1,2,3}});room::arrivals_bytes=3;
    const auto old_connection=room::connection_generation.load();room::leave();pump_until([] { return room::worker_done.load(); });
    assert(room::arrivals.empty() && !room::arrivals_bytes && room::tunnels.empty() && !room::own_virtual());
    assert(room::configured_code.empty() && !room::wanted);
    packet[2]=0x18;assert(room::room_recvfrom(0,packet,sizeof(packet),0,nullptr,nullptr)==17); // Leave restores legacy UDP discovery immediately
    assert(!room::on_status(&status));room::sockets()->CloseConnection(peer,0,"fixture complete",false);
    fake::clock+=2000;assert(room::resolve("23456789"));pump_until([] { return room::view().state==b::phase::room; });
    const auto current=room::view().generation;assert(current!=generation && !room::admission_target(generation,address));
    {
        std::lock_guard g(room::lock);
        for(const auto* line:{R"({"op":"peer_joined","identity":"legends:2222222222222222"})",R"({"op":"room_closed"})",R"({"op":"_closed","kind":"broker_unreachable"})"})
            room::inbox.push_back({generation,old_connection,line});
    }
    room::ui_tick();room::tick();assert(room::view().generation==current && room::view().authenticated && room::members.active_size()==2);
    room::leave();pump_until([] { return room::worker_done.load(); });
    std::cout<<"PASS production admission target: certificate/ICE/current generation/proven host identity, no master/plain UDP discovery, mismatch refusal, Leave queues and old callbacks/events discarded\n";
}
}

// ---- Rule 64 (review F finding 1): the room glue itself - removal_receive through the real poll(), removal_view's facts,
// the connection-up floor, removal_deliver_tick's retries, removal_start / removal_send_tick on real GNS socket pairs, the
// connection binding (finding 2), the responsive window and its close text (finding 3), the host's close text on the
// removed PC. Only gns_transport (the game reads) is the fixture's: fake::party and the recording deliver_removal.
namespace rx = online_removal_core;
void removal_wait(HSteamNetConnection c) { for(int i=0;i<200;++i) { SteamNetConnectionRealTimeStatus_t st{};room::sockets()->GetConnectionRealTimeStatus(c,&st,0,nullptr);if(!st.m_cbPendingUnreliable && !st.m_cbPendingReliable) break;std::this_thread::sleep_for(std::chrono::milliseconds(1)); } }
std::vector<std::vector<std::uint8_t>> removal_read(HSteamNetConnection c,std::uint8_t kind) {
    std::vector<std::vector<std::uint8_t>> out;
    for(int round=0;round<20;++round) {
        SteamNetworkingMessage_t* m[32];const int n=room::sockets()->ReceiveMessagesOnConnection(c,m,32);
        for(int i=0;i<n;++i) { const auto* d=static_cast<const std::uint8_t*>(m[i]->m_pData);if(m[i]->m_cbSize>=1 && d[0]==kind) out.emplace_back(d,d+m[i]->m_cbSize);m[i]->Release(); }
        if(n==0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return out;
}
void removal_send(HSteamNetConnection from,const std::vector<std::uint8_t>& bytes) {
    assert(room::sockets()->SendMessageToConnection(from,bytes.data(),std::uint32_t(bytes.size()),k_nSteamNetworkingSend_Reliable,nullptr)==k_EResultOK);
    removal_wait(from);
    for(int i=0;i<40;++i) { std::this_thread::sleep_for(std::chrono::milliseconds(5));room::poll(); } // the pair delivers on GNS's own thread
}
void removal_connect(room::tunnel& t,HSteamNetConnection local,const std::string& id) {
    t.connection=local;t.connected=false;
    SteamNetConnectionStatusChangedCallback_t status{};status.m_hConn=local;status.m_info.m_eState=k_ESteamNetworkingConnectionState_Connected;
    status.m_info.m_identityRemote.ParseString(online_room_core::gns_identity(id).c_str());
    assert(room::on_status(&status) && t.connected);
}
void removal_guest_test() {
    reset("success");assert(room::resolve("ABCDEFGH"));pump_until([] { return room::view().state==b::phase::room && room::cert_ready; });
    assert(room::room_ready && !room::hosting && room::host_id=="legends:1111111111111111");
    fake::party={};fake::party.session=true;fake::party.count=2;fake::party.participants[0].index=0;fake::party.participants[1].index=1;fake::party.participants[1].local=true;
    fake::party_ok=true;fake::delivered.clear();fake::deliver_ok=true;
    room::own_id="legends:4444444444444444"; // the fixture's own key is all zero, which no notice may name
    HSteamNetConnection local,peer;assert(room::sockets()->CreateSocketPair(&local,&peer,false,nullptr,nullptr));
    room::report_sequence=40;
    auto& host=room::ensure_tunnel(room::host_id);removal_connect(host,local,room::host_id);
    assert(host.removal_floor==40); // the floor is taken when the connection comes up (party_connected)
    room::report_sequence=45;
    // a second member's tunnel (not the host)
    const std::string other="legends:2222222222222222";assert(room::members.join(other));
    HSteamNetConnection local2,peer2;assert(room::sockets()->CreateSocketPair(&local2,&peer2,false,nullptr,nullptr));
    auto& member=room::ensure_tunnel(other);removal_connect(member,local2,other);
    rx::notice n;std::memcpy(n.code,room::room_code.c_str(),8);
    assert(online_party_core::key_from_identity(room::host_id,n.host) && online_party_core::key_from_identity(room::own_id,n.target));
    n.index=1;n.heard=44;n.serial=9;
    auto variant=[&](auto edit) { auto m=n;edit(m);return rx::encode(m); };
    // The removal view is built from the room's real state.
    {
        const auto v=room::removal_view(host);
        assert(v.room && !v.hosting && v.from_host_tunnel && v.floor==40 && v.sent==45 && v.session && v.own_index==1 && std::memcmp(v.code,"ABCDEFGH",8)==0);
        assert(!room::removal_view(member).from_host_tunnel);
    }
    removal_send(peer2,rx::encode(n));                                  // the same notice on another member's tunnel
    assert(!room::removal_in.removed() && room::removal_in.refused(rx::refusal::not_host)==1);
    removal_send(peer,variant([](rx::notice& m) { m.heard=40; }));      // the floor itself: an earlier connection's
    removal_send(peer,variant([](rx::notice& m) { m.heard=46; }));      // above anything this PC sent
    removal_send(peer,variant([](rx::notice& m) { m.index=2; }));       // not this PC's index
    removal_send(peer,variant([](rx::notice& m) { std::memcpy(m.code,"ABCDEFGJ",8); }));
    assert(!room::removal_in.removed() && room::removal_in.refused(rx::refusal::stale)==1 && room::removal_in.refused(rx::refusal::future)==1 &&
           room::removal_in.refused(rx::refusal::session)==1 && room::removal_in.refused(rx::refusal::room)==1 && room::removal_deliver<0);
    room::hosting=true;removal_send(peer,rx::encode(n));room::hosting=false; // a hosting PC is never removed
    assert(room::removal_in.refused(rx::refusal::not_host)==2 && !room::removal_in.removed());
    // Delivery retries while the native session lasts (finding 6).
    fake::deliver_ok=false;
    removal_send(peer,rx::encode(n));
    assert(room::removal_in.removed() && room::removed_by_host() && room::removal_deliver==1 && fake::delivered.empty() && !room::host_lost());
    room::removal_deliver_tick();assert(room::removal_deliver==1 && fake::delivered.size()==1 && fake::delivered[0]==1);
    for(int i=0;i<3;++i) { fake::clock+=3000;room::removal_deliver_tick(); }
    assert(room::removal_deliver==1 && fake::delivered.size()==4 && room::removal_deliver_slow_logged);
    removal_send(peer,rx::encode(n));                                   // a resend: repeat, nothing new
    assert(room::removal_in.refused(rx::refusal::repeat)==1 && room::removal_repeat_logged && fake::delivered.size()==4);
    fake::deliver_ok=true;room::removal_deliver_tick();
    assert(room::removal_deliver<0 && fake::delivered.size()==5 && fake::delivered.back()==1);
    // The host connection ends now: no host loss (the panel stays down).
    SteamNetConnectionStatusChangedCallback_t closed{};closed.m_hConn=local;closed.m_info.m_eState=k_ESteamNetworkingConnectionState_ClosedByPeer;
    std::strcpy(closed.m_info.m_szEndDebug,"closed");host.transport_bound=true;
    room::on_status(&closed);assert(!room::host_lost() && room::removed_by_host());
    room::sockets()->CloseConnection(peer,0,nullptr,false);room::sockets()->CloseConnection(peer2,0,nullptr,false);room::sockets()->CloseConnection(local2,0,nullptr,false);
    room::leave();pump_until([] { return room::worker_done.load(); });assert(!room::removed_by_host() && room::removal_deliver<0);
    // A session that ends before the delivery: the pending delivery is dropped (the latch already tells the Hub).
    reset("success");assert(room::resolve("ABCDEFGH"));pump_until([] { return room::view().state==b::phase::room && room::cert_ready; });
    assert(room::sockets()->CreateSocketPair(&local,&peer,false,nullptr,nullptr));
    room::own_id="legends:4444444444444444";room::report_sequence=50;auto& host2=room::ensure_tunnel(room::host_id);removal_connect(host2,local,room::host_id);room::report_sequence=51;
    fake::deliver_ok=false;fake::delivered.clear();n.heard=51;removal_send(peer,rx::encode(n));assert(room::removal_deliver==1);
    fake::party_ok=false;room::removal_deliver_tick();assert(room::removal_deliver<0 && room::removed_by_host());
    // The host's close text on the authenticated host connection is the removal too (finding 3): no notice taken before.
    room::leave();pump_until([] { return room::worker_done.load(); });
    reset("success");assert(room::resolve("ABCDEFGH"));pump_until([] { return room::view().state==b::phase::room && room::cert_ready; });
    room::sockets()->CloseConnection(peer,0,nullptr,false);
    assert(room::sockets()->CreateSocketPair(&local,&peer,false,nullptr,nullptr));
    auto& host3=room::ensure_tunnel(room::host_id);removal_connect(host3,local,room::host_id);host3.transport_bound=true;
    closed.m_hConn=local;std::strcpy(closed.m_info.m_szEndDebug,rx::close_text);
    room::on_status(&closed);assert(room::removed_by_host() && !room::host_lost());
    // ...and another close text is the host loss it always was.
    room::leave();pump_until([] { return room::worker_done.load(); });
    reset("success");assert(room::resolve("ABCDEFGH"));pump_until([] { return room::view().state==b::phase::room && room::cert_ready; });
    room::sockets()->CloseConnection(peer,0,nullptr,false);
    assert(room::sockets()->CreateSocketPair(&local,&peer,false,nullptr,nullptr));
    auto& host4=room::ensure_tunnel(room::host_id);removal_connect(host4,local,room::host_id);host4.transport_bound=false;
    room::guest_admitted(); // late guest: accepted local participant, no host native link yet
    closed.m_info.m_eState=k_ESteamNetworkingConnectionState_ProblemDetectedLocally;
    closed.m_info.m_eEndReason=5003; // 060s g4: unbound admitted host tunnel, Connection dropped
    closed.m_hConn=local;std::strcpy(closed.m_info.m_szEndDebug,"closed by the application");
    room::on_status(&closed);assert(!room::removed_by_host() && room::host_lost());
    room::sockets()->CloseConnection(peer,0,nullptr,false);
    room::leave();pump_until([] { return room::worker_done.load(); });fake::party_ok=false;
    std::cout<<"PASS rule 64 guest glue: real poll() routing, removal_view facts (floor at connection-up, sent, session, host tunnel), not-host / stale / future / session / room / hosting refusals, accept latch, delivery retried every tick and dropped when the session ends, repeats silent, host close text = removal, other close = host loss\n";
}
void removal_host_test() {
    reset("success");assert(room::host("Game",false));pump_until([] { return room::view().state==b::phase::room && room::cert_ready; });
    assert(room::hosting && room::room_ready);
    room::own_id=room::host_id="legends:5555555555555555"; // the fixture's own key is all zero, which no notice may carry
    fake::party={};fake::party.session=true;fake::party.hosting=true;fake::party_ok=true;
    const std::string id="legends:3333333333333333";assert(room::members.join(id));
    HSteamNetConnection local,peer;assert(room::sockets()->CreateSocketPair(&local,&peer,false,nullptr,nullptr));
    auto& t=room::ensure_tunnel(id);removal_connect(t,local,id);
    room::party_tick(); // opens the party model with this roster
    online_party_core::key who{};assert(online_party_core::key_from_identity(id,who));
    HSteamNetConnection cur=peer; // the member end the reports come from
    auto report=[&](std::uint32_t seq) {
        online_party_core::report r;std::memcpy(r.code,room::room_code.c_str(),8);r.seq=seq;r.now=online_party_core::phase::playing;r.index=1;r.token="FIXTURE";r.name="B";
        removal_send(cur,online_party_core::encode(r));
    };
    using R=online_room::removal_start_result;
    assert(room::removal_start(local,0)==R::bad_index && room::removal_start(local,4)==R::bad_index);
    report(5);assert(room::party_model.heard(who)==5);
    assert(room::removal_start(local,1)==R::started && room::removal_start(local,1)==R::pending);
    auto got=removal_read(peer,rx::link_kind);
    assert(got.size()==1);rx::notice n;assert(rx::parse(got[0].data(),got[0].size(),n)==rx::refusal::none);
    assert(n.index==1 && n.heard==5 && n.target==who && std::memcmp(n.code,room::room_code.c_str(),8)==0);
    const auto serial=n.serial;
    fake::clock+=100;room::removal_send_tick();assert(removal_read(peer,rx::link_kind).empty());
    fake::clock+=200;room::removal_send_tick();got=removal_read(peer,rx::link_kind);
    assert(got.size()==1 && rx::parse(got[0].data(),got[0].size(),n)==rx::refusal::none && n.serial==serial);
    // Finding 2: the member's connection is replaced (same tunnel object, new handle, connected): the old Kick ends there.
    HSteamNetConnection local2,peer2;assert(room::sockets()->CreateSocketPair(&local2,&peer2,false,nullptr,nullptr));
    room::sockets()->CloseConnection(local,0,nullptr,false);
    t.connection=local2;t.connected=true;room::party_model.reconnected(who);cur=peer2;report(1);
    fake::clock+=300;room::removal_send_tick();
    assert(room::removal_out.targets().empty() && removal_read(peer2,rx::link_kind).empty());
    // Finding 3: a new Kick on the new connection; the member stalls (no report): no drop at 10 s, closed at the 60 s bound.
    assert(room::removal_start(local2,1)==R::started);
    for(int i=0;i<200;++i) { fake::clock+=50;room::removal_send_tick(); } // 10 s, member stalled after its last report
    assert(!room::removal_out.targets().empty() && t.connected);
    for(int i=0;i<1100 && !room::removal_out.targets().empty();++i) { fake::clock+=50;room::removal_send_tick(); }
    assert(room::removal_out.targets().empty() && !t.connected && t.connection==k_HSteamNetConnection_Invalid);
    SteamNetConnectionInfo_t info{};
    for(int i=0;i<500;++i) { assert(room::sockets()->GetConnectionInfo(peer2,&info));if(info.m_eState!=k_ESteamNetworkingConnectionState_Connected) break;std::this_thread::sleep_for(std::chrono::milliseconds(2)); }
    assert(info.m_eState==k_ESteamNetworkingConnectionState_ClosedByPeer && std::strcmp(info.m_szEndDebug,rx::close_text)==0);
    removal_read(peer2,rx::link_kind);room::sockets()->CloseConnection(peer2,0,nullptr,false);room::sockets()->CloseConnection(peer,0,nullptr,false);
    // A responsive member that never takes the notice: closed after 10 s of responsive time.
    HSteamNetConnection local3,peer3;assert(room::sockets()->CreateSocketPair(&local3,&peer3,false,nullptr,nullptr));
    removal_connect(t,local3,id);room::party_model.reconnected(who);
    cur=peer3;report(1);
    assert(room::removal_start(local3,1)==R::started);
    const auto started=fake::clock.load();std::uint32_t seq=1;
    for(int i=0;i<400 && !room::removal_out.targets().empty();++i) { fake::clock+=50;if(i%20==0) report(++seq);room::removal_send_tick(); }
    const auto took=fake::clock.load()-started;
    assert(room::removal_out.targets().empty() && !t.connected && took>=10000 && took<11000);
    room::sockets()->CloseConnection(peer3,0,nullptr,false);
    room::leave();pump_until([] { return room::worker_done.load(); });fake::party_ok=false;
    std::cout<<"PASS rule 64 host glue: removal_start results, first notice at once on the member's connection (index, heard, code, target), 250 ms cadence with the same serial, a replaced connection ends the old Kick (nothing sent on the new one), a stalled member closed only at the 60 s bound, a responsive one after 10 s, close text on the wire\n";
}
namespace closure_fixture {
struct packet : SteamNetworkingMessage_t {
    std::vector<std::uint8_t> bytes;
    explicit packet(std::vector<std::uint8_t> b):SteamNetworkingMessage_t{},bytes(std::move(b)) {
        m_pData=bytes.data();m_cbSize=int(bytes.size());
        m_pfnRelease=[](SteamNetworkingMessage_t* m) { delete static_cast<packet*>(m); };
    }
};
std::deque<SteamNetworkingMessage_t*> queued;
unsigned heartbeats=0,closed=0;
bool close(HSteamNetConnection c,const char*,bool) {
    assert(c==123);++closed;
    while(!queued.empty()) { queued.front()->Release();queued.pop_front(); }
    return true;
}
EResult heartbeat(HSteamNetConnection c) { assert(c==123);++heartbeats;return k_EResultOK; }
int receive(HSteamNetConnection,SteamNetworkingMessage_t** out,int max) {
    int n=0;while(n<max && !queued.empty()) { out[n++]=queued.front();queued.pop_front(); }return n;
}
void send(std::vector<std::uint8_t> bytes) { queued.push_back(new packet(std::move(bytes))); }
room::tunnel& setup() {
    assert(queued.empty());room::tunnels.clear();room::client={};room::host_loss.reset();room::removal_reset();
    room::hosting=false;fake::clock=10000;room::room_clock=&fake::now;room::room_ready=true;room::room_code="ABCDEFGH";
    room::own_id="legends:1111111111111111";room::host_id="legends:2222222222222222";
    assert(room::members.reset({room::host_id,room::own_id}));
    assert(room::client.begin(b::operation::resolve,fake::clock,true) && room::client.issue());
    assert(room::client.joined(room::view().generation,b::operation::resolve,"ABCDEFGH"));
    room::guest_admitted();room::host_loss.connected();room::receive_messages=receive;room::send_keepalive=heartbeat;room::close_connection=static_cast<bool(*)(HSteamNetConnection,const char*,bool)>(close);heartbeats=closed=0;
    auto host=std::make_unique<room::tunnel>();host->id=room::host_id;host->connection=123;host->connected=true;host->removal_floor=40;
    room::tunnels.push_back(std::move(host));room::report_sequence=45;
    fake::party={};fake::party.session=true;fake::party.count=1;fake::party.participants[0].local=true;fake::party.participants[0].index=1;
    fake::party_ok=true;fake::delivered.clear();fake::deliver_ok=true;
    return *room::tunnels.back();
}
void close(const char* reason="host_left",bool stale=false) {
    room::inbox.push_back({room::view().generation,room::connection_generation.load()-unsigned(stale),
        "{\"op\":\"room_closed\",\"reason\":"+online_room_core::quote(reason)+"}"});
    room::process_inbox();
}
void removal() {
    rx::notice n;std::memcpy(n.code,room::room_code.c_str(),8);
    assert(online_party_core::key_from_identity(room::host_id,n.host) && online_party_core::key_from_identity(room::own_id,n.target));
    n.index=1;n.heard=44;n.serial=9;send(rx::encode(n));
}
void run(const std::string& name) {
    auto& host=setup();
    if(name=="healthy" || name=="expiry") {
        close(name=="expiry" ? "expired" : "host_left");assert(!room::host_lost());
        for(int i=0;i<120;++i) { fake::clock+=100;room::unbound_keepalive_tick();send({3});room::poll();assert(!room::host_lost() && fake::delivered.empty()); }
        assert(fake::party.session && host.connected && room::view().state==b::phase::room && heartbeats==120);
        room::unbound_keepalive_tick();assert(heartbeats==120); // no duplicate at the same tick
        host.transport_bound=true;fake::clock+=100;room::unbound_keepalive_tick();assert(heartbeats==120);
    } else if(name=="closure-removal" || name=="removal-closure") {
        if(name=="removal-closure") { removal();room::poll();assert(room::removed_by_host()); }
        close();fake::clock+=2950;
        if(name=="closure-removal") { removal();room::poll(); }
        assert(room::removed_by_host() && !room::host_lost());
        assert(room::removal_view(host).room); // admission closed, authenticated removal context still owned
        fake::clock+=10000;room::poll();room::removal_deliver_tick();
        assert(room::removed_by_host() && !room::host_lost() && fake::delivered==std::vector<unsigned>{1});
        removal();room::poll();assert(room::removal_in.refused(rx::refusal::repeat)==1);
    } else if(name=="silent" || name=="wrap" || name=="recent") {
        if(name=="wrap") fake::clock=0xfffffff0u;
        close();assert(!room::host_lost());fake::clock+=2999;room::poll();assert(!room::host_lost());
        if(name=="recent") { send({3});room::poll();++fake::clock;room::poll();assert(!room::host_lost());fake::clock+=2998;room::poll();assert(!room::host_lost()); }
        ++fake::clock;room::poll();assert(room::host_lost() && !room::removed_by_host());
    } else if(name=="queued-removal") {
        close();fake::clock+=3000;
        for(int i=0;i<64;++i) send({});
        removal();room::poll();assert(!room::host_lost() && !room::removed_by_host());
        room::poll();assert(room::removed_by_host() && !room::host_lost());
    } else if(name=="closed-removal" || name=="closed-batch-removal") {
        close("host_left");
        if(name=="closed-batch-removal") for(int i=0;i<64;++i) send({});
        removal();room::wanted=true;
        SteamNetConnectionStatusChangedCallback_t event{};event.m_hConn=123;
        event.m_info.m_eState=k_ESteamNetworkingConnectionState_ClosedByPeer;
        std::strcpy(event.m_info.m_szEndDebug,"host quit");
        room::on_status(&event);
        assert(closed==1 && room::removed_by_host() && !room::host_lost());
        room::wanted=false;
    } else if(name=="guards") {
        for(const char* guard:{"local","removed","unadmitted","hosting","stale","unauthenticated","foreign-host","service"}) {
            setup();const std::string g=guard;
            if(g=="local") room::host_loss.local_departure();
            if(g=="removed") room::host_loss.removed_by_host();
            if(g=="unadmitted") room::host_loss.reset();
            if(g=="hosting") room::hosting=true;
            if(g=="unauthenticated") room::client.disconnected("broker unavailable",fake::clock);
            if(g=="foreign-host") room::host_id="legends:3333333333333333";
            if(g=="service") { room::inbox.push_back({room::view().generation,room::connection_generation.load(),R"({"op":"_closed","kind":"broker_unreachable"})"});room::process_inbox(); }
            else close("host_left",g=="stale");
            fake::clock+=20000;room::poll();assert(!room::host_lost());
        }
    } else assert(false && "unknown closure case");
    assert(queued.empty());room::tunnels.clear();fake::party_ok=false;
    std::cout<<"PASS offline closure "<<name<<": production broker inbox + P2P poll + rule-64 receiver; no GNS initialization or sockets\n";
}
}
void room_closed_offline(const std::string& only={}) {
    bool ran=false;
    for(const char* name:{"healthy","expiry","closure-removal","removal-closure","silent","recent","wrap","queued-removal","closed-removal","closed-batch-removal","guards"})
        if(only.empty() || only==name) { closure_fixture::run(name);ran=true; }
    if(!ran) { std::cerr<<"unknown offline closure case\n";std::exit(2); }
}
int main(int argc,char** argv) {
    if(argc>=2 && std::string(argv[1])=="--room-closed-offline") { std::cout<<std::unitbuf;room_closed_offline(argc>2 ? argv[2] : "");return 0; }
    if(argc!=1) { std::cerr<<"unknown broker fixture argument\n";return 2; }
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
    native_host_metadata_test();native_target_test();
    removal_guest_test();removal_host_test();
    certificate_identity_outside_room_test();certificate_renewal_test();certificate_repeat_in_room_test();
    reset("success");assert(room::list());pump_until([] { return room::view().state==b::phase::listing; });
    room::worker_generation=room::connection_generation.load();room::worker_ticket=room::view().generation;
    { std::lock_guard g(room::lock);for(unsigned i=0;i<256;++i) room::inbox.push_back({room::worker_ticket,room::worker_generation,"{}"}); }
    room::arrived("{}");room::ui_tick();assert(room::view().error=="inbox_overflow");
    room::leave();pump_until([] { return room::worker_done.load(); });GameNetworkingSockets_Kill();
    std::cout<<"PASS production broker worker: mock TLS outcomes (not cryptographic pin verification), auth/list/host/resolve/reentry, malformed/oversize/duplicate/error/timeout/mismatch, limiter, cancel and stale inbox\n";
}
