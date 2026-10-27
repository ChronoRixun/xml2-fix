#pragma once
#include <Windows.h>
#include "patch_transaction.hpp"

// Presentation only. No broker, host, admission or participant allocation here.
namespace online_menus {
// Offline host fixture seam only; production has no provider until the room
// snapshot arrives in a later round. Values never confer native admission.
struct lobby_fixture {
    char code[10]{};
    struct member { unsigned native_index=0; int ping_ms=-1; bool same_build=false; };
    member members[4]{};
    unsigned count=0;
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
};
void install(HMODULE game,const install_hooks* hooks=nullptr);
// Shared with the host fixture for actual rollback/readback fault injection.
patch_transaction::contribution contribution();
}
