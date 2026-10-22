#include "online_room_core.hpp"
#include "gns_transport_core.hpp"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#if XML2_FIX_GNS
extern "C" void ed25519_publickey(const unsigned char* secret, unsigned char* public_key);
extern "C" void ed25519_sign(const unsigned char* message, size_t size, const unsigned char* secret, const unsigned char* public_key, unsigned char* signature);
extern "C" int ed25519_sign_open(const unsigned char* message, size_t size, const unsigned char* public_key, const unsigned char* signature);
#endif

// [Online] Host/JoinCode: the broker protocol's lines, identities, join codes, virtual addresses, the certificate blob
// and the tunnel's datagram framing (src/online_room_core.hpp, src/gns_transport_core.hpp) without the game or a
// broker; with GNS linked, the Ed25519 the DLL signs the broker's challenge with (RFC 8032 test 1).
namespace
{
	int failures = 0, passes = 0;
	void check(bool ok, const char* what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
		ok ? ++passes : ++failures;
	}
	std::vector<std::uint8_t> hex(const char* h)
	{
		std::vector<std::uint8_t> out;
		for (; h[0] && h[1]; h += 2) out.push_back(std::uint8_t(std::stoul(std::string(h, 2), nullptr, 16)));
		return out;
	}
	using namespace online_room_core;
}

