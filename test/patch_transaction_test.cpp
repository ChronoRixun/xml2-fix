// The protected code transaction on blocks of the test's own: union guards, conflicts, unexpected writes, page
// ledger, every injected protection/flush/readback failure, verified rollback and rollback failure. No game.
#include "patch_transaction.hpp"

#include <cstdio>
#include <cstring>
#include <string>

using namespace patch_transaction;
namespace
{
	int failures = 0;
	void check(bool ok, const char* what)
	{
		std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
		failures += !ok;
	}
	std::uint8_t* block = nullptr;
	constexpr std::size_t block_pages = 4, page = 4096;
	address va(std::size_t offset) { return static_cast<address>(reinterpret_cast<std::uintptr_t>(block) + offset); }

	void seed()
	{
		DWORD old = 0;
		VirtualProtect(block, block_pages * page, PAGE_READWRITE, &old);
		for (std::size_t i = 0; i < block_pages * page; ++i) block[i] = static_cast<std::uint8_t>(i * 7 + 3);
		VirtualProtect(block, block_pages * page, PAGE_EXECUTE_READ, &old);
	}
	std::vector<std::uint8_t> at(std::size_t offset, std::size_t n) { return std::vector<std::uint8_t>(block + offset, block + offset + n); }
	DWORD protection_at(std::size_t offset)
	{
		MEMORY_BASIC_INFORMATION info{};
		VirtualQuery(block + offset, &info, sizeof(info));
		return info.Protect;
	}
	expectation guard(std::size_t offset, std::size_t n, const char* what) { return {va(offset), at(offset, n), what}; }
	region hash_region(std::size_t lo, std::size_t hi, const char* name)
	{
		region r{name, va(lo), va(hi)};
		r.sha256 = sha256(block + lo, hi - lo);
		return r;
	}
	write new_bytes(std::size_t offset, std::vector<std::uint8_t> bytes, const char* what) { return {va(offset), std::move(bytes), what}; }

	struct counts { int publish = 0, abandon = 0, retain = 0; };
	contribution contrib(const char* owner, counts& c, std::vector<expectation> guards, std::vector<write> writes, std::vector<region> regions = {})
	{
		contribution x{owner, std::move(guards), std::move(regions), std::move(writes)};
		x.publish = [&c] { ++c.publish; };
		x.abandon = [&c] { ++c.abandon; };
		x.retain = [&c] { ++c.retain; };
		return x;
	}
}

