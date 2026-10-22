#pragma once

#include "gns_transport_core.hpp"
#include "receive_validation_rules.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace network_validation
{
	// One receive table for both paths. The stock-path hook at the common dispatch 0x60dbe0 (receive_validation) and
	// this GNS pre-dispatch boundary check the same bounds, so a message cannot pass one lane and fail the other:
	// the table, the reliable-wrapper convention (event length = payload + 2, the opcode byte excluded), the
	// body-aware index/string checks and the FastTransfer record shape all live in receive_validation_rules.hpp.
	// The first version of the GNS table (net030-harden, 0.3.0) predates the 0.2.3h captured-byte corrections and
	// the dynamically registered opcodes (0x20..0x25, 0x53..0x59); it was merged into this one.
	using rule = receive_validation_rules::rule;
	inline const rule* find_rule(std::uint8_t opcode) { return receive_validation_rules::find_rule(opcode); }

	struct context
	{
		bool fast_transfer = false;
		std::uint32_t admission_slots = 4;
		std::uint32_t peer_slots = 6;
	};

	inline std::uint32_t u32(const std::uint8_t* data) { return receive_validation_rules::u32(data); }

	inline bool valid_message(std::uint8_t opcode, const std::uint8_t* body, std::size_t length, const context& ctx = {})
	{
		if (length > gns_transport_core::max_game_body || (length && !body)) return false;
		receive_validation_rules::context shared;
		shared.fast_transfer = ctx.fast_transfer;
		shared.slots = ctx.admission_slots;
		shared.peers = ctx.peer_slots;
		return receive_validation_rules::valid(opcode, body, length, shared);
	}

	// Tunnel datagrams retain stock framing. Only the two admission messages need this pre-parser check.
	inline bool valid_tunneled_datagram(const std::uint8_t* data, std::size_t size, std::uint32_t admission_slots = 4)
	{
		if (!data || size < 3) return true;
		std::size_t offset = 3;
		auto opcode = data[2];
		if (!opcode)
		{
			if (size < 6) return true;
			opcode = data[3];
			offset = 6;
		}
		if (opcode != 0x1b && opcode != 0x1f) return true;
		const std::size_t minimum = opcode == 0x1b ? 6 : 77;
		if (size - offset < minimum) return false;
		return u32(data + offset + (opcode == 0x1b ? 0 : 0x30)) < admission_slots;
	}
}
