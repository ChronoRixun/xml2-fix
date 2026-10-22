#include <winsock2.h>
#include "gns_transport.hpp"
#include "gns_transport_core.hpp"
#include "network_validation.hpp"
#include "receive_validation.hpp"
#include "entity_capacity.hpp"
#include "gns_transport_rules.hpp"
#include "game_version.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "net_impair.hpp"
#include "online_room.hpp"
#include "online_room_core.hpp"
#include "join_in_progress.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <deque>
#include <memory>
#include <string>
#include <vector>

#if XML2_FIX_GNS
#include <steam/steamnetworkingsockets.h>
#include <steam/isteamnetworkingutils.h>
#endif

namespace gns_transport
{
	namespace
	{
		std::atomic<int> state{-1};
		template<std::size_t N> std::vector<unsigned char> bytes(const std::array<unsigned char, N>& a) { return {a.begin(), a.end()}; }
		template<class T> T& at(void* base, std::uint32_t offset) { return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(base) + offset); }
	}
}

#if XML2_FIX_GNS
namespace gns_transport
{
	namespace
	{
		using namespace gns_transport_rules;
		using gns_transport_core::message;

		using dispatch_t = int(__thiscall*)(void* manager, void* event);
		using session_t = std::uint8_t*(__cdecl*)();
		using received_t = void(__thiscall*)(void* peer, std::uint32_t clock, std::uint32_t length);
		using frame_t = void(__thiscall*)(void* peer, std::uint32_t frame);
		using pump_t = char(__thiscall*)(void* manager);
		using drain_t = void(__thiscall*)(void* manager);
		using peer_of_t = std::uint8_t*(__thiscall*)(void* manager, const void* endpoint);

		// One stock peer that is a session participant, and its GNS connection.
		struct link
		{
			std::uint8_t* peer = nullptr;
			std::uint32_t address = 0; // network order, as the peer stores it
			std::uint16_t port = 0;    // the peer's stock port
			HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
			bool connector = false, bound = false, switched = false, closed = false, overflow = false, sequenced = false;
			bool tunnel = false; // a room member's P2P connection (online_room owns it; the link only borrows it)
			std::uint16_t sequence = 0; // the next native reliable sequence the GNS messages continue
			gns_transport_core::outbox out;
			gns_transport_core::inbox in;
			DWORD opened = 0, bound_at = 0, hello_at = 0;
			std::uint64_t sent = 0, received = 0, sent_bytes = 0, received_bytes = 0, deferred = 0, delivered = 0;
			DWORD reported = 0, keepalive_at = 0, last_rx = 0, max_gap = 0;
			bool vanished = false; // its loss was handed to the session (abrupt disconnect)
			// Why this PC ended the link itself while GNS still had it open (the native timeout or another participant's
			// lost message): the loss then takes the same agreed-cut path as a closed link. Null: GNS ended it.
			const char* gone_why = nullptr;
			// A lost participant (this link gone): when this PC noticed, its session index, the next frame up to which
			// this PC sent the others its input records, and the next frame it announced (lost_ready; -1 none yet).
			DWORD gone_at = 0;
			int lost_index = -1, inputs_sent_next = -1, announced = -1;
			// A surviving participant (this link alive): per lost session index, when its input records arrived here
			// and the frame it announced (-1 none) and when; older than lost_memory_ms = from an earlier loss.
			DWORD inputs_at[4]{}, ready_at[4]{};
			int ready_from[4]{-1, -1, -1, -1};
#ifndef NDEBUG
			// [Dev] GnsDelayTo/GnsDelayMs: game messages held back before GNS (asymmetric delay of one link).
			std::deque<std::pair<DWORD, std::vector<std::uint8_t>>> delayed;
			std::deque<bool> delayed_reliable;
#endif
		};
		// An accepted connection whose hello has not yet named a participant on this side.
		struct stray { HSteamNetConnection connection; bool hello; std::uint32_t address; std::uint16_t port; DWORD since; };

		// All of it is touched on the game's thread only: the hooks run in the game's network code, the tick in the
		// receive pump, GNS callbacks inside RunCallbacks there.
		std::vector<std::unique_ptr<link>> links;
		std::vector<stray> strays;
		void* manager_ = nullptr;
		std::uint32_t own_address = 0; // network order
		std::uint16_t own_port = 0;
		HSteamListenSocket listener = k_HSteamListenSocket_Invalid;
		bool ready = false, failed = false, listen_failed = false, listener_disabled = false;
		int fake_loss = 0, fake_lag = 0;
		// Hook activity, logged every 10 s from whichever hook runs (development evidence).
		std::uint32_t pumps = 0, drains = 0, enqueues = 0, enqueued_gns = 0, directs = 0, backlogs = 0;
		std::uint32_t sent_by[256][4]{}, delivered_by[256][2]{};
		// Development trace of the rare (non-stream) opcodes: the logger caps all such lines at 20/s.
		const char* context = "other";
		bool rare(std::uint8_t op)
		{
			return op != 0x01 && op != 0x02 && op != 0x05 && op != 0x07 && op != 0x10 && op != 0x11 && op != 0x2e && op != 0x48 && op != 0x6a;
		}
		std::string census()
		{
			std::string out;
			for (int op = 0; op < 256; ++op)
			{
				const auto* s = sent_by[op]; const auto* d = delivered_by[op];
				if (!(s[0] | s[1] | s[2] | s[3] | d[0] | d[1])) continue;
				char one[80];
				std::snprintf(one, sizeof(one), " %02x:s%u/%u/%u/%u,d%u/%u", op, s[0], s[1], s[2], s[3], d[0], d[1]);
				out += one;
			}
			return out;
		}
		DWORD activity_at = 0;
		void activity()
		{
			const auto now = GetTickCount();
			if (now - activity_at < 10000) return;
			activity_at = now;
			logger::write("online: GNS hooks: pump %u, parser %u, enqueue %u (%u to GNS), direct %u, backlog %u, links %zu; opcode sent unreliable/reliable/coalesce-unrel/coalesce-rel, delivered unrel/rel:%s",
			              pumps, drains, enqueues, enqueued_gns, directs, backlogs, links.size(), census().c_str());
		}
		ISteamNetworkingSockets* sockets() { return SteamNetworkingSockets(); }

