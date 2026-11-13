// The timed-event pool (src/event_pool_rules.hpp, src/event_pool.cpp) against the retail manager's own code.
//   event_pool_test                      rules only (layout, key parsing, the edit plan, the watchdog's verdicts)
//   event_pool_test <retail exe> defect  the unmodified manager: the 1126th pending event cuts the heap and reuses a live record
//   event_pool_test <retail exe> raised  the production install: 8192 pending events fire once each, in time order; the 8193rd
//                                        is dropped; the watchdog reports a broken and an empty queue; a foreign byte refuses
//   event_pool_test <retail exe> rebuild the late-join queue rebuild's steps (retail pool init / alloc / store / push) on the raised pool
//   event_pool_test <retail exe> retail-holds   the raised expectations on the UNMODIFIED manager: fails (what the fix is for)
// The retail image is supplied locally and only mapped into this process; nothing of it is stored in the repository.
#include "../src/event_pool.cpp"
#include "../src/entity_capacity_layout.hpp"
#include "../src/gameplay_capabilities.hpp"

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

extern "C" unsigned char union_space[];
using namespace event_pool_rules;
using bytes = std::vector<std::uint8_t>;
static unsigned checks = 0, failures = 0;
#define CHECK(x) do { ++checks; if (!(x)) { ++failures; std::printf("FAIL line %d: %s\n", __LINE__, #x); } } while (0)
static std::string mode = "rules", key_text;
static bool key_present = false, entities_raised = true, startup_refused = false;
static std::vector<std::string> lines;
namespace ini { std::optional<std::string> text(const wchar_t*, const wchar_t*) { return key_present ? std::optional<std::string>(key_text) : std::nullopt; } }
namespace logger { void write(const char* f, ...) { char b[1024]; va_list a; va_start(a, f); std::vsnprintf(b, sizeof b, f, a); va_end(a); lines.emplace_back(b); std::puts(b); } }
namespace game_version { void refuse_startup() { startup_refused = true; } }
namespace entity_capacity { const entity_capacity_layout::descriptor& installed() { static entity_capacity_layout::descriptor d; d.raised = entities_raised; return d; } }

static void* ptr(std::uint32_t p) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(p)); }
static std::uint32_t& word(std::uint32_t p) { return *static_cast<std::uint32_t*>(ptr(p)); }
static bytes raw;
static bool map_image()
{
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
	const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(raw.data() + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.ImageBase != 0x400000) return false;
	DWORD old = 0; if (!VirtualProtect(union_space, 0x700000, PAGE_EXECUTE_READWRITE, &old)) return false;
	std::memset(union_space, 0, 0x700000);
	auto* s = IMAGE_FIRST_SECTION(nt);
	for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) std::memcpy(ptr(0x400000 + s[i].VirtualAddress), raw.data() + s[i].PointerToRawData, s[i].SizeOfRawData);
	return FlushInstructionCache(GetCurrentProcess(), union_space, 0x700000) != FALSE;
}

// ---- the fixture's two stand-ins inside the mapped image: the record fire and the game's CRT atexit ----
static std::vector<std::uint32_t> fired; // the owner field of each fired record, in firing order
static void __fastcall record_fired(void* record, void*) { fired.push_back(*static_cast<const std::uint32_t*>(record)); }
static void stub_services()
{
	auto* fire = static_cast<unsigned char*>(ptr(0x4199f0)); // record fire (thiscall, no stack arguments): mov eax, fn; jmp eax
	fire[0] = 0xb8; const auto target = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(&record_fired)); std::memcpy(fire + 1, &target, 4); fire[5] = 0xff; fire[6] = 0xe0;
	auto* at_exit = static_cast<unsigned char*>(ptr(0x67211e)); // cdecl atexit: xor eax, eax; ret
	at_exit[0] = 0x31; at_exit[1] = 0xc0; at_exit[2] = 0xc3;
	FlushInstructionCache(GetCurrentProcess(), nullptr, 0);
}
struct record { std::uint32_t owner, event; std::uint8_t payload[16]; };
static void* manager() { return reinterpret_cast<void*(__cdecl*)()>(ptr(manager_getter))(); }
static void add(void* m, std::uint32_t owner, float time)
{
	record r{owner, 0x6d7c5c, {}};
	const auto slot = word(word(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(m))) + 8);
	reinterpret_cast<void(__thiscall*)(void*, const record*, float)>(ptr(slot))(m, &r, time);
}
static void pump(void* m, float now)
{
	const auto slot = word(word(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(m))) + 0xc);
	reinterpret_cast<void(__thiscall*)(void*, float)>(ptr(slot))(m, now);
}
static counts read_counts(void* m, const layout& l)
{
	const auto* b = static_cast<const unsigned char*>(m) + body_at;
	const auto u = [&](std::uint32_t o) { std::uint32_t v; std::memcpy(&v, b + o, 4); return v; };
	const auto pop = [&](std::uint32_t o) { std::uint32_t n = 0; for (std::uint32_t i = 0; i < l.words; ++i) n += std::uint32_t(std::popcount(u(o + i * 4))); return n; };
	return {u(l.record_count), u(l.heap_count), u(l.ring_count), pop(l.used_bitmap), pop(l.alloc_bitmap)};
}
// A deterministic spread of times with many ties (every actor update of a frame shares one time).
static float time_of(std::uint32_t i) { return 10.f + float((i * 37u) % 97u) * 0.25f; }

