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
#include <cstdio>
#include <cstring>

namespace receive_validation_rules
{
	constexpr std::size_t max_body = 0x505;
	constexpr std::uint16_t max_body_u16 = 0x505;
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
	// corrections to the net030-harden values. The movement lengths come from the stock sender (0x559700)
	// and decoder (0x559eb0): full record 33, idle 2, combined delta 8, five full records + sequence 167,
	// angle pair + tick 6, five-record combined delta 142. These are bodies, without transport framing.
	// Synchronization 0x2e reads ten bytes, but its sender at 0x614c39 sends a padded twelve-byte struct.
	// Menu reply 0x2b similarly reads nine bytes and sends twelve (0x61158b).
	// Movie completion 0x59 is registered dynamically at 0x5c9e31, not in the static session table.
	// Its handler (0x5c9d30) reads only event context, never a payload; a captured reliable wrapper is 59 06 00.
	// Two more groups are registered dynamically (registration call 0x610ef0, dispatch list lookup 0x612be0), found by
	// scanning every registration site of the retail executable after the 0x59 miss:
	//  * 0x20..0x25, the NAT mesh phase (sites 0x60569a, 0x605999..0x605a20): the host's 0x23 topology update (a
	//    4-byte player-bitmask pair read at body+0) and its empty 0x25/0x24 completion messages, 0x21/0x22 (dword,
	//    dword, byte at body+0/4/8) and 0x20 (a handler that returns at once). Every drop-in or third-player join
	//    runs them; dropping 0x23/0x25 would stall the mesh.
	//  * 0x53..0x58, the shared in-game state messages of handler 0x5b5720 (registered at 0x5b6b0a..0x5b6bb1):
	//    0x53/0x54 read word@0 + dword@4, 0x55 dword@0 + bytes@4/@8 + dword@0xc, 0x57 byte@0, 0x58 dword@0, 0x56 nothing.
	// Their minimums are the bytes each handler reads; their maximums are the 0x505 body cap, since no handler walks
	// a length-driven loop. Handlers that read nothing take an empty body.
	inline constexpr rule rules[] = {
		{0x00,2,1285},{0x01,33,33},{0x02,2,2},{0x03,6,6},{0x04,4,4},{0x05,8,8},{0x06,167,167},{0x07,136,136},{0x08,138,138},
		{0x09,4,4},{0x0a,2,2},{0x0f,2,2},{0x10,0,1285},{0x11,4,4},{0x14,5,8},{0x16,0,8},{0x17,0,0},{0x18,68,game_info_max},
		{0x19,0,1285},{0x1a,48,224},{0x1b,6,224},{0x1c,0,224},{0x1d,0,224},{0x1e,6,8},{0x1f,77,224},{0x26,6,8},{0x28,0,0},
		{0x29,4,4},{0x2b,9,12},{0x2c,5,8},{0x2e,10,12},{0x38,0,0},{0x39,0,0},{0x3a,0,0},{0x3e,4,4},{0x3f,0,0},
		{0x40,1,1},{0x41,36,36},{0x42,0,0},{0x43,1,1},{0x44,238,1285},{0x45,40,1285},{0x46,4,4},{0x47,5,8},{0x48,200,200},
		{0x49,0,0},{0x4a,0,2},{0x4b,0,0},{0x4c,0,0},{0x4d,0,0},{0x4e,0,0},{0x4f,0,0},{0x50,0,0},{0x51,0,0},{0x5a,1,1},
		{0x5b,0,0},{0x5c,4,4},{0x5d,40,40},{0x5e,0,0},{0x5f,0,0},{0x60,0,1285},{0x61,0,0},{0x62,1,1},{0x63,0,0},
		{0x64,0,0},{0x65,0,0},{0x66,4,4},{0x67,8,8},{0x68,6,6},{0x69,140,140},{0x6a,142,142},{0x6b,138,138},{0x6c,142,142},
		{0x6d,140,140},{0x6e,0,224},{0x6f,25,25},{0x70,159,159},{0x71,0,0},{0x72,1,64},
		{0x59,0,0},
		{0x20,0,max_body_u16},{0x21,9,max_body_u16},{0x22,9,max_body_u16},{0x23,4,max_body_u16},{0x24,0,max_body_u16},{0x25,0,max_body_u16},
		{0x53,8,max_body_u16},{0x54,8,max_body_u16},{0x55,16,max_body_u16},{0x56,0,max_body_u16},{0x57,1,max_body_u16},{0x58,4,max_body_u16},
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
			// Reliable wrapper [opcode u8][seq u16][payload]. The stock parser excludes the opcode from
			// event+8 although event+4 still points at it. 0x6100ee subtracts two sequence bytes and
			// 0x610136 copies from body+3. Validate that same inner body, never its framed length.
			// The wrapper storage therefore contains length+1 bytes; an empty inner body has length 2.
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

	// One log line for a refused (or, in the development audit, would-be refused) message. A reliable wrapper (0x00)
	// is reported by the INNER opcode it carries, with the inner body length (wrapper length - 2) and the first sixteen
	// inner bytes, so a false drop can be traced to the handler's table row: e.g.
	//   "dropped opcode 0x6f (wrapped, inner 27 bytes, wrapper 29): body too long (max 25); first 16 bytes: ..."
	// A plain message reads "dropped opcode 0x01 (32 bytes): body too short (min 33); first 16 bytes: ...".
	// The bound shown is the table row the verdict came from (the 0x505 stock cap when only the cap was exceeded).
	// `body` must be readable for `length` bytes (the caller already guards the read); returns the characters written.
	inline int format_drop(char* out, std::size_t capacity, const char* verb, std::uint8_t opcode, const std::uint8_t* body,
	                       std::size_t length, drop_reason reason, const context& ctx = {})
	{
		std::uint8_t shown_opcode = opcode;
		const std::uint8_t* shown = body;
		std::size_t shown_length = length;
		bool wrapped = false;
		// Report the inner message when the wrapper itself was well formed and the verdict is the inner one.
		if (opcode == 0x00 && body && length >= 2 && length <= max_body && find_rule(0x00) && length >= find_rule(0x00)->min_len)
		{
			const auto inner_reason = check(body[0], body + 3, length - 2, ctx);
			if (inner_reason == reason)
			{
				wrapped = true;
				shown_opcode = body[0];
				shown = body + 3;
				shown_length = length - 2;
			}
		}
		char bound[40]{};
		if (reason == drop_reason::body_too_long || reason == drop_reason::body_too_short)
		{
			const auto* row = find_rule(shown_opcode);
			if (reason == drop_reason::body_too_long)
				std::snprintf(bound, sizeof(bound), " (max %u)", unsigned(row && shown_length <= max_body ? row->max_len : max_body));
			else if (row)
				std::snprintf(bound, sizeof(bound), " (min %u)", unsigned(row->min_len));
		}
		char hex[16 * 3 + 1]{};
		std::size_t used = 0;
		if (shown && length)
			for (std::size_t i = 0; i < shown_length && i < 16 && used + 3 < sizeof(hex); ++i)
				used += std::snprintf(hex + used, sizeof(hex) - used, i ? " %02x" : "%02x", unsigned(shown[i]));
		char size_text[64];
		if (wrapped) std::snprintf(size_text, sizeof(size_text), "wrapped, inner %u bytes, wrapper %u", unsigned(shown_length), unsigned(length));
		else std::snprintf(size_text, sizeof(size_text), "%u bytes", unsigned(length));
		return std::snprintf(out, capacity, "online: receive validation %s opcode 0x%02x (%s): %s%s%s%s", verb, unsigned(shown_opcode),
		                     size_text, drop_reason_text(reason), bound, used ? "; first 16 bytes: " : "", hex);
	}

	inline bool valid(std::uint8_t opcode, const std::uint8_t* body, std::size_t length, const context& ctx = {})
	{
		return check(opcode, body, length, ctx) == drop_reason::none;
	}
}