		std::string text(std::uint32_t address, std::uint16_t port)
		{
			const auto* b = reinterpret_cast<const std::uint8_t*>(&address);
			return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "." + std::to_string(b[3]) + ":" + std::to_string(port);
		}
		link* by_connection(HSteamNetConnection c)
		{
			for (auto& l : links) if (l->connection == c) return l.get();
			return nullptr;
		}
		link* by_peer(const void* peer)
		{
			for (auto& l : links) if (l->peer == peer) return l.get();
			return nullptr;
		}
		// The session index of the participant record of this link's peer (participant record +0x18, the native input
		// ring's index - the same on every PC), or -1.
		int participant_index(const link& l)
		{
			__try
			{
				const auto* s = reinterpret_cast<const std::uint8_t*>(session_static);
				if (!l.peer || *reinterpret_cast<const std::uint32_t*>(s) != session_vtable || !s[session_set_up]) return -1;
				for (auto* n = *reinterpret_cast<std::uint8_t* const*>(s + session_first); n; n = *reinterpret_cast<std::uint8_t**>(n + node_next_at))
				{
					auto* r = *reinterpret_cast<std::uint8_t**>(n + node_record_at);
					if (r && *reinterpret_cast<std::uint8_t**>(r + record_peer_at) == l.peer) return *reinterpret_cast<const std::int32_t*>(r + record_index_at);
				}
				return -1;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
		}
#ifndef NDEBUG
		// [Dev] GnsDelayTo = <session index>, GnsDelayMs = <ms>, GnsDelayTrigger = <file in the game folder> (tests only,
		// compiled out of Release): this PC holds its game messages to that participant back for GnsDelayMs before they
		// reach GNS - while the trigger file exists, or always without one. A process killed meanwhile never sends them:
		// the survivors of a 3+ player session then hold its inputs up to different frames, on demand.
		int delay_to = -1, delay_ms = 0;
		std::wstring delay_trigger;
		DWORD delay_checked = 0;
		bool delay_on = false;
		bool delay_active()
		{
			if (delay_to < 0 || delay_ms <= 0) return false;
			if (delay_trigger.empty()) return true;
			const auto now = GetTickCount();
			if (now - delay_checked >= 250 || !delay_checked)
			{
				delay_checked = now;
				const bool on = GetFileAttributesW(delay_trigger.c_str()) != INVALID_FILE_ATTRIBUTES;
				if (on != delay_on) logger::write("online: [Dev] GnsDelay %s - game messages to participant %d %s", on ? "on" : "off", delay_to, on ? "held back" : "flow again");
				delay_on = on;
			}
			return delay_on;
		}
#endif
		bool native_peer(void* manager, const void* peer)
		{
			if (!at<std::uint32_t>(manager, manager_peer_count)) return false;
			for (auto* node = at<std::uint8_t*>(manager, manager_peers); node; node = at<std::uint8_t*>(node, node_next))
				if (at<std::uint8_t*>(node, node_object) == peer) return true;
			return false;
		}
		void close(link& l, const char* why, bool linger)
		{
			if (l.connection != k_HSteamNetConnection_Invalid)
			{
				if (l.tunnel) online_room::set_transport_bound(l.connection, false);
				else sockets()->CloseConnection(l.connection, 0, why, linger);
				l.connection = k_HSteamNetConnection_Invalid;
			}
			if (!l.closed)
				logger::write("online: GNS link %s closed (%s) - sent %llu messages / %llu bytes, received %llu / %llu, %llu deferrals, open %lu ms",
				              text(l.address, l.port).c_str(), why, l.sent, l.sent_bytes, l.received, l.received_bytes, l.deferred,
				              unsigned long(GetTickCount() - l.opened));
			l.closed = true;
			l.out.clear();
			l.in.clear();
		}
		void close_all(const char* why)
		{
			for (auto& l : links) close(*l, why, true);
			links.clear();
			for (auto& s : strays) sockets()->CloseConnection(s.connection, 0, why, false);
			strays.clear();
			if (listener != k_HSteamListenSocket_Invalid) sockets()->CloseListenSocket(listener);
			listener = k_HSteamListenSocket_Invalid;
		}
		bool send_now(link& l, const std::vector<std::uint8_t>& payload, bool reliable)
		{
			const auto r = sockets()->SendMessageToConnection(l.connection, payload.data(), std::uint32_t(payload.size()),
			                                                  reliable ? k_nSteamNetworkingSend_Reliable : k_nSteamNetworkingSend_Unreliable, nullptr);
			if (r == k_EResultOK) { ++l.sent; l.sent_bytes += payload.size(); return true; }
			if (r != k_EResultLimitExceeded) logger::write_once("gns-send-failed", "online: GNS send to %s failed (EResult %d)", text(l.address, l.port).c_str(), int(r));
			return false;
		}
		bool send(link& l, const std::vector<std::uint8_t>& payload, bool reliable)
		{
#ifndef NDEBUG
			if (!l.delayed.empty() || (!payload.empty() && payload[0] == std::uint8_t(gns_transport_core::kind::game) && delay_active() && participant_index(l) == delay_to))
			{
				l.delayed.emplace_back(GetTickCount() + DWORD(delay_ms), payload);
				l.delayed_reliable.push_back(reliable);
				return true;
			}
#endif
			return send_now(l, payload, reliable);
		}
		void release_delayed(link& l)
		{
#ifndef NDEBUG
			const bool active = delay_active();
			while (!l.delayed.empty() && (!active || LONG(GetTickCount() - l.delayed.front().first) >= 0))
			{
				send_now(l, l.delayed.front().second, l.delayed_reliable.front());
				l.delayed.pop_front();
				l.delayed_reliable.pop_front();
			}
#else
			(void)l;
#endif
		}
		void outstanding(link& l)
		{
			if (l.connection == k_HSteamNetConnection_Invalid) { l.out.outstanding(0); return; }
			SteamNetConnectionRealTimeStatus_t status{};
			if (sockets()->GetConnectionRealTimeStatus(l.connection, &status, 0, nullptr) == k_EResultOK)
				l.out.outstanding(std::size_t(status.m_cbPendingReliable) + std::size_t(status.m_cbSentUnackedReliable));
		}

		void on_status(SteamNetConnectionStatusChangedCallback_t* info)
		{
			if (online_room::on_status(info)) return;
			const auto c = info->m_hConn;
			const auto now = info->m_info.m_eState;
			if (now == k_ESteamNetworkingConnectionState_Connecting && info->m_info.m_hListenSocket == listener && listener != k_HSteamListenSocket_Invalid)
			{
				const auto address = htonl(info->m_info.m_addrRemote.GetIPv4());
				bool duplicate = false;
				for (const auto& s : strays) duplicate = duplicate || s.address == address;
				if (!gns_transport_core::stray_allowed(strays.size(), duplicate))
				{
					logger::write_once("gns-stray-cap", "online: GNS refused an excess or duplicate stray connection from %s", text(address, 0).c_str());
					sockets()->CloseConnection(c, 0, "stray connection cap", false);
				}
				else if (sockets()->AcceptConnection(c) == k_EResultOK)
					strays.push_back({c, false, address, 0, GetTickCount()});
				else
					sockets()->CloseConnection(c, 0, "accept failed", false);
				return;
			}
			if (now == k_ESteamNetworkingConnectionState_Connected)
			{
				if (auto* l = by_connection(c); l && l->connector)
					send(*l, gns_transport_core::hello(gns_transport_core::kind::hello, own_address, own_port), true);
				return;
			}
			if (now == k_ESteamNetworkingConnectionState_ClosedByPeer || now == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
			{
				// The game sees its normal peer loss: nothing arrives from this peer any more, so its native
				// last-receive time (+0x307c) stops advancing, exactly as a stock socket gone quiet.
				if (auto* l = by_connection(c))
					close(*l, now == k_ESteamNetworkingConnectionState_ClosedByPeer ? "closed by the peer" : info->m_info.m_szEndDebug, false);
				else
					sockets()->CloseConnection(c, 0, nullptr, false);
				for (std::size_t i = 0; i < strays.size(); ++i)
					if (strays[i].connection == c) { strays.erase(strays.begin() + i); break; }
			}
		}
		void __cdecl on_debug(ESteamNetworkingSocketsDebugOutputType, const char* text)
		{
			logger::write_capped("gns-debug", "online: GNS: %s", text);
		}

		bool ensure(void* manager)
		{
			if (failed) return false;
			if (!ready)
			{
				SteamNetworkingErrMsg error{};
				// Rooms ([Online] Host/JoinCode): GNS starts as this install's identity, so the broker's certificate fits.
				SteamNetworkingIdentity identity;
				const bool room = online_room::identity(identity);
				if (!GameNetworkingSockets_Init(room ? &identity : nullptr, error))
				{
					failed = true;
					logger::write("online: GNS initialisation failed (%s) - the stock transport stays in use", error);
					return false;
				}
				auto* u = SteamNetworkingUtils();
				u->SetDebugOutputFunction(k_ESteamNetworkingSocketsDebugOutputType_Warning, &on_debug);
				u->SetGlobalCallback_SteamNetConnectionStatusChanged(&on_status);
				u->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMin, 512 * 1024);
				u->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendRateMax, 4 * 1024 * 1024);
				u->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_SendBufferSize, 4 * 1024 * 1024);
#ifndef NDEBUG
				if (fake_loss) { u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, float(fake_loss)); }
				if (fake_lag) { u->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketLag_Send, fake_lag); }
				// [Dev] NetFake*: the stock path's impairment (net_impair) applied to GNS's packets alike. GNS traffic starts
				// only once the peer is a participant, where the stock impairment also starts.
				if (const auto& n = net_impair::current(); n.on())
				{
					u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketLoss_Send, n.loss);
					u->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_FakePacketLag_Send, n.lag);
					if (n.jitter > 0)
					{
						u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Avg, n.jitter / 2.f);
						u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Max, float(n.jitter));
						u->SetGlobalConfigValueFloat(k_ESteamNetworkingConfig_FakePacketJitter_Send_Pct, 100.f);
					}
					fake_loss = int(n.loss); fake_lag = n.lag;
				}
