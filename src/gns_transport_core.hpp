#pragma once

// [Online] Transport=gns (docs/gns-transport.md): the game-independent half of the GameNetworkingSockets adapter -
// the per-peer outbound buffer (whole messages, the native coalesce rule, the reliable bytes GNS still holds), the
// per-peer inbound queue (arrival order, the dispatcher's return-2 deferral) and the link framing. No Windows, GNS
// or game calls here, so test/gns_transport_test.cpp drives it directly.

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <deque>
#include <vector>

namespace gns_transport_core
{
	inline bool hello_due(bool connector, bool bound, std::uint32_t now, std::uint32_t last)
	{
		return connector && !bound && (last == 0 || now - last >= 250);
	}
	// One whole game message: the native header's opcode (+0x10), reliable flag (+0xe) and body (+0, length +8).
	struct message
	{
		std::uint8_t opcode = 0;
		bool reliable = false;
		std::vector<std::uint8_t> body;
	};

	// The fragment opcodes of the native 300-byte path (descriptor 0x12, chunk 0x13; 0x6101b0 dereferences null at
	// 0x61020c for a chunk of a stream it never opened). Whole messages never need them: refused both ways.
	constexpr bool fragment_opcode(std::uint8_t opcode) { return opcode == 0x12 || opcode == 0x13; }

	// GNS payload framing: one kind byte, then for a game message the opcode and the body.
	enum class kind : std::uint8_t { game = 0, hello = 1, hello_ack = 2, keepalive = 3, datagram = 4, lost_inputs = 5, lost_ready = 6 };
	constexpr std::uint32_t hello_magic = 0x31534e47; // "GNS1"
	constexpr std::size_t hello_size = 1 + 4 + 4 + 2;
	// A stock 0x50a datagram carries at most a 0x505 reliable body (opcode, length and sequence excluded).
	constexpr std::size_t max_game_body = 0x505, max_datagram_body = 1500;

#define GNS_TRANSPORT_OPCODE_VALIDATOR 1
	using opcode_length_validator = bool(*)(std::uint8_t opcode, std::size_t length);
	inline opcode_length_validator& validator_slot()
	{
		static opcode_length_validator hook = nullptr;
		return hook;
	}
	inline void set_opcode_length_validator(opcode_length_validator hook) { validator_slot() = hook; }
	inline bool validate_opcode_length(std::uint8_t opcode, std::size_t length)
	{
		if (fragment_opcode(opcode) || length > max_game_body) return false;
		const auto hook = validator_slot();
		return !hook || hook(opcode, length);
	}

