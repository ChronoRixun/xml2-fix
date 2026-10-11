#pragma once

// [Save] Format=lc: the LC save format's engine side (save_format.cpp), kept apart from the patching so xml2_test
// can check it without the game: the key's rule, every retail byte the hooks rely on, the folder and file names,
// the container header as the slot list reads it, the import ledger, the stream flag stubs, the two bridges, and
// the old stream's hero blocks (where a missing talent is inserted).
//
// What the game does (SPEC-old-save 1.1, 2.10, 2.11; addresses re-read from the retail XMen2.exe, image base
// 0x400000, no relocations): CMemoryCardWin32 (object 0x6e03d8, vtable 0x69aa8c) is the only thing that touches
// save files. CSaveLoad calls its slots: +0x5c enumerate (device, first slot, record array, count) fills ten
// 0xa8-byte slot records from the 0x80-byte descriptions of Save\saveslot<N>.save; +0x64 write slot (device,
// slot, description, size, data, 0) writes description + u32 length + the 0x2fc00-byte stream the game's
// serializer (CGame vt+0x208, 0x46baf0) filled; +0x6c read slot (device, slot, u32* size, data) reads the stream
// back for the loader (CGame vt+0x20c, 0x46e2b0); +0x54 / +0x58 do the same for the 0x228-byte profile
// (settings.dat); +0x70 deletes a slot (device, slot), +0x28 everything (device), +0x2c says whether a profile
// exists (device), +0x74 formats the message the menu shows for a result code (code, device, argument). All
// __thiscall with the stack cleaned by the callee (ret 4 / 8 / 0xc / 0x10 / 0x18), bool results in al (the card's
// own return value is a dword whose low byte the callers test).
//
// The change (SPEC-new-save 4.1, D1, D10, D11): with [Save] Format=lc those nine slots point at the fix's own
// methods, which keep the engine's stream exactly as it is and translate it at the disk: a read of a slot gets the
// stream the library rebuilds from SaveLC\saveslot<N>.lcsave, a write goes through the library into a .lcsave
// written atomically (1.6), the slot list comes from the .lcsave files' META sections, and the old Save folder is
// never written again. At the first use, the library's bind tables are loaded and verified (3.5) and the old
// saves are converted once (5.6 B, with the ledger SaveLC\legacy-import.txt); when the tables fail, nothing is
// read or written in either folder and the slot list carries the message (D11).

