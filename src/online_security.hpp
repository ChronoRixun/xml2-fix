#pragma once

#include <Windows.h>
#include <wincrypt.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace online_security
{
	constexpr DWORD dpapi_current_user_flags = CRYPTPROTECT_UI_FORBIDDEN;
	enum class key_action { generate, decrypt, refuse };
	constexpr key_action key_load_action(bool exists, bool readable, std::size_t size)
	{
		if (!exists) return key_action::generate;
		return readable && size ? key_action::decrypt : key_action::refuse;
	}

	inline void secure_erase(void* data, std::size_t size)
	{
		if (data && size) SecureZeroMemory(data, size);
	}

	inline std::string sanitize_log_text(std::string_view text)
	{
		std::string out;
		out.reserve(text.size());
		for (const unsigned char c : text)
			if (c >= 0x20 && c != 0x7f) out += char(c);
		return out;
	}

	inline std::string redact_join_code(std::string_view code)
	{
		std::string out(code.substr(0, 2));
		if (code.size() > 2) out.append(code.size() - 2, '*');
		return out;
	}

	inline bool atomic_replace(const std::filesystem::path& target, const void* data, std::size_t size)
	{
		if ((size && !data) || target.empty()) return false;
		static std::atomic<std::uint32_t> sequence{0};
		const auto ticket = sequence.fetch_add(1, std::memory_order_relaxed) + 1;
		auto temporary = target;
		temporary += L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(ticket) + L".tmp";
		HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (file == INVALID_HANDLE_VALUE) return false;
		bool ok = true;
		const auto* bytes = static_cast<const std::uint8_t*>(data);
		for (std::size_t at = 0; ok && at < size;)
		{
			const auto chunk = static_cast<DWORD>((size - at) > 0x40000000 ? 0x40000000 : (size - at));
			DWORD written = 0;
			ok = WriteFile(file, bytes + at, chunk, &written, nullptr) && written == chunk;
			at += written;
		}
		if (ok) ok = FlushFileBuffers(file) != 0;
		CloseHandle(file);
		if (ok) ok = MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
		if (!ok) DeleteFileW(temporary.c_str());
		return ok;
	}
}
