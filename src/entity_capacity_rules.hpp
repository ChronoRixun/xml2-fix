#pragma once

// The entity-capacity install's decisions, apart from the patching, so the native test checks them without a
// game: the allocator layout and ring rebuild, the save-manager claim state machine, the native campaign transfer
// bounds, startup quiescence, and which ledger symbols other modules write themselves.

#include "entity_capacity_layout.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>

namespace entity_capacity_rules
{
	// ---- allocator (retail CEntityAllocator 0x7e6d4 bytes -> 0x189218; round6 layouts.json "allocator") -------
	namespace allocator_layout
	{
		constexpr std::uint32_t actor_base = 0x4, actor_stride = 0x848, actor_slots = 32;
		constexpr std::uint32_t generic_base = 0x109a0, generic_stride = 0x7d4; // the generic sub-object (ECX of 0x461680/0x461480)
		// sub-object relative (the native code addresses the generic arena from ECX = allocator + 0x109a0)
		constexpr std::uint32_t sub_ring = 0x177c04, sub_enqueue = 0x178808, sub_dequeue = 0x17880c, sub_count = 0x178810, sub_bitmap = 0x178814, sub_active = 0x178874;
		constexpr std::uint32_t bitmap_words = 24;
		constexpr std::uint32_t object_size = 0x189218;
		static_assert(generic_base + sub_active + 4 == object_size);
		static_assert(generic_base + entity_capacity_layout::raised_generic * generic_stride == generic_base + sub_ring - 4);
		static_assert(sub_bitmap + bitmap_words * 4 == sub_active);
	}
	constexpr std::uint32_t e10a2_manager_size = 0x80c24;

	// The ring rebuild of 0x461480 for N slots, preceded by the whole-bitmap clear the native constructor and empty
	// reset only did for seven dwords: enqueue/dequeue/count zeroed, then slot i stored at ring[i] (the native loop
	// increments the enqueue cursor, wraps it at N and stores at N-1 on the wrap), count N, cursor 0.
	inline void generic_ring_rebuild(std::uint8_t* generic, std::uint32_t n)
	{
		using namespace allocator_layout;
		auto u32 = [&](std::uint32_t at) -> std::uint32_t& { return *reinterpret_cast<std::uint32_t*>(generic + at); };
		for (std::uint32_t w = 0; w < bitmap_words; ++w) u32(sub_bitmap + 4 * w) = 0;
		u32(sub_active) = 0;
		u32(sub_enqueue) = 0; u32(sub_dequeue) = 0; u32(sub_count) = 0;
		for (std::uint32_t i = 0; i < n; ++i)
		{
			std::uint32_t at = u32(sub_enqueue);
			u32(sub_enqueue) = at + 1;
			u32(sub_count) = u32(sub_count) + 1;
			if (at + 1 > n - 1) { u32(sub_enqueue) = 0; at = n - 1; }
			u32(sub_ring + 4 * at) = i;
		}
	}

	// ---- the save-manager claim (plan section 6) ---------------------------------------------------------------
	enum class claim_state { unprepared, prepared, claimed, constructing, in_use, poisoned };
	constexpr const char* claim_state_text(claim_state s)
	{
		switch (s)
		{
		case claim_state::unprepared: return "unprepared";
		case claim_state::prepared: return "prepared";
		case claim_state::claimed: return "claimed";
		case claim_state::constructing: return "constructing";
		case claim_state::in_use: return "in-use";
		case claim_state::poisoned: return "poisoned";
		}
		return "?";
	}
	// Allocation (0x487344): only a prepared claim of exactly the manager size may be claimed; anything else is a
	// fail-stop in the caller (never a null return into unchecked native initialisation).
	constexpr claim_state claim_allocate(claim_state current, std::uint32_t size, std::uint32_t manager_size)
	{
		return current == claim_state::prepared && size == manager_size && manager_size != 0 ? claim_state::claimed : claim_state::poisoned;
	}
	// Construction result (0x487362): the native constructor must return exactly the claimed backing.
	inline claim_state claim_construct_result(bool returned, const void* result, const void* backing)
	{
		return returned && result && result == backing ? claim_state::in_use : claim_state::poisoned;
	}

