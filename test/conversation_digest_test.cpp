// Synthetic filesystem regressions: conversation identity without game content.
#include "internal.hpp"
#include <Windows.h>
#include <filesystem>
#include <fstream>
#include <cstdio>
#include <thread>
#include <chrono>

namespace fs = std::filesystem;
using namespace lcsave;
int failures = 0;
void check(bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL: %s\n", what); } }
void put(const fs::path& p, const char* text) { std::ofstream(p, std::ios::binary) << text; }
Tables fixture(const fs::path& root)
{
	auto data = std::make_shared<detail::TablesData>();
	data->build = std::make_unique<detail::Build>();
	data->build->data_root = root.wstring();
	Tables t; t.data = data; return t;
}
std::array<std::uint8_t, 32> digest(const Tables& t)
{
	std::array<std::uint8_t, 32> out{}; std::string error;
	check(conversation_digest(t, out, error), error.c_str()); return out;
}
int main(int argc, char** argv)
{
	if (argc == 2)
	{
		auto tables = fixture(fs::path(argv[1]));
		const auto start = GetTickCount64(); const auto first = digest(tables);
		const auto middle = GetTickCount64(); const auto second = digest(tables);
		check(first == second, "cold and cached identities agree");
		std::printf("conversation identity: cold %llu ms, unchanged %llu ms, %s\n",
			static_cast<unsigned long long>(middle-start), static_cast<unsigned long long>(GetTickCount64()-middle), failures ? "FAILED" : "PASSED");
		return failures ? 1 : 0;
	}
	const auto root = fs::temp_directory_path() / ("xml2-conversation-cache-" + std::to_string(GetCurrentProcessId()));
	fs::create_directories(root / "Conversations" / "nested");
	const auto a = root / "Conversations" / "InventedA.bin";
	const auto b = root / "Conversations" / "nested" / "InventedB.bin";
	put(a, "first"); put(b, "second");
	auto t = fixture(root);
	const auto initial = digest(t);
	check(initial == digest(fixture(root)), "cached digest matches a fresh uncached digest");
	// No data reopens on the unchanged path: an exclusive data handle permits
	// attribute queries, while the former GENERIC_READ open would be refused.
	const HANDLE held = CreateFileW(a.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	check(held != INVALID_HANDLE_VALUE, "exclusive data handle opened");
	if (held != INVALID_HANDLE_VALUE) { check(initial == digest(t), "unchanged cache works without a data read"); CloseHandle(held); }
	const auto stamp = fs::last_write_time(a);
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	put(a, "other"); fs::last_write_time(a, stamp);
	const auto edited = digest(t);
	check(edited != initial, "same length edit with restored write time invalidates by change time");
	check(edited == digest(fixture(root)), "edited digest matches independent fresh tables");
	put(root / "Conversations" / "extra.bin", "third");
	const auto added = digest(t); check(added != edited, "new file invalidates");
	fs::rename(b, root / "Conversations" / "nested" / "Renamed.bin");
	const auto renamed = digest(t); check(renamed != added, "rename invalidates path identity");
	fs::remove(root / "Conversations" / "extra.bin");
	check(digest(t) != renamed, "removal invalidates");
	// A failed refresh cannot return the old identity.
	put(a, "changed again");
	const HANDLE blocked = CreateFileW(a.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	std::array<std::uint8_t, 32> out{}; std::string error;
	check(blocked != INVALID_HANDLE_VALUE, "changed data locked");
	if (blocked != INVALID_HANDLE_VALUE) { check(!conversation_digest(t, out, error), "unreadable changed data refuses instead of stale success"); CloseHandle(blocked); }
	check(digest(t) == digest(fixture(root)), "failed refresh is retried after data becomes readable");
	fs::remove_all(root / "Conversations");
	check(digest(t) == sha256(nullptr, 0), "empty tree keeps the established digest");
	fs::create_directories(root / "Conversations"); put(a, "different build");
	check(digest(fixture(root)) != initial, "independent tables never inherit an earlier build identity");
	// A pinned engine build performs no inventory scan and refuses any later mutation.
	auto pinned = fixture(root);
	check(prepare_conversation_digest(pinned, error), "engine identity prepared");
	const auto pinned_digest = digest(pinned);
	const HANDLE pinned_lock = CreateFileW(a.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, 0, nullptr);
	check(pinned_lock != INVALID_HANDLE_VALUE, "pinned data locked");
	check(pinned_digest == digest(pinned), "pinned engine digest does not reopen data");
	if (pinned_lock != INVALID_HANDLE_VALUE) CloseHandle(pinned_lock);
	put(a, "mutated engine build");
	check(WaitForSingleObject(pinned.data->build->conversations->change_notification, 2000) == WAIT_OBJECT_0,
		"tree mutation signals the engine notification");
	check(!conversation_digest(pinned, out, error), "pinned identity refuses changed tree without refreshing");
	check(!conversation_digest(pinned, out, error), "pinned refusal remains until new build tables");
	check(prepare_conversation_digest(fixture(root), error), "fresh build can prepare changed identity");
	fs::remove_all(root);
	std::printf("conversation digest: %s (%d failures)\n", failures ? "FAILED" : "PASSED", failures);
	return failures ? 1 : 0;
}
