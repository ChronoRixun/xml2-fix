#pragma once

#include <winsock2.h>
#ifndef SECURITY_WIN32
#define SECURITY_WIN32
#endif
#include <sspi.h>
#include <schannel.h>
#include <wincrypt.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace broker_tls
{
	constexpr std::size_t pin_size = 32;
	bool parse_pin(const std::string& text, std::array<std::uint8_t, pin_size>& out);
	bool certificate_spki_sha256(PCCERT_CONTEXT certificate, std::array<std::uint8_t, pin_size>& out);
	// Empty pin selects the system-CA and DNS hostname path; otherwise only the exact leaf SPKI pin authenticates.
	bool verify_certificate(PCCERT_CONTEXT certificate, const std::wstring& hostname, const std::string& pin, std::string& error);
	DWORD enabled_protocols();

	class client
	{
		CredHandle credentials_{};
		CtxtHandle context_{};
		SecPkgContext_StreamSizes sizes_{};
		std::string encrypted_;
		bool credentials_valid_ = false, context_valid_ = false, ready_ = false;
		void reset();
	public:
		client() = default;
		~client();
		client(const client&) = delete;
		client& operator=(const client&) = delete;

		// The socket is connected and owned by the caller. Trust is verified before this returns success.
		bool handshake(SOCKET socket, const std::string& hostname, const std::string& pin, std::string& error);
		bool encrypt(const std::string& plain, std::vector<std::uint8_t>& wire, std::string& error);
		// Raw bytes may split or coalesce TLS records. Decrypted bytes append to plain; incomplete input stays buffered.
		bool decrypt(const std::uint8_t* data, std::size_t size, std::string& plain, std::string& error);
	};
}
