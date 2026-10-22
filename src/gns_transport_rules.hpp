#pragma once
#include "limits_rules.hpp"
#include <array>
#include <cstdint>

// [Online] Transport=gns (requested-only, gameplay rule 25 GnsTransport; docs/gns-transport.md). Admission, lobby and
// OpenSpy/GameSpy stay on the stock socket; once a stock peer is a participant of the session, its game messages go
// whole over a GameNetworkingSockets connection to the same address on the stock port + gns_port_offset:
//   0x60f620  enqueue (ecx manager; endpoint, header; ret 8)    entry -> bridge: a switched peer's message goes to
//                                                                 the adapter's buffer (coalesce +0xf honoured),
//                                                                 before the 300-byte fragment decision (0x60f6ef)
//   0x60d9c0  direct send (ecx manager; address, port, header)  entry -> bridge: unreliable, at once
//   0x610790  queued count of one opcode (ecx peer; opcode)     entry -> bridge: the adapter's backlog (buffered +
//                                                                 GNS-unacknowledged reliable + received-undelivered)
//   0x6a4aa4  manager vtable +0x1c = receive pump 0x616340      -> pump, then the adapter's tick (RunCallbacks,
//                                                                 ReceiveMessagesOnConnection, delivery, flush) on
//                                                                 the game's thread, inside its own scheduler task
//   0x60a86f  periodic flush task: jmp 0x60fe00                 -> the native flush, then the adapter's flush and
//                                                                 keepalive: the flush task runs in scheduler states the
//                                                                 pump task does not (live: a host reply waited out the
//                                                                 host's zone load in the adapter's buffer)
//   0x60a913  datagram parser task: jmp 0x60dca0                -> the native parser, then the adapter's: each received
//                                                                 GNS message enters the game as the parser enters a
//                                                                 datagram's (reliable -> slot 0 / 0x6100b0 -> the native
//                                                                 ordered queue and drain; unreliable -> dispatched)
// Inbound messages are delivered through the dispatcher 0x60dbe0 with the event the reliable drain 0x60ff00 builds.
// GNS listens on the stock game port + gns_port_offset (- the offset above 64535).
namespace gns_transport_rules
{
	// Lockstep stalls: the parser/flush tasks read the links when the pump task has not run for this long.
	constexpr unsigned long stall_service_ms = 250;
	constexpr std::uint32_t zones_static = 0x72a578, zones_vtable = 0x68878c, zones_loading_at = 0x220;
	constexpr std::uint32_t session_static = 0xa53058, session_vtable = 0x6a481c, session_set_up = 0x420, session_host = 0x421,
		session_first = 0x26c, session_count = 0x278, node_next_at = 0x8, node_record_at = 0x10, record_peer_at = 0x8,
		record_flags_at = 0x1c, record_index_at = 0x18;
	// Abrupt disconnects: the session's drop notice (0x611550) and lost-participant path (0x612c40), as the watchdog uses them.
	constexpr std::uint32_t native_drop_notice = 0x611550, native_lost = 0x612c40;
	// Their entries are filtered (one path for every loss, rule 25 revision 4): the first six bytes run in the bridge.
	constexpr std::uint32_t notice_resume = 0x611556, lost_resume = 0x612c46;
	inline constexpr std::array<unsigned char, 6> notice_original{0x83,0xec,0x20,0x55,0x8b,0xe9};
	inline constexpr std::array<unsigned char, 6> lost_original{0x55,0x8b,0xec,0x83,0xe4,0xf8};
	constexpr std::uint32_t lockstep_frame = 0xa3bbf4, lockstep_mode = 0xa3bbe4;
	constexpr unsigned long vanish_settle_ms = 1500;
	// The lockstep input ring (net drop3-030 RE): synchronizer 0x79fbb0 from its getter 0x55a390; one block per session
	// index (participant record +0x18, 0x614210) of input_block_size bytes: 300 records of input_record bytes
	// at +input_records ([0] valid, [1] delta kind - 0 once resolved, +4 the 0x24 record bytes whose first u16 is the
	// frame), read index +input_read, write index +input_write, next expected frame (u16) +input_next. The store
	// 0x559560 (thiscall: record bytes, index, kind; ret 0xc) files a record by its frame (first one wins, an earlier
	// frame is ignored) and resolves the contiguous ones; the consumer 0x559190 only clears a run frame's valid byte.
	constexpr std::uint32_t input_sync = 0x55a390, input_store = 0x559560;
	constexpr std::uint32_t input_block_size = 0x3184, input_records = 0x268, input_record = 0x28, input_read = 0x3170, input_write = 0x3174, input_next = 0x317c;
	using limits_rules::guard;
	constexpr std::uint32_t enqueue_va = 0x60f620, enqueue_resume = 0x60f627;
	constexpr std::uint32_t direct_va = 0x60d9c0, direct_resume = 0x60d9c6;
	constexpr std::uint32_t backlog_va = 0x610790, backlog_resume = 0x610796;
	constexpr std::uint32_t pump_slot = 0x6a4aa4, native_pump = 0x616340, parser_jump = 0x60a913, native_parser = 0x60dca0, flush_jump = 0x60a86f, native_flush = 0x60fe00, drain_jump = 0x60a7f3, native_drain = 0x610680;
	constexpr std::uint32_t peer_flush_va = 0x60f940, peer_flush_resume = 0x60f945;
	constexpr std::uint32_t native_dispatch = 0x60dbe0, native_session = 0x612be0, native_received = 0x610710, native_frame = 0x610750, native_peer_of = 0x60db20, fixed_lengths = 0xa3e490;
	inline constexpr std::array<unsigned char, 7> enqueue_original{0x8b,0x44,0x24,0x04,0x83,0xec,0x08};
	inline constexpr std::array<unsigned char, 6> direct_original{0x81,0xec,0x18,0x05,0x00,0x00};
	inline constexpr std::array<unsigned char, 6> backlog_original{0x8b,0x91,0x9c,0x30,0x00,0x00};
	inline constexpr std::array<unsigned char, 4> pump_original{0x40,0x63,0x61,0x00};
	inline constexpr std::array<unsigned char, 5> parser_original{0xe9,0x88,0x33,0x00,0x00};
	inline constexpr std::array<unsigned char, 5> flush_original{0xe9,0x8c,0x55,0x00,0x00};
	inline constexpr std::array<unsigned char, 5> drain_original{0xe9,0x88,0x5e,0x00,0x00};
	inline constexpr std::array<unsigned char, 5> peer_flush_original{0x83,0xec,0x28,0x53,0x55};
	inline constexpr std::array<guard, 21> guards{{
		{0x611550,"83ec20558be98b851c040000","the session's drop notice 0x611550 (index, lost code, others' code, flag)"},
		{0x612c40,"558bec83e4f881ec14010000","the session's lost-participant path 0x612c40 (record, reason, flag)"},
		{0x613adc,"6a00680711000068cc100000508bcfe860daffff6a0068cd100000568bcfe841f1ffff","the host's watchdog: notice 0x10cc/0x1107, then 0x612c40 with 0x10cd"},
		{0x60f940,"83ec2853558b6c24348d451c","flush-one-peer entry (mode 2)"},
		{0x60a7ef,"74078bc8e9885e0000c3","reliable drain task's jump to 0x610680"},
		{0x60a86b,"74078bc8e98c550000c3","periodic flush task's jump to 0x60fe00"},
		{0x61585a,"0fb64b10668b044d90e4a300","the body copy's fixed-length table at 0xa3e490"},
		{0x60a90f,"74078bc8e988330000c3","datagram parser task's jump to 0x60dca0"},
		{0x60db20,"8b81c424010085c0","enqueue's peer lookup by endpoint"},
		{0x60f620,"8b44240483ec085350e8f2e4ffff8bd8","enqueue entry (0x60db20 peer lookup)"},
		{0x60d9c0,"81ec18050000a1f8386f00538bd95589","direct send entry"},
		{0x610790,"8b919c300000538a5c240833c085d256","queued-count entry"},
		{0x6a4aa4,"40636100","manager vtable +0x1c (receive pump 0x616340)"},
		{0x60dbe0,"5356578b7c2410","dispatcher entry"},
		{0x610710,"8b44240489817c3000008b44240885c0","peer last-receive update"},
		{0x610750,"8b442404898188300000c20400","peer receive-frame update"},
		{0x55d7e0,"8b4104c3","connection endpoint getter (+4)"},
		{0x4692d0,"8b4108c3","participant connection getter (+8)"},
		{0x559560,"518b44240c69c08431000053568b7424","the input ring's store 0x559560 (record, index, kind)"},
		{0x5596f0,"c20c00","the input ring store's return (3 arguments)"},
		{0x55a390,"64a1000000008a0d40c27a00","the input synchronizer getter 0x55a390 (static 0x79fbb0, guard 0x7ac240)"},
	}};
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
			if (!limits_rules::matches(image + g.va - limits_rules::image_base, g.hex)) return &g;
		return nullptr;
	}

	// Native layouts the adapter reads (net030-map REPORT sections 2-4, verified there and here).
	constexpr std::uint32_t manager_peers = 0x124b8, manager_peer_count = 0x124c4, manager_socket = 0x14c28;
	constexpr std::uint32_t manager_clock = 0x137b8, manager_frame = 0x137b4, session_manager = 0x41c;
	constexpr std::uint32_t session_participants = 0x26c, session_participant_count = 0x278;
	constexpr std::uint32_t participant_flags = 0x1c, participant_connection = 8;
	constexpr std::uint32_t peer_endpoint = 4, peer_port = 8, peer_address = 0x14, peer_connected = 0x19;
	constexpr std::uint32_t peer_receive_count = 0x309c, peer_send_count = 0x30ac, peer_receive_queue = 0x3090, peer_expected = 0x308c;
	// The largest body the native message pools hold (0x60e910: 0x145); a longer reliable message is dispatched whole, as
	// the native reassembly does (0x61027d).
	constexpr std::size_t pool_body_max = 0x145;
	constexpr std::uint32_t node_next = 8, node_object = 0x10;

	// GNS listens on the stock game port + this offset, on the stock socket's address.
	constexpr std::uint16_t gns_port_offset = 1000;
	// Rule 25's effective values: {on, port offset, framing revision, 0}.
	// Revision 2 / framing 2: the datagram kind (4) for rooms' pre-admission tunnel joined the link framing.
	// Revision 3 / framing 3: a participant lost abruptly is cut at one agreed frame (kinds 5 lost_inputs and 6
	// lost_ready); a PC without it would cut at its own frame and end a 3+ player session, so the two refuse each other.
	// Revision 4 (framing 3): every loss takes that path - the host's native timeout/lag-report removal of a remote
	// participant is deferred to the agreed frame and another participant's lost message ends this PC's link too; a
	// revision-3 host would still remove natively without the agreement (swap-030 F01), so the two refuse each other.
	constexpr std::uint16_t agreement_revision = 4;
	constexpr std::uint32_t framing_revision = 3;
}
