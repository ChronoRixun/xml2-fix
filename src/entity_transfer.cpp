#include "entity_transfer.hpp"
#include "entity_capacity_layout.hpp"

#include <cstring>
#include <atomic>
#include <Windows.h>

namespace entity_transfer
{
	using namespace entity_capacity_rules;
	using entity_capacity_layout::native;

	namespace
	{
		const std::uint32_t native_reliable_continue = 0x6100ed;
		const std::uint32_t native_outgoing_continue = 0x60f647;
		const std::uint32_t native_sender_resume = 0x6080ba;
		std::uint32_t fast_payload = e10a2_transfer.payload;
		std::atomic<std::uint32_t> fast_sent{0};
		std::atomic<std::uint32_t> deferred{0};
		backpressure_observer pressure_observer = nullptr;
		failure_observer failed_observer = nullptr;
		prepare_observer prepared_observer = nullptr;
		bool failure_pending = false;
		struct waiting_peer { std::uint8_t* peer = nullptr; pressure_deadline deadline; };
		std::array<waiting_peer, 6> waiting{}; // native transport has at most six peer objects
		message_pools pool_usage()
		{
			const auto* cache = static_cast<const std::uint8_t*>(native<void*(__cdecl*)()>(0x60f240)());
			const auto* nodes = static_cast<const std::uint8_t*>(native<void*(__cdecl*)()>(0x6067d0)());
			return {*reinterpret_cast<const std::uint32_t*>(cache + 0x149a8),
			        *reinterpret_cast<const std::uint32_t*>(cache + 0x48d0),
			        *reinterpret_cast<const std::uint32_t*>(cache + 0x10948),
			        *reinterpret_cast<const std::uint32_t*>(nodes + 0x3da8)};
		}
		void fail_session(const char* reason, message_pools used)
		{
			if (failure_pending) return;
			failure_pending = true;
			if (failed_observer) failed_observer(reason, used);
		}
		void track_pressure(std::uint8_t* peer, std::uint16_t expected)
		{
			for (auto& w : waiting) if (w.peer == peer)
			{
				w.deadline.block(expected, GetTickCount64());
				return;
			}
			for (auto& w : waiting) if (!w.peer)
			{
				w.peer = peer;
				w.deadline.block(expected, GetTickCount64());
				return;
			}
			fail_session("pressure tracking capacity exhausted", pool_usage());
		}
		bool __cdecl reliable_prepare(std::uint8_t* event)
		{
			const auto length = *reinterpret_cast<const std::uint16_t*>(event + 8);
			// Keep the native malformed-length handling. This guard concerns valid messages that cannot fit now.
			if (length < 2 || length > 327) return true;
			const auto* body = *reinterpret_cast<const std::uint8_t**>(event + 4);
			auto* peer = *reinterpret_cast<std::uint8_t**>(event + 0x10);
			const auto sequence = *reinterpret_cast<const std::uint16_t*>(body + 1);
			const auto expected = *reinterpret_cast<const std::uint16_t*>(peer + 0x308c);
			const auto used = pool_usage();
			if (!failure_pending && reliable_queue_room(expected, sequence, length - 2u, used)) return true;
			// Return 2 stops 0x60dca0 before the trailing 0x0a ACK request. Discard this pending datagram batch;
			// already queued messages remain intact and duplicates on retransmission use the native sequence checks.
			*reinterpret_cast<std::uint8_t**>(peer + 0x1e98) = peer + 0x52e;
			deferred.fetch_add(1);
			track_pressure(peer, expected);
			if (pressure_observer && !failure_pending) pressure_observer(expected, sequence, used);
			return false;
		}
		bool __cdecl outgoing_prepare(const std::uint8_t* header)
		{
			if (failure_pending) return false;
			auto bytes = *reinterpret_cast<const std::uint16_t*>(header + 8);
			const bool has_body = *reinterpret_cast<const void* const*>(header) != nullptr;
			// 0x615840 resolves fixed-size bodies when the caller leaves the length zero.
			if (has_body && !bytes) bytes = reinterpret_cast<const std::uint16_t*>(0xa3e490)[header[0x10]];
			const auto used = pool_usage();
			if (outgoing_queue_room(bytes, has_body, used)) return true;
			fail_session("outgoing message cannot fit while preserving receive capacity", used);
			return false;
		}
		const std::uint32_t native_615810 = 0x615810, native_55d7e0 = 0x55d7e0, native_612be0 = 0x612be0, native_610f30 = 0x610f30;
		using free_buffer_t = void(__cdecl*)(void*);
		using pool_alloc_t = void*(__cdecl*)(std::uint32_t, std::uint32_t);
		using difficulty_getter_t = void*(__cdecl*)();
		using difficulty_t = void(__fastcall*)(void*, void*, std::uint32_t);
	}

