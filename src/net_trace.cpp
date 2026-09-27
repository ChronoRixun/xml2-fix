#include "net_trace.hpp"
#include "iat_hook.hpp"
#include "log.hpp"

#include <WinSock2.h>

#include <string>

namespace net_trace
{
	namespace
	{
		using connect_t = int(WSAAPI*)(SOCKET, const sockaddr*, int);
		using send_t = int(WSAAPI*)(SOCKET, const char*, int, int);
		using recv_t = int(WSAAPI*)(SOCKET, char*, int, int);
		using sendto_t = int(WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
		using recvfrom_t = int(WSAAPI*)(SOCKET, char*, int, int, sockaddr*, int*);

		connect_t real_connect = nullptr;
		send_t real_send = nullptr;
		recv_t real_recv = nullptr;
		sendto_t real_sendto = nullptr;
		recvfrom_t real_recvfrom = nullptr;

		std::string address(const sockaddr* where)
		{
			if (!where || where->sa_family != AF_INET)
			{
				return "?";
			}
			const auto* in = reinterpret_cast<const sockaddr_in*>(where);
			const auto* b = reinterpret_cast<const unsigned char*>(&in->sin_addr);
			return std::to_string(b[0]) + "." + std::to_string(b[1]) + "." + std::to_string(b[2]) + "." + std::to_string(b[3]) + ":" +
			       std::to_string(ntohs(in->sin_port));
		}

		// Printable characters as is, the rest as '.', capped.
		std::string readable(const char* data, const int length)
		{
			std::string text;
			for (int i = 0; i < length && i < 160; ++i)
			{
				const auto c = static_cast<unsigned char>(data[i]);
				text += (c >= 32 && c < 127) ? static_cast<char>(c) : '.';
			}
			return text;
		}

		std::string peer(const SOCKET socket)
		{
			sockaddr_in where{};
			int size = sizeof(where);
			return getpeername(socket, reinterpret_cast<sockaddr*>(&where), &size) == 0 ? address(reinterpret_cast<sockaddr*>(&where)) : "?";
		}

		int WSAAPI traced_connect(SOCKET socket, const sockaddr* where, int size)
		{
			const int result = real_connect(socket, where, size);
			logger::write("net: connect %s -> %d (error %d)", address(where).c_str(), result, result == SOCKET_ERROR ? WSAGetLastError() : 0);
			return result;
		}

		int WSAAPI traced_send(SOCKET socket, const char* data, int length, int flags)
		{
			const int result = real_send(socket, data, length, flags);
			logger::write("net: send %s %d bytes: %s", peer(socket).c_str(), length, readable(data, length).c_str());
			return result;
		}

		int WSAAPI traced_recv(SOCKET socket, char* data, int length, int flags)
		{
			const int result = real_recv(socket, data, length, flags);
			if (result > 0)
			{
				logger::write("net: recv %s %d bytes: %s", peer(socket).c_str(), result, readable(data, result).c_str());
			}
			else if (result == 0)
			{
				logger::write("net: recv %s closed", peer(socket).c_str());
			}
			return result;
		}

		int WSAAPI traced_sendto(SOCKET socket, const char* data, int length, int flags, const sockaddr* where, int size)
		{
			const int result = real_sendto(socket, data, length, flags, where, size);
			logger::write("net: sendto %s %d bytes: %s", address(where).c_str(), length, readable(data, length).c_str());
			return result;
		}

		int WSAAPI traced_recvfrom(SOCKET socket, char* data, int length, int flags, sockaddr* from, int* size)
		{
			const int result = real_recvfrom(socket, data, length, flags, from, size);
			if (result > 0)
			{
				logger::write("net: recvfrom %s %d bytes: %s", address(from).c_str(), result, readable(data, result).c_str());
			}
			return result;
		}

		template <typename T>
		void hook(const HMODULE module, const char* name, const WORD ordinal, T replacement, T& original)
		{
			original = reinterpret_cast<T>(iat_hook::hook(module, "WS2_32.dll", name, ordinal, reinterpret_cast<void*>(replacement)));
		}
	}

	void install(const HMODULE module)
	{
		// WS2_32 ordinals, as XMen2.exe imports them.
		hook(module, "connect", 4, &traced_connect, real_connect);
		hook(module, "send", 19, &traced_send, real_send);
		hook(module, "recv", 16, &traced_recv, real_recv);
		hook(module, "sendto", 20, &traced_sendto, real_sendto);
		hook(module, "recvfrom", 17, &traced_recvfrom, real_recvfrom);
		logger::write("net: tracing the game's network traffic (xml2-fix.ini [Debug] LogNetwork)");
	}
}
