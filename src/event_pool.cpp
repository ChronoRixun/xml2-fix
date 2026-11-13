#include "event_pool.hpp"
#include "event_pool_rules.hpp"
#include "entity_capacity.hpp"
#include "entity_capacity_layout.hpp"
#include "game_version.hpp"
#include "ini.hpp"
#include "limits_rules.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <bit>
#include <cstdio>
#include <cstring>

namespace event_pool
{
namespace
{
	using namespace event_pool_rules;
	using u32 = std::uint32_t;
	using reset_t = void(__fastcall*)(void*, void*);
	using add_t = void(__fastcall*)(void*, void*, u32, u32);
	using pump_t = void(__fastcall*)(void*, void*, u32);

	bool hooked = false, pool_raised = false;
	reset_t previous_reset = nullptr;
	add_t previous_add = nullptr;
	pump_t previous_pump = nullptr;

	// Everything below is the game thread's (the manager is only ever used from the world update).
	struct watch
	{
		u32 peak = 0, refused = 0, pumps = 0, last_event = 0, last_owner = 0, last_time_bits = 0;
		bool broken = false, full = false, had_entries = false, empty_logged = false;
		float empty_since = -1.f;
		char zone[64]{};
	} w;
	u32 lifetime_peak = 0, lifetime_refused = 0, breaks = 0, empties = 0;

	template <class T> T at(const void* p, const u32 offset) { T v{}; std::memcpy(&v, static_cast<const unsigned char*>(p) + offset, sizeof v); return v; }
	const unsigned char* body() { return reinterpret_cast<const unsigned char*>(static_cast<std::uintptr_t>(installed_object)) + body_at; }
	bool ours(const void* self) { return self == reinterpret_cast<const void*>(static_cast<std::uintptr_t>(installed_object)); }
	float pump_time = 0; // the game time of the last pump (the world update's own clock argument)
	unsigned pump_depth = 0;
	const char* zone_now() { return reinterpret_cast<const char*>(static_cast<std::uintptr_t>(0x72a758)); }
	void remember_zone()
	{
		if (w.zone[0]) return;
		std::strncpy(w.zone, zone_now(), sizeof w.zone - 1);
	}
	u32 bits(const unsigned char* words, const u32 count)
	{
		u32 n = 0;
		for (u32 i = 0; i < count; ++i) n += u32(std::popcount(at<u32>(words, i * 4)));
		return n;
	}
	counts read(const bool with_bitmaps)
	{
		const auto* b = body();
		const auto& l = installed_layout;
		counts c{at<u32>(b, l.record_count), at<u32>(b, l.heap_count), at<u32>(b, l.ring_count), 0, 0};
		if (with_bitmaps) { c.used_bits = bits(b + l.used_bitmap, l.words); c.alloc_bits = bits(b + l.alloc_bitmap, l.words); }
		return c;
	}

	void __fastcall reset_hook(void* self, void* edx)
	{
		if (ours(self))
		{
			if (w.peak)
				logger::write("timed events %s: peak %u of %u (retail pool %u%s); refused %u%s", w.zone[0] ? w.zone : "-", w.peak, installed_layout.records, retail_records,
				              w.peak > retail_records ? " - the retail pool would have overflowed" : "", w.refused, w.broken ? "; QUEUE BROKEN in this zone" : "");
			w = watch{};
		}
		previous_reset(self, edx);
	}

	void __fastcall add_hook(void* self, void* edx, const u32 record, const u32 time_bits)
	{
		if (!ours(self)) { previous_add(self, edx, record, time_bits); return; }
		const auto* b = body();
		const auto& l = installed_layout;
		const auto records = at<u32>(b, l.record_count), heap = at<u32>(b, l.heap_count);
		const auto* r = reinterpret_cast<const void*>(static_cast<std::uintptr_t>(record));
		w.last_owner = at<u32>(r, 0); w.last_event = at<u32>(r, 4); w.last_time_bits = time_bits;
		if (!add_fits(records, heap, l))
		{
			++w.refused; ++lifetime_refused;
			if (!w.full)
			{
				w.full = true; remember_zone();
				logger::write("timed events: pool FULL in zone %s at game time %.3f (%u records, heap %u, capacity %u) - the event %08x for owner %08x was dropped rather than overwrite a live record; raise [Limits] TimedEvents",
				              w.zone, pump_time, records, heap, l.records, w.last_event, w.last_owner);
			}
			return;
		}
		if (records + 1 > w.peak) { w.peak = records + 1; if (w.peak > lifetime_peak) lifetime_peak = w.peak; }
		previous_add(self, edx, record, time_bits);
	}

