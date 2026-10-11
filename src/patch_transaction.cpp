#include "patch_transaction.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#ifdef XML2_SAVE_STARTUP_FAULT_TEST
#include "../test/save_startup_faults.hpp" // the owned fault DLL wraps the transaction's Windows calls
#endif

namespace patch_transaction
{
	namespace
	{
		// ---- raw memory (no C++ objects in these: they run under SEH) ----------------------------------------
		bool raw_read(const void* from, void* out, std::size_t size)
		{
			__try { std::memcpy(out, from, size); return true; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		bool raw_write(void* to, const void* from, std::size_t size)
		{
			__try { std::memcpy(to, from, size); return true; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		void* at(address va) { return reinterpret_cast<void*>(static_cast<std::uintptr_t>(va)); }

		// ---- SHA-256 (FIPS 180-4), small and dependency-free ------------------------------------------------
		constexpr std::uint32_t k[64] = {
			0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
			0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
			0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
			0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
			0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
			0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
		constexpr std::uint32_t rotr(std::uint32_t x, unsigned n) { return (x >> n) | (x << (32 - n)); }

		void sha256_block(std::uint32_t h[8], const std::uint8_t* p)
		{
			std::uint32_t w[64];
			for (int i = 0; i < 16; ++i) w[i] = (std::uint32_t(p[4 * i]) << 24) | (std::uint32_t(p[4 * i + 1]) << 16) | (std::uint32_t(p[4 * i + 2]) << 8) | p[4 * i + 3];
			for (int i = 16; i < 64; ++i)
			{
				const auto s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
				const auto s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
				w[i] = w[i - 16] + s0 + w[i - 7] + s1;
			}
			std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
			for (int i = 0; i < 64; ++i)
			{
				const auto t1 = hh + (rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25)) + ((e & f) ^ (~e & g)) + k[i] + w[i];
				const auto t2 = (rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
				hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
			}
			h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
		}

		std::uint8_t nibble(char c) { return static_cast<std::uint8_t>(c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'); }
	}

	std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t size)
	{
		std::uint32_t h[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a, 0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
		std::size_t i = 0;
		for (; i + 64 <= size; i += 64) sha256_block(h, data + i);
		std::uint8_t tail[128]{};
		const std::size_t rest = size - i;
		std::memcpy(tail, data + i, rest);
		tail[rest] = 0x80;
		const std::size_t tail_len = rest < 56 ? 64 : 128;
		const std::uint64_t bits = std::uint64_t(size) * 8;
		for (int j = 0; j < 8; ++j) tail[tail_len - 1 - j] = static_cast<std::uint8_t>(bits >> (8 * j));
		sha256_block(h, tail);
		if (tail_len == 128) sha256_block(h, tail + 64);
		std::array<std::uint8_t, 32> out{};
		for (int j = 0; j < 8; ++j) for (int b = 0; b < 4; ++b) out[4 * j + b] = static_cast<std::uint8_t>(h[j] >> (24 - 8 * b));
		return out;
	}

	std::vector<std::uint8_t> bytes_of(std::string_view hex)
	{
		std::vector<std::uint8_t> out(hex.size() / 2);
		for (std::size_t i = 0; i < out.size(); ++i) out[i] = static_cast<std::uint8_t>((nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]));
		return out;
	}

	std::array<std::uint8_t, 32> sha256_of(std::string_view hex)
	{
		std::array<std::uint8_t, 32> out{};
		const auto v = bytes_of(hex);
		std::copy_n(v.begin(), std::min<std::size_t>(v.size(), 32), out.begin());
		return out;
	}

	write relocation(address va, std::string_view prefix_hex, bool relative, address target, unsigned pad, std::string what)
	{
		write w{va, bytes_of(prefix_hex), std::move(what)};
		const address next = va + static_cast<address>(w.bytes.size()) + 4;
		const std::uint32_t value = relative ? target - next : target;
		for (int i = 0; i < 4; ++i) w.bytes.push_back(static_cast<std::uint8_t>(value >> (8 * i)));
		for (unsigned i = 0; i < pad; ++i) w.bytes.push_back(0x90);
		return w;
	}

	bool read_memory(address va, void* out, std::size_t size) { return raw_read(at(va), out, size); }

	std::string hex(address va)
	{
		char text[16];
		std::snprintf(text, sizeof text, "0x%lx", static_cast<unsigned long>(va));
		return text;
	}

	const char* name(outcome o)
	{
		switch (o)
		{
		case outcome::committed: return "committed";
		case outcome::refused_empty: return "refused: nothing to write";
		case outcome::refused_sealed: return "refused: transaction already sealed";
		case outcome::refused_guard: return "refused: expected bytes not found";
		case outcome::refused_region: return "refused: code region hash differs";
		case outcome::refused_conflict: return "refused: conflicting expectations or writes";
		case outcome::refused_unexpected: return "refused: write outside every guard";
		case outcome::refused_protect: return "refused: page protection";
		case outcome::refused_unresolved: return "refused: a composed site is written by no contribution";
		case outcome::rolled_back: return "rolled back after a write failure";
		case outcome::rollback_failed: return "ROLLBACK FAILED";
		}
		return "?";
	}

	bool transaction::add(contribution c)
	{
		if (sealed_) return false;
		contributions_.push_back(std::move(c));
		return true;
	}

	outcome transaction::commit(const hooks* test_hooks)
	{
		// A second commit is a refusal that changes nothing: the first result, report and every contributor's state stay.
		if (sealed_) return outcome::refused_sealed;
		sealed_ = true;
		auto finish = [&](outcome o, std::string why)
		{
			result_ = o;
			report_ = std::move(why);
			// Certified refusals and rollbacks release the contributors' storage; an uncertified state retains it.
			if (o == outcome::rollback_failed) { for (auto& c : contributions_) if (c.retain) c.retain(); }
			else if (o != outcome::committed) for (auto& c : contributions_) if (c.abandon) c.abandon();
			return o;
		};
		if (contributions_.empty()) return finish(outcome::refused_empty, "no contributions");

		auto protect = [&](void* p, std::size_t n, DWORD np, DWORD* old) -> bool
		{
			if (test_hooks && test_hooks->protect) return test_hooks->protect(p, n, np, old);
			return VirtualProtect(p, n, np, old) != FALSE;
		};
		auto flush = [&](void* p, std::size_t n) -> bool
		{
			if (test_hooks && test_hooks->flush) return test_hooks->flush(p, n);
			return FlushInstructionCache(GetCurrentProcess(), p, n) != FALSE;
		};

		// 1. The expectation union: every guarded byte, agreeing wherever guards overlap; regions by hash.
		std::map<address, std::uint8_t> expected;
		for (const auto& c : contributions_)
		{
			for (const auto& g : c.guards)
			{
				for (std::size_t i = 0; i < g.bytes.size(); ++i)
				{
					const address a = g.va + static_cast<address>(i);
					const auto it = expected.find(a);
					if (it == expected.end()) expected.emplace(a, g.bytes[i]);
					else if (it->second != g.bytes[i])
						return finish(outcome::refused_conflict, c.owner + ": guard '" + g.what + "' disagrees with an earlier expectation at " + hex(static_cast<address>(a)));
				}
			}
		}
		guarded_ = expected.size();
		// 2. Memory must hold every expectation (checked once, as the union) and every region hash.
		std::vector<std::uint8_t> buffer;
		for (const auto& c : contributions_)
		{
			for (const auto& g : c.guards)
			{
				buffer.resize(g.bytes.size());
				if (!raw_read(at(g.va), buffer.data(), buffer.size()) || buffer != g.bytes)
					return finish(outcome::refused_guard, c.owner + ": " + g.what + " (" + hex(g.va) + ")");
			}
			for (const auto& r : c.regions)
			{
				if (r.hi_exclusive <= r.lo) return finish(outcome::refused_region, c.owner + ": empty region " + r.name);
				buffer.resize(r.hi_exclusive - r.lo);
				if (!raw_read(at(r.lo), buffer.data(), buffer.size()) || sha256(buffer.data(), buffer.size()) != r.sha256)
					return finish(outcome::refused_region, c.owner + ": region " + r.name);
			}
		}
		// 3. The write set: agreeing duplicates allowed, every byte inside a guard.
		std::map<address, std::uint8_t> writes;
		for (const auto& c : contributions_)
		{
			for (const auto& w : c.writes)
			{
				if (w.bytes.empty()) return finish(outcome::refused_conflict, c.owner + ": empty write '" + w.what + "'");
				for (std::size_t i = 0; i < w.bytes.size(); ++i)
				{
					const address a = w.va + static_cast<address>(i);
					if (!expected.count(a)) return finish(outcome::refused_unexpected, c.owner + ": '" + w.what + "' writes " + hex(static_cast<address>(a)) + " which no guard covers");
					const auto it = writes.find(a);
					if (it == writes.end()) writes.emplace(a, w.bytes[i]);
					else if (it->second != w.bytes[i]) return finish(outcome::refused_conflict, c.owner + ": '" + w.what + "' conflicts with an earlier write at " + hex(static_cast<address>(a)));
				}
			}
		}
		if (writes.empty()) return finish(outcome::refused_empty, "no bytes to write");
		// 3b. Composed sites: every address a contribution relies on another to write is written by someone.
		std::string unresolved;
		for (const auto& c : contributions_)
			for (const auto& r : c.requires_written)
				if (!writes.count(r.va)) unresolved += (unresolved.empty() ? "" : ", ") + c.owner + " needs " + hex(r.va) + " (" + r.what + ")";
		if (!unresolved.empty()) return finish(outcome::refused_unresolved, "unwritten composed sites: " + unresolved);
		written_ = writes.size();

		// 4. The page ledger: each distinct touched page once, original protection remembered.
		SYSTEM_INFO system{};
		GetSystemInfo(&system);
		const std::uintptr_t page_size = system.dwPageSize;
		std::vector<std::uintptr_t> pages;
		for (const auto& [a, b] : writes) pages.push_back(static_cast<std::uintptr_t>(a) & ~(page_size - 1));
		std::sort(pages.begin(), pages.end());
		pages.erase(std::unique(pages.begin(), pages.end()), pages.end());
		pages_ = pages.size();
		std::vector<DWORD> protection(pages.size());
		for (std::size_t i = 0; i < pages.size(); ++i)
		{
			if (!protect(reinterpret_cast<void*>(pages[i]), page_size, PAGE_EXECUTE_READWRITE, &protection[i]))
			{
				const DWORD error = GetLastError();
				// Undo the pages already acquired and certify it: nothing was written, but a page left writable is not
				// the state the refusal promises, so an unrestored page is reported as uncertified (rollback_failed).
				bool restored = true;
				for (std::size_t j = i; j-- > 0;)
				{
					DWORD ignored = 0;
					restored = protect(reinterpret_cast<void*>(pages[j]), page_size, protection[j], &ignored) && restored;
					MEMORY_BASIC_INFORMATION info{};
					restored = VirtualQuery(reinterpret_cast<void*>(pages[j]), &info, sizeof info) == sizeof info && info.Protect == protection[j] && restored;
				}
				const std::string why = "page " + hex(static_cast<address>(pages[i])) + " could not be unprotected (error " + std::to_string(error) + ")";
				if (!restored) return finish(outcome::rollback_failed, why + "; restoring the pages acquired before it failed");
				return finish(outcome::refused_protect, why);
			}
		}
		auto restore_all = [&]() -> bool
		{
			bool ok = true;
			for (std::size_t i = pages.size(); i-- > 0;)
			{
				DWORD ignored = 0;
				ok = protect(reinterpret_cast<void*>(pages[i]), page_size, protection[i], &ignored) && ok;
			}
			return ok;
		};
		// 5. Apply, flush, verify.
		std::string failure;
		for (const auto& [a, b] : writes)
		{
			if (!raw_write(at(a), &b, 1)) { failure = "write faulted at " + hex(static_cast<address>(a)); break; }
		}
		if (test_hooks && test_hooks->after_apply) test_hooks->after_apply();
		if (failure.empty())
			for (const auto page : pages)
				if (!flush(reinterpret_cast<void*>(page), page_size)) { failure = "instruction cache flush failed at page " + hex(static_cast<address>(page)); break; }
		if (failure.empty())
			for (const auto& [a, b] : writes)
			{
				std::uint8_t now = 0;
				if (!raw_read(at(a), &now, 1) || now != b) { failure = "readback differs at " + hex(static_cast<address>(a)); break; }
			}
		if (failure.empty() && !restore_all()) failure = "page protection could not be restored";
		if (failure.empty())
		{
			result_ = outcome::committed;
			report_ = std::to_string(contributions_.size()) + " contributions, " + std::to_string(writes.size()) + " bytes at " + std::to_string(pages.size()) + " page(s), " +
			          std::to_string(expected.size()) + " guarded bytes";
			for (auto& c : contributions_) if (c.publish) c.publish();
			return result_;
		}
		// 6. Roll back to the expectation, verify, restore protections (pages may already be restored: re-acquire).
		for (std::size_t i = 0; i < pages.size(); ++i)
		{
			DWORD ignored = 0;
			protect(reinterpret_cast<void*>(pages[i]), page_size, PAGE_EXECUTE_READWRITE, &ignored);
		}
		bool rolled = true;
		for (const auto& [a, b] : writes)
		{
			const std::uint8_t old = expected.at(a);
			rolled = raw_write(at(a), &old, 1) && rolled;
		}
		if (test_hooks && test_hooks->after_rollback) test_hooks->after_rollback();
		for (const auto page : pages) rolled = flush(reinterpret_cast<void*>(page), page_size) && rolled;
		for (const auto& [a, b] : writes)
		{
			std::uint8_t now = 0;
			rolled = raw_read(at(a), &now, 1) && now == expected.at(a) && rolled;
		}
		rolled = restore_all() && rolled;
		return finish(rolled ? outcome::rolled_back : outcome::rollback_failed, failure);
	}
}
