// Original synthetic containers; two real processes run the production LC writer.
#include "save_guard_rules.hpp"
#include "save_guard_fixture.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cctype>
#include <memory>

using namespace save_guard_rules;
namespace fs = std::filesystem;
int checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::cerr << __LINE__ << ": " #x "\n"; } } while (false)

struct handle
{
	HANDLE value = nullptr;
	explicit handle(HANDLE h = nullptr) : value(h) {}
	~handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
	handle(const handle&) = delete;
	handle& operator=(const handle&) = delete;
};

std::vector<std::uint8_t> sample(std::uint8_t kind, const std::string& name)
{
	auto sections = guard_fixture::campaign_sections(name);
	if (kind == kind_profile)
	{
		sections.clear();
		for (auto tag : profile_required)
			sections.push_back({std::string(tag),1,0,section_required,
			                   tag == "META" ? guard_fixture::meta(name,0,0) : guard_fixture::empty_section()});
	}
	return guard_fixture::container(kind, sections);
}
void put(const fs::path& p, const std::vector<std::uint8_t>& b)
{
	std::ofstream f(p, std::ios::binary);
	f.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
	if (!f) throw std::runtime_error("fixture write failed");
}
std::vector<std::uint8_t> get(const fs::path& p)
{
	std::string error;
	std::vector<std::uint8_t> b;
	save_format_rules::read_file(p.string(), b, error);
	return b;
}
std::string event_name(const std::string& prefix, const std::string& name)
{ return "Local\\lc-save-transaction-" + prefix + "-" + name; }
HANDLE make_event(const std::string& prefix, const std::string& name)
{
	const auto h = CreateEventA(nullptr, TRUE, FALSE, event_name(prefix, name).c_str());
	if (!h) throw std::runtime_error("CreateEvent failed");
	return h;
}
void signal(const std::string& prefix, const std::string& name)
{
	handle h(OpenEventA(EVENT_MODIFY_STATE, FALSE, event_name(prefix, name).c_str()));
	if (!h.value || !SetEvent(h.value)) throw std::runtime_error("signal failed");
}
void barrier(const std::string& prefix, const std::string& name)
{
	handle h(OpenEventA(SYNCHRONIZE, FALSE, event_name(prefix, name).c_str()));
	if (!h.value || WaitForSingleObject(h.value, 10000) != WAIT_OBJECT_0)
		throw std::runtime_error("barrier timed out: " + name);
}
struct writer_context { std::string prefix, role; };
void writer_barrier(transaction_step step, void* opaque)
{
	const auto& c = *static_cast<writer_context*>(opaque);
	if (step == transaction_step::lease_wait && c.role == "B") signal(c.prefix, "B-wait");
	if (step == transaction_step::scratch_verified)
	{
		signal(c.prefix, c.role + "-scratch");
		barrier(c.prefix, c.role + "-continue");
	}
}
std::string quote(const std::string& s) { return "\"" + s + "\""; }
struct child
{
	PROCESS_INFORMATION process{};
	child(const std::string& exe, const fs::path& root, const std::string& args)
	{
		auto command = quote(exe) + " " + args;
		STARTUPINFOA startup{}; startup.cb = sizeof(startup);
		if (!CreateProcessA(exe.c_str(), command.data(), nullptr, nullptr, FALSE,
		                    CREATE_NO_WINDOW | IDLE_PRIORITY_CLASS, nullptr, root.string().c_str(), &startup, &process))
			throw std::runtime_error("CreateProcess failed");
	}
	~child()
	{
		if (WaitForSingleObject(process.hProcess, 0) != WAIT_OBJECT_0)
		{ TerminateProcess(process.hProcess, 99); WaitForSingleObject(process.hProcess, 5000); }
		CloseHandle(process.hProcess); CloseHandle(process.hThread);
	}
	DWORD finish()
	{
		if (WaitForSingleObject(process.hProcess, 10000) != WAIT_OBJECT_0)
			throw std::runtime_error("child did not finish");
		DWORD code = 99; GetExitCodeProcess(process.hProcess, &code); return code;
	}
};

