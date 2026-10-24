#include <winsock2.h>
#include <ws2tcpip.h>
#include "online_room.hpp"
#include "receive_validation.hpp"
#include "save_format.hpp"
#include "online_room_core.hpp"
#include "online_browser_core.hpp"
#include "discord_rules.hpp"
#include "join_in_progress.hpp"
#include "online_security.hpp"
#include "gns_transport_core.hpp"
#include "network_validation.hpp"
#include "iat_hook.hpp"
#include "limits_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"
#include "game_version.hpp"
#include "online_status.hpp"
#include "ini.hpp"
#include "log.hpp"
#include <intrin.h>
#include <algorithm>
#include <cstring>
#include <atomic>
#include <cstdio>
#include <deque>
#include <fstream>
#include <memory>
#include <mutex>
#include <map>
#include <string>
#include <thread>
#include <vector>

#if XML2_FIX_GNS
#include "broker_tls.hpp"
#include <steam/isteamnetworkingutils.h>
#include <steam/steamnetworkingcustomsignaling.h>
#include <bcrypt.h>
#include <shlobj.h>
#include <wincrypt.h>

// Ed25519 (ed25519-donna, public domain) as linked into the GameNetworkingSockets static library (its
// USE_CRYPTO25519=Reference build exports these C symbols): this install's key signs the broker challenge, and the
// broker's certificate certifies the same key for GNS.
extern "C" void ed25519_publickey(const unsigned char* secret, unsigned char* public_key);
extern "C" void ed25519_sign(const unsigned char* message, size_t size, const unsigned char* secret, const unsigned char* public_key, unsigned char* signature);
extern "C" int ed25519_sign_open(const unsigned char* message, size_t size, const unsigned char* public_key, const unsigned char* signature);
#endif

namespace online_room
{
	namespace
	{
		namespace core = online_room_core;
		bool wanted = false, hosting = false;
		std::string configured_code, broker_address, broker_pin;
		core::room_metadata metadata;
		std::string configured_room_name; // [Online] RoomName as read (public rooms only)
		bool relay_only = false;
	}
}

