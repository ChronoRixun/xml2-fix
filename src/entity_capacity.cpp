#include "entity_capacity.hpp"
#include "entity_capacity_layout.hpp"
#include "entity_capacity_rules.hpp"
#include "entity_capacity_sites.hpp"
#include "entity_owners.hpp"
#include "entity_transfer.hpp"
#include "entity_world.hpp"
#include "gameplay_agreement.hpp"
#include "gameplay_capabilities.hpp"
#include "ini.hpp"
#include "limits.hpp"
#include "log.hpp"
#include "patch_transaction.hpp"
#include "save_format.hpp"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>

namespace entity_capacity
{
	using namespace entity_capacity_layout;
	using namespace entity_capacity_rules;

	namespace
	{
		descriptor published = stock_descriptor;
		descriptor prepared_descriptor = stock_descriptor;
		bool requested = false, prepared = false, install_published = false, fatal = false;
		// [Online] FastTransfer: requested-only; both peers must agree (gameplay agreement, docs/online-campaign-transfer.md).
		bool fast_requested = false, fast_installed = false;
		std::string failure;
		fallback_cause failure_cause = fallback_cause::internal; // picks the advice in the fallback warning
		entity_owners::set owners;
		std::uint8_t* allocator_backing = nullptr;  // 0x189218 bytes, zero-filled before every construction
		std::uint8_t* save_backing = nullptr;       // 0x80c24 bytes, the prepared CMap save-manager claim
		claim_state claim = claim_state::unprepared;
		std::atomic<unsigned> claim_rejections{0}, factory_failures{0}, transfer_refusals{0};

		std::uint32_t address_of(const void* p) { return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(p)); }
		bool read_dword(std::uint32_t va, std::uint32_t& out) { return patch_transaction::read_memory(va, &out, 4); }

		[[noreturn]] void fail_stop(const char* what)
		{
			logger::write("entity capacity: FATAL: %s; the process stops rather than run a half-initialised save manager", what);
			MessageBoxA(nullptr, (std::string("X-Men Legends II could not initialise its save manager under the entity-capacity install:\n") + what +
			                      "\n\nThe game stops here instead of continuing with an invalid layout. See xml2-fix.log.").c_str(),
			            "XML2 Fix: entity capacity", MB_OK | MB_ICONERROR);
			RaiseFailFastException(nullptr, nullptr, 0);
			TerminateProcess(GetCurrentProcess(), 3);
			for (;;) {}
		}

		// ---- allocator (retail 0x461910 / 0x4613e0 and the two full-bitmap-clear helpers) ----------------------
		namespace natives
		{
			using ctor_t = void*(__fastcall*)(void*, void*);
			using method_t = void(__fastcall*)(void*, void*);
			using dtor_t = void*(__fastcall*)(void*, void*, std::uint8_t);
			using construct_manager_t = void*(__fastcall*)(void*, void*);
			using free_t = void(__cdecl*)(void*, std::uint32_t);
			using pool_alloc_t = void*(__cdecl*)(std::uint32_t, std::uint32_t);
			inline ctor_t allocator_ctor = native<ctor_t>(0x4614f0);
			inline construct_manager_t construct_manager = native<construct_manager_t>(0x487440);
			inline free_t pool_free = native<free_t>(0x5606a0);
			inline pool_alloc_t transfer_alloc = native<pool_alloc_t>(0x5606e0);
			inline std::uint32_t* allocator_cell = reinterpret_cast<std::uint32_t*>(allocator_pointer_cell);
		}

		std::uint8_t* generic_subobject(std::uint8_t* allocator) { return allocator + allocator_layout::generic_base; }

		// The whole-bitmap clear plus ring rebuild the native constructor (0x46155c) and empty reset (0x4618d6) jump
		// to in place of their 224-slot ring rebuild 0x461480: every bitmap word of the 768-slot arena, the active
		// count, then the ascending FIFO exactly as 0x461480 builds it.
		void __fastcall GenericRebuildClear(void* generic, void*)
		{
			generic_ring_rebuild(static_cast<std::uint8_t*>(generic), raised_generic);
		}
		void __fastcall GenericResetClear(void* generic, void*)
		{
			generic_ring_rebuild(static_cast<std::uint8_t*>(generic), raised_generic);
		}

