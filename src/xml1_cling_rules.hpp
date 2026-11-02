#pragma once

// [Game] Xml1WallCling=xml1: X-Men Legends 1's wall cling for the acrobat moveset (Beast, Nightcrawler, Psylocke):
// the two FightMove handlers `ch_clingwalldecide` and `ch_clingwallbackflip`, which XMen2.exe never registers
// (its handler table 0x4fd8d0.. has neither name, so both moves run as %default% through 0x4fd860). XML2's own
// moveset_acrobat ships the same two moves, so the data needs no change; only the handlers are missing.
// Kept apart from the patch so the tests can check every rule and retail byte without the game.
//
// What X-Men Legends 1 does (default.xbe; handler objects are one word holding an 8-slot vtable, as here):
//   ch_clingwalldecide (vtable 0x3d7e84). Entry 0xda3a0 only sets bit 0 of its third argument's +0x64 (not
//   reproduced: the PC object behind that argument is not identified). Update 0xd9740 (actor, input), per frame:
//     1. When the talent acrobatics is at rank 2 or more and input bit 6 is held (0xe6f40 tests input+0), the
//        actor's movement trace 0x3c720(position, facing x 24, 0, &shift, &contact) runs; the contact must be the
//        level (0xa07b0) or a tileent (0x2f590, class 0x4d7378 "tileent"), its normal flat (|n.z| < 0.2) and
//        facing it (facing . n < -0.5, both strict: fcomp + test ah,5 + jp). Then the actor turns to yaw =
//        atan2(-n.y, -n.x) (0x120720, angles {0, 0, yaw}, entity vt+0x2c), moves by the trace's shift (vt+0x1c,
//        position + shift), input+0x24 |= 4 and the next move (actor+0x324) is its `special` chain (action 24).
//     2. Else, input bit 6 held: the `move` chain (action 4) when it resolves (and its name is not the empty
//        one), input+0x24 |= 4.
//     3. Else, grounded (actor+0xc0, the ground entity, or actor+4 bit 2): input+0x24 |= 4, `special2` (25).
//     4. Else nothing (false).
//   ch_clingwallbackflip (vtable 0x3d7ecc). Entry 0xd9590: velocity = facing x -60 in x and y, 500 up (0x760d0),
//   detach from the ground (0x27260 with the null handle), clear actor+4 bit 2 and publish the flags (0x66a90). Its
//   update 0xd96e0: once grounded, start the move named `idle` (0x38210 + 0x3b100); else nothing.
//   Neither entry calls the default entry (0xe6d80, the move's start time), so neither does this one.
//
// The same services in XMen2.exe (each matched by code shape, see docs/xml1-cling.md):
//   movement trace 0x42b6c0 (= 0x3c720; world_backfaces_rules.hpp says the same), contact holder 0x4a9570 /
//   0x4a95c0 (is the level) / 0x4a9590 (entity) / 0x4a9600 (normal = record+0x10), tileent cast 0x41e4d0 (class
//   0x778a4c, registered as "tileent" at 0x67ae25), angles -> facing 0x560990, input tests 0x4fc020 (+0) / 0x4fc000
//   (+4), action resolver 0x427c80, move vt+0xc (may it start: what XML2's ch_move_jump asks at 0x4efd61), named
//   move 0x41b120 + 0x427ca0, start move 0x42c340 (= 0x3b100), set velocity 0x473640 (= 0x760d0), ground handle
//   0x419f20 (= 0x27260) with the null handle 0x72988c, flags publish 0x4602c0 (= the store + 0x66a90), next move
//   actor+0x3bc (= +0x324), ground entity actor+0xbc (= +0xc0), talents CStats (actor+0x35c) +0x20 / +0xbc.
// XML2's own ch_move_jump update (0x4efce0) is XML1's 0xde4a0 byte for byte in shape with exactly these offsets, so
// the decide's fallbacks (steps 2-3) are written the way it writes them (move vt+0xc before taking the chain).

#include "limits_rules.hpp" // guard, matches, image_base

#include <array>
#include <cmath>
#include <cstdint>
#include <string_view>

namespace xml1_cling_rules
{
	using limits_rules::guard;
	using address = std::uint32_t;

	constexpr std::uint16_t agreement_revision = 1;