#include "limits_rules.hpp" // guard, matches, hex_size, image_base

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace save_format_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;
	using address = DWORD;

	// ---- [Save] Format -------------------------------------------------------------------------------

	enum class mode
	{
		off, // no key, or the game's own format named: nothing of this file is active
		lc,  // the LC format
	};

	struct parsed_mode
	{
		bool set = false;
		mode value = mode::off;
		std::string error; // a value that is neither: the key is refused (and logged); the game's own format stays
	};

	// "lc" (any case) = the LC format; "off", "xml2" or no value = the game's own; anything else is refused.
	inline parsed_mode parse_mode(const std::string_view text)
	{
		parsed_mode out;
		const auto value = limits_rules::value_text(text);
		if (value.empty())
		{
			return out;
		}
		std::string lower(value);
		for (auto& c : lower)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		out.set = true;
		if (lower == "lc")
		{
			out.value = mode::lc;
		}
		else if (lower == "off" || lower == "xml2")
		{
			out.value = mode::off;
		}
		else
		{
			out.set = false;
			out.error = "isn't lc, off or xml2";
		}
		return out;
	}

	// ---- The retail bytes every hook relies on -------------------------------------------------------

	constexpr address card_vtable = 0x69aa8c;     // CMemoryCardWin32's vtable
	constexpr address card_object = 0x6e03d8;     // the one card (its getter 0x55e9a0 returns it)
	constexpr address card_folder = 0x7ad6a0;     // char[0x104]: "<Documents>\Activision\<folder>\Save\" once vt+0 (0x55e760) ran
	constexpr address card_init = 0x55e760;       // vt+0: fills card_folder (SHGetFolderPathA, else "Save\")
	constexpr address card_message_text = 0x17c;  // card+0x17c: char[0x1a0], the text vt+0x74 formats and returns
	constexpr std::size_t card_message_size = 0x1a0;
	constexpr address game_vtable = 0x686e1c;     // CGame's vtable
	constexpr address game_serialize = 0x46baf0;  // vt+0x208: fills the stream buffer
	constexpr address game_load = 0x46e2b0;       // vt+0x20c (buffer, mode)
	constexpr std::size_t stream_capacity = 0x2fc00;     // the payload of a slot, and its wrapper's cursor offset (retail)
	constexpr std::size_t stream_wrapper = 0x2fc78;      // the buffer the game allocates (retail)
	constexpr std::size_t stream_description = 0x2fc04;  // the serializer's description text in the wrapper (0x40) (retail)

	// The native save layout the engine runs with: retail, or the Phase A expanded layout E10A2 (docs/
	// entity-capacity-phase-a-plan.md section 6; round6 layouts.json native_save). Every hook, bridge and buffer
	// size of the save seam derives from the installed profile; nothing infers a layout from a file or packet size.
	struct native_profile
	{
		const char* name;
		std::uint32_t states;          // zone state records per zone record, sentinel included (bound at 0x466659 / 0x4666e7)
		std::uint32_t ordinals;        // ordinal map entries (u16 each) per zone record (bound at 0x46667a)
		std::uint32_t dynamic;         // dynamic list entries (bound at 0x46668a)
		std::uint32_t smart;           // smart list entries (bound at 0x4666f3)
		std::uint32_t state_count;     // record offset of the state count dword
		std::uint32_t ordinal_map;     // record offset of the ordinal map
		std::uint32_t time_stamp;      // record offset of the time stamp float
		std::uint32_t record_size;     // one zone record
		std::uint32_t manager_size;    // the five records plus the manager's own fields (CMap+0x3b0 allocation)
		std::uint32_t usable_end;      // what the stream helpers compare their cursor against
		std::uint32_t payload;         // the slot payload the card reads and writes
		std::uint32_t wrapper;         // the buffer the game allocates around the payload
		std::uint32_t description;     // the serializer's description text in the wrapper
		std::uint32_t flag_offset;     // the wrapper's overflow/flag byte (retail 0x2fc44)
	};
	inline constexpr native_profile retail_profile{"retail", 768, 256, 300, 768, 0x4808, 0x480c, 0x55d8, 0x55e0, 0x1afdc, 0x2fc00, 0x2fc00, 0x2fc78, 0x2fc04, 0x2fc44};
	inline constexpr native_profile e10a2_profile{"E10A2", 4096, 1024, 1024, 1024, 0x18008, 0x1800c, 0x19b80, 0x19b88, 0x80c24, 0x200000, 0x200000, 0x200078, 0x200004, 0x200044};
	static_assert(retail_profile.payload == stream_capacity && retail_profile.wrapper == stream_wrapper && retail_profile.description == stream_description);
	static_assert(4 + e10a2_profile.states * 0x18 + 4 + 4 + e10a2_profile.ordinals * 2 + 4 + e10a2_profile.dynamic * 2 + 4 + 4 + e10a2_profile.smart * 2 + 4 + 4 + 0x34c + 20 + 4 + 4 == e10a2_profile.record_size);
	static_assert(5 * e10a2_profile.record_size + (0x1afdc - 5 * 0x55e0) == e10a2_profile.manager_size);
	static_assert(e10a2_profile.wrapper - e10a2_profile.payload == stream_wrapper - stream_capacity);
	constexpr std::size_t profile_size = 0x228;
	constexpr std::size_t old_envelope = 0x84;           // 0x80 description + u32 length
	constexpr std::size_t old_slot_file_size = old_envelope + stream_capacity;
	constexpr std::size_t old_profile_file_size = old_envelope + profile_size;
	constexpr int slot_count = 10;
	constexpr std::size_t slot_record_size = 0xa8;       // CSaveLoad's slot records: description[0x80], size text[0x20], u32 size, u8 empty, u8 corrupt
	constexpr std::size_t slot_record_size_text = 0x80;
	constexpr std::size_t slot_record_file_size = 0xa0;
	constexpr std::size_t slot_record_empty = 0xa4;
	constexpr std::size_t slot_record_corrupt = 0xa5;

	// The card's vtable slots the fix replaces, with their retail targets.
	struct slot
	{
		address offset;
		address retail;
		const char* what;
	};

	inline constexpr std::array<slot, 9> card_slots{{
		{0x28, 0x55f6f0, "delete every file of the save folder (device)"},
		{0x2c, 0x55f850, "profile exists (device)"},
		{0x54, 0x55f9a0, "write profile (device, size, data)"},
		{0x58, 0x55fcb0, "read profile (device, u32* size, data)"},
		{0x5c, 0x55f2f0, "enumerate slots (device, first, records, count)"},
		{0x64, 0x55fe70, "write slot (device, slot, description, size, data, 0)"},
		{0x6c, 0x55ff00, "read slot (device, slot, u32* size, data)"},
		{0x70, 0x55e930, "delete slot (device, slot)"},
		{0x74, 0x55e9b0, "message text (code, device, argument)"},
	}};
	constexpr slot game_load_slot{0x20c, game_load, "CGame load (buffer, mode)"};

	// The stream helpers' skip branches (SPEC-old-save 2.10; SPEC-new-save 4.5 rows 1-2): `cmp r32, 0x2fc00` then
	// `jge skip`. Each becomes a jump to a stub that repeats the compare, continues when the copy fits and otherwise
	// sets the fix's overflow flag before taking the skip. At 0x468a85 a `pop esi` sits between the two.
	struct flag_site
	{
		address va;
		std::string_view hex; // the retail bytes replaced (the compare, any instruction between, the jge rel8)
		const char* what;
	};

	inline constexpr std::array<flag_site, 12> flag_sites{{
		{0x468903, "81f900fc02007d26", "write primitive 0x4688f0: the copy is skipped when it doesn't fit"},
		{0x468953, "81f900fc02007d26", "read primitive 0x468940: the copy is skipped when it doesn't fit"},
		{0x4689cd, "81fa00fc02007d0e", "tag writer 0x468990: first marker byte"},
		{0x4689f0, "81fa00fc02007d10", "tag writer 0x468990: the 50 text bytes"},
		{0x468a13, "81f900fc02007d0e", "tag writer 0x468990: last marker byte"},
		{0x468a3d, "81fa00fc02007d07", "tag reader 0x468a30: first marker byte"},
		{0x468a5a, "81f900fc02007d18", "tag reader 0x468a30: the 50 text bytes"},
		{0x468a85, "81fa00fc02005e7d07", "tag reader 0x468a30: last marker byte (pop esi between compare and branch)"},
		{0x46bcb6, "3d00fc02007d17", "CGame save 0x46baf0: the inline copy of the 0xdc game block"},
		{0x46bcf0, "81f900fc02007d13", "CGame save 0x46baf0: the inline copy of the 0x4e0 side-mission stack"},
		{0x46e3ac, "3d00fc02007d22", "CGame load 0x46e2b0: the inline copy of the 0xdc game block"},
		{0x46e408, "81f900fc02007d1e", "CGame load 0x46e2b0: the inline copy of the 0x4e0 side-mission stack"},
	}};

	// The zone loader's placement file (SPEC-new-save 3.8, N14 c): CMap::load (0x484ce0) opens the zone's own XMLB
	// through the reader 0x564eb0 at 0x485689 (path "maps/<zone>" built by 0x486360; the package or the loose
	// .engb / .xmlb), and the reader converts the bytes in place at 0x565150 (`call 0x564d30` with the reader in
	// ecx, its buffer at [reader], and the byte count pushed) - the one moment the file's own bytes are in memory.
	constexpr address zone_open_call = 0x485689;          // call 0x564eb0 in CMap::load
	constexpr std::string_view zone_open_call_hex = "e822f80d00";
	constexpr address xmlb_reader_open = 0x564eb0;
	constexpr address xmlb_convert_call = 0x565150;       // push eax; mov ecx, esi; call 0x564d30
	constexpr std::string_view xmlb_convert_call_hex = "508bcee8d8fbffff";
	constexpr address xmlb_convert = 0x564d30;
	constexpr address xmlb_convert_call_insn = 0x565153;  // the call itself (5 bytes)

	// Every byte of XMen2.exe the hooks rely on, read from the retail build. All must match before anything is
	// written; xml2_test compares them with a copy of the exe.
	inline constexpr std::array<guard, 34> guards{{
		{0x46a690, "8b4424040fb68408e0050000c20400", "trivia cursor getter: unbounded per-act byte table"},
		{0x46a6a0, "8a4424088b54240488840ae0050000c20800", "trivia cursor setter: unbounded per-act byte table"},
		{0x469e10, "8b4424046bc032", "trivia getter: two stack arguments, act stride"},
		{0x469e22, "85d27c2a81fafa0000007d22", "trivia getter: signed 0..249 bound"},
		{0x469e3d, "8b8c96e8050000", "trivia getter: game bitmap at +0x5e8"},
		{0x469e4d, "c20800", "trivia getter: callee pops two arguments"},
		{0x469e60, "8b4424048b542408", "trivia setter: two stack arguments"},
		{0x469e6f, "85c07c213dfa0000007d1a", "trivia setter: signed 0..249 bound"},
		{0x469e7f, "8d9491e8050000", "trivia setter: game bitmap at +0x5e8"},
		{0x469e94, "c20800", "trivia setter: callee pops two arguments"},
		{0x55f190, "81ec48020000", "native campaign-file count used by device info and Load Game"},
		{card_vtable + 0x28, "f0f6550050f8550010f9550040e8550050e8550040e8550060e8550070e8550080e8550090e85500f0f35100a0f95500b0fc5500f0f2550000e9550070fe5500207c510000ff550030e95500b0e95500",
		 "CMemoryCardWin32's vtable slots +0x28..+0x74 (0x69aab4)"},
		{0x6e03d0, "7caa69006caa6900", "the slot extension and profile name pointers (0x6e03d0: \".save\", 0x6e03d4: \"settings.dat\")"},
		{0x69aa7c, "2e7361766500", "\".save\" (0x69aa7c)"},
		{0x69aa6c, "73657474696e67732e64617400", "\"settings.dat\" (0x69aa6c)"},
		{card_init, "e86bb10c0085c07466e862b10c00baa8d77a008a0840880a4284c975f6a02d3c", "the card's init 0x55e760: the save folder into 0x7ad6a0"},
		{0x55e9b0, "8b44240483f823568bf10f87b4000000", "the message method 0x55e9b0 (code, device, argument) -> card+0x17c"},
		{0x4b074a, "e851e20a008b106a0a8d8ee4000000510fbe8edc0000006a00518bc8ff525c84",
		 "CSaveLoad's slot list (0x4b074a): ten 0xa8-byte records at +0xe4 through card vt+0x5c"},
		{0x4b1751, "8b8ee00000008b106a005181c104fc02006800fc0200510fbe8edd000000510fbe8edc000000518bc8ff5264",
		 "CSaveLoad's slot write (0x4b1751): description at buffer+0x2fc04, 0x2fc00 bytes, through card vt+0x64"},
		{0x4b18be, "c744241000fc0200e8d5d00a008b8ee00000008b10518d4c2414510fbe8edd000000510fbe8edc000000518bc8ff526c",
		 "CSaveLoad's slot read (0x4b18be): capacity 0x2fc00 through card vt+0x6c"},
		{0x4b15f9, "e8a2d30a000fbe8edd0000008b10510fbe8edc000000518bc8ff527084c00f84fa010000",
		 "CSaveLoad's overwrite (0x4b15f9): the slot deleted through card vt+0x70 before the write, returning to 0x4b1615"},
		{0x4b2897, "b9c0cb7500891538d37500e839d0ffff", "the one CSaveLoad (0x4b2897): 0x75cbc0 built by 0x4af8e0"},
		{0x4af8e0, "8bc133c9c7008cdb68008848048888840000008888a40000008988d40000008988d8000000", "CSaveLoad's constructor 0x4af8e0: its mode dword +0xd4"},
		{0x4af7c1, "ff505484c0752ae8d3f10a000fbe8edc0000008b106a00516a068bc8ff5274", "the menu's profile write (0x4af7c1): vt+0x54, else message 6 through vt+0x74"},
		{0x4b154c, "83bed400000004", "the save sequence's mode test (0x4b154c): +0xd4 == 4 saves"},
		{game_vtable + 0x208, "f0ba4600b0e24600", "CGame's vtable: vt+0x208 = 0x46baf0 serialize, vt+0x20c = 0x46e2b0 load"},
		{game_serialize, "81ec1c010000a1f8386f005355565789", "CGame serialize 0x46baf0"},
		{game_load, "5355578be98b45006a016a01ff904801", "CGame load 0x46e2b0"},
		{0x4688f0, "8b5424088bc1578bb800fc02008bcf2bc803ca81f900fc02007d26", "the write primitive 0x4688f0"},
		{0x468940, "8b5424088bc1568bb000fc02008bce2bc803ca81f900fc02007d26", "the read primitive 0x468940"},
		{zone_open_call, "e822f80d00", "CMap::load opens the zone's placement file through 0x564eb0 (0x485689)"},
		{xmlb_convert_call, "508bcee8d8fbffff", "the XMLB reader converts the file's bytes in place (0x565150: call 0x564d30 with the size pushed)"},
		{xmlb_convert, "8b118d4208538b18", "the in-place XMLB converter 0x564d30"},
		{xmlb_reader_open, "81ec08050000a1f8", "the XMLB reader's open 0x564eb0"},
	}};

	// The first guard `image` (XMen2.exe's image base: 0x400000 in the game) doesn't match, or null.
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
		{
			if (!limits_rules::matches(image + (g.va - image_base), g.hex))
			{
				return &g;
			}
		}
		for (const auto& s : flag_sites)
		{
			if (!limits_rules::matches(image + (s.va - image_base), s.hex))
			{
				static guard failed{0, "", ""};
				failed = guard{s.va, s.hex, s.what};
				return &failed;
			}
		}
		return nullptr;
	}

	// ---- Code the fix writes ---------------------------------------------------------------------------

	inline void put32(std::vector<std::uint8_t>& out, const std::uint32_t v)
	{
		out.push_back(static_cast<std::uint8_t>(v));
		out.push_back(static_cast<std::uint8_t>(v >> 8));
		out.push_back(static_cast<std::uint8_t>(v >> 16));
		out.push_back(static_cast<std::uint8_t>(v >> 24));
	}

	// jmp / call rel32 from `from` (the instruction's address) to `to`, padded with nops to `length`.
	inline std::vector<std::uint8_t> rel32(const std::uint8_t opcode, const address from, const address to, const std::size_t length)
	{
		std::vector<std::uint8_t> out{opcode};
		put32(out, static_cast<std::uint32_t>(to - (from + 5)));
		while (out.size() < length)
		{
			out.push_back(0x90);
		}
		return out;
	}

	inline std::vector<std::uint8_t> jump(const address from, const address to, const std::size_t length)
	{
		return rel32(0xe9, from, to, length);
	}

	inline std::vector<std::uint8_t> call(const address from, const address to, const std::size_t length)
	{
		return rel32(0xe8, from, to, length);
	}

	// The stub one flag site jumps to, at `stub`: the retail compare (and the pop esi of 0x468a85), `jl resume`
	// when the copy fits, else `mov byte [flag], 1` and `jmp skip` - the retail jge's own target. With a `bound`
	// other than the retail 0x2fc00 the copied compare takes that immediate (the expanded stream's usable end).
	inline std::vector<std::uint8_t> flag_stub(const flag_site& site, const address stub, const address flag, const std::uint32_t bound = stream_capacity)
	{
		std::vector<std::uint8_t> out;
		const std::size_t length = limits_rules::hex_size(site.hex);
		for (std::size_t i = 0; i + 2 < length; ++i) // everything but the jge rel8
		{
			out.push_back(limits_rules::hex_byte(site.hex, i));
		}
		// The compare's imm32 sits after the opcode (3d) or the opcode and modrm (81 f9 / 81 fa); 0x468a85 also carries
		// the pop esi after it. The retail immediate is 0x2fc00 at every site; replace it with the bound.
		const std::size_t imm = out[0] == 0x3d ? 1 : 2;
		std::uint32_t current = 0;
		std::memcpy(&current, out.data() + imm, 4);
		if (current != stream_capacity) return {};
		std::memcpy(out.data() + imm, &bound, 4);
		const auto rel8 = static_cast<std::int8_t>(limits_rules::hex_byte(site.hex, length - 1));
		const address resume = site.va + static_cast<address>(length);
		const address skip = resume + static_cast<address>(rel8);
		// jl rel32 (0f 8c) to resume
		const address jl_at = stub + static_cast<address>(out.size());
		out.push_back(0x0f);
		out.push_back(0x8c);
		put32(out, static_cast<std::uint32_t>(resume - (jl_at + 6)));
		// mov byte ptr [flag], 1
		out.push_back(0xc6);
		out.push_back(0x05);
		put32(out, flag);
		out.push_back(0x01);
		// jmp skip
		const address jmp_at = stub + static_cast<address>(out.size());
		const auto j = jump(jmp_at, skip, 5);
		out.insert(out.end(), j.begin(), j.end());
		return out;
	}

	// The bridge the XMLB converter call goes through (at 0x565153 `call 0x564d30` becomes `call bridge`): the
	// reader (ecx) and the byte count ([esp + 4] at the bridge's entry, above the return address) go to the fix's
	// __cdecl hook(reader, size) with every register and the flags kept; then the converter itself.
	inline std::vector<std::uint8_t> xmlb_bridge(const address bridge, const address hook)
	{
		std::vector<std::uint8_t> out{
			0x9c,                               // pushfd
			0x60,                               // pushad
			0x8b, 0x44, 0x24, 0x28,             // mov eax, [esp + 0x28]  (0x20 pushad + 4 pushfd + 4 return address)
			0x50,                               // push eax               (size)
			0x51,                               // push ecx               (reader)
		};
		const address call_at = bridge + static_cast<address>(out.size());
		const auto c = call(call_at, hook, 5);
		out.insert(out.end(), c.begin(), c.end());
		out.push_back(0x83); out.push_back(0xc4); out.push_back(0x08); // add esp, 8
		out.push_back(0x61);                                           // popad
		out.push_back(0x9d);                                           // popfd
		const address jmp_at = bridge + static_cast<address>(out.size());
		const auto j = jump(jmp_at, xmlb_convert, 5);
		out.insert(out.end(), j.begin(), j.end());
		return out;
	}

	// The bridge the zone's open goes through (at 0x485689 `call 0x564eb0` becomes `call bridge`): the fix's
	// flag byte is 1 while the reader runs, so the XMLB bridge above knows these bytes are the zone's. The return
	// address is parked in `saved` (the loader runs on one thread), the reader cleans its own arguments (ret 0x10).
	inline std::vector<std::uint8_t> zone_open_bridge(const address bridge, const address flag, const address saved)
	{
		std::vector<std::uint8_t> out{0x58};                 // pop eax (the return address)
		out.push_back(0xa3); put32(out, saved);              // mov [saved], eax
		out.push_back(0xc6); out.push_back(0x05); put32(out, flag); out.push_back(0x01); // mov byte [flag], 1
		const address call_at = bridge + static_cast<address>(out.size());
		const auto c = call(call_at, xmlb_reader_open, 5);
		out.insert(out.end(), c.begin(), c.end());
		out.push_back(0xc6); out.push_back(0x05); put32(out, flag); out.push_back(0x00); // mov byte [flag], 0
		out.push_back(0xff); out.push_back(0x35); put32(out, saved);                     // push [saved]
		out.push_back(0xc3);                                                              // ret
		return out;
	}

	// ---- Folder and file names (SPEC-new-save 1.5) --------------------------------------------------

	constexpr const char* lc_folder_name = "SaveLC";
	constexpr const char* profile_file = "profile.lcsave";
	constexpr const char* ledger_file = "legacy-import.txt";
	constexpr const char* old_profile_file = "settings.dat";

	// "<...>\Activision\<folder>\Save\" (what the card's init builds, or "Save\" without a Documents folder) ->
	// "<...>\Activision\<folder>\SaveLC\"; nothing when the folder doesn't end in "Save\" or "Save/".
	inline std::optional<std::string> lc_folder(const std::string_view old_folder)
	{
		if (old_folder.size() < 5)
		{
			return std::nullopt;
		}
		const char sep = old_folder.back();
		if (sep != '\\' && sep != '/')
		{
			return std::nullopt;
		}
		const auto stem = old_folder.substr(0, old_folder.size() - 1);
		if (stem.size() < 4 || stem.substr(stem.size() - 4) != "Save")
		{
			return std::nullopt;
		}
		if (stem.size() > 4 && stem[stem.size() - 5] != '\\' && stem[stem.size() - 5] != '/')
		{
			return std::nullopt;
		}
		return std::string(stem.substr(0, stem.size() - 4)) + lc_folder_name + sep;
	}

	inline std::string slot_file(const int slot)
	{
		return "saveslot" + std::to_string(slot) + ".lcsave";
	}

	inline std::string old_slot_file(const int slot)
	{
		return "saveslot" + std::to_string(slot) + ".save";
	}

	// The slot number of an old file name "saveslot<N>.save" (0..9), or nothing.
	inline std::optional<int> old_slot_of(const std::string_view name)
	{
		constexpr std::string_view head = "saveslot";
		constexpr std::string_view tail = ".save";
		if (name.size() != head.size() + 1 + tail.size())
		{
			return std::nullopt;
		}
		std::string lower(name);
		for (auto& c : lower)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		if (lower.compare(0, head.size(), head) != 0 || lower.compare(head.size() + 1, tail.size(), tail) != 0)
		{
			return std::nullopt;
		}
		const char digit = lower[head.size()];
		return digit >= '0' && digit <= '9' ? std::optional<int>(digit - '0') : std::nullopt;
	}

	// ---- The container as the slot list reads it (SPEC-new-save 1.1-1.4, 2.2) ---------------------------

	constexpr std::array<std::uint8_t, 8> lc_magic{0x4c, 0x43, 0x53, 0x41, 0x56, 0x45, 0x31, 0x00}; // "LCSAVE1\0"
	constexpr std::size_t lc_header_size = 0x88;
	constexpr std::size_t lc_section_entry = 24;
	constexpr std::uint32_t lc_max_sections = 4096;
	constexpr std::uint32_t lc_max_file = 64u << 20;
	constexpr std::uint16_t lc_format_major = 1;
	// Required feature bits the running engine understands: bit 0 (ENTITY_LAYOUT_CONTIGUOUS10) only under the expanded
	// native layout E10A2 (set once by save_format when the entity-capacity install prepares; plan section 4).
	constexpr std::uint32_t lc_feature_entity_layout_contiguous10 = 1;
	inline std::uint32_t lc_known_required_features = 0;
	constexpr std::uint8_t kind_campaign = 1;
	constexpr std::uint8_t kind_profile = 2;
	constexpr std::uint16_t section_required = 1;
	constexpr std::uint16_t section_keep = 2;
	constexpr std::uint16_t section_ext_droppable = 8;

	// The tags a format-1.0 reader knows, each at section major 1; minor 0 but HERO (known_minor).
	inline constexpr std::array<std::string_view, 17> known_tags{"META", "KEYS", "GAME", "ITEM", "HERO", "ULCK", "REST", "MISS", "DNGR",
	                                                             "REVW", "ZONE", "SVAR", "XTRC", "PRFO", "PRFP", "PRFS", "LGCY"};
	inline constexpr std::array<std::string_view, 15> campaign_required{"META", "GAME", "ITEM", "HERO", "ULCK", "REST", "MISS", "DNGR",
	                                                                    "REVW", "ZONE", "SVAR", "XTRC", "PRFO", "PRFP", "PRFS"};
	inline constexpr std::array<std::string_view, 4> profile_required{"META", "PRFO", "PRFP", "PRFS"};
	// The newest minor of a known section this build reads (SPEC-new-save 1.4): HERO 1 = each power slot's flag byte
	// appended to PWR (revision 2.3); the library's section_minor says the same.
	inline std::uint8_t known_minor(const std::string_view tag) { return tag == "HERO" ? 1 : 0; }

	enum class file_status
	{
		ok,
		damaged,      // size, magic, a CRC or the structure: the backup may stand in (1.3)
		needs_newer,  // intact, written by a newer format or build (D10): never flagged corrupt, no backup tried
		wrong_kind,   // a profile where a campaign was expected, or the other way round
	};

	struct inspection
	{
		file_status status = file_status::damaged;
		std::string why;          // the reason, for the log
		std::uint8_t kind = 0;
		std::uint8_t origin = 0;
		std::uint8_t slot = 0;
		std::uint16_t major = 0, minor = 0;
		std::uint32_t content_version = 0;
		std::uint32_t catalogue_revision = 0;
		std::uint32_t writer_version = 0;
		std::uint64_t campaign_serial = 0;
		std::uint32_t save_sequence = 0;
		std::uint64_t saved_at = 0;
		float play_time = 0;
		std::string description;  // META's description, as the slot list shows it
		std::string writer;
		bool has_lgcy = false;
	};

	inline std::uint16_t rd16(const std::uint8_t* p)
	{
		return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
	}

	inline std::uint32_t rd32(const std::uint8_t* p)
	{
		return static_cast<std::uint32_t>(p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t>(p[3]) << 24));
	}

	inline std::uint64_t rd64(const std::uint8_t* p)
	{
		return static_cast<std::uint64_t>(rd32(p)) | (static_cast<std::uint64_t>(rd32(p + 4)) << 32);
	}

	// CRC-32 (IEEE 802.3, the zlib polynomial).
	inline std::uint32_t crc32(const std::uint8_t* data, const std::size_t size, std::uint32_t crc = 0)
	{
		static const auto table = []
		{
			std::array<std::uint32_t, 256> t{};
			for (std::uint32_t i = 0; i < 256; ++i)
			{
				std::uint32_t c = i;
				for (int k = 0; k < 8; ++k)
				{
					c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
				}
				t[i] = c;
			}
			return t;
		}();
		crc = ~crc;
		for (std::size_t i = 0; i < size; ++i)
		{
			crc = table[(crc ^ data[i]) & 0xff] ^ (crc >> 8);
		}
		return ~crc;
	}

	// The header, the section table and META of an LC file: enough for the slot list and the D10 decision, without
	// the library (which validates everything before a load). `expected_kind` 0 accepts both kinds.
	inline inspection inspect(const std::uint8_t* data, const std::size_t size, const std::uint8_t expected_kind)
	{
		inspection out;
		const auto damaged = [&](std::string why)
		{
			out.status = file_status::damaged;
			out.why = std::move(why);
			return out;
		};
		if (size < lc_header_size)
		{
			return damaged("shorter than the header");
		}
		if (std::memcmp(data, lc_magic.data(), lc_magic.size()) != 0)
		{
			return damaged("not an LC save (magic)");
		}
		if (crc32(data, 0x84) != rd32(data + 0x84))
		{
			return damaged("header CRC");
		}
		out.kind = data[8];
		out.major = rd16(data + 0x0a);
		out.minor = rd16(data + 0x0c);
		const std::uint32_t required = rd32(data + 0x10);
		const std::uint32_t length = rd32(data + 0x18);
		const std::uint32_t sections = rd32(data + 0x1c);
		const std::uint32_t table = rd32(data + 0x20);
		out.writer_version = rd32(data + 0x24);
		out.content_version = rd32(data + 0x28);
		out.catalogue_revision = rd32(data + 0x2c);
		out.campaign_serial = rd64(data + 0x30);
		out.save_sequence = rd32(data + 0x3c);
		if (data[9] != 0x11)
		{
			return damaged("header size");
		}
		if (rd32(data + 0x80) != 0)
		{
			return damaged("legacy length guard");
		}
		if (length != size || size > lc_max_file)
		{
			return damaged("file length " + std::to_string(length) + " for " + std::to_string(size) + " bytes");
		}
		if (out.major != lc_format_major)
		{
			out.status = file_status::needs_newer;
			out.why = "format " + std::to_string(out.major) + "." + std::to_string(out.minor);
			return out;
		}
		if (required & ~lc_known_required_features)
		{
			out.status = file_status::needs_newer;
			out.why = "required feature bits " + std::to_string(required);
			return out;
		}
		if (sections < 1 || sections > lc_max_sections || table < lc_header_size || table > size ||
		    static_cast<std::uint64_t>(table) + static_cast<std::uint64_t>(sections) * lc_section_entry > size)
		{
			return damaged("section table");
		}
		if (crc32(data + table, size - table) != rd32(data + 0x38))
		{
			return damaged("body CRC");
		}
		if (expected_kind && out.kind != expected_kind)
		{
			out.status = file_status::wrong_kind;
			out.why = out.kind == kind_profile ? "a profile file" : out.kind == kind_campaign ? "a campaign file" : "kind " + std::to_string(out.kind);
			return out;
		}
		std::uint32_t last_end = table + sections * lc_section_entry;
		const std::uint8_t* meta = nullptr;
		std::uint32_t meta_length = 0;
		std::vector<std::string> seen;
		for (std::uint32_t i = 0; i < sections; ++i)
		{
			const std::uint8_t* e = data + table + i * lc_section_entry;
			const std::string tag(reinterpret_cast<const char*>(e), 4);
			const std::uint8_t major = e[4];
			const std::uint8_t minor = e[5];
			const std::uint16_t flags = rd16(e + 6);
			const std::uint32_t offset = rd32(e + 8);
			const std::uint32_t plength = rd32(e + 12);
			for (const char c : tag)
			{
				if (c < 0x20 || c > 0x7e)
				{
					return damaged("section tag");
				}
			}
			if (std::find(seen.begin(), seen.end(), tag) != seen.end())
			{
				return damaged("tag " + tag + " twice");
			}
			seen.push_back(tag);
			if (offset < last_end || static_cast<std::uint64_t>(offset) + plength > size)
			{
				return damaged("section " + tag + " placement");
			}
			last_end = offset + plength;
			if (crc32(data + offset, plength) != rd32(e + 20))
			{
				return damaged("section " + tag + " CRC");
			}
			const bool known = std::find(known_tags.begin(), known_tags.end(), tag) != known_tags.end();
			if (!known && (flags & section_required))
			{
				out.status = file_status::needs_newer;
				out.why = "section " + tag;
				return out;
			}
			if (known && (major > 1 || (minor > known_minor(tag) && !(flags & section_ext_droppable))))
			{
				out.status = file_status::needs_newer;
				out.why = "section " + tag + " " + std::to_string(major) + "." + std::to_string(minor);
				return out;
			}
			if (tag == "META")
			{
				meta = data + offset;
				meta_length = plength;
			}
			if (tag == "LGCY")
			{
				out.has_lgcy = true;
			}
		}
		const auto& needed = out.kind == kind_profile ? std::vector<std::string_view>(profile_required.begin(), profile_required.end())
		                                              : std::vector<std::string_view>(campaign_required.begin(), campaign_required.end());
		if (out.kind != kind_campaign && out.kind != kind_profile)
		{
			return damaged("kind " + std::to_string(out.kind));
		}
		for (const auto tag : needed)
		{
			if (std::find(seen.begin(), seen.end(), std::string(tag)) == seen.end())
			{
				return damaged("no " + std::string(tag) + " section");
			}
		}
		// META head: rsize, saved at (u64), description (str), origin (u8), zone (id), act (u16), difficulty (u32),
		// play time (f32), writer (str), slot (u8).
		if (!meta || meta_length < 4)
		{
			return damaged("META");
		}
		const std::uint32_t rsize = rd32(meta);
		if (rsize > meta_length - 4 || rsize < 8 + 2 + 1 + 4 + 2 + 4 + 4 + 2 + 1)
		{
			return damaged("META head");
		}
		const std::uint8_t* p = meta + 4;
		const std::uint8_t* end = p + rsize;
		out.saved_at = rd64(p);
		p += 8;
		const std::uint16_t dlen = rd16(p);
		p += 2;
		if (dlen > 127 || p + dlen + 1 + 4 + 2 + 4 + 4 + 2 + 1 > end)
		{
			return damaged("META description");
		}
		out.description.assign(reinterpret_cast<const char*>(p), dlen);
		p += dlen;
		out.origin = *p++;
		p += 4 + 2 + 4; // zone, act, difficulty
		std::memcpy(&out.play_time, p, 4);
		p += 4;
		const std::uint16_t wlen = rd16(p);
		p += 2;
		if (wlen > 63 || p + wlen + 1 > end)
		{
			return damaged("META writer");
		}
		out.writer.assign(reinterpret_cast<const char*>(p), wlen);
		p += wlen;
		out.slot = *p;
		out.status = file_status::ok;
		return out;
	}

	// ---- The disk (SPEC-new-save 1.6) --------------------------------------------------------------------

	// Read-only, share read and write: the file is never locked against the game or the player.
	inline bool read_file(const std::string& path, std::vector<std::uint8_t>& out, std::string& error)
	{
		out.clear();
		const HANDLE h = CreateFileA(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE)
		{
			error = "can't open (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		LARGE_INTEGER size{};
		if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > lc_max_file)
		{
			CloseHandle(h);
			error = "size";
			return false;
		}
		out.resize(static_cast<std::size_t>(size.QuadPart));
		std::size_t done = 0;
		while (done < out.size())
		{
			DWORD got = 0;
			if (!ReadFile(h, out.data() + done, static_cast<DWORD>(std::min<std::size_t>(out.size() - done, 1u << 20)), &got, nullptr) || !got)
			{
				CloseHandle(h);
				error = "read failed (error " + std::to_string(GetLastError()) + ")";
				out.clear();
				return false;
			}
			done += got;
		}
		CloseHandle(h);
		return true;
	}

	// The steps of write_atomically, for a test that looks at the folder after each one.
	enum class write_step
	{
		tmp_written,  // X.tmp written, flushed, re-read and compared
		old_kept,     // the old X moved to X.bak (or, damaged, deleted); X.tmp not yet in place
		in_place,     // X.tmp moved over X
	};

	// SPEC 1.6: X.tmp written and flushed, re-read and compared, the old X kept as X.bak when it is intact
	// (deleted when it isn't), then X.tmp moved over X with write-through. False with the step that failed. At every
	// moment a complete file is on disk: the old X, or X.bak beside a complete X.tmp, or the new X. `after` (tests
	// only) is called after each step that changed the folder.
	inline bool write_atomically(const std::string& path, const std::vector<std::uint8_t>& bytes, std::string& error, void (*after)(write_step, void*) = nullptr,
	                             void* context = nullptr)
	{
		const std::string tmp = path + ".tmp";
		const std::string bak = path + ".bak";
		const auto exists = [](const std::string& p) {
			const DWORD a = GetFileAttributesA(p.c_str());
			return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
		};
		{
			const HANDLE h = CreateFileA(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
			if (h == INVALID_HANDLE_VALUE)
			{
				error = "can't create " + tmp + " (error " + std::to_string(GetLastError()) + ")";
				return false;
			}
			std::size_t done = 0;
			bool ok = true;
			while (ok && done < bytes.size())
			{
				DWORD put = 0;
				ok = WriteFile(h, bytes.data() + done, static_cast<DWORD>(std::min<std::size_t>(bytes.size() - done, 1u << 20)), &put, nullptr) && put;
				done += put;
			}
			ok = ok && FlushFileBuffers(h);
			CloseHandle(h);
			if (!ok)
			{
				error = "writing " + tmp + " failed (error " + std::to_string(GetLastError()) + ")";
				DeleteFileA(tmp.c_str());
				return false;
			}
		}
		std::vector<std::uint8_t> check;
		std::string why;
		if (!read_file(tmp, check, why) || check != bytes)
		{
			error = "re-reading " + tmp + " gave other bytes" + (why.empty() ? "" : " (" + why + ")");
			DeleteFileA(tmp.c_str());
			return false;
		}
		if (after)
		{
			after(write_step::tmp_written, context);
		}
		if (exists(path))
		{
			std::vector<std::uint8_t> old;
			const bool intact = read_file(path, old, why) && inspect(old.data(), old.size(), 0).status != file_status::damaged;
			if (intact)
			{
				if (!MoveFileExA(path.c_str(), bak.c_str(), MOVEFILE_REPLACE_EXISTING))
				{
					error = "can't keep the previous file as " + bak + " (error " + std::to_string(GetLastError()) + ")";
					DeleteFileA(tmp.c_str());
					return false;
				}
			}
			else if (!DeleteFileA(path.c_str()))
			{
				error = "can't replace the damaged " + path + " (error " + std::to_string(GetLastError()) + ")";
				DeleteFileA(tmp.c_str());
				return false;
			}
			if (after)
			{
				after(write_step::old_kept, context);
			}
		}
		if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			error = "can't move " + tmp + " into place (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		if (after)
		{
			after(write_step::in_place, context);
		}
		return true;
	}

	// Overwriting a slot: CSaveLoad's save sequence deletes the slot through card vt+0x70 (the call at 0x4b1612,
	// returning to 0x4b1615) before it serializes and writes it (vt+0x64 at 0x4b177a), with only the card's free
	// space checks between. Deleting there would leave no file at all until the write, and none if the write is
	// refused or the game stops in between; so that delete removes nothing and the write's 1.6 procedure keeps the old
	// file as X.bak. A delete from anywhere else (the menu's Delete, 0x4b2185; the list's clean-up, 0x4b25f7) is the
	// player's: X and X.bak go (SPEC 1.6, last paragraph).
	constexpr address overwrite_delete_return = 0x4b1615;

	enum class delete_action
	{
		remove,          // X and X.bak deleted
		keep_for_write,  // nothing deleted; the write that follows keeps X as X.bak
	};

	inline delete_action delete_action_for(const address return_address)
	{
		return return_address == overwrite_delete_return ? delete_action::keep_for_write : delete_action::remove;
	}

	// ---- The message a refusal shows (D11 on Load Game) -------------------------------------------------

	// The save/load menu is one global CSaveLoad (0x75cbc0, built at 0x4b28a2 by 0x4af8e0) whose dword +0xd4 says
	// what it was opened for. When vt+0x2c answers "no profile" (as D11 does: nothing is read), the menu writes a
	// fresh profile (0x4af780: vt+0x54 at 0x4af7c1) and, when that fails, shows card message 6 (0x4af7dd,
	// EMSG_SAVE_FAILED_DEVNUM: "Save failed!") with Retry - also when the player chose Load Game. Under D11 the fix
	// shows message 9 (EMSG_LOAD_FAILED_DEVNUM, the game's own "Load failed!") there instead, with the same cause.
	constexpr address save_load_menu = 0x75cbc0;
	constexpr std::size_t save_load_mode = 0xd4;
	constexpr int message_save_failed = 6;
	constexpr int message_load_failed = 9;
	constexpr int menu_mode_load = 3;  // the main menu's Load Game (read in game: 0 in the main menu, 3 in its Load Game)
	constexpr int menu_mode_save = 4;  // an X pad's Save Game (read in game; 0x4b154c: cmp +0xd4, 4)

	inline int message_code_for(const int code, const int menu_mode, const bool tables_failed)
	{
		return tables_failed && code == message_save_failed && menu_mode == menu_mode_load ? message_load_failed : code;
	}

	// ---- The slot list's texts -----------------------------------------------------------------------

	constexpr std::size_t description_max = 0x7f; // what the card's own writer keeps (strncpy 0x7f)

	// The description the slot list shows for a file: its own, or a prefix naming its state.
	inline std::string slot_text(const inspection& i, const bool from_backup)
	{
		std::string text;
		switch (i.status)
		{
		case file_status::ok:
			text = (from_backup ? "(restored from backup) " : "") + i.description;
			break;
		case file_status::needs_newer:
			text = "(needs a newer version) " + i.description;
			break;
		case file_status::wrong_kind:
			text = "(not a campaign) " + i.description;
			break;
		case file_status::damaged:
			text = "(damaged)";
			break;
		}
		if (text.size() > description_max)
		{
			text.resize(description_max);
		}
		return text;
	}

	// The text of the notice slot (the highest free slot while a notice is pending, D11 / an import refusal).
	inline std::string notice_text(const std::string_view notice)
	{
		std::string text = "XML2 Fix: " + std::string(notice);
		if (text.size() > description_max)
		{
			text.resize(description_max);
		}
		return text;
	}

	// The message shown when a read, write or delete was refused: the game's own text, then the reason.
	inline std::string message_with_reason(const std::string_view game_text, const std::string_view reason, const std::size_t capacity)
	{
		std::string text(game_text);
		if (!reason.empty())
		{
			text += text.empty() ? "" : " - ";
			text += reason;
		}
		if (text.size() + 1 > capacity)
		{
			text.resize(capacity - 1);
		}
		return text;
	}

	// ---- The import ledger (SPEC-new-save 5.6 B) --------------------------------------------------------

	// One line per old file ever handled, tab-separated: name, size, modification time (FILETIME as a decimal),
	// SHA-1 (40 hex characters), result (converted / refused / waiting), the slot it went to (-1 = none), the fix
	// version that handled it, then free text (the library's ledger fields or the refusal). Lines starting with #
	// are comments.
	struct ledger_entry
	{
		std::string name;
		std::uint64_t size = 0;
		std::uint64_t mtime = 0;
		std::string sha1; // lower-case hex
		std::string result;
		int slot = -1;
		std::string fix_version;
		std::string detail;
	};

	inline std::string ledger_line(const ledger_entry& e)
	{
		std::string detail = e.detail;
		for (auto& c : detail)
		{
			if (c == '\t' || c == '\n' || c == '\r')
			{
				c = ' ';
			}
		}
		return e.name + "\t" + std::to_string(e.size) + "\t" + std::to_string(e.mtime) + "\t" + e.sha1 + "\t" + e.result + "\t" + std::to_string(e.slot) + "\t" +
		       e.fix_version + "\t" + detail;
	}

	inline std::optional<ledger_entry> parse_ledger_line(const std::string_view line)
	{
		if (line.empty() || line[0] == '#')
		{
			return std::nullopt;
		}
		std::vector<std::string> fields;
		std::size_t start = 0;
		for (int i = 0; i < 7; ++i)
		{
			const auto tab = line.find('\t', start);
			if (tab == std::string_view::npos)
			{
				return std::nullopt;
			}
			fields.emplace_back(line.substr(start, tab - start));
			start = tab + 1;
		}
		ledger_entry e;
		e.name = fields[0];
		try
		{
			e.size = std::stoull(fields[1]);
			e.mtime = std::stoull(fields[2]);
			e.slot = std::stoi(fields[5]);
		}
		catch (...)
		{
			return std::nullopt;
		}
		e.sha1 = fields[3];
		e.result = fields[4];
		e.fix_version = fields[6];
		e.detail = std::string(line.substr(start));
		if (e.sha1.size() != 40)
		{
			return std::nullopt;
		}
		return e;
	}

	inline std::vector<ledger_entry> parse_ledger(const std::string_view text)
	{
		std::vector<ledger_entry> out;
		std::size_t start = 0;
		while (start < text.size())
		{
			auto end = text.find('\n', start);
			if (end == std::string_view::npos)
			{
				end = text.size();
			}
			auto line = text.substr(start, end - start);
			if (!line.empty() && line.back() == '\r')
			{
				line.remove_suffix(1);
			}
			if (const auto e = parse_ledger_line(line))
			{
				out.push_back(*e);
			}
			start = end + 1;
		}
		return out;
	}

	enum class ledger_verdict
	{
		convert,         // not in the ledger (by name + size + time, nor by SHA-1): read and convert it
		skip_unread,     // name, size and time match a line: not read again
		skip_converted,  // its SHA-1 was converted (or refused by this fix version) before
		retry,           // refused or waiting by another fix version, or waiting: tried again
	};

	// Before the file is read: name + size + time decide. `after_hash` (the SHA-1 is known) decides the rest.
	inline ledger_verdict ledger_check(const std::vector<ledger_entry>& ledger, const std::string_view name, const std::uint64_t size, const std::uint64_t mtime,
	                                   const std::string_view sha1, const std::string_view fix_version)
	{
		for (const auto& e : ledger)
		{
			const bool same_file = e.name == name && e.size == size && e.mtime == mtime;
			const bool same_bytes = !sha1.empty() && e.sha1 == sha1;
			if (!same_file && !same_bytes)
			{
				continue;
			}
			if (e.result == "converted")
			{
				return same_file ? ledger_verdict::skip_unread : ledger_verdict::skip_converted;
			}
			if (e.result == "refused" && e.fix_version == fix_version)
			{
				return same_file ? ledger_verdict::skip_unread : ledger_verdict::skip_converted;
			}
			return ledger_verdict::retry; // refused by another version, or waiting for a free slot
		}
		return ledger_verdict::convert;
	}

	// The target slot of an old file: its own number when free, else the lowest free slot; nothing when all ten
	// are taken. `taken[n]` = a .lcsave exists for slot n (or was assigned in this run).
	inline std::optional<int> import_slot(const std::array<bool, slot_count>& taken, const int own)
	{
		if (own >= 0 && own < slot_count && !taken[own])
		{
			return own;
		}
		for (int n = 0; n < slot_count; ++n)
		{
			if (!taken[n])
			{
				return n;
			}
		}
		return std::nullopt;
	}

	// ---- The old stream's hero blocks (SPEC-old-save 1.3-1.6) ------------------------------------------

	constexpr std::size_t tag_size = 52;
	constexpr std::uint8_t tag_marker = 0x0a;
	constexpr std::size_t item_head_size = 0x165;    // count byte, 160 pairs, 3 x u32, 6 x u32
	constexpr std::size_t instance_fixed = 5;        // definition index, the four bytes
	constexpr std::size_t enhancement_size = 6;      // u16 record, u32 value
	constexpr std::size_t attributes_size = 0x18;
	constexpr std::size_t heap_size = 0xa0;          // 19 entries x 8, bitmap, count
	constexpr std::size_t heap_entries = 19;
	constexpr std::size_t heap_entry_size = 8;
	constexpr std::size_t equipment_tail = 0x1c;
	constexpr std::size_t powers_size = 0x55;
	constexpr std::size_t costume_size = 5;
	constexpr std::size_t unlocked_size = 600;
	constexpr std::size_t max_hero_blocks = 64;      // reader ceiling (21 today)
	constexpr std::size_t max_instances = 4096;
	constexpr std::size_t max_enhancements = 64;

	struct hero_heap
	{
		std::size_t block;  // offset of the block in the image (the CAttributes scalars)
		std::size_t heap;   // offset of its talent heap
	};

	// Walks an old slot image (envelope + payload) up to the [CHARACTERSUNLOCKED] tag: the offset of every hero
	// block's talent heap, in stream order. Nothing when the stream doesn't parse (the tag never comes, a count
	// is out of range, the image is short).
	inline std::optional<std::vector<hero_heap>> hero_heaps(const std::uint8_t* data, const std::size_t size)
	{
		std::size_t p = old_envelope;
		const auto need = [&](const std::size_t n) { return p + n <= size; };
		const auto is_tag = [&](const char* text)
		{
			const std::size_t n = std::strlen(text);
			return need(tag_size) && data[p] == tag_marker && std::memcmp(data + p + 1, text, n) == 0;
		};
		const auto skip_instance = [&]() -> bool
		{
			if (!need(instance_fixed))
			{
				return false;
			}
			p += instance_fixed;
			for (int list = 0; list < 3; ++list)
			{
				if (!need(4))
				{
					return false;
				}
				const std::uint32_t count = rd32(data + p);
				p += 4;
				if (count > max_enhancements || !need(count * enhancement_size))
				{
					return false;
				}
				p += count * enhancement_size;
			}
			return true;
		};
		if (!is_tag("[SAVEGAMEBEGIN"))
		{
			return std::nullopt;
		}
		p += tag_size;
		if (!need(item_head_size + 4))
		{
			return std::nullopt;
		}
		p += item_head_size;
		const std::uint32_t carried = rd32(data + p);
		p += 4;
		if (carried > max_instances)
		{
			return std::nullopt;
		}
		for (std::uint32_t i = 0; i < carried; ++i)
		{
			if (!skip_instance())
			{
				return std::nullopt;
			}
		}
		if (!need(4))
		{
			return std::nullopt;
		}
		const std::uint32_t world = rd32(data + p);
		p += 4;
		if (world > max_instances)
		{
			return std::nullopt;
		}
		for (std::uint32_t i = 0; i < world; ++i)
		{
			if (!need(4))
			{
				return std::nullopt;
			}
			p += 4;
			if (!skip_instance())
			{
				return std::nullopt;
			}
		}
		std::vector<hero_heap> out;
		while (!is_tag("[CHARACTERSUNLOCKED]"))
		{
			if (out.size() >= max_hero_blocks || !need(attributes_size + heap_size + 1))
			{
				return std::nullopt;
			}
			out.push_back({p, p + attributes_size});
			p += attributes_size + heap_size;
			const std::uint8_t equipped = data[p++];
			if (equipped > 32)
			{
				return std::nullopt;
			}
			for (std::uint8_t i = 0; i < equipped; ++i)
			{
				if (!skip_instance())
				{
					return std::nullopt;
				}
			}
			if (!need(equipment_tail + powers_size + costume_size))
			{
				return std::nullopt;
			}
			p += equipment_tail + powers_size + costume_size;
		}
		return out;
	}

	enum class heap_insert
	{
		inserted,
		present, // the id is already in the heap
		full,    // 19 entries: refused
		invalid, // the heap's count or bitmap isn't what the engine writes
	};

	// Inserts talent `id` (rank 0, base ranks 0, every other bit 0) into the 0xa0-byte heap at `heap`, keeping the
	// entries sorted by id, the count and the bitmap as the engine keeps them (SPEC-old-save 1.6 b).
	inline heap_insert insert_talent(std::uint8_t* heap, const std::uint16_t id)
	{
		const std::uint32_t count = rd32(heap + heap_entries * heap_entry_size + 4);
		const std::uint32_t bitmap = rd32(heap + heap_entries * heap_entry_size);
		if (count > heap_entries || bitmap != ((count >= 32 ? 0xffffffffu : (1u << count) - 1)))
		{
			return heap_insert::invalid;
		}
		std::uint32_t at = 0;
		for (; at < count; ++at)
		{
			const std::uint16_t have = rd16(heap + at * heap_entry_size);
			if (have == id)
			{
				return heap_insert::present;
			}
			if (have > id)
			{
				break;
			}
		}
		if (count >= heap_entries)
		{
			return heap_insert::full;
		}
		std::memmove(heap + (at + 1) * heap_entry_size, heap + at * heap_entry_size, (count - at) * heap_entry_size);
		std::memset(heap + at * heap_entry_size, 0, heap_entry_size);
		heap[at * heap_entry_size] = static_cast<std::uint8_t>(id);
		heap[at * heap_entry_size + 1] = static_cast<std::uint8_t>(id >> 8);
		const std::uint32_t new_count = count + 1;
		const std::uint32_t new_bitmap = (1u << new_count) - 1;
		std::memcpy(heap + heap_entries * heap_entry_size, &new_bitmap, 4);
		std::memcpy(heap + heap_entries * heap_entry_size + 4, &new_count, 4);
		return heap_insert::inserted;
	}

	// The play time of an old slot image (GAME block +0xd4): found through the hero blocks (the game block follows
	// the unlocked list and the 22 restore records, whose equipment lists vary). Nothing when the image doesn't parse.
	inline std::optional<float> old_play_time(const std::uint8_t* data, const std::size_t size)
	{
		const auto heaps = hero_heaps(data, size);
		if (!heaps)
		{
			return std::nullopt;
		}
		// After the last hero block comes the tag, 600 bytes, the restore tag, the flag, 22 restore records.
		std::size_t p = old_envelope;
		// Re-walk: the cheapest is to find the [CHARACTERSUNLOCKED] tag after the last heap.
		p = heaps->empty() ? old_envelope + tag_size + item_head_size : heaps->back().heap + heap_size;
		for (; p + tag_size <= size; ++p)
		{
			if (data[p] == tag_marker && std::memcmp(data + p + 1, "[CHARACTERSUNLOCKED]", 20) == 0)
			{
				break;
			}
		}
		p += tag_size + unlocked_size + tag_size + 1;
		const auto need = [&](const std::size_t n) { return p + n <= size; };
		const auto skip_instance = [&]() -> bool
		{
			if (!need(instance_fixed))
			{
				return false;
			}
			p += instance_fixed;
			for (int list = 0; list < 3; ++list)
			{
				if (!need(4))
				{
					return false;
				}
				const std::uint32_t count = rd32(data + p);
				p += 4;
				if (count > max_enhancements || !need(count * enhancement_size))
				{
					return false;
				}
				p += count * enhancement_size;
			}
			return true;
		};
		for (int r = 0; r < 22; ++r)
		{
			if (!need(attributes_size + heap_size + 1))
			{
				return std::nullopt;
			}
			p += attributes_size + heap_size;
			const std::uint8_t equipped = data[p++];
			if (equipped > 32)
			{
				return std::nullopt;
			}
			for (std::uint8_t i = 0; i < equipped; ++i)
			{
				if (!skip_instance())
				{
					return std::nullopt;
				}
			}
			if (!need(equipment_tail))
			{
				return std::nullopt;
			}
			p += equipment_tail;
		}
		if (!need(0xdc))
		{
			return std::nullopt;
		}
		float play_time = 0;
		std::memcpy(&play_time, data + p + 0xd4, 4);
		return play_time;
	}

	// ---- The writer version (SPEC 1.1 offset 0x24) ---------------------------------------------------

	constexpr std::uint8_t writer_kind_fix = 0;
	constexpr std::uint8_t writer_kind_import = 2;

	inline std::uint32_t writer_version(const int major, const int minor, const int patch, const std::uint8_t kind)
	{
		return (static_cast<std::uint32_t>(major & 0xff) << 24) | (static_cast<std::uint32_t>(minor & 0xff) << 16) | (static_cast<std::uint32_t>(patch & 0xff) << 8) | kind;
	}
}