	inline std::vector<std::uint8_t> frame(const message& m)
	{
		std::vector<std::uint8_t> out(2 + m.body.size());
		out[0] = std::uint8_t(kind::game);
		out[1] = m.opcode;
		if (!m.body.empty()) std::memcpy(out.data() + 2, m.body.data(), m.body.size());
		return out;
	}
	// The hello names the sender's stock socket (address in network order, port in host order), so the accepting
	// side maps the connection onto the native peer of that address and port; the acknowledgement echoes the
	// acceptor's own.
	inline std::vector<std::uint8_t> hello(kind k, std::uint32_t address, std::uint16_t port)
	{
		std::vector<std::uint8_t> out(hello_size);
		out[0] = std::uint8_t(k);
		std::memcpy(out.data() + 1, &hello_magic, 4);
		std::memcpy(out.data() + 5, &address, 4);
		std::memcpy(out.data() + 9, &port, 2);
		return out;
	}
	// [Online] Host/JoinCode (docs/online-rooms.md): one stock-socket datagram carried unreliably through a P2P
	// connection (the pre-admission tunnel) - the sender's and the receiver's game ports, then the datagram as sent.
	constexpr std::size_t datagram_header = 1 + 2 + 2;
	inline std::vector<std::uint8_t> datagram(std::uint16_t from_port, std::uint16_t to_port, const void* data, std::size_t size)
	{
		std::vector<std::uint8_t> out(datagram_header + size);
		out[0] = std::uint8_t(kind::datagram);
		std::memcpy(out.data() + 1, &from_port, 2);
		std::memcpy(out.data() + 3, &to_port, 2);
		if (size) std::memcpy(out.data() + datagram_header, data, size);
		return out;
	}
	// A participant lost abruptly with three or more players (framing revision 3): every survivor sends the others the
	// lost one's last input records it holds (lost_inputs: its session index, the first record's frame, the records
	// as the native input ring holds them - 0x24 bytes each, the first two the record's frame), then, once its
	// lockstep has run every frame it has that player's input for and waits, the lost one's next frame (lost_ready).
	// Every survivor removes the lost player only when all survivors name the same next frame.
	// lost_inputs_max: what a message may carry (and the parser accepts); a sender sends at most lost_inputs_at_30
	// records at 30 fps and the same ~2.1 s at the session's [Online] TickRate (128 at 60, online_tick::frames_for).
	constexpr std::size_t input_record_size = 0x24, lost_inputs_max = 128, lost_inputs_at_30 = 64, lost_inputs_header = 1 + 1 + 2 + 1, lost_ready_size = 1 + 1 + 2;
	constexpr std::uint8_t lost_index_max = 3; // the engine's four participant slots
	inline std::vector<std::uint8_t> lost_inputs(std::uint8_t index, std::uint16_t first, const std::uint8_t* records, std::size_t count)
	{
		if (count > lost_inputs_max) count = lost_inputs_max;
		std::vector<std::uint8_t> out(lost_inputs_header + count * input_record_size);
		out[0] = std::uint8_t(kind::lost_inputs);
		out[1] = index;
		std::memcpy(out.data() + 2, &first, 2);
		out[4] = std::uint8_t(count);
		if (count) std::memcpy(out.data() + lost_inputs_header, records, count * input_record_size);
		return out;
	}
	inline std::vector<std::uint8_t> lost_ready(std::uint8_t index, std::uint16_t next_frame)
	{
		std::vector<std::uint8_t> out(lost_ready_size);
		out[0] = std::uint8_t(kind::lost_ready);
		out[1] = index;
		std::memcpy(out.data() + 2, &next_frame, 2);
		return out;
	}
	// The native input ring (synchronizer 0x79fbb0, one block per session index): 300 records; the write index is the
	// slot of the next expected frame. The slot of an earlier frame, or -1 when it is not among the last 299.
	constexpr int input_ring_records = 300;
	inline int ring_slot(int write, std::uint16_t next_frame, std::uint16_t frame)
	{
		const int back = std::uint16_t(next_frame - frame);
		if (back < 1 || back >= input_ring_records || write < 0 || write >= input_ring_records) return -1;
		return (write - back + input_ring_records) % input_ring_records;
	}
	// The agreement on a lost participant's cut frame (gns_transport.cpp, vanished_links). A survivor announces its next
	// frame for the lost one once its lockstep waits with that player's ring empty (it ran every frame it has input
	// for) and every other survivor's records arrived (or lost_inputs_wait_ms passed); it removes the player when every
	// other survivor announced the same frame (or lost_agree_timeout_ms passed: logged as not agreed - the native host
	// watchdog would remove the player 10 s after its last message anyway). Live D6/D10: a joiner that was behind can
	// stay at its frame until the host's native 5 s not-responding exchange (0x5c/0x5d/0x5e) and announce ~7 s after
	// the loss, so the timeout waits 8.5 s (still before that watchdog).
	constexpr std::uint32_t lost_inputs_wait_ms = 3000, lost_agree_timeout_ms = 8500;
	struct survivor_view { bool inputs = false; int ready = -1; };
	inline bool may_announce(bool waiting, bool ring_empty, bool all_inputs, std::uint32_t elapsed)
	{
		return waiting && ring_empty && (all_inputs || elapsed >= lost_inputs_wait_ms);
	}
	enum class lost_step { wait, remove_agreed, remove_timeout };
	inline lost_step lost_decision(int announced, const survivor_view* others, std::size_t count, std::uint32_t elapsed)
	{
		bool agreed = announced >= 0;
		for (std::size_t i = 0; agreed && i < count; ++i) agreed = others[i].ready == announced;
		if (agreed) return lost_step::remove_agreed;
		return elapsed >= lost_agree_timeout_ms ? lost_step::remove_timeout : lost_step::wait;
	}
	struct parsed
	{
		bool valid = false;
		kind k = kind::game;
		std::uint8_t opcode = 0;
		const std::uint8_t* body = nullptr;
		std::size_t length = 0;
		std::uint32_t address = 0;
		std::uint16_t port = 0;
		std::uint16_t to_port = 0; // datagram only
		std::uint8_t index = 0;    // lost_inputs / lost_ready: the lost participant's session index
		std::uint16_t frame = 0;   // lost_inputs: the first record's frame; lost_ready: the next frame
		std::size_t count = 0;     // lost_inputs: records (body = the records)
	};
	inline parsed parse(const std::uint8_t* data, std::size_t size)
	{
		parsed p;
		if (!data || size < 1) return p;
		p.k = kind(data[0]);
		if (p.k == kind::game)
		{
			if (size < 2 || size - 2 > max_game_body) return p;
			p.opcode = data[1];
			p.body = data + 2;
			p.length = size - 2;
			p.valid = true;
		}
		else if (p.k == kind::keepalive && size == 1)
			p.valid = true;
		else if (p.k == kind::datagram && size >= datagram_header && size - datagram_header <= max_datagram_body)
		{
			std::memcpy(&p.port, data + 1, 2);
			std::memcpy(&p.to_port, data + 3, 2);
			p.body = data + datagram_header;
			p.length = size - datagram_header;
			p.valid = true;
		}
		else if (p.k == kind::lost_inputs && size >= lost_inputs_header)
		{
			p.index = data[1];
			std::memcpy(&p.frame, data + 2, 2);
			p.count = data[4];
			p.body = data + lost_inputs_header;
			p.length = size - lost_inputs_header;
			bool ok = p.index <= lost_index_max && p.count <= lost_inputs_max && p.length == p.count * input_record_size; // 0: holds none
			// Each record carries its own frame: consecutive from the first.
			for (std::size_t i = 0; ok && i < p.count; ++i)
			{
				std::uint16_t f = 0;
				std::memcpy(&f, p.body + i * input_record_size, 2);
				ok = f == std::uint16_t(p.frame + i);
			}
			p.valid = ok;
		}
		else if (p.k == kind::lost_ready && size == lost_ready_size)
		{
			p.index = data[1];
			std::memcpy(&p.frame, data + 2, 2);
			p.valid = p.index <= lost_index_max;
		}
		else if ((p.k == kind::hello || p.k == kind::hello_ack) && size == hello_size)
		{
			std::uint32_t magic = 0;
			std::memcpy(&magic, data + 1, 4);
			std::memcpy(&p.address, data + 5, 4);
			std::memcpy(&p.port, data + 9, 2);
			p.valid = magic == hello_magic;
		}
		return p;
	}

#define GNS_TRANSPORT_TUNNEL_POLICY 1
	inline bool tunnel_message_allowed(bool transport_bound, const parsed& p)
	{
		if (!p.valid) return false;
		if (p.k == kind::datagram) return true;
		if (!transport_bound) return p.k == kind::hello || p.k == kind::hello_ack;
		return true;
	}
	constexpr bool create_ip_listener(bool room_requested) { return !room_requested; }
	constexpr bool stray_allowed(std::size_t count, bool same_remote_ip) { return count < 8 && !same_remote_ip; }