int main()
{
	{
		const std::string text = "foobar";
		check(base64(reinterpret_cast<const std::uint8_t*>(text.data()), 6) == "Zm9vYmFy" && base64(reinterpret_cast<const std::uint8_t*>(text.data()), 4) == "Zm9vYg==" &&
		      base64(reinterpret_cast<const std::uint8_t*>(text.data()), 5) == "Zm9vYmE=", "base64: RFC 4648 vectors");
		std::vector<std::uint8_t> back;
		check(unbase64("Zm9vYg==", back) && std::string(back.begin(), back.end()) == "foob" && unbase64("Zm9v\nYmE=", back) && std::string(back.begin(), back.end()) == "fooba",
		      "unbase64: padded and wrapped input");
		check(!unbase64("Zm9v*", back) && !unbase64("Z", back) && !unbase64("Zm=9", back), "unbase64: junk, a lone character and data after padding fail");
		std::vector<std::uint8_t> all(256);
		for (int i = 0; i < 256; ++i) all[std::size_t(i)] = std::uint8_t(i);
		check(unbase64(base64(all), back) && back == all, "base64 round trip of every byte value");
	}
	{
		const auto c = parse(R"({"op":"challenge","v":1,"nonce":"AAECAwQFBgcICQoLDA0ODw=="})");
		check(c.valid && c.text("op") == "challenge" && c.number("v") == 1 && c.text("nonce") == "AAECAwQFBgcICQoLDA0ODw==", "parse: the challenge line");
		const auto r = parse(R"({"op":"resolve_ok","code":"K7QN2X","host":"legends:9f76b95e897ee601","gen":2,"members":["legends:9f76b95e897ee601", "legends:0123456789abcdef"],"expires":1760000000})");
		check(r.valid && r["members"].t == value::type::list && r["members"].items.size() == 2 && r["members"].items[1] == "legends:0123456789abcdef" && r.number("expires") == 1760000000,
		      "parse: resolve_ok with its member list");
		const auto e = parse(R"({"op":"error","code":"no_such_room","msg":"no \"room\"\né😀","in_reply_to":"resolve","extra":{"a":[1,{"b":"}"}]},"n":null,"t":true})");
		check(e.valid && e.text("msg") == "no \"room\"\n\xc3\xa9\xf0\x9f\x98\x80" && e.text("in_reply_to") == "resolve" && e["extra"].t == value::type::other &&
		      e["n"].t == value::type::null && e["t"].t == value::type::boolean, "parse: escapes, surrogate pairs, an unknown nested field skipped");
		const auto u = parse(R"({"op":"turn_ok","uris":["stun:h:3478","turn:h:3478?transport=udp"],"ttl":21600,"list":[1,2]})");
		check(u.valid && u["uris"].items.size() == 2 && u["list"].t == value::type::other && u.number("ttl") == 21600, "parse: a list of numbers is skipped, strings kept");
		check(!parse("").valid && !parse("{\"op\":").valid && !parse("{\"op\":\"x\"").valid && !parse("[1]").valid && !parse("{\"op\":bogus}").valid && parse("{}").valid,
		      "parse: truncated, non-object and bare-word lines are invalid");
		check(request("signal", {{"to", "legends:0123456789abcdef"}, {"data", "AQ=="}}) == "{\"op\":\"signal\",\"to\":\"legends:0123456789abcdef\",\"data\":\"AQ==\"}\n" &&
		      request("cert") == "{\"op\":\"cert\"}\n" && quote("a\"b\\\x01") == "\"a\\\"b\\\\\\u0001\"", "request: one LF-terminated object, strings escaped");
	}
	{
		std::uint8_t key[public_key_size];
		for (int i = 0; i < 32; ++i) key[i] = std::uint8_t(0xa0 + i);
		const std::vector<std::uint8_t> nonce(16, 7);
		const auto m = auth_message(nonce, key);
		check(m.size() == 13 + 16 + 32 && std::memcmp(m.data(), "LCB1-auth-v2\0", 13) == 0 && m[13] == 7 && m[29] == 0xa0 && m.back() == 0xbf,
		      "auth message: \"LCB1-auth-v2\\0\" || nonce || public key");
		check(identity(key) == "legends:a0a1a2a3a4a5a6a7" && valid_identity(identity(key)) && gns_identity(identity(key)) == "str:legends:a0a1a2a3a4a5a6a7",
		      "identity: legends: + the key's first 8 bytes in lowercase hex");
		check(!valid_identity("legends:A0a1a2a3a4a5a6a7") && !valid_identity("legends:a0a1") && !valid_identity("str:legends:a0a1a2a3a4a5a6a7"), "identity: malformed ones refused");
	}
	{
		check(join_code("k7qn2x9m") == "K7QN2X9M" && join_code(" K7Q-N2X9M ") == "K7QN2X9M", "join code: eight characters, case and separators");
		check(join_code("K7QN2X9O").empty() && join_code("K7QN2X91").empty() && join_code("K7QN2X").empty() && join_code("K7QN2XY").empty() && join_code("K7QN2X9M2").empty() && join_code("10.77.1.2").empty(),
		      "join code: 0/O/1/I/L, old six/seven/nine-character lengths and addresses are no codes");
	}
	{
#if ONLINE_ROOM_HARDENING_CORE
		const std::string host = "legends:9f76b95e897ee601", joiner = "legends:0123456789abcdef", third = "legends:ffffffffffffffff";
		room_roster roster;
		check(roster.reset({host, joiner}) && roster.address(host) == 0x0a4d0001 && roster.address(joiner) == 0x0a4d0002 && dotted(roster.address(host)) == "10.77.0.1",
		      "room roster: broker order, not key bytes, assigns host .1 and the first joiner .2");
		roster.join(third);
		roster.join(joiner);
		roster.leave(joiner);
		check(roster.address(third) == 0x0a4d0003 && roster.address(joiner) == 0x0a4d0002 && !roster.contains(joiner) && roster.active_ids() == std::vector<std::string>{host, third},
		      "room roster: duplicates do not join twice and leaving never renumbers survivors or tombstones");
		roster.join(joiner);
		check(roster.contains(joiner) && roster.address(joiner) == 0x0a4d0002, "room roster: a rejoin keeps its earlier stable address");
		check(is_virtual(roster.address(host)) && !is_virtual(0x0a4e0001) && !is_virtual(0xc0a80001), "room roster: only the 10.77/16 virtual range is intercepted");

		token_bucket packets(2, 2);
		check(packets.allow(1000) && packets.allow(1000) && !packets.allow(1000) && packets.allow(1500),
		      "datagram rate: a token bucket enforces its burst and refills by elapsed time");
		token_bucket bytes(65536, 65536);
		check(bytes.allow(2000, 65036) && !bytes.allow(2000, 501) && bytes.allow(3000, 65536),
		      "datagram rate: byte costs share the same bounded budget");

		broker_line_buffer lines;
		std::string row;
		const std::string largest(16383, 'x');
		check(lines.append(largest + "\n") && lines.next(row) == broker_line_buffer::result::ready && row == largest,
		      "broker lines: 16384 bytes including LF are accepted");
		check(lines.append(std::string(16384, 'y') + "\n") && lines.next(row) == broker_line_buffer::result::overflow,
		      "broker lines: 16385 bytes including LF are refused");
		broker_line_buffer partial;
		check(partial.append(std::string(16383, 'z')) && partial.next(row) == broker_line_buffer::result::need_more && partial.append("x") && partial.next(row) == broker_line_buffer::result::overflow,
		      "broker lines: an unterminated line cannot grow past the cap");
		std::string pending;
		check(bounded_append(pending, "1234", 4) && pending == "1234" && !bounded_append(pending, "5", 4) && pending == "1234",
		      "broker pending bytes: exact-cap append succeeds and overflow leaves the buffer unchanged");

		char printable[6];
		const std::uint8_t terminated[]{'L','C','2','9',0}, unterminated[]{'A','B','C','D','E'}, control[]{'L','C',0x1f,'9',0};
		check(bounded_printable_field(terminated, sizeof(terminated), printable) && std::string(printable) == "LC29" &&
		      bounded_printable_field(unterminated, sizeof(unterminated), printable) && printable[5] == 0 && std::strlen(printable) == 5 &&
		      !bounded_printable_field(control, sizeof(control), printable),
		      "game-info logging: five bytes are always terminated and control bytes are rejected");

		std::vector<std::uint8_t> info(118, 0);
		info[2] = 0x18;
		const std::uint8_t hostile[]{192, 168, 1, 200, 0x39, 0x05};
		std::memcpy(info.data() + 13, hostile, sizeof(hostile));
		check(confine_embedded_address(info, 0x0a4d0003, 5165) && info[13] == 10 && info[14] == 77 && info[15] == 0 && info[16] == 3 && info[17] == 0x2d && info[18] == 0x14,
		      "game-info confinement: the embedded route is rewritten to the sender's own virtual address and port");
		info.resize(18);
		check(!confine_embedded_address(info, 0x0a4d0003, 5165), "game-info confinement: a truncated 0x18 cannot reach the stock parser");

		room_metadata metadata;
		metadata.name = "Weekend co-op"; metadata.game_mode = "campaign"; metadata.zone = "zone-1"; metadata.version_token = "build-token";
		metadata.joinable = false; metadata.in_progress = true;
		const std::string expected = std::string(R"({"op":"host","metadata":{"name":"Weekend co-op","game_mode":"campaign","zone":"zone-1","max_players":4,"public":false,"joinable":false,"in_progress":true,"version_token":"build-token"}})") + "\n";
		check(host_request(metadata) == expected && host_request(metadata).find("\"players\"") == std::string::npos,
		      "protocol 2 metadata: host claims are encoded and read-only players stay server-derived");
#else
		check(false, "room roster: broker order, not key bytes, assigns stable addresses");
		check(false, "room roster: leaving never renumbers survivors");
		check(false, "datagram rate: packet and byte token buckets");
		check(false, "broker lines: 16384-byte line cap");
		check(false, "game-info logging: bounded printable field");
		check(false, "game-info confinement: sender-only embedded route");
		check(false, "protocol 2 metadata: host request with server-derived players omitted");
#endif
	}
	{
		const std::string pem = "-----BEGIN STEAMDATAGRAM CERT-----\r\nAQID\nBAU=\n-----END STEAMDATAGRAM CERT-----\n";
		std::vector<std::uint8_t> body;
		check(pem_body(pem, body) && body == std::vector<std::uint8_t>{1, 2, 3, 4, 5}, "certificate: the PEM body across lines");
		std::uint8_t seed[seed_size];
		for (int i = 0; i < 32; ++i) seed[i] = std::uint8_t(i);
		const auto blob = certificate_blob(body, seed);
		check(blob.size() == 5 + 2 + 32 && blob[5] == 0x0a && blob[6] == 32 && blob[7] == 0 && blob.back() == 31, "certificate: private_key_data (field 1, 32 bytes) appended");
		check(!pem_body("no armour", body), "certificate: no PEM, no body");
	}
	{
		const auto s = ice({"stun:203.0.113.5:3478", "turn:203.0.113.5:3478?transport=udp", "turn:203.0.113.5:3478?transport=tcp", "http://x"}, "1760000000:legends:a", "pw==");
		check(s.stun == "203.0.113.5:3478" && s.turn == "203.0.113.5:3478" && s.users == "1760000000:legends:a" && s.passwords == "pw==", "ICE: one STUN and one TURN server from the broker's URIs");
	}
	{
		const std::uint8_t data[] = {0xfe, 0xfd, 0x00, 0x01};
		const auto f = gns_transport_core::datagram(5165, 6000, data, sizeof(data));
		const auto p = gns_transport_core::parse(f.data(), f.size());
		check(p.valid && p.k == gns_transport_core::kind::datagram && p.port == 5165 && p.to_port == 6000 && p.length == 4 && std::memcmp(p.body, data, 4) == 0,
		      "tunnel framing: kind 4, both game ports, the datagram as sent");
		const auto empty = gns_transport_core::datagram(1, 2, nullptr, 0);
		const auto q = gns_transport_core::parse(empty.data(), empty.size());
		check(q.valid && q.length == 0 && !gns_transport_core::parse(empty.data(), 4).valid, "tunnel framing: an empty datagram is valid, a short header is not");
	}
	{
		// Credentials kept between starts: the certificate (24 h, reused with more than an hour left) and the TURN
		// credentials (6 h, reused with more than 30 minutes left, same broker, same TURN secret id).
		const long long now = 1800000000;
		const std::string fresh = "{\"op\":\"turn_ok\",\"username\":\"1800021600:legends:0011223344556677\",\"password\":\"c2VjcmV0\",\"ttl\":21600,"
			"\"expires\":1800021600,\"turn_key\":\"k1\",\"uris\":[\"stun:10.0.0.5:3478\",\"turn:10.0.0.5:3478?transport=udp\",\"turn:10.0.0.5:3478?transport=tcp\"]}";
		const auto saved = online_room_core::with_fields(fresh, {{"cached_for", "broker.example:10041"}});
		const auto replay = online_room_core::parse(online_room_core::with_fields(saved, {{"from_cache", "1"}}));
		const auto original = online_room_core::parse(fresh);
		check(replay.valid && replay.text("from_cache") == "1" && replay.text("cached_for") == "broker.example:10041" && replay.text("password") == "c2VjcmV0" &&
		      replay["uris"].items == original["uris"].items && replay.number("expires") == 1800021600, "TURN cache: a saved and replayed turn_ok keeps every field and is marked");
		const auto a = online_room_core::ice(replay["uris"].items, replay.text("username"), replay.text("password"));
		const auto b = online_room_core::ice(original["uris"].items, original.text("username"), original.text("password"));
		check(a.stun == b.stun && a.turn == b.turn && a.users == b.users && a.passwords == b.passwords, "TURN cache: the replayed line configures GNS exactly as the broker's did");
		check(online_room_core::with_fields("{}", {{"a", "1"}}) == "{\"a\":\"1\"}" && online_room_core::with_fields("x", {{"a", "1"}}) == "x", "with_fields: an empty object takes no comma; a non-object is left alone");
		const auto parsed_saved = online_room_core::parse(saved);
		using online_room_core::turn_cache;
		check(online_room_core::turn_cache_check(parsed_saved, "broker.example:10041", "k1", now) == turn_cache::use, "TURN cache: 6 h credentials of this broker and secret are reused");
		check(online_room_core::turn_cache_check(parsed_saved, "broker.example:10041", "k1", 1800021600 - 1801) == turn_cache::use &&
		      online_room_core::turn_cache_check(parsed_saved, "broker.example:10041", "k1", 1800021600 - 1800) == turn_cache::expiring &&
		      online_room_core::turn_cache_check(parsed_saved, "broker.example:10041", "k1", 1800021600 + 5) == turn_cache::expiring, "TURN cache: reused while MORE than 30 minutes remain, never at or after");
		check(online_room_core::turn_cache_check(parsed_saved, "other.example:10041", "k1", now) == turn_cache::other_broker, "TURN cache: another broker address asks again");
		check(online_room_core::turn_cache_check(parsed_saved, "broker.example:10041", "k2", now) == turn_cache::rotated &&
		      online_room_core::turn_cache_check(parsed_saved, "broker.example:10041", "", now) == turn_cache::rotated, "TURN cache: a rotated (or withdrawn) TURN secret id asks again");
		const auto keyless = online_room_core::parse(online_room_core::with_fields("{\"op\":\"turn_ok\",\"username\":\"1800021600:legends:0011223344556677\",\"password\":\"p\",\"uris\":[\"turn:h:1\"]}", {{"cached_for", "b:1"}}));
		check(online_room_core::turn_cache_check(keyless, "b:1", "", now) == turn_cache::use && online_room_core::turn_expires(keyless) == 1800021600,
		      "TURN cache: a broker without a secret id - expiry from the REST username, reused");
		check(online_room_core::turn_cache_check(online_room_core::parse("{\"op\":\"turn_ok\",\"cached_for\":\"b:1\",\"username\":\"u\",\"uris\":[\"turn:h:1\"]}"), "b:1", "", now) == turn_cache::malformed &&
		      online_room_core::turn_cache_check(online_room_core::parse("not json"), "b:1", "", now) == turn_cache::malformed &&
		      online_room_core::turn_cache_check(online_room_core::parse("{\"op\":\"cert_ok\",\"cached_for\":\"b:1\"}"), "b:1", "", now) == turn_cache::malformed, "TURN cache: a line without password/URIs, garbage or another op is not used");
		check(online_room_core::turn_expires(online_room_core::parse("{\"username\":\"abc:legends:x\"}")) == 0 && online_room_core::turn_expires(online_room_core::parse("{\"username\":\"12345\"}")) == 0,
		      "TURN cache: a username without a numeric expiry gives no expiry (never reused)");
		const auto cert = online_room_core::parse("{\"op\":\"cert_ok\",\"cert_pem\":\"x\",\"expires\":1800003601}");
		check(online_room_core::cert_cache_usable(cert, now) && !online_room_core::cert_cache_usable(cert, now + 1) && !online_room_core::cert_cache_usable(online_room_core::parse("{\"op\":\"turn_ok\",\"expires\":1900000000}"), now),
		      "certificate cache: reused while MORE than an hour remains, only a cert_ok line");
	}
