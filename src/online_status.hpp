#pragma once

// online-status.json next to the game's exe (the launcher <-> XML2 Fix contract v1, docs/online-status-file.md):
// written atomically (temp file + rename) on every state change while [Online] Host or JoinCode is set.

#include "online_status_core.hpp"

#include <string>
#include <vector>
#include <Windows.h>

namespace online_status
{
	// Starts the file for this run (role fixed from here on). Nothing is written before begin().
	void begin(online_status_core::role who);
	bool active();
	// A state change: written at once when anything differs from the last write.
	void set(online_status_core::state now, const std::string& code, int players, online_status_core::error why = online_status_core::error::none,
	         const std::string& message = std::string());
	void error(online_status_core::error why, const std::string& message);
	void players(int count);
	// The room's browser listing (contract v1.3): public or not, and its name; written when it changes.
	void room(bool public_room, const std::string& name);
	online_status_core::state current();
	online_status_core::error last_error();
	// Process exit: `ended` (keeps the code and an error state's id out - the session is over). Loader-lock safe:
	// Win32 file calls only, skipped when another thread holds the writer's lock.
	void end();
	// One-shot launch keys (contract v1.1): removes [Online] Host, JoinCode, Public and RoomName from `ini_path` key by key with
	// WritePrivateProfileStringW (the launcher's writer), leaving every other line as it was. True when all are gone.
	bool remove_launch_keys(const std::wstring& ini_path);
	// Contract v1.6 proposal (docs/online-party.md section 9), additive: a new in-game room operation (Host / Join /
	// Retry) starts a party attempt (generation + 1, attempt state connecting); `party` mirrors this PC's phase, the last
	// host-table generation and the roster (no ping, no address), coalesced to one write per second; `tick` flushes a
	// coalesced change. v1's keys keep their meaning, including the process-final top-level error.
	void attempt();
	void party(const std::string& phase, int table, const std::vector<online_status_core::roster_row>& roster);
	void tick();
	online_status_core::status snapshot();
	// Host-test seams only.
	void set_clock_for_test(DWORD (WINAPI *clock)());
	void restart_for_test();
	void path_for_test(const std::wstring& path);
	// The game's xml2-fix.ini (next to the DLL).
	std::wstring game_ini();
	// The writer itself, for the unit test: atomically replaces `path` with `text`.
	bool write_atomic(const std::wstring& path, const std::string& text);
}
