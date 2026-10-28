#pragma once
#include <Windows.h>
#include "patch_transaction.hpp"
#include "online_browser_core.hpp"

// Guarded UI intents and snapshot bindings. Room operations use online_room;
// native admission and participant allocation retain their native owners.
namespace online_menus {
// Lobby card facts. Host fixtures supply them through install_hooks::lobby_fields;
// production through broker_api::party (online_room, docs/online-party.md section 8).
// None of these values confers native admission, readiness, Kick or Start.
struct lobby_fixture {
    char code[10]{};
    // Round-7 fields default to "not supplied" so round-4 fixtures keep their meaning:
    // build<0 -> same_build decides; phase<0 -> no phase text; route 0 unknown, 1 direct, 2 relay.
    struct member { unsigned native_index=0; int ping_ms=-1; bool same_build=false;
        bool ping_to_host=false; int route=0; int build=-1; int phase=-1; };
    member members[4]{};
    unsigned count=0;
    // Broker-roster members without a native row yet (Connecting / Checking build): never kick targets.
    struct pending_member { char name[32]{}; int phase=0; int build=0; int ping_ms=-1; bool ping_to_host=false; int route=0; };
    pending_member pending[3]{};
    unsigned pending_count=0;
};
struct broker_api {
    bool (*available)()=nullptr;
    const online_browser_core::client_view& (*view)()=nullptr;
    void (*tick)()=nullptr;
    bool (*list)()=nullptr;
    bool (*host)(const std::string&,bool)=nullptr;
    bool (*resolve)(const std::string&)=nullptr;
    bool (*retry)()=nullptr;
    void (*cancel)()=nullptr;
    void (*leave)()=nullptr;
    std::string (*version)()=nullptr;
    bool (*take_preset)(online_browser_core::launch_preset&)=nullptr;
    bool (*refresh_due)()=nullptr;
    bool (*host_native)(const std::string&,bool,int,bool)=nullptr;
    bool (*admission_target)(online_browser_core::ticket,std::uint32_t&)=nullptr;
    bool (*host_posted)(online_browser_core::ticket)=nullptr;
    bool (*drop_in)()=nullptr;
    // Optional: leave the room after the transport tick that flushes the quit notices a native session end just queued
    // (absent: the leave runs at once, as a Hub Leave does).
    void (*leave_deferred)()=nullptr;
    // Optional (round 7): the lobby cards' observed facts, and the measured RTT to a listed room's host.
    bool (*party)(lobby_fixture&)=nullptr;
    int (*room_rtt)(const std::string&)=nullptr;
    // Optional (round 15, presentation only): ms until the shared limiter/backoff allows a new request (0 = now); the rule
    // names the latest settings refusal gave ("" none); whether this joiner's connection to its room's host ended after it
    // was up. Absent seams mean "unknown": no countdown, no rule names, no host-left panel.
    std::uint32_t (*retry_in)()=nullptr;
    const char* (*refusal_rules)()=nullptr;
    bool (*host_lost)()=nullptr;
    std::uint32_t (*refusal_serial)()=nullptr; // with refusal_rules: names are shown only for a refusal that happened during this attempt
};
void keyboard(unsigned char* state);
bool requested();
bool installed();
// Optional host seams; production uses nullptr. Callback pointers are copied,
// not retained through this options object. No game/menu pointers cross threads.
struct install_hooks {
    DWORD (*clock)()=nullptr;
    std::uint64_t (*wall_clock)()=nullptr; // epoch seconds for the Hub's last-game age (host fixtures only)
    const patch_transaction::hooks* transaction=nullptr;
    void (*before_registration)()=nullptr;
    void (*before_request)()=nullptr;
    void (*before_tick)()=nullptr;
    bool (*lobby_fields)(lobby_fixture&)=nullptr;
    const broker_api* broker=nullptr;
};
void install(HMODULE game,const install_hooks* hooks=nullptr);
// Shared with the host fixture for actual rollback/readback fault injection.
patch_transaction::contribution contribution();
}
