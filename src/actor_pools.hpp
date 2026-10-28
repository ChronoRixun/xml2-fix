#pragma once
#include <Windows.h>
#include <string>
namespace patch_transaction {class transaction;}
namespace actor_pools
{
    void install(HMODULE game);
    bool contribute(patch_transaction::transaction&,HMODULE game);
    // The counters are updated synchronously at retail take sites; sampling logs
    // their changes and supplies the test pipe without running any game code.
    void sample();
    std::string telemetry_json();
}