#endif
				ready = true;
				logger::write("online: GNS initialised on the game's thread (fake loss %d %%, fake lag %d ms)", fake_loss, fake_lag);
			}
			if (manager != manager_)
			{
				close_all("the session's transport changed");
				manager_ = manager;
				own_address = 0;
				own_port = 0;
				listen_failed = false;
				listener_disabled = false;
			}
			if (listener == k_HSteamListenSocket_Invalid && !listen_failed && !listener_disabled)
			{
				sockaddr_in bound{};
				int size = sizeof(bound);
				const auto s = at<SOCKET>(manager, manager_socket);
				if (s == INVALID_SOCKET || getsockname(s, reinterpret_cast<sockaddr*>(&bound), &size) != 0 || !bound.sin_port) return false;
				own_address = bound.sin_addr.s_addr;
				own_port = ntohs(bound.sin_port);
				if (!gns_transport_core::create_ip_listener(online_room::requested()))
				{
					listener_disabled = true;
					logger::write("online: GNS IP listener disabled in room mode; only the custom-signaling P2P listener is used");
					return true;
				}
				SteamNetworkingIPAddr local;
				local.Clear();
				local.SetIPv4(ntohl(own_address), gns_transport_core::gns_port(own_port, gns_port_offset));
				listener = sockets()->CreateListenSocketIP(local, 0, nullptr);
				if (listener == k_HSteamListenSocket_Invalid)
				{
					listen_failed = true;
					logger::write("online: GNS could not listen on %s - participants stay on the stock transport", text(own_address, gns_transport_core::gns_port(own_port, gns_port_offset)).c_str());
					return false;
				}
				logger::write("online: GNS listening on %s (stock socket %s)", text(own_address, gns_transport_core::gns_port(own_port, gns_port_offset)).c_str(), text(own_address, own_port).c_str());
			}
			return listener_disabled || listener != k_HSteamListenSocket_Invalid;
		}

		// Is this native peer bound to a remote participant of the session (the broadcast's walk, 0x611270)?
		bool participant(std::uint8_t* session, std::uint8_t* peer)
		{
			if (!session || !at<std::uint32_t>(session, session_participant_count)) return false;
			// The endpoint is an ID the connection objects share with the peer (0 for the first one, live), not a pointer.
			const auto endpoint = at<void*>(peer, peer_endpoint);
			for (auto* node = at<std::uint8_t*>(session, session_participants); node; node = at<std::uint8_t*>(node, node_next))
			{
				auto* record = at<std::uint8_t*>(node, node_object);
				if (!record || (at<std::uint32_t>(record, participant_flags) & 1) || join_in_progress::pending_player(record)) continue;
				auto* connection = at<std::uint8_t*>(record, participant_connection);
				if (connection && at<void*>(connection, peer_endpoint) == endpoint) return true;
			}
			return false;
		}

		// One line whenever what discovery sees changes (session, its transport, participant and peer counts).
		void describe(void* manager, std::uint8_t* session)
		{
			static std::uint32_t last[4]{};
			const std::uint32_t now[4]{std::uint32_t(reinterpret_cast<std::uintptr_t>(session)), session ? at<std::uint32_t>(session, session_manager) : 0,
			                           session ? at<std::uint32_t>(session, session_participant_count) : 0, at<std::uint32_t>(manager, manager_peer_count)};
			if (std::memcmp(last, now, sizeof(now)) == 0) return;
			std::memcpy(last, now, sizeof(now));
			std::string line;
			if (session && now[2])
				for (auto* node = at<std::uint8_t*>(session, session_participants); node; node = at<std::uint8_t*>(node, node_next))
				{
					auto* record = at<std::uint8_t*>(node, node_object);
					auto* connection = record ? at<std::uint8_t*>(record, participant_connection) : nullptr;
					char one[96];
					std::snprintf(one, sizeof(one), " [record %p flags %x connection %p endpoint %p]", static_cast<void*>(record), record ? at<std::uint32_t>(record, participant_flags) : 0,
					              static_cast<void*>(connection), connection ? at<void*>(connection, peer_endpoint) : nullptr);
					line += one;
				}
			if (now[3])
				for (auto* node = at<std::uint8_t*>(manager, manager_peers); node; node = at<std::uint8_t*>(node, node_next))
				{
					auto* peer = at<std::uint8_t*>(node, node_object);
					char one[96];
					std::snprintf(one, sizeof(one), " [peer %p endpoint %p %s]", static_cast<void*>(peer), peer ? at<void*>(peer, peer_endpoint) : nullptr,
					              peer ? text(at<std::uint32_t>(peer, peer_address), at<std::uint16_t>(peer, peer_port)).c_str() : "");
					line += one;
				}
			logger::write("online: GNS discovery: manager %p, session %p (transport %08x), %u participants, %u peers:%s", manager, static_cast<void*>(session), now[1], now[2], now[3], line.c_str());
		}

		void discover(void* manager)
		{
			auto* session = reinterpret_cast<session_t>(native_session)();
			describe(manager, session);
			if (session && at<void*>(session, session_manager) != manager) session = nullptr;
			// Links whose peer left the native list, changed address or stopped being a participant end here.
			for (std::size_t i = 0; i < links.size();)
			{
				auto& l = *links[i];
				const bool alive = native_peer(manager, l.peer) && at<std::uint32_t>(l.peer, peer_address) == l.address &&
				                   at<std::uint16_t>(l.peer, peer_port) == l.port && participant(session, l.peer);
				if (alive) { ++i; continue; }
				close(l, "the native peer left the session", true);
				links.erase(links.begin() + i);
			}
			if (!session || !at<std::uint32_t>(manager, manager_peer_count)) return;
			for (auto* node = at<std::uint8_t*>(manager, manager_peers); node; node = at<std::uint8_t*>(node, node_next))
			{
				auto* peer = at<std::uint8_t*>(node, node_object);
				if (!peer || by_peer(peer) || !participant(session, peer)) continue;
				auto l = std::make_unique<link>();
				l->peer = peer;
				l->address = at<std::uint32_t>(peer, peer_address);
				l->port = at<std::uint16_t>(peer, peer_port);
				l->opened = GetTickCount();
				l->tunnel = online_room_core::is_virtual(ntohl(l->address));
				if (l->tunnel)
				{
					// A room member (its virtual address): the link rides the member's P2P connection; the hello still
					// decides who binds first. Both sides compare virtual addresses, as each sees the other.
					l->connector = gns_transport_core::connects(online_room::own_virtual(), own_port, ntohl(l->address), l->port);
					l->connection = online_room::connection_for(ntohl(l->address));
					if (l->connection == k_HSteamNetConnection_Invalid) l->closed = true;
					else if (l->connector)
					{
						send(*l, gns_transport_core::hello(gns_transport_core::kind::hello, own_address, own_port), true);
						l->hello_at = GetTickCount();
					}
					logger::write("online: GNS link %s for a new participant - %s", text(l->address, l->port).c_str(),
					              l->closed ? "no room connection to it, its messages stay on the tunnelled stock socket" : (l->connector ? "hello sent on its room connection" : "waiting for its hello on its room connection"));
					links.push_back(std::move(l));
					continue;
				}
				l->connector = gns_transport_core::connects(ntohl(own_address), own_port, ntohl(l->address), l->port);
				if (l->connector)
				{
					SteamNetworkingIPAddr remote;
					remote.Clear();
					remote.SetIPv4(ntohl(l->address), gns_transport_core::gns_port(l->port, gns_port_offset));
					l->connection = sockets()->ConnectByIPAddress(remote, 0, nullptr);
				}
				logger::write("online: GNS link %s for a new participant - %s", text(l->address, l->port).c_str(),
				              l->connector ? (l->connection != k_HSteamNetConnection_Invalid ? "connecting" : "connect failed, stock transport kept") : "waiting for its connection");
				if (l->connector && l->connection == k_HSteamNetConnection_Invalid) l->closed = true;
				links.push_back(std::move(l));
			}
		}

		void bind(link& l, HSteamNetConnection c)
		{
			l.connection = c;
			l.bound = true;
			l.bound_at = GetTickCount();
			if (l.tunnel) online_room::set_transport_bound(c, true);
			auto* session = reinterpret_cast<session_t>(native_session)();
			using record_of_t = void*(__thiscall*)(void* session, void* connection);
			logger::write("online: GNS link %s bound after %lu ms (%s); peer %p is participant record %p (0x611110)", text(l.address, l.port).c_str(), unsigned long(l.bound_at - l.opened),
			              l.connector ? "connected" : "accepted", static_cast<void*>(l.peer), session ? reinterpret_cast<record_of_t>(0x611110)(session, l.peer) : nullptr);
		}

		void lost_message(link& from, const gns_transport_core::parsed& p); // a lost participant's cut frame (below)
		void received(void* manager, link& l, SteamNetworkingMessage_t* m)
		{
			const auto p = gns_transport_core::parse(static_cast<const std::uint8_t*>(m->m_pData), std::size_t(m->m_cbSize));
			const auto now = GetTickCount();
			if (l.last_rx && now - l.last_rx > l.max_gap) l.max_gap = now - l.last_rx;
			l.last_rx = now;
			if (!p.valid) { logger::write_once("gns-malformed", "online: GNS message from %s malformed - dropped", text(l.address, l.port).c_str()); return; }
			if (p.k == gns_transport_core::kind::hello_ack)
			{
				if (l.connector && !l.bound) bind(l, l.connection);
				return;
			}
			if (p.k == gns_transport_core::kind::hello)
			{
				// A room connection's listener side binds on the connector's hello naming this link's stock port.
				if (l.tunnel && !l.connector && !l.bound && p.port == l.port)
				{
					bind(l, l.connection);
					send(l, gns_transport_core::hello(gns_transport_core::kind::hello_ack, own_address, own_port), true);
				}
				return;
			}
			if (l.tunnel && !l.bound) return; // nothing of the game's before the pair is bound
			if (p.k == gns_transport_core::kind::keepalive)
			{
				// The adapter's own keepalive (every 100 ms from a running transport): the native last-receive time
				// stays as fresh as with the stock socket's ACK and ping datagrams, and stops when the other side's
				// transport stops (its pump no longer runs) - then the game's own timeout takes over.
				reinterpret_cast<received_t>(native_received)(l.peer, at<std::uint32_t>(manager, manager_clock), 0);
				reinterpret_cast<frame_t>(native_frame)(l.peer, at<std::uint32_t>(manager, manager_frame));
				return;
			}
			if (p.k == gns_transport_core::kind::lost_inputs || p.k == gns_transport_core::kind::lost_ready) { lost_message(l, p); return; }
			if (p.k != gns_transport_core::kind::game) return;
			const network_validation::context validation{entity_capacity::fast_transfer_active()};
			if (!gns_transport_core::validate_opcode_length(p.opcode, p.length) || !network_validation::valid_message(p.opcode, p.body, p.length, validation))
			{
				logger::write_once("gns-invalid-message", "online: GNS refused an invalid game message (opcode 0x%02x, %zu bytes) from %s", unsigned(p.opcode), p.length, text(l.address, l.port).c_str());
				return;
			}
			message msg;
			msg.opcode = p.opcode;
			msg.reliable = (m->m_nFlags & k_nSteamNetworkingSend_Reliable) != 0;
			if (p.length) msg.body.assign(p.body, p.body + p.length);
			++l.received;
			if (rare(msg.opcode)) logger::write_capped("gns-trace", "online: GNS trace: received 0x%02x in the %s task", unsigned(msg.opcode), context);
			l.received_bytes += std::size_t(m->m_cbSize);
			// The pump's per-datagram bookkeeping (0x61640c..0x616424): last-receive time and frame.
			reinterpret_cast<received_t>(native_received)(l.peer, at<std::uint32_t>(manager, manager_clock), std::uint32_t(m->m_cbSize));
			reinterpret_cast<frame_t>(native_frame)(l.peer, at<std::uint32_t>(manager, manager_frame));
			if (!l.in.push(std::move(msg)) && !l.overflow)
			{
				l.overflow = true;
				close(l, "its receive queue is full (a handler deferred for too long)", false);
			}
		}

		void receive(void* manager)
		{
			SteamNetworkingMessage_t* batch[64];
			for (std::size_t i = 0; i < strays.size();)
			{
				auto& s = strays[i];
				if (!s.hello)
				{
					const int n = sockets()->ReceiveMessagesOnConnection(s.connection, batch, 1);
					if (n == 1)
					{
						const auto p = gns_transport_core::parse(static_cast<const std::uint8_t*>(batch[0]->m_pData), std::size_t(batch[0]->m_cbSize));
						batch[0]->Release();
						// The listener maps by the address GNS saw (a NAT may stand between) and the stock port the hello names.
						if (p.valid && p.k == gns_transport_core::kind::hello) { s.hello = true; s.port = p.port; }
						else { sockets()->CloseConnection(s.connection, 0, "no hello", false); strays.erase(strays.begin() + i); continue; }
					}
				}
				link* match = nullptr;
				if (s.hello)
					for (auto& l : links)
						if (!l->connector && !l->closed && l->connection == k_HSteamNetConnection_Invalid && l->address == s.address && l->port == s.port) match = l.get();
				if (match)
				{
					bind(*match, s.connection);
					send(*match, gns_transport_core::hello(gns_transport_core::kind::hello_ack, own_address, own_port), true);
					strays.erase(strays.begin() + i);
					continue;
				}
				if (GetTickCount() - s.since > 30000)
				{
					sockets()->CloseConnection(s.connection, 0, "no participant", false);
					strays.erase(strays.begin() + i);
					continue;
				}
				++i;
			}
			for (auto& l : links)
			{
				if (l->connection == k_HSteamNetConnection_Invalid) continue;
				const int n = l->tunnel ? online_room::fetch(l->connection, batch, 64) : sockets()->ReceiveMessagesOnConnection(l->connection, batch, 64);
				for (int k = 0; k < n; ++k)
				{
					if (l->connection != k_HSteamNetConnection_Invalid) received(manager, *l, batch[k]);
					batch[k]->Release();
				}
			}
		}

		// The receive queue's reliable messages of one opcode the native ordered queue holds (0x610790's first walk).
		std::uint32_t native_queued(std::uint8_t* peer, std::uint8_t opcode)
		{
			std::uint32_t n = 0;
			if (!at<std::uint32_t>(peer, peer_receive_count)) return 0;
			for (auto* node = at<std::uint8_t*>(peer, peer_receive_queue); node; node = at<std::uint8_t*>(node, node_next))
			{
				auto* header = at<std::uint8_t*>(node, node_object);
				n += header && header[0x10] == opcode;
			}
			return n;
		}
		// The next reliable sequence the native receive side expects after everything it already queued.
		std::uint16_t next_sequence(std::uint8_t* peer)
		{
			const auto expected = at<std::uint16_t>(peer, peer_expected);
			auto next = expected;
			if (at<std::uint32_t>(peer, peer_receive_count))
				for (auto* node = at<std::uint8_t*>(peer, peer_receive_queue); node; node = at<std::uint8_t*>(node, node_next))
				{
					auto* header = at<std::uint8_t*>(node, node_object);
					if (!header) continue;
					const auto after = std::uint16_t(at<std::uint16_t>(header, 0xa) + 1);
					if (std::uint16_t(after - expected) < 0x1ff && std::uint16_t(after - expected) > std::uint16_t(next - expected)) next = after;
				}
			return next;
		}

		// Each received GNS message enters the game as the native parser 0x60dca0 enters a datagram's message, in the
		// parser task: a reliable one is dispatched as slot 0 with [opcode][sequence][body] (0x6100b0 queues it in the
		// peer's ordered receive queue, the native drain delivers it with its own deferral and timing); an unreliable one
		// (and a reliable one longer than the pools hold, like a reassembled message) is dispatched as itself. The
		// sequence continues the native one. Return 2 (the fix's receive-room deferral) keeps the message for the next tick.
		void enter(void* manager)
		{
			for (std::size_t i = 0; i < links.size(); ++i)
			{
				auto* l = links[i].get();
				if (!l->in.size()) continue;
				bool gone = false;
				l->in.drain([&](const message& m)
				{
					if (gone) return 2;
					std::uint8_t event[0x1c]{};
					std::vector<std::uint8_t> framed;
					const bool queued = m.reliable && m.body.size() <= pool_body_max;
					if (queued)
					{
						if (!l->sequenced) { l->sequence = next_sequence(l->peer); l->sequenced = true; }
						framed.resize(3 + m.body.size());
						framed[0] = m.opcode;
						std::memcpy(framed.data() + 1, &l->sequence, 2);
						if (!m.body.empty()) std::memcpy(framed.data() + 3, m.body.data(), m.body.size());
						event[0] = 0;
						at<const std::uint8_t*>(event, 4) = framed.data();
						at<std::uint16_t>(event, 8) = std::uint16_t(m.body.size() + 2);
					}
					else
					{
						event[0] = m.opcode;
						at<const std::uint8_t*>(event, 4) = m.body.empty() ? nullptr : m.body.data();
						at<std::uint16_t>(event, 8) = std::uint16_t(m.body.size());
					}
					at<void*>(event, 0x10) = l->peer;
					at<void*>(event, 0x14) = manager;
					at<std::uint32_t>(event, 0x18) = at<std::uint8_t>(l->peer, peer_connected) ? 0 : 2;
					const int r = reinterpret_cast<dispatch_t>(native_dispatch)(manager, event);
					if (r == 2) { ++l->deferred; return 2; }
					if (queued) ++l->sequence;
					++l->delivered;
					if (rare(m.opcode)) logger::write_capped("gns-trace", "online: GNS trace: entered 0x%02x -> %d in the %s task", unsigned(m.opcode), r, context);
					++delivered_by[m.opcode][m.reliable ? 1 : 0];
					// A handler may end the session: nothing more goes to a peer object the game no longer lists.
					if (manager_ != manager || i >= links.size() || links[i].get() != l || !native_peer(manager, l->peer)) gone = true;
					return r;
				});
				if (gone) return;
			}
		}

		void flush_link(link& l)
		{
			if (!l.bound || l.closed || l.connection == k_HSteamNetConnection_Invalid) return;
			if (GetTickCount() - l.keepalive_at >= 100)
			{
				l.keepalive_at = GetTickCount();
				const std::vector<std::uint8_t> ping{std::uint8_t(gns_transport_core::kind::keepalive)};
				sockets()->SendMessageToConnection(l.connection, ping.data(), 1, k_nSteamNetworkingSend_UnreliableNoNagle, nullptr);
			}
			release_delayed(l);
			if (!l.switched) return;
			if (l.out.pending())
			{
				l.out.flush([&](const message& m, const std::vector<std::uint8_t>& payload)
				{
					if (rare(m.opcode)) logger::write_capped("gns-trace", "online: GNS trace: sent 0x%02x (%s, %zu bytes) from the %s task", unsigned(m.opcode), m.reliable ? "reliable" : "unreliable", m.body.size(), context);
					return send(l, payload, m.reliable);
				});
				sockets()->FlushMessagesOnConnection(l.connection);
			}
			outstanding(l);
		}
		void flush()
		{
			for (auto& l : links) flush_link(*l);
		}


		// Every 10 s per link: what moved and what waits (development evidence; one line per link).
		void report()
		{
			const auto now = GetTickCount();
			for (auto& l : links)
			{
				if (l->closed || now - l->reported < 10000) continue;
				l->reported = now;
				SteamNetConnectionRealTimeStatus_t st{};
				if (l->connection != k_HSteamNetConnection_Invalid) sockets()->GetConnectionRealTimeStatus(l->connection, &st, 0, nullptr);
				const float age = manager_ && l->peer ? at<float>(manager_, manager_clock) - at<float>(l->peer, 0x307c) : -1.f;
				logger::write("online: GNS link %s: sent %llu (%llu bytes), received %llu (%llu bytes), delivered %llu, waiting %zu, deferrals %llu, buffered %zu, in flight %zu; ping %d ms, quality %.2f/%.2f, queue %lld us, pending %d+%d bytes, unacked %d; longest receive gap %lu ms, native last-receive age %.2f s",
				              text(l->address, l->port).c_str(), l->sent, l->sent_bytes, l->received, l->received_bytes, l->delivered, l->in.size(), l->deferred,
				              l->out.pending(), l->out.in_flight(), st.m_nPing, st.m_flConnectionQualityLocal, st.m_flConnectionQualityRemote, (long long)st.m_usecQueueTime,
				              st.m_cbPendingReliable, st.m_cbPendingUnreliable, st.m_cbSentUnackedReliable, unsigned long(l->max_gap), age);
				l->max_gap = 0;
				{
					// The native ordered receive queue this adapter feeds: expected sequence, queued count, lowest queued.
					const auto expected = at<std::uint16_t>(l->peer, peer_expected);
					unsigned queued = 0, lowest = 0x10000;
					if (at<std::uint32_t>(l->peer, peer_receive_count))
						for (auto* node = at<std::uint8_t*>(l->peer, peer_receive_queue); node; node = at<std::uint8_t*>(node, node_next))
							if (auto* h = at<std::uint8_t*>(node, node_object)) { ++queued; lowest = std::min<unsigned>(lowest, at<std::uint16_t>(h, 0xa)); }
					logger::write("online: GNS link %s: native receive expects %u, %u queued (lowest %u), adapter next %u%s", text(l->address, l->port).c_str(),
					              unsigned(expected), queued, lowest == 0x10000 ? 0u : lowest, unsigned(l->sequence), l->sequenced ? "" : " (not started)");
				}
			}
		}

		void switch_bound()
		{
			for (auto& l : links)
			{
				// Outbound moves to GNS only once the native reliable queue to this peer is empty (all acknowledged),
				// so no native message can arrive after a GNS one.
				if (!l->bound || l->switched || l->closed || at<std::uint32_t>(l->peer, peer_send_count)) continue;
				l->switched = true;
				logger::write("online: GNS link %s carries the game's messages (switched %lu ms after binding)", text(l->address, l->port).c_str(),
				              unsigned long(GetTickCount() - l->bound_at));
			}
		}

		// ---- Abrupt disconnects ----------------------------------------------------------------------------------
		// A participant whose process died or whose network vanished: its room connection is gone (the broker saw
		// its TLS session end and the member left the room, ~1 s live) or GNS closed the link. The native session
		// would only notice after 10 s of silence (watchdog 0x613790) - and a joiner then blamed the HOST after its
		// 10 s grace and left (live E18: "You have lost connection to the host" on the healthy joiner while the
		// host had already removed the lost one). Handed to the session the way the native paths do it: the host
		// as its watchdog does (0x611550 notices 0x10cc/0x1107, then 0x612c40 with 0x10cd), a joiner as the host's
		// notice 0x2b would (0x612c40 with 0x1107). A vanished host is left to the native watchdog and its dialog.
		using lost_t = std::uint32_t(__thiscall*)(void* session, void* record, std::uint32_t reason, std::uint32_t flag);
		using notice_t = void(__thiscall*)(void* session, std::int32_t index, std::uint32_t lost, std::uint32_t others, std::uint32_t flag);
		// Set while hand_over itself calls the native notice/removal: the removal filters below let those through.
		bool own_removal = false;
		bool hand_over_now(link& l, int& index, bool& as_host);
		bool hand_over(link& l, int& index, bool& as_host)
		{
			own_removal = true;
			const bool done = hand_over_now(l, index, as_host);
			own_removal = false;
			return done;
		}
		bool hand_over_now(link& l, int& index, bool& as_host)
		{
			__try
			{
				auto* s = reinterpret_cast<std::uint8_t*>(session_static);
				if (*reinterpret_cast<const std::uint32_t*>(s) != session_vtable || !s[session_set_up]) return false;
				const auto* zones = reinterpret_cast<const std::uint8_t*>(zones_static);
				if (*reinterpret_cast<const std::uint32_t*>(zones) != zones_vtable || (zones[zones_loading_at] & 3)) return false;
				if (!*reinterpret_cast<const std::uint32_t*>(s + session_count)) return false;
				for (auto* n = *reinterpret_cast<std::uint8_t**>(s + session_first); n; n = *reinterpret_cast<std::uint8_t**>(n + node_next_at))
				{
					auto* r = *reinterpret_cast<std::uint8_t**>(n + node_record_at);
					if (!r || *reinterpret_cast<std::uint8_t**>(r + record_peer_at) != l.peer) continue;
					if (*reinterpret_cast<const std::uint32_t*>(r + record_flags_at) & 1) return false; // this PC
					index = *reinterpret_cast<const std::int32_t*>(r + record_index_at);
					as_host = s[session_host] != 0;
					if (as_host)
					{
						reinterpret_cast<notice_t>(native_drop_notice)(s, index, 0x10cc, 0x1107, 0);
						reinterpret_cast<lost_t>(native_lost)(s, r, 0x10cd, 0);
						return true;
					}
					if (index == 0) return false; // the host: the native watchdog and its "lost connection to the host" dialog
					reinterpret_cast<lost_t>(native_lost)(s, r, 0x1107, 0);
					return true;
				}
				return false;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		// The lockstep's frame (0xa3bbf4) and tick mode (0xa3bbe4: 2 = waiting for a participant's input) as last seen.
		std::uint16_t seen_frame = 0;
		DWORD frame_moved_at = 0;
		bool lockstep_waiting(DWORD now)
		{
			__try
			{
				const auto frame = *reinterpret_cast<const std::uint16_t*>(lockstep_frame);
				if (frame != seen_frame || !frame_moved_at) { seen_frame = frame; frame_moved_at = now; }
				return *reinterpret_cast<const std::uint32_t*>(lockstep_mode) == 2 && now - frame_moved_at >= vanish_settle_ms;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		// The lockstep frame has not moved for vanish_settle_ms, whatever the tick mode: the frame gate (0x606a50) leaves
		// mode 1 in place when it fails on this PC's own short input buffer first (its local pre-check), so a stall for a
		// lost player is not always mode 2 (live D8: neither survivor saw mode 2 until the native 10 s watchdog). With the
		// lost player's ring run out (ring_empty) this PC cannot pass that player's next frame.
		bool lockstep_still(DWORD now)
		{
			__try
			{
				const auto frame = *reinterpret_cast<const std::uint16_t*>(lockstep_frame);
				if (frame != seen_frame || !frame_moved_at) { seen_frame = frame; frame_moved_at = now; }
				return now - frame_moved_at >= vanish_settle_ms;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		// ---- A lost participant's cut frame, agreed (three or more players) ----------------------------------------
		// Live (net030-solo E20, rel-030 R2/R3/R7-R11): with three players one killed, the survivors held its inputs up
		// to frames one apart - its last messages reached one survivor and not the other - so each stopped at its own
		// first missing frame, removed it there and the frame gate/checksum compare ended the session. A relay of its
		// last unreliable messages (reverted 406e03c/ae31705) did not close the gap: a frame also arrives in a reliable
		// 0x02, which the receiver numbers by its own count. Here the survivors share the records of the native input
		// ring itself (0x559560's records, by frame), so every survivor ends with the union - the same first missing
		// frame - and each removes the lost player only when its lockstep has run every earlier frame and waits
		// there, and every other survivor announced that same frame.
		using store_t = void(__thiscall*)(void* sync, const void* record, std::int32_t index, char kind);
		std::uint8_t* input_block(int index)
		{
			if (index < 0 || index > gns_transport_core::lost_index_max) return nullptr;
			// The synchronizer is a function-local static (getter 0x55a390, guard bit 1 of 0x7ac240).
			if (!(*reinterpret_cast<const std::uint8_t*>(0x7ac240) & 1)) return nullptr;
			return reinterpret_cast<std::uint8_t*>(0x79fbb0) + std::uint32_t(index) * input_block_size;
		}
		struct ring_state { bool ok = false; std::uint16_t next = 0; int read = 0, write = 0; };
		ring_state ring(int index)
		{
			ring_state r;
			__try
			{
				auto* b = input_block(index);
				if (!b) return r;
				r.read = *reinterpret_cast<const std::int32_t*>(b + input_read);
				r.write = *reinterpret_cast<const std::int32_t*>(b + input_write);
				r.next = *reinterpret_cast<const std::uint16_t*>(b + input_next);
				r.ok = r.read >= 0 && r.read < gns_transport_core::input_ring_records && r.write >= 0 && r.write < gns_transport_core::input_ring_records;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { r.ok = false; }
			return r;
		}
		// The resolved records this PC holds for that index: the run that ends just before its next frame (at most
		// lost_inputs_max). Consumed records keep their bytes; the consumer clears only the valid byte.
		std::size_t ring_records(int index, const ring_state& r, std::uint16_t& first, std::uint8_t* out)
		{
			std::size_t count = 0;
			__try
			{
				auto* b = input_block(index);
				if (!b || !r.ok) return 0;
				while (count < gns_transport_core::lost_inputs_max)
				{
					const std::uint16_t f = std::uint16_t(r.next - 1 - count);
					const int slot = gns_transport_core::ring_slot(r.write, r.next, f);
					if (slot < 0) break;
					const auto* rec = b + input_records + std::uint32_t(slot) * input_record;
					if (rec[1] != 0 || *reinterpret_cast<const std::uint16_t*>(rec + 4) != f) break; // unresolved, or another frame's
					++count;
				}
				first = std::uint16_t(r.next - count);
				for (std::size_t i = 0; i < count; ++i)
				{
					const int slot = gns_transport_core::ring_slot(r.write, r.next, std::uint16_t(first + i));
					std::memcpy(out + i * gns_transport_core::input_record_size, b + input_records + std::uint32_t(slot) * input_record + 4, gns_transport_core::input_record_size);
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { count = 0; }
			return count;
		}
		bool ring_empty(const ring_state& r) { return r.ok && r.read == r.write; }
		// Files another survivor's records as the native receive path would (resolved: kind 0); the store ignores a
		// frame this PC has already run or already holds.
		bool file_records(int index, const std::uint8_t* records, std::size_t count)
		{
			__try
			{
				auto* b = input_block(index);
				if (!b) return false;
				for (std::size_t i = 0; i < count; ++i)
					reinterpret_cast<store_t>(input_store)(reinterpret_cast<void*>(0x79fbb0), records + i * gns_transport_core::input_record_size, index, 0);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		bool zone_running()
		{
			__try
			{
				const auto* s = reinterpret_cast<const std::uint8_t*>(session_static);
				const auto* zones = reinterpret_cast<const std::uint8_t*>(zones_static);
				return *reinterpret_cast<const std::uint32_t*>(s) == session_vtable && s[session_set_up] &&
					*reinterpret_cast<const std::uint32_t*>(zones) == zones_vtable && !(zones[zones_loading_at] & 3);
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		bool is_gone(const link& l)
		{
			return l.closed || (l.tunnel && l.connection != k_HSteamNetConnection_Invalid && !online_room::owns(l.connection));
		}
		bool survivor(const link& l)
		{
			return l.bound && l.peer && !l.vanished && !l.gone_at && !is_gone(l) && l.connection != k_HSteamNetConnection_Invalid;
		}
		// Records that arrived before this PC noticed the loss itself: filed once it does (a participant that is not
		// gone here keeps its own inputs).
		struct early_records { std::uint8_t index; DWORD at; std::vector<std::uint8_t> records; };
		constexpr DWORD lost_memory_ms = 15000;
		bool recent(DWORD at, DWORD now) { return at && now - at < lost_memory_ms; }
		std::vector<early_records> early;
		// ---- One path for every loss -------------------------------------------------------------------------------
		// Live (swap-030 F01/F03): a joiner frozen 30 s was removed by the HOST's native 10 s watchdog (0x613790 ->
		// 0x611550 notice + 0x612c40) ~1.3 s before GNS reported any link closed. discover() then dropped the host's
		// link without the agreement, so the other joiner's agreement waited for records the host never sent, its own
		// watchdog grace ran out and it left ("lost connection to the host"). Now every way a participant is lost - GNS
		// closing the link, the native timeout/lag-report removal on the host (filters below), another participant's
		// lost message - ends the link here and the loss goes through vanished_links' agreed cut frame on every PC.
		void declare_gone(link& l, const char* why)
		{
			if (l.gone_at || l.closed || l.vanished || !l.bound) return;
			l.gone_why = why;
			close(l, why, false);
		}
		void lost_message(link& from, const gns_transport_core::parsed& p)
		{
			// Only about a participant this PC has a link to (alive or lost): anything else is dropped unlogged.
			const bool known = std::any_of(links.begin(), links.end(), [&](const std::unique_ptr<link>& item)
			                               { return item.get() != &from && (item->lost_index == int(p.index) || participant_index(*item) == int(p.index)); });
			if (!known) return;
			// A surviving participant reports this one lost while this PC still hears it (it noticed first, or its own
			// timeout removed it): this PC ends its link too, so both cut it at the agreed frame. Never the host (index
			// 0): a vanished host is left to the native watchdog and its dialog.
			if (p.index != 0 && survivor(from) && zone_running())
				for (auto& item : links)
					if (item.get() != &from && !item->gone_at && !item->closed && item->bound && participant_index(*item) == int(p.index))
					{
						logger::write("online: participant %d (%s) reported lost by %s - this PC ends its link too", int(p.index), text(item->address, item->port).c_str(), text(from.address, from.port).c_str());
						declare_gone(*item, "reported lost by another participant");
					}
			if (p.k == gns_transport_core::kind::lost_ready)
			{
				from.ready_from[p.index] = int(p.frame);
				from.ready_at[p.index] = GetTickCount() | 1;
				logger::write_capped("gns-lost", "online: participant %d lost - %s announced its frame %u", int(p.index), text(from.address, from.port).c_str(), unsigned(p.frame));
				return;
			}
			from.inputs_at[p.index] = GetTickCount() | 1;
			link* lost = nullptr;
			for (auto& item : links)
				if (item.get() != &from && item->gone_at && !item->vanished && item->lost_index == int(p.index)) { lost = item.get(); break; }
			if (!lost || !zone_running())
			{
				if (early.size() < 16) early.push_back({p.index, GetTickCount(), std::vector<std::uint8_t>(p.body, p.body + p.length)});
				logger::write_capped("gns-lost", "online: participant %d - %zu input records (frames %u..%u) from %s kept until this PC sees it lost", int(p.index), p.count, unsigned(p.frame),
				              unsigned(std::uint16_t(p.frame + p.count - 1)), text(from.address, from.port).c_str());
				return;
			}
			const auto before = ring(p.index);
			if (p.count) file_records(p.index, p.body, p.count);
			const auto after = ring(p.index);
			logger::write_capped("gns-lost", "online: participant %d lost - %zu input records (frames %u..%u) from %s; its next frame here %u -> %u", int(p.index), p.count, unsigned(p.frame),
			              unsigned(std::uint16_t(p.frame + p.count - 1)), text(from.address, from.port).c_str(), unsigned(before.next), unsigned(after.next));
		}
#ifndef NDEBUG
		// [Dev] LostCutLegacy = 1 (tests only, compiled out of Release): the 1.8.0 candidate's removal at this PC's own
		// stop frame, unchanged, as the negative control of the agreed cut frame below.
		bool legacy_cut = false;
		void legacy_vanished_links()
		{
			// Only once this PC's lockstep has stopped at the first frame it lacks the lost player's input for (and stayed
			// there vanish_settle_ms, so inputs already in flight have arrived): every PC then removes the player at the
			// same frame, as after the native 10 s timeout. Removed at once (live E19), the PCs were at different
			// frames inside the input buffer and the checksum compare dropped the session 15 s later.
			if (!lockstep_waiting(GetTickCount())) return;
			logger::write_once("legacy-cut", "online: [Dev] LostCutLegacy=1 - a lost participant is removed at this PC's own stop frame (1.8.0 behaviour, negative control)");
			for (auto& item : links)
			{
				auto& l = *item;
				if (l.vanished || !l.bound || !l.peer) continue;
				const bool gone = l.closed || (l.tunnel && l.connection != k_HSteamNetConnection_Invalid && !online_room::owns(l.connection));
				if (!gone) continue;
				int index = -1;
				bool as_host = false;
				const std::string who = text(l.address, l.port);
				const auto r = ring(participant_index(l));
				if (!hand_over(l, index, as_host)) continue; // not in a running zone yet: tried again next tick
				l.vanished = true;
				logger::write_capped("gns-lost", "online: participant %d (%s) vanished (%s) - removed now at lockstep frame %u, its next frame %u (ring %d..%d) %s", index, who.c_str(), l.closed ? "GNS link closed" : "left the room",
				              unsigned(seen_frame), unsigned(r.next), r.read, r.write, as_host ? "as the host's watchdog would (notice 0x10cc/0x1107, then the native removal)" : "as the host's notice would");
			}
		}
#endif
		void vanished_links()
		{
			const auto now = GetTickCount();
#ifndef NDEBUG
			if (legacy_cut) { legacy_vanished_links(); return; }
#endif
			const bool waiting = lockstep_still(now);
			for (auto& item : links)
			{
				auto& l = *item;
				if (l.vanished || !l.bound || !l.peer || !is_gone(l)) continue;
				if (!l.gone_at)
				{
					l.gone_at = now ? now : 1;
					l.lost_index = participant_index(l);
					int others = 0;
					for (auto& o : links) others += o.get() != &l && survivor(*o);
					logger::write("online: participant %d (%s) gone (%s) - its cut frame is agreed with %d other participant(s)", l.lost_index, text(l.address, l.port).c_str(),
					              l.gone_why ? l.gone_why : (l.closed ? "GNS link closed" : "left the room"), others);
				}
				if (l.lost_index < 0) l.lost_index = participant_index(l);
				if (l.lost_index < 0 || !zone_running()) continue;
				for (auto it = early.begin(); it != early.end();)
				{
					if (!recent(it->at, now)) { it = early.erase(it); continue; }
					if (it->index != l.lost_index) { ++it; continue; }
					const auto before = ring(l.lost_index);
					file_records(l.lost_index, it->records.data(), it->records.size() / gns_transport_core::input_record_size);
					logger::write("online: participant %d lost - %zu input records kept earlier filed; its next frame here %u -> %u", l.lost_index,
					              it->records.size() / gns_transport_core::input_record_size, unsigned(before.next), unsigned(ring(l.lost_index).next));
					it = early.erase(it);
				}
				// This PC's records of it to every other survivor, again whenever it holds more.
				const auto r = ring(l.lost_index);
				if (!r.ok) continue;
				if (l.inputs_sent_next != int(r.next))
				{
					std::uint16_t first = 0;
					std::uint8_t records[gns_transport_core::lost_inputs_max * gns_transport_core::input_record_size];
					const auto count = ring_records(l.lost_index, r, first, records);
					int to = 0;
					for (auto& o : links)
						if (o.get() != &l && survivor(*o)) { send(*o, gns_transport_core::lost_inputs(std::uint8_t(l.lost_index), first, records, count), true); ++to; }
					l.inputs_sent_next = int(r.next);
					logger::write("online: participant %d lost - this PC holds its inputs up to frame %u (ring %d..%d); %zu records (from frame %u) sent to %d participant(s)", l.lost_index,
					              unsigned(std::uint16_t(r.next - 1)), r.read, r.write, count, unsigned(first), to);
				}
				// Announce, then remove at the agreed frame.
				std::vector<gns_transport_core::survivor_view> views;
				bool all_inputs = true;
				for (auto& o : links)
				{
					if (o.get() == &l || !survivor(*o)) continue;
					const bool inputs = recent(o->inputs_at[l.lost_index], now);
					all_inputs = all_inputs && inputs;
					views.push_back({inputs, recent(o->ready_at[l.lost_index], now) ? o->ready_from[l.lost_index] : -1});
				}
				const auto elapsed = std::uint32_t(now - l.gone_at);
				if (!gns_transport_core::may_announce(waiting, ring_empty(r), all_inputs, elapsed)) continue;
				if (l.announced != int(r.next))
				{
					l.announced = int(r.next);
					for (auto& o : links)
						if (o.get() != &l && survivor(*o)) send(*o, gns_transport_core::lost_ready(std::uint8_t(l.lost_index), r.next), true);
					logger::write("online: participant %d lost - this PC ran every frame it has its input for and waits at its frame %u: announced (lockstep frame %u, %zu other(s)%s)",
					              l.lost_index, unsigned(r.next), unsigned(seen_frame), views.size(), all_inputs ? "" : ", not every survivor's records arrived");
				}
				const auto step = gns_transport_core::lost_decision(l.announced, views.data(), views.size(), elapsed);
				if (step == gns_transport_core::lost_step::wait) continue;
				std::string others;
				for (const auto& v : views) others += " " + std::to_string(v.ready);
				int index = -1;
				bool as_host = false;
				const std::string who = text(l.address, l.port);
				if (!hand_over(l, index, as_host))
				{
					if (participant_index(l) < 0)
					{
						l.vanished = true; // the host's notice removed it already (this PC waited at the agreed frame)
						logger::write("online: participant %d (%s) removed by the host's notice; this PC waited at its frame %u", l.lost_index, who.c_str(), unsigned(r.next));
					}
					continue; // not in a running zone yet: tried again next tick
				}
				l.vanished = true;
				logger::write("online: participant %d (%s) vanished (%s) - removed now at its frame %u (lockstep frame %u), %s (others:%s) %s", index, who.c_str(),
				              l.gone_why ? l.gone_why : (l.closed ? "GNS link closed" : "left the room"), unsigned(r.next), unsigned(seen_frame),
				              step == gns_transport_core::lost_step::remove_agreed ? "agreed" : "NOT AGREED (timeout)", others.empty() ? " none" : others.c_str(),
				              as_host ? "as the host's watchdog would (notice 0x10cc/0x1107, then the native removal)" : "as the host's notice would");
			}
		}

		// The native removals of a participant that went quiet (the host's watchdog 0x613790 at 0x613adc, its lag-report
		// vote 0x614810 via 0x6133a0; lost codes 0x10cc/0x10cd) are filtered at the entries of the notice 0x611550 and
		// the removal 0x612c40: for a remote participant (never the host, index 0) with a bound link, in a running zone,
		// the link is ended here and vanished_links removes it at the agreed frame (hand_over calls the same two
		// functions with own_removal set). Not deferred: other reasons (leave, session teardown), loads, unbound links,
		// and a loss still not handed over native_fallback_ms after it began (the native removal then runs as before).
		constexpr DWORD native_fallback_ms = 20000;
		// A remote participant's record in a running zone: its session index and native peer.
		bool remote_record(void* session, std::uint8_t* record, int& index, std::uint8_t*& peer)
		{
			__try
			{
				if (reinterpret_cast<std::uintptr_t>(session) != session_static || !record || !zone_running()) return false;
				if (*reinterpret_cast<const std::uint32_t*>(record + record_flags_at) & 1) return false; // this PC
				index = *reinterpret_cast<const std::int32_t*>(record + record_index_at);
				peer = *reinterpret_cast<std::uint8_t**>(record + record_peer_at);
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		bool defer_native_removal(void* session, std::uint8_t* record, const char* path)
		{
			std::uint8_t* peer = nullptr;
			int index = -1;
			if (!remote_record(session, record, index, peer) || index <= 0 || !peer) return false;
			link* l = by_peer(peer);
			if (!l || !l->bound || l->vanished) return false;
			const auto now = GetTickCount();
			if (l->gone_at && now - l->gone_at > native_fallback_ms)
			{
				logger::write_once("native-fallback", "online: participant %d (%s) still not handed over %lu ms after its loss - the native removal runs", index, text(l->address, l->port).c_str(), static_cast<unsigned long>(native_fallback_ms));
				return false;
			}
			if (!l->gone_at && !l->closed)
			{
				logger::write("online: participant %d (%s) timed out natively (%s) - deferred to the agreed cut frame", index, text(l->address, l->port).c_str(), path);
				declare_gone(*l, "the native session timed it out");
			}
			return true;
		}
		std::uint8_t* record_of_index(void* session, int index)
		{
			__try
			{
				auto* s = static_cast<std::uint8_t*>(session);
				if (reinterpret_cast<std::uintptr_t>(session) != session_static || !*reinterpret_cast<const std::uint32_t*>(s + session_count)) return nullptr;
				for (auto* n = *reinterpret_cast<std::uint8_t**>(s + session_first); n; n = *reinterpret_cast<std::uint8_t**>(n + node_next_at))
				{
					auto* r = *reinterpret_cast<std::uint8_t**>(n + node_record_at);
					if (r && *reinterpret_cast<const std::int32_t*>(r + record_index_at) == index) return r;
				}
				return nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
		}

		void tick(void* manager)
		{
			const bool listening = ensure(manager);
			if (!ready) return;
			sockets()->RunCallbacks();
			online_room::tick();
			if (!listening) return;
			discover(manager);
			const auto now = GetTickCount();
			for (auto& item : links)
			{
				auto& l = *item;
				if (!l.closed && l.tunnel && gns_transport_core::hello_due(l.connector, l.bound, now, l.hello_at))
				{
					send(l, gns_transport_core::hello(gns_transport_core::kind::hello, own_address, own_port), true);
					l.hello_at = now;
				}
			}
			receive(manager);
			switch_bound();
			vanished_links();
			flush();
			report();
		}

		DWORD pumped_at = 0; // the receive-pump task's last run (GetTickCount)
		// The lockstep waits for a player's input (the frame gate's resync state): the scheduler stops the receive-pump
		// task, so no GNS message is read - not even a healthy player's keepalive - and the native 10 s session
		// timeout (session +0x438, watchdog 0x613790) dropped every remote player with the one that vanished (live:
		// net030-jip C13; net030-solo B runs). While the pump task is late and no zone loads (the lockstep handlers
		// must not see input then), the parser and flush tasks read the links instead.
		bool stalled_service()
		{
			if (GetTickCount() - pumped_at < stall_service_ms) return false;
			__try
			{
				const auto* zones = reinterpret_cast<const std::uint8_t*>(zones_static);
				const auto* s = reinterpret_cast<const std::uint8_t*>(session_static);
				return *reinterpret_cast<const std::uint32_t*>(zones) == zones_vtable && (zones[zones_loading_at] & 3) == 0 &&
					*reinterpret_cast<const std::uint32_t*>(s) == session_vtable && s[session_set_up] != 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		std::uint32_t stalled_services = 0;
		DWORD stalled_logged = 0;
		void serve_stalled(void* manager)
		{
			++stalled_services;
			const auto now = GetTickCount();
			if (now - stalled_logged >= 5000)
			{
				stalled_logged = now;
				logger::write("online: the receive pump has not run for %lu ms (the lockstep waits for a player) - links read from the %s task (%u times so far)",
				              static_cast<unsigned long>(now - pumped_at), context, stalled_services);
			}
			const char* was = context;
			tick(manager);
			context = was;
		}

		char __fastcall pump(void* manager, void*)
		{
			if (std::strcmp(context, "other") == 0) pumped_at = GetTickCount();
			context = "pump";
			const char result = reinterpret_cast<pump_t>(native_pump)(manager);
			++pumps;
			activity();
			tick(manager);
			join_in_progress::tick();
			context = "other";
			return result;
		}

		// The datagram parser task (0x60a8f0, mask 0x7fffffff) -> 0x60dca0: the native parser, then the GNS messages,
		// in the scheduler states the native parser runs in (not the pump task's: it also runs while a zone loads, when
		// the lockstep handlers must not see input - live: 0x6117b0 -> 0x559eb0 read null at 0x559faf).
		// The periodic flush task (0x60a830, mask 0x3ffffffe, every Nth tick) -> 0x60fe00: the native flush, then the
		// adapter's. It runs in scheduler states the pump task (0xfffffffc) does not - natively a reply enqueued there
		// still leaves at once; with only the pump flushing, the host's answer to the joiner's final 'J' waited out the
		// host's whole zone load, the joiner loaded after the host and stayed at "Waiting for Other Players".
		void __fastcall flush_task(void* manager, void*)
		{
			context = "flush";
			reinterpret_cast<drain_t>(native_flush)(manager);
			if (manager == manager_ && ready)
			{
				// The host's Adding Player/load wait can stop the receive-pump task. Keep networking and the
				// bring-in alive in this scheduler state; dispatch still belongs to the native parser/drain.
				if (join_in_progress::can_service()) { pump(manager, nullptr); context = "flush"; }
				else if (stalled_service()) serve_stalled(manager);
				flush();
			}
			context = "other";
		}
		// The reliable drain task (0x60a7d0, mask 0x7ffffffe) -> 0x610680: the native drain, then the adapter's flush,
		// so a reply a handler queued while the drain delivered leaves in the same tick.
		void __fastcall drain_task(void* manager, void*)
		{
			context = "drain";
			reinterpret_cast<drain_t>(native_drain)(manager);
			if (manager == manager_ && ready) flush();
			context = "other";
		}

		void __fastcall parse(void* manager, void*)
		{
			context = "parser";
			if (manager == manager_ && ready && join_in_progress::can_service()) { pump(manager, nullptr); context = "parser"; }
			else if (manager == manager_ && ready && stalled_service()) serve_stalled(manager);
			reinterpret_cast<drain_t>(native_parser)(manager);
			++drains;
			activity();
			if (manager == manager_)
			{
				enter(manager);
				if (ready && join_in_progress::can_service())
				{
					reinterpret_cast<drain_t>(native_drain)(manager);
					join_in_progress::tick();
					flush();
				}
			}
			context = "other";
		}

		// The body length as the native copy 0x615840 resolves it: a body with length 0 is a fixed-size message whose
		// length is the opcode's entry in the table at 0xa3e490 (filled at run time; written back into the header, as
		// 0x615840 does). Live: lockstep messages are sent this way, and a 0-length delivery made 0x559eb0 read null.
		std::uint16_t body_length(std::uint8_t* header)
		{
			auto& length = at<std::uint16_t>(header, 8);
			if (at<const void*>(header, 0) && !length) length = reinterpret_cast<const std::uint16_t*>(fixed_lengths)[header[0x10]];
			return at<const void*>(header, 0) ? length : 0;
		}

		link* switched_for_endpoint(void* manager, const void* endpoint)
		{
			if (manager != manager_ || links.empty()) return nullptr;
			// The enqueue's own lookup (0x60db20: the first listed peer whose +4 equals the endpoint).
			auto* l = by_peer(reinterpret_cast<peer_of_t>(native_peer_of)(manager, endpoint));
			return l && l->switched ? l : nullptr;
		}
	}

	// -1: not a switched peer, the native code runs; otherwise the native function's result.
	int __stdcall enqueue(void* manager, void* endpoint, std::uint8_t* header)
	{
		++enqueues;
		activity();
		auto* l = switched_for_endpoint(manager, endpoint);
		if (!l) return -1;
		++enqueued_gns;
		++sent_by[header[0x10]][(header[0xe] ? 1 : 0) + (header[0xf] ? 2 : 0)];
		if (rare(header[0x10])) logger::write_capped("gns-trace", "online: GNS trace: queued 0x%02x in the %s task", unsigned(header[0x10]), context);
		const auto opcode = header[0x10];
		// A link GNS closed: the message is lost like a datagram to a peer gone quiet (the native timeout ends it).
		if (l->closed || gns_transport_core::fragment_opcode(opcode)) return 1;
		if (!l->out.enqueue(opcode, header[0xe] != 0, header[0xf] != 0, at<const std::uint8_t*>(header, 0), body_length(header)))
			close(*l, "its outbound queue exceeded 2 MiB", false);
		return 1;
	}
	int __stdcall direct(void* manager, const std::uint8_t* address, std::uint32_t port, std::uint8_t* header)
	{
		++directs;
		if (manager != manager_ || !address) return -1;
		for (auto& l : links)
		{
			if (!l->switched || at<std::uint32_t>(l->peer, peer_address) != at<const std::uint32_t>(const_cast<std::uint8_t*>(address), 8) ||
			    at<std::uint16_t>(l->peer, peer_port) != std::uint16_t(port)) continue;
			if (l->closed || gns_transport_core::fragment_opcode(header[0x10])) return 1;
			message m;
			m.opcode = header[0x10];
			const auto length = body_length(header);
			if (length) m.body.assign(at<const std::uint8_t*>(header, 0), at<const std::uint8_t*>(header, 0) + length);
			send(*l, gns_transport_core::frame(m), false);
			return 1;
		}
		return -1;
	}
	// 0x60f940 flush-one-peer entry (session send / broadcast mode 2: enqueue, then this at once): the switched peer's
	// buffer leaves now, as the native datagram would. Live: the host's 0x26 answer to the final campaign 'J' is a
	// mode-2 broadcast inside the 'J' handler, which then runs ~1 s of campaign work; buffered until the next tick, it
	// arrived after the host had started its zone, and the joiner waited at "Waiting for Other Players".
	void __stdcall peer_flush(void* manager, void* peer)
	{
		if (manager != manager_ || !ready) return;
		if (auto* l = by_peer(peer)) { const char* was = context; context = "mode-2"; flush_link(*l); context = was; }
	}
	int __stdcall backlog(void* peer, std::uint32_t opcode)
	{
		++backlogs;
		auto* l = by_peer(peer);
		if (!l || !l->switched) return -1;
		outstanding(*l);
		return int(l->out.count(std::uint8_t(opcode)) + l->in.count(std::uint8_t(opcode)) + native_queued(static_cast<std::uint8_t*>(peer), std::uint8_t(opcode)));
	}

	// 0x611550 drop notice (ecx session; index, lost code, others' code, flag) / 0x612c40 removal (ecx session; record,
	// reason, flag): 1 = deferred to the agreed cut frame (the native call is skipped), 0 = the native code runs.
	int __stdcall notice_filter(void* session, std::int32_t index, std::uint32_t lost_code)
	{
		if (own_removal || !ready || lost_code != 0x10cc || index <= 0) return 0;
		auto* record = record_of_index(session, index);
		return record && defer_native_removal(session, record, "drop notice") ? 1 : 0;
	}
	int __stdcall lost_filter(void* session, std::uint8_t* record, std::uint32_t reason)
	{
		if (own_removal || !ready || (reason != 0x10cc && reason != 0x10cd)) return 0;
		return defer_native_removal(session, record, "removal") ? 1 : 0;
	}

	namespace
	{
		const std::uint32_t notice_resume_at = notice_resume, lost_resume_at = lost_resume;
		__declspec(naked) void notice_bridge()
		{
			__asm {
				push ecx
				push dword ptr [esp + 0xc]
				push dword ptr [esp + 0xc]
				push ecx
				call notice_filter
				pop ecx
				test eax, eax
				jnz skip
				sub esp, 0x20
				push ebp
				mov ebp, ecx
				jmp dword ptr [notice_resume_at]
			skip:
				ret 0x10
			}
		}
		__declspec(naked) void lost_bridge()
		{
			__asm {
				push ecx
				push dword ptr [esp + 0xc]
				push dword ptr [esp + 0xc]
				push ecx
				call lost_filter
				pop ecx
				test eax, eax
				jnz skip
				push ebp
				mov ebp, esp
				and esp, 0xfffffff8
				jmp dword ptr [lost_resume_at]
			skip:
				xor eax, eax
				ret 0xc
			}
		}
		const std::uint32_t enqueue_resume_at = enqueue_resume, direct_resume_at = direct_resume, backlog_resume_at = backlog_resume;
		__declspec(naked) void enqueue_bridge()
		{
			__asm {
				push ecx
				push dword ptr [esp + 0xc]
				push dword ptr [esp + 0xc]
				push ecx
				call enqueue
				pop ecx
				cmp eax, -1
				je native
				ret 8
			native:
				mov eax, dword ptr [esp + 4]
				sub esp, 8
				jmp dword ptr [enqueue_resume_at]
			}
		}
		__declspec(naked) void direct_bridge()
		{
			__asm {
				push ecx
				push dword ptr [esp + 0x10]
				push dword ptr [esp + 0x10]
				push dword ptr [esp + 0x10]
				push ecx
				call direct
				pop ecx
				cmp eax, -1
				je native
				ret 0xc
			native:
				sub esp, 0x518
				jmp dword ptr [direct_resume_at]
			}
		}
		const std::uint32_t peer_flush_resume_at = peer_flush_resume;
		__declspec(naked) void peer_flush_bridge()
		{
			__asm {
				push ecx
				push dword ptr [esp + 8]
				push ecx
				call peer_flush
				pop ecx
				sub esp, 0x28
				push ebx
				push ebp
				jmp dword ptr [peer_flush_resume_at]
			}
		}
		__declspec(naked) void backlog_bridge()
		{
			__asm {
				push ecx
				push dword ptr [esp + 8]
				push ecx
				call backlog
				pop ecx
				cmp eax, -1
				je native
				ret 4
			native:
				mov edx, dword ptr [ecx + 0x309c]
				jmp dword ptr [backlog_resume_at]
			}
		}
		std::vector<unsigned char> jump(std::uint32_t va, const void* target, std::size_t size)
		{
			std::vector<unsigned char> out(size, 0x90);
			out[0] = 0xe9;
			const auto relative = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)) - (va + 5);
			std::memcpy(out.data() + 1, &relative, 4);
			return out;
		}
		const gns_transport_rules::guard* mismatch()
		{
			__try
			{
				const auto* image = reinterpret_cast<const std::uint8_t*>(limits_rules::image_base);
				for (const auto& g : gns_transport_rules::guards)
				{
					if (limits_rules::matches(image + g.va - limits_rules::image_base, g.hex)) continue;
					// The stock-path receive validation (startup transaction, committed before this install) replaces the
					// dispatcher entry with its own jump after verifying the same retail bytes; that one patch is expected.
					if (g.va == gns_transport_rules::native_dispatch && receive_validation::owns_dispatch_entry()) continue;
					return &g;
				}
				return nullptr;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return &gns_transport_rules::guards[0]; }
		}
	}

	void install(HMODULE game)
	{
		using namespace gns_transport_rules;
		const auto value = ini::text(L"Online", L"Transport");
		// Rooms ([Online] Host=broker / JoinCode) need the transport: they imply Transport=gns.
		if ((!value || _stricmp(value->c_str(), "gns") != 0) && !online_room::requested()) return;
		state = 0;
		if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base || mismatch())
		{
			logger::write("online: [Online] Transport=gns refused - the retail network code differs; the stock transport is used");
			return;
		}
#ifndef NDEBUG
		fake_loss = ini::number(L"Dev", L"GnsFakePacketLoss", 0);
		fake_lag = ini::number(L"Dev", L"GnsFakePacketLag", 0);
		legacy_cut = ini::number(L"Dev", L"LostCutLegacy", 0) == 1;
		delay_to = ini::number(L"Dev", L"GnsDelayTo", -1);
		delay_ms = ini::number(L"Dev", L"GnsDelayMs", 0);
		if (const auto trigger = ini::text(L"Dev", L"GnsDelayTrigger"); trigger && !trigger->empty())
		{
			wchar_t folder[MAX_PATH]{};
			GetModuleFileNameW(nullptr, folder, MAX_PATH);
			std::wstring path(folder);
			path = path.substr(0, path.find_last_of(L"\\/") + 1);
			for (const char c : *trigger) path += wchar_t(static_cast<unsigned char>(c));
			delay_trigger = path;
		}
		if (delay_to >= 0 && delay_ms > 0)
			logger::write("online: [Dev] GnsDelayTo=%d GnsDelayMs=%d%s - game messages to that participant are held back (development build)", delay_to, delay_ms, delay_trigger.empty() ? "" : " while the trigger file exists");
#endif
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, {
			{enqueue_va, bytes(enqueue_original), jump(enqueue_va, &enqueue_bridge, enqueue_original.size())},
			{direct_va, bytes(direct_original), jump(direct_va, &direct_bridge, direct_original.size())},
			{backlog_va, bytes(backlog_original), jump(backlog_va, &backlog_bridge, backlog_original.size())},
			{pump_slot, bytes(pump_original), xml1_hook_transaction::pointer_bytes(reinterpret_cast<const void*>(&pump))},
			{parser_jump, bytes(parser_original), jump(parser_jump, reinterpret_cast<const void*>(&parse), parser_original.size())},
			{flush_jump, bytes(flush_original), jump(flush_jump, reinterpret_cast<const void*>(&flush_task), flush_original.size())},
			{drain_jump, bytes(drain_original), jump(drain_jump, reinterpret_cast<const void*>(&drain_task), drain_original.size())},
			{peer_flush_va, bytes(peer_flush_original), jump(peer_flush_va, &peer_flush_bridge, peer_flush_original.size())},
			{native_drop_notice, bytes(notice_original), jump(native_drop_notice, &notice_bridge, notice_original.size())},
			{native_lost, bytes(lost_original), jump(native_lost, &lost_bridge, lost_original.size())}}, &safe))
		{
			state = 1;
			gameplay_agreement::register_rule(gameplay_capabilities::gns_transport_id, agreement_revision, {1u, gns_port_offset, framing_revision, 0u}, true);
			logger::write("online: Transport=gns installed - participants' game messages go whole over GameNetworkingSockets on the stock port + %u (rule 25); admission and lobby stay stock; every PC must use it",
			              unsigned(gns_port_offset));
		}
		else if (safe)
			logger::write("online: [Online] Transport=gns transaction refused - the stock transport is used");
		else
		{
			game_version::refuse_startup();
			logger::write("online: [Online] Transport=gns rollback/protection verification failed; startup refused");
		}
	}
}
#else
namespace gns_transport
{
	void install(HMODULE)
	{
		const auto value = ini::text(L"Online", L"Transport");
		if (!value || _stricmp(value->c_str(), "gns") != 0) return;
		state = 0;
		logger::write("online: [Online] Transport=gns needs a build with GameNetworkingSockets - the stock transport is used");
	}
}
#endif

namespace gns_transport
{
	bool requested() { return state.load() != -1; }
	bool installed() { return state.load() == 1; }
}