		// 0x461910: the singleton getter. The game's heap allocation (0x7e6d4 from pool 0xe) is replaced by the
		// prepared, zero-filled backing; the native constructor runs on it with the ledger's operand edits in place.
		void* __cdecl AllocatorGet()
		{
			if (*natives::allocator_cell) return reinterpret_cast<void*>(static_cast<std::uintptr_t>(*natives::allocator_cell));
			if (!allocator_backing) fail_stop("allocator backing missing at first use");
			std::memset(allocator_backing, 0, allocator_layout::object_size); // game_allocator zero-fills (0x560627)
			natives::allocator_ctor(allocator_backing, nullptr);
			*natives::allocator_cell = address_of(allocator_backing);
			auto** vt = *reinterpret_cast<void***>(allocator_backing);
			reinterpret_cast<natives::method_t>(vt[1])(allocator_backing, nullptr); // vt+4 (0x48ef90, a bare RET)
			return allocator_backing;
		}

		// 0x4613e0: reset/destruction once, clear the singleton cell, retain the backing (never the native type-14
		// free), then the MoveTriggers timer retirement that the release wrapped around this function (section 0, P1).
		void __cdecl AllocatorShutdown()
		{
			if (const auto p = *natives::allocator_cell)
			{
				auto* object = reinterpret_cast<std::uint8_t*>(static_cast<std::uintptr_t>(p));
				auto** vt = *reinterpret_cast<void***>(object);
				reinterpret_cast<natives::method_t>(vt[2])(object, nullptr);          // vt+8 (0x48ef90)
				reinterpret_cast<natives::dtor_t>(vt[0])(object, nullptr, 0);         // vt+0 (0x4618e0, flag 0: no delete)
				*natives::allocator_cell = 0;
			}
			limits::retire_move_trigger_times();
		}

		// ---- the prepared save-manager claim (CMap 0x487320: allocation 0x487344, construction 0x487362,
		//      normal release 0x4876c0, unwind 0x673cb8) ---------------------------------------------------------
		void* __cdecl AllocatePreparedSaveManager(std::uint32_t size, std::uint32_t pool, std::uint32_t tag)
		{
			(void)pool; (void)tag;
			const auto decision = claim_allocate(claim, size, prepared_descriptor.raised ? e10a2_manager_size : 0);
			if (decision != claim_state::claimed)
			{
				claim_rejections.fetch_add(1);
				fail_stop(claim == claim_state::poisoned ? "the save-manager claim is poisoned" : "the save-manager claim was requested in an unexpected state or size");
			}
			claim = claim_state::claimed;
			std::memset(save_backing, 0, e10a2_manager_size);
			return save_backing;
		}

		bool run_native_constructor(void* backing, void*& result)
		{
			__try { result = natives::construct_manager(backing, nullptr); return true; }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}

		void* __fastcall ConstructPreparedSaveManager(void* backing, void*)
		{
			if (claim != claim_state::claimed || backing != save_backing)
			{
				claim = claim_state::poisoned;
				fail_stop("save-manager construction requested on an unclaimed or foreign backing");
			}
			claim = claim_state::constructing;
			void* result = nullptr;
			const bool returned = run_native_constructor(backing, result);
			claim = claim_construct_result(returned, result, save_backing);
			if (claim != claim_state::in_use) fail_stop(returned ? "the native save-manager constructor returned a different object" : "the native save-manager constructor raised an exception");
			return result;
		}

		void __cdecl ReleasePreparedSaveManager(void* object, std::uint32_t pool)
		{
			if (object == save_backing && claim == claim_state::in_use)
			{
				claim = claim_state::prepared;
				return;
			}
			if (object == save_backing) { claim = claim_state::poisoned; logger::write("entity capacity: save-manager release in state %u; claim poisoned", unsigned(claim)); return; }
			natives::pool_free(object, pool); // not ours: the native disposition
		}

		void __cdecl PoisonPreparedSaveClaim(std::uint32_t a, std::uint32_t b)
		{
			if (a == address_of(save_backing) || b == address_of(save_backing))
			{
				claim = claim_state::poisoned;
				logger::write("entity capacity: save-manager construction unwound; claim poisoned, backing retained until exit");
			}
		}

