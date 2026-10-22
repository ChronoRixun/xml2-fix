#include <winsock2.h>
#include "net_impair.hpp"
#include "iat_hook.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <vector>

// [Dev] NetFakeLoss / NetFakeLag / NetFakeJitter (tests only, docs/gns-transport.md): the same send-side impairment
// for the stock transport (the game's sendto, here) and for GNS (its FakePacketLoss/Lag/Jitter_Send, set by
// gns_transport from current()). OpenSpy traffic (destination ports 27900/27901) is never impaired.
namespace net_impair
{
#ifndef NDEBUG
	namespace
	{
		using sendto_t = int(WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
		sendto_t real_sendto = nullptr;
		settings chosen;
		struct packet { std::chrono::steady_clock::time_point due; std::uint64_t order; SOCKET s; std::vector<char> data; sockaddr_in to; };
		struct later { bool operator()(const packet& a, const packet& b) const { return a.due != b.due ? a.due > b.due : a.order > b.order; } };
		std::mutex lock;
		std::condition_variable wake;
		std::priority_queue<packet, std::vector<packet>, later> pending;
		std::mt19937 random;
		std::uint64_t order = 0, dropped = 0, delayed = 0;
		bool started = false;
		std::chrono::steady_clock::time_point last_due{};

		void worker()
		{
			std::unique_lock guard(lock);
			for (;;)
			{
				if (pending.empty()) { wake.wait(guard); continue; }
				const auto due = pending.top().due;
				if (std::chrono::steady_clock::now() < due) { wake.wait_until(guard, due); continue; }
				auto p = pending.top();
				pending.pop();
				guard.unlock();
				real_sendto(p.s, p.data.data(), int(p.data.size()), 0, reinterpret_cast<const sockaddr*>(&p.to), sizeof(p.to));
				guard.lock();
			}
		}

		int WSAAPI impaired_sendto(SOCKET s, const char* data, int length, int flags, const sockaddr* where, int size)
		{
			if (!where || size < int(sizeof(sockaddr_in)) || where->sa_family != AF_INET || length <= 0)
				return real_sendto(s, data, length, flags, where, size);
			const auto& to = *reinterpret_cast<const sockaddr_in*>(where);
			const auto port = ntohs(to.sin_port);
			if (port == 27900 || port == 27901) return real_sendto(s, data, length, flags, where, size);
			// Only inside a session (two participant records, session +0x278 from the getter 0x612be0): the join
			// handshake and admission stay clean, so both transports are compared on the same admitted session.
			const auto* session = reinterpret_cast<const std::uint8_t*(__cdecl*)()>(0x612be0)();
			if (!session || *reinterpret_cast<const std::uint32_t*>(session + 0x278) < 2) return real_sendto(s, data, length, flags, where, size);
			std::lock_guard guard(lock);
			if (std::uniform_real_distribution<float>(0.f, 100.f)(random) < chosen.loss) { ++dropped; return length; }
			const int extra = chosen.jitter > 0 ? std::uniform_int_distribution<int>(0, chosen.jitter)(random) : 0;
			if (chosen.lag + extra <= 0) return real_sendto(s, data, length, flags, where, size);
			if (!started) { started = true; std::thread(worker).detach(); }
			// Jitter delays but keeps the order, as GNS's FakePacketJitter does ("clumps").
			auto due = std::chrono::steady_clock::now() + std::chrono::milliseconds(chosen.lag + extra);
			if (due < last_due) due = last_due;
			last_due = due;
			pending.push({due, order++, s, std::vector<char>(data, data + length), to});
			++delayed;
			wake.notify_one();
			return length;
		}
		float number(const wchar_t* key)
		{
			const auto t = ini::text(L"Dev", key);
			return t ? std::strtof(t->c_str(), nullptr) : 0.f;
		}
	}

	const settings& current() { return chosen; }

	void install(HMODULE game)
	{
		chosen.loss = number(L"NetFakeLoss");
		chosen.lag = int(number(L"NetFakeLag"));
		chosen.jitter = int(number(L"NetFakeJitter"));
		if (!chosen.on()) return;
		random.seed(GetCurrentProcessId());
		real_sendto = reinterpret_cast<sendto_t>(iat_hook::hook(game, "WS2_32.dll", "sendto", 20, reinterpret_cast<void*>(&impaired_sendto)));
		if (!real_sendto)
		{
			chosen = {};
			logger::write("net impair: [Dev] NetFake* refused - the game's sendto import was not found");
			return;
		}
		logger::write("net impair: [Dev] the game's UDP sends lose %.1f %% and wait %d ms + 0..%d ms (GNS gets the same); OpenSpy ports untouched - tests only",
		              chosen.loss, chosen.lag, chosen.jitter);
	}
#else
	const settings& current() { static const settings disabled{}; return disabled; }
	void install(HMODULE) {}
#endif
}