	// ---- native campaign transfer (0x608630, opcodes 'G'/'H') --------------------------------------------------
	constexpr std::uint32_t transfer_chunk_size = 200;
	struct transfer_profile { std::uint32_t payload, wrapper, description, flag; };
	constexpr transfer_profile retail_transfer{0x2fc00, 0x2fc78, 0x2fc04, 0x2fc44};
	constexpr transfer_profile e10a2_transfer{0x200000, 0x200078, 0x200004, 0x200044};
	// All four pools are shared with sends and other native lists. Future receives leave gap capacity;
	// sends reserve it too. This is admission control, not a promise that unrelated native users cannot
	// consume the reserve: the runtime also bounds a pressure stall and fails the session at a safe boundary.
	struct message_pools { std::uint32_t large, headers, small, nodes; };
	constexpr bool reliable_queue_room(std::uint16_t expected, std::uint16_t sequence, std::uint32_t body_bytes,
	                                   message_pools used)
	{
		const unsigned reserve = expected == sequence ? 0 : 1;
		return used.headers < 768 - reserve && used.nodes < 650 - reserve &&
		       (body_bytes <= 60 ? used.small < 768 - reserve : used.large < 50 - reserve);
	}
	constexpr bool outgoing_queue_room(std::uint32_t bytes, bool has_body, message_pools used)
	{
		// Native fragmentation first queues a six-byte descriptor, then ceil(bytes/300) chunks.
		// A chunk has a one-byte fragment tag; even a 60-byte tail therefore uses a large body.
		unsigned headers = 1, small = has_body && bytes <= 60 ? 1 : 0, large = has_body && bytes > 60 ? 1 : 0;
		if (bytes > 300)
		{
			const auto full = bytes / 300, tail = bytes % 300;
			headers = 1 + full + (tail != 0);
			small = 1 + (tail != 0 && tail < 60);
			large = full + (tail >= 60);
		}
		return used.headers < 768 && headers < 768 - used.headers &&
		       used.nodes < 650 && headers < 650 - used.nodes &&
		       (small == 0 || (used.small < 768 && small < 768 - used.small)) &&
		       (large == 0 || (used.large < 50 && large < 50 - used.large));
	}
	struct pressure_deadline
	{
		static constexpr std::uint64_t limit_ms = 30000;
		bool active = false;
		std::uint16_t expected = 0;
		std::uint64_t since = 0;
		void block(std::uint16_t next, std::uint64_t now)
		{
			if (!active || expected != next) { active = true; expected = next; since = now; }
		}
		bool expired(std::uint16_t next, std::uint64_t now)
		{
			if (active && expected != next) active = false;
			return active && now - since >= limit_ms;
		}
	};
	enum class transfer_verdict { accept, complete, no_buffer, too_long, overflow, short_before_end };
	constexpr const char* transfer_verdict_text(transfer_verdict v)
	{
		switch (v)
		{
		case transfer_verdict::accept: return "accept";
		case transfer_verdict::complete: return "complete";
		case transfer_verdict::no_buffer: return "no transfer buffer";
		case transfer_verdict::too_long: return "chunk longer than 200 bytes";
		case transfer_verdict::overflow: return "chunk past the payload end";
		case transfer_verdict::short_before_end: return "short chunk before the final one";
		}
		return "?";
	}
	// Chunks arrive in order (the counter is the only index the native protocol carries): each is at most 200 bytes,
	// lands inside the payload, and only the final chunk may be short. The native code copied unconditionally.
	constexpr transfer_verdict transfer_chunk(std::uint32_t chunks_so_far, std::uint32_t length, std::uint32_t payload, bool have_buffer)
	{
		if (!have_buffer) return transfer_verdict::no_buffer;
		if (length > transfer_chunk_size) return transfer_verdict::too_long;
		const std::uint64_t at = std::uint64_t(chunks_so_far) * transfer_chunk_size;
		if (at + length > payload) return transfer_verdict::overflow;
		if (at + length == payload) return transfer_verdict::complete;
		if (length < transfer_chunk_size) return transfer_verdict::short_before_end;
		return transfer_verdict::accept;
	}