		// ---- 0x465d05: a required dynamic factory returned null during reconstruction --------------------------
		void __cdecl latch_factory_failure()
		{
			factory_failures.fetch_add(1);
			save_format::latch_reconstruction_failure("a dynamic entity factory returned null during reconstruction");
		}
		__declspec(naked) void RestoreFactoryResult()
		{
			__asm
			{
				test edi, edi
				jz latch
				mov edx, dword ptr [edi]
				push 0x465d0b
				ret
			latch:
				pushfd
				pushad
				call latch_factory_failure
				popad
				popfd
				push 0x465d43
				ret
			}
		}

		// ---- 0x608630: the joiner's native campaign transfer receiver ('G' prepares, 'H' appends chunks) ------
		// The receiver and its 'J' acknowledgement live in entity_transfer.cpp (executed next to the retail function by
		// test/entity_transfer_test.cpp); this adapter supplies the installed profile and the telemetry.
		entity_transfer::state transfer_state;
		void transfer_refused(transfer_verdict verdict, std::uint32_t chunk, std::uint32_t length, std::uint32_t payload)
		{
			transfer_refusals.fetch_add(1);
			logger::write_once("entity-capacity-transfer", "entity capacity: native campaign transfer chunk refused (%s: chunk %lu, %u bytes, payload 0x%lX)",
			                   transfer_verdict_text(verdict), static_cast<unsigned long>(chunk), unsigned(length), static_cast<unsigned long>(payload));
		}
		void transfer_no_wrapper(std::uint32_t size)
		{
			logger::write("entity capacity: native campaign transfer: the game's pool refused a %lu-byte wrapper", static_cast<unsigned long>(size));
		}
		void transfer_backpressure(std::uint16_t expected, std::uint16_t sequence, message_pools used)
		{
			logger::write_once("native-reliable-backpressure", "entity capacity: reliable receive deferred before ACK (expected %u, sequence %u, large bodies %u/50, headers %u/768, small bodies %u/768, nodes %u/650); sender will retry",
			                  unsigned(expected), unsigned(sequence), used.large, used.headers, used.small, used.nodes);
		}
		void transport_failed(const char* reason, message_pools used)
		{
			logger::write("online: shared message pool pressure: %s (large %u/50, headers %u/768, small %u/768, nodes %u/650); ending this session safely",
			              reason, used.large, used.headers, used.small, used.nodes);
		}
		void fast_record_refused(fast_verdict verdict, std::uint32_t received, std::uint32_t length, std::uint32_t payload)
		{
			transfer_refusals.fetch_add(1);
			logger::write_once("entity-capacity-fast-transfer", "online: FastTransfer campaign record refused (%s at image byte %lu, %u bytes, payload 0x%lX)",
			                   fast_verdict_text(verdict), static_cast<unsigned long>(received), unsigned(length), static_cast<unsigned long>(payload));
		}
		std::uint32_t __cdecl ReceiveNativeCampaign(std::uint8_t* event)
		{
			static const entity_transfer::observer observer{&transfer_refused, &transfer_no_wrapper, &fast_record_refused};
			return entity_transfer::receive(event, prepared_descriptor.raised ? e10a2_transfer : retail_transfer, transfer_state, observer);
		}

		// ---- refusal telemetry: the World reports every refusal; the line carries the arena occupancies ------------
		std::uint32_t arena_active(bool actor)
		{
			if (!allocator_backing || !*natives::allocator_cell) return 0;
			std::uint32_t v = 0;
			std::memcpy(&v, actor ? allocator_backing + allocator_layout::actor_base + 0x10998 : generic_subobject(allocator_backing) + allocator_layout::sub_active, 4);
			return v;
		}
		void log_world_refusal(const entity_world::refusal& r)
		{
			using K = entity_world::refusal_kind;
			const char* kind = r.kind == K::allocation_full ? "world free ring empty" : r.kind == K::allocation_during_reset ? "allocation during reset" :
			                   r.kind == K::allocator_null ? "allocator arena full" : r.kind == K::reentrant_release ? "reentrant release" :
			                   r.kind == K::name_map_full ? "name map full" : r.kind == K::name_nodes_exhausted ? "name nodes exhausted" :
			                   r.kind == K::hierarchy_full ? "hierarchy full" : r.kind == K::relation_table_index ? "relation table index" : "delete flag";
			if (r.kind == K::allocation_full || r.kind == K::allocation_during_reset || r.kind == K::allocator_null)
				logger::write("entity allocation refused: size=%lu; actor=%lu/%u; generic=%lu/%u; world=%lu/%u (free %lu, retired %lu); reason=%s; refusals=%lu",
				              static_cast<unsigned long>(r.size), static_cast<unsigned long>(arena_active(true)), published.actors, static_cast<unsigned long>(arena_active(false)), published.generic,
				              static_cast<unsigned long>(r.world.live), published.world, static_cast<unsigned long>(r.world.free_slots), static_cast<unsigned long>(r.world.retired), kind,
				              static_cast<unsigned long>(r.world.refusals));
			else
				logger::write_once(std::string("entity-world-refusal-") + kind, "entity world refused: %s (token 0x%lx); world live %lu", kind, static_cast<unsigned long>(r.token), static_cast<unsigned long>(r.world.live));
		}

