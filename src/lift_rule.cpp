#include "lift_rule.hpp"

#include "game_version.hpp"
#include "ini.hpp"
#include "lift_rule_rules.hpp"
#include "log.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

namespace lift_rule
{
	namespace
	{
		using namespace lift_rule_rules;
		using u32 = std::uint32_t;
		std::atomic<int> state{-1}; // 1 installed; -1 the game's own; 0 asked for but nothing patched
		bool trace = false;
		heaviness_store store;

		template<class T> T at(const void* p, u32 off) { T v{}; std::memcpy(&v, static_cast<const unsigned char*>(p) + off, sizeof v); return v; }

		// The entity reader's number getter (vt+0x18): the attribute's name and a 16-bit destination it leaves alone
		// when the attribute is absent (break_rule.cpp reads xml1structure the same way).
		using read_number = void(__fastcall*)(void*, void*, const char*, std::int16_t*);
		using might_t = unsigned char(__fastcall*)(void* stats, void*);

		// From the bridge at 0x498991: the object (esi) has just read its `heaviness` through its reader (edi).
		void __cdecl object_parsed(void* object, void* reader)
		{
			std::int16_t xml1 = -1;
			at<read_number>(at<const void*>(reader, 0), reader_number)(reader, nullptr, heaviness_attribute, &xml1);
			store.set(at<u32>(object, entity_handle), xml1 < 0 ? std::nullopt : std::optional<int>(xml1));
			if (trace && xml1 >= 0)
				logger::write("lift rule: object %08X: %s %d", at<u32>(object, entity_handle), heaviness_attribute, xml1);
		}
		// From the bridge at 0x427fa3: may `actor` lift `object`? 0 no, 1 yes, 2 the game's own test.
		int __cdecl lift_gate(void* object, void* actor)
		{
			const auto xml1 = store.get(at<u32>(object, entity_handle));
			if (!xml1) return static_cast<int>(verdict::game);
			auto* stats = static_cast<unsigned char*>(actor) + actor_lift_stats;
			const int might = reinterpret_cast<might_t>(static_cast<std::uintptr_t>(might_getter_va))(stats, nullptr);
			const int byte = at<std::uint8_t>(object, object_heaviness);
			const auto v = decide(xml1, byte, might);
			if (trace)
				logger::write("lift rule: actor %08X might %d object %08X xml1heaviness %d byte %d: %s", at<u32>(actor, entity_handle), might,
				              at<u32>(object, entity_handle), *xml1, byte, v == verdict::lift ? "lift" : v == verdict::refuse ? "refuse" : "game's rule");
			return static_cast<int>(v);
		}
		auto parsed_function = &object_parsed;
		auto gate_function = &lift_gate;
		u32 parse_resume = parse_resume_va, gate_native = gate_native_va, gate_refuse = gate_refuse_va;
		int __cdecl scaled_heaviness(void* object)
		{
			const int byte = at<std::uint8_t>(object, object_heaviness);
			const auto h = consumer_heaviness(store.get(at<u32>(object, entity_handle)), byte);
			if (trace) logger::write("lift rule: consumer object %08X byte %d XML1 heaviness %d", at<u32>(object, entity_handle), byte, h);
			return h;
		}
		int __cdecl throw_damage(void* entry)
		{
			const int level = at<std::int8_t>(entry, 0xb);
			const int damage = carried_damage(level);
			if (trace) logger::write("lift rule: carried throw damagelevel %d damage %d", level, damage);
			return damage;
		}
		auto heaviness_function = &scaled_heaviness;
		auto damage_function = &throw_damage;
		u32 impact_resume = impact_resume_va, carry_resume = carry_resume_va, tk_resume = tk_resume_va;

		// All three sites have an empty x87 stack. Preserve registers/flags except the displaced output.
		// pushad saves EAX at +28 and EDX at +20; cdecl helpers do not alter the game's frame.
		__declspec(naked) void impact_bridge()
		{
			__asm {
				pushfd
				pushad
				push esi
				call dword ptr [heaviness_function]
				add esp, 4
				mov byte ptr [esp + 28], al
				popad
				popfd
				inc al
				shl al, 1
				jmp dword ptr [impact_resume]
			}
		}
		__declspec(naked) void carry_bridge()
		{
			__asm {
				mov eax, [esp + 0x10]
				pushfd
				pushad
				push eax
				call dword ptr [damage_function]
				add esp, 4
				mov [esp + 28], eax
				popad
				popfd
				jmp dword ptr [carry_resume]
			}
		}
		__declspec(naked) void tk_bridge()
		{
			__asm {
				pushfd
				pushad
				push esi
				call dword ptr [heaviness_function]
				add esp, 4
				mov [esp + 20], eax
				popad
				popfd
				jmp dword ptr [tk_resume]
			}
		}

