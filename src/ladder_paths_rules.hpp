#pragma once
#include <array>
#include <cstdint>
namespace ladder_paths_rules
{
	using address = std::uint32_t;
	inline bool known_path(address assigned, const std::array<address, 2>& candidates)
	{ return assigned && (assigned == candidates[0] || assigned == candidates[1]); }

	struct identity
	{
		address actor = 0;
		std::uint32_t entity_id = 0;
		address path = 0;
		std::uint32_t start_bits = 0;
		bool operator==(const identity&) const = default;
	};
	enum class phase { unseen, driving, completed };
	struct tracking
	{
		struct entry { identity key{}; phase state = phase::unseen; };
		std::array<entry, 256> entries{};
		phase state(identity key) const
		{
			const auto& e = entries[key.entity_id & 255];
			return e.key == key ? e.state : phase::unseen;
		}
		void set(identity key, phase value) { entries[key.entity_id & 255] = {key, value}; }
	};

	template <typename Engine>
	bool update(Engine& engine, address actor, float delay, bool force, bool timer_callback = false)
	{
		const bool result = engine.original_update(actor, delay, force);
		if (engine.is_ladder_path(actor))
		{
			const auto key = engine.key(actor);
			if (engine.path_active(actor))
			{
				engine.tracker.set(key, phase::driving);
				engine.schedule_native(actor, delay, force);
			}
			else if (timer_callback)
			{
				// The final evaluator callback must finish before disarming.
				engine.tracker.set(key, phase::completed);
			}
		}
		return result;
	}
	template <typename Engine>
	bool goal_complete(Engine& engine, address actor, bool path_evaluator)
	{
		if (path_evaluator && engine.is_ladder_path(actor))
		{
			const auto key = engine.key(actor);
			if (engine.path_active(actor) || engine.tracker.state(key) != phase::completed)
			{
				// Also handles a restored native timer in a fresh process. The
				// expired final callback uses the native velocity fallback instead
				// of snapping to a character movement goal that was never set.
				engine.tracker.set(key, phase::driving);
				return false;
			}
		}
		return engine.original_goal_complete(actor);
	}
}