		// ---- the three owner getters the ledger replaces (0x4c7f20, 0x465470, 0x4ab550): cdecl, no arguments -----
		void* __cdecl WorldGet() { return owners.world; }
		void* __cdecl GridGet() { return owners.grid; }
		void* __cdecl QueryManagerGet() { return owners.query_manager; }

		// ---- the ledger bound to this DLL -----------------------------------------------------------------------
		std::map<std::string_view, std::uint32_t> symbols;
		void bind(std::string_view name, std::uint32_t address) { symbols[name] = address; }

		void bind_own_symbols()
		{
			bind("entity_capacity::GenericRebuildClear", address_of(reinterpret_cast<const void*>(&GenericRebuildClear)));
			bind("entity_capacity::GenericResetClear", address_of(reinterpret_cast<const void*>(&GenericResetClear)));
			bind("entity_capacity::AllocatorGet", address_of(reinterpret_cast<const void*>(&AllocatorGet)));
			bind("entity_capacity::AllocatorShutdown", address_of(reinterpret_cast<const void*>(&AllocatorShutdown)));
			bind("entity_capacity::AllocatePreparedSaveManager", address_of(reinterpret_cast<const void*>(&AllocatePreparedSaveManager)));
			bind("entity_capacity::ConstructPreparedSaveManager", address_of(reinterpret_cast<const void*>(&ConstructPreparedSaveManager)));
			bind("entity_capacity::ReleasePreparedSaveManager", address_of(reinterpret_cast<const void*>(&ReleasePreparedSaveManager)));
			bind("entity_capacity::PoisonPreparedSaveClaim", address_of(reinterpret_cast<const void*>(&PoisonPreparedSaveClaim)));
			bind("entity_capacity::RestoreFactoryResult", address_of(reinterpret_cast<const void*>(&RestoreFactoryResult)));
			bind("entity_capacity::ReceiveNativeCampaign", address_of(reinterpret_cast<const void*>(&ReceiveNativeCampaign)));
			bind("entity_capacity::ReliableReceiveRoom", address_of(reinterpret_cast<const void*>(&entity_transfer::reliable_receive_room)));
			bind("entity_capacity::OutgoingRoom", address_of(reinterpret_cast<const void*>(&entity_transfer::outgoing_room)));
			bind("entity_capacity::TransportMaintenance", address_of(reinterpret_cast<const void*>(&entity_transfer::transport_maintenance)));
			bind("entity_capacity::FastSender", address_of(reinterpret_cast<const void*>(&entity_transfer::fast_sender)));
			bind("entity_capacity::WorldGet", address_of(reinterpret_cast<const void*>(&WorldGet)));
			bind("entity_capacity::GridGet", address_of(reinterpret_cast<const void*>(&GridGet)));
			bind("entity_capacity::QueryManagerGet", address_of(reinterpret_cast<const void*>(&QueryManagerGet)));
			entity_owners::bind_symbols(bind);
		}

