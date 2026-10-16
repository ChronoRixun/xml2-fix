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
	// The reliable queue shares 768 message headers and only 50 bodies above 60 bytes. Leave one of each for
	// the missing in-order message: otherwise an out-of-order backlog can prevent its own gap from ever closing.
	constexpr bool reliable_queue_room(std::uint16_t expected, std::uint16_t sequence, std::uint32_t body_bytes,
	                                   std::uint32_t large_used, std::uint32_t headers_used)
	{
		const bool next = expected == sequence;
		return headers_used < (next ? 768u : 767u) &&
		       (body_bytes <= 60 || large_used < (next ? 50u : 49u));
	}
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
	inline constexpr std::array<extra_guard, 3> extra_guards{{
		{0x614280, "8d411cc3", "CEntityGrid slot 8 body (lea eax,[ecx+0x1c]; ret) outside the grid region"},
		{0x44ea40, "8b44240c8b542404", "CCollide vtable slot 3 (0x44ea40) entry: replaced whole (plan section 10, deviation 1)"},
		{0x6100e8, "53668b5e08", "reliable receive: before header/body allocation"},
	}};
	// Sites the implementation adds to the reviewed ledger (plan section 10): CCollide vtable slot 3 (0x44ea40) forwards
	// a caller's entity set into 0x44d940, whose two failure paths clear only eight dwords of it; the frame is replaced
	// whole like the twelve enumerated physics frames. Its body (0x44ea40..0x44eaf6) is hashed like the other regions.
	struct extra_relocation { std::uint32_t va; std::string_view old_hex, prefix_hex; std::string_view symbol; std::uint8_t pad; const char* role; };
	inline constexpr std::array<extra_relocation, 2> extra_relocations{{
		{0x44ea40, "8b44240c8b542404", "e9", "entity_capacity::Physics_0044ea40", 3, "CCollide slot 3: complete frame replacement with a 128-byte set clear"},
		{0x6100e8, "53668b5e08", "e9", "entity_capacity::ReliableReceiveRoom", 0, "reliable receive: reserve gap capacity and defer full datagrams before ACK"},
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
	inline constexpr std::array<extra_region, 7> extra_regions{{
		{"physics slot 3 (0x44ea40)", 0x44ea40, 0x44eaf6, "629c487136d717b8751b749df1f7fcc9724146f6864e51b77eaf45d142a17e7e"},
		{"reliable receive wrapper", 0x6100b0, 0x6101af, "b9c5891b893d9e40d09d01f21aead69e87fb38a155e0a787b1182d3a9e45244f"},
		{"reliable datagram parser", 0x60dca0, 0x60de27, "9ff3b92457b79f589aa9043d9e72139864f80d351ca34793e4459c977688f9be"},
		{"queued datagram ACK ordering", 0x60f940, 0x60fdf2, "70ad7edac2247083a7961d4bb14c16e6eaa7ff16a828abc1f914395bbbd6436d"},
		{"message cache accessor", 0x60f240, 0x60f27a, "78ed02d98e1a87fb945b06ac23333f861a7194314486499152f16b0e2f7ab2da"},
		{"large-message allocator", 0x60e910, 0x60e971, "3ff8e18fb4e97f90f5bce6bfed2e31ec28d7211bdd28f920c4bdc831864eaccc"},
		{"message-header allocator", 0x60e740, 0x60e7c9, "6d3aef70f7c79414198d9ae1eb8f55ed5560ba55b4208ebb17dec17da2231f82"},
	}};
}
