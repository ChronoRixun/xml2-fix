// [Balance] ReceiveRounding / DamageCap (balance_parity_rules.hpp), executed side by side with the first game: X-Men
// Legends I's receive stages (default.xbe 0x45517..0x4555e, with its own _ftol 0x1a41f0 -> 0x3439c4) and cap
// (0x5ce0d..0x5ce19) against XMen2.exe's (0x437a54..0x437a7b, 0x453bfb..0x453c10), retail and with the rule's writes
// and stubs. Both games' runs are read from the owner's files at run time and checked (XMen2.exe against the guarded
// bytes, default.xbe by SHA-256) before anything executes; only their constants and calls are relocated into the
// test. No game code is embedded; the game is not launched.
//
// usage: xml2_balance_parity_test <XMen2.exe> [<default.xbe>]
#include "balance_parity_rules.hpp"
#include "gameplay_capabilities.hpp"
#include "sha256.hpp"

#include <windows.h>

#include <float.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace
{
	using namespace balance_parity_rules;
	using u8 = std::uint8_t;
	using u32 = std::uint32_t;
	int failures = 0, checks = 0;
	void check(bool ok, const char* why)
	{
		++checks;
		if (!ok)
		{
			++failures;
			std::printf("FAIL %s\n", why);
		}
	}

	std::vector<u8> read_file(const char* path)
	{
		std::ifstream file(path, std::ios::binary);
		return std::vector<u8>((std::istreambuf_iterator<char>(file)), {});
	}
	u32 word(const std::vector<u8>& raw, std::size_t at)
	{
		u32 v = 0;
		if (at + 4 <= raw.size()) std::memcpy(&v, raw.data() + at, 4);
		return v;
	}
	std::optional<std::vector<u8>> pe_bytes(const std::vector<u8>& raw, u32 va, u32 size)
	{
		if (raw.size() < 0x400) return std::nullopt;
		const u32 pe = word(raw, 0x3c);
		if (pe + 0xf8 > raw.size()) return std::nullopt;
		const u32 sections = raw[pe + 6] | (raw[pe + 7] << 8), optional = raw[pe + 20] | (raw[pe + 21] << 8);
		const u32 base = word(raw, pe + 24 + 28), rva = va - base, table = pe + 24 + optional;
		for (u32 i = 0; i < sections; ++i)
		{
			const u32 vsize = word(raw, table + 40 * i + 8), vaddr = word(raw, table + 40 * i + 12), rsize = word(raw, table + 40 * i + 16),
			          rptr = word(raw, table + 40 * i + 20);
			if (rva >= vaddr && rva + size <= vaddr + (vsize > rsize ? vsize : rsize) && rptr + rva - vaddr + size <= raw.size())
				return std::vector<u8>(raw.begin() + rptr + (rva - vaddr), raw.begin() + rptr + (rva - vaddr) + size);
		}
		return std::nullopt;
	}
	std::optional<std::vector<u8>> xbe_bytes(const std::vector<u8>& raw, u32 va, u32 size)
	{
		if (raw.size() < 0x124 || std::memcmp(raw.data(), "XBEH", 4)) return std::nullopt;
		const u32 image = word(raw, 0x104), count = word(raw, 0x11c), table = word(raw, 0x120);
		if (table < image || count > 64) return std::nullopt;
		for (u32 i = 0; i < count; ++i)
		{
			const std::size_t s = std::size_t(table - image) + i * 0x38;
			const u32 vaddr = word(raw, s + 4), offset = word(raw, s + 12), bytes = word(raw, s + 16);
			if (va >= vaddr && va - vaddr + size <= bytes && std::size_t(offset) + va - vaddr + size <= raw.size())
				return std::vector<u8>(raw.begin() + offset + (va - vaddr), raw.begin() + offset + (va - vaddr) + size);
		}
		return std::nullopt;
	}
	std::array<u8, 32> hash_of(const char* hex)
	{
		std::array<u8, 32> out{};
		for (std::size_t i = 0; i < 32; ++i) out[i] = static_cast<u8>(std::strtoul(std::string(hex + 2 * i, 2).c_str(), nullptr, 16));
		return out;
	}
	bool identity(const std::optional<std::vector<u8>>& run, const char* hex)
	{
		return run && sha256_fips::digest(run->data(), run->size()) == hash_of(hex);
	}

	// One executable page per relocated run.
	struct page
	{
		u8* at = static_cast<u8*>(VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
		~page() { if (at) VirtualFree(at, 0, MEM_RELEASE); }
		u32 va(std::size_t offset = 0) const { return static_cast<u32>(reinterpret_cast<std::uintptr_t>(at)) + static_cast<u32>(offset); }
		void put(std::size_t offset, const std::vector<u8>& bytes) { std::memcpy(at + offset, bytes.data(), bytes.size()); }
		void abs(std::size_t operand, const void* to) { const auto v = static_cast<u32>(reinterpret_cast<std::uintptr_t>(to)); std::memcpy(at + operand, &v, 4); }
		void rel(std::size_t operand, u32 to) { const auto v = to - (va(operand) + 4); std::memcpy(at + operand, &v, 4); }
		void flush() { FlushInstructionCache(GetCurrentProcess(), at, 4096); }
	};

	void* entry = nullptr;
	std::uint16_t cw_after = 0, sw_before = 0, sw_after = 0;
	u32 kept_ebx = 0, kept_esi = 0, kept_edi = 0, kept_ebp = 0;
	// The receive runs: esi = the hit record, the scale at the run's [esp+0x10] and the defense at [esp+0x14] (the
	// run is called, so those are the caller's [esp+0x0c] / [esp+0x10]); XML1's temporary [esp+0x24] is in the frame.
	__declspec(naked) void receive_run(void* record, float defense, float scale)
	{
		__asm {
			push ebp
			mov ebp, esp
			push ebx
			push esi
			push edi
			mov esi, [ebp+8]
			mov ebx, 0x13572468
			mov edi, 0x2468ace0
			sub esp, 0x40
			mov eax, [ebp+16]
			mov [esp+0x0c], eax
			mov eax, [ebp+12]
			mov [esp+0x10], eax
			fnstsw word ptr [sw_before]
			call dword ptr [entry]
			fnstsw word ptr [sw_after]
			fnstcw word ptr [cw_after]
			mov [kept_ebx], ebx
			mov [kept_edi], edi
			mov [kept_ebp], ebp
			add esp, 0x40
			pop edi
			pop esi
			pop ebx
			pop ebp
			ret
		}
	}
	// The cap runs: XMen2.exe's with the damage in st0 (st0 back), XML1's with it in eax (eax back).
	__declspec(naked) float cap_run_pc(float)
	{
		__asm {
			fld dword ptr [esp+4]
			call dword ptr [entry]
			ret
		}
	}
	__declspec(naked) int cap_run_x1(int)
	{
		__asm {
			mov eax, [esp+4]
			call dword ptr [entry]
			ret
		}
	}

	// XMen2.exe's receive run 0x437a54..0x437a7b, then `fstp dword [esi+0x9c]; ret` (the retail push edi before that
	// store is left out: it only passes edi to the call after it).
	struct pc_receive
	{
		page code, stubs;
		static constexpr float zero = 0.0f;
		pc_receive(bool rule)
		{
			auto run = retail_bytes(receive_site, receive_site.va, 0x27);
			code.put(0, run);
			code.abs(0x0c, &zero);
			code.abs(0x1d, &zero);
			code.put(0x27, {0xd9, 0x9e, 0x9c, 0x00, 0x00, 0x00, 0xc3});
			if (rule)
			{
				stubs.put(0, block(stubs.va()));
				stubs.flush();
				const layout l{stubs.va()};
				code.put(0x00, call6(code.va(0x00), l.load()));
				code.put(0x21, call6(code.va(0x21), l.scale()));
			}
			code.flush();
		}
		float operator()(float damage, float defense, float scale)
		{
			std::array<u8, 0x100> record{};
			std::memcpy(record.data() + 0x9c, &damage, 4);
			entry = code.at;
			receive_run(record.data(), defense, scale);
			float out = 0;
			std::memcpy(&out, record.data() + 0x9c, 4);
			return out;
		}
	};
	// XML1's receive run 0x45517..0x4555e (integer damage at +0x94), its three 0.0 operands and two _ftol calls
	// relocated to the test's 0.0 and a copy of XML1's own conversion pair.
	struct x1_receive
	{
		page code, ftol;
		static constexpr float zero = 0.0f;
		bool ok = false;
		x1_receive(const std::vector<u8>& run, const std::vector<u8>& wrapper, const std::vector<u8>& chop)
		{
			ftol.put(0, wrapper);
			ftol.put(0x40, chop);
			ftol.rel(0x0b, ftol.va(0x40)); // 0x1a41fa: call 0x3439c4
			ftol.flush();
			code.put(0, run);
			code.abs(0x0c, &zero);
			code.abs(0x1d, &zero);
			code.abs(0x3d, &zero);
			code.rel(0x22, ftol.va());
			code.rel(0x33, ftol.va());
			code.put(0x47, {0xc3});
			code.flush();
			ok = run.size() == 0x47 && run[0x21] == 0xe8 && run[0x32] == 0xe8 && wrapper.size() == 0x13 && wrapper[0x0a] == 0xe8;
		}
		std::int32_t operator()(std::int32_t damage, float defense, float scale)
		{
			std::array<u8, 0x100> record{};
			std::memcpy(record.data() + 0x94, &damage, 4);
			entry = code.at;
			receive_run(record.data(), defense, scale);
			std::int32_t out = 0;
			std::memcpy(&out, record.data() + 0x94, 4);
			return out;
		}
	};

	bool frame_kept() { return kept_ebx == 0x13572468 && kept_edi == 0x2468ace0 && (sw_before & 0x3800) == (sw_after & 0x3800); }
}

int main(int argc, char** argv)
{
	std::printf("[Balance] ReceiveRounding / DamageCap: XML1 0x45517 / 0x5ce0d against XMen2.exe 0x437a54 / 0x453bfb\n");

	// The values.
	check(parse("xml1").xml1 && parse(" XML1 ; the first game's").xml1 && parse("Xml2").set && !parse("xml2").xml1 && !parse("").set && parse("").error.empty(),
	      "ReceiveRounding / DamageCap: xml1, xml2 and absent");
	check(!parse("on").xml1 && parse("on").error == "isn't xml1 or xml2" && parse("1").set && !parse("1").error.empty(), "any other value is refused (unverified)");
	// The arithmetic and the reviewer's example.
	check(receive_xml1(10, 0.5f, 0.5f) == 4 && receive_xml2(10.0f, 0.5f, 0.5f) == 4.75 && receive_patched(10.0f, 0.5f, 0.5f) == 4,
	      "10, defense 0.5, scale 0.5: XML1 4, XMen2.exe 4.75, the rule 4");
	check(receive_xml1(3, 5.0f, 2.0f) == 0 && receive_xml1(7, 0.0f, 1.0f) == 7 && receive_patched(9.7f, 0.5f, 1.0f) == 8 && xml1_ftol(-2.5) == -2 &&
	          xml1_ftol(std::numeric_limits<double>::quiet_NaN()) == 0,
	      "the clamp, an identity scale, a fractional damage truncated first, toward zero, NaN is 0");
	check(capped(31999.5f, xml1_damage_cap) == 31999.5f && capped(32000.0f, xml1_damage_cap) == 32000.0f && capped(32000.5f, xml1_damage_cap) == 32000.0f &&
	          capped(2.0e6f, xml2_damage_cap) == 1.0e6f,
	      "the caps: only a greater value is replaced");
	// The writes: none without a key; two calls (6 bytes each) for the receive, two operands for the cap.
	{
		const address base = 0x10001000;
		const layout l{base};
		check(writes_for(0, base).empty() && writes_for(receive_rounding, base).size() == 2 && writes_for(damage_cap, base).size() == 2 &&
		          writes_for(receive_rounding | damage_cap, base).size() == 4,
		      "OFF writes nothing; each key its own writes");
		const auto r = writes_for(receive_rounding, base), c = writes_for(damage_cap, base);
		const auto target = [](const site_write& w) { u32 rel = 0; std::memcpy(&rel, w.after.data() + 1, 4); return w.va + 5 + rel; };
		check(r[0].va == 0x437a54 && r[0].before == std::vector<u8>({0xd9, 0x86, 0x9c, 0, 0, 0}) && r[0].after[0] == 0xe8 && target(r[0]) == l.load() && r[0].after[5] == 0x90 &&
		          r[1].va == 0x437a75 && r[1].before == std::vector<u8>({0xd8, 0x4c, 0x24, 0x10, 0x8b, 0x0e}) && target(r[1]) == l.scale() && r[1].after[5] == 0x90,
		      "the receive: call the load stub at 0x437a54, the scale stub at 0x437a75");
		u32 operand_a = 0, operand_b = 0;
		std::memcpy(&operand_a, c[0].after.data(), 4);
		std::memcpy(&operand_b, c[1].after.data(), 4);
		check(c[0].va == 0x453bfd && c[1].va == 0x453c0c && c[0].before == std::vector<u8>({0xf4, 0x5b, 0x68, 0x00}) && c[1].before == c[0].before &&
		          operand_a == l.cap() && operand_b == l.cap(),
		      "the cap: both operands of 0x685bf4 point at the block's 32000.0f");
		const auto b = block(base);
		float cap = 0;
		std::memcpy(&cap, b.data() + (l.cap() - base), 4);
		check(b.size() == block_size && cap == 32000.0f && trunc_helper().size() == trunc_size && load_stub(l).size() == load_size && scale_stub(l).size() == scale_size &&
		          l.trunc() + trunc_size <= l.load() && l.load() + load_size <= l.scale() && l.scale() + scale_size <= l.cap(),
		      "the block: helper, stubs and constant apart");
		check(agreement_values(0) == std::array<u32, 4>{0, 0, 0, 0} && agreement_values(receive_rounding | damage_cap) == std::array<u32, 4>{1, 32000, 0, 0} &&
		          gameplay_capabilities::balance_parity_id == 41 && std::string(gameplay_capabilities::rule_name(41)) == "ReceiveRounding/DamageCap" &&
		          gameplay_capabilities::bucket_of(41) != gameplay_capabilities::bucket_of(30),
		      "rule 41: {receive installed, cap in effect, 0, 0}, named, its own bucket");
	}

	if (argc < 2)
	{
		check(false, "XMen2.exe path required");
		std::printf("Balance parity: %d checks, %d failures\n", checks, failures);
		return 1;
	}
	const auto exe = read_file(argv[1]);
	const auto pc_receive_bytes = pe_bytes(exe, receive_site.va, static_cast<u32>(limits_rules::hex_size(receive_site.hex)));
	const auto pc_cap_bytes = pe_bytes(exe, cap_site.va, static_cast<u32>(limits_rules::hex_size(cap_site.hex)));
	const auto pc_cap_value = pe_bytes(exe, cap_constant_site.va, 4);
	const bool pc_ok = pc_receive_bytes && limits_rules::matches(pc_receive_bytes->data(), receive_site.hex) && pc_cap_bytes &&
	                   limits_rules::matches(pc_cap_bytes->data(), cap_site.hex) && pc_cap_value && limits_rules::matches(pc_cap_value->data(), cap_constant_site.hex);
	check(pc_ok, "XMen2.exe: the receive run, the cap run and the cap constant are the guarded retail bytes");
	if (!pc_ok)
	{
		std::printf("Balance parity: %d checks, %d failures\n", checks, failures);
		return 1;
	}
	float retail_cap = 0;
	std::memcpy(&retail_cap, pc_cap_value->data(), 4);

	pc_receive retail(false), rule(true);
	// The reviewer's example on XMen2.exe's own code, retail and with the rule; registers, x87 depth, control word.
	{
		std::uint16_t cw_before = 0;
		__asm { fnstcw cw_before }
		const float r = retail(10.0f, 0.5f, 0.5f);
		check(r == 4.75f && frame_kept(), "XMen2.exe retail: 10, defense 0.5, scale 0.5 -> 4.75");
		const float p = rule(10.0f, 0.5f, 0.5f);
		check(p == 4.0f && frame_kept() && cw_after == cw_before, "the rule on XMen2.exe's code: 4, registers, x87 depth and control word kept");
	}

	// The cap, XMen2.exe's run retail and with the rule's operands.
	page pc_cap_code, pc_cap_rule, stubs;
	pc_cap_code.put(0, *pc_cap_bytes);
	pc_cap_code.abs(0x02, &retail_cap);
	pc_cap_code.abs(0x11, &retail_cap);
	pc_cap_code.put(0x15, {0xc3});
	pc_cap_code.flush();
	stubs.put(0, block(stubs.va()));
	stubs.flush();
	pc_cap_rule.put(0, *pc_cap_bytes);
	const layout sl{stubs.va()};
	pc_cap_rule.put(0x02, operand(sl.cap()));
	pc_cap_rule.put(0x11, operand(sl.cap()));
	pc_cap_rule.put(0x15, {0xc3});
	pc_cap_rule.flush();
	const auto pc_cap = [&](page& p, float v) { entry = p.at; return cap_run_pc(v); };
	check(pc_cap(pc_cap_code, 2.0e6f) == 1.0e6f && pc_cap(pc_cap_code, 40000.0f) == 40000.0f && pc_cap(pc_cap_rule, 40000.0f) == 32000.0f &&
	          pc_cap(pc_cap_rule, 31999.5f) == 31999.5f && pc_cap(pc_cap_rule, -5.0f) == -5.0f,
	      "XMen2.exe's cap: retail 1,000,000, with the rule 32000");

	if (argc < 3)
	{
		std::printf("  skip  XML1 comparison not requested (supply the owned default.xbe as the second path)\n");
		std::printf("Balance parity: %d checks, %d failures\n", checks, failures);
		return failures ? 1 : 0;
	}
	const auto xbe = read_file(argv[2]);
	const auto x1_run = xbe_bytes(xbe, 0x45517, 0x47), x1_wrapper = xbe_bytes(xbe, 0x1a41f0, 0x13), x1_chop = xbe_bytes(xbe, 0x3439c4, 0x27),
	           x1_cap = xbe_bytes(xbe, 0x5ce0d, 12), x1_gate = xbe_bytes(xbe, 0x44579, 0x1c);
	const bool x1_ok = identity(x1_run, "526cb254de86c0e918417c08aefb2967106626c8c1462cdc8e0f008b7c574425") &&
	                   identity(x1_wrapper, "005338f55c62ac3e78bdfe923016669cf24222b4e34503543dbdc77908d7c6b7") &&
	                   identity(x1_chop, "d0a2aca68648a0ae9d5cbfae1b276d4a64d4f797fc0c60f28b64096cdfa3148c") &&
	                   identity(x1_cap, "877d6c32e4e58fe1f821f4f9ff2bea322fe3a62a5023a6e2d525d0c3ac5f629d");
	check(x1_ok, "default.xbe: the receive run, its _ftol pair and the cap run (SHA-256)");
	if (!x1_ok)
	{
		std::printf("Balance parity: %d checks, %d failures\n", checks, failures);
		return 1;
	}
	x1_receive original(*x1_run, *x1_wrapper, *x1_chop);
	check(original.ok, "XML1's run relocated (three 0.0 operands, two _ftol calls)");
	check(original(10, 0.5f, 0.5f) == 4 && frame_kept(), "XML1's own code: 10, defense 0.5, scale 0.5 -> 4");

	// Side by side, in both precision controls the game may run with (the D3D8 device's 24-bit one and 53-bit):
	// whole-number damage through XML1's run and XMen2.exe's patched run agree on every case; XMen2.exe's retail run
	// differs wherever a stage is fractional. A fractional XMen2.exe damage is XML1's integer field: truncated first.
	const float defenses[] = {0.0f, 0.5f, 1.0f, 0.3f, 2.5f, 7.25f, 0.1f, 1.75f, 12.0f, 0.999f};
	const float scales[] = {1.0f, 0.5f, 0.75f, 0.3f, 0.1f, 0.7f, 0.9f, 1.5f, 2.0f, 0.25f, 0.6f, 0.333f};
	for (const unsigned precision : {static_cast<unsigned>(_PC_24), static_cast<unsigned>(_PC_53)})
	{
		unsigned int before = 0, ignored = 0;
		_controlfp_s(&before, 0, 0);
		_controlfp_s(&ignored, precision, _MCW_PC);
		int agree = 0, total = 0, retail_differs = 0, fractional_ok = 0, fractional = 0;
		bool frames = true;
		for (int damage = 0; damage <= 600; ++damage)
			for (const float defense : defenses)
				for (const float scale : scales)
				{
					const std::int32_t x1 = original(damage, defense, scale);
					frames &= frame_kept();
					const float patched = rule(static_cast<float>(damage), defense, scale);
					frames &= frame_kept();
					const float stock = retail(static_cast<float>(damage), defense, scale);
					++total;
					agree += patched == static_cast<float>(x1);
					retail_differs += stock != static_cast<float>(x1);
					if (damage % 7 == 0)
					{
						const float fraction = static_cast<float>(damage) + 0.625f;
						++fractional;
						fractional_ok += rule(fraction, defense, scale) == static_cast<float>(original(damage, defense, scale));
					}
				}
		for (const std::int32_t damage : {1000, 4999, 12345, 31999, 32000})
			for (const float scale : scales)
			{
				++total;
				agree += rule(static_cast<float>(damage), 0.5f, scale) == static_cast<float>(original(damage, 0.5f, scale));
			}
		_controlfp_s(&ignored, before, _MCW_PC);
		char line[200];
		std::snprintf(line, sizeof line, "precision %s: the rule equals XML1 on %d / %d cases (retail differs on %d), fractional damage %d / %d",
		              precision == static_cast<unsigned>(_PC_24) ? "24-bit" : "53-bit", agree, total, retail_differs, fractional_ok, fractional);
		std::printf("  %s\n", line);
		check(agree == total && fractional_ok == fractional && retail_differs > 0 && frames, line);
	}

	// The cap side by side: XML1's integer compare on whole numbers against the rule's float compare.
	{
		page x1_cap_code;
		x1_cap_code.put(0, *x1_cap);
		x1_cap_code.put(12, {0xc3});
		x1_cap_code.flush();
		bool same = true, retail_differs = false;
		for (const std::int32_t v : {-40000, -1, 0, 1, 31999, 32000, 32001, 40000, 999999, 1000000, 1000001, 2000000, 0x7fffff})
		{
			entry = x1_cap_code.at;
			const int x1 = cap_run_x1(v);
			same &= pc_cap(pc_cap_rule, static_cast<float>(v)) == static_cast<float>(x1);
			retail_differs |= pc_cap(pc_cap_code, static_cast<float>(v)) != static_cast<float>(x1);
		}
		check(same && retail_differs, "the cap: the rule equals XML1's 32000 on every whole number; retail differs above it");
	}
	// XML1's hit gate between 0x44579 and 0x44595 (item 22 of rule 16, for the record): the move's power check and
	// nothing reading the game mode.
	check(identity(x1_gate, "c750012ca76f237a43f39ae2bc3183de6109781c50fc59d7d6b2140883e7ad21"), "default.xbe 0x44579..0x44595: no Danger Room gate");

	std::printf("Balance parity: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
