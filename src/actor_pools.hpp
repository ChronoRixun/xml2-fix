#pragma once
#include <Windows.h>
#include <cstdint>
#include <string>
#include <vector>
namespace patch_transaction {class transaction;}
namespace actor_pools
{
    void install(HMODULE game);
    bool contribute(patch_transaction::transaction&,HMODULE game);
    // The bytes the standalone install (stage 1: [Limits] ActorPools=124 without Actors=96) committed at the W11
    // controller-reset site 0x522a18 (a JMP into its cave plus pad), so a later module guarding that span can compose
    // them (join_in_progress). Empty when nothing was written there: not installed, or the E4 core path, which does
    // not write W11.
    const std::vector<std::uint8_t>& installed_controller_reset();
    // The counters are updated synchronously at retail take sites; sampling logs
    // their changes and supplies the test pipe without running any game code.
    void sample();
    std::string telemetry_json();
}