static void agreement_rules();
static void rules()
{
	CHECK(layout_for(0x465) == retail_layout && retail_layout.body_bytes == 0x9f70 && retail_layout.words == 0x24);
	const auto r = layout_for(raised_records);
	CHECK(r.records == 8192 && r.words == 256 && r.used_bitmap == 8192 * 24 && r.ring == r.used_bitmap + 1024 && r.heap_count - r.heap == 8192 * 8 + 4);
	CHECK(r.ring_read - r.ring == 8192 * 4 + 8 && r.ring_count - r.ring == 8192 * 4 + 12 && r.body_bytes == r.heap_count + 4);
	std::uint32_t v = 0;
	CHECK(parse_capacity("", 8192, v) && v == 8192); CHECK(parse_capacity("", 1125, v) && v == 1125);
	CHECK(parse_capacity("0", 8192, v) && v == 1125); CHECK(parse_capacity("1125", 8192, v) && v == 1125);
	CHECK(parse_capacity("4500", 1125, v) && v == 4512); CHECK(parse_capacity("65536", 1125, v) && v == 65536);
	CHECK(!parse_capacity("1124", 8192, v)); CHECK(!parse_capacity("65537", 8192, v)); CHECK(!parse_capacity("8k", 8192, v)); CHECK(!parse_capacity("-1", 8192, v));
	const auto same = plan(retail_layout, retail_object);
	CHECK(same.size() == event_pool_sites::sites.size() && same.size() == 75);
	for (const auto& e : same) CHECK(e.before == e.after); // the retail layout at the retail object is the identity
	const auto p = plan(r, 0x12340000);
	unsigned changed = 0; for (const auto& e : p) { CHECK(e.before.size() == e.after.size()); changed += e.before != e.after; }
	CHECK(changed == 75);
	CHECK(judge({5, 5, 1120, 5, 5}, retail_layout, true) == verdict::ok);
	CHECK(judge({561, 0, 564, 284, 284}, retail_layout, true) == verdict::counts_disagree); // the frozen game's reading (2026-11-13)
	CHECK(judge({5, 5, 1120, 4, 5}, retail_layout, true) == verdict::bitmaps_disagree && judge({5, 5, 1120, 4, 5}, retail_layout, false) == verdict::ok);
	CHECK(judge({1126, 564, 0xffffffffu, 1125, 1125}, retail_layout, true) == verdict::over_capacity);
	CHECK(add_fits(1124, 1124, retail_layout) && !add_fits(1125, 1125, retail_layout) && !add_fits(8192, 8192, r) && add_fits(8191, 8191, r));
	agreement_rules();
}