int main()
{
	block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, block_pages * page, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
	if (!block) return 90;

	{ // SHA-256 known answer
		const auto d = sha256(reinterpret_cast<const std::uint8_t*>("abc"), 3);
		check(d == sha256_of("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "sha256(abc) known answer");
		const std::string long_text(1000, 'x');
		const auto e = sha256(reinterpret_cast<const std::uint8_t*>(long_text.data()), long_text.size());
		check(e != d && e[0] != 0, "sha256 multi-block runs");
		std::uint8_t empty = 0;
		check(sha256(&empty, 0) == sha256_of("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"), "sha256(empty) known answer");
	}
	{ // relocation encoding
		const auto w = relocation(0x401000, "e9", true, 0x20000000, 2, "jump");
		check(w.bytes.size() == 7 && w.bytes[0] == 0xe9 && w.bytes[5] == 0x90 && w.bytes[6] == 0x90, "rel32 jump with two NOPs has 7 bytes");
		std::uint32_t rel = 0;
		std::memcpy(&rel, w.bytes.data() + 1, 4);
		check(rel == 0x20000000u - 0x401005u, "rel32 is target minus the end of the five-byte branch");
		const auto a = relocation(0x401000, "68", false, 0x12345678, 0, "push abs");
		std::uint32_t abs = 0;
		std::memcpy(&abs, a.bytes.data() + 1, 4);
		check(a.bytes.size() == 5 && abs == 0x12345678, "abs32 push");
		const auto b = bytes_of("00ff7f80");
		check(b.size() == 4 && b[1] == 0xff && b[2] == 0x7f && b[3] == 0x80, "hex bytes");
	}
	{ // commit: two contributions sharing a page, a write crossing a page boundary, publication order
		seed();
		counts a, b;
		transaction t;
		t.add(contrib("a", a, {guard(0x10, 8, "a site"), guard(0xff8, 16, "a boundary site")}, {new_bytes(0x10, {1, 2, 3, 4}, "a write"), new_bytes(0xffc, {9, 9, 9, 9, 9, 9, 9, 9}, "crossing")}));
		t.add(contrib("b", b, {guard(0x10, 4, "b same site"), guard(0x2000, 4, "b far site")}, {new_bytes(0x2000, {5, 6}, "b write")}, {hash_region(0x3000, 0x3100, "b region")}));
		const auto o = t.commit();
		check(o == outcome::committed, "commit succeeds");
		check(a.publish == 1 && b.publish == 1 && a.abandon == 0 && b.abandon == 0, "both contributions published, none abandoned");
		check(at(0x10, 4) == std::vector<std::uint8_t>{1, 2, 3, 4} && at(0xffc, 8) == std::vector<std::uint8_t>(8, 9) && at(0x2000, 2) == std::vector<std::uint8_t>{5, 6}, "bytes written");
		check(t.pages() == 3 && t.written_bytes() == 14, "page ledger: three distinct pages (0, 1 via the crossing write, 2), 14 bytes");
		check(protection_at(0) == PAGE_EXECUTE_READ && protection_at(page) == PAGE_EXECUTE_READ && protection_at(2 * page) == PAGE_EXECUTE_READ, "exact protections restored");
		check(!t.add(contrib("late", a, {}, {})) && t.commit() == outcome::refused_sealed, "late add and a second commit are refused");
		check(a.abandon == 0 && b.abandon == 0 && a.retain == 0 && t.result() == outcome::committed && a.publish == 1,
		      "a second commit after success changes nothing: no abandonment, the committed result stays");
	}
	{ // guard mismatch leaves everything untouched
		seed();
		counts a;
		transaction t;
		auto g = guard(0x40, 4, "stale");
		g.bytes[2] ^= 1;
		t.add(contrib("a", a, {g}, {new_bytes(0x40, {1}, "w")}));
		const auto before = at(0, block_pages * page);
		check(t.commit() == outcome::refused_guard && at(0, block_pages * page) == before && a.abandon == 1 && a.publish == 0, "guard mismatch refuses, nothing written, abandoned");
	}
	{ // region hash mismatch
		seed();
		counts a;
		transaction t;
		auto r = hash_region(0x3000, 0x3040, "r");
		r.sha256[0] ^= 1;
		t.add(contrib("a", a, {guard(0x40, 4, "g")}, {new_bytes(0x40, {1}, "w")}, {r}));
		check(t.commit() == outcome::refused_region, "region hash mismatch refuses");
	}
	{ // conflicting guards, conflicting writes, agreeing duplicates, unexpected byte
		seed();
		counts a, b;
		transaction t;
		auto g2 = guard(0x50, 4, "g2");
		g2.bytes[1] ^= 1;
		t.add(contrib("a", a, {guard(0x50, 4, "g1")}, {new_bytes(0x50, {1}, "w")}));
		t.add(contrib("b", b, {g2}, {}));
		check(t.commit() == outcome::refused_conflict && a.abandon == 1 && b.abandon == 1, "overlapping guards that disagree refuse");
		transaction u;
		u.add(contrib("a", a, {guard(0x60, 4, "g")}, {new_bytes(0x60, {1, 2}, "w1")}));
		u.add(contrib("b", b, {guard(0x60, 4, "g")}, {new_bytes(0x61, {3}, "w2")}));
		check(u.commit() == outcome::refused_conflict, "two writes disagreeing about a byte refuse");
		transaction v;
		v.add(contrib("a", a, {guard(0x70, 4, "g")}, {new_bytes(0x70, {1, 2}, "w1")}));
		v.add(contrib("b", b, {guard(0x70, 4, "g")}, {new_bytes(0x71, {2, 7}, "w2")}));
		check(v.commit() == outcome::committed && at(0x70, 3) == std::vector<std::uint8_t>{1, 2, 7}, "agreeing duplicate bytes compose");
		transaction w;
		w.add(contrib("a", a, {guard(0x80, 4, "g")}, {new_bytes(0x82, {1, 2, 3}, "spills")}));
		check(w.commit() == outcome::refused_unexpected, "a write past its guard refuses");
		transaction x;
		check(x.commit() == outcome::refused_empty, "empty transaction refuses");
	}
	{ // injected protection failure on the second page: first page restored, nothing written
		seed();
		counts a;
		transaction t;
		t.add(contrib("a", a, {guard(0x90, 4, "g1"), guard(0x1090, 4, "g2")}, {new_bytes(0x90, {1}, "w1"), new_bytes(0x1090, {2}, "w2")}));
		int calls = 0;
		hooks h;
		h.protect = [&](void* p, std::size_t n, DWORD np, DWORD* old) { ++calls; if (calls == 2 && np == PAGE_EXECUTE_READWRITE) return false; return VirtualProtect(p, n, np, old) != FALSE; };
		const auto before = at(0, block_pages * page);
		check(t.commit(&h) == outcome::refused_protect && at(0, block_pages * page) == before && protection_at(0) == PAGE_EXECUTE_READ, "protection failure refuses with the first page re-locked");
	}
	{ // injected flush failure: rolled back and verified
		seed();
		counts a;
		transaction t;
		t.add(contrib("a", a, {guard(0xa0, 4, "g")}, {new_bytes(0xa0, {1, 2, 3, 4}, "w")}));
		hooks h;
		int flushes = 0;
		h.flush = [&](void* p, std::size_t n) { ++flushes; return flushes == 1 ? false : FlushInstructionCache(GetCurrentProcess(), p, n) != FALSE; };
		const auto before = at(0xa0, 4);
		check(t.commit(&h) == outcome::rolled_back && at(0xa0, 4) == before && protection_at(0) == PAGE_EXECUTE_READ && a.abandon == 1, "flush failure rolls back, verifies and re-locks");
	}
	{ // readback corruption after apply: rolled back; persistent corruption: rollback failed
		seed();
		counts a;
		transaction t;
		t.add(contrib("a", a, {guard(0xb0, 4, "g")}, {new_bytes(0xb0, {1, 2, 3, 4}, "w")}));
		hooks h;
		h.after_apply = [] { block[0xb1] = 0x77; };
		check(t.commit(&h) == outcome::rolled_back && at(0xb0, 4) == at(0xb0, 4) && block[0xb1] == static_cast<std::uint8_t>(0xb1 * 7 + 3), "readback mismatch rolls back to the expected bytes");
		seed();
		transaction u;
		u.add(contrib("a", a, {guard(0xc0, 4, "g")}, {new_bytes(0xc0, {1, 2, 3, 4}, "w")}));
		hooks k;
		k.after_apply = [] { block[0xc1] = 0x77; };
		k.after_rollback = [] { block[0xc1] = 0x66; };
		counts r;
		u.add(contrib("r", r, {}, {}));
		check(u.commit(&k) == outcome::rollback_failed, "a rollback that does not verify is reported as failed");
		check(a.retain == 1 && r.retain == 1 && r.abandon == 0 && a.publish == 0, "an uncertified rollback RETAINS every contribution's storage (retain, never abandon)");
	}
	{ // restore failure after a good write rolls back (the plan: exact protections before publication)
		seed();
		counts a;
		transaction t;
		t.add(contrib("a", a, {guard(0xd0, 4, "g")}, {new_bytes(0xd0, {1}, "w")}));
		hooks h;
		h.protect = [&](void* p, std::size_t n, DWORD np, DWORD* old) { if (np == PAGE_EXECUTE_READ) { static int fails = 0; if (++fails == 1) return false; } return VirtualProtect(p, n, np, old) != FALSE; };
		check(t.commit(&h) == outcome::rolled_back && a.publish == 0 && protection_at(0) == PAGE_EXECUTE_READ, "a protection-restore failure rolls back and never publishes");
	}
	{ // protection acquisition fails on the second page AND the first page cannot be restored: uncertified
		seed();
		counts a;
		transaction t;
		t.add(contrib("a", a, {guard(0x90, 4, "g1"), guard(0x1090, 4, "g2")}, {new_bytes(0x90, {1}, "w1"), new_bytes(0x1090, {2}, "w2")}));
		int calls = 0;
		hooks h;
		h.protect = [&](void* p, std::size_t n, DWORD np, DWORD* old) { ++calls; if (calls == 2) return false; if (calls == 3) return false; return VirtualProtect(p, n, np, old) != FALSE; };
		const auto o = t.commit(&h);
		DWORD ignored = 0; VirtualProtect(block, page, PAGE_EXECUTE_READ, &ignored); // the test restores its own page
		check(o == outcome::rollback_failed && a.retain == 1 && a.abandon == 0, "acquisition unwind that cannot restore a page is uncertified: rollback_failed, storage retained");
	}
	{ // composed sites: a contribution requiring a write nobody makes refuses the whole transaction, naming the site
		seed();
		counts a, b;
		transaction t;
		auto x = contrib("needs", a, {guard(0xe0, 4, "g")}, {new_bytes(0xe0, {1}, "w")});
		x.requires_written.push_back({va(0xe2), "composed site"});
		t.add(std::move(x));
		const auto before = at(0, block_pages * page);
		const auto o = t.commit();
		check(o == outcome::refused_unresolved && at(0, block_pages * page) == before && a.abandon == 1 && t.report().find("0x") != std::string::npos,
		      "an unwritten required site refuses before any byte changes and is reported in hex");
		seed();
		transaction u;
		auto y = contrib("needs", a, {guard(0xe0, 4, "g")}, {new_bytes(0xe0, {1}, "w")});
		y.requires_written.push_back({va(0xe2), "composed site"});
		u.add(std::move(y));
		u.add(contrib("writer", b, {}, {new_bytes(0xe2, {9}, "the composed write")}));
		check(u.commit() == outcome::committed && at(0xe2, 1) == std::vector<std::uint8_t>{9}, "the same requirement is met when another contribution writes the site");
	}
	check(hex(0x4c6910) == "0x4c6910" && hex(0) == "0x0", "hex() formats addresses as 0x + lower-case hex");
	std::printf("%d failure(s)\n", failures);
	return failures ? 1 : 0;
}
