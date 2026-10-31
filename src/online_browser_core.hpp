#pragma once

// The public game browser (launcher <-> XML2 Fix contract v1.3/v1.4, docs/online-rooms.md): the game-independent half
// of what a host's room tells the broker - [Online] Public / RoomName, the room name, the screen name, the zone and
// its display name, game mode, in_progress and joinable - and the pacing of the `presence` updates (on change, at
// most one per 5 s). No Windows, GNS or game calls here, so test/online_browser_test.cpp drives it directly.

#include "online_room_core.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace online_browser_core
{
	using online_room_core::room_metadata;

	constexpr std::size_t room_name_max = 32;     // [Online] RoomName (contract v1.4: printable ASCII, no ; or ")
	constexpr std::size_t display_name_max = 32;  // host_display_name (broker: 32 characters / 128 bytes)
	constexpr std::size_t zone_max = 48;          // zone and zone_name (broker: 48 characters / 192 bytes)
	constexpr std::size_t name_max = 48;          // name (broker: 48 characters / 192 bytes)
	constexpr std::uint32_t presence_interval_ms = 5000;
	constexpr std::size_t list_max = 50;
	constexpr std::uint32_t lookup_interval_ms = 2000, list_refresh_ms = 10000, request_timeout_ms = 10000;
	enum class operation { none, list, host, resolve };
	struct launch_preset { operation action = operation::none; std::string code, name; bool public_room = false; };
	enum class phase { idle, authenticating, listing, hosting, resolving, room, canceling, leaving, retry_wait, error, refused };
	struct ticket {
		std::uint64_t request = 0, room = 0;
		bool operator==(const ticket&) const = default;
	};
	struct public_room {
		std::string code, name, host_name, version, zone, mode;
		int players = -1, max_players = 4;
		bool joinable = true, in_progress = false, password = false;
		bool max_players_known = false; // absence is a display default, not evidence of a free seat
	};
	struct client_view {
		phase state = phase::idle;
		operation action = operation::none;
		ticket generation;
		std::vector<public_room> rows;
		std::string error, code, room_name;
		std::string error_detail; // presentation only: version_mismatch's build token, or trusted room_not_joinable context
		bool stale = false, authenticated = false, list_known = false;
		bool public_room = false, room_metadata_known = false;
	};
	inline bool integer(const online_room_core::value& v, int low, int high, int& out) {
		if (v.t != online_room_core::value::type::number || v.text.empty() ||
			v.text.find_first_not_of("0123456789") != std::string::npos || v.text.size() > 9) return false;
		const auto n = std::strtol(v.text.c_str(), nullptr, 10);
		if (n < low || n > high) return false;
		out = int(n); return true;
	}
	// All-or-nothing parse: never clip an identity, partially publish a list,
	// silently discard duplicate rooms, or invent absent optional metadata.
	inline bool public_rooms(const online_room_core::line& msg, std::vector<public_room>& out) {
		using namespace online_room_core;
		if (!msg.valid || msg.text("op") != "list_rooms_ok") return false;
		const auto& array = msg["rooms"];
		if (array.t == value::type::list && array.items.empty()) { out.clear(); return true; }
		if (array.t != value::type::other || array.text.empty() || array.text.front() != '[') return false;
		std::vector<public_room> next;
		std::set<std::string> codes;
		std::size_t i = 1;
		for (;;) {
			detail::space(array.text, i);
			if (i >= array.text.size()) return false;
			if (array.text[i] == ']') break;
			const auto start = i;
			if (!json_value(array.text, i)) return false;
			const auto row = parse(array.text.substr(start, i - start));
			public_room r;
			r.code = row.text("code");
			if (!row.valid || r.code.size() != 8 || join_code(r.code) != r.code || !codes.insert(r.code).second || next.size() == list_max) return false;
			auto text = [&](const char* key, std::string& dest, std::size_t bound) {
				const auto& v = row[key];
				if (v.t == value::type::missing) return true;
				if (v.t != value::type::string || v.text.size() > bound) return false;
				dest = v.text; return true;
			};
			auto flag = [&](const char* key, bool& dest) {
				const auto& v = row[key];
				if (v.t == value::type::missing) return true;
				if (v.t != value::type::boolean) return false;
				dest = v.text == "true"; return true;
			};
			if (!text("name", r.name, 192) || !text("host_display_name", r.host_name, 128) || !text("version_token", r.version, 32) ||
				!text("zone_name", r.zone, 192) || !text("game_mode", r.mode, 32) || !flag("joinable", r.joinable) ||
				!flag("in_progress", r.in_progress) || !flag("password_protected", r.password)) return false;
			if (row["players"].t != value::type::missing && !integer(row["players"], 0, 4, r.players)) return false;
			if (row["max_players"].t != value::type::missing && !integer(row["max_players"], 1, 4, r.max_players)) return false;
			r.max_players_known = row["max_players"].t != value::type::missing;
			if (r.players > r.max_players) return false;
			next.push_back(std::move(r));
			detail::space(array.text, i);
			if (i < array.text.size() && array.text[i] == ',') { ++i; continue; }
			if (i < array.text.size() && array.text[i] == ']') break;
			return false;
		}
		out = std::move(next); return true;
	}

	// A fresh public list can explain a resolve refusal using existing protocol-2 fields. Unknown/private/missing/full
	// rows cannot explain the host's drop-in policy. This is a candidate only: occupancy can change before presence.
	inline bool started_room_refusal(const std::string& code, const online_room_core::line& msg) {
		std::vector<public_room> rows;
		if (!public_rooms(msg, rows)) return false;
		for (const auto& row : rows) if (row.code == code)
			return row.in_progress && !row.joinable && row.max_players_known && row.players >= 1 && row.players < row.max_players;
		return false;
	}

	// A single outstanding user operation; the runtime closes the old worker
	// connection on cancellation/timeout because protocol v2 has no request IDs.
	// Limiter/backoff survive Back, cancel, reconnect and room re-entry.
	class client_state {
		client_view view_;
		std::uint32_t started_ = 0, sent_ = 0, refreshed_ = 0, failed_ = 0, backoff_ = 0;
		bool sent_once_ = false, listed_once_ = false, issued_ = false;
		std::string refused_code_; // explanatory lists inside the original resolve's timeout/generation
		enum class refusal_phase { first, settling, confirming };
		refusal_phase refusal_phase_ = refusal_phase::first;
		std::uint32_t refusal_seen_ = 0;
	public:
		const client_view& view() const { return view_; }
		bool pending() const { return view_.state == phase::authenticating || view_.state == phase::listing || view_.state == phase::hosting || view_.state == phase::resolving; }
		bool may_lookup(std::uint32_t now) const {
			return (!sent_once_ || std::uint32_t(now - sent_) >= lookup_interval_ms) && (!backoff_ || std::uint32_t(now - failed_) >= backoff_);
		}
		// Milliseconds until the shared limiter/backoff lets a new request start (0: now). The same arithmetic as may_lookup.
		std::uint32_t retry_in(std::uint32_t now) const {
			std::uint32_t wait = 0;
			if (sent_once_ && std::uint32_t(now - sent_) < lookup_interval_ms) wait = lookup_interval_ms - std::uint32_t(now - sent_);
			if (backoff_ && std::uint32_t(now - failed_) < backoff_) { const auto b = backoff_ - std::uint32_t(now - failed_); if (b > wait) wait = b; }
			return wait;
		}
		bool begin(operation op, std::uint32_t now, bool authenticated = false) {
			if (op == operation::none || pending() || view_.state == phase::room || !may_lookup(now)) return false;
			++view_.generation.request;
			if (op != operation::list) { ++view_.generation.room; view_.room_name.clear(); view_.room_metadata_known = false; }
			view_.action = op; view_.error.clear(); view_.error_detail.clear(); view_.code.clear(); issued_ = false; refused_code_.clear();
			view_.authenticated = authenticated;
			view_.state = authenticated ? active_phase() : phase::authenticating;
			started_ = now;
			// Reserve budget before authentication so reconnect spam cannot bypass it.
			sent_ = now; sent_once_ = true;
			return true;
		}
		phase active_phase() const { return view_.action == operation::list ? phase::listing : view_.action == operation::host ? phase::hosting : phase::resolving; }
		bool authenticated(ticket t) {
			if (t != view_.generation || view_.state != phase::authenticating) return false;
			view_.authenticated = true; view_.state = active_phase(); return true;
		}
		bool issue() { if (!pending() || !view_.authenticated || issued_) return false; issued_ = true; return true; }
		bool accepts(ticket t, operation op) const { return t == view_.generation && view_.action == op && view_.state == active_phase() && issued_; }
		bool explaining_refusal() const { return !refused_code_.empty(); }
		bool explain_refusal(ticket t, const std::string& code) {
			if (!accepts(t, operation::resolve) || explaining_refusal() || code.size() != 8 || online_room_core::join_code(code) != code) return false;
			refused_code_ = code; refusal_phase_ = refusal_phase::first; return true;
		}
		bool confirm_refusal(std::uint32_t now) {
			// Broker occupancy changes before the host's paced presence. Allow its 5 s interval plus the 500 ms
			// browser observation cadence (rounded up to 6 s) before asking again; never extend the 10 s attempt.
			if (!explaining_refusal() || expired(now) || refusal_phase_ != refusal_phase::settling || std::uint32_t(now-refusal_seen_) < presence_interval_ms+1000) return false;
			refusal_phase_ = refusal_phase::confirming; return true;
		}
		void refusal_listed(ticket t, const online_room_core::line& msg, std::uint32_t now) {
			if (!accepts(t, operation::resolve) || !explaining_refusal() || refusal_phase_ == refusal_phase::settling) return;
			if (expired(now)) { fail(t, "room_not_joinable", now); return; }
			const bool started = started_room_refusal(refused_code_, msg);
			if (started && refusal_phase_ == refusal_phase::first) {
				refusal_phase_ = refusal_phase::settling; refusal_seen_ = now; return;
			}
			fail(t, "room_not_joinable", now);
			if (started) detail(t, "session_started");
		}
		bool listed(ticket t, const online_room_core::line& msg, std::uint32_t now) {
			if (!accepts(t, operation::list)) return false;
			std::vector<public_room> rows;
			if (!public_rooms(msg, rows)) { fail(t, "malformed_reply", now); return false; }
			view_.rows = std::move(rows); view_.stale = false; view_.list_known = true; view_.state = phase::idle;
			refreshed_ = now; listed_once_ = true; backoff_ = 0; return true;
		}
		bool joined(ticket t, operation op, const std::string& code, const std::string& name = {}) {
			if (!accepts(t, op) || explaining_refusal() || online_room_core::join_code(code) != code || code.size() != 8) return false;
			view_.state = phase::room; view_.code = code; view_.room_name = name; backoff_ = 0; return true;
		}
		void fail(ticket t, const std::string& error, std::uint32_t now) {
			if (t != view_.generation) return;
			if (view_.state == phase::room) { disconnected(error, now); return; }
			// One failure per generation: a duplicate or late report of the same request neither replaces the cause nor grows the backoff.
			if (view_.state == phase::retry_wait || view_.state == phase::error || view_.state == phase::refused) return;
			// The resolve was already refused. Failure of the optional explanation must keep that first cause,
			// including timeout, malformed list, disconnect, queue overflow and service rate limiting.
			view_.error = explaining_refusal() ? "room_not_joinable" : error; refused_code_.clear();
			view_.error_detail.clear(); view_.stale = listed_once_; view_.authenticated = false;
			failed_ = now; backoff_ = backoff_ ? (backoff_ >= 30000 ? 60000 : backoff_ * 2) : 2000;
			if (error == "rate_limited" && backoff_ < 10000) backoff_ = 10000;
			view_.state = phase::retry_wait;
		}
		// Presentation detail for the failure just recorded (ignored for a stale ticket or outside a failure).
		void detail(ticket t, const std::string& text) { if (t == view_.generation && view_.state == phase::retry_wait) view_.error_detail = text; }
		void refuse(const std::string& error) {
			if (view_.state == phase::room) { disconnected(error, 0); return; }
			view_.error = error; view_.error_detail.clear(); refused_code_.clear(); view_.state = phase::refused;
		}
		void metadata(const std::string& name, bool public_room) { view_.room_name = name; view_.public_room = public_room; view_.room_metadata_known = true; }
		void disconnected(const std::string& error, std::uint32_t now) {
			if (view_.state != phase::room) { fail(view_.generation, error, now); return; }
			view_.authenticated = false; view_.error = error; view_.stale = true;
		}
		bool expired(std::uint32_t now) const { return pending() && std::uint32_t(now - started_) >= request_timeout_ms; }
		bool refresh_due(std::uint32_t now) const { return !pending() && view_.state != phase::room && may_lookup(now) && (!listed_once_ || std::uint32_t(now - refreshed_) >= list_refresh_ms); }
		void cancel(bool leave = false) {
			if (view_.state == phase::room && !leave) return; // only intentional Leave releases a committed room
			++view_.generation.request; ++view_.generation.room;
			view_.state = leave ? phase::leaving : phase::canceling;
			view_.authenticated = false; view_.code.clear(); view_.error.clear(); view_.error_detail.clear(); refused_code_.clear(); view_.stale = listed_once_;
			view_.room_name.clear(); view_.room_metadata_known = false;
		}
		void stopped() { if (view_.state == phase::canceling || view_.state == phase::leaving) view_.state = phase::idle; }
	};

	inline std::string trim(const std::string& text)
	{
		std::size_t a = 0, b = text.size();
		while (a < b && (text[a] == ' ' || text[a] == '\t')) ++a;
		while (b > a && (text[b - 1] == ' ' || text[b - 1] == '\t')) --b;
		return text.substr(a, b - a);
	}
	// [Online] RoomName: trimmed; printable ASCII without ; and " (anything else is dropped), at most 32 characters.
	inline std::string room_name(const std::string& configured)
	{
		std::string out;
		for (const char ch : trim(configured))
		{
			const auto c = static_cast<unsigned char>(ch);
			if (c < 0x20 || c > 0x7e || c == ';' || c == '"') continue;
			if (out.size() == room_name_max) break;
			out += ch;
		}
		return trim(out);
	}
	inline bool valid_room_name(const std::string& text) {
		const auto value = trim(text);
		return value.size() <= room_name_max && room_name(value) == value;
	}
	// UTF-8 text cut to `max_chars` code points, never inside one; control characters and broken sequences dropped.
	inline std::string clip_utf8(const std::string& text, std::size_t max_chars)
	{
		std::string out;
		std::size_t chars = 0;
		for (std::size_t i = 0; i < text.size() && chars < max_chars;)
		{
			const auto c = static_cast<unsigned char>(text[i]);
			std::size_t n = c < 0x80 ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : (c >> 3) == 30 ? 4 : 0;
			bool ok = n && i + n <= text.size();
			for (std::size_t k = 1; ok && k < n; ++k) ok = (static_cast<unsigned char>(text[i + k]) & 0xc0) == 0x80;
			if (!ok) { ++i; continue; }
			if (n == 1 && (c < 0x20 || c == 0x7f)) { ++i; continue; }
			out.append(text, i, n);
			i += n;
			++chars;
		}
		return trim(out);
	}
	// The game's online screen name (the Play Online name entry; Windows-1252 bytes, already UTF-8 here).
	inline std::string display_name(const std::string& screen_name) { return clip_utf8(screen_name, display_name_max); }
	// The room's name in the browser: [Online] RoomName, else "<screen name>'s game" (empty until the name is known).
	inline std::string browser_name(const std::string& configured, const std::string& screen_name)
	{
		const auto own = room_name(configured);
		if (!own.empty()) return own;
		const auto who = display_name(screen_name);
		return who.empty() ? std::string() : clip_utf8(who + "'s game", name_max);
	}

	// What the game is doing, as the room reports it (read from the game's memory by online_room.cpp).
	struct game_view
	{
		std::string zone;        // the current zone's path ("" before the first one, "menu/..." in the menus)
		std::string zone_title;  // its zoneinfo savename, UTF-8 ("" when it has none)
		std::string screen_name; // the online screen name, UTF-8 ("" until the player typed / confirmed it)
		bool danger_room = false;
		bool session = false;    // an online game is set up (the host chose a campaign)
		bool playing = false;    // in a level on screen: no zone load pending, no loading screen, movie or menu
	};
	inline bool menu_zone(const std::string& zone) { return zone.empty() || zone.rfind("menu/", 0) == 0; }
	// Printable ASCII path, at most 48 characters (the broker refuses longer); "" for the menus.
	inline std::string zone_token(const std::string& zone)
	{
		if (menu_zone(zone)) return {};
		std::string out;
		for (const char ch : zone)
		{
			const auto c = static_cast<unsigned char>(ch);
			if (c < 0x20 || c > 0x7e) continue;
			out += ch == '\\' ? '/' : ch;
		}
		return out.size() > zone_max ? out.substr(out.size() - zone_max) : out; // the end names the level
	}

	// The room's state from here on: the session started once a game zone loaded while the online game is set up
	// (latched - it never goes back); new players can be taken while a seat is free, and after the start only with
	// drop-in (JoinInProgress) installed.
	struct room_state
	{
		bool started = false;
		void observe(const game_view& g) { if (g.session && !menu_zone(g.zone)) started = true; }
	};
	inline bool joinable(bool started, bool drop_in, int players, int max_players) { return players < max_players && (!started || drop_in); }

	// The metadata this host's room should carry now. `last` is what it carried until now (public, version token and
	// max players never change; the zone stays while the host is in the menus between levels).
	inline room_metadata current(const room_metadata& last, const std::string& configured_name, const game_view& g, const room_state& s, bool drop_in, int players)
	{
		room_metadata m = last;
		m.host_display_name = display_name(g.screen_name);
		m.name = last.public_room ? browser_name(configured_name, g.screen_name) : std::string();
		m.game_mode = g.danger_room ? "danger_room" : "campaign";
		const auto zone = zone_token(g.zone);
		// The menus between levels keep the last level listed (the host is still in that game).
		if (!zone.empty() || !s.started) { m.zone = zone; m.zone_name = zone.empty() ? std::string() : clip_utf8(g.zone_title, zone_max); }
		m.in_progress = s.started;
		m.joinable = joinable(s.started, drop_in, players, last.max_players);
		return m;
	}

	// {"op":"presence","metadata":{...the fields that differ from `sent`...}}\n, or "" when nothing changed.
	inline std::string presence_request(const room_metadata& now, const room_metadata& sent)
	{
		using online_room_core::quote;
		std::string fields;
		auto text = [&](const char* key, const std::string& a, const std::string& b) { if (a != b) fields += (fields.empty() ? "" : ",") + quote(key) + ":" + quote(a); };
		auto flag = [&](const char* key, bool a, bool b) { if (a != b) fields += (fields.empty() ? "" : ",") + quote(key) + ":" + (a ? "true" : "false"); };
		text("name", now.name, sent.name);
		text("host_display_name", now.host_display_name, sent.host_display_name);
		text("game_mode", now.game_mode, sent.game_mode);
		text("zone", now.zone, sent.zone);
		text("zone_name", now.zone_name, sent.zone_name);
		flag("joinable", now.joinable, sent.joinable);
		flag("in_progress", now.in_progress, sent.in_progress);
		return fields.empty() ? std::string() : "{\"op\":\"presence\",\"metadata\":{" + fields + "}}\n";
	}

	// On change, at most one update per presence_interval_ms; a change inside the interval waits for its end (only the
	// latest state is sent then). The clock is a millisecond tick that may wrap.
	class presence_pacer
	{
		bool sent_once_ = false;
		std::uint32_t last_ = 0;
	public:
		bool may_send(std::uint32_t now) const { return !sent_once_ || std::uint32_t(now - last_) >= presence_interval_ms; }
		void sent(std::uint32_t now) { sent_once_ = true; last_ = now; }
	};

	// The in-game line: "Room code: ABCD-EFGH" (the launcher's display form; short enough for the help bar at 800x600).
	inline std::string code_line(const std::string& code)
	{
		if (code.size() != 8) return {};
		return "Room code: " + code.substr(0, 4) + "-" + code.substr(4);
	}
	// The narrow place (Play Online's Host/Join menu, left of "Back"): "Code: ABCD-EFGH".
	inline std::string short_code_line(const std::string& code)
	{
		if (code.size() != 8) return {};
		return "Code: " + code.substr(0, 4) + "-" + code.substr(4);
	}
	// Where the host's lobby menus show it: a free place in the menu's bottom help bar (its desctext slot 0-4; retail
	// layouts, live: the host lobby leaves desctext2 empty beside "Back" - room for the long line; Play Online's
	// Host/Join menu centres "Back" and "Select" and leaves desctext1, right-aligned left of "Back", empty - the short
	// line; desctext3 is hidden on both). -1: not a lobby menu.
	inline int lobby_slot(const std::string& menu) { return menu == "host" ? 1 : menu == "campaign_lobby" ? 0 : -1; }
	inline bool lobby_menu(const std::string& menu) { return lobby_slot(menu) >= 0; }
}