// Gameplay agreement rule 65 (TimedEvents): a peer on the retail 1125 pool (1.11.0-beta.8 and older, which register no
// rule 65) and a peer on the raised pool refuse each other in both directions, and so do two different raised capacities.
static gameplay_capabilities::record lc_record(const agreement_entry* timed)
{
	gameplay_capabilities::record r;
	r.layout(3, {32, 768, 1024, 10}, true); // Legends Classic's entity capacity (the pool's default follows it)
	r.add(gameplay_capabilities::forced_party_revival_id, 1, {1, 0, 0, 0}, true);
	if (timed && timed->registers) r.add(gameplay_capabilities::timed_events_id, agreement_revision, timed->values, timed->verified);
	r.finalize();
	return r;
}
static std::string refusal_names(const gameplay_capabilities::record& mine, const gameplay_capabilities::record& peer)
{
	gameplay_capabilities::difference d{};
	if (mine.compare(peer.bytes(), &d) == gameplay_capabilities::result::match) return "MATCH";
	char text[256]{}; mine.describe(d.buckets, d.local_only, true, text, sizeof text);
	return text;
}
static void agreement_rules()
{
	CHECK(gameplay_capabilities::timed_events_id == 65 && agreement_revision == 1);
	CHECK(std::string(gameplay_capabilities::rule_name(65)) == "TimedEvents");
	for (std::uint16_t id = 1; id <= 64; ++id) CHECK(std::string(gameplay_capabilities::rule_name(id)) != "a newer rule"); // every ID below 65 is assigned
	CHECK(gameplay_capabilities::bucket_of(65) == gameplay_capabilities::bucket_of(gameplay_capabilities::entity_layout_id));
	// The four ways an install settles.
	CHECK(agreement_for(false, retail_records, retail_records, false) == (agreement_entry{false, {0, 0, 0, 0}, true}));  // plain XML2
	CHECK(agreement_for(true, raised_records, raised_records, true) == (agreement_entry{true, {8192, 0, 0, 0}, true})); // LC default
	CHECK(agreement_for(true, retail_records, retail_records, true) == (agreement_entry{false, {0, 0, 0, 0}, true}));   // TimedEvents=0 / 1125
	CHECK(agreement_for(true, 4512, 4512, true) == (agreement_entry{true, {4512, 0, 0, 0}, true}));                      // TimedEvents=4500
	const auto failed = agreement_for(true, raised_records, retail_records, false);                                       // raise refused
	CHECK(failed.registers && !failed.verified && failed.values[0] == retail_records);
	// Records: beta.8 (no rule 65, the 1125 pool) against beta.9 (rule 65 = 8192).
	const auto raised = agreement_for(true, raised_records, raised_records, true), other = agreement_for(true, 4512, 4512, true);
	const auto beta8 = lc_record(nullptr), beta9 = lc_record(&raised), beta9b = lc_record(&raised), custom = lc_record(&other);
	const auto kept = agreement_for(true, retail_records, retail_records, true);
	const auto kept_retail = lc_record(&kept);
	CHECK(beta9.ready() && beta8.ready() && beta9.compare(beta9b.bytes()) == gameplay_capabilities::result::match);
	CHECK(beta9.compare(beta8.bytes()) != gameplay_capabilities::result::match && beta8.compare(beta9.bytes()) != gameplay_capabilities::result::match);
	CHECK(beta9.compare(custom.bytes()) != gameplay_capabilities::result::match && custom.compare(beta9.bytes()) != gameplay_capabilities::result::match);
	CHECK(kept_retail.compare(beta8.bytes()) == gameplay_capabilities::result::match); // a pool kept retail on purpose plays with beta.8
	const auto named = refusal_names(beta9, beta8), named_back = refusal_names(beta8, beta9), named_custom = refusal_names(custom, beta9);
	std::printf("refusal beta.9 -> beta.8: %s\nrefusal beta.8 -> beta.9: %s\nrefusal 4512 -> 8192: %s\n", named.c_str(), named_back.c_str(), named_custom.c_str());
	CHECK(named.find("65 TimedEvents") != std::string::npos && named_custom.find("65 TimedEvents") != std::string::npos);
	CHECK(named_back.find("1 entity layout") != std::string::npos); // an older PC names the bucket's rule it knows
	gameplay_capabilities::record unverified;
	unverified.layout(3, {32, 768, 1024, 10}, true);
	unverified.add(gameplay_capabilities::timed_events_id, agreement_revision, failed.values, failed.verified);
	CHECK(!unverified.finalize() && !unverified.ready()); // a raise that did not install closes host/join
}

