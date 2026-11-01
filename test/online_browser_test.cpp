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
	// Fix AN2: the drop-in the room advertises. Public rooms follow the host's choice; a private room with the host's
	// started refusal installed stays resolvable after the start, so the code join reaches that refusal.
	check(!b::advertised_drop_in(true, false, true) && b::advertised_drop_in(true, true, true), "AN2: public room advertises the host's DROP-IN choice");
	check(b::advertised_drop_in(false, false, true), "AN2: private room, DROP-IN off, host refusal installed: resolvable");
	check(!b::advertised_drop_in(false, false, false) && !b::advertised_drop_in(true, false, false), "AN2: no installed host refusal: DROP-IN off stays unjoinable");
	check(b::advertised_drop_in(false, true, true), "AN2: private room, DROP-IN on: joinable as before");
	{
		room_metadata priv_base = base; priv_base.public_room = false;
		const bool priv = b::advertised_drop_in(false, false, true), pub = b::advertised_drop_in(true, false, true);
		check(b::current(priv_base, "", level, s, priv, 2).joinable && b::current(priv_base, "", level, s, priv, 2).in_progress, "AN2: started private room, DROP-IN off: in progress and joinable at the broker");
		check(!b::current(m, "", level, s, pub, 2).joinable, "AN2: started public room, DROP-IN off: not joinable (Browse row unchanged)");
		check(!b::current(priv_base, "", level, s, priv, 4).joinable, "AN2: started private room, full: not joinable");
		check(b::current(priv_base, "", menus, b::room_state{}, priv, 1).joinable && !b::current(priv_base, "", menus, b::room_state{}, priv, 4).joinable, "AN2: private lobby unchanged (free seat joinable, full not)");
	}
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

	using online_room_core::parse;
	std::vector<b::public_room> rows;
	check(b::public_rooms(parse(R"({"op":"list_rooms_ok","rooms":[{"code":"ABCDEFGH"}]})"),rows) && rows.size()==1 && rows[0].name.empty(), "list: absent optional metadata stays unknown");
	check(!b::public_rooms(parse(R"({"op":"list_rooms_ok","rooms":[{"code":"ABCDEFGH"},{"code":"ABCDEFGH"}]})"),rows) && rows.size()==1, "list: duplicate codes reject whole reply, retain prior snapshot");
	for(const auto* json : {R"({"op":"list_rooms_ok","rooms":[{"code":"ABCDEFGH-extra"}]})",
		R"({"op":"list_rooms_ok","rooms":[{"code":"abcdefgh"}]})",R"({"op":"list_rooms_ok","rooms":[{"code":"ABCDEFGH","players":1.2}]})",
		R"({"op":"list_rooms_ok","rooms":[{"code":"ABCDEFGH","joinable":"true"}]})",R"({"op":"list_rooms_ok","rooms":[{"code":"ABCDEFGH","players":4,"max_players":2}]})"})
		check(!b::public_rooms(parse(json),rows), "list: invalid identity/type/seat count refused");
	std::string many="{\"op\":\"list_rooms_ok\",\"rooms\":[";
	for(unsigned i=0;i<51;++i) {
		if(i) many+=',';
		many+="{\"code\":\"AAAAAA";many+=char('A'+i/8);many+=char('A'+i%8);many+="\"}";
		if(i==49) check(b::public_rooms(parse(many+"]}"),rows) && rows.size()==50,"list: 50 complete identities fit");
	}
	check(!b::public_rooms(parse(many+"]}"),rows) && rows.size()==50,"list: 51 rows refused, no partial publication");
	for(const auto* json:{"{} trailing","{\"x\":1garbage}","{\"x\":01}","{\"x\":true,\"x\":false}","{\"x\":[{]}","{\"x\":[1,]}","{\"x\":\"raw\ncontrol\"}","{\"x\":\"\\ud800\"}"})
		check(!parse(json).valid,"broker JSON: complete strict grammar");
	check(b::valid_room_name("  Friday  ") && b::valid_room_name(std::string(32,'x')) && !b::valid_room_name(std::string(33,'x')) &&
		!b::valid_room_name("a;b") && !b::valid_room_name("a\"b") && !b::valid_room_name("a\nb"),"in-game room name: validate rather than silently clip/drop");
	b::client_state state;
	check(state.view().state==b::phase::idle,"client: Hub idle has no host or resolve side effect");
	check(state.begin(b::operation::list,1000),"client: authenticate for list");auto first=state.view().generation;
	check(!state.issue() && !state.begin(b::operation::resolve,3000),"client: one pending request and no request before auth");
	check(state.authenticated(first) && state.issue() && !state.issue(),"client: issue once after auth");
	check(state.listed(first,parse(R"({"op":"list_rooms_ok","rooms":[]})"),1001) && !state.refresh_due(11000) && state.refresh_due(11001),"client: empty list refreshes after 10 seconds idle");
	check(!state.begin(b::operation::resolve,2999) && state.begin(b::operation::resolve,3000,true),"client: list and resolve share limiter, manual Refresh cannot bypass");
	auto second=state.view().generation;check(second.request>first.request && second.room>first.room && state.issue(),"client: request and room generations advance");
	state.cancel();state.stopped();
	check(!state.joined(second,b::operation::resolve,"ABCDEFGH") && !state.begin(b::operation::list,4999),"client: Back/cancel drops stale completion and retains limiter");
	check(state.begin(b::operation::list,5000,true) && state.issue(),"client: room reentry allowed after limiter");
	state.fail(state.view().generation,"rate_limited",5001);
	check(state.view().stale && !state.begin(b::operation::resolve,15000) && state.begin(b::operation::resolve,15001),"client: broker rate limit backs off for 10 seconds across operations");
	auto late=state.view().generation;
	check(!state.expired(25000) && state.expired(25001),"client: request/auth timeout boundary");
	state.fail(late,"timeout",25001);state.cancel(true);state.stopped();
	check(!state.begin(b::operation::list,35001) && state.begin(b::operation::list,45001),"client: leave/retry cannot reset escalating backoff");
	b::client_state wrapped;
	check(wrapped.begin(b::operation::list,0xfffffff0u,true) && wrapped.issue(),"client: wrapping clock starts");
	wrapped.listed(wrapped.view().generation,parse(R"({"op":"list_rooms_ok","rooms":[]})"),0xfffffff0u);
	check(!wrapped.may_lookup(0x20u) && wrapped.may_lookup(0x800u),"client: limiter survives DWORD wrap");
	b::client_state committed;
	check(committed.begin(b::operation::host,100,true) && committed.issue() && committed.joined(committed.view().generation,b::operation::host,"ABCDEFGH"),"room: committed setup");
	const auto room_ticket=committed.view().generation;
	committed.fail(room_ticket,"inbox_overflow",200);
	check(committed.view().state==b::phase::room && committed.view().generation==room_ticket && committed.view().code=="ABCDEFGH" && !committed.view().authenticated,"room: fail degrades service, preserves phase/identity");
	committed.refuse("transport_identity_busy");committed.cancel();
	check(committed.view().state==b::phase::room && committed.view().generation==room_ticket,"room: refuse/cancel cannot release committed ownership");
	check(!committed.begin(b::operation::host,99999) && !committed.begin(b::operation::resolve,99999),"room: no retry/replacement before explicit Leave");
	committed.cancel(true);committed.stopped();
	check(committed.view().state==b::phase::idle && committed.view().generation.room>room_ticket.room && committed.begin(b::operation::resolve,99999),"room: explicit Leave releases ownership for next attempt");
	// Round 15 (presentation): the shared limiter's remaining wait, one failure per generation, the other build's token.
	b::client_state limiter;
	check(limiter.retry_in(0)==0,"limiter: nothing sent yet, nothing to wait for");
	check(limiter.begin(b::operation::list,1000) && limiter.retry_in(1000)==b::lookup_interval_ms && limiter.retry_in(2500)==500 && limiter.retry_in(3000)==0,"limiter: the two-second spacing counts down and ends");
	limiter.authenticated(limiter.view().generation);limiter.issue();
	const auto limited=limiter.view().generation;limiter.fail(limited,"rate_limited",3000);
	check(limiter.view().state==b::phase::retry_wait && limiter.retry_in(3000)==10000 && limiter.retry_in(3001)==9999 && limiter.retry_in(12999)==1 && limiter.retry_in(13000)==0,"limiter: a rate-limit failure waits the service's 10 seconds");
	check(!limiter.begin(b::operation::list,12999) && limiter.begin(b::operation::list,13000),"limiter: retry_in agrees with may_lookup/begin");
	b::client_state wrap;wrap.begin(b::operation::list,0xfffffff0u,true);wrap.issue();
	check(wrap.retry_in(0xfffffff0u)==b::lookup_interval_ms && wrap.retry_in(0x7f0u)==0 && wrap.retry_in(0x20u)==b::lookup_interval_ms-0x30u,"limiter: retry_in survives the 32-bit clock wrap");
	b::client_state once;once.begin(b::operation::resolve,100000,true);once.issue();const auto id=once.view().generation;
	once.fail(id,"room_full",100001);once.detail(id,"LC36");
	const auto wait=once.retry_in(100001);
	once.fail(id,"timeout",100002);once.fail(id,"timeout",100003);once.disconnected("timeout",100004);
	check(once.view().error=="room_full" && once.view().state==b::phase::retry_wait && once.retry_in(100001)==wait && once.view().error_detail=="LC36","client: a duplicate or late failure changes neither the cause nor the backoff");
	once.detail({id.request+7,id.room},"WRONG");check(once.view().error_detail=="LC36","client: a stale ticket cannot set the detail");
	once.cancel();once.stopped();check(once.view().error_detail.empty() && once.view().error.empty(),"client: cancel clears the failure and its detail");
	b::client_state fresh;fresh.begin(b::operation::list,1,true);fresh.issue();fresh.detail(fresh.view().generation,"LC1");check(fresh.view().error_detail.empty(),"client: detail needs a recorded failure");
    // Fix AN: a fresh, complete public row is the only evidence for the specific started-session refusal.
    auto list=[](const std::string& fields) { return online_room_core::parse("{\"op\":\"list_rooms_ok\",\"rooms\":["+fields+"]}"); };
    const std::string row="\"code\":\"ABCDEFGH\",\"in_progress\":true,\"joinable\":false";
    check(b::started_room_refusal("ABCDEFGH",list("{"+row+",\"players\":1,\"max_players\":4}")),"refusal: started, unjoinable, known free seat -> specific");
    for(int max:{2,3,4}) {
        check(b::started_room_refusal("ABCDEFGH",list("{"+row+",\"players\":"+std::to_string(max-1)+",\"max_players\":"+std::to_string(max)+"}")),"refusal: free seat at each native capacity");
        check(!b::started_room_refusal("ABCDEFGH",list("{"+row+",\"players\":"+std::to_string(max)+",\"max_players\":"+std::to_string(max)+"}")),"refusal: full at each native capacity remains inconclusive");
    }
    for(const char* fields:{
        R"({"code":"ABCDEFGH","in_progress":false,"joinable":false,"players":1,"max_players":4})",
        R"({"code":"ABCDEFGH","in_progress":true,"joinable":true,"players":1,"max_players":4})",
        R"({"code":"ABCDEFGH","in_progress":true,"joinable":false,"max_players":4})",
        R"({"code":"ABCDEFGH","in_progress":true,"joinable":false,"players":1})",
        R"({"code":"ABCDEFGH","in_progress":true,"joinable":false,"players":0,"max_players":4})",
        R"({"code":"ABCDEFGH","joinable":false,"players":1,"max_players":4})",
        R"({"code":"ABCDEFGH","in_progress":true,"players":1,"max_players":4})",
        R"({"code":"AAAAAAAB","in_progress":true,"joinable":false,"players":1,"max_players":4})",
        R"({"code":"ABCDEFGH","in_progress":"true","joinable":false,"players":1,"max_players":4})",
        R"({"code":"ABCDEFGH","in_progress":true,"joinable":false,"players":5,"max_players":4})",
        ""}) check(!b::started_room_refusal("ABCDEFGH",list(fields)),"refusal: transient/unknown/private/other-code/malformed row -> retry");
    const auto good=list("{"+row+",\"players\":1,\"max_players\":4}");
    check(!b::started_room_refusal("ABCDEFGH",list("{"+row+",\"players\":1,\"max_players\":4},{\"code\":\"bad\"}")),"refusal: partial list never used");
    b::client_state refusal;refusal.begin(b::operation::resolve,1000,true);refusal.issue();auto rt=refusal.view().generation;
    check(refusal.explain_refusal(rt,"ABCDEFGH") && !refusal.explain_refusal(rt,"ABCDEFGH"),"refusal: at most one explanation per resolve");
    check(!refusal.joined(rt,b::operation::resolve,"ABCDEFGH"),"refusal: late resolve success cannot admit the refused room");
    refusal.refusal_listed({rt.request+1,rt.room},good,1100);
    check(refusal.pending() && refusal.explaining_refusal(),"refusal: stale list cannot finish the attempt");
    refusal.refusal_listed(rt,good,1100);
    check(refusal.pending() && refusal.view().error_detail.empty() && !refusal.confirm_refusal(7099),"refusal: one matching snapshot waits through host presence interval");
    refusal.refusal_listed(rt,good,1200); // duplicate first reply cannot confirm policy
    check(refusal.pending() && refusal.confirm_refusal(7100) && !refusal.confirm_refusal(7101),"refusal: one bounded confirmation after 6 seconds");
    refusal.refusal_listed(rt,good,7100);
    check(refusal.view().error=="room_not_joinable" && refusal.view().error_detail=="session_started" && !refusal.explaining_refusal(),"refusal: original id plus trusted presentation detail");
    refusal.fail(rt,"timeout",1200);check(refusal.view().error_detail=="session_started","refusal: duplicate failure keeps first explanation");
    refusal.cancel();refusal.stopped();refusal.refusal_listed(rt,good,1300);
    check(refusal.view().error.empty() && refusal.view().error_detail.empty(),"refusal: cancel clears context and rejects late list");
    for(const char* error:{"timeout","broker_unreachable","rate_limited","send_queue_full","malformed_reply"}) {
        b::client_state no_detail;no_detail.begin(b::operation::resolve,1000,true);no_detail.issue();auto t=no_detail.view().generation;
        no_detail.explain_refusal(t,"ABCDEFGH");no_detail.fail(t,error,1100);
        check(no_detail.view().error=="room_not_joinable" && no_detail.view().error_detail.empty(),"refusal: explanation failure preserves original id, no guess");
        check(no_detail.begin(b::operation::resolve,80000,true) && !no_detail.explaining_refusal() && no_detail.view().error_detail.empty(),"refusal: next attempt has no stale context");
    }
    b::client_state seat_freed;seat_freed.begin(b::operation::resolve,1000,true);seat_freed.issue();const auto ft=seat_freed.view().generation;
    seat_freed.explain_refusal(ft,"ABCDEFGH");seat_freed.refusal_listed(ft,good,1100);seat_freed.confirm_refusal(7100);
    seat_freed.refusal_listed(ft,list(R"({"code":"ABCDEFGH","in_progress":true,"joinable":true,"players":1,"max_players":4})"),7100);
    check(seat_freed.view().error=="room_not_joinable" && seat_freed.view().error_detail.empty(),"refusal: full-to-free drop-in ON advert settles to retry, never started");
    b::client_state too_late;too_late.begin(b::operation::resolve,1000,true);too_late.issue();const auto lt=too_late.view().generation;
    too_late.explain_refusal(lt,"ABCDEFGH");too_late.refusal_listed(lt,good,9000);
    check(!too_late.confirm_refusal(15000),"refusal: confirmation cannot extend the original deadline");
    std::printf("%d PASS / %d FAIL\n", passed, failed);
	return failed ? 1 : 0;
}
