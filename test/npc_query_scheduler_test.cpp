// The shared AI query scheduler of both games, executed: X-Men Legends 1's admission code (default.xbe 0xfc9aa..0xfca7c,
// inside 0xfc990) and XMen2.exe's (0x50274a..0x50281c, inside 0x502730), retail and with gameplay rule 16's
// reservation / budget bytes (npc_power_rules.hpp, item 7), run against the same request schedules. Both are read from
// the owner's files at run time and checked by full SHA-256 before anything executes; no game code is embedded. The six
// scheduler globals are relocated into the test, the two refusal branches go to a stand-in for the refusal tail
// (0xfcba0 / 0x50294a: the reserved owner already served this tick gets the cached answer, everyone else "deferred").
// Does not launch the game.
//
// usage: xml2_npc_query_scheduler_test <XMen2.exe> [<default.xbe>]
#include "npc_power_rules.hpp"
#include "sha256.hpp"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace
{
	using u8 = std::uint8_t;
	using u32 = std::uint32_t;
	int failures = 0, checks = 0;
	void check(bool ok, const char* why)
	{
		++checks;
		if (!ok)
		{
			++failures;
			std::printf("FAIL %s\n", why);
		}
	}

	// The scheduler's six globals, in the test. XML1: 0x4fee0c clock, 0x4fee10 count, 0x4ff9dc reserved owner served
	// this tick, 0x4fee18 cached hit byte, 0x4fee14 oldest queued age, 0x4fee34 reserved owner. XMen2.exe: 0x78a9e8,
	// 0x78a9ec, 0x78aa10, 0x78a9f4, 0x78a9f0, 0x78ac20.
	struct globals
	{
		float clock = 0;
		u32 count = 0, served_reserved = 0;
		u32 flag = 0;
		u32 oldest = 0, reserved = 0;
		bool operator==(const globals& o) const
		{
			return std::memcmp(&clock, &o.clock, 4) == 0 && count == o.count && served_reserved == o.served_reserved &&
			       (flag & 0xff) == (o.flag & 0xff) && oldest == o.oldest && reserved == o.reserved;
		}
	};

	struct layout
	{
		const char* name;
		u32 begin, end, refusal;   // the admission run, its fall-through (served), the refusal tail
		u32 clock_offset, age_offset; // AI fields: the AI clock, the queued age
		std::array<u32, 6> global; // clock, count, served_reserved, flag, oldest, reserved
		std::array<int, 6> uses;   // how often each global appears in the run
		std::array<u8, 32> hash;   // the retail run's SHA-256
	};
	constexpr layout xml1_layout{"XML1 0xfc990", 0xfc9aa, 0xfca7c, 0xfcba0, 0x51c, 0x2a8,
	                             {0x4fee0c, 0x4fee10, 0x4ff9dc, 0x4fee18, 0x4fee14, 0x4fee34}, {2, 3, 2, 1, 4, 4},
	                             {0x10, 0x75, 0x7e, 0x81, 0xe1, 0x5c, 0xc5, 0xb7, 0xd2, 0xcf, 0xb5, 0x6b, 0x7a, 0x3b, 0x6a, 0x81,
	                              0x78, 0xd3, 0x75, 0x5b, 0x5e, 0xcf, 0x2f, 0x57, 0x57, 0x40, 0xd8, 0xbb, 0x52, 0x89, 0xcc, 0xcc}};
	constexpr layout xml2_layout{"XMen2.exe 0x502730", 0x50274a, 0x50281c, 0x50294a, 0x550, 0x2d8,
	                             {0x78a9e8, 0x78a9ec, 0x78aa10, 0x78a9f4, 0x78a9f0, 0x78ac20}, {2, 3, 2, 1, 4, 4},
	                             {0x52, 0xf7, 0x80, 0xb5, 0xea, 0x1c, 0x3b, 0x22, 0xd7, 0x3c, 0x18, 0x12, 0x1a, 0x60, 0x2e, 0x1f,
	                              0xef, 0xe8, 0x96, 0xe6, 0xdf, 0x17, 0xdc, 0xf6, 0x82, 0x92, 0xfd, 0xaa, 0xee, 0x0c, 0xbb, 0x0f}};

	std::vector<u8> read_file(const char* path)
	{
		std::ifstream file(path, std::ios::binary);
		return std::vector<u8>((std::istreambuf_iterator<char>(file)), {});
	}
	u32 word(const std::vector<u8>& raw, std::size_t at)
	{
		u32 v = 0;
		if (at + 4 <= raw.size()) std::memcpy(&v, raw.data() + at, 4);
		return v;
	}
	std::optional<std::vector<u8>> pe_bytes(const std::vector<u8>& raw, u32 va, u32 size)
	{
		if (raw.size() < 0x400) return std::nullopt;
		const u32 pe = word(raw, 0x3c);
		if (pe + 0xf8 > raw.size()) return std::nullopt;
		const u32 sections = raw[pe + 6] | (raw[pe + 7] << 8), optional = raw[pe + 20] | (raw[pe + 21] << 8);
		const u32 base = word(raw, pe + 24 + 28), rva = va - base, table = pe + 24 + optional;
		for (u32 i = 0; i < sections; ++i)
		{
			const u32 vsize = word(raw, table + 40 * i + 8), vaddr = word(raw, table + 40 * i + 12),
			          rsize = word(raw, table + 40 * i + 16), rptr = word(raw, table + 40 * i + 20);
			if (rva >= vaddr && rva + size <= vaddr + (vsize > rsize ? vsize : rsize) && rptr + rva - vaddr + size <= raw.size())
				return std::vector<u8>(raw.begin() + rptr + (rva - vaddr), raw.begin() + rptr + (rva - vaddr) + size);
		}
		return std::nullopt;
	}
	std::optional<std::vector<u8>> xbe_bytes(const std::vector<u8>& raw, u32 va, u32 size)
	{
		if (raw.size() < 0x124 || std::memcmp(raw.data(), "XBEH", 4)) return std::nullopt;
		const u32 image = word(raw, 0x104), count = word(raw, 0x11c), table = word(raw, 0x120);
		if (table < image || count > 64) return std::nullopt;
		for (u32 i = 0; i < count; ++i)
		{
			const std::size_t s = std::size_t(table - image) + i * 0x38;
			const u32 vaddr = word(raw, s + 4), offset = word(raw, s + 12), bytes = word(raw, s + 16);
			if (va >= vaddr && va - vaddr + size <= bytes && std::size_t(offset) + va - vaddr + size <= raw.size())
				return std::vector<u8>(raw.begin() + offset + (va - vaddr), raw.begin() + offset + (va - vaddr) + size);
		}
		return std::nullopt;
	}

	// esp at the call = S; the run pushes edi, so its [esp+0x30] (the caller's fifth argument, the request's weight)
	// is S+0x28. ESI = the AI; EBX is zeroed by the run itself.
#pragma warning(push)
#pragma warning(disable : 4100) // the naked body reads its arguments from the stack
	__declspec(naked) int __cdecl run_admission(void* code, void* ai, int weight)
	{
		__asm {
			push ebp
			push ebx
			push esi
			push edi
			mov esi, [esp + 0x18]
			mov eax, [esp + 0x1c]
			mov edx, [esp + 0x14]
			sub esp, 0x40
			mov [esp + 0x28], eax
			call edx
			add esp, 0x40
			pop edi
			pop esi
			pop ebx
			pop ebp
			ret
		}
	}
#pragma warning(pop)

	enum answer : int { deferred = 0, served = 1, cached = 2 };

	// One engine's admission run, made executable with the test's globals and AIs.
	class scheduler
	{
	public:
		static constexpr int ais = 12;
		scheduler(const layout& l, std::vector<u8> run, bool& ok) : l_(l)
		{
			ok = false;
			if (run.size() != l.end - l.begin) return;
			// The refusal exits (jle / jge rel32 to the refusal tail) - exactly two - go to the stand-in.
			const u32 stub_served = static_cast<u32>(run.size()), stub_refusal = stub_served + 7;
			int exits = 0;
			for (u32 i = 0; i + 6 <= run.size(); ++i)
			{
				if (run[i] != 0x0f || (run[i + 1] != 0x8e && run[i + 1] != 0x8d)) continue;
				const u32 target = l.begin + i + 6 + word(run, i + 2);
				if (target != l.refusal) continue;
				const u32 rel = stub_refusal - (i + 6);
				std::memcpy(run.data() + i + 2, &rel, 4);
				++exits;
			}
			if (exits != 2) return;
			// The globals, by operand value; every count as expected.
			const std::array<void*, 6> fields{&g_.clock, &g_.count, &g_.served_reserved, &g_.flag, &g_.oldest, &g_.reserved};
			for (int k = 0; k < 6; ++k)
			{
				int found = 0;
				for (u32 i = 0; i + 4 <= run.size(); ++i)
				{
					if (word(run, i) != l.global[k]) continue;
					const u32 to = static_cast<u32>(reinterpret_cast<std::uintptr_t>(fields[k]));
					std::memcpy(run.data() + i, &to, 4);
					++found;
					i += 3;
				}
				if (found != l.uses[k]) return;
			}
			// Served: pop edi; mov eax,1; ret. Refusal: the reserved owner already served this tick (its token in
			// served_reserved) gets the cached answer, everyone else deferred: mov edx,[esi+0x10]; mov eax,[edx+0x1c];
			// cmp eax,[served_reserved]; jne +7; pop edi; mov eax,2; ret; pop edi; xor eax,eax; ret.
			const u8 tail[]{0x5f, 0xb8, 1, 0, 0, 0, 0xc3, 0x8b, 0x56, 0x10, 0x8b, 0x42, 0x1c, 0x3b, 0x05, 0, 0, 0, 0, 0x75, 0x07,
			                0x5f, 0xb8, 2, 0, 0, 0, 0xc3, 0x5f, 0x33, 0xc0, 0xc3};
			run.insert(run.end(), std::begin(tail), std::end(tail));
			const u32 sr = static_cast<u32>(reinterpret_cast<std::uintptr_t>(&g_.served_reserved));
			std::memcpy(run.data() + stub_served + 15, &sr, 4);
			code_ = VirtualAlloc(nullptr, run.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
			if (!code_) return;
			std::memcpy(code_, run.data(), run.size());
			DWORD old = 0;
			if (!VirtualProtect(code_, run.size(), PAGE_EXECUTE_READ, &old) || !FlushInstructionCache(GetCurrentProcess(), code_, run.size()))
				return;
			for (int i = 0; i < ais; ++i)
			{
				actor_[i][7] = 0x1000 + static_cast<u32>(i); // the token at actor+0x1c
				void* a = actor_[i].data();
				std::memcpy(ai_[i].data() + 0x10, &a, sizeof a);
			}
			ok = true;
		}
		~scheduler()
		{
			if (code_) VirtualFree(code_, 0, MEM_RELEASE);
		}
		scheduler(const scheduler&) = delete;
		scheduler& operator=(const scheduler&) = delete;
		int request(int who, float clock, int weight)
		{
			std::memcpy(ai_[who].data() + l_.clock_offset, &clock, 4);
			return run_admission(code_, ai_[who].data(), weight);
		}
		u32 age(int who) const { return read_u32(ai_[who], l_.age_offset); }
		u32 token(int who) const { return actor_[who][7]; }
		const globals& state() const { return g_; }

	private:
		static u32 read_u32(const std::array<u8, 0x600>& a, u32 at)
		{
			u32 v = 0;
			std::memcpy(&v, a.data() + at, 4);
			return v;
		}
		const layout& l_;
		globals g_{};
		void* code_ = nullptr;
		std::array<std::array<u8, 0x600>, ais> ai_{};
		std::array<std::array<u32, 16>, ais> actor_{};
	};

	// The scan weight of 0x104b40 / 0x50b560 (both: param + int(100 - 0.1 x distance), param 5 or 50).
	int scan_weight(int param, float distance) { return param + static_cast<int>(100.0f - 0.1f * distance); }

	struct request_t
	{
		int who, weight;
	};

	// The harness artifact (beta6-ai-rest r-original-moving-02): the fighter (AI 0) asks every tick, weight 1, first
	// in tick order; at tick 5 ten parked, unaware enemies (setAIActive(false): AI+4 = 2 -> one scan with weight 5,
	// then state 3, no more scans) ask once each after the fighter was served. Returns the fighter's answers for
	// `after` ticks and the reserved owner's state; then the reserved owner asks once more, first in its tick.
	struct stale_result
	{
		std::vector<int> fighter;
		u32 reserved = 0, oldest = 0;
		int owner_answer = -1;
		std::vector<int> after_release;
	};
	stale_result stale_reservation(scheduler& s, int after)
	{
		stale_result r;
		float t = 1.0f;
		const float distances[10]{367, 420, 500, 610, 700, 772, 800, 845, 900, 990};
		for (int tick = 0; tick < 5; ++tick, t += 0.0204f) s.request(0, t, 1);
		s.request(0, t, 1);
		for (int p = 0; p < 10; ++p) s.request(1 + p, t, scan_weight(5, distances[p]));
		t += 0.0204f;
		r.reserved = s.state().reserved;
		r.oldest = s.state().oldest;
		for (int tick = 0; tick < after; ++tick, t += 0.0204f) r.fighter.push_back(s.request(0, t, 1));
		int owner = -1;
		for (int p = 1; p <= 10; ++p)
			if (s.token(p) == r.reserved) owner = p;
		if (owner > 0)
		{
			r.owner_answer = s.request(owner, t, scan_weight(5, distances[owner - 1]));
			r.after_release.push_back(s.request(0, t, 1));
			t += 0.0204f;
			for (int tick = 0; tick < 20; ++tick, t += 0.0204f) r.after_release.push_back(s.request(0, t, 1));
		}
		return r;
	}

	// 1. One query per AI tick in XML1 and the rule; two in the retail port.
	void budget(scheduler& x1, scheduler& patched, scheduler& retail)
	{
		const int a[]{x1.request(0, 1.0f, 1), x1.request(1, 1.0f, 1), x1.request(2, 1.0f, 1)};
		const int b[]{patched.request(0, 1.0f, 1), patched.request(1, 1.0f, 1), patched.request(2, 1.0f, 1)};
		const int c[]{retail.request(0, 1.0f, 1), retail.request(1, 1.0f, 1), retail.request(2, 1.0f, 1)};
		check(a[0] == served && a[1] == deferred && a[2] == deferred, "XML1: one query per tick");
		check(std::memcmp(a, b, sizeof a) == 0, "rule 16: one query per tick, as XML1");
		check(c[0] == served && c[1] == served && c[2] == deferred, "retail XMen2.exe: two queries per tick");
	}
	// 2. A refused request with the oldest age reserves the next query, across ticks: the reserved AI is served on the
	//    next tick although another AI asks first, and its second ask in that tick gets the cached answer.
	void reservation(scheduler& x1, scheduler& patched)
	{
		const int a[]{x1.request(0, 2.0f, 1), x1.request(1, 2.0f, 1), x1.request(0, 2.0f, 1), x1.request(1, 2.0f, 1)};
		const int b[]{patched.request(0, 2.0f, 1), patched.request(1, 2.0f, 1), patched.request(0, 2.0f, 1),
		              patched.request(1, 2.0f, 1)};
		check(a[0] == served && a[1] == deferred && a[2] == deferred && a[3] == deferred, "XML1: AI 1 waits, reserved");
		check(std::memcmp(a, b, sizeof a) == 0, "rule 16: the same answers");
		check(x1.state().reserved == x1.token(1) && x1.state().oldest == 2, "XML1: AI 1 holds the reservation with age 2");
		const int c[]{x1.request(0, 2.1f, 1), x1.request(1, 2.1f, 1), x1.request(1, 2.1f, 1)};
		const int d[]{patched.request(0, 2.1f, 1), patched.request(1, 2.1f, 1), patched.request(1, 2.1f, 1)};
		check(c[0] == deferred && c[1] == served && c[2] == cached, "XML1: the reservation holds across the tick; cached second ask");
		check(std::memcmp(c, d, sizeof c) == 0, "rule 16: the reservation holds across the tick, as XML1");
		check(x1.state() == patched.state() && x1.state().served_reserved == x1.token(1), "rule 16: the same globals as XML1");
	}

	// 3. The stale reservation.
	void stale(scheduler& x1, scheduler& patched, scheduler& retail)
	{
		const auto a = stale_reservation(x1, 400), b = stale_reservation(patched, 400), c = stale_reservation(retail, 400);
		// The heaviest parked scan (367 units: 5 + 63 = 68) reserves; the fighter is then served once per 69 requests.
		check(a.reserved == x1.token(1) && a.oldest == 68, "XML1: the closest parked enemy's scan holds the reservation (age 68)");
		int served_a = 0;
		bool pattern = true;
		for (std::size_t i = 0; i < a.fighter.size(); ++i)
		{
			const bool expect = (i + 1) % 69 == 0;
			pattern &= (a.fighter[i] == served) == expect;
			served_a += a.fighter[i] == served;
		}
		check(pattern && served_a == 5, "XML1: stale reservation -> the fighter is served once per 69 requests (5 of 400)");
		check(a.owner_answer == served && a.after_release.size() == 21 && a.after_release[0] == deferred,
		      "XML1: the reserved owner, asking first, is served and releases the reservation");
		bool free_after = true;
		for (std::size_t i = 1; i < a.after_release.size(); ++i) free_after &= a.after_release[i] == served;
		check(free_after, "XML1: afterwards the fighter is served every tick");
		check(b.fighter == a.fighter && b.reserved == patched.token(1) && b.oldest == a.oldest && b.owner_answer == a.owner_answer &&
		          b.after_release == a.after_release,
		      "rule 16: the same stale-reservation throttle as XML1, request by request");
		int served_c = 0;
		for (const int v : c.fighter) served_c += v == served;
		check(served_c == 400, "retail XMen2.exe: the first request of a tick bypasses the reservation (no throttle)");
		std::printf("  stale reservation: fighter served %d / 400 (XML1), %d / 400 (rule 16), %d / 400 (retail)\n", served_a,
		            static_cast<int>(std::count(b.fighter.begin(), b.fighter.end(), static_cast<int>(served))), served_c);
	}

	// Random schedules: several AIs, random order and repeats per tick, the four request weights the games use (1
	// visibility, 30 0xfce90 / 0x5036b0, 5+ and 50+ scans), occasional repeated clocks. XML1 and the rule must give the
	// same answer to every request and keep the same globals and ages; the retail port must differ somewhere.
	void fuzz(scheduler& x1, scheduler& patched, scheduler& retail, unsigned seed, int ticks)
	{
		std::mt19937 rng(seed);
		float t = 100.0f + static_cast<float>(seed);
		bool same = true, state_same = true, retail_differs = false;
		long served1 = 0, served_retail = 0;
		for (int tick = 0; tick < ticks; ++tick)
		{
			if (rng() % 16 != 0) t += 0.0204f;
			std::vector<request_t> order;
			const int n = static_cast<int>(rng() % 7);
			for (int k = 0; k < n; ++k)
			{
				const int who = static_cast<int>(rng() % 6);
				int weight = 1;
				switch (rng() % 8)
				{
				case 0: weight = 30; break;
				case 1: weight = scan_weight(5, static_cast<float>(rng() % 1000)); break;
				case 2: weight = scan_weight(50, static_cast<float>(rng() % 1000)); break;
				default: break;
				}
				order.push_back({who, weight});
			}
			for (const auto& q : order)
			{
				const int a = x1.request(q.who, t, q.weight), b = patched.request(q.who, t, q.weight), c = retail.request(q.who, t, q.weight);
				same &= a == b;
				retail_differs |= a != c;
				served1 += a != deferred;
				served_retail += c != deferred;
			}
			state_same &= x1.state() == patched.state();
			for (int who = 0; who < 6; ++who) state_same &= x1.age(who) == patched.age(who);
		}
		char line[160];
		std::snprintf(line, sizeof line, "random schedule %u: rule 16 answers every request as XML1", seed);
		check(same, line);
		std::snprintf(line, sizeof line, "random schedule %u: rule 16 keeps XML1's globals and queued ages", seed);
		check(state_same, line);
		std::snprintf(line, sizeof line, "random schedule %u: retail XMen2.exe differs (%ld vs %ld answered)", seed, served_retail, served1);
		check(retail_differs && served_retail > served1, line);
	}
}

int main(int argc, char** argv)
{
	using namespace npc_power_rules;
	std::printf("Shared AI query scheduler: XML1 0xfc990 against XMen2.exe 0x502730 (retail and rule 16)\n");
	// The rule's bytes land where the run expects them.
	check(query_priority_site.va - xml2_layout.begin == 0x73 && query_budget_site.va - xml2_layout.begin == 0x96,
	      "rule 16's reservation and budget sites inside the admission run");
	check(query_priority_bytes().size() == query_priority_site.patched && query_budget_bytes().size() == query_budget_site.patched,
	      "rule 16's reservation and budget byte counts");
	if (argc < 2)
	{
		check(false, "XMen2.exe path required");
		std::printf("Query scheduler: %d checks, %d failures\n", checks, failures);
		return 1;
	}
	const auto exe = read_file(argv[1]);
	const auto pc = pe_bytes(exe, xml2_layout.begin, xml2_layout.end - xml2_layout.begin);
	check(pc && sha256_fips::digest(pc->data(), pc->size()) == xml2_layout.hash, "XMen2.exe admission run identity (retail)");
	if (!pc || sha256_fips::digest(pc->data(), pc->size()) != xml2_layout.hash)
	{
		std::printf("Query scheduler: %d checks, %d failures\n", checks, failures);
		return 1;
	}
	// The guarded retail bytes of the two rule sites are the ones in the run.
	check(limits_rules::matches(pc->data() + 0x73, query_priority_site.hex) && limits_rules::matches(pc->data() + 0x96, query_budget_site.hex),
	      "rule 16's guards match the run");
	auto patched_run = *pc;
	const auto priority_bytes = query_priority_bytes(), budget_bytes = query_budget_bytes();
	std::memcpy(patched_run.data() + 0x73, priority_bytes.data(), priority_bytes.size());
	std::memcpy(patched_run.data() + 0x96, budget_bytes.data(), budget_bytes.size());
	if (argc < 3)
	{
		std::printf("  skip  XML1 comparison not requested (supply the owned default.xbe as the second path)\n");
		std::printf("Query scheduler: %d checks, %d failures\n", checks, failures);
		return failures ? 1 : 0;
	}
	const auto xbe = read_file(argv[2]);
	const auto x1 = xbe_bytes(xbe, xml1_layout.begin, xml1_layout.end - xml1_layout.begin);
	const bool x1_ok = x1 && sha256_fips::digest(x1->data(), x1->size()) == xml1_layout.hash;
	check(x1_ok, "default.xbe admission run identity");
	if (!x1_ok)
	{
		std::printf("Query scheduler: %d checks, %d failures\n", checks, failures);
		return 1;
	}
	// Every scenario on fresh copies of the three runs.
	const auto trio = [&](auto&& body) {
		bool a = false, b = false, c = false;
		scheduler s1(xml1_layout, *x1, a), s2(xml2_layout, patched_run, b), s3(xml2_layout, *pc, c);
		check(a && b && c, "admission runs relocated (two refusal exits, every global)");
		if (a && b && c) body(s1, s2, s3);
	};
	trio([](scheduler& s1, scheduler& s2, scheduler& s3) { budget(s1, s2, s3); });
	trio([](scheduler& s1, scheduler& s2, scheduler&) { reservation(s1, s2); });
	trio([](scheduler& s1, scheduler& s2, scheduler& s3) { stale(s1, s2, s3); });
	for (unsigned seed : {1u, 7u, 49u, 2026u})
		trio([seed](scheduler& s1, scheduler& s2, scheduler& s3) { fuzz(s1, s2, s3, seed, 20000); });
	std::printf("Query scheduler: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
