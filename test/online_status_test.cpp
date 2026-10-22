// online-status.json (launcher <-> XML2 Fix contract v1): the JSON text for every state and error id, escaping, the
// broker error mapping, the version token, and the atomic writer (temp file + rename, replaces an existing file).
#include "online_status.hpp"
#include "online_status_core.hpp"
#include "online_room_core.hpp"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>

namespace
{
	int passed = 0, failed = 0;
	void check(bool ok, const std::string& what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
		(ok ? passed : failed)++;
	}
	std::string slurp(const std::wstring& path)
	{
		std::ifstream in(path, std::ios::binary);
		std::ostringstream s;
		s << in.rdbuf();
		return s.str();
	}
}

int main()
{
	namespace core = online_status_core;
	const core::utc when{2026, 10, 18, 23, 5, 9};
	check(core::iso8601(when) == "2026-10-18T23:05:09Z", "updated is UTC ISO-8601 with a Z");

	// Every state, as the contract spells it.
	const struct { core::state s; const char* text; } states[] = {
		{core::state::connecting, "connecting"}, {core::state::room, "room"}, {core::state::joined, "joined"},
		{core::state::in_game, "in_game"}, {core::state::ended, "ended"}, {core::state::error, "error"}};
	for (const auto& row : states)
	{
		core::status s;
		s.now = row.s;
		const auto text = core::json(s, when);
		check(text.find(std::string("\"state\":\"") + row.text + "\"") != std::string::npos, std::string("state ") + row.text + " is written by name");
	}

	// Host with a room: exact bytes, key order v, role, state, code, players, error, message, updated.
	{
		core::status s;
		s.who = core::role::host;
		s.now = core::state::room;
		s.code = "K7QN2X9M";
		s.players = 1;
		check(core::json(s, when) == "{\"v\":1,\"role\":\"host\",\"state\":\"room\",\"code\":\"K7QN2X9M\",\"players\":1,\"max_players\":4,\"public\":false,\"room_name\":null,\"error\":null,\"message\":null,\"updated\":\"2026-10-18T23:05:09Z\"}\n",
		      "host room: exact JSON (code set, error and message null)");
	}
	// A public room (contract v1.3): "public":true and its browser name, escaped like every string.
	{
		core::status s;
		s.now = core::state::room;
		s.code = "K7QN2X9M";
		s.players = 1;
		s.public_room = true;
		s.room_name = "Ann's \"co-op\"";
		check(core::json(s, when) == "{\"v\":1,\"role\":\"host\",\"state\":\"room\",\"code\":\"K7QN2X9M\",\"players\":1,\"max_players\":4,\"public\":true,\"room_name\":\"Ann's \\\"co-op\\\"\",\"error\":null,\"message\":null,\"updated\":\"2026-10-18T23:05:09Z\"}\n",
		      "public room: exact JSON (public true, room_name set and escaped)");
	}
	// Joiner connecting without a code yet: code null.
	{
		core::status s;
		s.who = core::role::join;
		check(core::json(s, when) == "{\"v\":1,\"role\":\"join\",\"state\":\"connecting\",\"code\":null,\"players\":0,\"max_players\":4,\"public\":false,\"room_name\":null,\"error\":null,\"message\":null,\"updated\":\"2026-10-18T23:05:09Z\"}\n",
		      "joiner connecting: exact JSON (code null)");
	}

	// Every error id, as the contract spells it; an error state always carries one.
	const struct { core::error e; const char* text; } errors[] = {
		{core::error::broker_unreachable, "broker_unreachable"}, {core::error::broker_rejected, "broker_rejected"},
		{core::error::code_not_found, "code_not_found"}, {core::error::code_expired, "code_expired"}, {core::error::room_full, "room_full"},
		{core::error::room_closed, "room_closed"},
		{core::error::version_mismatch, "version_mismatch"}, {core::error::test_mode_on, "test_mode_on"},
		{core::error::tls_pin_mismatch, "tls_pin_mismatch"}, {core::error::internal, "internal"}};
	for (const auto& row : errors)
	{
		core::status s;
		s.now = core::state::error;
		s.why = row.e;
		s.message = "why";
		const auto text = core::json(s, when);
		check(text.find(std::string("\"error\":\"") + row.text + "\",\"message\":\"why\"") != std::string::npos, std::string("error id ") + row.text);
	}
	{
		core::status s;
		s.now = core::state::error;
		check(core::json(s, when).find("\"error\":\"internal\"") != std::string::npos, "an error state without an id says internal");
		s.now = core::state::in_game;
		s.why = core::error::room_full;
		check(core::json(s, when).find("\"error\":null") != std::string::npos, "a non-error state never names an error");
		s.now = core::state::ended;
		check(core::json(s, when).find("\"state\":\"ended\",\"code\":null,\"players\":0,\"max_players\":4,\"public\":false,\"room_name\":null,\"error\":\"room_full\"") != std::string::npos,
		      "ended after an error keeps the error id");
	}

	// Escaping: the message is English text for logs, but broker text can carry anything.
	check(core::escaped("a\"b\\c\nd\te\x01\x7f\xc3\xa9") == "a\\\"b\\\\c\\nd\\te\\u0001\\u007f\xc3\xa9", "JSON escaping: quote, backslash, controls; UTF-8 kept");

	// Broker error codes -> contract ids.
	check(core::from_broker("no_such_room") == core::error::code_not_found, "broker no_such_room -> code_not_found");
	check(core::from_broker("room_full") == core::error::room_full, "broker room_full -> room_full");
	check(core::from_broker("room_not_joinable") == core::error::room_closed, "broker room_not_joinable -> room_closed (contract v1.5), not room_full");
	check(core::from_broker("room_closed") == core::error::code_expired, "broker room_closed -> code_expired");
	check(core::from_broker("unsupported_version") == core::error::broker_rejected && core::from_broker("auth_failed") == core::error::broker_rejected, "broker version/auth refusal -> broker_rejected");
	check(core::from_broker("rate_limited") == core::error::internal, "other broker errors -> internal");

	// Version token: game version + 16 fingerprint digits, within the broker's 32 characters.
	check(core::version_token("LC33") == "LC33", "version token: the GameVersion exactly (the launcher compares it with its ini's GameVersion)");
	check(core::version_token("").empty() && core::version_token(std::string(40, 'A')).size() == 32, "version token: empty stays empty, long versions stay within 32");

	// resolve_ok carries the room's summary as a nested object: its version token is read for version_mismatch.
	{
		const auto msg = online_room_core::parse(R"({"op":"resolve_ok","code":"K7QN2X9M","host":"legends:0123456789abcdef","members":["legends:0123456789abcdef"],"room":{"code":"K7QN2X9M","gen":1,"name":"","version_token":"LC30-1b87836ed197fe8b","players":1,"public":false},"expires":5})");
		const auto room = online_room_core::parse(msg["room"].text);
		check(msg.valid && msg.text("code") == "K7QN2X9M" && msg["members"].items.size() == 1, "resolve_ok with a nested room object still parses");
		check(room.valid && room.text("version_token") == "LC30-1b87836ed197fe8b", "the nested room object's version token is readable");
	}

	// The atomic writer: a fresh file, then a replacement; no temp file left behind.
	{
		wchar_t dir[MAX_PATH]{};
		GetTempPathW(MAX_PATH, dir);
		std::wstring path = std::wstring(dir) + L"xml2-online-status-test-" + std::to_wstring(GetCurrentProcessId()) + L".json";
		DeleteFileW(path.c_str());
		check(online_status::write_atomic(path, "{\"v\":1}\n") && slurp(path) == "{\"v\":1}\n", "atomic writer: creates the file");
		check(online_status::write_atomic(path, "{\"v\":1,\"state\":\"room\"}\n") && slurp(path) == "{\"v\":1,\"state\":\"room\"}\n", "atomic writer: replaces an existing file");
		check(GetFileAttributesW((path + L".tmp").c_str()) == INVALID_FILE_ATTRIBUTES, "atomic writer: no temp file remains");
		// A reader holding the file open (shared read, as a launcher polling it would) does not make the write fail.
		const HANDLE reader = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
		const bool replaced = online_status::write_atomic(path, "{\"v\":1,\"state\":\"joined\"}\n");
		if (reader != INVALID_HANDLE_VALUE) CloseHandle(reader);
		check(replaced && slurp(path) == "{\"v\":1,\"state\":\"joined\"}\n", "atomic writer: works while a reader holds the file with delete sharing");
		DeleteFileW(path.c_str());
		check(!online_status::write_atomic(std::wstring(dir) + L"no-such-folder-xml2\\x.json", "x"), "atomic writer: an unwritable path reports failure");
	}

	// One-shot launch keys (contract v1.1): Host and JoinCode removed key by key, every other line byte-identical.
	{
		wchar_t dir[MAX_PATH]{};
		GetTempPathW(MAX_PATH, dir);
		const std::wstring ini = std::wstring(dir) + L"xml2-online-keys-test-" + std::to_wstring(GetCurrentProcessId()) + L".ini";
		const std::string before = "[Display]\r\nMode = windowed\r\n\r\n[Online]\r\nGameVersion = LC30\r\nHost=broker\r\nBroker = broker.example.org:10020 ; note\r\n"
		                           "JoinCode=K7QN2X9M\r\nBrokerPin = " + std::string(64, 'a') + "\r\n\r\n[Game]\r\nSaveFolder = X-Men Legends (test)\r\n";
		const std::string expected = "[Display]\r\nMode = windowed\r\n\r\n[Online]\r\nGameVersion = LC30\r\nBroker = broker.example.org:10020 ; note\r\n"
		                             "BrokerPin = " + std::string(64, 'a') + "\r\n\r\n[Game]\r\nSaveFolder = X-Men Legends (test)\r\n";
		{ std::ofstream out(ini, std::ios::binary); out << before; }
		check(online_status::remove_launch_keys(ini), "one-shot keys: Host, JoinCode, Public and RoomName removed");
		const auto after = slurp(ini);
		check(after == expected, "one-shot keys: every other line byte-identical");
		check(online_status::remove_launch_keys(ini) && slurp(ini) == expected, "one-shot keys: removing absent keys changes nothing");
		DeleteFileW(ini.c_str());
	}

	// The stateful writer without begin(): nothing is written (OFF writes no file).
	check(!online_status::active(), "no status file before begin() (Host/JoinCode absent)");
	online_status::set(core::state::room, "K7QN2X9M", 1);
	check(!online_status::active(), "set() before begin() stays inactive");

	// The stateful writer (writes online-status.json next to this test exe, removed afterwards): the first error is final.
	{
		online_status::begin(core::role::join);
		online_status::set(core::state::connecting, "K7QN2X9M", 0);
		online_status::error(core::error::version_mismatch, "another build");
		online_status::error(core::error::code_expired, "the room closed");
		online_status::set(core::state::joined, "K7QN2X9M", 2);
		check(online_status::current() == core::state::error && online_status::last_error() == core::error::version_mismatch,
		      "the first error stays: a later room_closed / state change does not replace it");
		online_status::end();
		check(online_status::current() == core::state::ended && online_status::last_error() == core::error::version_mismatch, "exit writes ended and keeps the error id");
		const auto ini = online_status::game_ini();
		const auto file = ini.substr(0, ini.size() - std::wstring(L"xml2-fix.ini").size()) + L"online-status.json";
		// A host's listing: written into the file at once, and kept by later state changes and the exit.
		online_status::begin(core::role::host);
		online_status::set(core::state::room, "K7QN2X9M", 1);
		online_status::room(true, "Friday night");
		const auto listed = slurp(file);
		online_status::players(2);
		const auto two = slurp(file);
		online_status::end();
		const auto ended = slurp(file);
		check(listed.find("\"state\":\"room\"") != std::string::npos && listed.find("\"public\":true,\"room_name\":\"Friday night\"") != std::string::npos,
		      "status file: the room's public flag and name are written when they are known");
		check(two.find("\"players\":2") != std::string::npos && two.find("\"public\":true,\"room_name\":\"Friday night\"") != std::string::npos &&
		      ended.find("\"state\":\"ended\"") != std::string::npos && ended.find("\"public\":true") != std::string::npos,
		      "status file: later changes and the exit keep public and room_name");
		online_status::begin(core::role::host);
		online_status::set(core::state::room, "K7QN2X9M", 1);
		check(slurp(file).find("\"public\":false,\"room_name\":null") != std::string::npos, "status file: a private room says public false, room_name null");
		DeleteFileW((ini.substr(0, ini.size() - std::wstring(L"xml2-fix.ini").size()) + L"online-status.json").c_str());
	}

	std::printf("%d PASS / %d FAIL\n", passed, failed);
	return failed ? 1 : 0;
}
