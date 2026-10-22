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
