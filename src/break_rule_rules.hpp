#pragma once

// [Game] BreakRule: X-Men Legends 1's rule for which attack breaks which object, for a mod that carries XML1's
// objects and attacks (the X-Men Legends 1 port), kept apart from the hooks so xml2_test can check it without the
// game: the ini value's rules, XML1's numbers, the decision, what the fix remembers between the game's calls, every
// retail byte the change relies on and the code it writes.
//
// Every XMen2.exe address is the retail build's (image base 0x400000, no relocations) and is in `guards`; every
// XML1 one is from XML1's default.xbe (Xbox, 2004).
//
// What XML1 does. An object has a `structure` of 0..10 and an attack a damage level. The object's damage function
// (0x92220) refuses a hit whose level is below the structure, or whose target has structure 10 (exceptions: 32000
// damage, the direct damage type). The level of a melee hit (0x59a50) is the attack's authored `damagelevel` plus
// 0 / 3 / 6 / 8 for a hero's Might of 0 / 1 / 2 / 3 (0xb1ba0, the number the pickup gate at 0x38457 also uses) plus
// the `damageLevel` affecter, at most 9. A plain punch is 1; a wall of structure 2 needs a power or Might.
//
// What XMen2.exe does. The same shape on narrower numbers: `structure` is clamped to 0..2 where it is read
// (0x498974) and 2 is unbreakable; the object's damage function (0x497e70, shared by ten classes) refuses a hit
// whose level byte (hit+0x2e) is below the structure byte (object+0x31c) at 0x498044-0x498062. The melee level
// (0x44f770: authored + the might_structure affecter + the damageLevel affecter) is capped at 1; it is slot 0x34 of
// the combat object's vtable (0x685a98), called from one place (0x4501e9, the melee sweep), and its result replaces
// the hit's authored byte (0x4501fb). Both bytes are read by much else: a character takes a hit only when the
// level byte is above its own structure byte (0x4293e0), about thirty sites test the structure byte for 2. So
// neither can hold XML1's 0..10, and a port that maps XML1's structure 2-9 to 1 (to keep those objects
// breakable at all) lets a plain punch break what XML1 kept for powers.
//
// BreakRule=xml1 adds XML1's comparison at the object's gate and changes no number anyone else reads:
//   - the object's XML1 structure comes from a new attribute of its entity definition, `xml1structure`, which the
//     fix reads with the game's own reader where the game reads `structure` (0x498970) and keeps by the object's
//     handle (object+0x1c). The game reads attributes by name, so nothing else sees the new one;
//   - the attack's XML1 level: the authored `DamageLevel` is stored unclamped (0x4dc094) and copied into the hit
//     (0x4dc271), so in a build that carries XML1's attacks the byte the melee sweep is about to cap IS XML1's
//     authored level. The fix's function in slot 0x34 notes it, calls the game's own (whose result goes back
//     unchanged), and remembers XML1's sum for that attacker and attack id (hit+0x28);
//   - at 0x49805b, a hit the game is about to let through is refused when XML1's level is below XML1's structure.
//     The refusal is the game's own refused branch (0x498064), with its exceptions and its effects.
// It only ever refuses: a hit the game refuses stays refused. Where a number isn't known - an object without the
// attribute, an object whose structure byte no longer is what the attribute stands for (a script changed it), a
// hit that never went through the melee sweep and so still carries its authored level - the rule uses what there
// is, or leaves the game's decision alone (decide()).

#include "limits_rules.hpp" // guard, matches, image_base

#include <Windows.h>

