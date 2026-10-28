#pragma once

// CSmartEntMgr replaced by a DLL object at the W2 layout (ledger row W2-E15-13; rules and the retail behaviour in
// smart_entities_rules.hpp). The getter 0x4b76f0 jumps to `getter()`, which returns the DLL object; its own eight-slot
// vtable implements the whole native method family on 2048 x 0x20 records. The retail static object at 0x767390 is
// never created once the getter is replaced (its exit handler 0x67dd50 is registered only by the retail getter).

#include "patch_transaction.hpp"
#include "smart_entities_rules.hpp"

#include <cstdint>
#include <string>

namespace smart_entities
{
	// Adds the getter guard, the getter-caller and exit-handler guards, the owner-region hash and the getter jump to
	// `c`. False (with `why`) when the retail bytes or the region hash are not in memory, or when the retail object
	// has already been created (init flag 0x76bbfc bit 0: the DLL object would start empty while records are live).
	bool contribute_w2(patch_transaction::contribution& c, std::string& why);

	// The replacement getter (native ABI: no arguments, object in EAX) and what it returns.
	void* __cdecl getter();
	void* object();
	const void* vtable();

	// Refusals at capacity (allocate returns 0 like retail at 768; append-copy, unbounded in retail, refuses).
	struct counters { std::uint32_t allocate_full, append_full; };
	counters stats();
}