// The unmodified manager at its retail address: what the 1126th pending event does.
static void defect(bool expect_holds)
{
	CHECK(map_image()); stub_services();
	auto* m = manager();
	CHECK(m == ptr(retail_object) && word(retail_object) == manager_vtable);
	const auto& l = retail_layout;
	for (std::uint32_t i = 0; i < retail_records; ++i) add(m, 1000 + i, time_of(i));
	CHECK(judge(read_counts(m, l), l, true) == verdict::ok); // 1125 pending: still whole
	add(m, 5000, 99.f);                                       // one more
	const auto c = read_counts(m, l);
	std::printf("retail after 1126 adds: records %u heap %u ring %d used %u alloc %u\n", c.record_count, c.heap_count, int(c.ring_count), c.used_bits, c.alloc_bits);
	if (expect_holds)
	{
		// What a correct queue would do. On the unmodified manager these fail.
		CHECK(c.heap_count == 1126); CHECK(judge(c, l, true) == verdict::ok);
		fired.clear(); pump(m, 1.0e9f);
		CHECK(fired.size() == 1126);
		return;
	}
	// The ring was empty: the alloc handed out ring[read] = record 0 again (still in use by event 1000), and the heap
	// pair written at [1125] put that index on the heap's count: 1 entry left of 1125, 1124 events cut off.
	CHECK(c.record_count == 1126 && int(c.ring_count) == -1 && c.used_bits == 1125 && c.alloc_bits == 1125);
	CHECK(c.heap_count == 1);
	CHECK(judge(c, l, true) != verdict::ok);
	fired.clear(); pump(m, 1.0e9f);
	CHECK(fired.size() == 1 && fired[0] == 5000); // record 0 now holds the new event; event 1000 and 1124 others never fire
	const auto after = read_counts(m, l);
	CHECK(after.heap_count == 0 && after.record_count == 1125 && after.used_bits == 1124); // an empty queue with 1124 records "in use": the freeze
	std::printf("retail after the pump: records %u heap %u ring %d used %u alloc %u; fired %zu\n", after.record_count, after.heap_count, int(after.ring_count), after.used_bits, after.alloc_bits, fired.size());
}

static bool has_line(const char* part) { return std::any_of(lines.begin(), lines.end(), [&](const std::string& s) { return s.find(part) != std::string::npos; }); }