#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace break_rule_rules
{
	using limits_rules::guard;
	using limits_rules::image_base;
	using address = std::uint32_t;

	// ---- The ini value ----------------------------------------------------------------------------------

	enum class rule
	{
		xml2,
		xml1,
	};

	// The rule asked for, or why the value isn't one. `set` false and `error` empty: the key isn't set.
	struct rule_choice
	{
		rule value = rule::xml2;
		bool set = false;
		std::string error;
	};

	// "xml1" or "xml2", any case; a ';' starts a comment.
	inline rule_choice parse_rule(const std::string_view raw)
	{
		const auto value = ini_rules::value_text(raw);
		if (value.empty())
		{
			return {};
		}
		std::string lower(value);
		for (auto& c : lower)
		{
			c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		if (lower == "xml1")
		{
			return {rule::xml1, true, {}};
		}
		if (lower == "xml2")
		{
			return {rule::xml2, true, {}};
		}
		rule_choice refused;
		refused.error = "isn't xml1 (X-Men Legends 1's rule for breaking objects) or xml2 (the game's own)";
		return refused;
	}

	// ---- XML1's numbers -----------------------------------------------------------------------------------

	constexpr int xml1_max_structure = 10; // default.xbe's parser clamps to it; 10 is never broken
	constexpr int xml1_max_level = 9;      // 0x59a50's cap
	constexpr const char* structure_attribute = "xml1structure";

	// What a hero's Might adds to a melee hit's level: default.xbe 0x59a96 (0, 3, 6, 8 for Might 0..3, Might
	// above 3 counts as 3).
	constexpr int might_level(const int might)
	{
		constexpr std::array<int, 4> table{0, 3, 6, 8};
		return table[static_cast<std::size_t>(might < 0 ? 0 : might > 3 ? 3 : might)];
	}

	// The affecter's float as the game's ftol (0x67217c) reads it: toward zero. Not-a-number is 0, and anything
	// far outside a level's range is the nearest end of a range the sum's own clamp then cuts.
	inline int whole(const float value)
	{
		if (std::isnan(value))
		{
			return 0;
		}
		if (value >= 1000.0f)
		{
			return 1000;
		}
		if (value <= -1000.0f)
		{
			return -1000;
		}
		return static_cast<int>(value);
	}

	// XML1's level of a melee hit: the attack's authored level, Might's share and the damageLevel affecter, at
	// most 9 (0x59a50). Below zero is zero: the level travels as a byte.
	inline int xml1_level(const int authored, const int might, const float affecter)
	{
		const int sum = authored + might_level(might) + whole(affecter);
		return sum < 0 ? 0 : sum > xml1_max_level ? xml1_max_level : sum;
	}

	// The structure byte XMen2.exe has to hold for an object of XML1 structure `xml1`, as the port's builder
	// writes it: 0-1 anyone breaks (0), 2-9 breakable (1), 10 never (2).
	constexpr int engine_structure(const int xml1)
	{
		return xml1 <= 1 ? 0 : xml1 >= xml1_max_structure ? 2 : 1;
	}

	// ---- The decision -------------------------------------------------------------------------------------

	enum class verdict
	{
		allowed,      // XML1's level reaches XML1's structure
		refused,      // it doesn't: the game's refused branch
		no_structure, // the object has no usable XML1 structure: the game's decision stands
	};

	// For a hit the game is about to let through. `xml1` is the object's attribute (nullopt: none), `engine` its
	// structure byte now, `level` XML1's level of the hit.
	inline verdict decide(const std::optional<int> xml1, const int engine, const int level)
	{
		if (!xml1 || *xml1 < 0 || *xml1 > xml1_max_structure || engine_structure(*xml1) != engine)
		{
			return verdict::no_structure;
		}
		return level < *xml1 ? verdict::refused : verdict::allowed;
	}

	// ---- What the fix remembers ---------------------------------------------------------------------------

	// The XML1 structure of the objects alive now, by handle. The game finds an object as table[handle & mask]
	// (0x4c6c3c), so two live objects never share a handle's low bits; a slot remembers the whole handle and
	// answers only for it, so an object that took a destroyed one's slot, or was never read, has none.
	class structure_store
	{
	public:
		static constexpr std::uint32_t slots = 4096;

		void set(const std::uint32_t handle, const std::optional<int> xml1)
		{
			auto& slot = entries_[handle % slots];
			slot.handle = handle;
			slot.value = xml1 && *xml1 >= 0 && *xml1 <= xml1_max_structure ? static_cast<std::int8_t>(*xml1) : std::int8_t{-1};
		}

		std::optional<int> get(const std::uint32_t handle) const
		{
			const auto& slot = entries_[handle % slots];
			return handle && slot.handle == handle && slot.value >= 0 ? std::optional<int>(slot.value) : std::nullopt;
		}

	private:
		struct entry
		{
			std::uint32_t handle = 0;
			std::int8_t value = -1;
		};
		std::array<entry, slots> entries_{};
	};

	// The melee hits whose XML1 level the fix worked out, newest first: who attacks (the attacker's handle, which
	// the hit carries at +0) and which attack (the id at +0x28, by which the game itself keeps one swing from
	// hitting an object twice). The sweep works the level out and applies its hits in one call, so a hit's record is
	// there when the hit arrives; the ring only has to outlast whatever else attacks in between.
	class attack_ring
	{
	public:
		static constexpr std::size_t size = 64;

		void record(const std::uint32_t attacker, const std::uint32_t attack, const int level)
		{
			next_ = (next_ + size - 1) % size;
			entries_[next_] = {attacker, attack, level, true};
		}

		std::optional<int> find(const std::uint32_t attacker, const std::uint32_t attack) const
		{
			for (std::size_t i = 0; i < size; ++i)
			{
				const auto& e = entries_[(next_ + i) % size];
				if (e.used && e.attacker == attacker && e.attack == attack)
				{
					return e.level;
				}
			}
			return std::nullopt;
		}

	private:
		struct entry
		{
			std::uint32_t attacker = 0;
			std::uint32_t attack = 0;
			int level = 0;
			bool used = false;
		};
		std::array<entry, size> entries_{};
		std::size_t next_ = 0;
	};

	// XML1's level of a hit at the object's gate: the melee sweep's record for its attacker and attack, or - a hit
	// that never went through the sweep, so nothing capped it - the authored level it still carries.
	inline int hit_level(const attack_ring& ring, const std::uint32_t attacker, const std::uint32_t attack, const int hit_byte)
	{
		return ring.find(attacker, attack).value_or(hit_byte);
	}

	// ---- XMen2.exe ----------------------------------------------------------------------------------------

	// The hit (0x4297f0 builds one, 0x4350d0 copies it).
	constexpr address hit_attacker = 0x00; // the attacker's handle
	constexpr address hit_attack = 0x28;   // the attack's id (0x4dc309)
	constexpr address hit_level_byte = 0x2e;
	// The object and the actor.
	constexpr address entity_handle = 0x1c;
	constexpr address entity_structure = 0x31c;
	constexpr address actor_lift = 0x57a;     // bits 3-6: the might_heaviness sum (0x427dc0)
	constexpr address actor_affecter = 0x530; // the damageLevel affecter's sum, a float (0x44f7b8)
	constexpr address reader_number = 0x18;   // the entity reader's vtable slot: an attribute as a 16-bit number

	constexpr address level_slot_va = 0x685a98;     // the combat object's vtable (0x685a64), slot 0x34
	constexpr address level_function_va = 0x44f770; // what the slot holds
	constexpr address parse_hook_va = 0x498970;     // the structure clamp, after the game read `structure`
	constexpr address parse_resume_va = 0x49897a;   // the store that follows it
	constexpr address gate_hook_va = 0x49805b;      // the gate's last test: structure byte below 2
	constexpr address gate_refused_va = 0x498064;
	constexpr address gate_allowed_va = 0x498081;
	constexpr address gate_hit_offset = 0x24; // the function's copy of the hit, from esp at the gate

	constexpr std::string_view parse_displaced = "8a4424103c027602b002";
	constexpr std::string_view gate_displaced = "80be1c03000002721d";

	inline constexpr std::array<guard, 11> guards{{
		{level_slot_va, "70f74400", "the combat object's level slot"},
		{level_function_va,
		 "8b442408538a582e568b74240c8a867a05000081c670040000a8015774078bcee88bc70e008a860a01000033c98a8e0b0100000fb6d383e10f03caa8018bf974078bcee868c70e00"
		 "d986c0000000e8b92922000fbfc003f883ff02b8010000007d028bc75f5e5bc20800",
		 "the melee level: authored + might_structure + damageLevel, capped at 1"},
		{0x4501e3, "8b1753568bcfff5234d944241cd80dfc1968006a028d4e2c88432e", "the melee sweep's call of the slot and its store to the hit"},
		{0x427dc0, "568bf1f6860a010000017405e84f4111008a860a010000c0e803240f5ec3", "the lift value's getter"},
		{0x4dc094, "e8e36019005f88461c", "DamageLevel stored unclamped"},
		{0x4dc26e, "894e108a571c88562e", "the authored level copied to the hit"},
		{0x49895f, "8b078d4c24105168b89d68008bcfff5018", "the object's `structure` read"},
		{parse_hook_va, "8a4424103c027602b00288861c030000", "the structure clamp and store"},
		{0x498044, "8a4424523a861c03000072148bcee8c9e9ffff84c07509", "the object's gate: level against structure, invulnerable"},
		{gate_hook_va, "80be1c03000002721dd9442428d905bc9c6800", "the gate's structure test and the refused branch"},
		{0x4c6c3c, "8b86380c000023c78b0486", "the entity table's lookup by a handle's low bits"},
	}};

	// The first guard whose bytes `image` (XMen2.exe mapped at its base) doesn't have; nullptr when all match.
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for (const auto& g : guards)
		{
			if (!limits_rules::matches(image + (g.va - image_base), g.hex))
			{
				return &g;
			}
		}
		return nullptr;
	}

	// ---- The code the fix writes --------------------------------------------------------------------------

	// `jmp target` at `at`, then nops up to `size` bytes (the instructions it replaces).
	inline std::vector<std::uint8_t> jump(const address at, const address target, const std::size_t size)
	{
		std::vector<std::uint8_t> bytes(size, 0x90);
		bytes[0] = 0xe9;
		const address relative = target - at - 5;
		std::memcpy(bytes.data() + 1, &relative, 4);
		return bytes;
	}

	namespace detail
	{
		inline void word(std::vector<std::uint8_t>& b, const address v)
		{
			for (unsigned n = 0; n < 4; ++n)
			{
				b.push_back(static_cast<std::uint8_t>(v >> (8 * n)));
			}
		}

		// The flags, the general registers and the x87/SSE state (fxsave, on a 16-byte aligned block of the stack)
		// saved; eax is then esp as it was after the pushes: the hook site's esp is eax + 0x24.
		inline void save(std::vector<std::uint8_t>& b)
		{
			b.insert(b.end(), {
				0x9c,                            // pushfd
				0x60,                            // pushad
				0x8b, 0xc4,                      // mov eax, esp
				0x81, 0xec, 0x10, 0x02, 0, 0,    // sub esp, 0x210
				0x83, 0xe4, 0xf0,                // and esp, -16
				0x89, 0x84, 0x24, 0, 0x02, 0, 0, // mov [esp + 0x200], eax
				0x0f, 0xae, 0x04, 0x24,          // fxsave [esp]
			});
		}

		inline void restore(std::vector<std::uint8_t>& b)
		{
			b.insert(b.end(), {
				0x0f, 0xae, 0x0c, 0x24,          // fxrstor [esp]
				0x8b, 0xa4, 0x24, 0, 0x02, 0, 0, // mov esp, [esp + 0x200]
				0x61,                            // popad
				0x9d,                            // popfd
			});
		}

		// `push to; ret`: every register and flag as they are.
		inline void leave(std::vector<std::uint8_t>& b, const address to)
		{
			b.push_back(0x68);
			word(b, to);
			b.push_back(0xc3);
		}

		inline void displaced(std::vector<std::uint8_t>& b, const std::string_view hex)
		{
			for (std::size_t i = 0; i < limits_rules::hex_size(hex); ++i)
			{
				b.push_back(limits_rules::hex_byte(hex, i));
			}
		}
	}

	// Where 0x498970 jumps: esi the object, edi its reader. Calls `callback(object, reader)` (cdecl) with
	// everything saved, then runs the clamp the jump replaced and goes on at `resume`.
	inline std::vector<std::uint8_t> parse_bridge(const address callback, const address resume)
	{
		std::vector<std::uint8_t> b;
		detail::save(b);
		b.insert(b.end(), {0x57, 0x56, 0xb8}); // push edi; push esi; mov eax, callback
		detail::word(b, callback);
		b.insert(b.end(), {0xff, 0xd0, 0x83, 0xc4, 0x08}); // call eax; add esp, 8
		detail::restore(b);
		detail::displaced(b, parse_displaced); // mov al, [esp+0x10]; cmp al, 2; jbe +2; mov al, 2
		detail::leave(b, resume);
		return b;
	}

	// Where 0x49805b jumps: esi the object, the function's copy of the hit at esp + 0x24. The game's own test
	// first: structure byte 2 or more goes to `refused` as before. Otherwise `callback(object, hit)` (cdecl)
	// decides with everything saved: zero goes to `refused`, anything else to `allowed`.
	inline std::vector<std::uint8_t> gate_bridge(const address callback, const address refused, const address allowed)
	{
		std::vector<std::uint8_t> b{
			0x80, 0xbe, 0x1c, 0x03, 0, 0, 0x02, // cmp byte ptr [esi + 0x31c], 2
			0x72, 0x06,                         // jb ask
		};
		detail::leave(b, refused);
		detail::save(b);
		b.insert(b.end(), {
			0x8d, 0x48, static_cast<std::uint8_t>(0x24 + gate_hit_offset), // lea ecx, [eax + 0x24 + 0x24]: the hit
			0x51,                                                         // push ecx
			0x56,                                                         // push esi
			0xb8,                                                         // mov eax, callback
		});
		detail::word(b, callback);
		b.insert(b.end(), {
			0xff, 0xd0,       // call eax
			0x83, 0xc4, 0x08, // add esp, 8
			0x85, 0xc0,       // test eax, eax
			0x75, 0,          // jnz allow (the distance is filled in below)
		});
		const auto branch = b.size() - 1;
		detail::restore(b);
		detail::leave(b, refused);
		b[branch] = static_cast<std::uint8_t>(b.size() - (branch + 1));
		detail::restore(b);
		detail::leave(b, allowed);
		return b;
	}
}