#if XML2_FIX_GNS
namespace online_room
{
	namespace
	{
		using sendto_t = int(WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
		using recvfrom_t = int(WSAAPI*)(SOCKET, char*, int, int, sockaddr*, int*);
		sendto_t real_sendto = nullptr;
		recvfrom_t real_recvfrom = nullptr;
		// The receive pump's recvfrom (0x616385 in 0x616340, the game socket's only reader): tunnelled datagrams are
		// handed to this caller only.
		constexpr std::uintptr_t pump_recvfrom_return = 0x61638a;
		constexpr std::uint16_t game_port = 5165; // the host's fixed game port (0x606eba, 0x608314), the LAN search's target
		bool retail_pump()
		{
			// 0x616385 call 0x671f5c; 0x671f5c jmp [0x67f2a4] (recvfrom's import)
			__try { return limits_rules::matches(reinterpret_cast<const std::uint8_t*>(0x616385), "e8d2bb0500") && limits_rules::matches(reinterpret_cast<const std::uint8_t*>(0x671f5c), "ff25a4f26700"); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		// ---- this install's key (the user's profile, not the game folder; DPAPI-protected) ----
		std::uint8_t seed[core::seed_size]{}, public_key[core::public_key_size]{};
		bool key_loaded = false, key_failed = false;
		std::string own_id;
		std::uint32_t own_address = 0;

		std::wstring key_path(bool public_part)
		{
			wchar_t* local = nullptr;
			if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &local))) return {};
			std::wstring folder = std::wstring(local) + L"\\XML2 Fix\\online";
			CoTaskMemFree(local);
			// One key per install: the game folder's path, lower-cased, hashed (FNV-1a 64).
			wchar_t exe[MAX_PATH]{};
			GetModuleFileNameW(nullptr, exe, MAX_PATH);
			std::wstring dir(exe);
			dir.resize(dir.find_last_of(L"\\/") == std::wstring::npos ? 0 : dir.find_last_of(L"\\/"));
			std::uint64_t h = 1469598103934665603ull;
			for (wchar_t c : dir) { h ^= std::uint64_t(towlower(c)); h *= 1099511628211ull; }
			wchar_t name[64];
			swprintf_s(name, L"\\install-%016llx.%s", h, public_part ? L"pub" : L"key");
			SHCreateDirectoryExW(nullptr, folder.c_str(), nullptr);
			return folder + name;
		}
		// The broker's cert_ok line for this install, kept next to the key (public data: the certificate the project CA
		// signed for this install's public key). Reused while it has more than an hour left, so a player can start
		// online games all day without hitting the broker's 4 certificate requests per identity per day.
		std::wstring cert_cache;
		// The broker's turn_ok line (TURN credentials, valid 6 h; the broker allows a few a day per identity), sealed with
		// DPAPI for this Windows user because it holds the relay password. Reused while more than 30 minutes are left
		// and the broker (address and TURN secret id) is the same; replaced after a relay failure with it.
		std::wstring turn_cache;
		bool turn_from_cache = false, turn_refreshed = false;
		std::string load_sealed(const std::wstring& path)
		{
			std::vector<std::uint8_t> blob;
			{
				std::ifstream in(std::filesystem::path(path), std::ios::binary);
				if (!in.is_open()) return {};
				blob.assign(std::istreambuf_iterator<char>(in), {});
			}
			if (blob.empty() || blob.size() > 65536) return {};
			DATA_BLOB sealed{DWORD(blob.size()), blob.data()}, plain{};
			if (!CryptUnprotectData(&sealed, nullptr, nullptr, nullptr, nullptr, online_security::dpapi_current_user_flags, &plain)) return {};
			std::string text(reinterpret_cast<const char*>(plain.pbData), plain.cbData);
			online_security::secure_erase(plain.pbData, plain.cbData);
			LocalFree(plain.pbData);
			return text;
		}
		bool save_sealed(const std::wstring& path, const std::string& text)
		{
			DATA_BLOB plain{DWORD(text.size()), reinterpret_cast<BYTE*>(const_cast<char*>(text.data()))}, sealed{};
			if (!CryptProtectData(&plain, L"XML2 Fix TURN credentials", nullptr, nullptr, nullptr, online_security::dpapi_current_user_flags, &sealed)) return false;
			const bool saved = online_security::atomic_replace(std::filesystem::path(path), sealed.pbData, sealed.cbData);
			LocalFree(sealed.pbData);
			return saved;
		}
		bool load_key()
		{
			if (key_loaded || key_failed) return key_loaded;
			key_failed = true;
			const auto path = key_path(false);
			if (path.empty()) { logger::write("online: rooms off - no profile folder for this install's key"); return false; }
			const std::filesystem::path file_path(path);
			std::error_code existence_error;
			const bool exists = std::filesystem::exists(file_path, existence_error);
			if (existence_error)
			{
				logger::write("online: rooms off - this install's key (%ls) cannot be checked", file_path.filename().c_str());
				return false;
			}
			std::vector<std::uint8_t> blob;
			bool readable = false;
			{
				std::ifstream in(file_path, std::ios::binary);
				readable = in.is_open();
				if (readable) blob.assign(std::istreambuf_iterator<char>(in), {});
				readable = readable && !in.bad();
			}
			const auto action = online_security::key_load_action(exists, readable, blob.size());
			if (action == online_security::key_action::refuse)
			{
				logger::write("online: rooms off - this install's key (%ls) is empty or unreadable; delete it to reset this identity", file_path.filename().c_str());
				return false;
			}
			bool created = false;
			if (action == online_security::key_action::decrypt)
			{
				DATA_BLOB sealed{DWORD(blob.size()), blob.data()}, plain{};
				if (!CryptUnprotectData(&sealed, nullptr, nullptr, nullptr, nullptr, online_security::dpapi_current_user_flags, &plain) || plain.cbData != core::seed_size)
				{
					if (plain.pbData) { online_security::secure_erase(plain.pbData, plain.cbData); LocalFree(plain.pbData); }
					logger::write("online: rooms off - this install's key (%ls) could not be read for this Windows user; delete it to reset this identity", file_path.filename().c_str());
					return false;
				}
				std::memcpy(seed, plain.pbData, core::seed_size);
				online_security::secure_erase(plain.pbData, plain.cbData);
				LocalFree(plain.pbData);
			}
			else
			{
				if (!BCRYPT_SUCCESS(BCryptGenRandom(nullptr, seed, sizeof(seed), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
				{
					logger::write("online: rooms off - no random source for a new key");
					return false;
				}
				DATA_BLOB plain{DWORD(sizeof(seed)), seed}, sealed{};
				if (!CryptProtectData(&plain, L"XML2 Fix online identity", nullptr, nullptr, nullptr, online_security::dpapi_current_user_flags, &sealed))
				{
					logger::write("online: rooms off - the new key could not be protected");
					return false;
				}
				const bool saved = online_security::atomic_replace(file_path, sealed.pbData, sealed.cbData);
				online_security::secure_erase(sealed.pbData, sealed.cbData);
				LocalFree(sealed.pbData);
				if (!saved) { logger::write("online: rooms off - the new key could not be saved (%ls)", file_path.filename().c_str()); return false; }
				created = true;
			}
			ed25519_publickey(seed, public_key);
			own_id = core::identity(public_key);
			// The public half next to it, plain (tests mint certificates for it; it is no secret).
			const auto public_text = core::base64(public_key, sizeof(public_key)) + "\n";
			cert_cache = key_path(true);
			cert_cache.replace(cert_cache.size() - 3, 3, L"crt");
			turn_cache = key_path(true);
			turn_cache.replace(turn_cache.size() - 3, 3, L"turn");
			const std::filesystem::path public_path(key_path(true));
			if (!online_security::atomic_replace(public_path, public_text.data(), public_text.size()))
				logger::write("online: the public key could not be saved (%ls)", public_path.filename().c_str());
			key_failed = false;
			key_loaded = true;
			logger::write("online: this install is %s (key %s); its room address is assigned after the broker orders the room", own_id.c_str(), created ? "created" : "loaded");
			return true;
		}

		// ---- the broker connection (its own thread: verified TLS 1.2+; the game's thread reads bounded lines) ----
		std::mutex lock;
		std::deque<std::string> inbox, outbox;
		std::size_t inbox_bytes = 0, outbox_bytes = 0;
		constexpr std::size_t broker_queue_cap = 256 * 1024, broker_pending_cap = 64 * 1024;
		std::atomic<bool> stop{false};
		bool broker_started = false, broker_up = false, broker_gone = false;
		std::string broker_error;

		bool post(const std::string& line)
		{
			std::lock_guard g(lock);
			if (outbox.size() >= 256 || line.size() > broker_queue_cap || outbox_bytes > broker_queue_cap - line.size()) return false;
			outbox.push_back(line);
			outbox_bytes += line.size();
			return true;
		}
		void arrived(const std::string& line)
		{
			std::lock_guard g(lock);
			if (line.size() > broker_queue_cap) return;
			while (!inbox.empty() && (inbox.size() >= 256 || inbox_bytes > broker_queue_cap - line.size()))
			{
				inbox_bytes -= inbox.front().size();
				inbox.pop_front();
			}
			inbox.push_back(line);
			inbox_bytes += line.size();
		}
		// kind: the contract's error id for the launcher (online-status.json): unreachable / rejected / tls.
		void gone_text(const std::string& text, const char* kind = "broker_unreachable") { arrived(core::request("_closed", {{"msg", text}, {"kind", kind}})); }
		void gone(const char* why, int code)
		{
			char text[200];
			std::snprintf(text, sizeof(text), "%s (%d)", why, code);
			gone_text(text);
		}
		// A failed TLS handshake: a certificate the pin or the system's trust refuses is tls_pin_mismatch; a
		// connection that ended or failed before the broker answered is broker_unreachable.
		const char* tls_kind(const std::string& error)
		{
			return error.find("certificate") != std::string::npos || error.find("BrokerPin") != std::string::npos || error.find("SPKI") != std::string::npos
				? "tls_pin_mismatch" : "broker_unreachable";
		}

		void broker_thread(std::string where, bool host, std::string code, std::string pin, core::room_metadata room)
		{
			const auto colon = where.rfind(':');
			const std::string name = colon == std::string::npos ? where : where.substr(0, colon);
			const std::string port = colon == std::string::npos ? "10020" : where.substr(colon + 1);
			addrinfo hints{}, *found = nullptr;
			hints.ai_family = AF_INET;
			hints.ai_socktype = SOCK_STREAM;
			if (getaddrinfo(name.c_str(), port.c_str(), &hints, &found) != 0 || !found) { gone("the broker's address does not resolve", WSAGetLastError()); return; }
			SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
			u_long nonblocking = 1;
			ioctlsocket(s, FIONBIO, &nonblocking);
			connect(s, found->ai_addr, int(found->ai_addrlen));
			freeaddrinfo(found);
			{
				fd_set w, e;
				FD_ZERO(&w); FD_ZERO(&e); FD_SET(s, &w); FD_SET(s, &e);
				timeval t{5, 0};
				int socket_error = 0, error_size = sizeof(socket_error);
				if (select(0, nullptr, &w, &e, &t) <= 0 || FD_ISSET(s, &e) || getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socket_error), &error_size) != 0 || socket_error)
				{
					closesocket(s);
					gone("the broker does not answer", socket_error ? socket_error : WSAGetLastError());
					return;
				}
			}
			broker_tls::client tls;
			std::string error;
			if (!tls.handshake(s, name, pin, error)) { closesocket(s); gone_text(error, tls_kind(error)); return; }

			core::broker_line_buffer lines;
			std::string pending, failure, cached_cert, cached_turn;
			const char* failure_kind = "broker_unreachable";
			std::vector<std::uint8_t> wire;
			DWORD pinged = GetTickCount();
			bool authed = false;
			auto append_pending = [&](const std::string& line)
			{
				if (core::bounded_append(pending, line, broker_pending_cap)) return true;
				failure = "the broker send queue exceeded 65536 bytes";
				return false;
			};
			auto consume = [&](const std::string& row)
			{
				const auto msg = core::parse(row);
				if (msg.valid && msg.text("op") == "challenge")
				{
					if (msg.number("v", 0) != 2) { failure = "the broker does not speak protocol 2"; failure_kind = "broker_rejected"; return false; }
					std::vector<std::uint8_t> nonce;
					if (!core::unbase64(msg.text("nonce"), nonce) || nonce.size() != core::nonce_size) { failure = "the broker's challenge is malformed"; return false; }
					const auto signed_bytes = core::auth_message(nonce, public_key);
					std::uint8_t signature[core::signature_size];
					ed25519_sign(signed_bytes.data(), signed_bytes.size(), seed, public_key, signature);
					return append_pending(core::request("auth", {{"pubkey", core::base64(public_key, sizeof(public_key))}, {"sig", core::base64(signature, sizeof(signature))}}));
				}
				if (msg.valid && msg.text("op") == "auth_ok" && !authed)
				{
					authed = true;
					// A cached certificate with more than an hour left is used instead of asking for a new one.
					const auto now = static_cast<long long>(_time64(nullptr));
					std::string cached;
					if (!cert_cache.empty())
					{
						std::ifstream in(std::filesystem::path(cert_cache), std::ios::binary);
						std::getline(in, cached);
						if (!core::cert_cache_usable(core::parse(cached), now)) cached.clear();
					}
					cached_cert = cached.empty() ? std::string() : core::with_fields(cached, {{"from_cache", "1"}});
					// Cached TURN credentials with more than 30 minutes left, for this broker and its current TURN secret.
					if (!turn_cache.empty())
					{
						auto text = load_sealed(turn_cache);
						if (!text.empty())
						{
							const auto verdict = core::turn_cache_check(core::parse(text), where, msg.text("turn_key"), now);
							if (verdict == core::turn_cache::use) cached_turn = core::with_fields(text, {{"from_cache", "1"}});
							else logger::write("online: cached TURN credentials not used (%s) - asking the broker", core::describe(verdict));
							online_security::secure_erase(text.data(), text.size());
						}
					}
					if ((cached.empty() && !append_pending(core::request("cert"))) || !append_pending(host ? core::host_request(room) : core::request("resolve", {{"code", code}}))) return false;
				}
				const bool room_ok = authed && (msg.text("op") == "host_ok" || msg.text("op") == "resolve_ok");
				if (room_ok && cached_turn.empty() && !append_pending(core::request("turn"))) return false;
				if (msg.valid && msg.text("op") == "cert_ok" && !cert_cache.empty())
					online_security::atomic_replace(std::filesystem::path(cert_cache), row.data(), row.size());
				if (msg.valid && msg.text("op") == "turn_ok" && !turn_cache.empty())
				{
					auto record = core::with_fields(row, {{"cached_for", where}});
					if (!save_sealed(turn_cache, record)) logger::write("online: the TURN credentials could not be kept for the next start");
					online_security::secure_erase(record.data(), record.size());
				}
				arrived(row);
				if (!cached_cert.empty()) { arrived(cached_cert); cached_cert.clear(); } // after auth_ok, as the broker would
				if (room_ok && !cached_turn.empty()) // after host_ok/resolve_ok, where the broker's turn_ok would come
				{
					arrived(cached_turn);
					online_security::secure_erase(cached_turn.data(), cached_turn.size());
					cached_turn.clear();
				}
				return true;
			};
			auto accept_plain = [&](const std::string& plain)
			{
				if (!plain.empty() && !lines.append(plain)) { failure = "the broker line buffer exceeded its cap"; return false; }
				for (;;)
				{
					std::string row;
					const auto result = lines.next(row);
					if (result == core::broker_line_buffer::result::need_more) return true;
					if (result == core::broker_line_buffer::result::overflow) { failure = "a broker line exceeded 16384 bytes"; return false; }
					if (!consume(row)) return false;
				}
			};
			while (!stop)
			{
				{
					std::lock_guard g(lock);
					while (!outbox.empty() && pending.size() + outbox.front().size() <= broker_pending_cap)
					{
						pending += outbox.front();
						outbox_bytes -= outbox.front().size();
						outbox.pop_front();
					}
				}
				while (!wire.empty())
				{
					const int n = send(s, reinterpret_cast<const char*>(wire.data()), int(wire.size()), 0);
					if (n > 0) { wire.erase(wire.begin(), wire.begin() + n); continue; }
					if (WSAGetLastError() == WSAEWOULDBLOCK) break;
					closesocket(s); gone("the broker connection broke", WSAGetLastError()); return;
				}
				if (wire.empty() && !pending.empty())
				{
					if (!tls.encrypt(pending, wire, error)) { closesocket(s); gone_text(error); return; }
					pending.clear();
				}
				std::string plain;
				if (!tls.decrypt(nullptr, 0, plain, error)) { closesocket(s); gone_text(error); return; }
				if (!accept_plain(plain)) { closesocket(s); gone_text(failure, failure_kind); return; }
				fd_set r;
				FD_ZERO(&r); FD_SET(s, &r);
				timeval t{0, 50000};
				if (select(0, &r, nullptr, nullptr, &t) > 0)
				{
					char chunk[4096];
					const int n = recv(s, chunk, sizeof(chunk), 0);
					if (n == 0) { closesocket(s); gone("the broker closed the connection", 0); return; }
					if (n < 0 && WSAGetLastError() != WSAEWOULDBLOCK) { closesocket(s); gone("the broker connection broke", WSAGetLastError()); return; }
					plain.clear();
					if (n > 0 && !tls.decrypt(reinterpret_cast<const std::uint8_t*>(chunk), std::size_t(n), plain, error)) { closesocket(s); gone_text(error); return; }
					if (!accept_plain(plain)) { closesocket(s); gone_text(failure, failure_kind); return; }
				}
				if (authed && GetTickCount() - pinged > 30000)
				{
					pinged = GetTickCount();
					if (!append_pending(core::request("ping"))) { closesocket(s); gone_text(failure, failure_kind); return; }
				}
			}
			// Leaving: the broker treats the closed connection as leave (a host's room closes).
			closesocket(s);
		}

		// ---- room state (the game's thread) ----
		std::string room_code, host_id, own_version_token;
		core::room_roster members;
		namespace st = online_status_core;
		std::string status_code() { return !room_code.empty() ? room_code : configured_code; }
		int status_players() { return int(members.active_size()); }
		// ---- the public browser's listing and the in-game code (contract v1.3/v1.4; the game's thread) ----
		namespace browser = online_browser_core;
		// The game's memory, read on its own thread: every copy SEH-guarded (a torn pointer reads as nothing).
		bool guarded_read(DWORD va, void* out, std::size_t size)
		{
			__try { std::memcpy(out, reinterpret_cast<const void*>(static_cast<std::uintptr_t>(va)), size); return true; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		struct game_memory
		{
			bool read(DWORD va, void* out, std::size_t size) { return va >= 0x10000 && size && va + size > va && guarded_read(va, out, size); }
		};
		bool game_readable = false; // discord_rules' guards matched: zone, Danger Room and session reads are the retail ones
		std::string read_text(DWORD va, std::size_t size)
		{
			std::string bytes(size, '\0');
			if (!guarded_read(va, bytes.data(), size)) return {};
			bytes.resize(std::strlen(bytes.c_str()));
			return bytes;
		}
		// The menu on screen: [[0x8aff18] + 0x86090] + 0xc, its name (frame_rate_rules' menu manager; vtable 0x6a236c).
		std::string current_menu()
		{
			std::uint32_t manager = 0, vtable = 0, menu = 0;
			if (!guarded_read(frame_rate_rules::menu_manager_cell, &manager, 4) || !manager || !guarded_read(manager, &vtable, 4) || vtable != discord_rules::menu_manager_vtable ||
			    !guarded_read(manager + frame_rate_rules::menu_current, &menu, 4) || !menu)
				return {};
			return read_text(menu + 0xc, 64);
		}
		// The online screen name: the Play Online name entry's text (see screen_name_at).
		std::string screen_name();
		browser::game_view read_game()
		{
			browser::game_view g;
			if (!game_readable) return g;
			game_memory memory;
			discord_rules::game_reader<game_memory> reader(memory);
			if (const auto s = reader.read())
			{
				g.zone = s->zone;
				g.zone_title = s->zone_title;
				g.danger_room = s->danger_room;
				g.session = s->session;
				g.playing = !s->loading && !s->movie && !s->menu;
			}
			g.screen_name = screen_name();
			return g;
		}
		browser::room_state room_progress;
		browser::presence_pacer pacer;
		core::room_metadata listed; // what the broker has (sent with host / the last presence)
		DWORD looked = 0;
		// The room code, as the host lobby menus show it ("Online room code: XXXX-XXXX"; the game keeps its own copy).
		char code_text[64]{}, short_code_text[64]{};
		std::string last_menu;
		std::uint32_t last_menu_object = 0, lobby_handle = 0;
		bool lobby_shown = false;
		int lobby_reapplied = 0;
		// The host lobby screens: the code in the bottom help bar, the game's own menu description text (a menu's five
		// desctext slots at +0x2f4, set by 0x5bc6b0(slot, text) - the text goes into the game's string pool - then the
		// help bar laid out again by 0x5ae6b0, as the game's own 0x5b00ec..0x5b0195 does; 0x5adc10 finds the item, for
		// the log). The slot is a free one of that menu (online_browser_core). Showing the item with 0x5ade10(name, 1),
		// as the game does next, hid the text (live).
		using desc_set_t = void(__thiscall*)(void*, std::int32_t, const char*);
		bool lobby_text_ready()
		{
			static const bool ok = [] { __try { return limits_rules::matches(reinterpret_cast<const std::uint8_t*>(0x5bc6b0), "8b44240485c07c1283f8057d0d8b542408528d0c81e8b6f0feff") &&
			                                           limits_rules::matches(reinterpret_cast<const std::uint8_t*>(0x5adc10), "8b44240485c0568bf1743a8038007435508d4c240ce856db") &&
			                                           limits_rules::matches(reinterpret_cast<const std::uint8_t*>(0x5cbbbf), "6a038d8df4020000e8e40affff") &&
			                                           limits_rules::matches(reinterpret_cast<const std::uint8_t*>(0x5ae6b0), "81ec30010000a1f8386f005355568bd9"); }
			                            __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }();
			return ok;
		}
		using item_find_t = void*(__thiscall*)(void*, const char*);
		const char* const desc_items[5] = {"desctext1", "desctext2", "desctext3", "desctext4", "desctext5"};
		std::uint32_t lobby_slot_value(std::uint32_t menu, int slot)
		{
			std::uint32_t handle = 0;
			return menu && slot >= 0 && slot < 5 && guarded_read(menu + 0x2f4 + slot * 4, &handle, 4) ? handle : 0;
		}
		bool lobby_item(std::uint32_t menu, int slot)
		{
			__try { return reinterpret_cast<item_find_t>(0x5adc10)(reinterpret_cast<void*>(static_cast<std::uintptr_t>(menu)), desc_items[slot]) != nullptr; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		bool lobby_code(std::uint32_t menu, int slot)
		{
			if (!code_text[0] || !menu || slot < 0 || slot > 4 || !lobby_text_ready()) return false;
			__try
			{
				reinterpret_cast<desc_set_t>(0x5bc6b0)(reinterpret_cast<void*>(static_cast<std::uintptr_t>(menu + 0x2f4)), slot, slot == 0 ? short_code_text : code_text);
				reinterpret_cast<void(__thiscall*)(void*)>(0x5ae6b0)(reinterpret_cast<void*>(static_cast<std::uintptr_t>(menu))); // lay out the help bar again
				return true;
			}
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		std::uint32_t current_menu_object()
		{
			std::uint32_t manager = 0, vtable = 0, menu = 0;
			if (!guarded_read(frame_rate_rules::menu_manager_cell, &manager, 4) || !manager || !guarded_read(manager, &vtable, 4) || vtable != discord_rules::menu_manager_vtable ||
			    !guarded_read(manager + frame_rate_rules::menu_current, &menu, 4))
				return 0;
			return menu;
		}
		void browser_tick();
		std::map<std::string, core::token_bucket> signal_rates;
		bool cert_ready = false, ice_ready = false, room_ready = false;
		HSteamListenSocket listener = k_HSteamListenSocket_Invalid;

		struct datagram { DWORD at; std::uint16_t from, to; std::vector<std::uint8_t> data; };
		struct received_datagram { std::uint32_t address; std::uint16_t from; std::vector<std::uint8_t> data; };
		struct tunnel
		{
			std::string id;
			std::uint32_t address = 0;
			HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
			bool outgoing = false, connected = false, transport_bound = false;
			int attempts = 0;
			DWORD started = 0, connected_at = 0, retry_at = 0, reported = 0;
			std::deque<datagram> waiting;
			std::size_t waiting_bytes = 0, game_bytes = 0;
			std::deque<SteamNetworkingMessage_t*> game;
			core::token_bucket datagram_packets{200, 200}, datagram_bytes{65536, 65536};
			std::uint64_t datagrams_out = 0, datagrams_in = 0, bytes_out = 0, bytes_in = 0, dropped = 0;
			// The first few tunnel messages refused on this connection are logged with the reason (the 10 s report only counts).
			void refused(const char* why, int kind, std::size_t length)
			{
				++dropped;
				if (dropped <= 4) logger::write("online: refused a tunnel message from %s (%s; kind %d, %zu bytes)%s", id.c_str(), why, kind, length, dropped == 4 ? " - further refusals are only counted" : "");
			}
		};
		std::vector<std::unique_ptr<tunnel>> tunnels;
		std::deque<received_datagram> arrivals;
		std::size_t arrivals_bytes = 0;
		constexpr std::size_t tunnel_game_cap = 1024 * 1024, arrivals_cap = 256 * 1024, waiting_cap = 128 * 1024;

		ISteamNetworkingSockets* sockets() { return SteamNetworkingSockets(); }
		tunnel* by_id(const std::string& id) { for (auto& t : tunnels) if (t->id == id) return t.get(); return nullptr; }
		tunnel* by_address(std::uint32_t a) { for (auto& t : tunnels) if (t->address == a) return t.get(); return nullptr; }
		tunnel* by_connection(HSteamNetConnection c) { for (auto& t : tunnels) if (t->connection == c && c != k_HSteamNetConnection_Invalid) return t.get(); return nullptr; }
		const char* route(HSteamNetConnection c)
		{
			SteamNetConnectionInfo_t info{};
			if (!sockets()->GetConnectionInfo(c, &info)) return "?";
			if (info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Relayed) return "relay";
			if (info.m_nFlags & k_nSteamNetworkConnectionInfoFlags_Fast) return "direct (local)";
			return "direct";
		}

		// GNS's signals for one member travel as the broker's signal op (SPEC.md 3.4); GNS may call from its own thread.
		struct signaling final : ISteamNetworkingConnectionSignaling
		{
			std::string peer;
			explicit signaling(std::string p) : peer(std::move(p)) {}
			bool SendSignal(HSteamNetConnection, const SteamNetConnectionInfo_t&, const void* data, int size) override
			{
				if (size <= 0 || size > 8192) return false;
				return post(core::request("signal", {{"to", peer}, {"data", core::base64(static_cast<const std::uint8_t*>(data), std::size_t(size))}}));
			}
			void Release() override { delete this; }
		};
		// A signal from a member the broker named: a connection request is answered only when the identity GNS carries
		// is that member's (the broker authenticated the sender; a signal claiming another identity is refused).
		struct incoming final : ISteamNetworkingSignalingRecvContext
		{
			std::string from;
			ISteamNetworkingConnectionSignaling* OnConnectRequest(HSteamNetConnection, const SteamNetworkingIdentity& peer, int) override
			{
				char text[SteamNetworkingIdentity::k_cchMaxString]{};
				peer.ToString(text, sizeof(text));
				if (core::gns_identity(from) != text)
				{
					logger::write("online: refused a connection request from %s carrying identity %s - identity mismatch", from.c_str(), text);
					return nullptr;
				}
				if (!room_ready || !members.contains(from))
				{
					logger::write("online: refused a connection request from %s - not a member of this room", from.c_str());
					return nullptr;
				}
				if (const auto* t = by_id(from); t && t->connected) return nullptr;
				return new signaling(from);
			}
			void SendRejectionSignal(const SteamNetworkingIdentity&, const void*, int) override {}
		};

		tunnel& ensure_tunnel(const std::string& id)
		{
			if (auto* t = by_id(id)) return *t;
			auto t = std::make_unique<tunnel>();
			t->id = id;
			t->address = members.address(id);
			if (!t->address || by_address(t->address) || t->address == own_address)
			{
				logger::write("online: member %s has no unique room-assigned virtual address - it stays unreachable", id.c_str());
				t->address = 0;
			}
			tunnels.push_back(std::move(t));
			return *tunnels.back();
		}
		void drop(tunnel& t, const char* why)
		{
			if (t.connection != k_HSteamNetConnection_Invalid)
			{
				sockets()->CloseConnection(t.connection, 0, why, false);
				logger::write("online: connection to %s closed (%s) - datagrams %llu out / %llu in", t.id.c_str(), why, t.datagrams_out, t.datagrams_in);
			}
			t.connection = k_HSteamNetConnection_Invalid;
			t.connected = t.transport_bound = false;
			for (auto* m : t.game) m->Release();
			t.game.clear();
			t.game_bytes = 0;
			t.waiting.clear();
			t.waiting_bytes = 0;
			for (std::size_t i = 0; i < arrivals.size();)
			{
				if (arrivals[i].address != t.address) { ++i; continue; }
				arrivals_bytes -= arrivals[i].data.size();
				arrivals.erase(arrivals.begin() + i);
			}
		}
		void forget(const std::string& id, const char* why)
		{
			for (std::size_t i = 0; i < tunnels.size(); ++i)
				if (tunnels[i]->id == id) { drop(*tunnels[i], why); tunnels.erase(tunnels.begin() + i); return; }
		}
		void connect(tunnel& t)
		{
			SteamNetworkingIdentity remote;
			remote.Clear();
			remote.ParseString(core::gns_identity(t.id).c_str());
			t.outgoing = true;
			t.started = GetTickCount();
			++t.attempts;
			t.connection = sockets()->ConnectP2PCustomSignaling(new signaling(t.id), &remote, 0, 0, nullptr);
			logger::write("online: connecting to %s (%s, attempt %d)%s", t.id.c_str(), core::dotted(t.address).c_str(), t.attempts,
			              t.connection == k_HSteamNetConnection_Invalid ? " - GNS refused to start it" : "");
		}
		// Who opens which connection: a joiner to the host; between two joiners the smaller identity.
		bool opens_to(const std::string& id) { return id == host_id || (!hosting && id != host_id && own_id < id); }

		void write_room_file()
		{
			wchar_t exe[MAX_PATH]{};
			GetModuleFileNameW(nullptr, exe, MAX_PATH);
			std::wstring path(exe);
			path.resize(path.find_last_of(L"\\/") + 1);
			path += L"online-room.txt";
			std::ofstream out(path, std::ios::trunc);
			out << room_code << "\n";
			logger::write("online: room code written to online-room.txt");
		}

		void apply_ice(const core::line& msg)
		{
			const auto servers = core::ice(msg["uris"].items, msg.text("username"), msg.text("password"));
			auto* u = SteamNetworkingUtils();
			if (!servers.stun.empty()) u->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_STUN_ServerList, servers.stun.c_str());
			if (!servers.turn.empty())
			{
				u->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_TURN_ServerList, servers.turn.c_str());
				u->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_TURN_UserList, servers.users.c_str());
				u->SetGlobalConfigValueString(k_ESteamNetworkingConfig_P2P_TURN_PassList, servers.passwords.c_str());
			}
			// Every candidate kind (host, server-reflexive, relay) normally; relay only on request. (SPEC.md suggests 3 =
			// relay + private, which leaves out the server-reflexive candidates two home networks need.)
			u->SetGlobalConfigValueInt32(k_ESteamNetworkingConfig_P2P_Transport_ICE_Enable,
			                             relay_only ? k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_Relay : k_nSteamNetworkingConfig_P2P_Transport_ICE_Enable_All);
			ice_ready = true;
			turn_from_cache = msg.text("from_cache") == "1";
			const auto expires = core::turn_expires(msg);
			const long long left = expires ? expires - static_cast<long long>(_time64(nullptr)) : msg.number("ttl");
			// Never the username or password: host:port lists only.
			logger::write("online: ICE: STUN %s, TURN %s (credentials %s, valid %lld s more), route %s", servers.stun.empty() ? "none" : servers.stun.c_str(),
			              servers.turn.empty() ? "none" : servers.turn.c_str(), turn_from_cache ? "reused from this install's cache" : "new from the broker",
			              left, relay_only ? "relay only ([Online] Route=relay)" : "any");
		}
		void apply_cert(const core::line& msg)
		{
#ifndef NDEBUG
			if (const auto mode = ini::text(L"Dev", L"OnlineCert"); mode && _stricmp(mode->c_str(), "none") == 0)
			{
				// Tests only: no certificate - GNS presents a self-signed one, which the pinned build must refuse.
				cert_ready = true;
				logger::write("online: [Dev] OnlineCert=none - no certificate installed, GNS presents an unsigned one (tests only)");
				return;
			}
#endif
			std::string pem = msg.text("cert_pem");
#ifndef NDEBUG
			if (const auto file = ini::text(L"Dev", L"OnlineCertFile"))
			{
				// Tests only: present another certificate (a rogue CA's, another identity's) instead of the broker's.
				std::ifstream in(*file, std::ios::binary);
				pem.assign(std::istreambuf_iterator<char>(in), {});
				logger::write("online: [Dev] OnlineCertFile - presenting %s instead of the broker's certificate (tests only)", file->c_str());
			}
#endif
			std::vector<std::uint8_t> signed_cert;
			if (!core::pem_body(pem, signed_cert)) { if (!cert_cache.empty()) DeleteFileW(cert_cache.c_str()); logger::write("online: the broker's certificate is malformed - rooms stay off"); return; }
			auto blob = core::certificate_blob(signed_cert, seed);
			SteamNetworkingErrMsg error{};
			const bool installed = sockets()->SetCertificate(blob.data(), int(blob.size()), error);
			online_security::secure_erase(blob.data(), blob.size());
			if (!installed)
			{
				if (!cert_cache.empty()) DeleteFileW(cert_cache.c_str()); // a stale or foreign cached certificate: ask again next start
				logger::write("online: GNS refused the certificate (%s) - rooms stay off", error);
				return;
			}
			cert_ready = true;
			logger::write("online: certificate for %s installed (expires %lld, %s)", msg.text("gns_identity").c_str(), msg.number("expires"),
			              msg.text("from_cache") == "1" ? "reused from this install's cache" : "new from the broker");
		}

		void handle(const core::line& msg)
		{
			const auto op = msg.text("op");
			if (op == "auth_ok")
			{
				broker_up = true;
				if (msg.text("identity") != own_id) logger::write("online: the broker names this PC %s, expected %s", msg.text("identity").c_str(), own_id.c_str());
#ifdef XML2_FIX_GNS_CA_PIN
				if (msg.text("ca") != XML2_FIX_GNS_CA_PIN)
				{
					broker_up = room_ready = false;
					broker_gone = stop = true;
					members.deactivate_all();
					logger::write("online: the broker's CA (%s) is not the one this build pins - room start refused", msg.text("ca").c_str());
					online_status::error(st::error::tls_pin_mismatch, "the broker's certificate authority is not the one this build trusts");
					return;
				}
#endif
				logger::write("online: broker %s: signed in as %s", broker_address.c_str(), msg.text("identity").c_str());
			}
			else if (op == "cert_ok") apply_cert(msg);
			else if (op == "turn_ok") apply_ice(msg);
			else if (op == "host_ok")
			{
				room_code = core::join_code(msg.text("code"));
				host_id = own_id;
				room_ready = !room_code.empty() && members.reset({own_id});
				own_address = members.address(own_id);
				if (!room_ready) { host_id.clear(); own_address = 0; logger::write("online: broker returned an invalid host response - admission stays off"); online_status::error(st::error::internal, "the broker returned an invalid room"); return; }
				logger::write("online: room %s open (host %s, up to %lld players) - joiners set [Online] JoinCode=%s", online_security::redact_join_code(room_code).c_str(), own_id.c_str(), msg.number("max_participants"), online_security::redact_join_code(room_code).c_str());
				write_room_file();
				online_status::set(st::state::room, room_code, status_players());
				online_status::room(metadata.public_room, listed.name);
				logger::write("online: room %s is %s", online_security::redact_join_code(room_code).c_str(),
				              metadata.public_room ? ("listed in the public game browser as \"" + online_security::sanitize_log_text(listed.name) + "\"").c_str() : "private (join by code only)");
				const auto line = browser::code_line(room_code);
				std::snprintf(code_text, sizeof(code_text), "%s", line.c_str());
				std::snprintf(short_code_text, sizeof(short_code_text), "%s", browser::short_code_line(room_code).c_str());
			}
			else if (op == "resolve_ok")
			{
				room_code = core::join_code(msg.text("code"));
				host_id = msg.text("host");
				std::vector<std::string> ordered;
				for (const auto& m : msg["members"].items) if (core::valid_identity(m)) ordered.push_back(m);
				room_ready = !room_code.empty() && core::valid_identity(host_id) && !ordered.empty() && ordered.front() == host_id && members.reset(ordered) && members.contains(own_id);
				own_address = members.address(own_id);
				if (!room_ready) { host_id.clear(); own_address = 0; logger::write("online: broker returned an invalid resolve response - admission stays off"); online_status::error(st::error::internal, "the broker returned an invalid room"); return; }
				// Another game build's room: refused before any connection (the room carries the host's version token).
				const auto room = core::parse(msg["room"].text);
				const auto theirs = room.valid ? room.text("version_token") : std::string();
				if (!theirs.empty() && !own_version_token.empty() && theirs != own_version_token)
				{
					room_ready = false;
					members.deactivate_all();
					host_id.clear();
					own_address = 0;
					logger::write("online: room %s is another game build (%s, this PC %s) - not joining", online_security::redact_join_code(room_code).c_str(),
					              online_security::sanitize_log_text(theirs).c_str(), own_version_token.c_str());
					online_status::error(st::error::version_mismatch, "the host plays another build (" + online_security::sanitize_log_text(theirs) + "), this PC " + own_version_token);
					return;
				}
				if (room.valid)
				{
					const bool listed_room = room["public"].t == core::value::type::boolean && room["public"].text == "true";
					online_status::room(listed_room, listed_room ? browser::clip_utf8(room.text("name"), browser::name_max) : std::string());
				}
				logger::write("online: join code %s: host %s at %s - in Join Game, Connect by IP ([Del]) type 0 (or that address), Enter; %zu members", online_security::redact_join_code(room_code).c_str(),
				              host_id.c_str(), core::dotted(members.address(host_id)).c_str(), members.active_size());
			}
			else if (op == "peer_joined")
			{
				const auto id = msg.text("identity");
				if (room_ready && core::valid_identity(id) && members.join(id))
				{
					logger::write("online: %s joined room %s (%s)", id.c_str(), online_security::redact_join_code(room_code).c_str(), core::dotted(members.address(id)).c_str());
					online_status::players(status_players());
				}
			}
			else if (op == "peer_left")
			{
				const auto id = msg.text("identity");
				members.leave(id);
				signal_rates.erase(id);
				forget(id, "left the room");
				logger::write("online: %s left room %s", id.c_str(), online_security::redact_join_code(room_code).c_str());
				online_status::players(status_players());
			}
			else if (op == "room_closed")
			{
				logger::write("online: room %s closed (%s); established P2P connections stay up, new admission stops", online_security::redact_join_code(room_code).c_str(), msg.text("reason").c_str());
				members.deactivate_all();
				signal_rates.clear();
				room_ready = false;
				// Before the session started the room is gone for good (a joiner can no longer get in).
				if (online_status::current() != st::state::in_game) online_status::error(st::error::code_expired, "the room closed (" + online_security::sanitize_log_text(msg.text("reason")) + ")");
			}
			else if (op == "signal")
			{
				const auto from = msg.text("from");
				const auto& encoded = msg.text("data");
				std::vector<std::uint8_t> data;
				if (!room_ready || !members.contains(from) || encoded.size() > 10924 || !core::unbase64(encoded, data) || data.empty() || data.size() > 8192) return;
				auto rate = signal_rates.try_emplace(from, 20, 20).first;
				if (!rate->second.allow(GetTickCount())) return;
				incoming context;
				context.from = from;
				if (!sockets()->ReceivedP2PCustomSignal(data.data(), int(data.size()), &context))
					logger::write_once("online-signal-refused", "online: GNS did not take a signal from %s", from.c_str());
			}
			else if (op == "error")
			{
				const auto code = msg.text("code"), about = msg.text("in_reply_to");
				if (about == "resolve" && code == "no_such_room")
					logger::write("online: join code %s is not an open room (wrong or expired code) - joining by code is not available", online_security::redact_join_code(configured_code).c_str());
				else
					logger::write("online: broker error %s%s%s: %s", code.c_str(), about.empty() ? "" : " in reply to ", about.c_str(), msg.text("msg").c_str());
				// A refused sign-in, room or join ends this run's online start; signal/ping refusals do not.
				// A TURN refusal once credentials are in use (a refresh) leaves the running room as it is.
				if (about == "resolve" || about == "host" || about == "auth" || about == "cert" || (about == "turn" && !ice_ready) || code == "auth_failed")
					online_status::error(st::from_broker(code), online_security::sanitize_log_text(msg.text("msg")));
			}
			else if (op == "_closed")
			{
				broker_gone = true;
				broker_error = msg.text("msg");
				const bool had_room = room_ready;
				room_ready = false;
				members.deactivate_all();
				logger::write("online: broker %s unavailable - %s; %s", broker_address.c_str(), broker_error.c_str(),
				              had_room ? "connections already open stay up, no new ones" : (hosting ? "no room was opened (the game can still be hosted by IP/LAN)" : "joining by code is not available"));
				// A session already running keeps going on its P2P connections; anything earlier cannot continue.
				if (online_status::current() != st::state::in_game)
				{
					const auto kind = msg.text("kind");
					const auto why = kind == "tls_pin_mismatch" ? st::error::tls_pin_mismatch : kind == "broker_rejected" ? st::error::broker_rejected : st::error::broker_unreachable;
					online_status::error(why, "broker " + broker_address + ": " + online_security::sanitize_log_text(broker_error));
				}
			}
		}

		void process_inbox()
		{
			std::deque<std::string> lines;
			{
				std::lock_guard g(lock);
				lines.swap(inbox);
				inbox_bytes = 0;
			}
			for (const auto& row : lines)
			{
				const auto msg = core::parse(row);
				if (msg.valid) handle(msg);
				else logger::write_once("online-bad-line", "online: a broker line was not understood");
			}
		}

		// Datagrams and messages arriving on room connections: datagrams for the game socket's reader, everything else
		// (the GNS transport's lane) kept for fetch(). One bounded batch per tunnel leaves GNS backpressure engaged.
		void poll()
		{
			SteamNetworkingMessage_t* batch[64];
			for (auto& t : tunnels)
			{
				if (!t->connected) continue;
				const int n = sockets()->ReceiveMessagesOnConnection(t->connection, batch, 64);
				const auto now = GetTickCount();
				for (int i = 0; i < n; ++i)
				{
					const auto* data = static_cast<const std::uint8_t*>(batch[i]->m_pData);
					const auto p = gns_transport_core::parse(data, std::size_t(batch[i]->m_cbSize));
					// The member's keepalive can overtake its hello at the start (seen once per session on the host): it carries
					// nothing, so it is ignored rather than counted as a refused message.
					if (!t->transport_bound && p.valid && p.k == gns_transport_core::kind::keepalive) { batch[i]->Release(); continue; }
					if (!gns_transport_core::tunnel_message_allowed(t->transport_bound, p))
					{
						t->refused(t->transport_bound ? "not allowed on a bound transport" : "not allowed before the transport is bound", int(p.k), std::size_t(batch[i]->m_cbSize));
						batch[i]->Release();
						continue;
					}
					if (p.k == gns_transport_core::kind::datagram)
					{
						if (!t->datagram_packets.allow(now) || !t->datagram_bytes.allow(now, p.length))
						{
							t->refused("datagram rate", int(p.k), p.length);
							batch[i]->Release();
							continue;
						}
						std::vector<std::uint8_t> bytes(p.body, p.body + p.length);
						if (!network_validation::valid_tunneled_datagram(bytes.data(), bytes.size()) || !core::confine_embedded_address(bytes, t->address, p.port))
						{
							t->refused(network_validation::valid_tunneled_datagram(bytes.data(), bytes.size()) ? "embedded address not this member's" : "datagram validation", int(p.k), p.length);
							batch[i]->Release();
							continue;
						}
						if (arrivals.size() >= 512 || p.length > arrivals_cap || arrivals_bytes > arrivals_cap - p.length)
						{
							++t->dropped;
							for (int k = i; k < n; ++k) batch[k]->Release();
							drop(*t, "its tunnelled datagram queue exceeded 256 KiB");
							break;
						}
						++t->datagrams_in;
						t->bytes_in += p.length;
						arrivals.push_back({t->address, p.port, std::move(bytes)});
						arrivals_bytes += p.length;
						batch[i]->Release();
						continue;
					}
					const auto size = std::size_t(batch[i]->m_cbSize);
					if (t->game.size() >= 8192 || size > tunnel_game_cap || t->game_bytes > tunnel_game_cap - size)
					{
						++t->dropped;
						for (int k = i; k < n; ++k) batch[k]->Release();
						drop(*t, "its transport receive queue exceeded 1 MiB");
						break;
					}
					t->game.push_back(batch[i]);
					t->game_bytes += size;
				}
			}
		}

		// The game writes its own address into some datagrams (the game info 0x18 answering a search: the address
		// and little-endian port at +13 of the datagram, live) and the other PC joins THAT address, not the one the
		// answer came from. Through a room connection this PC is its virtual address: the 6 bytes of the game socket's
		// address and port become the virtual address and the same port.
		void present_virtual(SOCKET s, std::vector<std::uint8_t>& data)
		{
			sockaddr_in a{};
			int size = sizeof(a);
			if (!own_address || getsockname(s, reinterpret_cast<sockaddr*>(&a), &size) != 0 || !a.sin_addr.s_addr) return;
			std::uint8_t real[6], virt[6];
			const std::uint16_t port = ntohs(a.sin_port);
			const std::uint32_t mine = htonl(own_address);
			std::memcpy(real, &a.sin_addr.s_addr, 4);
			std::memcpy(real + 4, &port, 2);
			std::memcpy(virt, &mine, 4);
			std::memcpy(virt + 4, &port, 2);
			for (std::size_t i = 0; i + 6 <= data.size(); ++i)
				if (std::memcmp(data.data() + i, real, 6) == 0) { std::memcpy(data.data() + i, virt, 6); i += 5; }
		}
		std::uint16_t port_of(SOCKET s)
		{
			sockaddr_in a{};
			int size = sizeof(a);
			return getsockname(s, reinterpret_cast<sockaddr*>(&a), &size) == 0 ? ntohs(a.sin_port) : 0;
		}
		bool send_datagram(tunnel& t, const datagram& d)
		{
			if (d.data.size() > gns_transport_core::max_datagram_body || t.connection == k_HSteamNetConnection_Invalid) return false;
			const auto frame = gns_transport_core::datagram(d.from, d.to, d.data.data(), d.data.size());
			const auto r = sockets()->SendMessageToConnection(t.connection, frame.data(), std::uint32_t(frame.size()), k_nSteamNetworkingSend_UnreliableNoNagle, nullptr);
			if (r != k_EResultOK) return false;
			++t.datagrams_out;
			t.bytes_out += d.data.size();
			return true;
		}

		bool session_bound()
		{
			for (const auto& t : tunnels)
				if (t->transport_bound) return true;
			return false;
		}
		int WSAAPI room_sendto(SOCKET s, const char* data, int length, int flags, const sockaddr* where, int size)
		{
			if (where && size >= int(sizeof(sockaddr_in)) && where->sa_family == AF_INET && length >= 0)
			{
				const auto& to = *reinterpret_cast<const sockaddr_in*>(where);
				const auto address = ntohl(to.sin_addr.s_addr);
				if (core::is_virtual(address))
				{
					// A member's virtual address: the datagram goes through that member's P2P connection (queued while it
					// opens); an address no member has is dropped like a datagram to nowhere.
					auto* t = by_address(address);
					if (!t) { logger::write_once("online-no-member", "online: the game sent to %s, which is no member of the room - dropped", core::dotted(address).c_str()); return length; }
					if (std::size_t(length) > gns_transport_core::max_datagram_body) return length;
					datagram d{GetTickCount(), port_of(s), ntohs(to.sin_port), std::vector<std::uint8_t>(data, data + length)};
					present_virtual(s, d.data);
					if (t->datagrams_out < 8) logger::write("online: tunnel trace: to %s:%u %d bytes, first byte %02x", core::dotted(address).c_str(), unsigned(d.to), length, length ? unsigned(std::uint8_t(data[0])) : 0u);
					if (!(t->connected && send_datagram(*t, d)))
					{
						while (!t->waiting.empty() && (t->waiting.size() >= 64 || t->waiting_bytes > waiting_cap - d.data.size()))
						{
							t->waiting_bytes -= t->waiting.front().data.size();
							t->waiting.pop_front();
						}
						t->waiting.push_back(std::move(d));
						t->waiting_bytes += t->waiting.back().data.size();
					}
					return length;
				}
				// The games list's LAN search (a broadcast to the game port): with a join code it asks the room's members
				// instead of the local network - a copy through each connected member's P2P connection; their answers come
				// back from their virtual addresses and list like LAN games. Without one (the host) it is the game's.
				if ((address == INADDR_BROADCAST || (address & 0xff) == 0xff) && ntohs(to.sin_port) == game_port && !configured_code.empty())
				{
					int asked = 0;
					for (auto& t : tunnels)
					{
						datagram d{GetTickCount(), port_of(s), ntohs(to.sin_port), std::vector<std::uint8_t>(data, data + length)};
						present_virtual(s, d.data);
						if (t->connected && send_datagram(*t, d)) ++asked;
					}
					logger::write_once("online-lan-search", "online: the games list's LAN search asks room %s's members (%d connected) instead of the local network", online_security::redact_join_code(configured_code).c_str(), asked);
					return length;
				}
			}
			return real_sendto(s, data, length, flags, where, size);
		}
		int WSAAPI room_recvfrom(SOCKET s, char* data, int length, int flags, sockaddr* from, int* size)
		{
			if (reinterpret_cast<std::uintptr_t>(_ReturnAddress()) == pump_recvfrom_return && from && size && *size >= int(sizeof(sockaddr_in)) && SteamNetworkingSockets())
			{
				if (arrivals.empty()) poll();
				if (!arrivals.empty())
				{
					auto d = std::move(arrivals.front());
					arrivals.pop_front();
					arrivals_bytes -= d.data.size();
					auto& bytes = d.data;
					// The game info 0x18 carries "1.30" at +67 (the field 0x6090d0 compares with this PC's version at
					// session +0x410) even when [Online] GameVersion changed the version, which it carries at +113 (live,
					// both PCs LC29): the answer is filed as another version's and never listed (the stock Connect by IP
					// on the test rig alike). Through a room connection the field gets the answering PC's own version.
					if (bytes.size() >= 118 && bytes[2] == 0x18 && std::memcmp(bytes.data() + 67, "1.30", 5) == 0 && std::memcmp(bytes.data() + 113, "1.30", 5) != 0)
					{
						char version[6];
						const bool printable = core::bounded_printable_field(bytes.data() + 113, bytes.size() - 113, version);
						std::memcpy(bytes.data() + 67, bytes.data() + 113, 5);
						if (printable) logger::write_once("online-gameinfo-version", "online: a room member's game info (0x18) names version 1.30 beside %s - listed under %s", version, version);
					}
					const int n = int(std::min<std::size_t>(d.data.size(), std::size_t(length)));
					if (n) std::memcpy(data, d.data.data(), std::size_t(n));
					auto& a = *reinterpret_cast<sockaddr_in*>(from);
					std::memset(&a, 0, sizeof(a));
					a.sin_family = AF_INET;
					a.sin_port = htons(d.from);
					a.sin_addr.s_addr = htonl(d.address);
					*size = sizeof(sockaddr_in);
					if (by_address(d.address) && by_address(d.address)->datagrams_in < 8)
					{
						std::string hex;
						char b[3];
						for (int k = 0; k < n && k < 64; ++k) { std::snprintf(b, sizeof(b), "%02x", unsigned(std::uint8_t(data[k]))); hex += b; }
						logger::write("online: tunnel trace: from %s:%u %d bytes: %s", core::dotted(d.address).c_str(), unsigned(d.from), n, hex.c_str());
					}
					return n;
				}
			}
			return real_recvfrom(s, data, length, flags, from, size);
		}
		// Join Game -> Connect by IP ([Del]) -> Enter IP takes digits and dots only (and no empty Done), so a code cannot
		// be typed there. With [Online] JoinCode set, anything that is not a whole dotted address (e.g. "0") joins the
		// room: the entry's copy into the game's address buffer (0x5cf078: call 0x5604f0(0xa3bc1c, 15, text)) gets the
		// host's virtual address. A whole address is the game's as before (LAN / direct IP still work).
		constexpr std::uint32_t enter_ip_copy = 0x5cf078, native_copy = 0x5604f0;
		// The games list's master-server path: 0x605bc0 (server added) and 0x605e70 (server updated) build an entry from
		// the GameSpy server-browsing keys (hostname, numplayers, maxplayers, gamemode, gamever) and allocate it with
		// the games table's 0x608c20 (thiscall, ECX = table from 0x6097b0, no stack arguments; null = no entry, both
		// callers skip). The LAN / Connect-by-IP path files answers through 0x6090d0's own call (0x609122), untouched.
		constexpr std::uint32_t sb_entry_added = 0x605d55, sb_entry_updated = 0x606171, games_entry_alloc = 0x608c20;
		void __cdecl enter_ip(char* destination, int size, const char* text)
		{
			std::string filled;
			if (text && std::count(text, text + std::strlen(text), '.') < 3 && !configured_code.empty())
			{
				for (int i = 0; i < 50 && host_id.empty() && !broker_gone && SteamNetworkingSockets(); ++i) { process_inbox(); if (host_id.empty()) Sleep(100); }
				if (!host_id.empty())
				{
					filled = core::dotted(members.address(host_id));
					logger::write("online: Connect by IP \"%s\" - joining room %s: host %s at %s", text, online_security::redact_join_code(configured_code).c_str(), host_id.c_str(), filled.c_str());
					text = filled.c_str();
				}
				else
					logger::write("online: Connect by IP without an address, but room %s is not resolved%s", online_security::redact_join_code(configured_code).c_str(), broker_gone ? " (the broker is unavailable)" : " (yet)");
			}
			reinterpret_cast<void(__cdecl*)(char*, int, const char*)>(native_copy)(destination, size, text);
		}
		bool session_bound();
		// With a join code, until the session runs, a master-list host gets no games-list entry: Join Game lists the room's
		// host alone (answered through its P2P connection). A master-list copy of the same host would join it by its
		// real address around the room (seen live: the joiner picked it and played over plain UDP, no NAT traversal).
		void* __fastcall master_list_entry(void* table, void*)
		{
			if (!configured_code.empty() && !session_bound())
			{
				logger::write_once("online-master-list", "online: Join Game lists room %s's game only - master server entries are not listed while joining by code",
				                   online_security::redact_join_code(configured_code).c_str());
				return nullptr;
			}
			return reinterpret_cast<void*(__thiscall*)(void*)>(games_entry_alloc)(table);
		}
		std::vector<unsigned char> call_to(std::uint32_t va, const void* target)
		{
			std::vector<unsigned char> out(5);
			out[0] = 0xe8;
			const auto relative = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)) - (va + 5);
			std::memcpy(out.data() + 1, &relative, 4);
			return out;
		}

		void report()
		{
			const auto now = GetTickCount();
			for (auto& t : tunnels)
			{
				if (!t->connected || now - t->reported < 10000) continue;
				t->reported = now;
				SteamNetConnectionRealTimeStatus_t st{};
				sockets()->GetConnectionRealTimeStatus(t->connection, &st, 0, nullptr);
				logger::write("online: connection to %s: route %s, ping %d ms, quality %.2f/%.2f, datagrams %llu out (%llu bytes) / %llu in (%llu bytes), %zu transport messages waiting, %llu dropped",
				              t->id.c_str(), route(t->connection), st.m_nPing, st.m_flConnectionQualityLocal, st.m_flConnectionQualityRemote, t->datagrams_out, t->bytes_out,
				              t->datagrams_in, t->bytes_in, t->game.size(), t->dropped);
			}
		}
	}

