#pragma once

// The timed-event manager's record pool (docs/timed-event-pool.md).
//
// Every entity think, actor update, physics step and "moved" notice of the world simulation is one record of the
// timed-event manager (static object 0x75d340, vtable 0x68de14, getter 0x4b2fc0; body = object + 4): a 24-byte record
// {owner handle, event*, 16 payload bytes} taken from a free ring, and an 8-byte (time, record index) pair in a binary
// heap. Retail holds 1125 records and 1125 heap pairs and checks neither bound:
//   - record alloc 0x4b2c10 reads ring[read] whether or not the ring is empty, so the 1126th pending event is handed
//     the index of a record that is still in use (its event is overwritten and never fires);
//   - heap push 0x4b2c90 writes pair[count]; pair[1125] ends on the heap's own count (heap + 0x232c), so the same
//     event's record index becomes the heap count and every pair behind it is cut off the queue.
// Retail cannot get there with its 256 world entities. With the entity capacity raised to 1024 a heavy fight can, and
// the world then stops updating piece by piece (the game clock, menus and sound run outside the queue).
//
// The raise moves the whole object to a larger block of the same shape and edits every operand of the manager's own
// code that holds the capacity or a member offset behind the record array (event_pool_sites.hpp). The code, the heap
// order and the record order stay retail; only the bounds move.

#include "event_pool_sites.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <string_view>
#include <vector>

namespace event_pool_rules
{
	constexpr std::uint32_t retail_records = 0x465, raised_records = 0x2000, min_records = retail_records, max_records = 0x10000;
	constexpr std::uint32_t retail_object = 0x75d340, manager_vtable = 0x68de14, manager_getter = 0x4b2fc0, init_flag = 0x7672b4;
	constexpr std::uint32_t record_bytes = 24, body_at = 4;
	constexpr std::uint32_t reset_slot = manager_vtable + 4, add_slot = manager_vtable + 8, pump_slot = manager_vtable + 0xc;
	constexpr std::uint32_t native_reset = 0x4b2ad0, native_add = 0x4b2b20, native_pump = 0x4b2d60;

	// Member offsets from the body, for a pool of `records` records. The shape is retail's: records, the used bitmap,
	// the free ring (one spare word, then write / read / count), the alloc bitmap, the record count, the heap (one
	// spare word, then its count).
	struct layout
	{
		std::uint32_t records, words, used_bitmap, ring, ring_write, ring_read, ring_count, alloc_bitmap, record_count, heap, heap_count, body_bytes;
		constexpr bool operator==(const layout&) const = default;
	};
	constexpr layout layout_for(const std::uint32_t records)
	{
		layout l{};
		l.records = records;
		l.words = (records + 31) / 32;
		l.used_bitmap = records * record_bytes;
		l.ring = l.used_bitmap + l.words * 4;
		l.ring_write = l.ring + records * 4 + 4;
		l.ring_read = l.ring_write + 4;
		l.ring_count = l.ring_read + 4;
		l.alloc_bitmap = l.ring_count + 4;
		l.record_count = l.alloc_bitmap + l.words * 4;
		l.heap = l.record_count + 4;
		l.heap_count = l.heap + records * 8 + 4;
		l.body_bytes = l.heap_count + 4;
		return l;
	}
	// Retail keeps 0x24 bitmap words for 1125 records (one more than 1125 bits need); the formula above gives 36 too.
	inline constexpr layout retail_layout = layout_for(retail_records);
	static_assert(retail_layout == layout{0x465, 0x24, 0x6978, 0x6a08, 0x7ba0, 0x7ba4, 0x7ba8, 0x7bac, 0x7c3c, 0x7c40, 0x9f6c, 0x9f70});
	static_assert(retail_layout.heap_count - retail_layout.heap == 0x232c && retail_layout.ring_read - retail_layout.ring == 0x119c &&
	              retail_layout.ring_count - retail_layout.ring == 0x11a0);

	// What the running game uses: retail until event_pool::install published a raise. Header-only so that the modules
	// that read the queue (join in progress, the turrets' dispatch hook) need no link dependency.
	inline layout installed_layout = retail_layout;
	inline std::uint32_t installed_object = retail_object;

	// [Limits] TimedEvents: absent -> `fallback` (the raise when the entity capacity is raised, else retail);
	// 0 or 1125 -> retail; otherwise 1125..65536 rounded up to a whole bitmap word. Nothing else is accepted.
	constexpr bool parse_capacity(const std::string_view text, const std::uint32_t fallback, std::uint32_t& out)
	{
		if (text.empty()) { out = fallback; return true; }
		std::uint32_t v = 0;
		for (const char c : text)
		{
			if (c < '0' || c > '9') return false;
			v = v * 10 + std::uint32_t(c - '0');
			if (v > max_records) return false;
		}
		if (v == 0 || v == retail_records) { out = retail_records; return true; }
		if (v < min_records) return false;
		out = (v + 31) / 32 * 32;
		return true;
	}

