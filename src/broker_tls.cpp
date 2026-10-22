#include "broker_tls.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <vector>

namespace broker_tls
{
	namespace
	{
		std::string status_text(const char* what, SECURITY_STATUS status)
		{
			char text[160];
			std::snprintf(text, sizeof(text), "%s (0x%08lx)", what, static_cast<unsigned long>(status));
			return text;
		}
		bool send_all(SOCKET socket, const void* data, std::size_t size)
		{
			const auto* bytes = static_cast<const char*>(data);
			while (size)
			{
				const int n = send(socket, bytes, static_cast<int>(size), 0);
				if (n <= 0) return false;
				bytes += n;
				size -= static_cast<std::size_t>(n);
			}
			return true;
		}
#ifndef BROKER_TLS_TEST_READ_GRAIN
#define BROKER_TLS_TEST_READ_GRAIN 4096
#endif
		bool receive_more(SOCKET socket, std::string& input)
		{
			char chunk[BROKER_TLS_TEST_READ_GRAIN];
			const int n = recv(socket, chunk, sizeof(chunk), 0);
			if (n <= 0) return false;
			input.append(chunk, static_cast<std::size_t>(n));
			return true;
		}
		int hex(char c)
		{
			if (c >= '0' && c <= '9') return c - '0';
			if (c >= 'a' && c <= 'f') return c - 'a' + 10;
			if (c >= 'A' && c <= 'F') return c - 'A' + 10;
			return -1;
		}
	}

	bool parse_pin(const std::string& text, std::array<std::uint8_t, pin_size>& out)
	{
		if (text.size() != pin_size * 2) return false;
		std::array<std::uint8_t, pin_size> parsed{};
		for (std::size_t i = 0; i < parsed.size(); ++i)
		{
			const int high = hex(text[i * 2]), low = hex(text[i * 2 + 1]);
			if (high < 0 || low < 0) return false;
			parsed[i] = static_cast<std::uint8_t>((high << 4) | low);
		}
		out = parsed;
		return true;
	}

	bool certificate_spki_sha256(PCCERT_CONTEXT certificate, std::array<std::uint8_t, pin_size>& out)
	{
		if (!certificate || !certificate->pCertInfo) return false;
		DWORD size = 0;
		const DWORD encoding = X509_ASN_ENCODING | PKCS_7_ASN_ENCODING;
		const auto* spki = &certificate->pCertInfo->SubjectPublicKeyInfo;
		if (!CryptEncodeObjectEx(encoding, X509_PUBLIC_KEY_INFO, spki, 0, nullptr, nullptr, &size) || !size) return false;
		std::vector<std::uint8_t> der(size);
		if (!CryptEncodeObjectEx(encoding, X509_PUBLIC_KEY_INFO, spki, 0, nullptr, der.data(), &size)) return false;
		der.resize(size);
		DWORD hash_size = static_cast<DWORD>(out.size());
		return CryptHashCertificate(0, CALG_SHA_256, 0, der.data(), static_cast<DWORD>(der.size()), out.data(), &hash_size) != 0 && hash_size == out.size();
	}