	// ---- [Online] FastTransfer: the agreed campaign record stream (docs/online-campaign-transfer.md) ---------
	// The raised image is 2 MiB but mostly zero (a real slot: 27,068 non-zero bytes). Every 'H' body is one record
	// covering the next image bytes in order: 'D' + 1..299 bytes copied, or 'Z' + a u32 run of zero bytes. A record
	// is at most 300 bytes (one large body, never fragmented); a zero record and short data use small bodies.
	constexpr std::uint8_t fast_data_tag = 'D', fast_zero_tag = 'Z';
	constexpr std::uint32_t fast_data_max = 299, fast_zero_min = 64;
	// Records in flight per peer, and the room each send leaves in the shared pools for receiving and other traffic.
	constexpr std::uint32_t fast_window = 32, fast_large_margin = 8, fast_queue_margin = 64;
	// Per-peer datagram budget (peer +0x212c, bytes per second; retail 100,000 at 0x61097b).
	constexpr std::uint32_t fast_peer_budget = 400000;
	struct fast_record { std::uint8_t tag; std::uint32_t covered; };
	// The next record at `at` (< end). Zero runs shorter than fast_zero_min travel inside data records.
	inline fast_record fast_next(const std::uint8_t* image, std::uint32_t at, std::uint32_t end)
	{
		std::uint32_t zeros = 0;
		while (at + zeros < end && !image[at + zeros]) ++zeros;
		if (zeros && (zeros >= fast_zero_min || at + zeros == end)) return {fast_zero_tag, zeros};
		const auto limit = end - at < fast_data_max ? end - at : fast_data_max;
		std::uint32_t n = 0, run = 0;
		for (; n < limit; ++n)
		{
			run = image[at + n] ? 0 : run + 1;
			if (run == fast_zero_min) return {fast_data_tag, n + 1 - run};
		}
		// A run reaching the record limit may continue: end the data before it if it can become a zero record.
		if (run && n == limit && limit < end - at && run < n)
		{
			std::uint32_t more = 0;
			while (at + n + more < end && run + more < fast_zero_min && !image[at + n + more]) ++more;
			if (run + more >= fast_zero_min || at + n + more == end) return {fast_data_tag, n - run};
		}
		return {fast_data_tag, n};
	}
	// How many records this peer may queue now: its window, then every shared pool keeps its margin. One record
	// takes one header, one queue node and one body (large or small).
	constexpr std::uint32_t fast_sends(std::uint32_t outstanding, message_pools used)
	{
		auto room = [](std::uint32_t used, std::uint32_t capacity, std::uint32_t margin) { return used + margin < capacity ? capacity - margin - used : 0u; };
		std::uint32_t n = outstanding < fast_window ? fast_window - outstanding : 0;
		for (const auto r : {room(used.large, 50, fast_large_margin), room(used.small, 768, fast_queue_margin),
		                     room(used.headers, 768, fast_queue_margin), room(used.nodes, 650, fast_queue_margin)})
			n = r < n ? r : n;
		return n;
	}
	enum class fast_verdict { accept, complete, no_buffer, malformed, overflow };
	constexpr const char* fast_verdict_text(fast_verdict v)
	{
		switch (v)
		{
		case fast_verdict::accept: return "accept";
		case fast_verdict::complete: return "complete";
		case fast_verdict::no_buffer: return "no transfer buffer";
		case fast_verdict::malformed: return "malformed record";
		case fast_verdict::overflow: return "record past the payload end";
		}
		return "?";
	}
	// A received record at image offset `received`: its tag and size, the covered bytes, the payload bound.
	constexpr fast_verdict fast_chunk(std::uint32_t received, const std::uint8_t* body, std::uint32_t length, std::uint32_t payload,
	                                  bool have_buffer, std::uint32_t& covered)
	{
		covered = 0;
		if (!have_buffer) return fast_verdict::no_buffer;
		if (length < 2 || !body) return fast_verdict::malformed;
		if (body[0] == fast_data_tag)
		{
			if (length - 1 > fast_data_max) return fast_verdict::malformed;
			covered = length - 1;
		}
		else if (body[0] == fast_zero_tag)
		{
			if (length != 5) return fast_verdict::malformed;
			covered = std::uint32_t(body[1]) | std::uint32_t(body[2]) << 8 | std::uint32_t(body[3]) << 16 | std::uint32_t(body[4]) << 24;
			if (!covered) return fast_verdict::malformed;
		}
		else return fast_verdict::malformed;
		if (received > payload || covered > payload - received) { covered = 0; return fast_verdict::overflow; }
		return received + covered == payload ? fast_verdict::complete : fast_verdict::accept;
	}

