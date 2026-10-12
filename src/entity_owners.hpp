#pragma once

// The seam between the entity-capacity install and the ported owners (World, Grid, query manager, physics frames):
// entity_capacity.cpp prepares and binds through this interface only, so each owner is built, tested and merged on
// its own. entity_owners.cpp implements it with the real ports; a stub implementation reports "unavailable".

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace entity_capacity_layout { struct descriptor; }

namespace entity_owners
{
	struct set
	{
		void* world = nullptr;
		void* grid = nullptr;
		void* query_manager = nullptr;
	};
	// Allocates and constructs every owner for the descriptor; on any failure releases what was built and says why.
	bool create(const entity_capacity_layout::descriptor& d, set& out, std::string& why);
	void destroy(set& owners);
	// Every DLL symbol the ledger's relocation sites name for these owners (World/Grid/query getters are the
	// install's own; the query API bridges, physics frames and entity-set callers are the owners').
	void bind_symbols(const std::function<void(std::string_view name, std::uint32_t address)>& bind);
	// Runtime telemetry from the World (live/free/retired, max generation, refusals, reset-time attempts).
	struct world_counters
	{
		std::uint32_t live = 0, free_slots = 0, retired = 0, max_generation = 0, refusals = 0, reset_time_attempts = 0;
		bool available = false;
	};
	world_counters counters(const set& owners);
	bool retire_slot(const set& owners, std::uint32_t index); // test-pipe fixture (plan P3.4)
}
