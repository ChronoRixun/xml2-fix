#pragma once

#include <cmath>
#include <cstdint>

// XML1's AI visibility query (default.xbe 0xfe550). Kept independent of native addresses so its complete
// geometry, deferral and hit decisions can be exercised with synthetic entities.
namespace npc_visibility_rules
{
	struct point
	{
		float x, y, z;
	};
	struct query_result
	{
		bool served;
		bool hit_valid;
		bool hit_target;
	};
	constexpr float height = 60.0f, extent = 1.0f, query_distance = 600.0f;

	// Engine supplies actor/target positions and the existing collision query. The query can be deferred;
	// its failure is not a wall hit. Only that case changes the caller's deferred byte, as in XML1.
	template<class Engine>
	bool visible(Engine& engine, std::uint8_t& deferred)
	{
		if (!engine.has_target()) return false;
		auto from = engine.actor_position();
		auto to = engine.target_position();
		from.z += height;
		to.z += height;
		const double dx = static_cast<double>(from.x) - to.x;
		const double dy = static_cast<double>(from.y) - to.y;
		const double dz = static_cast<double>(from.z) - to.z;
		// XML1 also takes the no-query path for an unordered distance. A coincident target still queries.
		if (!(std::sqrt(dx * dx + dy * dy + dz * dz) < query_distance)) return true;
		const auto result = engine.query(from, to, point{extent, extent, extent});
		if (!result.served)
		{
			deferred = 1;
			return false;
		}
		return !result.hit_valid || result.hit_target;
	}
}