	// GNS listens on the stock port + offset (below the stock port when that would pass 65535): both sides compute it
	// from the stock port alone.
	constexpr std::uint16_t gns_port(std::uint16_t stock, std::uint16_t offset)
	{
		return std::uint16_t(stock <= 0xffff - offset ? stock + offset : stock - offset);
	}
	// Exactly one side of a pair connects: the one whose stock socket (port, then address) is lower. Ports first: behind
	// a port-preserving NAT both sides see the same two ports but different addresses (each its own private one and
	// the other's public one), so an address-first rule made both sides connect (live, step 4b proxy).
	constexpr bool connects(std::uint32_t own_address, std::uint16_t own_port, std::uint32_t peer_address, std::uint16_t peer_port)
	{
		return own_port != peer_port ? own_port < peer_port : own_address < peer_address;
	}

	class outbox
	{
		std::deque<message> pending_; // accepted from the game, not yet handed to GNS
		struct flight { std::uint8_t opcode; std::size_t bytes; };
		std::deque<flight> flight_;   // reliable messages handed to GNS, oldest first
		std::size_t flight_bytes_ = 0, pending_bytes_ = 0, byte_limit_, count_limit_;
	public:
		explicit outbox(std::size_t byte_limit = 2 * 1024 * 1024, std::size_t count_limit = 8192) : byte_limit_(byte_limit), count_limit_(count_limit) {}
		// The native enqueue's coalesce rule (0x60f66d..0x60f6ed): with header +0xf set, a queued reliable message of
		// the same opcode is dropped and the new one goes to the back - at most one queued per opcode, the newest wins.
		// GNS cannot recall a message it already holds, so the rule covers what this buffer still has.
		bool enqueue(std::uint8_t opcode, bool reliable, bool coalesce, const std::uint8_t* body, std::size_t length)
		{
			if ((!body && length) || length > max_game_body) return false;
			std::size_t replaced = 0, replaced_bytes = 0;
			if (coalesce)
				for (const auto& m : pending_)
					if (m.reliable && m.opcode == opcode) { ++replaced; replaced_bytes += 2 + m.body.size(); }
			const auto kept_bytes = pending_bytes_ - replaced_bytes, kept_count = pending_.size() - replaced;
			const auto framed = 2 + length;
			if (kept_count >= count_limit_ || framed > byte_limit_ || kept_bytes > byte_limit_ - framed) return false;
			if (coalesce)
			{
				pending_.erase(std::remove_if(pending_.begin(), pending_.end(), [&](const message& m) { return m.reliable && m.opcode == opcode; }), pending_.end());
				pending_bytes_ -= replaced_bytes;
			}
			message m;
			m.opcode = opcode;
			m.reliable = reliable;
			if (length) m.body.assign(body, body + length);
			pending_.push_back(std::move(m));
			pending_bytes_ += framed;
			return true;
		}
		// Hands pending messages to `send` in order (send(const message&, framed bytes) -> true when GNS took it); the
		// first refusal (GNS's send buffer is full) stops and keeps it and everything after it for the next tick.
		template<class Send> std::size_t flush(Send&& send)
		{
			std::size_t sent = 0;
			while (!pending_.empty())
			{
				const auto bytes = frame(pending_.front());
				if (!send(pending_.front(), bytes)) break;
				if (pending_.front().reliable)
				{
					flight_.push_back({pending_.front().opcode, bytes.size()});
					flight_bytes_ += bytes.size();
				}
				pending_bytes_ -= bytes.size();
				pending_.pop_front();
				++sent;
			}
			return sent;
		}
		// GNS's reliable bytes not yet acknowledged (pending + sent-unacked). Its count includes GNS's own per-message
		// framing, so a message is retired only once every byte after it covers the outstanding count: the backlog is
		// never under-reported.
		void outstanding(std::size_t bytes)
		{
			while (!flight_.empty() && flight_bytes_ - flight_.front().bytes >= bytes)
			{
				flight_bytes_ -= flight_.front().bytes;
				flight_.pop_front();
			}
		}
		// The send half of the native queue query 0x610790: messages of `opcode` not yet delivered to the peer.
		std::uint32_t count(std::uint8_t opcode) const
		{
			std::uint32_t n = 0;
			for (const auto& m : pending_) n += m.opcode == opcode;
			for (const auto& f : flight_) n += f.opcode == opcode;
			return n;
		}
		std::size_t pending() const { return pending_.size(); }
		std::size_t pending_bytes() const { return pending_bytes_; }
		std::size_t in_flight() const { return flight_.size(); }
		void clear() { pending_.clear(); flight_.clear(); flight_bytes_ = 0; pending_bytes_ = 0; }
	};