	namespace
	{
		// The online screen name: the session's local player name, 31 characters at +0x3e8 (getter 0x610d70; the Play
		// Online menus set it through 0x610d80: a default "<name><n>" first (0x5caeba), then the name entry's text).
		bool name_readable()
		{
			static const bool ok = [] { __try { return limits_rules::matches(reinterpret_cast<const std::uint8_t*>(0x610d70), "8d81e8030000c3") &&
			                                           limits_rules::matches(reinterpret_cast<const std::uint8_t*>(0x610d80), "8b442404568bf16a1f508d8ee803000051e8da13060083c40cc68607040000005e"); }
			                            __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }();
			return ok;
		}
		std::string screen_name()
		{
			std::uint32_t vtable = 0;
			if (!name_readable() || !guarded_read(discord_rules::session, &vtable, 4) || vtable != discord_rules::session_vtable) return {};
			return discord_rules::utf8_from_game(read_text(discord_rules::session + 0x3e8, 0x20));
		}
		void browser_tick()
		{
			if (!hosting || !room_ready || broker_gone) return;
			const DWORD now = GetTickCount();
			if (now - looked < 500) return;
			looked = now;
			const auto g = read_game();
			room_progress.observe(g);
			auto desired = browser::current(listed, configured_room_name, g, room_progress, join_in_progress::installed(), status_players());
			online_status::room(metadata.public_room, desired.name);
			if (pacer.may_send(now))
			{
				const auto request = browser::presence_request(desired, listed);
				if (!request.empty() && post(request))
				{
					pacer.sent(now);
					logger::write("online: room listing update: zone %s (%s), %s, %s, %d of %d players%s", desired.zone.empty() ? "-" : desired.zone.c_str(),
					              desired.zone_name.empty() ? "no display name" : online_security::sanitize_log_text(desired.zone_name).c_str(), desired.in_progress ? "in progress" : "not started",
					              desired.joinable ? "joinable" : "not joinable", status_players(), desired.max_players,
					              desired.host_display_name != listed.host_display_name ? (", host " + online_security::sanitize_log_text(desired.host_display_name)).c_str() : "");
					listed = desired;
				}
			}
			// The code in the game: on the online lobby menus, each time the host opens one.
			const auto menu = current_menu();
			const auto menu_object = current_menu_object();
			if (menu != last_menu || menu_object != last_menu_object)
			{
				last_menu = menu;
				last_menu_object = menu_object;
				lobby_shown = false;
			}
			// The menu fills its help bar from its data when it opens (after this PC first sees it) and on some focus
			// changes, so the line is put back whenever the slot no longer holds it.
			const int slot = browser::lobby_slot(menu);
			if (code_text[0] && slot >= 0 && menu_object && (!lobby_shown || lobby_slot_value(menu_object, slot) != lobby_handle))
			{
				const auto before = lobby_slot_value(menu_object, slot);
				if (lobby_code(menu_object, slot))
				{
					lobby_handle = lobby_slot_value(menu_object, slot);
					if (!lobby_shown) logger::write("online: room code shown on the %s menu (help bar %s %s, slot %08x -> %08x)", menu.c_str(), desc_items[slot], lobby_item(menu_object, slot) ? "found" : "MISSING", before, lobby_handle);
					else if (++lobby_reapplied <= 6) logger::write("online: room code put back on the %s menu (slot %08x -> %08x)", menu.c_str(), before, lobby_handle);
				}
				else logger::write_once("online-lobby-code", "online: the room code could not be put on the %s menu (not the retail menu code)", menu.c_str());
				lobby_shown = true;
			}
		}
	}

