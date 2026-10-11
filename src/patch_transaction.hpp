#pragma once

// One protected code transaction (docs/entity-capacity-phase-a-plan.md section 7): every module that writes
// XMen2.exe for the entity-capacity install contributes its guards, regions, writes and publication callbacks;
// the transaction unions the expectations once, refuses any mismatch, conflict or unexpected byte, acquires each
// touched page once, applies every write, flushes, verifies readback, restores the exact protections and only
// then publishes. Any failure after bytes changed rolls back to the expected bytes and verifies the rollback; a
// rollback that cannot be certified is reported so the caller refuses startup rather than run a mixed layout.
//
// No game knowledge here: addresses are absolute, so the native tests drive it on blocks of their own.

#include <Windows.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace patch_transaction
{
	using address = std::uint32_t;

	struct expectation { address va; std::vector<std::uint8_t> bytes; std::string what; };
	struct region { std::string name; address lo = 0, hi_exclusive = 0; std::array<std::uint8_t, 32> sha256{}; };
	struct write { address va; std::vector<std::uint8_t> bytes; std::string what; };

	struct contribution
	{
		std::string owner;
		std::vector<expectation> guards;
		std::vector<region> regions;
		std::vector<write> writes;
		std::function<void()> publish; // runs after the verified commit, in contribution order
		std::function<void()> abandon; // runs when the transaction refuses or rolls back with every byte certified
		// Runs instead of `abandon` when the transaction cannot certify memory (rollback_failed): written bytes or page
		// protections may still reference the contributor's storage, so it must record the failure WITHOUT releasing
		// anything (stubs, backing, thunks stay allocated until the process exits).
		std::function<void()> retain;
		// Addresses this contribution relies on another contribution to write in the same transaction (composed sites):
		// a required address no contribution writes refuses the whole transaction (refused_unresolved), naming each.
		struct requirement { address va; std::string what; };
		std::vector<requirement> requires_written;
	};

	// Test seams: every Windows call the commit makes, and a point after the writes where a test may corrupt
	// memory to force a readback failure. Production passes nullptr.
	struct hooks
	{
		std::function<bool(void*, std::size_t, DWORD, DWORD*)> protect;
		std::function<bool(void*, std::size_t)> flush;
		std::function<void()> after_apply;      // called once after all bytes are written, before readback
		std::function<void()> after_rollback;   // called once after rollback bytes are written, before its readback
	};

	enum class outcome
	{
		committed,
		refused_empty,
		refused_sealed,        // add() after commit, or commit() twice
		refused_guard,         // a guard's bytes are not in memory
		refused_region,        // a region hash differs
		refused_conflict,      // two guards or two writes disagree about a byte
		refused_unexpected,    // a write touches a byte no guard describes
		refused_protect,       // a page could not be made writable (nothing written, every protection restored and certified)
		refused_unresolved,    // a contribution requires a write at an address no contribution writes
		rolled_back,           // bytes were written, a later step failed, the expected bytes are back and verified
		rollback_failed,       // bytes were written, a later step failed, and the rollback could not be certified
	};
	const char* name(outcome);

	class transaction
	{
	public:
		bool add(contribution c);
		outcome commit(const hooks* test_hooks = nullptr);
		outcome result() const { return result_; }
		const std::string& report() const { return report_; } // the first failure, or a one-line summary
		std::size_t contributions() const { return contributions_.size(); }
		std::size_t pages() const { return pages_; }
		std::size_t written_bytes() const { return written_; }
		std::size_t guarded_bytes() const { return guarded_; }

	private:
		std::vector<contribution> contributions_;
		bool sealed_ = false;
		outcome result_ = outcome::refused_empty;
		std::string report_;
		std::size_t pages_ = 0, written_ = 0, guarded_ = 0;
	};

	// "0x" + lower-case hexadecimal, for reports.
	std::string hex(address va);
	// Byte helpers shared with the contributors.
	std::vector<std::uint8_t> bytes_of(std::string_view hex);
	std::array<std::uint8_t, 32> sha256_of(std::string_view hex);
	std::array<std::uint8_t, 32> sha256(const std::uint8_t* data, std::size_t size);
	// E9/E8 rel32 (or any prefix) + rel32 + `pad` NOPs, and a raw abs32 dword, as writes.
	write relocation(address va, std::string_view prefix_hex, bool relative, address target, unsigned pad, std::string what);
	// Guarded memory reads for the contributors' own checks (false when the address faults).
	bool read_memory(address va, void* out, std::size_t size);
}
