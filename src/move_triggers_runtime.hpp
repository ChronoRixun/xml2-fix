#pragma once

#include "move_triggers_rules.hpp"
#include <cstring>

// These are the production callbacks, also executed by the native image fixtures.
// No actor layout or native stack packet grows. Missing cache storage runs only the
// first nineteen events through scratch storage, mirrored back to the native actor.
namespace move_triggers_runtime
{
	using namespace move_triggers_rules;
	using initialize_t = void(__fastcall*)(void*, void*, void*, float);
	using tick_t = void(__fastcall*)(void*, void*, float);
	using start_t = void(__fastcall*)(void*, void*, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD);
	inline initialize_t original_initialize = nullptr;
	inline tick_t original_tick = nullptr;
	inline start_t original_start = nullptr;
	inline actor_times* (*lookup)(void*) = nullptr;
	inline void (*notify)(const char*, void*) = nullptr;
	inline int cap = stock_capacity;
	inline DWORD address(const void* p) { return static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(p)); }
	inline DWORD field(const void* p, DWORD offset) { DWORD n; std::memcpy(&n, static_cast<const std::uint8_t*>(p) + offset, 4); return n; }
	inline void report(const char* event, void* actor) { if (notify) notify(event, actor); }

	struct fallback_scope;
	inline thread_local fallback_scope* fallback = nullptr;
	struct fallback_scope
	{
		fallback_scope* prior;
		void* actor;
		actor_times times;
		explicit fallback_scope(void* a) : prior(fallback), actor(a)
		{
			times.actor = address(a); times.source.fill(0xbf800000); times.scheduled.fill(0xbf800000);
			std::memcpy(times.scheduled.data(), static_cast<std::uint8_t*>(a) + 0x640, stock_capacity * 4);
			fallback = this;
		}
		~fallback_scope() { fallback = prior; }
	};
	inline actor_times* cached(void* actor) { return lookup ? lookup(actor) : nullptr; }
	inline actor_times& get(void* actor)
	{
		if (auto* t = cached(actor)) return *t;
		for (auto* f = fallback; f; f = f->prior) if (f->actor == actor) return f->times;
		// Cancellation can run directly from construction, outside a wrapped operation.
		thread_local actor_times scratch;
		scratch = {}; scratch.actor = address(actor); scratch.source.fill(0xbf800000); scratch.scheduled.fill(0xbf800000);
		std::memcpy(scratch.scheduled.data(), static_cast<std::uint8_t*>(actor) + 0x640, stock_capacity * 4);
		return scratch;
	}
	inline void* __cdecl scheduled_times(void* actor) { return get(actor).scheduled.data(); }
	inline void __cdecl capture_times(void* actor, void* move, void* packet)
	{
		auto& t = get(actor); t.packet = address(packet); t.move = address(move);
		t.source.fill(0xbf800000);
		std::memcpy(t.source.data(), static_cast<std::uint8_t*>(packet) + 8, stock_capacity * 4);
		using getter_t = float(__fastcall*)(void*, void*, int);
		if (cached(actor))
		{
			const auto getter = reinterpret_cast<getter_t>(field(reinterpret_cast<void*>(field(move, 0)), 0x20));
			for (int i = stock_capacity; i < cap; ++i)
			{
				const float value = getter(move, nullptr, i);
				std::memcpy(&t.source[i], &value, 4);
			}
		}
		report("capture", actor);
	}
	inline void* __cdecl source_times(void* actor, void* packet)
	{
		auto& t = get(actor);
		if (t.packet != address(packet) || t.move != field(actor, 0x378))
			std::fill(t.source.begin() + stock_capacity, t.source.end(), DWORD{0xbf800000});
		std::memcpy(t.source.data(), static_cast<std::uint8_t*>(packet) + 8, stock_capacity * 4);
		return t.source.data();
	}
	inline void __cdecl clear_times(void* actor) { get(actor).scheduled.fill(0xbf800000); report("clear", actor); }
	inline void mirror(void* actor) { std::memcpy(static_cast<std::uint8_t*>(actor) + 0x640, get(actor).scheduled.data(), stock_capacity * 4); }
	inline void __fastcall initialize_times(void* actor, void*, void* packet, float scale)
	{
		fallback_scope scope(actor);
		original_initialize(actor, nullptr, packet, scale); mirror(actor);
		if (packet && get(actor).packet == address(packet)) get(actor).packet = 0;
		report("initialize", actor);
	}
	inline void __fastcall tick_times(void* actor, void*, float delta)
	{
		fallback_scope scope(actor);
		original_tick(actor, nullptr, delta); mirror(actor);
	}
	inline void __fastcall start_move(void* actor, void*, DWORD a, DWORD b, DWORD c, DWORD d, DWORD e, DWORD f, DWORD g, DWORD h)
	{
		fallback_scope scope(actor);
		auto& t = get(actor);
		const source_scope enclosing(t);
		t.packet = 0; t.move = 0; t.source.fill(0xbf800000);
		report("start", actor);
		original_start(actor, nullptr, a, b, c, d, e, f, g, h);
		// A nested start may fail while the old move is current. The enclosing packet
		// remains owned by its still-live invocation, regardless of the current move.
		enclosing.finish(t);
	}
}