#if XML2_FIX_GNS
	{
		const auto secret = hex("9d61b19deffd5a60ba844af492ec2cc44449c5697b326919703bac031cae7f60");
		const auto expected_public = hex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
		const auto expected_signature = hex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
		std::uint8_t pk[32], sig[64];
		ed25519_publickey(secret.data(), pk);
		ed25519_sign(nullptr, 0, secret.data(), pk, sig);
		check(std::memcmp(pk, expected_public.data(), 32) == 0 && std::memcmp(sig, expected_signature.data(), 64) == 0, "Ed25519 (the GNS library's): RFC 8032 test 1 key and signature");
		check(ed25519_sign_open(nullptr, 0, pk, sig) == 0, "Ed25519: the signature verifies");
	}
#endif
	{
		using online_room_core::room_guard_refusal;
		check(room_guard_refusal(true, true, false) == nullptr && room_guard_refusal(false, true, true) == nullptr,
		      "rooms: a host needs the receive validation, a joiner also the checked campaign load");
		check(room_guard_refusal(true, false, true) != nullptr && room_guard_refusal(false, false, true) != nullptr,
		      "rooms: refused (host and joiner) when the receive validation is not installed");
		check(room_guard_refusal(false, true, false) != nullptr && std::strstr(room_guard_refusal(false, true, false), "[Save] Format") != nullptr,
		      "rooms: a joiner without the checked campaign load is refused, naming [Save] Format");
	}
	std::printf("%d PASS / %d FAIL\n", passes, failures);
	return failures ? 1 : 0;
}
