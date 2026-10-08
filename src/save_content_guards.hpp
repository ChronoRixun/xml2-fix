#pragma once
#include <Windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <utility>

namespace save_content_guards
{
	// Effective startup mod winners, not a second independently recomputed load order.
	bool verify_mod_files(const std::filesystem::path& build, const std::vector<std::pair<std::wstring,std::wstring>>& winners, std::string& error);
	bool verify_mods(const std::filesystem::path& build, std::string& error);
	bool verify_keys(const std::vector<std::uint8_t>& file, std::string& error);
	void install(HMODULE game);
	std::string runtime_failure();
}
