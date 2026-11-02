#pragma once

// The objective-side view of the installed native save layout (docs/limits-wave2.md 3.1 / 4.5, ledger W2-L-10..12):
// what objective_text, ForcedTeams and the test-state reader need from the loaded-objective pool, the global objective
// state array and the side-mission stack. They read this view instead of holding literals, so a layout raise is one
// publication, never a module that still reads the retail offsets beside the raised ones (re-review finding 2).
//
// RETAIL / E10A2 share one view (the E10A2 layout changes no objective structure). The W2 install (ledger rows
// W2-E14-03..08, a later step) publishes the W2 view from its transaction's commit callback: the relocated state array
// and side-mission block, 127 records per act, and the wide-index side table keyed by pool slot. Until it does, the
// published view is the retail one and every reader behaves exactly as before.

#include <cstdint>

namespace save_layout_view
{
	struct objectives_view
	{
		const char* layout;                  // "RETAIL" (also E10A2) / "W2"
		std::uint32_t snapshot_size;         // one side-mission frame (and the manager's snapshot): 0x26c / 0x540
		std::uint32_t side_count_offset;     // the stack's count after two frames and a pad: 0x4dc / 0xa84
		std::uint32_t side_names_offset;     // the four party names in a frame: 0xa4 (unchanged)
		std::uint32_t objectives;            // global objective indices / state bytes: 300 / 1024
		std::uint32_t objectives_per_act;    // loaded-objective records: 75 / 127
		std::uint32_t record_size;           // a loaded objective record: 0x1ac (unchanged)
		std::uint32_t index_offset;          // the record's byte index (W2: its low 8 bits; the wide index is the side table's)
		std::uint32_t state_bytes;           // the global state array: 0x72b118 (W2: the install's DLL array)
		const std::uint16_t* wide_index;     // W2: wide_index[pool slot], 0xffff = free; nullptr: the byte at index_offset
		std::uint32_t pool_offset;           // the records' offset in CMissionMgr (+8)
		std::uint32_t pool_address = 0;      // W2: the relocated pool's absolute address (0: CMissionMgr + pool_offset)
		std::uint32_t count_offset = 0x7d78; // live count, relative to CMissionMgr (not the pool)
	};

	inline constexpr objectives_view retail_objectives{"RETAIL", 0x26c, 0x4dc, 0xa4, 300, 75, 0x1ac, 0x1a9, 0x72b118, nullptr, 8};
	// the W2 shape (the addresses are the install's: state_bytes, wide_index and pool_offset are filled at its commit)
	inline constexpr objectives_view w2_objectives_shape{"W2", 0x540, 0xa84, 0xa4, 1024, 127, 0x1ac, 0x1a9, 0, nullptr, 8, 0, 8 + 8 + 127 * 0x1ac + 16};
	static_assert(2 * retail_objectives.snapshot_size + 4 == retail_objectives.side_count_offset &&
	              2 * w2_objectives_shape.snapshot_size + 4 == w2_objectives_shape.side_count_offset);

	namespace detail
	{
		inline const objectives_view* published = &retail_objectives;
	}
	inline const objectives_view& objectives() { return *detail::published; }
	// the W2 install's publication callback (the view must outlive the process: static storage)
	inline void publish(const objectives_view& v) { detail::published = &v; }

	// the global objective index of a loaded objective record: the byte at index_offset, or (W2) the side table's entry
	// for the record's pool slot ((record - pool) / record_size); 0xffff when the slot is outside the pool or free
	inline std::uint32_t objective_index(const objectives_view& v, const std::uint8_t* record, const std::uint8_t* pool)
	{
		if (!v.wide_index) return record[v.index_offset];
		if (record < pool) return 0xffff;
		const auto offset = static_cast<std::uint32_t>(record - pool);
		if (offset % v.record_size || offset / v.record_size >= v.objectives_per_act) return 0xffff;
		return v.wide_index[offset / v.record_size];
	}
}
