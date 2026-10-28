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
    // Round 8 (the in-session Party screen): the native participant's name, whether it is this PC / the host, and the
    // reported campaign-transfer percentage (-1 none). The lobby cards ignore these (they read the native rows).
    struct member { unsigned native_index=0; int ping_ms=-1; bool same_build=false;
        bool ping_to_host=false; int route=0; int build=-1; int phase=-1;
        char name[32]{}; bool self=false, host=false; int percent=-1; };
    member members[4]{};
    unsigned count=0;
    // Broker-roster members without a native row yet (Connecting / Checking build): never kick targets.
    struct pending_member { char name[32]{}; int phase=0; int build=0; int ping_ms=-1; bool ping_to_host=false; int route=0; };
    pending_member pending[3]{};
    unsigned pending_count=0;
    // Round 8: this PC hosts the native session / a native session is set up (online_room::party).
    bool hosting=false, session=false;
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
    // Optional (round 8): the host removes the remote participant with this native index through the transport's agreed
    // departure (never a pre-game leave). Refused (false) unless this PC hosts a running session and the index is a
    // remote participant it has a bound link to; a repeated call for the same loss is a no-op.
    bool (*remove_member)(unsigned native_index)=nullptr;
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