	// The retail chunk send 0x608068..0x60808e: session->send(endpoint of the peer's connection (0x55d7e0), header,
	// flags 0), the session from the argument-free getter 0x612be0. Same ABIs as send_ack below.
	__declspec(naked) void send_record(void* /*connection*/, std::uint8_t* /*header*/)
	{
		__asm
		{
			push ebp
			mov ebp, esp
			push esi
			push edi
			push ebx
			push 0
			push dword ptr [ebp + 0xc]
			mov ecx, dword ptr [ebp + 8]
			call dword ptr [native_55d7e0]
			push eax
			call dword ptr [native_612be0]
			mov ecx, eax
			call dword ptr [native_610f30]
			pop ebx
			pop edi
			pop esi
			pop ebp
			ret
		}
	}

	void set_fast_payload(std::uint32_t payload) { fast_payload = payload; }
	std::uint32_t fast_records_sent() { return fast_sent.load(); }

	// One peer's turn in the native sender: its cursor (context +0x2240 + index*4) walks the image as fast records.
	// The outgoing preflight still guards every send; a refusal there fails the session and stops this loop.
	std::uint32_t __cdecl fast_send_peer(std::uint8_t* context, std::uint8_t* peer, void* connection, std::uint32_t outstanding)
	{
		auto* image = *reinterpret_cast<std::uint8_t**>(context + 0x2250);
		const auto index = native<std::uint32_t(__thiscall*)(void*)>(0x614210)(peer);
		if (!image || index >= 4) return 0;
		auto** cursor = reinterpret_cast<std::uint8_t**>(context + 0x2240 + index * 4);
		std::uint32_t queued = 0;
		for (auto n = fast_sends(outstanding, pool_usage()); n && !failure_pending; --n)
		{
			const auto at = static_cast<std::uint32_t>(*cursor - image);
			if (at >= fast_payload) break;
			const auto record = fast_next(image, at, fast_payload);
			std::array<std::uint8_t, 1 + fast_data_max> body{};
			body[0] = record.tag;
			std::uint16_t length = 5;
			if (record.tag == fast_zero_tag) std::memcpy(body.data() + 1, &record.covered, 4);
			else { std::memcpy(body.data() + 1, image + at, record.covered); length = static_cast<std::uint16_t>(1 + record.covered); }
			std::array<std::uint8_t, 0x14> header{};
			native<void(__thiscall*)(void*)>(0x615810)(header.data());
			*reinterpret_cast<std::uint8_t**>(header.data()) = body.data();
			*reinterpret_cast<std::uint16_t*>(header.data() + 8) = length;
			header[0xe] = 1;
			header[0x10] = 'H';
			send_record(connection, header.data());
			if (failure_pending) break;
			*cursor += record.covered;
			++queued;
		}
		fast_sent.fetch_add(queued);
		return queued;
	}

	// At 0x608004 EAX holds the peer's queued 'H' count, EBP the peer, [esp+0x18] its connection, [esp+0x38] the
	// campaign context. 0x6080ba reloads everything it uses from the stack (next peer or the completion checks).
	__declspec(naked) void fast_sender()
	{
		__asm
		{
			push eax
			push dword ptr [esp + 0x1c]
			push ebp
			push dword ptr [esp + 0x44]
			call fast_send_peer
			add esp, 16
			jmp dword ptr [native_sender_resume]
		}
	}

	void observe_backpressure(backpressure_observer o) { pressure_observer = o; }
	void observe_failure(failure_observer o) { failed_observer = o; }
	void observe_prepare(prepare_observer o) { prepared_observer = o; }
	std::uint32_t backpressure_count() { return deferred.load(); }

