#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

// X-Men Legends 1's solo mode (docs/solo-mode.md): the decisions, without the game.
//
// The original keeps one bit per character (default.xbe: character+0x334 bit 1, getter 0x2e030). The party toggle
// (0x33a70) sets or clears it on every member of the party list at once, so solo is party-wide; a character built
// later starts clear (constructor 0x33cd0). XMen2.exe has no such bit (its +0x3d8 byte reuses bit 1), so the fix
// keeps the members' generation handles instead: a handle stays solo until the party leaves solo mode, and an
// entity made after the toggle (a zone load, a new body) is not solo - as in the original.
namespace solo_mode_rules
{
	// default.xbe world update 0x73420: the first automatic regroup check 1.5 s after solo began (constant 0x3ca844),
	// then one check a second (0x3c6e40), on the game clock.
	constexpr float first_check_seconds = 1.5f;
	constexpr float retry_seconds = 1.0f;
	// default.xbe 0x34bb0: the navigator query of the regroup predicate (radius 80, 200 steps; 2 = reached) and the
	// manual exit's on-screen test (150).
	constexpr float regroup_radius = 80.0f;
	constexpr int regroup_steps = 200;
	constexpr int regroup_reached = 2;
	constexpr float regroup_view_margin = 150.0f;
	// default.xbe 0x31860 / 0x318b0: the character's own voice line ids (XMen2.exe's table names them
	// snd_solo_begin / snd_solo_end, 0x6d56b0 / 0x6d56b8).
	constexpr int voice_begin = 0x13;
	constexpr int voice_end = 0x14;

	constexpr std::size_t max_members = 8;

	struct member
	{
		std::uint32_t handle = 0; // generation handle (entity+0x1c)
		bool character = false;   // character class
		bool seated = false;      // controller assignment != -1 (the party list's test, 0x72470 / PC 0x46d460)
		bool human = false;       // not AI-controlled (+0x3d8 bit 0)
	};

	enum class toggle_result
	{
		entered,        // the party is solo now
		left,           // the party left solo mode
		unchanged,      // nothing to do (already in that state)
		refused_players,// enter with more than one player (0x33a70: the session's player count > 1)
		too_few,        // enter with a party of one (0x33a70: the list must hold more than one member)
	};

	class party
	{
		std::array<std::uint32_t, max_members> handles_{};
		std::size_t count_ = 0;
		float next_check_ = 0; // 0: not scheduled (default.xbe game+0x3ac)

	public:
		bool contains(const std::uint32_t handle) const
		{
			if (!handle)
			{
				return false;
			}
			for (std::size_t i = 0; i < count_; ++i)
			{
				if (handles_[i] == handle)
				{
					return true;
				}
			}
			return false;
		}
		bool active() const { return count_ != 0; }
		std::size_t size() const { return count_; }
		void clear()
		{
			handles_.fill(0);
			count_ = 0;
			next_check_ = 0;
		}

		// 0x33a70(on): the party list is every seated character; entering needs one player and more than one
		// member, leaving clears whoever is solo. `members` is the party as the game has it at the call.
		template <std::size_t N>
		toggle_result toggle(const bool on, const std::array<member, N>& members, const std::size_t n)
		{
			std::size_t listed = 0, humans = 0;
			for (std::size_t i = 0; i < n && i < N; ++i)
			{
				if (members[i].handle && members[i].character && members[i].seated)
				{
					++listed;
					humans += members[i].human ? 1 : 0;
				}
			}
			if (!on)
			{
				if (!active())
				{
					return toggle_result::unchanged;
				}
				clear();
				return toggle_result::left;
			}
			if (humans > 1)
			{
				return toggle_result::refused_players;
			}
			if (listed < 2)
			{
				return toggle_result::too_few;
			}
			bool added = false;
			for (std::size_t i = 0; i < n && i < N && count_ < max_members; ++i)
			{
				const auto& m = members[i];
				if (m.handle && m.character && m.seated && !contains(m.handle))
				{
					handles_[count_++] = m.handle;
					added = true;
				}
			}
			return added ? toggle_result::entered : toggle_result::unchanged;
		}

		// 0x73420, once per world update: whether the regroup predicate runs now. `current_solo` is the solo state
		// of the hero the update follows; while it isn't solo the schedule resets.
		bool regroup_due(const bool current_solo, const float now)
		{
			if (!current_solo)
			{
				next_check_ = 0;
				return false;
			}
			if (next_check_ == 0)
			{
				next_check_ = now + first_check_seconds;
			}
			if (!(now > next_check_))
			{
				return false;
			}
			next_check_ = now + retry_seconds;
			return true;
		}
		float next_check() const { return next_check_; }
	};

	// The AI's solo tests (default.xbe 0xf373e/0xf374d, 0x104ea0/0x104eb0, 0x1053d1/0x1053e1): the follower or the
	// one it follows is solo.
	inline bool either(const party& p, const std::uint32_t self, const std::uint32_t leader)
	{
		return p.contains(self) || p.contains(leader);
	}
}
