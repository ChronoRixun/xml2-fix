// Fix AS (RR-1): every PC of a room builds its own address map through the PRODUCTION room code - handle() for the
// broker's host_ok / resolve_ok / peer_joined / peer_left, poll() -> roster_receive() for the host's roster on the room
// connection - and every PC must end at the host's map. A broker model (net030-broker rooms.py: members in join order,
// leavers removed, gen +1 per membership change, the joiner gets resolve_ok and the others peer_joined) produces each
// scenario's lines. The host runs first and records its map and its roster after every event; then each membership
// interval of every other identity runs as a joiner on its own (a fresh room state), with the host's roster taken at
// several points of the host's stream and delivered at every position of the joiner's own broker stream, with or
// without the host's later pushes, before, with or after the joiner's copy of each event. No GNS, sockets or worker.
namespace roster_fixture {
namespace core = online_room_core;
using closure_fixture::queued;
const std::string H = "legends:5000000000000000";
std::string member(int n) { return "legends:5000000000000" + std::string(n < 10 ? "00" : "0") + std::to_string(n); }
struct event { long long gen; bool join; std::string id; std::vector<std::string> after; }; // after = broker members after it
struct broker_model {
    std::vector<std::string> members{H};
    long long gen = 1; // host_ok
    std::vector<event> events;
    bool join(const std::string& id) {
        if (std::find(members.begin(), members.end(), id) != members.end() || members.size() >= 4) return false;
        members.push_back(id); events.push_back({++gen, true, id, members}); return true;
    }
    bool leave(const std::string& id) {
        auto it = std::find(members.begin(), members.end(), id);
        if (it == members.end() || id == H) return false;
        members.erase(it); events.push_back({++gen, false, id, members}); return true;
    }
};
std::string line(const event& e) {
    return std::string("{\"op\":\"") + (e.join ? "peer_joined" : "peer_left") + "\",\"identity\":" + core::quote(e.id) + ",\"gen\":" + std::to_string(e.gen) + "}";
}
std::string resolve_line(const event& e) {
    std::string m;
    for (const auto& id : e.after) m += (m.empty() ? "" : ",") + core::quote(id);
    return "{\"op\":\"resolve_ok\",\"code\":\"ABCDEFGH\",\"host\":" + core::quote(H) + ",\"gen\":" + std::to_string(e.gen) + ",\"members\":[" + m + "]}";
}
using map_t = std::map<std::string, std::uint32_t>; // active member -> address
map_t current() { map_t m; for (const auto& id : room::members.active_ids()) m[id] = room::members.address(id); return m; }
std::vector<std::uint8_t> captured;
EResult capture(HSteamNetConnection, const void* data, std::uint32_t size) { const auto* b = static_cast<const std::uint8_t*>(data); captured.assign(b, b + size); return k_EResultOK; }
int receive(HSteamNetConnection c, SteamNetworkingMessage_t** out, int max) { return closure_fixture::receive(c, out, max); }
void pc(const std::string& id, bool hosting) {
    while (!queued.empty()) { queued.front()->Release(); queued.pop_front(); }
    room::tunnels.clear(); room::arrivals.clear(); room::arrivals_bytes = 0; room::members.clear(); room::client = {};
    room::hosting = hosting; room::own_id = id; room::room_ready = false; room::room_code.clear(); room::host_id.clear();
    room::configured_code = "ABCDEFGH"; room::own_address = 0; room::room_clock = &fake::now; fake::clock = 10000;
    room::receive_messages = receive; room::send_roster_bytes = capture; room::wanted = true;
    room::menu_client = true; room::cert_ready = room::ice_ready = true; room::broker_gone = false; room::key_loaded = true;
}
struct host_run { std::vector<map_t> maps; std::vector<std::vector<std::uint8_t>> rosters; }; // after event k
host_run run_host(const broker_model& bm) {
    host_run out;
    pc(H, true);
    assert(room::members.reset({H}, 1)); room::room_ready = true; room::host_id = H; room::room_code = "ABCDEFGH"; room::own_address = room::members.address(H);
    for (const auto& e : bm.events) {
        room::handle(core::parse(line(e)));
        out.maps.push_back(current());
        out.rosters.push_back(core::encode_roster(room::members.gen(), room::members.rows()));
        assert(!out.rosters.back().empty() && room::own_virtual() == 0x0a4d0001);
    }
    // The production send path carries exactly these bytes to a connected member.
    if (!bm.events.empty() && room::members.active_size() > 1) {
        auto t = std::make_unique<room::tunnel>(); t->id = room::members.active_ids().back(); t->connection = 123; t->connected = true;
        room::tunnels.push_back(std::move(t)); captured.clear(); room::send_roster_all("fixture");
        assert(captured == out.rosters.back());
        room::tunnels.clear();
    }
    return out;
}
unsigned runs = 0, intervals = 0;
// One joiner interval [j, l): its resolve at event j, its own broker stream j+1 .. l-1, the host's roster after event k
// at stream position q, then (pushes) the host's later rosters m = k+1 .. l-1 at stream position (m - j) + shift.
void run_joiner(const broker_model& bm, const host_run& h, std::size_t j, std::size_t l, std::size_t k, std::size_t q, int pushes, int shift) {
    const auto& joined = bm.events[j];
    pc(joined.id, false);
    assert(room::client.begin(b::operation::resolve, fake::clock, true) && room::client.issue());
    room::handle(core::parse(resolve_line(joined)));
    assert(room::room_ready && room::host_id == H && !room::members.confirmed() && room::own_virtual() == 0 && room::members.address(H) == 0x0a4d0001);
    auto& host = room::ensure_tunnel(H); host.connection = 123; host.connected = true; host.connected_at = fake::clock;
    std::uint32_t target = 7;
    assert(!room::admission_target(room::view().generation, target) && target == 0); // no admission before the roster
    std::vector<std::pair<std::size_t, std::vector<std::uint8_t>>> rosters{{q, h.rosters[k]}};
    if (pushes)
        for (std::size_t m = k + 1; m < l; ++m) {
            const long long at = static_cast<long long>(m - j) + shift;
            rosters.push_back({std::size_t(std::max<long long>(at, static_cast<long long>(rosters.back().first))), h.rosters[m]});
        }
    std::size_t next = 0;
    const std::size_t stream = l - j - 1;
    for (std::size_t p = 0; p <= stream; ++p) {
        while (next < rosters.size() && std::min(rosters[next].first, stream) == p) {
            closure_fixture::send(rosters[next].second); room::poll(); ++next;
            assert(room::members.confirmed() && queued.empty());
        }
        if (p < stream) room::handle(core::parse(line(bm.events[j + 1 + p])));
    }
    assert(next == rosters.size());
    const auto mine = current(), theirs = h.maps[l - 1];
    if (mine != theirs || room::own_virtual() != theirs.at(joined.id)) {
        std::cerr << "MAP DISAGREES: joiner " << joined.id << " interval " << j << ".." << l << " roster after " << k << " at " << q << " pushes " << pushes << " shift " << shift << "\n";
        for (const auto& [id, a] : theirs) std::cerr << "  host  " << id << " " << core::dotted(a) << "\n";
        for (const auto& [id, a] : mine) std::cerr << "  joiner " << id << " " << core::dotted(a) << "\n";
        std::exit(1);
    }
    assert(room::admission_target(room::view().generation, target) && target == 0x0a4d0001);
    ++runs;
}
void check(const broker_model& bm) {
    const auto h = run_host(bm);
    for (std::size_t j = 0; j < bm.events.size(); ++j) {
        if (!bm.events[j].join) continue;
        std::size_t l = j + 1;
        while (l < bm.events.size() && bm.events[l].id != bm.events[j].id) ++l; // its leave, or the end
        ++intervals;
        const std::size_t stream = l - j - 1;
        for (std::size_t k = j; k < l && k <= j + 3; ++k)
            for (std::size_t q = 0; q <= stream; ++q)
                for (int pushes = 0; pushes < 2; ++pushes)
                    for (int shift = -1; shift <= (pushes ? 1 : -1); ++shift) run_joiner(bm, h, j, l, k, q, pushes, shift);
    }
}
broker_model script(const std::vector<std::pair<char, int>>& ops) {
    broker_model bm;
    for (const auto& [o, n] : ops) { const bool ok = o == 'j' ? bm.join(member(n)) : bm.leave(member(n)); assert(ok); (void)ok; }
    return bm;
}
// The host's roster refused or missing: the map stays provisional, then the old broker order (a host build before AS).
void refusals() {
    const broker_model bm = script({{'j', 1}, {'j', 2}, {'j', 3}, {'l', 2}, {'j', 2}});
    const auto h = run_host(bm);
    const auto& e = bm.events.back();
    auto start = [&] {
        pc(e.id, false); assert(room::client.begin(b::operation::resolve, fake::clock, true) && room::client.issue());
        room::handle(core::parse(resolve_line(e))); assert(room::room_ready && !room::members.confirmed());
    };
    start();
    auto& other = room::ensure_tunnel(member(1)); other.connection = 124; other.connected = true; // not the host's tunnel
    closure_fixture::send(h.rosters.back()); room::poll(); assert(!room::members.confirmed() && other.dropped == 1);
    auto& host = room::ensure_tunnel(H); host.connection = 123; host.connected = true; host.connected_at = fake::clock; other.connected = false;
    auto bad = h.rosters.back(); bad.pop_back(); closure_fixture::send(bad); room::poll(); assert(!room::members.confirmed() && host.dropped == 1);
    bad = h.rosters.back(); bad.back() = 2; closure_fixture::send(bad); room::poll(); assert(!room::members.confirmed());
    // led by another identity, without this PC, more than four active, repeated identity: decoded but refused by adopt
    for (int v = 0; v < 4; ++v) {
        std::vector<core::roster_row_t> rows{{H, true}, {member(1), true}, {member(2), true}, {member(3), true}};
        if (v == 0) std::swap(rows[0], rows[1]);
        if (v == 1) rows.pop_back(), rows.back().active = false;
        if (v == 2) rows.push_back({member(4), true});
        if (v == 3) rows[3].id = member(1);
        closure_fixture::send(core::encode_roster(e.gen, rows)); room::poll(); assert(!room::members.confirmed() && room::own_virtual() == 0);
    }
    // No roster: provisional for 5 s on the connected host tunnel, then the broker's order (the old behaviour).
    fake::clock += 4999; room::roster_wait_tick(); assert(!room::members.confirmed());
    fake::clock += 1; room::roster_wait_tick(); assert(room::members.confirmed() && room::own_virtual() == 0x0a4d0004);
    // A roster arriving after that still wins (the host's map is the one its game hands out).
    closure_fixture::send(h.rosters.back()); room::poll();
    assert(current() == h.maps.back() && room::own_virtual() == h.maps.back().at(e.id) && room::own_virtual() == 0x0a4d0003);
    // An older roster than the one adopted is refused.
    closure_fixture::send(h.rosters[h.rosters.size() - 2]); room::poll(); assert(current() == h.maps.back());
    std::cout << "PASS roster refusals: non-host tunnel, truncated, bad flag, foreign leader, missing self, five active, repeated id, stale; 5 s broker-order fallback; a late host roster still wins\n";
}
void run_all() {
    std::cout << std::unitbuf;
    // RR-1: g1 seat 1, s1 seat 2, r1 seat 3; s1 leaves; START; s1 joins late (the same identity).
    const auto g1 = 1, s1 = 2, r1 = 3;
    struct named { const char* name; std::vector<std::pair<char, int>> ops; };
    const std::vector<named> scripted{
        {"RR-1 (middle member leaves in the lobby, rejoins late)", {{'j', g1}, {'j', s1}, {'j', r1}, {'l', s1}, {'j', s1}}},
        {"SO-1 shape (the same order, a new identity late instead)", {{'j', g1}, {'j', s1}, {'j', r1}, {'l', s1}, {'j', 4}}},
        {"leaver first", {{'j', 1}, {'j', 2}, {'j', 3}, {'l', 1}, {'j', 1}, {'l', 2}, {'j', 4}}},
        {"leaver middle", {{'j', 1}, {'j', 2}, {'j', 3}, {'l', 2}, {'j', 4}, {'l', 4}, {'j', 2}}},
        {"leaver last", {{'j', 1}, {'j', 2}, {'j', 3}, {'l', 3}, {'j', 3}, {'l', 3}, {'j', 4}}},
        {"new id after a leave (2 players)", {{'j', 1}, {'l', 1}, {'j', 2}}},
        {"returning id (2 players)", {{'j', 1}, {'l', 1}, {'j', 1}, {'l', 1}, {'j', 1}}},
        {"3 players, both guests leave and come back in reverse", {{'j', 1}, {'j', 2}, {'l', 1}, {'l', 2}, {'j', 2}, {'j', 1}}},
        {"4 players, everyone cycles", {{'j', 1}, {'j', 2}, {'j', 3}, {'l', 1}, {'l', 2}, {'l', 3}, {'j', 3}, {'j', 4}, {'j', 1}, {'l', 4}, {'j', 2}}},
        {"lobby fills, two late joiners after two leaves", {{'j', 1}, {'j', 2}, {'j', 3}, {'l', 2}, {'l', 1}, {'j', 5}, {'j', 6}}},
    };
    for (const auto& s : scripted) {
        const auto before = runs;
        check(script(s.ops));
        std::cout << "PASS roster agreement " << s.name << ": " << (runs - before) << " joiner runs agree with the host's map\n";
    }
    // Random churn: up to 7 identities, 2-4 present, 12 membership changes per room, 150 rooms.
    std::uint32_t seed = 0x5a5a1234u;
    auto rnd = [&](std::uint32_t n) { seed = seed * 1664525u + 1013904223u; return (seed >> 8) % n; };
    const auto before = runs;
    for (int room_n = 0; room_n < 150; ++room_n) {
        broker_model bm;
        while (bm.events.size() < 12) {
            const int who = 1 + int(rnd(7));
            if (rnd(2)) bm.join(member(who)); else bm.leave(member(who));
        }
        check(bm);
    }
    std::cout << "PASS roster agreement random churn: 150 rooms x 12 changes, " << (runs - before) << " joiner runs agree with the host's map\n";
    refusals();
    std::cout << "PASS roster offline: " << runs << " joiner runs over " << intervals << " membership intervals, every PC's map equals the host's\n";
}
}