	bool verify_certificate(PCCERT_CONTEXT certificate, const std::wstring& hostname, const std::string& pin, std::string& error)
	{
		if (!certificate) { error = "the broker sent no TLS certificate"; return false; }
		if (!pin.empty())
		{
			std::array<std::uint8_t, pin_size> expected{}, actual{};
			if (!parse_pin(pin, expected)) { error = "BrokerPin is not 64 hexadecimal characters"; return false; }
			if (!certificate_spki_sha256(certificate, actual)) { error = "the broker certificate's SPKI could not be hashed"; return false; }
			if (actual != expected) { error = "the broker certificate's SPKI pin does not match"; return false; }
			return true;
		}
		if (hostname.empty()) { error = "the broker hostname is empty"; return false; }
		PCCERT_CHAIN_CONTEXT chain = nullptr;
		if (!CertGetCertificateChain(nullptr, certificate, nullptr, nullptr, nullptr, 0, nullptr, &chain) || !chain)
		{
			error = "the broker certificate chain could not be built";
			return false;
		}
		SSL_EXTRA_CERT_CHAIN_POLICY_PARA ssl{};
		ssl.cbSize = sizeof(ssl);
		ssl.dwAuthType = AUTHTYPE_SERVER;
		ssl.pwszServerName = const_cast<wchar_t*>(hostname.c_str());
		CERT_CHAIN_POLICY_PARA policy{};
		policy.cbSize = sizeof(policy);
		policy.pvExtraPolicyPara = &ssl;
		CERT_CHAIN_POLICY_STATUS result{};
		result.cbSize = sizeof(result);
		const bool checked = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policy, &result) != 0;
		const DWORD problem = result.dwError;
		CertFreeCertificateChain(chain);
		if (!checked || problem != 0)
		{
			char text[120];
			std::snprintf(text, sizeof(text), "public-CA/hostname verification failed (0x%08lx)", static_cast<unsigned long>(problem));
			error = text;
			return false;
		}
		return true;
	}

	DWORD enabled_protocols() { return SP_PROT_TLS1_2_CLIENT | SP_PROT_TLS1_3_CLIENT; }

	client::~client() { reset(); }
	void client::reset()
	{
		if (context_valid_) DeleteSecurityContext(&context_);
		if (credentials_valid_) FreeCredentialsHandle(&credentials_);
		context_valid_ = credentials_valid_ = ready_ = false;
		context_ = CtxtHandle{};
		credentials_ = CredHandle{};
		sizes_ = SecPkgContext_StreamSizes{};
		encrypted_.clear();
	}

	bool client::handshake(SOCKET socket, const std::string& hostname, const std::string& pin, std::string& error)
	{
		reset();
		if (socket == INVALID_SOCKET || hostname.empty()) { error = "invalid broker socket or hostname"; return false; }
		std::array<std::uint8_t, pin_size> ignored{};
		if (!pin.empty() && !parse_pin(pin, ignored)) { error = "BrokerPin is not 64 hexadecimal characters"; return false; }
		const int wide_size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, hostname.data(), static_cast<int>(hostname.size()), nullptr, 0);
		if (wide_size <= 0) { error = "the broker hostname is not valid UTF-8"; return false; }
		std::wstring wide(static_cast<std::size_t>(wide_size), L'\0');
		MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, hostname.data(), static_cast<int>(hostname.size()), wide.data(), wide_size);
		u_long blocking = 0;
		ioctlsocket(socket, FIONBIO, &blocking);
		DWORD timeout = 5000;
		setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
		setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

		SCHANNEL_CRED credential{};
		credential.dwVersion = SCHANNEL_CRED_VERSION;
		credential.grbitEnabledProtocols = enabled_protocols();
		credential.dwFlags = SCH_CRED_NO_DEFAULT_CREDS | SCH_CRED_MANUAL_CRED_VALIDATION;
		auto status = AcquireCredentialsHandleA(nullptr, const_cast<SEC_CHAR*>(UNISP_NAME_A), SECPKG_CRED_OUTBOUND, nullptr, &credential, nullptr, nullptr, &credentials_, nullptr);
		if (FAILED(status)) { error = status_text("AcquireCredentialsHandle failed", status); reset(); return false; }
		credentials_valid_ = true;

		DWORD flags = ISC_REQ_SEQUENCE_DETECT | ISC_REQ_REPLAY_DETECT | ISC_REQ_CONFIDENTIALITY | ISC_REQ_ALLOCATE_MEMORY | ISC_REQ_STREAM | ISC_REQ_MANUAL_CRED_VALIDATION;
		std::string input;
		bool first = true;
		for (;;)
		{
			SecBuffer in[2] = {};
			in[0].BufferType = SECBUFFER_TOKEN;
			in[0].pvBuffer = input.empty() ? nullptr : input.data();
			in[0].cbBuffer = static_cast<unsigned long>(input.size());
			in[1].BufferType = SECBUFFER_EMPTY;
			SecBufferDesc input_desc{SECBUFFER_VERSION, 2, in};
			SecBuffer out[1] = {};
			out[0].BufferType = SECBUFFER_TOKEN;
			SecBufferDesc output_desc{SECBUFFER_VERSION, 1, out};
			DWORD attributes = 0;
			status = InitializeSecurityContextA(&credentials_, first ? nullptr : &context_, const_cast<char*>(hostname.c_str()), flags, 0, 0,
			                                    first ? nullptr : &input_desc, 0, &context_, &output_desc, &attributes, nullptr);
			const bool sent = !out[0].pvBuffer || !out[0].cbBuffer || send_all(socket, out[0].pvBuffer, out[0].cbBuffer);
			if (out[0].pvBuffer) FreeContextBuffer(out[0].pvBuffer);
			if (!sent) { error = "the broker TLS handshake could not be sent"; reset(); return false; }
			if (status == SEC_E_INCOMPLETE_MESSAGE)
			{
				first = false;
				if (!receive_more(socket, input)) { error = "the broker TLS handshake ended early"; reset(); return false; }
				continue;
			}
			bool extra = !first && in[1].BufferType == SECBUFFER_EXTRA && in[1].cbBuffer;
			if (!first)
			{
				if (extra)
				{
					const auto keep = static_cast<std::size_t>(in[1].cbBuffer);
					std::memmove(input.data(), input.data() + input.size() - keep, keep);
					input.resize(keep);
				}
				else input.clear();
			}
			if (status == SEC_E_OK)
			{
				encrypted_ = input;
				break;
			}
			if (status != SEC_I_CONTINUE_NEEDED)
			{
				error = status_text("InitializeSecurityContext failed", status);
				reset();
				return false;
			}
			first = false;
			if (!extra && !receive_more(socket, input)) { error = "the broker TLS handshake ended early"; reset(); return false; }
		}

		PCCERT_CONTEXT certificate = nullptr;
		status = QueryContextAttributesA(&context_, SECPKG_ATTR_REMOTE_CERT_CONTEXT, &certificate);
		if (FAILED(status) || !certificate)
		{
			error = status_text("the broker TLS certificate is unavailable", status);
			reset();
			return false;
		}
		const bool trusted = verify_certificate(certificate, wide, pin, error);
		CertFreeCertificateContext(certificate);
		if (!trusted) { reset(); return false; }
		status = QueryContextAttributesA(&context_, SECPKG_ATTR_STREAM_SIZES, &sizes_);
		if (FAILED(status)) { error = status_text("the broker TLS stream sizes are unavailable", status); reset(); return false; }
		u_long nonblocking = 1;
		ioctlsocket(socket, FIONBIO, &nonblocking);
		ready_ = true;
		return true;
	}

	bool client::encrypt(const std::string& plain, std::vector<std::uint8_t>& wire, std::string& error)
	{
		if (!ready_) { error = "TLS is not established"; return false; }
		for (std::size_t offset = 0; offset < plain.size();)
		{
			const auto chunk = std::min<std::size_t>(plain.size() - offset, sizes_.cbMaximumMessage);
			std::vector<std::uint8_t> message(sizes_.cbHeader + chunk + sizes_.cbTrailer);
			SecBuffer buffers[3] = {};
			buffers[0].BufferType = SECBUFFER_STREAM_HEADER;
			buffers[0].pvBuffer = message.data();
			buffers[0].cbBuffer = sizes_.cbHeader;
			buffers[1].BufferType = SECBUFFER_DATA;
			buffers[1].pvBuffer = message.data() + sizes_.cbHeader;
			buffers[1].cbBuffer = static_cast<unsigned long>(chunk);
			buffers[2].BufferType = SECBUFFER_STREAM_TRAILER;
			buffers[2].pvBuffer = message.data() + sizes_.cbHeader + chunk;
			buffers[2].cbBuffer = sizes_.cbTrailer;
			std::memcpy(buffers[1].pvBuffer, plain.data() + offset, chunk);
			SecBufferDesc desc{SECBUFFER_VERSION, 3, buffers};
			const auto status = EncryptMessage(&context_, 0, &desc, 0);
			if (FAILED(status)) { error = status_text("EncryptMessage failed", status); reset(); return false; }
			const auto used = buffers[0].cbBuffer + buffers[1].cbBuffer + buffers[2].cbBuffer;
			wire.insert(wire.end(), message.begin(), message.begin() + used);
			offset += chunk;
		}
		return true;
	}

	bool client::decrypt(const std::uint8_t* data, std::size_t size, std::string& plain, std::string& error)
	{
		if (!ready_) { error = "TLS is not established"; return false; }
		if ((size && !data) || size > 65536 || encrypted_.size() > 65536 - size)
		{
			error = "the broker TLS input buffer exceeded 64 KiB";
			reset();
			return false;
		}
		if (size) encrypted_.append(reinterpret_cast<const char*>(data), size);
		while (!encrypted_.empty())
		{
			SecBuffer buffers[4] = {};
			buffers[0].BufferType = SECBUFFER_DATA;
			buffers[0].pvBuffer = encrypted_.data();
			buffers[0].cbBuffer = static_cast<unsigned long>(encrypted_.size());
			for (int i = 1; i < 4; ++i) buffers[i].BufferType = SECBUFFER_EMPTY;
			SecBufferDesc desc{SECBUFFER_VERSION, 4, buffers};
			const auto status = DecryptMessage(&context_, &desc, 0, nullptr);
			if (status == SEC_E_INCOMPLETE_MESSAGE) return true;
			if (status == SEC_I_CONTEXT_EXPIRED) { error = "the broker closed the TLS session"; reset(); return false; }
			if (FAILED(status) || status == SEC_I_RENEGOTIATE)
			{
				error = status_text("DecryptMessage failed", status);
				reset();
				return false;
			}
			for (const auto& buffer : buffers)
				if (buffer.BufferType == SECBUFFER_DATA && buffer.pvBuffer && buffer.cbBuffer)
					plain.append(static_cast<const char*>(buffer.pvBuffer), buffer.cbBuffer);
			const auto extra = std::find_if(buffers, buffers + 4, [](const SecBuffer& b) { return b.BufferType == SECBUFFER_EXTRA && b.cbBuffer; });
			if (extra != buffers + 4)
			{
				const auto keep = static_cast<std::size_t>(extra->cbBuffer);
				std::memmove(encrypted_.data(), encrypted_.data() + encrypted_.size() - keep, keep);
				encrypted_.resize(keep);
			}
			else encrypted_.clear();
		}
		return true;
	}
}