void two_writers(const std::string& exe, const fs::path& root, std::uint8_t kind, bool initially_missing)
{
	const auto failures_before = failures;
	fs::create_directories(root / "alias");
	const std::string filename = kind == kind_campaign ? "campaign.lcsave" : "profile.lcsave";
	const auto path = root / filename;
	const auto original = sample(kind, "original"), a = sample(kind, "writer A"), b = sample(kind, "writer B");
	if (!initially_missing) put(path, original);
	const auto prefix = std::to_string(GetCurrentProcessId()) + "-" + root.filename().string();
	handle a_scratch(make_event(prefix, "A-scratch")), b_scratch(make_event(prefix, "B-scratch"));
	handle b_wait(make_event(prefix, "B-wait"));
	handle a_continue(make_event(prefix, "A-continue")), b_continue(make_event(prefix, "B-continue"));
	const auto args = "--writer " + quote(root.string()) + " " + prefix + " ";
	child first(exe, root, args + "A " + std::to_string(kind));
	CHECK(WaitForSingleObject(a_scratch.value, 10000) == WAIT_OBJECT_0);
	child second(exe, root, args + "B " + std::to_string(kind));
	// On the old writer B reaches its scratch barrier and replaces A's shared scratch. With the lease it
	// must report contention before reading the original or touching any scratch. No sleeps choose the schedule.
	HANDLE next[] = {b_wait.value, b_scratch.value};
	const auto order = WaitForMultipleObjects(2, next, FALSE, 10000);
	CHECK(order == WAIT_OBJECT_0);
	SetEvent(a_continue.value);
	CHECK(first.finish() == 0);
	CHECK(get(path) == a);
	CHECK(initially_missing ? !fs::exists(path.string() + ".bak") : get(path.string() + ".bak") == original);
	CHECK(WaitForSingleObject(b_scratch.value, 10000) == WAIT_OBJECT_0);
	// B has now reread A while holding the lease; its own scratch cannot modify A's published bytes.
	CHECK(get(path) == a);
	SetEvent(b_continue.value);
	CHECK(second.finish() == 0);
	CHECK(get(path) == b);
	CHECK(get(path.string() + ".bak") == a);
	std::cout << "two writers " << filename << (initially_missing ? " absent" : " existing")
	          << ": " << (failures == failures_before ? "PASS" : "FAIL") << "\n";
}

#ifndef SAVE_TRANSACTION_RACE_ONLY
struct mutation { std::string path; std::vector<std::uint8_t> bytes; transaction_step at; };
void mutate(transaction_step step, void* opaque)
{
	const auto& m = *static_cast<mutation*>(opaque);
	if (step == m.at) put(m.path, m.bytes);
}
void hold_lease(transaction_step step, void* opaque)
{
	if (step != transaction_step::lease_acquired) return;
	const auto& prefix = *static_cast<std::string*>(opaque);
	signal(prefix, "held"); barrier(prefix, "release");
}
void safeguards(const std::string& exe, const fs::path& root)
{
	fs::create_directories(root);
	const auto path = (root / "campaign.lcsave").string();
	const auto original = sample(kind_campaign, "original"), a = sample(kind_campaign, "A");
	std::string error;
	put(path, original);
	{
		destination_lease lease;
		CHECK(lease.acquire(path, error));
		const auto start = GetTickCount64();
		CHECK(!write_lc_atomically(path, a, error));
		const auto elapsed = GetTickCount64() - start;
		CHECK(elapsed >= save_lease_wait_ms && elapsed < save_lease_wait_ms + 2000);
		CHECK(error.find("another writer") != std::string::npos && error.find("timed out") != std::string::npos);
		CHECK(get(path) == original && !fs::exists(path + ".bak"));
	}
	CHECK(write_lc_atomically(path, a, error)); // persistent OPEN_ALWAYS lease can be reused
	{
		destination_lease no_lease;
		CHECK(!write_lc_under_lease(no_lease, original, error) && get(path) == a);
	}
	// An independent destination is not blocked by another destination's lease.
	{
		destination_lease lease;
		CHECK(lease.acquire(path, error));
		CHECK(write_lc_atomically((root / "other.lcsave").string(), original, error));
	}
	// Legacy/foreign scratch is left alone, on successes and injected failures.
	put(path + ".tmp", original); put(path + ".bak.tmp", original);
	put(path + ".tmp.foreign", original);
	CHECK(write_lc_atomically(path, a, error));
	CHECK(!write_lc_atomically(path, a, error, failure_point::flush));
	CHECK(get(path + ".tmp") == original && get(path + ".bak.tmp") == original && get(path + ".tmp.foreign") == original);
	unsigned scratch = 0;
	for (const auto& entry : fs::directory_iterator(root))
		if (entry.path().filename().string().find(".tmp.") != std::string::npos) ++scratch;
	CHECK(scratch == 1); // only the deliberately foreign scratch
	// The final destination recheck still catches nonparticipating writers.
	mutation m{path, original, transaction_step::before_replace};
	CHECK(!write_lc_atomically(path, a, error, failure_point::none, mutate, &m));
	CHECK(error.find("destination changed") != std::string::npos && get(path) == original);
	// A successful rename followed by wrong/missing published bytes must not report save success.
	m.at = transaction_step::published;
	CHECK(!write_lc_atomically(path, a, error, failure_point::none, mutate, &m));
	CHECK(error.find("published save did not verify") != std::string::npos && get(path) == original);
	// OS handle ownership is crash-released. A stale lease file itself is never a permanent refusal.
	const auto prefix = std::to_string(GetCurrentProcessId()) + "-crash";
	handle held(make_event(prefix, "held")), release(make_event(prefix, "release"));
	{
		child crashed(exe, root, "--hold " + quote(path) + " " + prefix);
		CHECK(WaitForSingleObject(held.value, 10000) == WAIT_OBJECT_0);
		CHECK(TerminateProcess(crashed.process.hProcess, 77));
		CHECK(crashed.finish() == 77);
	}
	CHECK(write_lc_atomically(path, a, error));
	CHECK(get(path) == a && get(path + ".bak") == original);
}

