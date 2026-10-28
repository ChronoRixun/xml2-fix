#pragma once

// Online removal notice (docs/online-party-menu.md section 4, gameplay rule 64 OnlineRemovalNotice): the host tells the
// member it removes in session with the Party screen's Kick, so the removed PC ends its session like a native Kick
// (the retail removal notice 0x2b for its own index, reason 0x10ce, flag 1) and its Hub shows "The host removed you
// from the game." instead of ending silently. Link kind 9 inside GNS framing 4, on the authenticated room tunnel from
// the room's host to that member only. Pure: no Windows, GNS or game calls, no clocks (callers pass milliseconds), so
// test/online_removal_test.cpp drives every byte, both machines and a lossy wire.

#include "online_party_core.hpp"
#include "gameplay_capabilities.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace online_removal_core
{
	// ---- rule 64 OnlineRemovalNotice, revision 1: {on, wire, link kind, 0} (registered with rule 25 GnsTransport) ----
	constexpr std::uint16_t rule_id = 64, rule_revision = 1;
	constexpr std::uint8_t link_kind = 9, wire = 1;
	constexpr std::array<std::uint32_t, 4> rule_effective{1u, wire, link_kind, 0u};
	static_assert(rule_id == gameplay_capabilities::online_removal_notice_id);
	static_assert(link_kind != online_party_core::kind_report && link_kind != online_party_core::kind_table && link_kind != 16, "kind 9 is free (7/8 party, 16 restore)");

	using key = online_party_core::key;
	constexpr std::size_t code_size = online_party_core::code_size, key_size = online_party_core::key_size;
	// kind u8, wire u8, code[8], host key[8], target key[8], index u8, heard u32, serial u32 (little endian).
	constexpr std::size_t message_size = 1 + 1 + code_size + key_size + key_size + 1 + 4 + 4; // 35
	// The host repeats the notice every resend_ms until the removed member's tunnel closes (it left the room after its
	// session ended), at most resend_window_ms; then it closes that tunnel itself ("removed by the host").
	constexpr std::uint32_t resend_ms = 250, resend_window_ms = 10000;
	// The retail removal notice the removed PC's own native Kick handling runs on (0x2b, 12 bytes: index u32, reason u32,
	// flag u8 + padding; online_menus_rules kicked_by_host).
	constexpr std::uint8_t native_opcode = 0x2b;
	constexpr std::uint32_t native_reason = 0x10ce, native_flag = 1;
	constexpr std::size_t native_body_size = 12;
	constexpr std::uint8_t index_min = 1, index_max = 3; // never the host's own index 0

	struct notice
	{
		char code[code_size]{};
		key host{}, target{};
		std::uint8_t index = 0;
		std::uint32_t heard = 0;  // the newest member report sequence (kind 7) the host accepted on this connection
		std::uint32_t serial = 0; // the host's removal serial (per process, never 0); every resend repeats it
	};

	enum class refusal : std::uint8_t
	{
		none, size, kind, wire, code, keys, index, heard, serial, // structure (parse)
		not_host,  // not from the room's host: another member's tunnel, a host key that is not the room's, or this PC hosts
		room,      // not this PC's room (no room, another code)
		target,    // names another PC
		stale,     // heard at or below this connection's floor: an earlier connection's / session's notice
		future,    // heard above any report this PC ever sent: never produced by the host of this connection
		session,   // this PC is not a guest of a native session at that index
		repeat,    // this PC already accepted a removal in this room (a resend): no state change
		count
	};
	inline const char* name(refusal r)
	{
		static const char* const n[] = {"none", "size", "kind", "wire", "code", "keys", "index", "heard", "serial", "not the host", "room", "target", "stale",
		                                "future", "session", "repeat"};
		return r < refusal::count ? n[unsigned(r)] : "?";
	}

	namespace detail
	{
		inline bool zero(const key& k) { for (auto b : k) if (b) return false; return true; }
		inline void put32(std::uint8_t* p, std::uint32_t v) { for (int i = 0; i < 4; ++i) p[i] = std::uint8_t(v >> (8 * i)); }
		inline std::uint32_t get32(const std::uint8_t* p) { return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) | (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24); }
	}

	inline std::vector<std::uint8_t> encode(const notice& n)
	{
		std::vector<std::uint8_t> o(message_size);
		o[0] = link_kind;
		o[1] = wire;
		std::memcpy(o.data() + 2, n.code, code_size);
		std::memcpy(o.data() + 10, n.host.data(), key_size);
		std::memcpy(o.data() + 18, n.target.data(), key_size);
		o[26] = n.index;
		detail::put32(o.data() + 27, n.heard);
		detail::put32(o.data() + 31, n.serial);
		return o;
	}
	// Structure only: exact size, kind, wire, a valid room code, two distinct nonzero keys, a remote index, nonzero
	// heard and serial. Context (room, sender, target, freshness, session) is the receiver's.
	inline refusal parse(const std::uint8_t* d, std::size_t size, notice& out)
	{
		out = {};
		if (!d || size != message_size) return refusal::size;
		if (d[0] != link_kind) return refusal::kind;
		if (d[1] != wire) return refusal::wire;
		std::memcpy(out.code, d + 2, code_size);
		if (!online_party_core::valid_code(out.code)) return refusal::code;
		std::memcpy(out.host.data(), d + 10, key_size);
		std::memcpy(out.target.data(), d + 18, key_size);
		if (detail::zero(out.host) || detail::zero(out.target) || out.host == out.target) return refusal::keys;
		out.index = d[26];
		if (out.index < index_min || out.index > index_max) return refusal::index;
		out.heard = detail::get32(d + 27);
		if (!out.heard) return refusal::heard;
		out.serial = detail::get32(d + 31);
		if (!out.serial) return refusal::serial;
		return refusal::none;
	}

	// The 12-byte body of the retail removal notice for this PC's own index, as the host's native Kick sends it.
	inline std::array<std::uint8_t, native_body_size> native_body(std::uint8_t index)
	{
		std::array<std::uint8_t, native_body_size> b{};
		detail::put32(b.data(), index);
		detail::put32(b.data() + 4, native_reason);
		b[8] = std::uint8_t(native_flag);
		return b;
	}

	// ---- the removed PC (a joiner) ----
	// What the receiver needs to know, all of it this PC's own facts at the moment the message arrives.
	struct guest_view
	{
		bool room = false;               // in a room with a valid code
		bool hosting = false;            // this PC hosts it (a host is never removed)
		const char* code = nullptr;      // the room code (8 chars)
		key self{}, host{};              // this PC's key and the room's host key (broker roster)
		bool from_host_tunnel = false;   // the message arrived on the tunnel whose authenticated identity is the room's host
		std::uint32_t floor = 0;         // this PC's report sequence when its connection to the host came up
		std::uint32_t sent = 0;          // this PC's newest report sequence
		bool session = false;            // a native session runs here as a guest
		int own_index = -1;              // this PC's native index in it
	};
	class receiver
	{
		bool removed_ = false;
		std::uint32_t serial_ = 0;
		unsigned refused_[unsigned(refusal::count)]{};
		refusal count(refusal r) { if (r != refusal::none && r < refusal::count) ++refused_[unsigned(r)]; return r; }
	public:
		// A new room (or none): the latch and the counters start again.
		void reset() { *this = {}; }
		bool removed() const { return removed_; }
		std::uint32_t serial() const { return serial_; }
		unsigned refused(refusal r) const { return r < refusal::count ? refused_[unsigned(r)] : 0; }
		// none: accepted - the first removal of this PC in this room; the caller ends the session (native_body) once.
		refusal on_message(const guest_view& v, const std::uint8_t* d, std::size_t size, notice* out = nullptr)
		{
			notice n;
			if (const auto why = parse(d, size, n); why != refusal::none) return count(why);
			if (out) *out = n;
			// Authentication first: only the room's host, on its own authenticated tunnel, naming itself.
			if (!v.from_host_tunnel || v.hosting || n.host != v.host) return count(refusal::not_host);
			if (!v.room || !v.code || std::memcmp(n.code, v.code, code_size) != 0) return count(refusal::room);
			if (n.target != v.self) return count(refusal::target);
			// Freshness: the host can only know a report sequence this PC sent on THIS connection.
			if (n.heard <= v.floor) return count(refusal::stale);
			if (n.heard > v.sent) return count(refusal::future);
			if (removed_) return count(refusal::repeat);
			if (!v.session || v.own_index != int(n.index)) return count(refusal::session);
			removed_ = true;
			serial_ = n.serial;
			return refusal::none;
		}
	};

	// ---- the host ----
	// One pending notice per removed member (at most three remote members); every resend repeats the same serial.
	class sender
	{
	public:
		struct target
		{
			key who{};
			std::uint8_t index = 0;
			std::uint32_t serial = 0, started = 0, last = 0;
			unsigned sends = 0;
		};
		enum class step { wait, send, expire };
	private:
		std::vector<target> targets_;
		std::uint32_t next_serial_ = 0;
	public:
		void reset() { targets_.clear(); }
		const std::vector<target>& targets() const { return targets_; }
		const target* find(const key& who) const { for (const auto& t : targets_) if (t.who == who) return &t; return nullptr; }
		// False: not a remote index, a zero key, already pending for this member, or three pending.
		bool start(const key& who, std::uint8_t index, std::uint32_t now)
		{
			if (detail::zero(who) || index < index_min || index > index_max || find(who) || targets_.size() >= index_max) return false;
			if (++next_serial_ == 0) next_serial_ = 1;
			targets_.push_back({who, index, next_serial_, now, 0, 0});
			return true;
		}
		// What to do for this member now: send (first at once, then every resend_ms), expire (the window passed), wait.
		step due(const target& t, std::uint32_t now) const
		{
			if (now - t.started >= resend_window_ms) return step::expire;
			if (!t.sends || now - t.last >= resend_ms) return step::send;
			return step::wait;
		}
		void sent(const key& who, std::uint32_t now)
		{
			for (auto& t : targets_) if (t.who == who) { t.last = now; ++t.sends; }
		}
		// Its tunnel closed / it left the roster / the window passed: no more notices.
		void finish(const key& who)
		{
			for (std::size_t i = 0; i < targets_.size(); ++i) if (targets_[i].who == who) { targets_.erase(targets_.begin() + std::ptrdiff_t(i)); return; }
		}
		notice make(const target& t, const char* code, const key& host, std::uint32_t heard) const
		{
			notice n;
			std::memcpy(n.code, code, code_size);
			n.host = host;
			n.target = t.who;
			n.index = t.index;
			n.heard = heard;
			n.serial = t.serial;
			return n;
		}
	};
}
