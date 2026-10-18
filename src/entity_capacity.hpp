#pragma once

// [Limits] EntityCapacity=1: the Phase A entity-capacity raise (docs/entity-capacity-phase-a-plan.md) - actor 32 /
// generic 768 / World 1024 handles with contiguous ten-bit indices - installed as one guarded transaction:
// prepared owners (allocator backing, World, Grid, query manager, native save manager), every literal operand
// edit and relocation of the reviewed ledger, the save adapters, the GCAP layout entry, telemetry and the
// test-state descriptor. Off (the default) leaves the engine at its stock capacities with the retail layout.

#include <Windows.h>

#include <cstdint>
#include <string>

namespace patch_transaction { class transaction; }
namespace entity_capacity_layout { struct descriptor; }

namespace entity_capacity
{
	// Reads the INI, decides, and prepares every owner object and backing before anything is contributed (the
	// save seam needs the chosen native profile first). Nothing is written to the game.
	void prepare(patch_transaction::transaction& transaction, HMODULE game);
	// True when the raise is requested and prepared: the actor-arena teardown at 0x4613e0 is then this module's
	// (AllocatorShutdown), which calls limits::retire_move_trigger_times() after the native teardown.
	bool owns_arena_teardown();
	// Adds the ledger's guards, region hashes and writes bound to this DLL's symbols.
	void contribute(patch_transaction::transaction& transaction, HMODULE game);
	// After the one commit: publishes the descriptor, registers the GCAP layout entry (verified on success,
	// unverified when requested and failed) and latches startup refusal when a rollback could not be certified.
	void after_commit(bool committed, bool rollback_failed);
	bool startup_allowed();
	const std::string& startup_error();
	// When the raise was requested but fell back to stock without refusing startup (a capacity-only mismatch, or a
	// startup transaction that refused cleanly), the player's one notice about it: what failed and what that means.
	// Empty otherwise (not requested, installed, or startup refused - that has its own notice).
	std::string fallback_notice();

	// The installed layout for every consumer (test state, observers, caches). Stock until published.
	const entity_capacity_layout::descriptor& installed();
	// One line for the pipe's status: capacities, live/free/retired counts, max generation, refusals, reset-time
	// allocation attempts.
	std::string status();
	// Test-pipe fixture (plan P3.4): force-retire a free World slot so a peer compares against a non-identical ring.
	bool retire_slot(std::uint32_t index);
	// The DLL address the installed ledger's rel32 relocation at `site` transfers to (its bound replacement), or 0
	// when the raise is not installed or `site` is not a relocation the install wrote itself.
	std::uint32_t installed_target(std::uint32_t site);
	// [Online] FastTransfer published with the capacity install: received 'H' bodies are then the agreed
	// data/zero-run records, not the stock fixed 200-byte chunks (receive_validation reads this).
	bool fast_transfer_installed();
}
