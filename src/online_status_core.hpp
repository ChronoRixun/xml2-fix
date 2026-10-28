#pragma once

// online-status.json (the launcher <-> XML2 Fix contract v1, docs/online-status-file.md): the pure part - the state
// and error names, the JSON text and the UTC time stamp. No Windows calls, so the unit test checks every byte.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace online_status_core
{
	enum class role { host, join };
	enum class state { connecting, room, joined, in_game, ended, error };
	enum class error
	{
		none,
		broker_unreachable,
		broker_rejected,
		code_not_found,
		code_expired,
		room_full,
		room_closed, // contract v1.5
		version_mismatch,
		test_mode_on,
		tls_pin_mismatch,
		internal,
	};

	inline const char* name(role r) { return r == role::host ? "host" : "join"; }
	inline const char* name(state s)
	{
		switch (s)
		{
		case state::connecting: return "connecting";
		case state::room: return "room";
		case state::joined: return "joined";
		case state::in_game: return "in_game";
		case state::ended: return "ended";
		case state::error: return "error";
		}
		return "error";
	}
	inline const char* name(error e)
	{
		switch (e)
		{
		case error::none: return nullptr;
		case error::broker_unreachable: return "broker_unreachable";
		case error::broker_rejected: return "broker_rejected";
		case error::code_not_found: return "code_not_found";
		case error::code_expired: return "code_expired";
		case error::room_full: return "room_full";
		case error::room_closed: return "room_closed";
		case error::version_mismatch: return "version_mismatch";
		case error::test_mode_on: return "test_mode_on";
		case error::tls_pin_mismatch: return "tls_pin_mismatch";
		case error::internal: return "internal";
		}
		return "internal";
	}

	// A broker `error` reply's code (SPEC.md) -> the contract's id. Codes the contract has no id for are `internal`
	// for a host and `code_not_found` never (only an explicit no_such_room means the code is wrong).
	inline error from_broker(const std::string& code)
	{
		if (code == "no_such_room") return error::code_not_found;
		if (code == "room_closed") return error::code_expired;
		if (code == "room_full") return error::room_full;
		// contract v1.5: the room exists but takes no players right now (drop-in off, a level load, the host closing)
		if (code == "room_not_joinable") return error::room_closed;
		if (code == "unsupported_version" || code == "bad_auth" || code == "auth_failed") return error::broker_rejected;
		return error::internal;
	}

	// JSON string body: quotes, backslash and control characters escaped; bytes >= 0x80 are copied (UTF-8 in, UTF-8 out).
	inline std::string escaped(const std::string& text)
	{
		std::string out;
		out.reserve(text.size() + 2);
		for (const char ch : text)
		{
			const auto c = static_cast<unsigned char>(ch);
			if (c == '"') out += "\\\"";
			else if (c == '\\') out += "\\\\";
			else if (c == '\n') out += "\\n";
			else if (c == '\r') out += "\\r";
			else if (c == '\t') out += "\\t";
			else if (c < 0x20 || c == 0x7f)
			{
				char u[8];
				std::snprintf(u, sizeof(u), "\\u%04x", unsigned(c));
				out += u;
			}
			else out += ch;
		}
		return out;
	}

	struct utc
	{
		int year = 1970, month = 1, day = 1, hour = 0, minute = 0, second = 0;
	};
	inline std::string iso8601(const utc& t)
	{
		char text[32];
		std::snprintf(text, sizeof(text), "%04d-%02d-%02dT%02d:%02d:%02dZ", t.year, t.month, t.day, t.hour, t.minute, t.second);
		return text;
	}

	// Contract v1.6 proposal (docs/online-party.md section 9): additive keys after `updated`; `v` stays 1.
	struct roster_row
	{
		int slot = 1;
		std::string name; // <= 31 bytes, escaped
		bool host = false, you = false;
		std::string phase; // online_party_core::name(phase)
		int ready = -1;    // -1 null (unknown / pending), 0 false, 1 true
		std::string build; // checking | other | same | agreed
		std::string route; // direct | relay | empty -> null
		bool operator==(const roster_row&) const = default;
	};
	struct party_block
	{
		bool known = false;
		std::uint32_t generation = 0;
		state now = state::connecting; // this attempt's state (v1's top-level `state` stays process-final)
		error why = error::none;
		std::string phase; // this PC's phase name; empty -> null
		int table = -1;    // last accepted host-table generation; -1 -> null
		std::vector<roster_row> roster;
		bool operator==(const party_block&) const = default;
	};
	struct process_info
	{
		std::uint32_t pid = 0; // 0 -> no `process` key (as every v1 writer)
		utc started;
	};

	struct status
	{
		role who = role::host;
		state now = state::connecting;
		std::string code;    // empty -> null
		int players = 0;     // everyone in the room, the host included (host alone = 1)
		int max_players = 4; // the game's limit (contract v1.1)
		bool public_room = false; // listed in the public game browser (contract v1.3)
		std::string room_name;    // its name in the browser; empty -> null
		error why = error::none;
		std::string message; // empty -> null
		party_block party;
		process_info process;
	};

	// One object, keys in the contract's order, then a newline. `error` is non-null in the `error` state (an error
	// state without an id says `internal`) and in an `ended` state that followed an error (the run's last word).
	inline std::string json(const status& s, const utc& when)
	{
		const char* error_id = s.now == state::error ? (s.why == error::none ? "internal" : name(s.why)) : s.now == state::ended ? name(s.why) : nullptr;
		std::string out = "{\"v\":1,\"role\":\"";
		out += name(s.who);
		out += "\",\"state\":\"";
		out += name(s.now);
		out += "\",\"code\":";
		out += s.code.empty() ? std::string("null") : "\"" + escaped(s.code) + "\"";
		out += ",\"players\":" + std::to_string(s.players < 0 ? 0 : s.players);
		out += ",\"max_players\":" + std::to_string(s.max_players);
		out += std::string(",\"public\":") + (s.public_room ? "true" : "false");
		out += ",\"room_name\":";
		out += s.room_name.empty() ? std::string("null") : "\"" + escaped(s.room_name) + "\"";
		out += ",\"error\":";
		out += error_id ? "\"" + std::string(error_id) + "\"" : std::string("null");
		out += ",\"message\":";
		out += s.message.empty() ? std::string("null") : "\"" + escaped(s.message) + "\"";
		out += ",\"updated\":\"" + iso8601(when) + "\"";
		if (s.party.known)
		{
			const auto& p = s.party;
			const char* attempt_error = p.now == state::error ? (p.why == error::none ? "internal" : name(p.why)) : nullptr;
			out += ",\"party\":{\"generation\":" + std::to_string(p.generation) + ",\"state\":\"" + name(p.now) + "\",\"error\":";
			out += attempt_error ? "\"" + std::string(attempt_error) + "\"" : std::string("null");
			out += ",\"phase\":";
			out += p.phase.empty() ? std::string("null") : "\"" + escaped(p.phase) + "\"";
			out += ",\"table\":" + (p.table < 0 ? std::string("null") : std::to_string(p.table));
			out += ",\"roster\":[";
			for (std::size_t i = 0; i < p.roster.size() && i < 4; ++i)
			{
				const auto& r = p.roster[i];
				if (i) out += ",";
				out += "{\"slot\":" + std::to_string(r.slot) + ",\"name\":\"" + escaped(r.name.substr(0, 31)) + "\"";
				out += std::string(",\"host\":") + (r.host ? "true" : "false") + ",\"you\":" + (r.you ? "true" : "false");
				out += ",\"phase\":\"" + escaped(r.phase) + "\",\"ready\":" + (r.ready < 0 ? "null" : r.ready ? "true" : "false");
				out += ",\"build\":\"" + escaped(r.build) + "\",\"route\":";
				out += r.route.empty() ? std::string("null") : "\"" + escaped(r.route) + "\"";
				out += "}";
			}
			out += "]}";
		}
		if (s.process.pid)
			out += ",\"process\":{\"pid\":" + std::to_string(s.process.pid) + ",\"started\":\"" + iso8601(s.process.started) + "\"}";
		out += "}\n";
		return out;
	}

	// The token a room carries so a joiner of another build is refused before it connects: the build's [Online]
	// GameVersion ("LC33"), exactly as the launcher's browser compares it with its own ini (contract v1.3: the
	// GameVersion token; an earlier "LC30-<16 hex of the content fingerprint>" made every listed room read
	// "Different game version" in the launcher). At most 32 characters (broker limit).
	inline std::string version_token(const std::string& game_version)
	{
		return game_version.substr(0, 32);
	}
}
