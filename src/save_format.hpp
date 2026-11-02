#pragma once

// [Save] Format=lc: the LC save format's engine side - the disk seam (the card's file methods), the bind tables,
// the one-time import of old saves, the missing-talent insertion, the placement-file hash and the stream flag
// (save_format_rules.hpp has the decisions and every retail byte).

#include <Windows.h>
#include "save_format_rules.hpp"

namespace patch_transaction { class transaction; }

#include <string>
#include <cstdint>

namespace save_format
{
	// Reads [Save] Format and, for lc, replaces the card's file methods and patches the stream helpers; every
	// site byte-guarded, nothing patched when one differs. The tables and the import run at the first file
	// operation (the game's thread).
	void install(HMODULE game);
	// The same seam as a contribution to the one startup transaction (patch_transaction): guards, writes and the
	// publication; false when LC is off or the guards/stubs refused (installation_failure says why). The native
	// profile (retail, or the expanded E10A2 layout of the entity-capacity install) must be chosen before.
	bool contribute(patch_transaction::transaction& transaction, HMODULE game);
	// Published only after the zone-string transaction commits. Game thread only.
	void publish_zone_strings(std::uint32_t address);
	void use_native_profile(const save_format_rules::native_profile& chosen);
	void enable_actor_capacity96();
	// [Limits] Actors=96 was requested but did not install this start: a 0x4 save's refusal names why.
	void actor_capacity96_refused(const std::string& why);
	const save_format_rules::native_profile& installed_profile();

	// A requested LC installation must finish before the executable may start.
	// Runtime table/file refusals remain protected by the already-installed card hooks.
	bool startup_allowed();
	// [Save] Format=lc published: among it the native campaign-load hook that checks an incoming (online) campaign
	// image before the game's loader reads it (online rooms refuse a joiner without it).
	bool campaign_load_checked();
	const std::string& startup_error();

	// A required reconstruction step failed (a dynamic entity factory returned null, the World or allocator refused
	// a registration): the pending load is not confirmed and campaign writes stay blocked (plan section 6).
	void latch_reconstruction_failure(const char* why);

	// Game thread only, after a proven successful native load (not just translation into the card buffer).
	// Until that confirmation, a prior refused path stays protected; menu Delete also clears its latch.
	void confirm_load_success(std::uint8_t kind, int slot);

	// One line for the pipe's status.
	std::string status();

	// The pipe's `save ...` commands (test_input): "save status", "save dump <file.lcsave>" (the live game as an
	// LC file, written on the game's thread), "save check" (the live game against the file last loaded).
	std::string pipe(const std::string& arguments);

	// From the game's thread (the keyboard read): runs a pending pipe job.
	void pump();
	// The live campaign through the game's own serializer into a wrapper of installed_profile().wrapper bytes laid out
	// as CSaveLoad's (the online campaign image, [Online] JoinInProgress). Game thread; false: faulted or overflowed.
	bool serialize_wrapper(std::uint8_t* base);
}
