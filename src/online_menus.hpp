#pragma once
#include <Windows.h>
#include "patch_transaction.hpp"
#include "online_browser_core.hpp"

// Guarded UI intents and snapshot bindings. Room operations use online_room;
// native admission and participant allocation retain their native owners.
namespace online_menus {
// Offline host fixture seam. Production obtains code/title/visibility from the
// room client; these extra member values never confer native admission.
struct lobby_fixture {
    char code[10]{};
    struct member { unsigned native_index=0; int ping_ms=-1; bool same_build=false; };
    member members[4]{};
    unsigned count=0;
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
};
void keyboard(unsigned char* state);
bool requested();
bool installed();
// Optional host seams; production uses nullptr. Callback pointers are copied,
// not retained through this options object. No game/menu pointers cross threads.
struct install_hooks {
    DWORD (*clock)()=nullptr;
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