		// The game's clamp, then the callback with every register kept.
		__declspec(naked) void parse_bridge()
		{
			__asm {
				mov al, [esp + 0x14]
				cmp al, 3
				jbe clamped
				mov al, 3
			clamped:
				pushfd
				pushad
				push edi
				push esi
				call dword ptr [parsed_function]
				add esp, 8
				popad
				popfd
				jmp dword ptr [parse_resume]
			}
		}
		// esi the object, edi the actor; the game's frame still holds edi and esi above the return address.
		__declspec(naked) void gate_bridge()
		{
			__asm {
				push edi
				push esi
				call dword ptr [gate_function]
				add esp, 8
				cmp eax, 2
				je native
				pop edi
				pop esi
				ret 4
			native:
				cmp byte ptr [esi + 0x31d], 3
				jae refuse
				jmp dword ptr [gate_native]
			refuse:
				jmp dword ptr [gate_refuse]
			}
		}

		bool live_matches(const guard& g)
		{
			__try { return limits_rules::matches(reinterpret_cast<const std::uint8_t*>(static_cast<std::uintptr_t>(g.va)), g.hex); }
			__except (EXCEPTION_EXECUTE_HANDLER) { return false; }
		}
		const guard* first_live_mismatch()
		{
			for (const auto& g : guards) if (!live_matches(g)) return &g;
			return nullptr;
		}
		std::vector<unsigned char> jump(u32 site, const void* target, std::size_t length)
		{
			std::vector<unsigned char> b(length, 0x90);
			b[0] = 0xe9;
			const auto rel = static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(target) - (site + 5));
			std::memcpy(b.data() + 1, &rel, 4);
			return b;
		}
	}

	void install(const HMODULE game)
	{
		state = -1;
		const auto value = ini::text(L"Game", L"LiftRule").value_or("");
		const auto wanted = parse(value);
		if (wanted == setting::native) return;
		state = 0;
		trace = ini::flag(L"Debug", L"LogLiftRule", false);
		const char* keep = " - lifting keeps the game's own rule";
		if (wanted == setting::invalid)
		{
			logger::write("lift rule: [Game] LiftRule=%s is neither xml1 nor xml2%s", value.c_str(), keep);
			return;
		}
		if (!game || reinterpret_cast<std::uintptr_t>(game) != limits_rules::image_base)
		{
			logger::write("lift rule: XMen2.exe isn't loaded at 0x400000 (not the game?)%s", keep);
			return;
		}
		if (const guard* g = first_live_mismatch())
		{
			logger::write("lift rule: 0x%08lX isn't the retail code (%s)%s", static_cast<unsigned long>(g->va), g->what, keep);
			return;
		}
		const std::vector<xml1_hook_transaction::patch> writes{
			{parse_hook_va, {parse_displaced.begin(), parse_displaced.end()}, jump(parse_hook_va, &parse_bridge, parse_displaced.size())},
			{gate_hook_va, {gate_displaced.begin(), gate_displaced.end()}, jump(gate_hook_va, &gate_bridge, gate_displaced.size())},
			{impact_hook_va, {impact_displaced.begin(), impact_displaced.end()}, jump(impact_hook_va, &impact_bridge, impact_displaced.size())},
			{carry_hook_va, {carry_displaced.begin(), carry_displaced.end()}, jump(carry_hook_va, &carry_bridge, carry_displaced.size())},
			{tk_hook_va, {tk_displaced.begin(), tk_displaced.end()}, jump(tk_hook_va, &tk_bridge, tk_displaced.size())},
		};
		xml1_hook_transaction::memory memory;
		bool safe = true;
		if (xml1_hook_transaction::apply(memory, writes, &safe))
		{
			state = 1;
			logger::write("lift rule: xml1 ([Game] LiftRule=xml1): objects with %s are lifted by X-Men Legends' rule (might + 1 >= heaviness, %d "
			              "never); revision 2 restores XML1 throw impact, carried throw damage and telekinesis limits; the heaviness byte is unchanged",
			              heaviness_attribute, xml1_max_heaviness);
		}
		else if (safe)
			logger::write("lift rule: the patch was refused (protection or readback)%s", keep);
		else
		{
			game_version::refuse_startup();
			logger::write("lift rule: rollback/protection verification failed; process startup refused");
		}
	}

	int heaviness(void* object) { return scaled_heaviness(object); }
	std::string status()
	{
		const int s = state.load();
		return std::string("lift rule ") + (s == 1 ? "xml1" : s == -1 ? "game's own" : "unavailable");
	}
	bool requested() { return state.load() != -1; }
	bool installed() { return state.load() == 1; }
	bool verified() { return !requested() || installed(); }
}
