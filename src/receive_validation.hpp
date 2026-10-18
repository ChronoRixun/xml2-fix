#pragma once

// [Online] ReceiveValidation (default on): a guarded hook on the stock common event dispatch 0x60dbe0. Every
// network message - from the datagram parser (0x60ddcf), the ordered drain (0x60ffd7) or fragment reassembly
// (0x61027d) - is checked against receive_validation_rules (net030-d3's per-handler table) before its handler
// runs; anything invalid is dropped with a rate-limited log line, exactly as the native dispatch drops an
// opcode with no registration. A peer without this hook interoperates unchanged: nothing on the wire changes.
//
// Installed through the one startup transaction (a 7-byte guarded JMP at the dispatch entry, resume 0x60dbe7);
// a guard mismatch or any conflict leaves stock bytes and one log line. The existing fragment guard
// (gameplay_agreement, 0x610200) stays as the second line for the orphan-0x13 crash; messages that reach a
// handler at all have already passed the table.

#include <Windows.h>

#include <cstdint>

namespace patch_transaction { class transaction; }

namespace receive_validation
{
	// Reads [Online] ReceiveValidation (default on) and joins the startup transaction when on. A mismatch with
	// the retail dispatch entry skips the contribution instead of refusing the transaction (optional part).
	void contribute(patch_transaction::transaction& transaction, HMODULE game);
	bool installed();
	// Drops since startup (also the rate-limited log's source), for the status line and the native test.
	std::uint32_t dropped();
}
