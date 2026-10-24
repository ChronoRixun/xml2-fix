#include "online_security.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace
{
	int failures = 0, passes = 0;
	void check(bool ok, const char* what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
		ok ? ++passes : ++failures;
	}
	std::string read(const std::filesystem::path& path)
	{
		std::ifstream in(path, std::ios::binary);
		return {std::istreambuf_iterator<char>(in), {}};
	}
}

int main()
{
	using namespace online_security;
	check((dpapi_current_user_flags & CRYPTPROTECT_UI_FORBIDDEN) != 0 && (dpapi_current_user_flags & CRYPTPROTECT_LOCAL_MACHINE) == 0,
	      "DPAPI: current-user scope with UI forbidden, never local-machine scope");
	check(key_load_action(false, false, 0) == key_action::generate && key_load_action(true, true, 32) == key_action::decrypt &&
	      key_load_action(true, false, 0) == key_action::refuse && key_load_action(true, true, 0) == key_action::refuse,
	      "key loading: only a missing key regenerates; existing empty or unreadable files refuse");
	std::vector<unsigned char> secret{1, 2, 3, 4, 5};
	secure_erase(secret.data(), secret.size());
	check(secret == std::vector<unsigned char>(5, 0), "secure erase: certificate and plaintext buffers are zeroed");
	check(sanitize_log_text("bad\r\nline\x01!\x7f") == "badline!", "log text: broker-controlled line breaks and control bytes are stripped");
	check(redact_join_code("K7QN2X9M") == "K7******" && redact_join_code("").empty(),
	      "join code: only the first two characters reach logs");

	const auto root = std::filesystem::current_path() / "native" / "online-security-output";
	std::filesystem::remove_all(root);
	std::filesystem::create_directories(root);
	const auto target = root / "identity.key";
	const std::string first = "first", second = "second-long";
	check(atomic_replace(target, first.data(), first.size()) && read(target) == first &&
	      atomic_replace(target, second.data(), second.size()) && read(target) == second,
	      "atomic replace: a complete new file replaces the old contents");
	bool temporary_left = false;
	for (const auto& entry : std::filesystem::directory_iterator(root))
		if (entry.path().filename().wstring().find(L".tmp") != std::wstring::npos) temporary_left = true;
	check(!temporary_left, "atomic replace: successful writes leave no temporary-file residue");
	const auto blocked = root / "blocked";
	std::filesystem::create_directory(blocked);
	check(!atomic_replace(blocked, first.data(), first.size()), "atomic replace: an incompatible destination fails instead of truncating it");
	temporary_left = false;
	for (const auto& entry : std::filesystem::directory_iterator(root))
		if (entry.path().filename().wstring().find(L".tmp") != std::wstring::npos) temporary_left = true;
	check(!temporary_left, "atomic replace: failed replacement also cleans its temporary file");
	std::filesystem::remove_all(root);
	std::printf("%d PASS / %d FAIL\n", passes, failures);
	return failures ? 1 : 0;
}