	class inbox
	{
		std::deque<message> queue_;
		std::size_t limit_, byte_limit_, bytes_ = 0;
	public:
		explicit inbox(std::size_t limit = 8192, std::size_t byte_limit = 1024 * 1024) : limit_(limit), byte_limit_(byte_limit) {}
		// False when either queue bound is exhausted: the caller ends the link instead of retaining attacker bytes.
		bool push(message m)
		{
			if (queue_.size() >= limit_ || m.body.size() > byte_limit_ || bytes_ > byte_limit_ - m.body.size()) return false;
			bytes_ += m.body.size();
			queue_.push_back(std::move(m));
			return true;
		}
		// Delivers in arrival order through `deliver` (the native dispatcher's result); 2 = the handler deferred: the
		// message stays at the head and nothing after it is delivered before a later call (0x60ff00's stop).
		template<class Deliver> std::size_t drain(Deliver&& deliver)
		{
			std::size_t n = 0;
			while (!queue_.empty())
			{
				if (deliver(queue_.front()) == 2) break;
				bytes_ -= queue_.front().body.size();
				queue_.pop_front();
				++n;
			}
			return n;
		}
		// The receive half of 0x610790: reliable messages of `opcode` received but not yet consumed.
		std::uint32_t count(std::uint8_t opcode) const
		{
			std::uint32_t n = 0;
			for (const auto& m : queue_) n += m.reliable && m.opcode == opcode;
			return n;
		}
		std::size_t size() const { return queue_.size(); }
		std::size_t bytes() const { return bytes_; }
		void clear() { queue_.clear(); bytes_ = 0; }
	};
}