	// Replaces the scheduler's maintenance callback, 0x60a880. No parser, ordered-drain or sender
	// frame is active here. In particular, never clean up a session from inside a reply's send:
	// ordered delivery still owns that received header/body until its application handler returns.
	void __cdecl transport_maintenance(void* managers)
	{
		auto* list = static_cast<std::uint8_t*>(managers);
		std::uint8_t* manager = nullptr;
		if (list && *reinterpret_cast<std::uint32_t*>(list + 0xc))
		{
			auto* node = *reinterpret_cast<std::uint8_t**>(list);
			*reinterpret_cast<void**>(list + 8) = *reinterpret_cast<void**>(node + 8);
			manager = *reinterpret_cast<std::uint8_t**>(node + 0x10);
		}
		for (auto& w : waiting) if (w.peer)
		{
			// Validate membership before reading the saved pointer; transport/peer lifetimes can end.
			bool member = false;
			auto* node = manager ? *reinterpret_cast<std::uint8_t**>(manager + 0x124b8) : nullptr;
			for (unsigned i = 0; node && i < 6; ++i, node = *reinterpret_cast<std::uint8_t**>(node + 8))
				if (*reinterpret_cast<void**>(node + 0x10) == w.peer) member = true;
			if (!member) { w = {}; continue; }
			if (w.deadline.expired(*reinterpret_cast<std::uint16_t*>(w.peer + 0x308c), GetTickCount64()))
				fail_session("reliable receive made no progress for 30 seconds under pool pressure", pool_usage());
			if (!w.deadline.active) w = {};
		}
		if (failure_pending && !manager)
		{
			// The transport already ended by another path: nothing to fail, and a stale flag must not
			// refuse the next session's traffic.
			waiting = {};
			failure_pending = false;
			return;
		}
		if (failure_pending)
		{
			// The native connection-failure event reports failure to the current campaign callback,
			// releases transfer/peer queues, and returns to the menu. No outgoing refusal allocation.
			std::array<std::uint8_t, 0x1c> event{};
			event[0] = 0x6e;
			*reinterpret_cast<void**>(event.data() + 0xc) = native<void*(__cdecl*)()>(0x6097b0)();
			native<int(__cdecl*)(void*)>(0x609ad0)(event.data());
			waiting = {};
			failure_pending = false;
			return;
		}
		if (manager) native<void(__thiscall*)(void*)>(0x60f0a0)(manager);
	}

	__declspec(naked) void outgoing_room()
	{
		__asm
		{
			pushfd
			pushad
			push edi
			call outgoing_prepare
			add esp, 4
			test al, al
			jz refuse_send
			popad
			popfd
			mov cl, byte ptr [edi + 0xe]
			test cl, cl
			jmp dword ptr [native_outgoing_continue]
		refuse_send:
			popad
			popfd
			pop edi
			pop ebx
			add esp, 8
			xor eax, eax
			ret 8
		}
	}

	__declspec(naked) void reliable_receive_room()
	{
		__asm
		{
			pushfd
			pushad
			push esi
			call reliable_prepare
			add esp, 4
			test al, al
			jz defer_packet
			popad
			popfd
			push ebx
			mov bx, word ptr [esi + 8]
			jmp dword ptr [native_reliable_continue]
		defer_packet:
			popad
			popfd
			pop edi
			pop esi
			add esp, 8
			mov eax, 2
			ret
		}
	}

	// The retail acknowledgement 0x60872c..0x60875a on a 0x14-byte local header: the header is zeroed by 0x615810 with
	// ECX = the header (a thiscall that clears bytes 0..0x11 except 0x10); ECX then becomes the event's sender (+0x10);
	// two of the three arguments of 0x610f30 (flags 0, then the header) are pushed first; opcode 0x4a goes to +0x10 and
	// 1 to +0xe; 0x55d7e0 returns the sender's endpoint (+4, no stack arguments) and that endpoint is pushed as the
	// send's FIRST argument; 0x612be0 is the cdecl, argument-free getter of the session static 0xa53058 (it leaves the
	// pushed endpoint alone); 0x610f30 is the session's thiscall send(endpoint, header, flags) and cleans all three
	// (ret 0xc). The sequence below is byte-for-byte that order, so the stack balances exactly as in retail.
	__declspec(naked) void send_ack(std::uint8_t* /*event*/)
	{
		__asm
		{
			push ebp
			mov ebp, esp
			sub esp, 0x14
			push esi
			push edi
			lea ecx, [ebp - 0x14]
			call dword ptr [native_615810]
			mov eax, dword ptr [ebp + 8]
			mov ecx, dword ptr [eax + 0x10]
			push 0
			lea edx, [ebp - 0x14]
			push edx
			mov byte ptr [ebp - 0x14 + 0x10], 0x4a
			mov byte ptr [ebp - 0x14 + 0x0e], 1
			call dword ptr [native_55d7e0]
			push eax
			call dword ptr [native_612be0]
			mov ecx, eax
			call dword ptr [native_610f30]
			pop edi
			pop esi
			mov esp, ebp
			pop ebp
			ret
		}
	}