	// The ini value: xml1 (or 1 / on / true) asks for the handlers; nothing, empty, xml2, 0 or off is the game's own.
	enum class setting { native, xml1, invalid };
	inline setting parse(std::string_view v)
	{
		auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? char(c + 32) : c; };
		auto is = [&](std::string_view w) {
			if (v.size() != w.size()) return false;
			for (std::size_t i = 0; i < v.size(); ++i) if (lower(v[i]) != w[i]) return false;
			return true;
		};
		while (!v.empty() && (v.front() == ' ' || v.front() == '\t')) v.remove_prefix(1);
		while (!v.empty() && (v.back() == ' ' || v.back() == '\t')) v.remove_suffix(1);
		if (v.empty() || is("xml2") || is("0") || is("off") || is("false")) return setting::native;
		if (is("xml1") || is("1") || is("on") || is("true")) return setting::xml1;
		return setting::invalid;
	}

	// ---- The original's numbers ------------------------------------------------------------------------------------
	constexpr float probe_length = 24.0f;     // 0xd97bf: facing x [0x3c9784]
	constexpr float flat_limit = 0.2f;        // 0xd983c: |n.z| < [0x3c97d0]
	constexpr float facing_limit = -0.5f;     // 0xd986b: facing . n < [0x3c8c30]
	constexpr float launch_back = -60.0f;     // 0xd95c3: facing x [0x3ca9fc]
	constexpr float launch_up = 500.0f;       // 0xd95d8: 0x43fa0000
	constexpr int min_acrobatics = 2;         // 0xd9782: rank below 2 skips the wall test
	constexpr unsigned jump_bit = 6;          // 0xd9793: input bit 6 (XML2's ch_move_jump: bit 6 -> action 4)
	constexpr int action_move = 4, action_special = 24, action_special2 = 25; // same ids in both games
	constexpr std::uint32_t consumed = 4;     // input+0x24 |= 4
	constexpr std::uint32_t grounded_bit = 1u << 2; // actor+4 bit 2
	constexpr const char* talent_name = "acrobatics";

	struct vec { float x = 0, y = 0, z = 0; };
	inline float dot(const vec& a, const vec& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

	// The wall test of step 1 after a contact: the level or a tileent, a flat normal, facing it.
	inline bool wall(const vec& normal, const vec& facing)
	{
		return std::fabs(normal.z) < flat_limit && dot(facing, normal) < facing_limit;
	}
	// 0x120720 on {-n.x, -n.y, 0}: the yaw that faces the wall, in [0, 2 pi).
	inline float yaw_into(const vec& normal)
	{
		constexpr float two_pi = 6.28318530718f;
		float yaw = std::atan2(-normal.y, -normal.x);
		if (yaw < 0) yaw += two_pi;
		if (!(yaw < two_pi)) yaw = 0;
		return yaw;
	}
	inline vec backflip_velocity(const vec& facing) { return {facing.x * launch_back, facing.y * launch_back, launch_up}; }
	inline vec probe_delta(const vec& facing) { return {facing.x * probe_length, facing.y * probe_length, facing.z * probe_length}; }

	// The decide's choice for one update, from what the update has read.
	enum class choice { none, cling, move, land };
	struct reading
	{
		int acrobatics = 0;
		bool jump_held = false;
		bool contact = false;   // the trace met something
		bool wall_kind = false; // the level or a tileent
		vec normal{}, facing{};
		bool move_ready = false; // the `move` chain resolves and may start
		bool grounded = false;
	};
	inline choice decide(const reading& r)
	{
		if (r.acrobatics >= min_acrobatics && r.jump_held && r.contact && r.wall_kind && wall(r.normal, r.facing))
			return choice::cling;
		if (r.jump_held && r.move_ready) return choice::move;
		if (r.grounded) return choice::land;
		return choice::none;
	}

	// ---- XMen2.exe -------------------------------------------------------------------------------------------------
	constexpr address lookup_va = 0x4fd860;       // FightMove handler lookup (cdecl name -> handler object)
	constexpr address lookup_resume_va = 0x4fd865; // after the five displaced bytes
	constexpr address default_vtable_va = 0x692f4c;
	constexpr address trace_va = 0x42b6c0, is_level_va = 0x4a95c0, contact_entity_va = 0x4a9590, contact_normal_va = 0x4a9600;
	constexpr address tileent_cast_va = 0x41e4d0, facing_va = 0x560990, held_va = 0x4fc020;
	constexpr address action_va = 0x427c80, intern_va = 0x41b120, named_va = 0x427ca0, start_va = 0x42c340;
	constexpr address velocity_va = 0x473640, ground_va = 0x419f20, flags_va = 0x4602c0, null_handle_va = 0x72988c;
	constexpr address registry_va = 0x4c05b0, attribute_va = 0x5649c0;
	constexpr std::uint32_t actor_ground = 0xbc, actor_flags = 0x4, actor_next_move = 0x3bc, actor_stats = 0x35c;
	constexpr std::uint32_t actor_position = 0x20, actor_angles = 0x2c, input_flags = 0x24;
	constexpr std::uint32_t stats_talents = 0x20, stats_talent_count = 0xbc;
	constexpr int stats_talent_capacity = 19;
	constexpr std::uint32_t entity_set_position = 0x1c, entity_set_angles = 0x2c, move_may_start = 0xc;

	// The five bytes the hook replaces (push esi; mov esi, [esp+8]) and what follows them.
	inline constexpr std::array<guard, 22> guards{{
		{lookup_va, "568b74240885f67425803e007420", "FightMove handler lookup entry (push esi; mov esi,[esp+8])"},
		{0x4f6bf6, "e8", "the style parser's lookup call (npc_semi_auto may own its operand)"},
		{default_vtable_va, "10d74f00d0bd4f0010eb590010eb5900b0cb480000c54f0050ce4f0060c84f00", "default handler's eight callbacks"},
		{trace_va, "6aff68882e670064a100000000506489", "actor movement trace (default.xbe 0x3c720)"},
		{0x42b803, "c21400", "movement trace takes five arguments"},
		{is_level_va, "51568bf1833e00741c8d", "contact is the level (default.xbe 0xa07b0)"},
		{contact_entity_va, "568bf1833e007418e8", "contact entity (0xa0780)"},
		{contact_normal_va, "8b0185c0740483c010c3", "contact normal at record+0x10 (0xa07f0)"},
		{tileent_cast_va, "568b74240885f6742a8b06578bceff108b15508a7700", "tileent cast (class 0x778a4c, index 0x778a50)"},
		{0x67ae25, "68d4976800682c030000b94c8a7700", "class 0x778a4c registered as tileent"},
		{facing_va, "8b4c2408d90530006800", "angles -> facing direction"},
		{held_va, "8b4c240cb801000000d3e08b4c240885010f95c0c20c00", "handler input test of input+0 (0xe6f40)"},
		{action_va, "8b4424048b9178030000515052e83e54ffff8bc8e8579e0d00c20400", "actor action resolver"},
		{intern_va, "56578b7c240c85ff8bf17428803f007423", "move-name interner"},
		{named_va, "8b4424045150e82554ffff8bc8e89e9d0d00c20400", "named move resolver"},
		{start_va, "56578b7c240c85ff8bf17434", "start move (0x3b100)"},
		{velocity_va, "83ec18568bf18b4604c1e805", "set velocity (0x760d0)"},
		{ground_va, "5356578b7c24108bf18b86bc", "set ground entity handle at actor+0xbc (0x27260)"},
		{flags_va, "8b44240456578b388d51048bf2893e8b", "set entity flags and publish (0x66a90)"},
		// XML2's ch_move_jump update: grounded = actor+0xbc or actor+4 bit 2; next move at +0x3bc; input+0x24 |= 0xc.
		{0x4efcea, "8b86bc00000085c0578bf9750a8b4604c1e802a80174046a", "ch_move_jump's grounded test"},
		{0x4efd5c, "8b17568bcfff520c84c0741789bebc0300008b43245f83c80c894324", "ch_move_jump's chain contract"},
		{0x4eff38, "688c9872008bcee8dc9ff2ff", "ch_move_jump entry detaching with the null handle"},
	}};
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
			if (!limits_rules::matches(image + (g.va - limits_rules::image_base), g.hex)) return &g;
		return nullptr;
	}
	constexpr std::array<std::uint8_t, 5> retail_entry{0x56, 0x8b, 0x74, 0x24, 0x08};
}
