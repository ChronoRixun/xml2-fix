#pragma once
#include <Windows.h>
#include "patch_transaction.hpp"
#include "online_browser_core.hpp"
#include "linked_frame_rate_core.hpp"

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
        char name[32]{}; bool self=false, host=false; int percent=-1;
        unsigned local_fps=linked_frame_rate_core::unknown_rate, session_fps=0, measured_fps_x10=0;
        linked_frame_rate_core::capacity rate_capacity=linked_frame_rate_core::capacity::unknown; };
    member members[4]{};
    unsigned count=0;
    // Broker-roster members without a native row yet (Connecting / Checking build): never kick targets.
    struct pending_member { char name[32]{}; int phase=0; int build=0; int ping_ms=-1; bool ping_to_host=false; int route=0; bool host=false;
        unsigned local_fps=linked_frame_rate_core::unknown_rate, session_fps=0, measured_fps_x10=0;
        linked_frame_rate_core::capacity rate_capacity=linked_frame_rate_core::capacity::unknown; };
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
    // Optional (round 15, presentation only): ms until the shared limiter/backoff allows a new request (0 = now); the rule
    // names the latest settings refusal gave ("" none); whether this joiner's connection to its room's host ended after it
    // was up. Absent seams mean "unknown": no countdown, no rule names, no host-left panel.
    std::uint32_t (*retry_in)()=nullptr;
    const char* (*refusal_rules)()=nullptr;
    bool (*host_lost)()=nullptr;
    std::uint32_t (*refusal_serial)()=nullptr; // with refusal_rules: names are shown only for a refusal that happened during this attempt
    // Optional (0.6 preview fix F, rule 64): this joiner accepted its room host's in-session removal notice (the Party Kick). A
    // session end after it is the removal (the Hub's "The host removed you from the game."), never a host loss.
    bool (*removed_by_host)()=nullptr;
    // Optional (0.6 preview fix J): the host's own departure notice ended this guest's session - its connection closing
    // afterwards is that departure, not a second loss.
    void (*suppress_host_lost)()=nullptr;
    void (*guest_admitted)()=nullptr;
    bool (*set_drop_in)(bool)=nullptr;
    bool (*drop_in_editable)()=nullptr;
};
void keyboard(unsigned char* state);
// Unicode WM_CHAR text is queued only for the owning LC editor activation.
// Returns true when consumed, including DI controls whose WM_CHAR copy is ignored.
bool text_entry_active();
bool text_entry_character(unsigned character,unsigned scan=0);
bool requested();
bool installed();
// The record+0x40 accessor the online menus put at 0x6141e0 (a JMP; `refresh-scoped display name getter`): every caller but
// the two guarded roster formatting reads (0x5ba66e / 0x5c829b, during a names refresh) gets the native record+0x40 pointer.
// join_in_progress's AS guard composes with exactly this JMP (int31: shipped Menus=1 refused JoinInProgress otherwise).
const void* display_name_getter();
// Diagnostics: how often the native session-end wrapper ran (one kick may clear twice; review E finding 6).
unsigned session_clear_count();
// Pad buttons on the co-op screens (online_menus_rules.hpp pad_menu_mask): `probe(player)` says whether that player's
// prompts show the pad (pad_prompts::pad_shown, wired by exports.cpp; null reads the raw menu mask). pad_prompt_scope() is
// true while one of the custom screens (Hub, Host, Code, Browse, Party) or a skinned lobby (lc_lobby_v1; fix E) is the
// current menu; an unskinned native lobby keeps the engine's labels and bits.
void set_pad_probe(bool (*probe)(int player));
bool pad_prompt_scope();
// Mouse hit boxes (online_menus_hits_rules.hpp): `probe` gives the engine-screen pixel (and that screen's size) of the
// pointer message the window procedure is handing the engine right now (display::pointer_pixel, wired by exports.cpp;
// null or false: the hit test works from the engine's view-unit point, to half a view unit).
void set_pointer_probe(bool (*probe)(int& x,int& y,int& screen_width,int& screen_height));
// The mouse diagnostic (fix K): with [Test] InputPipe=1 (read at install) every mouse message on a co-op screen writes one
// log line (raw point, bar, handed point, the model's answer, the engine's focus / hover after its walk). Host fixtures switch
// it with set_hits_trace and register their item-test recorder as a text item's hit test with set_hits_text_alias.
void set_hits_trace(bool on);
void set_hits_text_alias(std::uintptr_t test_function);
// The prompts follow the device (pad_prompts): the co-op screens consume some keys before the game's poll sees them (Browse F5 /
// Page Up / Page Down, Ctrl+C / Ctrl+V); `note` tells the device tracker the keyboard was used (pad_prompts::note_keyboard,
// wired by exports.cpp; null: nothing).
void set_keyboard_note(void (*note)());
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
    void** visual_context=nullptr; // synthetic active graphics context slot; production resolves the engine export
};
void install(HMODULE game,const install_hooks* hooks=nullptr);
// Shared with the host fixture for actual rollback/readback fault injection.
patch_transaction::contribution contribution();
}
