#pragma once

#include <Windows.h>

#include <array>
#include <cstdint>
#include <string>
#include "pool_counter_rules.hpp"

// The model component pools (SPEC-runtime-limits 2.4): every model the game shows - heroes, enemies, props, effects,
// HUD heads - takes a handle and components from fixed pools in one static object (handles 692, transforms 692,
// attributes 260, shader effects 432, IGB geometry 400, animated actors 32, procedural geometry 220, particle clouds
// 40). When one is full the model is not created and nothing says so: effects and props go missing, and the beta 1
// crash was a team-combo popup whose second head the full IGB pool refused. Opt-in:
//
//   [Engine]
//   ModelPools = x4   ; handles and transforms 4096, attributes 1024, shader effects 2048, IGB 2048, actors 128,
//                     ; procedural geometry 1024, particle clouds 128 with as many particle systems (the second
//                     ; manager each cloud takes a system from, 0x7dad78, moves with them); x2: half (clouds 80);
//                     ; off: the game's own
//   ModelHandles = 8192 ; with x2 / x4: handles and transforms (they move together) from the profile's count up to
//                     ; 16384 - for a raised [Limits] IGBCache (about 3.5 model handles per IGB record)
//
// The whole object moves into a block of the DLL that the game's own constructor builds and its own destructor tears
// down; every field, capacity and bitmap size of the model code is patched (model_pools_rules.hpp,
// model_pools_sites.hpp), all or nothing: on any byte that isn't the retail build's the game keeps its own pools and
// the log says so in one line. Online, a raise is gameplay-agreement rule 28 (ModelPools): a full pool refuses spawns,
// so peers must share the capacities; absent/off registers nothing (model_pools_rules::agreement_for).
namespace model_pools
{
    // Installed addresses/capacities, including the relocated particle free count.
    std::array<pool_counter_rules::source,10> diagnostic_sources();
	// Reads [Engine] ModelPools and raises the pools. From DllMain, before XMen2.exe's own code runs.
	void install(HMODULE game);

	// For the test pipe's status: "model pools x4: models 312/4096, transforms 300/4096, ..." (live/capacity), or
	// "model pools off: models 312/692, ..." when the game's own pools are in use.
	std::string status();

	// Rule 28 (ModelPools) as installed: requested (the key asks for a raise or is invalid), verified, values.
	bool requested();
	bool verified();
	std::array<std::uint32_t, 4> agreement_values();

	// The live count of a pool ("models", "transforms", "attributes", "shader effects", "IGB models", "actors",
	// "procedural geometry", "particle clouds", "movie"), wherever the aggregate is; -1 when unreadable.
	long live(int kind_index);
	int capacity(int kind_index);
}
