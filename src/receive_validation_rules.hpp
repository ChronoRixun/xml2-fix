#pragma once

// [Online] ReceiveValidation: the receive-side rule table checked before the stock event dispatch 0x60dbe0 runs a
// handler (net030-d3 validation-table.json: all 81 registrations in the retail exe, each handler's body reads
// disassembly-verified; backported from net030-harden's GNS-lane network_validation.hpp to the stock UDP path).
// No stock handler checks the body length it is handed: a short body reads past the parser's stack copy, an
// oversized 'H' overwrites the transfer buffer (0x608630), and the 0x1b/0x1f/0x29/0x46/0x5d index fields are used
// unchecked. The rules below are exactly what a stock sender (plus the gameplay-agreement record on 0x18/0x1a/
// 0x1b/0x1f and the FastTransfer record stream on 'H') can legitimately produce, so a valid peer is unaffected.
//
// Lengths are the dispatch event's body length (event+8). The largest stock datagram is 0x50a bytes, so no
// legitimate body exceeds 0x505; the parser accepts up to 0x514, and anything past 0x505 is refused here.

#include "entity_capacity_rules.hpp" // fast_chunk: the FastTransfer 'H' record shape

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace receive_validation_rules
{
	constexpr std::size_t max_body = 0x505;
	// Admission and in-game index fields are bounded by the session's four player slots and the transport's six
	// peer entries (net030-d3 section 3.3).
	constexpr std::uint32_t admission_slots = 4, peer_slots = 6;
	// 0x18 carries the native 0x74-byte game-info body plus the gameplay-agreement record (protocol 4, 108 bytes).
	constexpr std::uint16_t game_info_max = 0x74 + 108;

	struct rule
	{
		std::uint8_t opcode;
		std::uint16_t min_len, max_len;
	};

	// Every accepted registration; 0x12/0x13 (fragmentation, dead code in stock and a receive-side crash/overflow)
	// and every unknown opcode reject. The wrapper and the stock eight-byte 0x14/0x16/0x1e/0x26/0x2c/0x47 bodies are stock-path
	// corrections to the net030-harden values; the other entries use its lengths.
	inline constexpr rule rules[] = {
		{0x00,2,1285},{0x01,39,39},{0x02,4,4},{0x03,6,6},{0x04,4,4},{0x05,6,6},{0x06,39,39},{0x07,136,136},{0x08,138,138},
		{0x09,4,4},{0x0a,2,2},{0x0f,2,2},{0x10,0,1285},{0x11,4,4},{0x14,5,8},{0x16,0,8},{0x17,0,0},{0x18,68,game_info_max},
		{0x19,0,1285},{0x1a,48,224},{0x1b,6,224},{0x1c,0,224},{0x1d,0,224},{0x1e,6,8},{0x1f,77,224},{0x26,6,8},{0x28,0,0},
		{0x29,4,4},{0x2b,9,9},{0x2c,5,8},{0x2e,10,10},{0x38,0,0},{0x39,0,0},{0x3a,0,0},{0x3e,4,4},{0x3f,0,0},
		{0x40,1,1},{0x41,36,36},{0x42,0,0},{0x43,1,1},{0x44,238,1285},{0x45,40,1285},{0x46,4,4},{0x47,5,8},{0x48,200,200},
		{0x49,0,0},{0x4a,0,2},{0x4b,0,0},{0x4c,0,0},{0x4d,0,0},{0x4e,0,0},{0x4f,0,0},{0x50,0,0},{0x51,0,0},{0x5a,1,1},
		{0x5b,0,0},{0x5c,4,4},{0x5d,40,40},{0x5e,0,0},{0x5f,0,0},{0x60,0,1285},{0x61,0,0},{0x62,1,1},{0x63,0,0},
		{0x64,0,0},{0x65,0,0},{0x66,4,4},{0x67,8,8},{0x68,4,4},{0x69,140,140},{0x6a,140,140},{0x6b,138,138},{0x6c,142,142},
		{0x6d,140,140},{0x6e,0,224},{0x6f,25,25},{0x70,159,159},{0x71,0,0},{0x72,1,64},
	};

	inline const rule* find_rule(std::uint8_t opcode)
	{
		for (const auto& candidate : rules) if (candidate.opcode == opcode) return &candidate;
		return nullptr;
	}

	struct context
	{
		bool fast_transfer = false; // [Online] FastTransfer installed here: 'H' bodies are data/zero-run records
		std::uint32_t slots = admission_slots;
		std::uint32_t peers = peer_slots;
	};

	enum class drop_reason
	{
		none,
		unknown_opcode,        // no registration: the native dispatch would silently drop it too
		fragment_unsupported,  // 0x12/0x13: receive-side streams never open in stock (orphan deref / copy overflow)
		body_too_long,         // past the per-opcode maximum or the 0x505 stock body cap
		body_too_short,        // below what the handler reads, or a null body with a nonzero length
		bad_index,             // 0x1b/0x1f/0x29/0x46/0x5d index field out of range (unchecked native write)
		unterminated_string,   // 0x18/0x72: the native copy/compare runs to NUL with no bound
		malformed_record,      // FastTransfer 'H' record that is not a well-formed data/zero-run record
		roster_underflow,      // 0x44: the body[0xaa]-counted entry loop would read past the body
	};

	inline const char* drop_reason_text(drop_reason reason)
	{
		switch (reason)
		{
		case drop_reason::unknown_opcode: return "unknown opcode";
		case drop_reason::fragment_unsupported: return "fragmentation is never used on this path";
		case drop_reason::body_too_long: return "body too long";
		case drop_reason::body_too_short: return "body too short";
		case drop_reason::bad_index: return "index out of range";
		case drop_reason::unterminated_string: return "string field not terminated";
		case drop_reason::malformed_record: return "malformed campaign record";
		case drop_reason::roster_underflow: return "entry count overruns the body";
		default: return "invalid";
		}
	}

	inline std::uint32_t u32(const std::uint8_t* data)
	{
		std::uint32_t value;
		std::memcpy(&value, data, sizeof(value));
		return value;
	}

	// The one check, run on the dispatch event's opcode/body/length before any handler sees them. drop_reason::none
	// = the message is within every bound its handler trusts. Reads stay inside [body, body+length); a rule's
	// minimum guarantees the bytes its field checks need (0x1b u32@0, 0x1f u32@0x30, 0x44 byte@0xaa, ...).
	inline drop_reason check(std::uint8_t opcode, const std::uint8_t* body, std::size_t length, const context& ctx = {})
	{
		if (length > max_body) return drop_reason::body_too_long;
		if (opcode == 0x12 || opcode == 0x13) return drop_reason::fragment_unsupported;
		if (length && !body) return drop_reason::body_too_short;
		if (opcode == 0x48 && ctx.fast_transfer)
		{
			std::uint32_t covered = 0;
			const auto verdict = entity_capacity_rules::fast_chunk(0, body, static_cast<std::uint32_t>(length), 0xffffffffu, true, covered);
			return verdict == entity_capacity_rules::fast_verdict::accept || verdict == entity_capacity_rules::fast_verdict::complete
				? drop_reason::none : drop_reason::malformed_record;
		}
		const auto* accepted = find_rule(opcode);
		if (!accepted) return drop_reason::unknown_opcode;
		if (length < accepted->min_len) return drop_reason::body_too_short;
		if (length > accepted->max_len) return drop_reason::body_too_long;
		if (opcode == 0x00)
		{
			// Reliable wrapper [opcode u8][seq u16][payload]: event+8 is payload+2 (0x6100b0), so the inner
			// message starts at body+3 and its declared length is length-2. Empty inner bodies have outer length 2.
			if (!body[0]) return drop_reason::unknown_opcode;
			return check(body[0], body + 3, length - 2, ctx);
		}
		switch (opcode)
		{
		case 0x18: return std::memchr(body + 0x3e, 0, length - 0x3e) ? drop_reason::none : drop_reason::unterminated_string;
		case 0x1b: return u32(body) < ctx.slots ? drop_reason::none : drop_reason::bad_index;
		case 0x1f: return u32(body + 0x30) < ctx.slots ? drop_reason::none : drop_reason::bad_index;
		case 0x29:
		case 0x46: return u32(body) < ctx.peers ? drop_reason::none : drop_reason::bad_index;
		case 0x44:
		{
			const std::size_t required = 0x2a + 4 * std::size_t(body[0xaa]) + 3;
			return length >= required ? drop_reason::none : drop_reason::roster_underflow;
		}
		case 0x5d:
		{
			const auto index = static_cast<std::int8_t>(body[0]);
			return index >= 0 && static_cast<std::uint32_t>(index) < ctx.peers ? drop_reason::none : drop_reason::bad_index;
		}
		case 0x72: return std::memchr(body, 0, length) ? drop_reason::none : drop_reason::unterminated_string;
		default: return drop_reason::none;
		}
	}

	inline bool valid(std::uint8_t opcode, const std::uint8_t* body, std::size_t length, const context& ctx = {})
	{
		return check(opcode, body, length, ctx) == drop_reason::none;
	}
}