	// ---- startup quiescence: the raise needs LC saves and stock owners not yet built -------------------------
	template <typename Read>
	bool startup_quiescent(Read&& read, std::string_view save_format_text, std::uint32_t game_base, std::string& why)
	{
		std::string lower(save_format_text);
		for (auto& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
		const auto value = lower.find_first_not_of(" \t");
		if (value == std::string::npos || lower.substr(value, 2) != "lc") { why = "[Limits] EntityCapacity needs [Save] Format=lc (expanded native saves exist only as LC files)"; return false; }
		if (game_base != 0x400000) { why = "XMen2.exe is not loaded at 0x400000"; return false; }
		std::uint32_t v = 0;
		if (!read(entity_capacity_layout::allocator_pointer_cell, v) || v) { why = "the game already built its entity allocator"; return false; }
		if (!read(0x781ee4, v) || (v & 1)) { why = "the game already built its World"; return false; }
		if (!read(0x729888, v) || (v & 1)) { why = "the game already built its entity grid"; return false; }
		if (!read(0x758900, v) || (v & 1)) { why = "the game already built its query manager"; return false; }
		if (!read(entity_capacity_layout::world_pointer_cell, v) || v) { why = "the World pointer is already published"; return false; }
		return true;
	}

	// ---- why a requested raise fell back to stock, and what the player can do about it ------------------------
	// The startup warning used to blame the executable for every fallback; the advice now follows the cause.
	enum class fallback_cause
	{
		save_format,  // [Save] Format=lc missing: a configuration problem, the executable is fine
		executable,   // XMen2.exe is not loaded at 0x400000 (not the game, or relocated)
		changed_code, // a ledger guard or region hash differs: a different or patched XMen2.exe, or another patch
		load_order,   // the game had already built its entity owners when the fix ran
		internal,     // memory, symbol binding, owner preparation or the transaction itself: the fix's own problem
	};
	// The cause of a startup_quiescent refusal, from the same inputs it refused.
	inline fallback_cause quiescence_cause(std::string_view save_format_text, std::uint32_t game_base)
	{
		std::string lower(save_format_text);
		for (auto& c : lower) c = static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
		const auto value = lower.find_first_not_of(" \t");
		if (value == std::string::npos || lower.substr(value, 2) != "lc") return fallback_cause::save_format;
		if (game_base != 0x400000) return fallback_cause::executable;
		return fallback_cause::load_order;
	}
	inline const char* fallback_advice(fallback_cause cause)
	{
		switch (cause)
		{
		case fallback_cause::save_format:
			return "xml2-fix.ini (next to XMen2.exe) is missing [Save] Format=lc, which the raised capacity needs. A Legends Classic "
			       "build writes it: rebuild or reinstall the build (or add the line yourself), or set [Limits] EntityCapacity=0 "
			       "to play with the stock limits without this warning.";
		case fallback_cause::executable:
			return "The running program is not loaded where the retail XMen2.exe loads, so it is not the executable this fix patches. "
			       "Start the retail XMen2.exe from the build folder.";
		case fallback_cause::changed_code:
			return "XMen2.exe's entity code is not the retail code: the executable is a different or modified version, or another "
			       "patch, trainer or tool changed it. Use the unmodified retail XMen2.exe and turn off other executable patches.";
		case fallback_cause::load_order:
			return "The game had already started building its world when XML2 Fix loaded. Start XMen2.exe directly (or from the "
			       "launcher) without other loaders or injectors.";
		case fallback_cause::internal:
			return "This is a problem in XML2 Fix itself, not in your setup: please report it with xml2-fix.log.";
		}
		return "";
	}
	// The whole startup warning (one message box; the game continues with stock capacities).
	inline std::string fallback_notice_text(const std::string& failure, fallback_cause cause)
	{
		return "The raised entity capacity ([Limits] EntityCapacity=1) could not be installed:\n" + failure +
		       "\n\nThe game continues with the stock entity limits (224 props, projectiles and triggers in a zone): crowded "
		       "rooms can lose objects, saves made with the raised capacity cannot be loaded, and online play is unavailable "
		       "in this session.\n\n" + fallback_advice(cause) + "\nxml2-fix.log has the details.";
	}

	// Ledger symbols another module writes as part of its own contribution (same transaction, same bytes).
	constexpr bool owned_elsewhere(std::string_view symbol)
	{
		return symbol.starts_with("save_format::") || symbol.starts_with("gameplay_agreement::") ||
		       symbol.starts_with("entity_capacity::StreamBound_") || symbol.starts_with("entity_capacity::SaveCore_");
	}

	// The receive-lifecycle sites of plan section 5 (certificate epochs, dispatch floor, reliable/fragment epochs,
	// deferred topology, Node purge) that this implementation does NOT install (plan section 10, deviation 17): the
	// ledger names them as gameplay_agreement symbols; no contribution writes them, and the install reports them as
	// descoped instead of skipping them silently. Every OTHER composed ledger site must be written by its owner in the
	// same transaction (patch_transaction requires_written), or the transaction refuses.
	inline constexpr std::array<std::uint32_t, 21> descoped_receive_sites{{
		0x6097f0, 0x60d9c0, 0x60dbe0, 0x60dc60, 0x60dca0, 0x60e300, 0x60eac0, 0x60ece0, 0x60f010, 0x60f070, 0x60f0a0,
		0x60f620, 0x60f940, 0x60ff00, 0x6101b0, 0x610680, 0x6106f0, 0x610950, 0x610a50, 0x616340, 0x616600,
	}};
	constexpr bool descoped_receive_site(std::uint32_t va)
	{
		for (const auto d : descoped_receive_sites) if (d == va) return true;
		return false;
	}

	// Guards the ledger's region hashes do not cover but the ports depend on.
	struct extra_guard { std::uint32_t va; std::string_view hex; const char* what; };
	inline constexpr std::array<extra_guard, 5> extra_guards{{
		{0x614280, "8d411cc3", "CEntityGrid slot 8 body (lea eax,[ecx+0x1c]; ret) outside the grid region"},
		{0x44ea40, "8b44240c8b542404", "CCollide vtable slot 3 (0x44ea40) entry: replaced whole (plan section 10, deviation 1)"},
		{0x6100e8, "53668b5e08", "reliable receive: before header/body allocation"},
		{0x60f642, "8a4f0e84c9", "outgoing enqueue: before any allocation"},
		{0x60a880, "8b4c240485c9", "transport maintenance: safe deferred failure boundary"},
	}};
	// Sites the implementation adds to the reviewed ledger (plan section 10): CCollide vtable slot 3 (0x44ea40) forwards
	// a caller's entity set into 0x44d940, whose two failure paths clear only eight dwords of it; the frame is replaced
	// whole like the twelve enumerated physics frames. Its body (0x44ea40..0x44eaf6) is hashed like the other regions.
	struct extra_relocation { std::uint32_t va; std::string_view old_hex, prefix_hex; std::string_view symbol; std::uint8_t pad; const char* role; };
	inline constexpr std::array<extra_relocation, 4> extra_relocations{{
		{0x44ea40, "8b44240c8b542404", "e9", "entity_capacity::Physics_0044ea40", 3, "CCollide slot 3: complete frame replacement with a 128-byte set clear"},
		{0x6100e8, "53668b5e08", "e9", "entity_capacity::ReliableReceiveRoom", 0, "reliable receive: reserve gap capacity and defer full datagrams before ACK"},
		{0x60f642, "8a4f0e84c9", "e9", "entity_capacity::OutgoingRoom", 0, "outgoing enqueue: preflight all fragments and shared pools"},
		{0x60a880, "8b4c240485c9", "e9", "entity_capacity::TransportMaintenance", 1, "transport maintenance: bounded pressure failure outside dispatch"},
	}};
	// Literal sites the reviewed ledger missed (plan section 10, deviation 19). 0x484c76 is the zone manager loader's
	// read of the last side frame's four party names, `lea eax,[edi+edx+0x1ae0c]` = manager + 0x1ad68 (the side frame)
	// + 0xa4: every other field of that frame and of the manager tail is in the ledger, this derived one was not, so a
	// raised cold load seated four names read from E10A2 zone-state storage (an empty party). Under E10A2 every manager
	// tail field moves by 5 * (0x19b88 - 0x55e0) = 0x65c48.
	struct extra_literal { std::uint32_t va; std::string_view old_hex, new_hex; const char* role; };
	inline constexpr std::array<extra_literal, 1> extra_literals{{
		{0x484c76, "8d84170cae0100", "8d8417540a0800", "zone manager load: last side frame party names (manager + 0x1ae0c -> + 0x80a54)"},
	}};
	// Manager tail fields (retail [0x1ad60, 0x1afdc)) that belong to OTHER objects of the same size class and must not
	// be moved: the 0x1aefc-byte object of 0x4f7570..0x5003c0 (fields 0x1aec0..0x1aef8). The capacity test scans the
	// retail .text for every 4-byte value in the tail range and requires each occurrence to be a ledger/extra site or
	// one of these.
	struct foreign_tail { std::uint32_t lo, hi_inclusive; const char* what; };
	inline constexpr std::array<foreign_tail, 1> foreign_tail_ranges{{
		{0x4f7570, 0x500450, "the 0x1aefc-byte object (fields 0x1aec0..0x1aefc), not the save manager"},
	}};
	struct extra_region { const char* name; std::uint32_t lo, hi_exclusive; std::string_view sha256; };
	inline constexpr std::array<extra_region, 17> extra_regions{{
		{"physics slot 3 (0x44ea40)", 0x44ea40, 0x44eaf6, "629c487136d717b8751b749df1f7fcc9724146f6864e51b77eaf45d142a17e7e"},
		{"reliable receive wrapper", 0x6100b0, 0x6101af, "b9c5891b893d9e40d09d01f21aead69e87fb38a155e0a787b1182d3a9e45244f"},
		{"reliable datagram parser", 0x60dca0, 0x60de27, "9ff3b92457b79f589aa9043d9e72139864f80d351ca34793e4459c977688f9be"},
		{"queued datagram ACK ordering", 0x60f940, 0x60fdf4, "4a670d5ae79d600bd0d4b09ea8fc6904cae97a9c02e3d31ce588f4c74b60601b"},
		{"message cache accessor", 0x60f240, 0x60f27a, "78ed02d98e1a87fb945b06ac23333f861a7194314486499152f16b0e2f7ab2da"},
		{"large-message allocator", 0x60e910, 0x60e973, "fd37209d13e6fb0596154f5c076964600840caf6fbb1f82bb5b8b03179fb0884"},
		{"message-header allocator", 0x60e740, 0x60e7c9, "6d3aef70f7c79414198d9ae1eb8f55ed5560ba55b4208ebb17dec17da2231f82"},
		{"shared queue-node accessor", 0x6067d0, 0x606840, "83b0bbcf1149c75d970ae09ab17339cc4f38af470bce933bef5d9ebe8fe734fc"},
		{"shared queue-node allocator", 0x60e7d0, 0x60e82b, "8160ab528998f74b6b15686159ed42bbc978c6461a2c2259e13d8dbf97770d27"},
		{"shared queue-node ring allocation", 0x605740, 0x6057a2, "a4e8d6de9e72c94c04d812e564260afba942375fafb874601c16a4b7fc8fe6f6"},
		{"outgoing message enqueue", 0x60f620, 0x60f933, "e774b655b938b459802f5d39891c1866a9ebaad4ef5689ba32a60ca9fcc3f608"},
		{"outgoing message copy", 0x615840, 0x6158d2, "f98018c8f54621f94a47241654c7f16516d820dcdf86543ed9e51fab34f69f6e"},
		{"transport maintenance callback", 0x60a880, 0x60a8a9, "fc1e1a03a7b39dcfc48819250e2cfd615f291ece67ce79b08eec4e607075d825"},
		{"campaign context accessor", 0x6097b0, 0x6097ea, "f728e0c5720a0c4e8a49786db01824aa62a89a7a83b12fc6301154090c9a85ae"},
		{"connection failure callback", 0x609ad0, 0x609b00, "08e532aea2606cb2e42f67f10114292c174e67a4ccfa31a6c36e6de71d39ea2d"},
		{"connection failure presentation", 0x609b30, 0x609b77, "21e3f9a0fb11d911ef88f23bade980c9a0cb344831fb06d0d353cd068082d110"},
		{"small-body and header ring allocation", 0x60e090, 0x60e167, "61c5991594ddc123e4d11ab11fb8946f7ac8d6fcce3de8a95bd20b87ea0f07c8"},
	}};

	// [Online] FastTransfer (requested-only; absent, the contribution above is byte-identical): the native sender's
	// per-peer chunk loop is replaced from 0x608004 (after the queued-'H' count) to 0x6080ba (next peer), and a new
	// peer's datagram budget is raised. Same transaction: guards, region hashes, readback, rollback.
	inline constexpr std::array<extra_relocation, 1> fast_relocations{{
		{0x608004, "8b7c24388bb750220000", "e9", "entity_capacity::FastSender", 5, "campaign sender: agreed fast records within window and pool headroom"},
	}};
	inline constexpr std::array<extra_literal, 1> fast_literals{{
		{0x61097b, "c7862c210000a0860100", "c7862c210000801a0600", "peer datagram budget 100,000 -> 400,000 bytes per second"},
	}};
	inline constexpr std::array<extra_region, 4> fast_regions{{
		{"campaign sender up to its chunk loop", 0x607fb0, 0x608035, "4395b861de6c073a971ae06d11f5509f33b330da9451cd7baf630a53f304960d"},
		{"campaign sender next-peer resume", 0x6080ba, 0x6080cb, "4758c8be59373c6e5cf3989c72f5c991e7c86731150d78059634424adb9e4fe6"},
		{"peer initialisation (datagram budget)", 0x610950, 0x6109b3, "b72ec5dedd9f943a70450f7b9167d07c06b407f3d339789f3a04784ea3e6e2d5"},
		{"peer index accessor", 0x614210, 0x614214, "38058506e8e859d5be73a33baa621352c08ea01f2224ee170173253757193941"},
	}};
}
