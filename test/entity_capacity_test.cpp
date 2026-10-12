// The entity-capacity install's game-free decisions: the generated ledger's shape, symbol binding and write
// lengths, the allocator ring rebuild, the save-manager claim state machine, the native campaign transfer bounds,
// startup quiescence and the ownership split of composed sites. No game, no game content.
#include "entity_capacity_layout.hpp"
#include "entity_capacity_rules.hpp"
#include "entity_capacity_sites.hpp"
#include "gameplay_agreement_rules.hpp"
#include "patch_transaction.hpp"

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

using namespace entity_capacity_layout;
using namespace entity_capacity_rules;
using namespace entity_capacity_sites;
namespace
{
	int failures = 0;
	void check(bool ok, const char* what) { std::printf("%s %s\n", ok ? "PASS" : "FAIL", what); failures += !ok; }
}

int main()
{
	// ---- the ledger header
	check(literal_sites.size() == 223 && relocation_sites.size() == 121 && expected_ranges.size() == 341 && region_hashes.size() == 31 && owner_slots.size() == 83,
	      "ledger counts: 223 literal + 121 relocation sites, 341 ranges, 31 regions, 83 owner slots");
	bool same_length = true;
	for (const auto& s : literal_sites) same_length = same_length && s.old_hex.size() == s.new_hex.size() && s.old_hex.size() % 2 == 0 && !s.old_hex.empty();
	check(same_length, "every literal site replaces exactly its old bytes");
	bool reloc_ok = true;
	for (const auto& s : relocation_sites) reloc_ok = reloc_ok && s.old_hex.size() / 2 == s.prefix_hex.size() / 2 + 4 + s.pad;
	check(reloc_ok, "every relocation site is prefix + 4 + pad bytes long");
	{ // every site lies inside the expectation union and no two sites overlap
		std::map<std::uint32_t, std::uint8_t> expected;
		for (const auto& r : expected_ranges) for (std::size_t i = 0; i < r.hex.size() / 2; ++i) expected[r.va + static_cast<std::uint32_t>(i)] = 1;
		std::set<std::uint32_t> written;
		bool covered = true, disjoint = true;
		auto visit = [&](std::uint32_t va, std::size_t n) { for (std::size_t i = 0; i < n; ++i) { covered = covered && expected.count(va + static_cast<std::uint32_t>(i)); disjoint = disjoint && written.insert(va + static_cast<std::uint32_t>(i)).second; } };
		for (const auto& s : literal_sites) visit(s.va, s.old_hex.size() / 2);
		for (const auto& s : relocation_sites) visit(s.va, s.old_hex.size() / 2);
		check(covered, "every written byte is covered by an expected range");
		check(disjoint, "no two sites overlap");
		check(expected.size() == guarded_bytes, "the union covers exactly the ledger's guarded byte count");
	}
	{ // binding: a fake symbol table resolves every symbol the install owns; composed symbols are skipped
		std::map<std::string_view, std::uint32_t> table;
		for (std::size_t i = 0; i < symbol_count; ++i) table[symbol_names[i]] = 0x20000000 + static_cast<std::uint32_t>(i) * 0x100;
		std::size_t skipped = 0, bound = 0;
		bool lengths = true;
		for (const auto& s : relocation_sites)
		{
			const auto name = symbol_names[static_cast<std::size_t>(s.target)];
			if (owned_elsewhere(name)) { ++skipped; continue; }
			const auto w = patch_transaction::relocation(s.va, s.prefix_hex, s.kind == relocation_kind::rel32, table[name], s.pad, "x");
			lengths = lengths && w.bytes.size() == s.old_hex.size() / 2;
			if (s.kind == relocation_kind::rel32)
			{
				std::uint32_t rel = 0;
				std::memcpy(&rel, w.bytes.data() + s.prefix_hex.size() / 2, 4);
				lengths = lengths && rel == table[name] - (s.va + static_cast<std::uint32_t>(s.prefix_hex.size() / 2) + 4);
			}
			++bound;
		}
		check(lengths, "every bound relocation has the old length and a rel32 from the end of its displacement");
		check(skipped == 68 && bound == 53, "68 composed sites (save seam, agreement) are the other modules'; 53 are the install's");
		std::printf("  bound %zu skipped %zu\n", bound, skipped);
	}
	// ---- allocator ring rebuild
	{
		std::vector<std::uint8_t> sub(allocator_layout::sub_active + 4, 0xaa);
		generic_ring_rebuild(sub.data(), raised_generic);
		auto u32 = [&](std::uint32_t at) { std::uint32_t v; std::memcpy(&v, sub.data() + at, 4); return v; };
		bool ring_ok = u32(allocator_layout::sub_enqueue) == 0 && u32(allocator_layout::sub_dequeue) == 0 && u32(allocator_layout::sub_count) == raised_generic && u32(allocator_layout::sub_active) == 0;
		for (std::uint32_t i = 0; i < raised_generic; ++i) ring_ok = ring_ok && u32(allocator_layout::sub_ring + 4 * i) == i;
		for (std::uint32_t w = 0; w < allocator_layout::bitmap_words; ++w) ring_ok = ring_ok && u32(allocator_layout::sub_bitmap + 4 * w) == 0;
		check(ring_ok, "generic ring rebuild: ascending FIFO of 768, cursor 0, count 768, 24 bitmap words and the active count cleared");
		check(u32(allocator_layout::sub_ring - 4) == 0xaaaaaaaa, "ring rebuild touches nothing before the ring");
		std::vector<std::uint8_t> small(allocator_layout::sub_active + 4, 0);
		generic_ring_rebuild(small.data(), 224);
		std::uint32_t v224; std::memcpy(&v224, small.data() + allocator_layout::sub_ring + 4 * 223, 4);
		check(v224 == 223, "the same rebuild at 224 reproduces the native wrap at the last slot");
	}
	// ---- save-manager claim
	{
		check(claim_allocate(claim_state::prepared, e10a2_manager_size, e10a2_manager_size) == claim_state::claimed, "prepared claim of the manager size is claimed");
		check(claim_allocate(claim_state::prepared, 0x1afdc, e10a2_manager_size) == claim_state::poisoned, "a retail-sized request against the expanded claim poisons");
		check(claim_allocate(claim_state::in_use, e10a2_manager_size, e10a2_manager_size) == claim_state::poisoned, "a second allocation while in use poisons (no reuse)");
		check(claim_allocate(claim_state::poisoned, e10a2_manager_size, e10a2_manager_size) == claim_state::poisoned, "a poisoned claim stays poisoned");
		check(claim_allocate(claim_state::prepared, e10a2_manager_size, 0) == claim_state::poisoned, "no descriptor: refused");
		int backing = 0, other = 0;
		check(claim_construct_result(true, &backing, &backing) == claim_state::in_use, "constructor returning the backing: in use");
		check(claim_construct_result(true, &other, &backing) == claim_state::poisoned, "wrong return poisons");
		check(claim_construct_result(true, nullptr, &backing) == claim_state::poisoned, "null return poisons");
		check(claim_construct_result(false, &backing, &backing) == claim_state::poisoned, "exception poisons");
	}
	// ---- native campaign transfer bounds
	{
		const auto p = e10a2_transfer.payload;
		check(transfer_chunk(0, 200, p, true) == transfer_verdict::accept, "first full chunk accepted");
		check(transfer_chunk(0, 201, p, true) == transfer_verdict::too_long, "chunk over 200 refused");
		check(transfer_chunk(0, 200, p, false) == transfer_verdict::no_buffer, "no buffer refused");
		check(transfer_chunk(p / 200, p % 200, p, true) == transfer_verdict::complete, "the exact final chunk completes");
		check(transfer_chunk(p / 200, 200, p, true) == transfer_verdict::overflow, "a full chunk past the payload end refused");
		check(transfer_chunk(5, 100, p, true) == transfer_verdict::short_before_end, "a short chunk before the end refused");
		check(transfer_chunk(0x2fc00 / 200, 0x2fc00 % 200, retail_transfer.payload, true) == transfer_verdict::complete, "retail payload completes at 0x2fc00");
		check(e10a2_transfer.wrapper - e10a2_transfer.payload == retail_transfer.wrapper - retail_transfer.payload, "wrapper keeps the retail trailer size");
	}
	// ---- startup quiescence
	{
		std::map<std::uint32_t, std::uint32_t> mem;
		auto read = [&](std::uint32_t va, std::uint32_t& out) { out = mem[va]; return true; };
		std::string why;
		check(startup_quiescent(read, "lc", 0x400000, why), "LC + stock owners unbuilt: quiescent");
		check(!startup_quiescent(read, "off", 0x400000, why) && why.find("Format=lc") != std::string::npos, "no LC refuses with the reason");
		check(!startup_quiescent(read, "lc", 0x10000000, why), "wrong base refuses");
		mem[allocator_pointer_cell] = 0x12345678;
		check(!startup_quiescent(read, "LC", 0x400000, why) && why.find("allocator") != std::string::npos, "a built allocator refuses");
		mem[allocator_pointer_cell] = 0; mem[0x781ee4] = 1;
		check(!startup_quiescent(read, "lc", 0x400000, why) && why.find("World") != std::string::npos, "a built World refuses");
	}
	// ---- the fallback warning's advice follows the cause (it used to blame the executable every time)
	{
		check(quiescence_cause("off", 0x400000) == fallback_cause::save_format && quiescence_cause("", 0x400000) == fallback_cause::save_format,
		      "no [Save] Format=lc: a configuration cause");
		check(quiescence_cause(" LC", 0x10000000) == fallback_cause::executable, "LC set but the module is not at 0x400000: executable cause");
		check(quiescence_cause("lc", 0x400000) == fallback_cause::load_order, "LC set, retail base, owners already built: load-order cause");
		const auto format = fallback_notice_text("why", fallback_cause::save_format);
		check(format.find("Format=lc") != std::string::npos && format.find("EntityCapacity=0") != std::string::npos && format.find("retail XMen2.exe") == std::string::npos,
		      "a missing Format=lc names the ini line and the opt-out, not the executable");
		const auto changed = fallback_notice_text("why", fallback_cause::changed_code);
		check(changed.find("unmodified retail XMen2.exe") != std::string::npos && changed.find("Format=lc") == std::string::npos, "changed entity code names the executable");
		check(fallback_notice_text("why", fallback_cause::internal).find("report it") != std::string::npos, "an internal failure asks for a report, not a different exe");
		check(fallback_notice_text("why", fallback_cause::load_order).find("loaders") != std::string::npos, "a late load names other loaders");
		bool every = true;
		for (const auto c : {fallback_cause::save_format, fallback_cause::executable, fallback_cause::changed_code, fallback_cause::load_order, fallback_cause::internal})
		{
			const auto text = fallback_notice_text("the reason", c);
			every = every && *fallback_advice(c) && text.find("the reason") != std::string::npos && text.find("xml2-fix.log") != std::string::npos;
		}
		check(every, "every cause has advice; the notice keeps the logged reason and points to the log");
	}
	// ---- layout contract
	{
		check(world_core_for(256).handles == stock_descriptor.world_core.handles && world_core_for(1024).shift + 4 == 0x30a4, "handle core derivation matches retail and the plan");
		check(migrate_typed_field(0x101) == 0x401 && migrate_typed_field(0x501) == 0x1401 && migrate_typed_field(0) == 0 && migrate_typed_field(0x80000001) == 0,
		      "typed migration: retail (g,s) -> contiguous (g,s); null, negative stay null");
		check(migrate_typed_field((0x3fff << 8) | 7) == ((0x3fffu << 10) | 7) && migrate_typed_field((0x4000 << 8) | 7) == 0, "generation 0x3fff migrates, 0x4000 nulls");
		check(raised_tokens.token(0x3fff, {1023}).value == 0x00ffffff, "the largest minted token is 0x00ffffff");
		const std::uint32_t words[32] = {0, 0, 0x100, 0, 0, 0, 0, 0, 0x80000000};
		check(next_set(words, 0, 1024) == 72 && next_set(words, 73, 1024) == 287 && next_set(words, 288, 1024) == 1024 && next_set(words, 0, 256) == 72 && next_set(words, 73, 256) == 256,
		      "next_set walks dwords lowest bit first and stops at the capacity");
		check(extra_guards[0].va == 0x614280 && extra_guards[0].hex == "8d411cc3", "the grid slot-8 body guard is the retail lea/ret");
	}
	{ // composed sites: every gameplay_agreement ledger site is either written by gameplay_agreement::contribute (its
	  // operand address is one of the twelve it writes) or one of the 21 descoped receive-lifecycle sites (deviation 17)
		namespace ga = gameplay_agreement_rules;
		const std::set<std::uint32_t> agreement_writes{ga::request_send + 1, ga::reply_broadcast + 1, ga::reply_send + 1, ga::info_send + 1, ga::ack_send + 1,
		                                               ga::request_registration + 1, ga::reply_registration + 1, ga::info_registration + 1, ga::ack_registration + 1,
		                                               ga::rejection_registration + 1, ga::failure_text + 1, ga::native_join + 1};
		std::size_t written = 0, descoped = 0, neither = 0, descoped_other_owner = 0;
		for (const auto& s : relocation_sites)
		{
			const auto name = symbol_names[static_cast<std::size_t>(s.target)];
			const auto operand = s.va + static_cast<std::uint32_t>(s.prefix_hex.size() / 2);
			if (descoped_receive_site(s.va)) { ++descoped; descoped_other_owner += !name.starts_with("gameplay_agreement::"); continue; }
			if (!name.starts_with("gameplay_agreement::")) continue;
			if (agreement_writes.count(operand)) ++written; else ++neither;
		}
		check(descoped == 21 && descoped_other_owner == 0, "21 receive-lifecycle ledger sites are descoped, all of them gameplay_agreement symbols");
		check(written == 12 && neither == 0, "every other gameplay_agreement ledger site is one of the twelve operands gameplay_agreement writes");
	}
	{ // manager tail coverage (deviation 19). The instruction sites of the retail executable whose operand addresses the
	  // save manager's tail (retail [0x1ad60, 0x1afdc): record count, side frame, flag, time, size), from an instruction-
	  // level sweep of the whole program export (the 0x1aefc-byte object at 0x4f7570..0x500450 excluded: not the save
	  // manager). Each must be rewritten by the install: a ledger site or an extra literal.
		struct tail_site { std::uint32_t va, offset; };
		static constexpr tail_site tail_sites[] = {{0x4657b9, 0x1ad68}, {0x465da7, 0x1ad64}, {0x465ded, 0x1ad64}, {0x465e20, 0x1ad64}, {0x465e2b, 0x1afd8}, {0x465e42, 0x1afd8}, {0x465e51, 0x1afd8}, {0x465e57, 0x1ad64}, {0x465f94, 0x1ad64}, {0x465faf, 0x1ad64}, {0x465fc1, 0x1ad64}, {0x465fc7, 0x1afd4}, {0x465fcd, 0x1afd8}, {0x466039, 0x1ad64}, {0x4660fb, 0x1ad64}, {0x466278, 0x1ad64}, {0x46630e, 0x1ad64}, {0x4663a3, 0x1ad64}, {0x466422, 0x1ad64}, {0x466996, 0x1ad64}, {0x4669ff, 0x1ad64}, {0x466a05, 0x1ad64}, {0x466a1a, 0x1afd8}, {0x466a50, 0x1afd8}, {0x466a60, 0x1ad68}, {0x48438f, 0x1afd4}, {0x48492d, 0x1afd4}, {0x484adf, 0x1ad64}, {0x484b4e, 0x1ad68}, {0x484b91, 0x1ad64}, {0x484bd8, 0x1ad64}, {0x484bdf, 0x1ad64}, {0x484c25, 0x1ad68}, {0x484c4f, 0x1afd4}, {0x484c76, 0x1ae0c}, {0x484cbf, 0x1ad68}, {0x485198, 0x1afd4}, {0x4851af, 0x1ad68}, {0x4862ce, 0x1afd4}, {0x4862d8, 0x1afd4}, {0x4862e5, 0x1ad68}, {0x48733d, 0x1afdc}, {0x487476, 0x1ad64}, {0x48747c, 0x1ad68}, {0x673cdb, 0x1ad68}};
		auto covered = [&](std::uint32_t va) {
			for (const auto& r : relocation_sites) if (va >= r.va && va < r.va + r.old_hex.size() / 2) return true;
			for (const auto& r : literal_sites) if (va >= r.va && va < r.va + r.old_hex.size() / 2) return true;
			for (const auto& r : extra_literals) if (va >= r.va && va < r.va + r.old_hex.size() / 2) return true;
			return false;
		};
		std::string missing;
		for (const auto& t : tail_sites) if (!covered(t.va)) { char b[24]; std::snprintf(b, sizeof b, "0x%x ", t.va); missing += b; }
		std::printf("manager tail: %zu instruction sites, uncovered: %s\n", std::size(tail_sites), missing.empty() ? "none" : missing.c_str());
		check(std::size(tail_sites) == 45 && missing.empty(), "every instruction addressing the save manager's tail is rewritten (ledger or the extra 0x484c76 literal)");
		const auto& x = extra_literals[0];
		check(x.va == 0x484c76 && x.old_hex == "8d84170cae0100" && x.new_hex == "8d8417540a0800" && 0x1ae0cu + 5u * (0x19b88u - 0x55e0u) == 0x80a54u,
		      "the extra literal moves manager + 0x1ae0c to + 0x80a54 (the E10A2 tail shift 0x65c48)");
	}
	std::printf("entity capacity rules: %d failure(s)\n", failures);
	return failures ? 1 : 0;
}