		void release_prepared()
		{
			entity_owners::destroy(owners);
			if (allocator_backing) { VirtualFree(allocator_backing, 0, MEM_RELEASE); allocator_backing = nullptr; }
			if (save_backing) { VirtualFree(save_backing, 0, MEM_RELEASE); save_backing = nullptr; }
			claim = claim_state::unprepared;
			prepared = false;
		}
	}

	namespace
	{
		// The whole capacity contribution, built and checked at preparation (before save_format chooses its native
		// profile): bound symbols, guards, region hashes, literal and relocation writes, composed requirements.
		patch_transaction::contribution built;
		bool built_ok = false;
		bool build_contribution(patch_transaction::contribution& out)
		{
			bind_own_symbols();
			patch_transaction::contribution& c = out;
			c.owner = "entity_capacity";
			using namespace entity_capacity_sites;
			for (const auto& r : expected_ranges) c.guards.push_back({r.va, patch_transaction::bytes_of(r.hex), "ledger expectation"});
			for (const auto& s : owner_slots) c.guards.push_back({s.slot_va, patch_transaction::bytes_of(s.hex), std::string(s.owner) + " native vtable slot"});
			for (const auto& g : extra_guards) c.guards.push_back({g.va, patch_transaction::bytes_of(g.hex), g.what});
			for (const auto& r : region_hashes) c.regions.push_back({std::string(r.name), r.lo, r.hi_exclusive, patch_transaction::sha256_of(r.sha256)});
			for (const auto& r : extra_regions) c.regions.push_back({r.name, r.lo, r.hi_exclusive, patch_transaction::sha256_of(r.sha256)});
			for (const auto& s : literal_sites) c.writes.push_back({s.va, patch_transaction::bytes_of(s.new_hex), std::string(s.role)});
			for (const auto& s : extra_literals)
			{
				c.guards.push_back({s.va, patch_transaction::bytes_of(s.old_hex), std::string("extra literal: ") + s.role});
				c.writes.push_back({s.va, patch_transaction::bytes_of(s.new_hex), s.role});
			}
			std::string unresolved;
			std::size_t descoped = 0;
			for (const auto& s : relocation_sites)
			{
				const auto name = symbol_names[static_cast<std::size_t>(s.target)];
				if (owned_elsewhere(name))
				{
					// save_format / gameplay_agreement write these themselves, in this transaction: required, so a composed site
					// nobody writes refuses the install. The section-5 receive-lifecycle sites are descoped (deviation 17).
					if (descoped_receive_site(s.va)) { ++descoped; continue; }
					c.requires_written.push_back({s.va + static_cast<std::uint32_t>(s.prefix_hex.size() / 2), std::string(name)});
					continue;
				}
				const auto found = symbols.find(name);
				if (found == symbols.end()) { unresolved += std::string(name) + " "; continue; }
				c.writes.push_back(patch_transaction::relocation(s.va, s.prefix_hex, s.kind == relocation_kind::rel32, found->second, s.pad, std::string(s.role)));
			}
			for (const auto& s : extra_relocations)
			{
				const auto found = symbols.find(s.symbol);
				if (found == symbols.end()) { unresolved += std::string(s.symbol) + " "; continue; }
				c.writes.push_back(patch_transaction::relocation(s.va, s.prefix_hex, true, found->second, s.pad, s.role));
			}
			if (fast_requested)
			{
				for (const auto& r : fast_regions) c.regions.push_back({r.name, r.lo, r.hi_exclusive, patch_transaction::sha256_of(r.sha256)});
				for (const auto& l : fast_literals)
				{
					c.guards.push_back({l.va, patch_transaction::bytes_of(l.old_hex), std::string("FastTransfer: ") + l.role});
					c.writes.push_back({l.va, patch_transaction::bytes_of(l.new_hex), l.role});
				}
				for (const auto& r : fast_relocations)
				{
					c.guards.push_back({r.va, patch_transaction::bytes_of(r.old_hex), std::string("FastTransfer: ") + r.role});
					const auto found = symbols.find(r.symbol);
					if (found == symbols.end()) { unresolved += std::string(r.symbol) + " "; continue; }
					c.writes.push_back(patch_transaction::relocation(r.va, r.prefix_hex, true, found->second, r.pad, r.role));
				}
			}
			if (!unresolved.empty())
			{
				failure = "unresolved DLL symbols: " + unresolved;
				return false;
			}
			const auto required = c.requires_written.size();
			c.publish = [descoped, required]
			{
				logger::write("entity capacity: %zu composed ledger sites written by save_format/gameplay_agreement (verified in this transaction); %zu receive-lifecycle sites of plan section 5 descoped (deviation 17: admission refusal + process-fixed layout)",
				              required, descoped);
				published = prepared_descriptor;
				install_published = true;
				entity_transfer::observe_backpressure(&transfer_backpressure);
				entity_transfer::observe_failure(&transport_failed);
				logger::write("entity capacity: shared message pools reserve receive capacity; sends preflight fragments; pressure stalls end safely after 30 seconds");
				gameplay_agreement::register_entity_layout(layout_revision_raised, raised_actors, raised_generic, raised_world, raised_index_bits, true);
				if (fast_requested)
				{
					fast_installed = true;
					transfer_state.fast = true;
					entity_transfer::set_fast_payload(prepared_descriptor.raised ? e10a2_transfer.payload : retail_transfer.payload);
					if constexpr (gameplay_capabilities::fast_transfer_id == 0)
						gameplay_agreement::reject_unregistered_rule("FastTransfer");
					else
						gameplay_agreement::register_rule(gameplay_capabilities::fast_transfer_id, gameplay_capabilities::fast_transfer_revision,
						                                  {1u, fast_window, fast_data_max + 1, fast_zero_min}, true);
					logger::write("online: FastTransfer installed - campaign image as data/zero-run records, %u in flight per peer within shared-pool margins, peer budget %u bytes/s; every PC must use it",
					              unsigned(fast_window), unsigned(fast_peer_budget));
				}
				logger::write("entity capacity: installed - actor %u / generic %u / world %u, %zu literal edits, %zu relocations, GCAP layout rule 1 revision %u",
				              published.actors, published.generic, published.world, literal_sites.size(), relocation_sites.size(), unsigned(layout_revision_raised));
			};
			c.abandon = []
			{
				if (failure.empty()) failure = "the startup transaction did not commit";
			};
			c.retain = []
			{
				failure = "the startup transaction could not be rolled back; prepared owners and backing retained";
				fatal = true;
			};
			return true;
		}
	}

	void prepare(patch_transaction::transaction&, HMODULE game)
	{
		requested = ini::flag(L"Limits", L"EntityCapacity", false);
		fast_requested = requested && ini::flag(L"Online", L"FastTransfer", false);
		if (!requested)
		{
			if (ini::flag(L"Online", L"FastTransfer", false))
				logger::write("online: [Online] FastTransfer needs [Limits] EntityCapacity=1 - the stock campaign transfer is used");
			return;
		}
		std::string why;
		const auto lc = ini::text(L"Save", L"Format").value_or("");
		if (!startup_quiescent(read_dword, lc, address_of(game), why))
		{
			failure = why;
			failure_cause = quiescence_cause(lc, address_of(game));
			logger::write("entity capacity: requested but refused before preparation: %s", why.c_str());
			return;
		}
		allocator_backing = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, allocator_layout::object_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		save_backing = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, e10a2_manager_size, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
		if (!allocator_backing || !save_backing)
		{
			failure = "cannot reserve the allocator/save-manager backing";
			release_prepared();
			return;
		}
		logger::write("entity capacity: preparing owners (allocator backing %p, save manager backing %p)", allocator_backing, save_backing);
		if (!entity_owners::create(raised_descriptor_for(0, 0, 0, address_of(allocator_backing)), owners, why))
		{
			failure = why;
			logger::write("entity capacity: requested but the owners could not be prepared: %s", why.c_str());
			release_prepared();
			return;
		}
		prepared_descriptor = raised_descriptor_for(address_of(owners.world), address_of(owners.grid), address_of(owners.query_manager), address_of(allocator_backing));
		entity_world::set_refusal_logger(&log_world_refusal);
		claim = claim_state::prepared;
		prepared = true;
		// Build the whole contribution now and check its guards and region hashes against the executable (read-only):
		// a capacity-only mismatch falls back to stock HERE, before save_format chooses the E10A2 native profile, so
		// the LC seam and the rest of the startup transaction still install with retail layouts (plan section 8 step 6;
		// review P2/P3). Nothing later can leave save_format on E10A2 with stock owners.
		built = {};
		if (!build_contribution(built) || !patch_transaction::verify_expectations(built, why))
		{
			if (failure.empty())
			{
				failure = "the executable's entity code differs from the ledger: " + why;
				failure_cause = fallback_cause::changed_code;
			}
			logger::write("entity capacity: %s - EntityCapacity falls back to stock capacities (online admission poisoned); the rest of the fix installs normally", failure.c_str());
			built = {};
			release_prepared();
			return;
		}
		built_ok = true;
		save_format::use_native_profile(save_format_rules::e10a2_profile);
		logger::write("entity capacity: prepared actor %u / generic %u / world %u (ten-bit indices, generations 1..0x%X); allocator %lu bytes, World %p, Grid %p, query manager %p, save manager %lu bytes",
		              prepared_descriptor.actors, prepared_descriptor.generic, prepared_descriptor.world, generation_max, static_cast<unsigned long>(allocator_layout::object_size),
		              owners.world, owners.grid, owners.query_manager, static_cast<unsigned long>(e10a2_manager_size));
	}

	bool owns_arena_teardown() { return prepared; }

	void contribute(patch_transaction::transaction& transaction, HMODULE)
	{
		if (!prepared || !built_ok) return;
		built_ok = false;
		// save_format already chose the E10A2 profile at preparation: failing to join the transaction now would leave a
		// mixed layout, so it is fatal (startup refused), never a silent stock fallback.
		if (!transaction.add(std::move(built))) { failure = "the startup transaction was sealed before the capacity install joined it"; fatal = true; }
	}

	void after_commit(const bool committed, const bool rollback_failed)
	{
		(void)committed;
		if (rollback_failed) { fatal = true; failure = "the startup transaction could not be rolled back; a mixed layout is refused"; }
		if (requested && !install_published)
		{
			// Requested but not installed: online is poisoned even though the engine is physically stock (plan section 7).
			gameplay_agreement::register_entity_layout(layout_revision_raised, raised_actors, raised_generic, raised_world, raised_index_bits, false);
			logger::write("entity capacity: requested but not installed (%s); online admission poisoned, stock capacities", failure.c_str());
			if (!rollback_failed) release_prepared(); // an uncertified rollback may still reference the prepared owners
		}
	}

	bool startup_allowed() { return !fatal; }
	const std::string& startup_error() { return failure; }

	std::string fallback_notice()
	{
		if (!requested || install_published || fatal) return {};
		return fallback_notice_text(failure, failure_cause);
	}
	const descriptor& installed() { return published; }

	std::string status()
	{
		std::string out = std::string("entity capacity ") + (published.raised ? "raised" : "stock") + " " + std::to_string(published.actors) + "/" + std::to_string(published.generic) + "/" + std::to_string(published.world);
		if (!published.raised) return out + (requested ? " (requested: " + failure + ")" : "");
		if (allocator_backing)
		{
			std::uint32_t actors = 0, generic = 0;
			std::memcpy(&actors, allocator_backing + allocator_layout::actor_base + 0x10998, 4);            // the actor arena's active count (sub-object +0x10998)
			std::memcpy(&generic, generic_subobject(allocator_backing) + allocator_layout::sub_active, 4);   // the generic arena's active count
			out += "; arenas actor " + std::to_string(*natives::allocator_cell ? actors : 0) + "/" + std::to_string(published.actors) + " generic " + std::to_string(*natives::allocator_cell ? generic : 0) + "/" + std::to_string(published.generic);
		}
		const auto w = entity_owners::counters(owners);
		if (w.available)
			out += "; world live " + std::to_string(w.live) + " free " + std::to_string(w.free_slots) + " retired " + std::to_string(w.retired) + " maxgen " + std::to_string(w.max_generation) +
			       " refusals " + std::to_string(w.refusals) + " reset-time attempts " + std::to_string(w.reset_time_attempts);
		out += "; save claim " + std::string(claim_state_text(claim)) + " rejections " + std::to_string(claim_rejections.load()) + "; factory failures " + std::to_string(factory_failures.load()) +
		       "; transfer refusals " + std::to_string(transfer_refusals.load()) +
		       "; reliable receive deferrals " + std::to_string(entity_transfer::backpressure_count()) +
		       (fast_installed ? "; fast transfer records sent " + std::to_string(entity_transfer::fast_records_sent()) + " received " + std::to_string(transfer_state.chunks) : std::string());
		return out;
	}

	bool retire_slot(std::uint32_t index) { return published.raised && entity_owners::retire_slot(owners, index); }
	bool fast_transfer_installed() { return fast_installed; }
	std::uint32_t installed_target(std::uint32_t site)
	{
		if (!install_published || !published.raised) return 0;
		using namespace entity_capacity_sites;
		for (const auto& s : relocation_sites)
		{
			if (s.va != site || s.kind != relocation_kind::rel32) continue;
			const auto name = symbol_names[static_cast<std::size_t>(s.target)];
			if (owned_elsewhere(name)) return 0;
			const auto found = symbols.find(name);
			return found == symbols.end() ? 0 : found->second;
		}
		return 0;
	}
}