static void raised_install()
{
	CHECK(map_image()); stub_services();
	const bytes before(union_space, union_space + 0x700000);
	// A foreign byte in one edited instruction refuses everything and leaves the image as it was.
	static_cast<unsigned char*>(ptr(0x4b2c14))[0] ^= 0x01; // the opcode byte of an edited instruction (record alloc)
	event_pool::install(reinterpret_cast<HMODULE>(ptr(0x400000)));
	CHECK(!event_pool::installed() && !event_pool::raised() && has_line("not retail") && installed_object == retail_object && installed_layout == retail_layout);
	CHECK(event_pool::agreement() == (agreement_entry{true, {retail_records, 0, 0, 0}, false})); // rule 65 unverified: online closed
	static_cast<unsigned char*>(ptr(0x4b2c14))[0] ^= 0x01;
	CHECK(std::memcmp(before.data(), union_space, before.size()) == 0);
	// Not requested: plain XML2 (no key, stock entity capacity) writes nothing.
	entities_raised = false; lines.clear();
	event_pool::install(reinterpret_cast<HMODULE>(ptr(0x400000)));
	CHECK(!event_pool::installed() && lines.empty() && std::memcmp(before.data(), union_space, before.size()) == 0);
	CHECK(!event_pool::agreement().registers); // plain XML2: the released record
	entities_raised = true;
	// The production default: raised with the entity capacity.
	event_pool::install(reinterpret_cast<HMODULE>(ptr(0x400000)));
	CHECK(event_pool::installed() && event_pool::raised() && !startup_refused && installed_layout == layout_for(raised_records) && installed_object != retail_object);
	CHECK(event_pool::agreement() == (agreement_entry{true, {raised_records, 0, 0, 0}, true})); // rule 65 {8192,0,0,0}
	const auto& l = installed_layout;
	auto* m = manager(); // the game's own getter constructs the relocated object
	CHECK(m == ptr(installed_object) && word(installed_object) == manager_vtable && manager() == m);
	CHECK(judge(read_counts(m, l), l, true) == verdict::ok && read_counts(m, l).ring_count == l.records);
	// The retail statics behind the old object were not touched by the constructor (the init flag byte is the getter's).
	CHECK(std::memcmp(before.data() + (static_cast<const unsigned char*>(ptr(retail_object)) - union_space), ptr(retail_object), retail_layout.body_bytes + 4) == 0);
	// 1126 pending events (the retail break) and then the whole pool.
	std::vector<std::pair<float, std::uint32_t>> expected;
	for (std::uint32_t i = 0; i < l.records; ++i)
	{
		add(m, 1000 + i, time_of(i)); expected.emplace_back(time_of(i), 1000 + i);
		if (i == retail_records) CHECK(judge(read_counts(m, l), l, true) == verdict::ok && read_counts(m, l).heap_count == 1126);
	}
	auto c = read_counts(m, l);
	CHECK(c.record_count == l.records && c.heap_count == l.records && c.ring_count == 0 && c.used_bits == l.records && judge(c, l, true) == verdict::ok);
	add(m, 9999999, 5.f); // the pool is full: dropped, nothing overwritten
	c = read_counts(m, l);
	CHECK(c.record_count == l.records && c.heap_count == l.records && c.ring_count == 0 && has_line("pool FULL"));
	fired.clear(); pump(m, 20.f); // only what is due
	const auto due = std::size_t(std::count_if(expected.begin(), expected.end(), [](const auto& e) { return e.first < 20.f; }));
	CHECK(fired.size() == due && due > 0 && due < expected.size());
	pump(m, 1.0e9f);
	CHECK(fired.size() == expected.size());
	// Each event exactly once, in time order (ties in any order).
	std::vector<std::uint32_t> sorted = fired; std::sort(sorted.begin(), sorted.end());
	bool each_once = sorted.size() == expected.size(); for (std::size_t i = 0; each_once && i < sorted.size(); ++i) each_once = sorted[i] == 1000 + i;
	CHECK(each_once);
	bool ordered = true; for (std::size_t i = 1; i < fired.size(); ++i) ordered = ordered && time_of(fired[i - 1] - 1000) <= time_of(fired[i] - 1000);
	CHECK(ordered);
	c = read_counts(m, l);
	CHECK(c.record_count == 0 && c.heap_count == 0 && c.ring_count == l.records && c.used_bits == 0 && c.alloc_bits == 0 && !has_line("QUEUE BROKEN"));
	// The same push/pop history gives the same firing order as the retail manager for a retail-sized load: checked by
	// the caller against the "order" run of the unmodified image (the heap code is unchanged, only its bounds moved).
	// Watchdog: an emptied queue in a live zone, then a count that disagrees.
	std::strcpy(static_cast<char*>(ptr(0x72a758)), "fixture/zone");
	reinterpret_cast<void(__thiscall*)(void*)>(ptr(word(manager_vtable + 4)))(m); // reset (zone change): the per-zone line and a fresh watch
	CHECK(has_line("timed events fixture/zone: peak 8192 of 8192") || has_line("peak 8192 of 8192"));
	add(m, 1, 10.f); pump(m, 2.f);  // one event pending
	pump(m, 11.f);                  // it fires: the queue is empty from here
	pump(m, 12.f); CHECK(!has_line("EMPTY"));
	pump(m, 13.5f); CHECK(has_line("EMPTY"));
	add(m, 2, 100.f);
	auto* b = static_cast<unsigned char*>(m) + body_at;
	std::uint32_t zero = 0; std::memcpy(b + l.heap_count, &zero, 4); // what the retail overflow leaves: records in use, nothing queued
	pump(m, 6.f); CHECK(has_line("QUEUE BROKEN") && has_line("counts disagree"));
	CHECK(event_pool::status().find("breaks 1") != std::string::npos && event_pool::status().find("empty alarms 1") != std::string::npos);
}