	void __fastcall pump_hook(void* self, void* edx, const u32 time_bits)
	{
		if (!ours(self)) { previous_pump(self, edx, time_bits); return; }
		std::memcpy(&pump_time, &time_bits, 4);
		// A pump entered from inside an event's fire sees the outer pump's popped, not yet freed record: only the
		// outermost pump's end is a quiescent queue.
		++pump_depth;
		previous_pump(self, edx, time_bits);
		if (--pump_depth) return;
		remember_zone();
		const bool deep = (++w.pumps & 31u) == 0;
		auto c = read(deep);
		auto v = judge(c, installed_layout, deep);
		if (v != verdict::ok && !w.broken)
		{
			if (!deep) c = read(true);
			w.broken = true; ++breaks;
			float last = 0; std::memcpy(&last, &w.last_time_bits, 4);
			logger::write("timed events: QUEUE BROKEN in zone %s at game time %.3f - %s: records %u, heap %u, free ring %u, used bits %u, alloc bits %u, capacity %u; peak %u; last add: event %08x owner %08x for %.3f; pump %u since the zone's reset (diagnostic only)",
			              w.zone, pump_time, name(v), c.record_count, c.heap_count, c.ring_count, c.used_bits, c.alloc_bits, installed_layout.records, w.peak, w.last_event, w.last_owner, last, w.pumps);
		}
		if (c.heap_count)
		{
			w.had_entries = true; w.empty_since = -1.f;
		}
		else if (w.had_entries && !w.empty_logged && std::strncmp(w.zone, "menu/", 5) != 0)
		{
			const float now = pump_time;
			if (w.empty_since < 0) w.empty_since = now;
			else if (now - w.empty_since >= empty_alarm_seconds)
			{
				w.empty_logged = true; ++empties;
				logger::write("timed events: the queue has been EMPTY for %.1f game seconds in zone %s (since %.3f; %u records still counted in use): no character or object update is booked, the world stands still (diagnostic only)",
				              now - w.empty_since, w.zone, w.empty_since, c.record_count);
			}
		}
	}