	bool identity(SteamNetworkingIdentity& out)
	{
		if (!wanted || !load_key()) return false;
		out.Clear();
		std::string id = core::gns_identity(own_id);
#ifndef NDEBUG
		if (const auto forced = ini::text(L"Dev", L"OnlineIdentity"))
		{
			// Tests only: GNS carries another identity than the one the broker authenticated (must be refused).
			id = core::gns_identity(*forced);
			logger::write("online: [Dev] OnlineIdentity - GNS uses %s (tests only)", id.c_str());
		}
#endif
		return out.ParseString(id.c_str());
	}

	void tick()
	{
		if (!wanted || !key_loaded) return;
		if (!broker_started)
		{
			broker_started = true;
			if (hosting)
			{
				// The room's first listing: what the game shows now (the Play Online menus; the screen name if entered).
				const auto g = read_game();
				room_progress.observe(g);
				listed = browser::current(metadata, configured_room_name, g, room_progress, join_in_progress::installed(), 1);
			}
			std::thread(broker_thread, broker_address, hosting, configured_code, broker_pin, listed).detach();
			logger::write("online: broker %s: %s", broker_address.c_str(), hosting ? "opening a room" : ("joining room " + online_security::redact_join_code(configured_code)).c_str());
		}
		process_inbox();
		browser_tick();
		if (cert_ready && ice_ready && listener == k_HSteamListenSocket_Invalid)
		{
			listener = sockets()->CreateListenSocketP2P(0, 0, nullptr);
			logger::write(listener == k_HSteamListenSocket_Invalid ? "online: GNS could not listen for room members" : "online: listening for room members (P2P, custom signaling)");
		}
		if (cert_ready && ice_ready && room_ready)
		{
			const auto now = GetTickCount();
			for (const auto& id : members.active_ids())
			{
				if (id == own_id || !opens_to(id)) continue;
				auto& t = ensure_tunnel(id);
				if (!t.address || t.connection != k_HSteamNetConnection_Invalid || t.attempts >= 3 || now < t.retry_at) continue;
				connect(t);
			}
		}
		for (auto& t : tunnels)
		{
			if (!t->connected) continue;
			while (!t->waiting.empty())
			{
				if (GetTickCount() - t->waiting.front().at > 10000 || send_datagram(*t, t->waiting.front()))
				{
					t->waiting_bytes -= t->waiting.front().data.size();
					t->waiting.pop_front();
					continue;
				}
				break;
			}
		}
		poll();
		report();
	}