// What join in progress's canonical restart does to the queue (join_in_progress_residue.inl), on the raised pool: read
// the heap pairs and their records, retail pool init, both bitmaps and counts cleared, every entry re-added through
// the retail alloc / store / heap push in sorted order.
static void rebuild()
{
	CHECK(map_image()); stub_services();
	event_pool::install(reinterpret_cast<HMODULE>(ptr(0x400000)));
	CHECK(event_pool::raised());
	const auto& l = installed_layout;
	auto* m = manager(); auto* body = static_cast<unsigned char*>(m) + body_at;
	const std::uint32_t n = 1500; // more than a retail pool holds
	for (std::uint32_t i = 0; i < n; ++i) add(m, 1000 + i, time_of(i));
	struct entry { float time; record r; };
	std::vector<entry> entries;
	std::uint32_t heap_count = 0; std::memcpy(&heap_count, body + l.heap_count, 4);
	CHECK(heap_count == n);
	for (std::uint32_t i = 0; i < heap_count; ++i)
	{
		entry e{}; std::uint32_t index = 0;
		std::memcpy(&e.time, body + l.heap + i * 8, 4); std::memcpy(&index, body + l.heap + i * 8 + 4, 4);
		CHECK(index < l.records);
		std::memcpy(&e.r, body + index * record_bytes, sizeof e.r); entries.push_back(e);
	}
	std::stable_sort(entries.begin(), entries.end(), [](const entry& a, const entry& b) { return a.time != b.time ? a.time < b.time : a.r.owner < b.r.owner; });
	reinterpret_cast<void(__thiscall*)(void*)>(ptr(0x4b2a10))(body);
	std::memset(body + l.used_bitmap, 0, l.words * 4); std::memset(body + l.alloc_bitmap, 0, l.words * 4);
	std::uint32_t zero = 0; std::memcpy(body + l.record_count, &zero, 4); std::memcpy(body + l.heap_count, &zero, 4);
	std::uint32_t expected_index = 0; bool ascending = true;
	for (const auto& e : entries)
	{
		const auto index = reinterpret_cast<std::uint32_t(__thiscall*)(void*)>(ptr(0x4b2c10))(body);
		ascending = ascending && index == expected_index++;
		reinterpret_cast<void(__thiscall*)(void*, std::uint32_t, const void*)>(ptr(0x4b2b80))(body, index, &e.r);
		struct { float time; std::uint32_t index; } pair{e.time, index};
		reinterpret_cast<void(__thiscall*)(void*, const void*)>(ptr(0x4b2c90))(body + l.heap, &pair);
	}
	CHECK(ascending); // the canonical record order: 0, 1, 2, ... on every peer
	const auto c = read_counts(m, l);
	CHECK(c.record_count == n && c.heap_count == n && c.ring_count == l.records - n && c.used_bits == n && c.alloc_bits == n && judge(c, l, true) == verdict::ok);
	fired.clear(); pump(m, 1.0e9f);
	CHECK(fired.size() == n);
	bool ordered = true; for (std::size_t i = 1; i < fired.size(); ++i) ordered = ordered && time_of(fired[i - 1] - 1000) <= time_of(fired[i] - 1000);
	std::vector<std::uint32_t> sorted = fired; std::sort(sorted.begin(), sorted.end());
	bool each_once = true; for (std::size_t i = 0; i < sorted.size(); ++i) each_once = each_once && sorted[i] == 1000 + i;
	CHECK(ordered && each_once && !has_line("QUEUE BROKEN"));
	CHECK(judge(read_counts(m, l), l, true) == verdict::ok && read_counts(m, l).record_count == 0);
}

// The firing order of one fixed load on the unmodified image and on the raised install (separate processes).
static void order(bool raise)
{
	CHECK(map_image()); stub_services();
	if (raise) { event_pool::install(reinterpret_cast<HMODULE>(ptr(0x400000))); CHECK(event_pool::raised()); }
	auto* m = manager();
	std::uint32_t h = 2166136261u;
	const auto mix = [&] { for (const auto f : fired) h = (h ^ f) * 16777619u; fired.clear(); };
	std::uint32_t next = 1000;
	for (unsigned frame = 0; frame < 400; ++frame) // adds and pumps interleaved, never above 1000 pending
	{
		const float now = 10.f + float(frame) * 0.033f;
		for (unsigned k = 0; k < 1 + frame % 5; ++k) { add(m, next, now + float((next * 7u) % 13u) * 0.033f); ++next; }
		pump(m, now); mix();
	}
	pump(m, 1.0e9f); mix();
	std::printf("ORDER %08x events %u\n", h, next - 1000);
}

int main(int argc, char** argv)
{
	rules();
	if (argc > 2)
	{
		std::ifstream f(argv[1], std::ios::binary); raw.assign(std::istreambuf_iterator<char>(f), {});
		if (raw.size() < 0x1000) { std::puts("retail image not readable"); return 2; }
		mode = argv[2];
		if (mode == "defect") defect(false);
		else if (mode == "retail-holds") defect(true);
		else if (mode == "raised") raised_install();
		else if (mode == "rebuild") rebuild();
		else if (mode == "order-retail") order(false);
		else if (mode == "order-raised") order(true);
		else return 2;
	}
	std::printf("%s event pool (%s): %u checks, %u failures\n", failures ? "FAIL" : "PASS", mode.c_str(), checks, failures);
	return failures ? 1 : 0;
}
