// The owners seam with the real ports: World, Grid, query manager and the physics/navigation/named-query frames.
#include "entity_owners.hpp"
#include "entity_capacity_layout.hpp"
#include "entity_frames.hpp"
#include "entity_grid.hpp"
#include "entity_query.hpp"
#include "entity_world.hpp"
#include "log.hpp"

namespace entity_owners
{
	namespace
	{
		std::uint32_t address_of(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }
		entity_capacity_layout::descriptor frames_descriptor;
	}

	bool create(const entity_capacity_layout::descriptor& d, set& out, std::string& why)
	{
		out = {};
		// Build order: World first, then the grid, then the query manager with the World's address in its descriptor
		// (no owner may call a native getter here: DllMain runs before the executable's CRT, and the retail World/grid
		// getters register atexit handlers on construction).
		entity_capacity_layout::descriptor staged = d;
		out.world = entity_world::create(staged);
		if (!out.world) { why = "World owner could not be built"; destroy(out); return false; }
		staged.world_object = address_of(out.world);
		out.grid = entity_grid::create(staged);
		if (!out.grid) { why = "entity grid owner could not be built"; destroy(out); return false; }
		staged.grid_object = address_of(out.grid);
		out.query_manager = entity_query::create_manager(staged);
		if (!out.query_manager) { why = "query manager could not be built"; destroy(out); return false; }
		// The frames read the installed descriptor; publish the owners' addresses into a copy for them.
		frames_descriptor = d;
		frames_descriptor.world_object = address_of(out.world);
		frames_descriptor.grid_object = address_of(out.grid);
		frames_descriptor.query_manager = address_of(out.query_manager);
		entity_frames::set_descriptor(&frames_descriptor);
		return true;
	}

	void destroy(set& owners)
	{
		entity_frames::set_descriptor(nullptr);
		if (owners.query_manager) entity_query::destroy_manager(owners.query_manager);
		if (owners.grid) entity_grid::destroy(owners.grid);
		if (owners.world) entity_world::destroy(owners.world);
		owners = {};
	}

	void bind_symbols(const std::function<void(std::string_view, std::uint32_t)>& bind)
	{
#define BIND_QUERY(name) bind("entity_capacity::" #name, address_of(reinterpret_cast<const void*>(&entity_query::name)))
		BIND_QUERY(QueryApi_004a9580); BIND_QUERY(QueryApi_004a9590); BIND_QUERY(QueryApi_004a95c0); BIND_QUERY(QueryApi_004a95f0);
		BIND_QUERY(QueryApi_004a9600); BIND_QUERY(QueryApi_004a9610); BIND_QUERY(QueryApi_004a9620); BIND_QUERY(QueryApi_004a9630);
		BIND_QUERY(QueryApi_004a9650); BIND_QUERY(QueryApi_004a9730); BIND_QUERY(QueryApi_004a97a0); BIND_QUERY(QueryApi_004a98b0);
		BIND_QUERY(QueryApi_004a9a80); BIND_QUERY(QueryApi_004ab5b0); BIND_QUERY(QueryApi_004ab610); BIND_QUERY(QueryApi_004ab670);
		BIND_QUERY(QueryApi_004ab6d0); BIND_QUERY(QueryApi_004ab720); BIND_QUERY(QueryApi_004ab740); BIND_QUERY(QueryApi_004ab770);
		BIND_QUERY(QueryApi_004ab7d0); BIND_QUERY(QueryApi_004ab7f0); BIND_QUERY(QueryApi_004ab890); BIND_QUERY(QueryApi_004ab960);
		BIND_QUERY(QueryApi_004aba30);
#undef BIND_QUERY
#define BIND_FRAME(name) bind("entity_capacity::" #name, address_of(reinterpret_cast<const void*>(&::entity_capacity::name))) // the frames declare them in namespace entity_capacity, as the ledger names them
		BIND_FRAME(Physics_0044cd70); BIND_FRAME(Physics_0044ce70); BIND_FRAME(Physics_0044d170); BIND_FRAME(Physics_0044d360);
		BIND_FRAME(Physics_0044dca0); BIND_FRAME(Physics_0044e140); BIND_FRAME(Physics_0044e210); BIND_FRAME(Physics_0044e440);
		BIND_FRAME(Physics_0044e4d0); BIND_FRAME(Physics_0044e590); BIND_FRAME(Physics_0044e6a0); BIND_FRAME(Physics_0044e7d0);
		BIND_FRAME(Physics_0044ea40);
		BIND_FRAME(EntitySetCaller_00491210); BIND_FRAME(EntitySetCaller_00493330); BIND_FRAME(EntitySetCaller_004cd350);
#undef BIND_FRAME
	}

	world_counters counters(const set& owners)
	{
		world_counters out;
		if (!owners.world) return out;
		const auto c = entity_world::counters(owners.world);
		out.live = c.live; out.free_slots = c.free_slots; out.retired = c.retired; out.max_generation = c.max_generation;
		out.refusals = c.refusals; out.reset_time_attempts = c.reset_time_attempts; out.available = true;
		return out;
	}

	bool retire_slot(const set& owners, std::uint32_t index) { return owners.world && entity_world::retire_slot(owners.world, index); }
}