	bool on_status(SteamNetConnectionStatusChangedCallback_t* info)
	{
		if (!wanted) return false;
		const auto c = info->m_hConn;
		const auto state = info->m_info.m_eState;
		char remote[SteamNetworkingIdentity::k_cchMaxString]{};
		info->m_info.m_identityRemote.ToString(remote, sizeof(remote));
		if (state == k_ESteamNetworkingConnectionState_Connecting && listener != k_HSteamListenSocket_Invalid && info->m_info.m_hListenSocket == listener)
		{
			std::string id = remote;
			if (id.rfind("str:", 0) == 0) id.erase(0, 4);
			if (!room_ready || !members.contains(id) || id == own_id)
			{
				logger::write("online: refused a connection from %s - not a member of room %s", remote, online_security::redact_join_code(room_code).c_str());
				sockets()->CloseConnection(c, 0, "not a room member", false);
				return true;
			}
			auto& t = ensure_tunnel(id);
			if (t.connection != k_HSteamNetConnection_Invalid && t.connection != c) drop(t, "replaced by the member's new connection");
			t.connection = c;
			t.outgoing = false;
			t.started = GetTickCount();
			if (!t.address || sockets()->AcceptConnection(c) != k_EResultOK)
			{
				logger::write("online: could not accept the connection from %s", remote);
				drop(t, "accept failed");
			}
			return true;
		}
		auto* t = by_connection(c);
		if (!t) return false;
		if (state == k_ESteamNetworkingConnectionState_Connected)
		{
			// Belt and braces on top of GNS's own certificate check: the peer is the member this connection is for.
			if (core::gns_identity(t->id) != remote)
			{
				logger::write("online: connection to %s refused - the peer proved identity %s", t->id.c_str(), remote);
				drop(*t, "identity mismatch");
				return true;
			}
			t->connected = true;
			t->connected_at = GetTickCount();
			SteamNetConnectionRealTimeStatus_t st{};
			sockets()->GetConnectionRealTimeStatus(c, &st, 0, nullptr);
			logger::write("online: connected to %s (%s) in %lu ms - route %s, ping %d ms; the game reaches it at %s", t->id.c_str(), t->outgoing ? "outgoing" : "incoming",
			              unsigned long(t->connected_at - t->started), route(c), st.m_nPing, core::dotted(t->address).c_str());
			// A joiner reaching the host: the host's game now answers the games list's search through this connection.
			if (!hosting && t->id == host_id && online_status::current() == online_status_core::state::connecting)
				online_status::set(online_status_core::state::joined, status_code(), status_players());
			return true;
		}
		if (state == k_ESteamNetworkingConnectionState_ClosedByPeer || state == k_ESteamNetworkingConnectionState_ProblemDetectedLocally)
		{
			logger::write("online: connection to %s ended (%s, reason %d: %s)", t->id.c_str(), state == k_ESteamNetworkingConnectionState_ClosedByPeer ? "closed by the peer" : "problem detected locally",
			              info->m_info.m_eEndReason, info->m_info.m_szEndDebug);
			const bool retry = t->outgoing && !t->connected;
			// A connection that never came up while cached TURN credentials were in use: the relay may have refused them
			// (GNS's ICE client fails a refused allocation silently). Forget them and ask the broker for new ones once;
			// connections started after the reply use those.
			if (!t->connected && turn_from_cache && !turn_refreshed)
			{
				turn_refreshed = true;
				turn_from_cache = false;
				if (!turn_cache.empty()) DeleteFileW(turn_cache.c_str());
				const bool asked = post(core::request("turn"));
				logger::write("online: a connection failed to come up with cached TURN credentials - %s", asked ? "asking the broker for new ones" : "cache cleared (the broker is not reachable now)");
			}
			drop(*t, "ended");
			if (retry) t->retry_at = GetTickCount() + 3000;
			return false; // the GNS transport closes a link it had on this connection too
		}
		return state != k_ESteamNetworkingConnectionState_None;
	}

