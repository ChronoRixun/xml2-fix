// The public game browser (contract v1.3/v1.4): [Online] RoomName, the default room name, the host's room metadata
// built from the game's state, the presence update (only the fields that changed) and its pacing, the in-game code line.
#include "online_browser_core.hpp"

#include <cstdio>
#include <string>

namespace
{
	int passed = 0, failed = 0;
	void check(bool ok, const std::string& what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what.c_str());
		(ok ? passed : failed)++;
	}
}

int main()
{
	namespace b = online_browser_core;
	using online_room_core::room_metadata;

	// [Online] RoomName: printable ASCII, no ; or ", trimmed, at most 32.
	check(b::room_name("  Friday night  ") == "Friday night", "RoomName: trimmed");
	check(b::room_name("a;b\"c\td\x01" "e") == "abcde", "RoomName: ; \" and control characters dropped");
	check(b::room_name(std::string(40, 'x')) == std::string(32, 'x'), "RoomName: at most 32 characters");
	check(b::room_name("Caf\xc3\xa9 crew") == "Caf crew", "RoomName: non-ASCII dropped (printable ASCII only)");
	check(b::browser_name("", "Logan") == "Logan's game", "default room name: <screen name>'s game");
	check(b::browser_name("Night shift", "Logan") == "Night shift", "RoomName wins over the default");
	check(b::browser_name("", "") == "", "no screen name yet: no name");
	check(b::display_name(std::string(40, 'n')).size() == 32, "screen name: at most 32 characters");
	check(b::clip_utf8("Mont\xc3\xb1" "a\xff", 5) == "Mont\xc3\xb1", "UTF-8 clip: whole code points, broken bytes dropped");

	// Zones: menus are no level; the path is printable ASCII, at most 48 (its end kept).
	check(b::zone_token("menu/main_back") == "" && b::zone_token("") == "", "zone: the menus are listed as no zone");
	check(b::zone_token("nyc\\alison\\nyc1_1_3") == "nyc/alison/nyc1_1_3", "zone: path with forward slashes");
	check(b::zone_token(std::string(60, 'z') + "/end").size() == 48 && b::zone_token(std::string(60, 'z') + "/end").substr(44) == "/end", "zone: 48 characters, the level's end kept");

	// The host's metadata from the game's state.
	room_metadata base;
	base.public_room = true;
	base.version_token = "LC31";
	b::room_state s;
	b::game_view menus{"menu/main_back", "", "", false, false};
	auto m = b::current(base, "", menus, s, false, 1);
	check(m.zone.empty() && m.zone_name.empty() && !m.in_progress && m.joinable && m.game_mode == "campaign" && m.name.empty() && m.version_token == "LC31" && m.public_room,
	      "Play Online menus: no zone, not started, joinable, campaign, no name until the screen name is known");
	b::game_view named{"menu/main_back", "", "Logan", false, true};
	s.observe(named);
	m = b::current(m, "", named, s, false, 1);
	check(m.name == "Logan's game" && m.host_display_name == "Logan" && !s.started, "screen name known: host_display_name and the default name; a set-up game in the menus is not started");
	b::game_view level{"nyc/alison/nyc1_1_3", "East Manhattan", "Logan", false, true};
	s.observe(level);
	m = b::current(m, "", level, s, false, 2);
	check(s.started && m.in_progress && m.zone == "nyc/alison/nyc1_1_3" && m.zone_name == "East Manhattan", "first level: in_progress, zone and its display name");
	check(!m.joinable, "started without drop-in: not joinable");
	check(b::current(m, "", level, s, true, 2).joinable, "started with drop-in (JoinInProgress) and a free seat: joinable");
	check(!b::current(m, "", level, s, true, 4).joinable, "full (4 of 4): not joinable");
	check(!b::current(base, "", menus, b::room_state{}, false, 4).joinable, "full before the start: not joinable");
	auto between = b::current(m, "", b::game_view{"menu/main_back", "", "Logan", false, true}, s, true, 2);
	check(between.zone == "nyc/alison/nyc1_1_3" && between.zone_name == "East Manhattan" && between.in_progress, "menus after the start keep the last level and in_progress (latched)");
	auto dr = b::current(m, "", b::game_view{"danger/danger_room", "", "Logan", true, true}, s, true, 2);
	check(dr.game_mode == "danger_room" && dr.zone_name.empty(), "Danger Room: game_mode danger_room; no savename -> empty zone_name");
	room_metadata priv = base;
	priv.public_room = false;
	check(b::current(priv, "My room", level, s, true, 1).name.empty(), "private room: no name sent (RoomName only for public rooms)");

	// Presence: only what changed; nothing when nothing did.
	check(b::presence_request(m, m).empty(), "presence: nothing changed -> nothing sent");
	auto next = m;
	next.zone = "nyc/alison/nyc1_1_4";
	next.zone_name = "Subway";
	check(b::presence_request(next, m) == "{\"op\":\"presence\",\"metadata\":{\"zone\":\"nyc/alison/nyc1_1_4\",\"zone_name\":\"Subway\"}}\n", "presence: zone change -> zone and zone_name only");
	auto flags = m;
	flags.joinable = true;
	flags.in_progress = false;
	check(b::presence_request(flags, m) == "{\"op\":\"presence\",\"metadata\":{\"joinable\":true,\"in_progress\":false}}\n", "presence: booleans as JSON booleans");
	auto quoted = m;
	quoted.host_display_name = "A \"B\"";
	check(b::presence_request(quoted, m) == "{\"op\":\"presence\",\"metadata\":{\"host_display_name\":\"A \\\"B\\\"\"}}\n", "presence: strings escaped");
	check(b::presence_request(m, m).find("players") == std::string::npos && b::presence_request(next, m).find("public") == std::string::npos,
	      "presence: never players (broker-derived) nor public (fixed for the room)");

	// The host request carries the listing fields.
	auto host = online_room_core::host_request(m);
	check(host.find("\"public\":true") != std::string::npos && host.find("\"host_display_name\":\"Logan\"") != std::string::npos &&
	      host.find("\"zone_name\":\"East Manhattan\"") != std::string::npos && host.find("\"version_token\":\"LC31\"") != std::string::npos,
	      "host request: public, host_display_name, zone_name and version_token");
	check(online_room_core::host_request(priv).find("\"public\":false") != std::string::npos, "host request: a private room says public false");

	// Pacing: the first change at once, then at most one per 5 s (a wrapped tick counter included).
	b::presence_pacer p;
	check(p.may_send(1000), "pacer: the first update goes at once");
	p.sent(1000);
	check(!p.may_send(1001) && !p.may_send(5999), "pacer: nothing within 5 s");
	check(p.may_send(6000), "pacer: again after 5 s");
	b::presence_pacer w;
	w.sent(0xfffff000u);
	check(!w.may_send(0x00000100u) && w.may_send(0x00000fa0u + 0x1000u), "pacer: tick counter wrap");

	// The in-game code line and the lobby menus that repeat it.
	check(b::code_line("K7QN2X9M") == "Room code: K7QN-2X9M", "code line: XXXX-XXXX");
	check(b::code_line("") == "" && b::code_line("SHORT") == "", "code line: nothing without a code");
	check(b::short_code_line("K7QN2X9M") == "Code: K7QN-2X9M" && b::short_code_line("").empty(), "short code line for the narrow help bar place");
	check(b::lobby_menu("campaign_lobby") && b::lobby_menu("host") && !b::lobby_menu("main") && !b::lobby_menu("online") && !b::lobby_menu(""), "lobby menus repeat the code");
	check(b::lobby_slot("host") == 1 && b::lobby_slot("campaign_lobby") == 0 && b::lobby_slot("team") == -1, "lobby help-bar slots: host desctext2, Host/Join desctext1");

	std::printf("%d PASS / %d FAIL\n", passed, failed);
	return failed ? 1 : 0;
}