	bool readable_retail(const std::vector<edit>& edits)
	{
		__try
		{
			for (const auto& e : edits)
				if (std::memcmp(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(e.va)), e.before.data(), e.before.size()) != 0) return false;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	bool read_slots(u32& reset, u32& add, u32& pump, bool& constructed)
	{
		__try
		{
			reset = at<u32>(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(reset_slot)), 0);
			add = at<u32>(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(add_slot)), 0);
			pump = at<u32>(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(pump_slot)), 0);
			constructed = (at<unsigned char>(reinterpret_cast<const void*>(static_cast<std::uintptr_t>(init_flag)), 0) & 1) != 0;
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
	}
	void install_checked(const HMODULE game, const std::optional<std::string>& key, const bool entities_raised)
	{
		u32 records = retail_records;
		const u32 fallback = entities_raised ? raised_records : retail_records;
		if (!parse_capacity(key ? std::string_view(*key) : std::string_view(), fallback, records))
		{
			logger::write("timed events: [Limits] TimedEvents=%s is not 0, 1125 or a count up to %u; the pool is %u", key->c_str(), max_records, fallback);
			records = fallback;
		}
		if (reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base) { logger::write("timed events: XMen2.exe is not at its image base; the pool stays retail, no watchdog"); return; }
		u32 reset = 0, add = 0, pump = 0; bool constructed = true;
		if (!read_slots(reset, add, pump, constructed) || reset != native_reset || add != native_add)
		{
			logger::write("timed events: the manager's reset/add slots are not retail (%08x, %08x); the pool stays retail, no watchdog", reset, add);
			return;
		}
		// The pump slot is retail or holds the xml1 turrets dispatch hook (which calls the retail pump first): chained as found.
		void* block = nullptr;
		const auto l = layout_for(records);
		if (records != retail_records)
		{
			if (constructed) logger::write("timed events: the manager is already constructed; the pool stays retail (%u)", retail_records);
			else
			{
				block = VirtualAlloc(nullptr, body_at + l.body_bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
				if (!block) logger::write("timed events: no memory for %u records; the pool stays retail", records);
			}
		}
		const auto object = block ? static_cast<u32>(reinterpret_cast<std::uintptr_t>(block)) : retail_object;
		std::vector<edit> edits;
		if (block)
		{
			edits = plan(l, object);
			if (!readable_retail(edits))
			{
				logger::write("timed events: the manager's code is not retail; the pool stays retail (%u), no watchdog", retail_records);
				VirtualFree(block, 0, MEM_RELEASE);
				return;
			}
		}
		using xml1_hook_transaction::pointer_bytes;
		std::vector<xml1_hook_transaction::patch> writes;
		const auto edit_count = unsigned(edits.size());
		for (auto& e : edits) writes.push_back({e.va, std::move(e.before), std::move(e.after)});
		const auto pointer = [](const u32 v) { return pointer_bytes(reinterpret_cast<void*>(static_cast<std::uintptr_t>(v))); };
		writes.push_back({reset_slot, pointer(reset), pointer_bytes(reinterpret_cast<void*>(&reset_hook))});
		writes.push_back({add_slot, pointer(add), pointer_bytes(reinterpret_cast<void*>(&add_hook))});
		writes.push_back({pump_slot, pointer(pump), pointer_bytes(reinterpret_cast<void*>(&pump_hook))});
		previous_reset = reinterpret_cast<reset_t>(static_cast<std::uintptr_t>(reset));
		previous_add = reinterpret_cast<add_t>(static_cast<std::uintptr_t>(add));
		previous_pump = reinterpret_cast<pump_t>(static_cast<std::uintptr_t>(pump));
		// Published before the bytes change: nothing runs the manager during startup, and the readers (join in progress,
		// the turrets' hook) only look when the game does.
		if (block) { installed_layout = l; installed_object = object; }
		xml1_hook_transaction::memory memory; bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			hooked = true; pool_raised = block != nullptr;
			logger::write("timed events: pool %u records (%s; retail %u), object %08X, %u operand edits; watchdog on the manager's reset / add / pump (%s) - logs a queue whose counts disagree, an empty queue in a live zone, and a full pool (the add is dropped, never overwritten)",
			              installed_layout.records, pool_raised ? (key ? "[Limits] TimedEvents" : "raised with the entity capacity") : "retail", retail_records, installed_object,
			              edit_count, pump == native_pump ? "retail pump" : "chained after the xml1 turrets dispatch");
			return;
		}
		installed_layout = retail_layout; installed_object = retail_object;
		if (block) VirtualFree(block, 0, MEM_RELEASE);
		if (safe) logger::write("timed events: the install was refused; the pool stays retail (%u), no watchdog", retail_records);
		else { game_version::refuse_startup(); logger::write("timed events: rollback/protection verification failed; process startup refused"); }
	}
}

bool installed() { return hooked; }
bool raised() { return pool_raised; }
std::string status()
{
	char line[256];
	std::snprintf(line, sizeof line, "timed events: capacity %u (%s), watchdog %s, zone peak %u, peak %u, refused %u, breaks %u, empty alarms %u", installed_layout.records,
	              pool_raised ? "raised" : "retail", hooked ? "on" : "off", w.peak, lifetime_peak, lifetime_refused, breaks, empties);
	return line;
}

void install(const HMODULE game)
{
	const auto key = ini::text(L"Limits", L"TimedEvents");
	const bool entities_raised = entity_capacity::installed().raised;
	if (!key && !entities_raised) return; // plain XML2: nothing is read, written or hooked
	install_checked(game, key, entities_raised);
}
}