	std::uint32_t receive(std::uint8_t* event, const transfer_profile& profile, state& s, const observer& o)
	{
		auto* context = *reinterpret_cast<std::uint8_t**>(event + 0xc);
		const auto opcode = event[0];
		if (opcode == 'G')
		{
			if (prepared_observer) prepared_observer(event);
			s.chunks = 0;
			s.received = 0;
			if (const auto callback = *reinterpret_cast<void(__cdecl**)(int, std::uint32_t, int)>(context + 0x2298))
				callback(7, *reinterpret_cast<std::uint32_t*>(context + 0x229c), 0);
			if (auto* old = *reinterpret_cast<void**>(context + 0x2250))
			{
				native<free_buffer_t>(0x560700)(old);
				*reinterpret_cast<void**>(context + 0x2250) = nullptr;
			}
			if (context[0x2294]) context[0x2294] = 0;
			auto* wrapper = static_cast<std::uint8_t*>(native<pool_alloc_t>(0x5606e0)(profile.wrapper, 0x2d));
			if (wrapper)
			{
				wrapper[profile.description] = 0;
				wrapper[profile.flag] = 0;
				*reinterpret_cast<std::uint8_t**>(wrapper + profile.payload) = wrapper;
			}
			else if (o.no_wrapper) o.no_wrapper(profile.wrapper);
			*reinterpret_cast<std::uint8_t**>(context + 0x2250) = wrapper;
			const auto* body = *reinterpret_cast<const std::uint32_t**>(event + 4);
			auto* game = native<difficulty_getter_t>(0x46dce0)();
			auto** vt = *reinterpret_cast<void***>(game);
			reinterpret_cast<difficulty_t>(vt[0x26c / 4])(game, nullptr, body[0]);
			context[0x2294] = static_cast<std::uint8_t>(body[1]);
			return 0;
		}
		if (opcode == 'H')
		{
			const auto length = *reinterpret_cast<const std::uint16_t*>(event + 8);
			auto* wrapper = *reinterpret_cast<std::uint8_t**>(context + 0x2250);
			if (s.fast)
			{
				const auto* record = *reinterpret_cast<const std::uint8_t**>(event + 4);
				std::uint32_t covered = 0;
				const auto verdict = fast_chunk(s.received, record, length, profile.payload, wrapper != nullptr, covered);
				if (verdict != fast_verdict::accept && verdict != fast_verdict::complete)
				{
					if (o.fast_refused) o.fast_refused(verdict, s.received, length, profile.payload);
					return 0;
				}
				// The wrapper comes from the game's pool, not zeroed: a zero record writes its zeros.
				if (record[0] == fast_data_tag) std::memcpy(wrapper + s.received, record + 1, covered);
				else std::memset(wrapper + s.received, 0, covered);
				s.received += covered;
				++s.chunks;
				if (verdict == fast_verdict::complete) send_ack(event);
				return 0;
			}
			const auto verdict = transfer_chunk(s.chunks, length, profile.payload, wrapper != nullptr);
			if (verdict != transfer_verdict::accept && verdict != transfer_verdict::complete)
			{
				if (o.refused) o.refused(verdict, s.chunks, length, profile.payload);
				return 0;
			}
			const auto* body = *reinterpret_cast<const std::uint8_t**>(event + 4);
			std::memcpy(wrapper + s.chunks * transfer_chunk_size, body, length);
			++s.chunks;
			if (verdict == transfer_verdict::complete) send_ack(event);
		}
		return 0;
	}
}
