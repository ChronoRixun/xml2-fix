#include "patch_transaction.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>

#ifdef XML2_SAVE_STARTUP_FAULT_TEST
#include "../test/save_startup_faults.hpp" // the owned fault DLL wraps the transaction's Windows calls
#include "../test/w2_startup_faults.hpp"
#undef VirtualProtect
#define VirtualProtect w2_startup_faults::protect
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

		std::uint8_t nibble(char c) { return static_cast<std::uint8_t>(c >= 'a' ? c - 'a' + 10 : c >= 'A' ? c - 'A' + 10 : c - '0'); }
	}

	std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t size) { return sha256_fips::digest(data, size); }

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

	bool verify_expectations(const contribution& c, std::string& why)
	{
		std::vector<std::uint8_t> buffer;
		for (const auto& g : c.guards)
		{
			buffer.resize(g.bytes.size());
			if (!raw_read(at(g.va), buffer.data(), buffer.size()) || buffer != g.bytes) { why = g.what + " (" + hex(g.va) + ")"; return false; }
		}
		for (const auto& r : c.regions)
		{
			if (r.hi_exclusive <= r.lo) { why = "empty region " + r.name; return false; }
			buffer.resize(r.hi_exclusive - r.lo);
			if (!raw_read(at(r.lo), buffer.data(), buffer.size()) || sha256(buffer.data(), buffer.size()) != r.sha256) { why = "region " + r.name + " (" + hex(r.lo) + ")"; return false; }
		}
		return true;
	}

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

		// Allocate optional diagnostic history before acquiring pages or writing.
        std::vector<write> originals, pending_writes;
        if (record_verified_writes) {
            for (const auto& c : contributions_) for (const auto& w : c.writes) {
                write old{w.va, {}, w.what};old.bytes.reserve(w.bytes.size());
                for (std::size_t j=0;j<w.bytes.size();++j)old.bytes.push_back(expected.at(w.va+j));
                originals.push_back(std::move(old));pending_writes.push_back(w);
            }
            verified_originals.reserve(verified_originals.size()+originals.size());
            verified_writes.reserve(verified_writes.size()+originals.size());
        }
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
			if (record_verified_writes) {
                verified_originals.insert(verified_originals.end(),std::make_move_iterator(originals.begin()),std::make_move_iterator(originals.end()));
                verified_writes.insert(verified_writes.end(),std::make_move_iterator(pending_writes.begin()),std::make_move_iterator(pending_writes.end()));
            }
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