	constexpr std::uint32_t operand_value(const event_pool_sites::operand what, const layout& l, const std::uint32_t object)
	{
		using o = event_pool_sites::operand;
		switch (what)
		{
		case o::records: return l.records;
		case o::last: return l.records - 1;
		case o::words: return l.words;
		case o::used_bitmap: return l.used_bitmap;
		case o::ring: return l.ring;
		case o::ring_write: return l.ring_write;
		case o::ring_read: return l.ring_read;
		case o::ring_count: return l.ring_count;
		case o::alloc_bitmap: return l.alloc_bitmap;
		case o::record_count: return l.record_count;
		case o::heap: return l.heap;
		case o::heap_count: return l.heap_count;
		case o::heap_rel_count: return l.heap_count - l.heap;
		case o::ring_rel_read: return l.ring_read - l.ring;
		case o::ring_rel_count: return l.ring_count - l.ring;
		case o::object: return object;
		case o::body: return object + body_at;
		}
		return 0;
	}
	constexpr std::uint8_t nibble(const char c) { return std::uint8_t(c <= '9' ? c - '0' : c - 'a' + 10); }
	// Every site's operand holds exactly the retail value of its kind (checked at compile time against the table).
	constexpr bool table_is_retail()
	{
		for (const auto& s : event_pool_sites::sites)
		{
			if (s.hex.size() % 2 || s.at + 4u > s.hex.size() / 2) return false;
			std::uint32_t v = 0;
			for (unsigned i = 0; i < 4; ++i)
				v |= std::uint32_t(nibble(s.hex[(s.at + i) * 2]) << 4 | nibble(s.hex[(s.at + i) * 2 + 1])) << (8 * i);
			if (v != operand_value(s.what, retail_layout, retail_object)) return false;
		}
		return true;
	}
	static_assert(table_is_retail());

	struct edit { std::uint32_t va; std::vector<std::uint8_t> before, after; };
	// The operand edits for `l` at `object`: the whole instruction is the guard, only the 4 operand bytes change.
	inline std::vector<edit> plan(const layout& l, const std::uint32_t object)
	{
		std::vector<edit> out;
		out.reserve(event_pool_sites::sites.size());
		for (const auto& s : event_pool_sites::sites)
		{
			edit e{s.va, {}, {}};
			for (std::size_t i = 0; i + 1 < s.hex.size(); i += 2) e.before.push_back(std::uint8_t(nibble(s.hex[i]) << 4 | nibble(s.hex[i + 1])));
			e.after = e.before;
			const auto v = operand_value(s.what, l, object);
			std::memcpy(e.after.data() + s.at, &v, 4);
			out.push_back(std::move(e));
		}
		return out;
	}

	// ---- the watchdog's reading of one quiescent queue (between two pumps every allocated record is in the heap) ----
	struct counts { std::uint32_t record_count, heap_count, ring_count, used_bits, alloc_bits; };
	enum class verdict : std::uint8_t { ok, counts_disagree, bitmaps_disagree, over_capacity };
	constexpr verdict judge(const counts& c, const layout& l, const bool with_bitmaps)
	{
		if (c.record_count > l.records || c.heap_count > l.records) return verdict::over_capacity;
		if (c.record_count != c.heap_count || c.ring_count != l.records - c.record_count) return verdict::counts_disagree;
		if (with_bitmaps && (c.used_bits != c.record_count || c.alloc_bits != c.record_count)) return verdict::bitmaps_disagree;
		return verdict::ok;
	}
	constexpr const char* name(const verdict v)
	{
		return v == verdict::ok ? "ok" : v == verdict::counts_disagree ? "the record, heap and free-ring counts disagree"
		     : v == verdict::bitmaps_disagree ? "the in-use bitmaps disagree with the record count" : "a count is over the pool's capacity";
	}
	// The queue is "empty while the world runs" when the pump has found it empty for this long (game seconds) since a
	// pump that still had entries, with no reset (zone change) in between.
	constexpr float empty_alarm_seconds = 2.0f;
	// An add is refused (the event is dropped, logged once per zone) only when the pool is full: the alternative is
	// the retail overwrite above.
	constexpr bool add_fits(const std::uint32_t record_count, const std::uint32_t heap_count, const layout& l)
	{
		return record_count < l.records && heap_count < l.records;
	}
}
