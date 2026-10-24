#pragma once

// The joiner's native campaign transfer receiver (retail 0x608630, opcodes 'G' prepare / 'H' append a chunk), as the
// entity-capacity install replaces it: the same frame and side effects as the retail function, with the transfer
// buffer sized by the installed native save profile (retail 0x2fc78 / E10A2 0x200078) and every chunk bounded before
// it is copied (entity_capacity_rules::transfer_chunk). Its own translation unit so the native test executes it next
// to the retail function (test/entity_transfer_test.cpp).

#include "entity_capacity_rules.hpp"

#include <cstdint>

namespace entity_transfer
{
	using prepare_observer = void(*)(const void* event);
	void observe_prepare(prepare_observer);
	// JMP adapter for 0x6100e8, after the native reliable sequence-window check.
	void reliable_receive_room();
	void outgoing_room();
	void __cdecl transport_maintenance(void* managers);
	using backpressure_observer = void(*)(std::uint16_t expected, std::uint16_t sequence, entity_capacity_rules::message_pools used);
	using failure_observer = void(*)(const char* reason, entity_capacity_rules::message_pools used);
	void observe_backpressure(backpressure_observer);
	void observe_failure(failure_observer);
	std::uint32_t backpressure_count();
	struct observer
	{
		// A chunk the bounds refused (nothing copied) and a wrapper the game's pool could not allocate.
		void (*refused)(entity_capacity_rules::transfer_verdict verdict, std::uint32_t chunk, std::uint32_t length, std::uint32_t payload) = nullptr;
		void (*no_wrapper)(std::uint32_t size) = nullptr;
		void (*fast_refused)(entity_capacity_rules::fast_verdict verdict, std::uint32_t received, std::uint32_t length, std::uint32_t payload) = nullptr;
	};

	// [Online] FastTransfer sender: JMP adapter for 0x608004 in the native sender 0x607fb0, after the peer's queued
	// 'H' count (0x610790). It queues that peer's next records within fast_sends() and resumes at 0x6080ba.
	void fast_sender();
	void set_fast_payload(std::uint32_t payload);
	std::uint32_t __cdecl fast_send_peer(std::uint8_t* context, std::uint8_t* peer, void* connection, std::uint32_t outstanding);
	std::uint32_t fast_records_sent();

	// The receiver's own chunk counter (the counterpart of the retail DAT_00a3bc38).
	struct state
	{
		std::uint32_t chunks = 0;
		// [Online] FastTransfer (agreed by both peers): 'H' bodies are fast records and `received` counts image bytes.
		bool fast = false;
		std::uint32_t received = 0;
	};

	// event: the native callback event (+0 opcode byte, +4 body, +8 u16 length, +0xc context, +0x10 sender).
	std::uint32_t receive(std::uint8_t* event, const entity_capacity_rules::transfer_profile& profile, state& s, const observer& o);

	// 0x60872c..0x60875a: the 'J' acknowledgement after the final chunk - a zeroed 0x12-byte header (0x615810 on a local),
	// opcode 0x4a at +0x10 and 1 at +0xe, sent through the event's sender (+0x10).
	void send_ack(std::uint8_t* event);
}
