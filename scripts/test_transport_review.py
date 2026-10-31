"""Offline regression for the handoff service/close paths and diagnostic wiring.

Compile the production functions verbatim against an invented peer/socket sink;
no Steam initialization, sockets, native entry point or game is executed.
"""
from pathlib import Path
import argparse
import ctypes
import re
import subprocess


def function(source, name):
    match = re.search(r"^\s*(?:bool|void|int)\s+(?:__\w+\s+)?" + name + r"\(", source, re.M)
    assert match, name
    start = source.index("{", match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    kernel = ctypes.WinDLL("kernel32")
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.SetPriorityClass.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
    assert kernel.SetPriorityClass(kernel.GetCurrentProcess(), 0x40)
    repo = Path(__file__).resolve().parents[1]
    source = (repo / "src/gns_transport.cpp").read_text()
    failures = []
    for name in ("log_self_drop_checksums", "log_self_drop_parent", "log_removal_stack", "notice_filter"):
        body = function(source, name)
        if re.search(r"logger::write\(", body):
            failures.append(f"{name}: repeatable drop diagnostics bypass write_capped")
    refusal = function((repo / "src/join_in_progress.cpp").read_text(), "refuse_started_admission")
    if re.search(r"logger::write\(", refusal):
        failures.append("late refusal: repeatable success/fault diagnostics bypass write_capped")
    preamble = r'''
#include <winsock2.h>
#include "gns_transport_core.hpp"
#include "gns_transport_rules.hpp"
#include <cstdarg>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>
using namespace gns_transport_rules;
using gns_transport_core::message;
constexpr int k_HSteamNetConnection_Invalid=0, k_EResultOK=1, k_EResultLimitExceeded=25;
constexpr int k_nSteamNetworkingSend_Unreliable=0, k_nSteamNetworkingSend_NoNagle=1;
constexpr int k_nSteamNetworkingSend_UnreliableNoNagle=1, k_nSteamNetworkingSend_Reliable=8;
std::vector<std::string> logs;
namespace logger {
void write(const char* format,...) { char line[2048];va_list a;va_start(a,format);vsnprintf(line,sizeof line,format,a);va_end(a);logs.emplace_back(line); }
void write_once(const char*,const char*,...) {}
void write_capped(const char*,const char*,...) {}
}
namespace online_room { void set_transport_bound(int,bool) {} }
struct socket_sink {
    std::vector<int> flags;
    bool CloseConnection(int,int,const char*,bool) { return true; }
    int SendMessageToConnection(int,const void*,std::uint32_t,int flag,void*) { flags.push_back(flag);return k_EResultOK; }
} sink;
socket_sink* sockets() { return &sink; }
struct link {
    std::uint8_t* peer=nullptr;
    int connection=1;
    bool tunnel=false,bound=true,switched=false,closed=false,handoff_logged=false;
    const char* gone_why=nullptr;
    std::uint32_t address=0;std::uint16_t port=0;DWORD opened=0;
    std::uint64_t sent=0,sent_bytes=0,received=0,received_bytes=0,deferred=0;
    gns_transport_core::outbox out{128,2};
    gns_transport_core::inbox in;
};
std::vector<std::unique_ptr<link>> links;
void* manager_=reinterpret_cast<void*>(1);
unsigned enqueues=0,enqueued_gns=0,directs=0,sent_by[256][4]{};
const char* context="fixture";
template<class T>T& at(void* base,std::uint32_t offset) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base)+offset); }
std::string text(std::uint32_t,std::uint16_t) { return "fixture-peer"; }
bool rare(std::uint8_t) { return false; }
void activity() {}
link* bound_for_endpoint(void*,const void*) { return links[0].get(); }
std::uint16_t body_length(std::uint8_t* header) { return at<std::uint16_t>(header,8); }
'''
    extracted = "\n".join(function(source, name) for name in ("close", "send_now", "send", "enqueue", "direct"))
    tests = r'''
int failures=0;
void check(bool ok,const char* label) { printf("%s %s\n",ok ? "PASS" : "FAIL",label);if(!ok)++failures; }
bool logged(const char* word) { for(const auto& s:logs)if(s.find(word)!=std::string::npos)return true;return false; }
int main() {
    std::vector<std::uint8_t> peer(0x4000),header(0x20),address(12);
    auto reset=[&](bool switched) {
        links.clear();links.push_back(std::make_unique<link>());
        links[0]->peer=peer.data();links[0]->switched=switched;
        sink.flags.clear();logs.clear();header.assign(0x20,0);
    };
    for(bool switched:{false,true}) for(unsigned op:{0x0au,0x0fu,0x10u,0x11u}) {
        reset(switched);header[0x10]=std::uint8_t(op);
        enqueue(manager_,nullptr,header.data());
        check(sink.flags==std::vector<int>{k_nSteamNetworkingSend_UnreliableNoNagle},"enqueue ACK/ping bypasses queue with UnreliableNoNagle");
        check(links[0]->out.pending()==0,"service never joins held gameplay");
        sink.flags.clear();direct(manager_,address.data(),0,header.data());
        check(sink.flags==std::vector<int>{k_nSteamNetworkingSend_UnreliableNoNagle},"direct ACK/ping bypasses queue with UnreliableNoNagle");
    }
    reset(false);header[0x10]=2;header[0xe]=1;
    enqueue(manager_,nullptr,header.data());enqueue(manager_,nullptr,header.data());
    check(sink.flags.empty() && links[0]->out.pending()==2,"handoff holds reliable gameplay without native/GNS reorder");
    enqueue(manager_,nullptr,header.data());
    check(links[0]->closed && links[0]->connection==0 && !links[0]->out.pending(),"overflow terminates held link and releases queues");
    check(logged("handoff-outbound-overflow") && logged("held=2"),"overflow logs classified failure and held depth before clear");
    const auto terminal_logs=logs.size();
    enqueue(manager_,nullptr,header.data());direct(manager_,address.data(),0,header.data());close(*links[0],"repeat close",false);
    check(logs.size()==terminal_logs && links[0]->out.pending()==0,"terminal link cannot recapture gameplay or repeat its failure report");
    reset(false);header[0x10]=2;header[0xe]=1;
    std::vector<std::uint8_t> body(70,0x5a);at<const std::uint8_t*>(header.data(),0)=body.data();at<std::uint16_t>(header.data(),8)=70;
    enqueue(manager_,nullptr,header.data());enqueue(manager_,nullptr,header.data());
    check(links[0]->closed && logged("handoff-outbound-overflow") && logged("held=1"),"byte-limit overflow uses the same classified termination");
    reset(false);enqueue(manager_,nullptr,header.data()); // header is reset; any gameplay is staged
    close(*links[0],"fixture connection failure",false);
    check(logged("handoff-link-closed") && logged("held=1"),"connection close before switch logs classified failure and held depth");
    check(links[0]->closed && links[0]->connection==0,"connection close ends link instead of unsafe native fallback");
    reset(false);links[0]->peer=reinterpret_cast<std::uint8_t*>(1);close(*links[0],"native peer already freed",false);
    check(links[0]->closed && logged("handoff-link-closed"),"close diagnostics never dereference a removed native peer");
    reset(false);header[0x10]=2;
    direct(manager_,address.data(),0,header.data());direct(manager_,address.data(),0,header.data());direct(manager_,address.data(),0,header.data());
    check(links[0]->closed && logged("handoff-outbound-overflow"),"direct handoff overflow is classified too");
    reset(true);close(*links[0],"ordinary close",false);
    check(!logged("handoff-link-closed"),"post-switch close is not misclassified as a handoff failure");
    reset(false);header[0x10]=0x0f;header[0xe]=1;enqueue(manager_,nullptr,header.data());
    check(sink.flags.empty() && links[0]->out.pending()==1,"a reliable opcode never enters the unreliable service lane");
    return failures ? 1 : 0;
}
'''
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / "fixture.cpp").write_text(preamble + extracted + tests, encoding="utf-8", newline="\n")
    (out / "CMakeLists.txt").write_text('cmake_minimum_required(VERSION 3.21)\nproject(transport_review LANGUAGES CXX)\nset(CMAKE_CXX_STANDARD 20)\nadd_executable(transport_review fixture.cpp)\ntarget_compile_definitions(transport_review PRIVATE NDEBUG WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)\ntarget_include_directories(transport_review PRIVATE "' + (repo / "src").as_posix() + '")\n', encoding="utf-8", newline="\n")
    subprocess.run(["cmake", "-S", str(out), "-B", str(out / "build"), "-A", "Win32"], check=True)
    subprocess.run(["cmake", "--build", str(out / "build"), "--config", "Release", "--", "/m:1", "/nr:false"], check=True)
    result = subprocess.run([str(out / "build/Release/transport_review.exe")])
    for failure in failures:
        print("FAIL", failure)
    print("diagnostic wiring:", len(failures), "failures")
    return bool(result.returncode or failures)


if __name__ == "__main__":
    raise SystemExit(main())