	HSteamNetConnection connection_for(std::uint32_t address)
	{
		auto* t = by_address(address);
		return t && t->connected ? t->connection : k_HSteamNetConnection_Invalid;
	}
	bool owns(HSteamNetConnection c) { return wanted && by_connection(c) != nullptr; }
	void set_transport_bound(HSteamNetConnection c, bool bound)
	{
		auto* t = by_connection(c);
		if (!t) return;
		t->transport_bound = bound;
		// The game's session runs over the room connection (the joiner admitted to the host's game).
		if (bound) { online_status::set(online_status_core::state::in_game, status_code(), status_players()); return; }
		for (auto* m : t->game) m->Release();
		t->game.clear();
		t->game_bytes = 0;
	}
	int fetch(HSteamNetConnection c, SteamNetworkingMessage_t** out, int max)
	{
		poll();
		auto* t = by_connection(c);
		int n = 0;
		while (t && n < max && !t->game.empty())
		{
			out[n++] = t->game.front();
			t->game_bytes -= std::size_t(t->game.front()->m_cbSize);
			t->game.pop_front();
		}
		return n;
	}
	std::uint32_t own_virtual() { return key_loaded ? own_address : 0; }

	void install(HMODULE game)
	{
		const auto host = ini::text(L"Online", L"Host");
		const auto code = ini::text(L"Online", L"JoinCode");
		// [Online] Public / RoomName (contract v1.3): one-shot with Host, read before the keys are removed.
		const bool listed_publicly = ini::number(L"Online", L"Public", 0) != 0;
		const auto raw_room_name = ini::text(L"Online", L"RoomName");
		// One-shot keys (contract v1.1): read once, then removed from xml2-fix.ini before any room is created or
		// joined, so a later direct start of the exe (Steam, a desktop shortcut) never re-hosts or re-joins.
		if (host || code)
		{
			const bool removed = online_status::remove_launch_keys(online_status::game_ini());
			logger::write("online: [Online] %s%s%s%s read for this start and %s xml2-fix.ini", host ? "Host" : "", host && code ? " / " : "", code ? "JoinCode" : "",
			              host ? (listed_publicly ? " (Public=1)" : " (private)") : "", removed ? "removed from" : "could NOT be removed from");
		}
		hosting = host && _stricmp(host->c_str(), "broker") == 0;
		namespace st = online_status_core;
		if (code)
		{
			configured_code = core::join_code(*code);
			if (configured_code.empty())
			{
				logger::write("online: [Online] JoinCode is not 8 characters from the broker alphabet - ignored");
				// The launcher validates the format; a hand-edited ini still gets an answer in online-status.json.
				if (!hosting)
				{
					online_status::begin(st::role::join);
					online_status::error(st::error::code_not_found, "the join code is not 8 characters from ABCDEFGHJKMNPQRSTUVWXYZ23456789");
				}
			}
		}
		if (!hosting && configured_code.empty()) return;
		if (hosting && !configured_code.empty()) { logger::write("online: [Online] JoinCode set - this PC joins room %s instead of hosting one", online_security::redact_join_code(configured_code).c_str()); hosting = false; }
		online_status::begin(hosting ? st::role::host : st::role::join);
		if (game_version::test_mode_active())
		{
			// [TestMode] blocks online play (game_version refuses the game's sockets): no room, no broker sign-in.
			logger::write("online: [Online] %s refused - %s", hosting ? "Host=broker" : "JoinCode", game_version::test_mode_reason());
			online_status::error(st::error::test_mode_on, game_version::test_mode_reason());
			return;
		}
		if (const char* why = core::room_guard_refusal(hosting, receive_validation::installed(), save_format::campaign_load_checked()))
		{
			logger::write("online: [Online] %s refused - %s", hosting ? "Host=broker" : "JoinCode", why);
			online_status::error(st::error::internal, why);
			return;
		}
		const auto where = ini::text(L"Online", L"Broker");
		if (!where || where->empty())
		{
			logger::write("online: [Online] %s needs [Online] Broker=<host>[:port] - rooms off", hosting ? "Host=broker" : "JoinCode");
			online_status::error(st::error::broker_unreachable, "no broker is configured ([Online] Broker)");
			return;
		}
		broker_address = *where;
		if (const auto pin = ini::text(L"Online", L"BrokerPin")) broker_pin = *pin;
		metadata = {};
		metadata.public_room = listed_publicly;
		configured_room_name = raw_room_name ? browser::room_name(*raw_room_name) : std::string();
		if (raw_room_name && configured_room_name != browser::trim(*raw_room_name))
			logger::write("online: [Online] RoomName kept to 32 printable ASCII characters without ; and \" (\"%s\")", configured_room_name.c_str());
		// This build's token ([Online] GameVersion, contract v1.3): a host's room carries it, a joiner compares it
		// with the room's before connecting (version_mismatch), the launcher's browser greys out other tokens.
		// [Online] RoomVersionToken overrides it (tests).
		{
			const auto forced = ini::text(L"Online", L"RoomVersionToken");
			const bool usable = forced && !forced->empty() && forced->size() <= 32 &&
				std::none_of(forced->begin(), forced->end(), [](unsigned char c) { return c < 0x20 || c > 0x7e; });
			const auto version = ini::text(L"Online", L"GameVersion");
			own_version_token = usable ? *forced : st::version_token(version ? *version : std::string());
			metadata.version_token = own_version_token;
		}
		// Zone, mode, progress and the screen name come from the game's memory (discord_rules' retail reads).
		game_readable = !discord_rules::first_mismatch(reinterpret_cast<const std::uint8_t*>(game));
		if (hosting && !game_readable) logger::write("online: the game's zone/session reads are not the retail ones - the room's listing keeps its first state");
		if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base || !retail_pump())
		{
			logger::write("online: rooms off - the retail receive pump differs");
			online_status::error(st::error::internal, "rooms need the retail game executable");
			return;
		}
		const auto r = ini::text(L"Online", L"Route");
		relay_only = r && _stricmp(r->c_str(), "relay") == 0;
		real_sendto = reinterpret_cast<sendto_t>(iat_hook::hook(game, "WS2_32.dll", "sendto", 20, reinterpret_cast<void*>(&room_sendto)));
		real_recvfrom = reinterpret_cast<recvfrom_t>(iat_hook::hook(game, "WS2_32.dll", "recvfrom", 17, reinterpret_cast<void*>(&room_recvfrom)));
		if (!real_sendto || !real_recvfrom)
		{
			if (real_sendto) iat_hook::hook(game, "WS2_32.dll", "sendto", 20, reinterpret_cast<void*>(real_sendto));
			if (real_recvfrom) iat_hook::hook(game, "WS2_32.dll", "recvfrom", 17, reinterpret_cast<void*>(real_recvfrom));
			logger::write("online: rooms off - the game's sendto/recvfrom imports were not found");
			online_status::error(st::error::internal, "the game's network imports were not found");
			return;
		}
		if (!configured_code.empty())
		{
			// master_list_entry: two guarded 5-byte calls (call 0x6097b0; mov ecx,eax; call 0x608c20 at each site).
			bool safe = true;
			xml1_hook_transaction::memory memory;
			const bool guarded = [] { __try { return limits_rules::matches(reinterpret_cast<const std::uint8_t*>(0x605d4e), "e85d3a00008bc8e8c62e0000") &&
			                                         limits_rules::matches(reinterpret_cast<const std::uint8_t*>(0x60616a), "e8413600008bc8e8aa2a0000"); }
			                          __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }();
			if (!guarded || !xml1_hook_transaction::apply(memory, {{sb_entry_added, {0xe8, 0xc6, 0x2e, 0x00, 0x00}, call_to(sb_entry_added, reinterpret_cast<const void*>(&master_list_entry))},
			                                                       {sb_entry_updated, {0xe8, 0xaa, 0x2a, 0x00, 0x00}, call_to(sb_entry_updated, reinterpret_cast<const void*>(&master_list_entry))}}, &safe))
			{
				if (!safe) { game_version::refuse_startup(); logger::write("online: the master-list filter could not be rolled back; startup refused"); online_status::error(st::error::internal, "startup refused"); return; }
				logger::write("online: the games list's master-server path is not the retail one - Join Game also shows master server entries");
			}
		}
		if (!configured_code.empty())
		{
			bool safe = true;
			xml1_hook_transaction::memory memory;
			const bool guarded = [] { __try { return limits_rules::matches(reinterpret_cast<const std::uint8_t*>(0x5cf071), "6a0f681cbca300e87314f9ff83c40c"); } __except (EXCEPTION_EXECUTE_HANDLER) { return false; } }();
			if (!guarded || !xml1_hook_transaction::apply(memory, {{enter_ip_copy, {0xe8, 0x73, 0x14, 0xf9, 0xff}, call_to(enter_ip_copy, reinterpret_cast<const void*>(&enter_ip))}}, &safe))
			{
				if (!safe) { game_version::refuse_startup(); logger::write("online: the Enter IP fill could not be rolled back; startup refused"); online_status::error(st::error::internal, "startup refused"); return; }
				logger::write("online: Enter IP is not the retail one - type the host's virtual address (logged once the room resolves)");
			}
		}
		wanted = true;
		online_status::set(st::state::connecting, hosting ? std::string() : configured_code, 0);
		logger::write("online: rooms on - %s through broker %s (route %s); the game's datagrams to 10.77.x.y go through P2P connections",
		              hosting ? "hosting a room" : ("joining room " + online_security::redact_join_code(configured_code)).c_str(), broker_address.c_str(), relay_only ? "relay only" : "any");
	}
}
#else
namespace online_room
{
	void install(HMODULE)
	{
		const auto host = ini::text(L"Online", L"Host");
		const auto code = ini::text(L"Online", L"JoinCode");
		if (host || code)
		{
			online_status::remove_launch_keys(online_status::game_ini());
			logger::write("online: [Online] Host/JoinCode need a build with GameNetworkingSockets - rooms off");
			online_status::begin(code ? online_status_core::role::join : online_status_core::role::host);
			online_status::error(online_status_core::error::internal, "this XML2 Fix build has no online rooms");
		}
	}
}
#endif

namespace online_room
{
	bool requested() { return wanted; }
	void shutdown() { online_status::end(); }
}