void import_safeguards(const std::string& exe, const fs::path& root)
{
	fs::create_directories(root);
	const auto path = (root / "campaign.lcsave").string();
	const auto a = sample(kind_campaign, "A"), imported = sample(kind_campaign, "import");
	std::string error;
	const auto prefix = std::to_string(GetCurrentProcessId()) + "-import";
	handle waiting(make_event(prefix, "B-wait"));
	std::unique_ptr<child> importer;
	{
		destination_lease lease;
		CHECK(lease.acquire(path, error));
		importer = std::make_unique<child>(exe, root, "--import " + quote(path) + " " + prefix);
		CHECK(WaitForSingleObject(waiting.value, 10000) == WAIT_OBJECT_0);
		CHECK(write_lc_under_lease(lease, a, error));
		// Release only after publication/readback; importer must reread the newly occupied destination.
	}
	CHECK(importer->finish() == 0);
	CHECK(!import_lc_if_absent(path, imported, error));
	CHECK(error.find("no longer empty") != std::string::npos && get(path) == a);
	const auto backup_only = (root / "backup-only.lcsave").string();
	put(backup_only + ".bak", a);
	CHECK(!import_lc_if_absent(backup_only, imported, error));
	CHECK(error.find("no longer empty") != std::string::npos && !fs::exists(backup_only) && get(backup_only + ".bak") == a);
	const auto free_path = (root / "free.lcsave").string();
	CHECK(import_lc_if_absent(free_path, imported, error));
	CHECK(get(free_path) == imported && !fs::exists(free_path + ".bak"));
}
#endif

int main(int argc, char** argv)
{
	SetPriorityClass(GetCurrentProcess(), IDLE_PRIORITY_CLASS);
	try
	{
		if (argc == 6 && std::string(argv[1]) == "--writer")
		{
			const fs::path root(argv[2]);
			writer_context context{argv[3], argv[4]};
			const auto kind = static_cast<std::uint8_t>(std::stoi(argv[5]));
			const std::string name = kind == kind_campaign ? "campaign.lcsave" : "profile.lcsave";
			auto alias = name;
			for (auto& c : alias) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			const auto path = context.role == "A" ? (root / name).string() : ".\\alias\\..\\" + alias;
			const auto bytes = sample(kind, "writer " + context.role);
			std::string error;
			const auto ok = write_lc_atomically(path, bytes, error, failure_point::none, writer_barrier, &context);
			const bool own = ok && get(path) == bytes;
			std::ofstream result(root / (context.role + "-result.txt"));
			result << "success=" << ok << " own_bytes=" << own << " error=" << error << "\n";
			return own ? 0 : 10;
		}
#ifndef SAVE_TRANSACTION_RACE_ONLY
		if (argc == 4 && std::string(argv[1]) == "--import")
		{
			writer_context context{argv[3], "B"}; std::string error;
			const auto ok = import_lc_if_absent(argv[2], sample(kind_campaign, "import"), error, writer_barrier, &context);
			return !ok && error.find("no longer empty") != std::string::npos ? 0 : 12;
		}
		if (argc == 4 && std::string(argv[1]) == "--hold")
		{
			std::string error, prefix(argv[3]); destination_lease lease;
			return lease.acquire(argv[2], error, 3000, hold_lease, &prefix) ? 0 : 11;
		}
#endif
		char image[32768]{}; GetModuleFileNameA(nullptr, image, sizeof(image));
		const auto root = fs::temp_directory_path() / ("lc-save-transaction-" + std::to_string(GetCurrentProcessId()));
		std::cout << "fixtures: " << root.string() << "\n";
		for (auto kind : {kind_campaign, kind_profile})
			for (bool absent : {false, true})
				two_writers(image, root / (std::to_string(kind) + (absent ? "-absent" : "-existing")), kind, absent);
#ifndef SAVE_TRANSACTION_RACE_ONLY
		safeguards(image, root / "guards");
		import_safeguards(image, root / "import");
#endif
		std::cout << "Transactions: " << checks << " checks, " << failures << " failures\n";
		return failures ? 1 : 0;
	}
	catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 2; }
}
