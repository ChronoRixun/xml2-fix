#pragma once

// [Save] Format=lc: the LC save format's engine side - the disk seam (the card's file methods), the bind tables,
// the one-time import of old saves, the missing-talent insertion, the placement-file hash and the stream flag
// (save_format_rules.hpp has the decisions and every retail byte).

#include <Windows.h>

#include <string>
#include <cstdint>

namespace save_format
{
	// Reads [Save] Format and, for lc, replaces the card's file methods and patches the stream helpers; every
	// site byte-guarded, nothing patched when one differs. The tables and the import run at the first file
	// operation (the game's thread).
	void install(HMODULE game);

	// A requested LC installation must finish before the executable may start.
	// Runtime table/file refusals remain protected by the already-installed card hooks.
	bool startup_allowed();
	const std::string& startup_error();

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
}
