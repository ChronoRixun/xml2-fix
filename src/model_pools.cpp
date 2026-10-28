#include "model_pools.hpp"
#include "model_pools_rules.hpp"
#include "combo_heads.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "patch_transaction.hpp"

#include <Windows.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace model_pools
{
	namespace
	{
		using namespace model_pools_rules;

		const char* setting_name = "off";
		aggregate_layout layout = aggregate_layout_for(retail_capacities);
		DWORD aggregate_va = retail_aggregate; // where the live counts are read
		DWORD particle_manager_va = retail_particle_manager;
		bool readable = false;                 // the exe is the retail build (status reads its counts)
        std::uint32_t procedural_find_next=0;
		agreement rule{};                      // rule 28 as installed (install sets it once)

		bool read_dword(const DWORD address, DWORD& value)
		{
			return patch_transaction::read_memory(address, &value, sizeof(value));
		}

		DWORD address_of(const void* p)
		{
			return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(p));
		}

		DWORD live_address(const kind k)
		{
			if (k == kind::movie) return aggregate_va + layout.offset[index(kind::movie)] + 0x24;
			return aggregate_va + pool_start(layout, k) + layout.pool[index(k)].live;
		}

		// The retail bytes of the getter and the init guard's reference, enough to know the code is the retail build's.
		bool retail_build()
		{
			std::uint8_t b[5]{};
			return patch_transaction::read_memory(0x575637, b, sizeof(b)) && std::memcmp(b, "\xb9\xd0\xf6\x7b\x00", 5) == 0;
		}
	}

    std::array<pool_counter_rules::source,10> diagnostic_sources()
    {
        std::array<pool_counter_rules::source,10> out{};
        for(std::size_t i=0;i<kinds;++i)
            out[i]={readable?live_address(static_cast<kind>(i)):0,0,static_cast<DWORD>(layout.capacity[i]),init_guard,false,false};
        out[9]={readable?particle_manager_va:0,layout.psys.count,static_cast<DWORD>(layout.psys.capacity),particle_manager_guard,false,true};
        return out;
    }
	long live(const int kind_index)
	{
		if (!readable || kind_index < 0 || kind_index >= static_cast<int>(kinds)) return -1;
		DWORD guard = 0, v = 0;
		if (!read_dword(init_guard, guard) || !(guard & 1)) return 0; // not built yet
		return read_dword(live_address(static_cast<kind>(kind_index)), v) ? static_cast<long>(v) : -1;
	}

	int capacity(const int kind_index)
	{
		return kind_index >= 0 && kind_index < static_cast<int>(kinds) ? layout.capacity[kind_index] : 0;
	}

	std::string status()
	{
		std::string out = std::string("model pools ") + setting_name + ":";
		static constexpr kind order[] = {kind::model, kind::transform, kind::attributes, kind::shader_fx, kind::igb, kind::actor, kind::procedural_geometry, kind::particle_cloud, kind::movie};
		bool first = true;
		for (const auto k : order)
		{
			const long n = live(static_cast<int>(index(k)));
			char buf[64];
			if (n < 0) std::snprintf(buf, sizeof(buf), "%s %s -/%d", first ? "" : ",", kind_name[index(k)], layout.capacity[index(k)]);
			else std::snprintf(buf, sizeof(buf), "%s %s %ld/%d", first ? "" : ",", kind_name[index(k)], n, layout.capacity[index(k)]);
			out += buf;
			first = false;
		}
		DWORD guard = 0, free = 0;
		if (readable && read_dword(particle_manager_guard, guard) && (guard & 1) && read_dword(particle_manager_va + layout.psys.count, free))
		{
			char buf[64];
			std::snprintf(buf, sizeof(buf), ", particle systems free %lu/%d", free, layout.psys.capacity);
			out += buf;
		}
		return out;
	}

	bool requested() { return rule.requested; }
    std::uint32_t installed_target(std::uint32_t call) {return call==0x571724 ? procedural_find_next : 0;}
	bool verified() { return rule.verified; }
	std::array<std::uint32_t, 4> agreement_values() { return rule.values; }

	void install(const HMODULE game)
	{
		readable = reinterpret_cast<std::uintptr_t>(game) == image_base && retail_build();
		combo_heads::set_pool_live_addresses(live_address(kind::igb), live_address(kind::shader_fx), layout.capacity[index(kind::igb)], layout.capacity[index(kind::shader_fx)]);
		std::string handles_note;
		const auto set = with_handles(parse_setting(ini::text(L"Engine", L"ModelPools")), ini::text(L"Engine", L"ModelHandles"), &handles_note);
		if (!handles_note.empty()) logger::write("model pools: %s", handles_note.c_str());
		rule = agreement_for(set, std::nullopt); // unverified until the raise is committed
		if (!set.valid)
			logger::write("model pools: [Engine] ModelPools is not x2, x4 or off - the game's own model pools stay (handles 692, IGB 400, actors 32)");
		if (!set.raise) return;
		constexpr const char* stays = "the game's own model pools stay (handles 692, IGB 400, actors 32)";
		if (!readable)
		{
			logger::write("model pools: XMen2.exe isn't the retail build at 0x400000 - %s", stays);
			return;
		}
		if (!capacities_valid(*set.raise))
		{
			logger::write("model pools: invalid capacities - %s", stays);
			return;
		}
		DWORD built = 0;
		if (!read_dword(init_guard, built) || (built & 1))
		{
			logger::write("model pools: the game has built its model aggregate already (0x7dad68 = 0x%08lX) - %s", built, stays);
			return;
		}
		if (!read_dword(particle_manager_guard, built) || (built & 1))
		{
			logger::write("model pools: the game has built its particle-system manager already (0x7dc868 = 0x%08lX) - %s", built, stays);
			return;
		}
		const auto wanted = aggregate_layout_for(*set.raise);
		// The block is zero-filled (the game's constructors clear only part of each bitmap and rely on a zeroed static).
		auto* block = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, wanted.size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		auto* caves = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		auto* systems = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, wanted.psys.size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
		const auto release = [block, caves, systems]
		{
			if (block) VirtualFree(block, 0, MEM_RELEASE);
			if (caves) VirtualFree(caves, 0, MEM_RELEASE);
			if (systems) VirtualFree(systems, 0, MEM_RELEASE);
		};
		if (!block || !caves || !systems)
		{
			logger::write("model pools: can't allocate the model aggregate (error %lu) - %s", GetLastError(), stays);
			release();
			return;
		}
		const auto p = plan_for(wanted, blocks{address_of(block), address_of(systems)}, address_of(caves));
		if (!p || p->cave_code.size() > 0x1000)
		{
			logger::write("model pools: the site table doesn't fit its own rules (a build error) - %s", stays);
			release();
			return;
		}
		std::memcpy(caves, p->cave_code.data(), p->cave_code.size());
		DWORD old = 0;
		if (!VirtualProtect(caves, 0x1000, PAGE_EXECUTE_READ, &old))
		{
			logger::write("model pools: can't make the caves executable (error %lu) - %s", GetLastError(), stays);
			release();
			return;
		}
		FlushInstructionCache(GetCurrentProcess(), caves, p->cave_code.size());

		patch_transaction::contribution c;
		c.owner = "model pools";
		for (const auto& w : p->writes)
		{
			c.guards.push_back({w.va, w.before, w.what});
			c.writes.push_back({w.va, w.bytes, w.what});
		}
		for (const auto& e : p->expects) c.guards.push_back({e.va, e.before, e.what});
		bool committed = false;
		c.publish = [&] { committed = true; };
		bool keep_memory = false;
		c.retain = [&] { keep_memory = true; };
		patch_transaction::transaction t;
		t.add(std::move(c));
		const auto result = t.commit();
		if (!committed)
		{
			logger::write("model pools: not raised (%s: %s) - %s", patch_transaction::name(result), t.report().c_str(), stays);
			if (!keep_memory) release();
			return;
		}
		layout = wanted;
        for(const auto& w:p->writes)if(w.va==0x571724) {
            std::uint32_t delta=0;std::memcpy(&delta,w.bytes.data()+1,4);procedural_find_next=w.va+5+delta;
        }
		rule = agreement_for(set, *set.raise);
		aggregate_va = address_of(block);
		particle_manager_va = address_of(systems);
		setting_name = set.name;
		combo_heads::set_pool_live_addresses(live_address(kind::igb), live_address(kind::shader_fx), layout.capacity[index(kind::igb)], layout.capacity[index(kind::shader_fx)]);
		logger::write("model pools: raised to %s - the model aggregate moves from 0x7bf6d0 to 0x%08lX (%lu bytes, was 0x1b698), built and torn down by the game's own code: "
		              "handles %d, transforms %d, attributes %d, shader effects %d, IGB %d, actors %d, procedural geometry %d, particle clouds %d; the particle-system manager "
		              "moves from 0x7dad78 to 0x%08lX (%lu bytes, %d systems, was 40); %zu fields, %zu caves (at 0x%08lX), %zu writes",
		              set.name, aggregate_va, layout.size, layout.capacity[index(kind::model)], layout.capacity[index(kind::transform)], layout.capacity[index(kind::attributes)],
		              layout.capacity[index(kind::shader_fx)], layout.capacity[index(kind::igb)], layout.capacity[index(kind::actor)], layout.capacity[index(kind::procedural_geometry)],
		              layout.capacity[index(kind::particle_cloud)], particle_manager_va, layout.psys.size, layout.psys.capacity, p->fields, p->caves, address_of(caves), p->writes.size());
	}
}
