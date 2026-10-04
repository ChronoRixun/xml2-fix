#pragma once

// Geometry sharing's decisions, kept apart from the hook so xml2_test can check them on memory of its
// own and against a copy of the game's files: the code guards, where a legacy vertex array keeps its
// packed blend indices, the comparison of two geometries' indices, and the bridge the hook jumps to.
//
// When the game loads a skinned mesh it looks for a loaded one it can share. Its comparison checks the
// positions and the weights, not the packed blend (bone) indices, so two meshes that differ only there
// share one, and one of them is drawn with the other's bones. The hook sits in that candidate loop: a
// candidate whose indices differ is skipped, and the loop goes on to the next one. Anything this file
// can't read or doesn't know (another array layout, an extended format) is left to the game's own
// comparison.
//
// Every XMen2.exe address is the retail build's (image base 0x400000, no relocations); the engine
// libraries' are RVAs, added to wherever the module is loaded.

#include "limits_rules.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <vector>

namespace geometry_sharing_rules
{
	using address = std::uint32_t;
	using guard = limits_rules::guard;

	// [Game] GeometrySharingBlendIndices when it is absent (or isn't a switch): off. The X-Men Legends I
	// port sets it; nobody has seen the bug in XML2 as it ships, so XML2 keeps the game's own comparison.
	constexpr bool enabled_by_default = false;

	// The vtables that identify the legacy geometry and vertex array classes in the loaded modules.
	struct layout
	{
		address geometry_vtable;
		address vertex_vtable;
	};

	enum class comparison
	{
		equal,       // the packed blend indices match (or there are none): the game's comparison decides
		different,   // they differ: not this candidate
		unsupported, // a layout or format this file doesn't know, or unreadable data: the game's comparison decides
	};

	constexpr address hook_va = 0x56d42c;           // the candidate's vertex loop, after the format counts matched
	constexpr address continue_va = 0x56d432;       // back into it, past the two displaced instructions
	constexpr address next_candidate_va = 0x56d617; // the loop's path to the next candidate
	constexpr address geometry_vtable_rva = 0x21cb8;
	constexpr address vertex_vtable_rva = 0xdd6a0;

	constexpr std::array<guard, 4> exe_guards{{
		{0x56d353, "8bf03bf789742418", "candidate register and saved pointer"},
		{0x56d424, "3ac30f85eb010000", "format counts checked before the hook"},
		{hook_va, "8b44241433f6", "vertex-loop entry"},
		{next_candidate_va, "8b5d088b74242446", "next candidate path"},
	}};

	// RVAs relative to the actual loaded module, never preferred-base addresses.
	constexpr std::array<guard, 7> gfx_guards{{
		{0x4010, "8d411cc3", "legacy vertex format"},
		{0x22e0, "8b410cc3", "legacy vertex count"},
		{0x1d60, "8b01c1e80883e00fc3", "blend-index count"},
		{0x1d30, "8b01c1e80483e00fc3", "blend-weight count"},
		{0xa310, "8a413dc3", "packed blend-index offset"},
		{0xa2c0, "8a4138c3", "legacy stride"},
		{0x4605d, "33c08b51088a4138560faf4424088b725033d28a513903c6", "legacy vertex payload"},
	}};

	constexpr std::array<guard, 2> attrs_guards{{
		{0x3090, "8b410c8b400cc3", "legacy geometry vertex array/count"},
		{0x17000, "83ec105355", "legacy geometry apply"},
	}};

	// The first guard whose bytes `matches` refuses; nullptr when they all match.
	template <typename Guards, typename Matches>
	const guard* first_mismatch(const Guards& guards, Matches matches)
	{
		for (const auto& g : guards)
		{
			if (!matches(g.va, g.hex))
			{
				return &g;
			}
		}
		return nullptr;
	}

	template <typename Memory>
	std::optional<address> word(Memory& mem, address a)
	{
		address value = 0;
		return mem.read(a, &value, sizeof value) ? std::optional<address>(value) : std::nullopt;
	}

	template <typename Memory>
	std::optional<unsigned> byte(Memory& mem, address a)
	{
		std::uint8_t value = 0;
		return mem.read(a, &value, sizeof value) ? std::optional<unsigned>(value) : std::nullopt;
	}

	// Where a geometry's vertices keep their packed blend indices: `count` vertices of `stride` bytes
	// from `data`, the indices (`index_count` of them, packed in one four-byte field) at `index_offset`.
	struct vertex_view
	{
		address data;
		address count;
		unsigned stride;
		unsigned index_offset;
		unsigned index_count;
	};

	// A geometry's vertex view, or nullopt when it isn't the legacy geometry and vertex array this file
	// knows, or something can't be read.
	template <typename Memory>
	std::optional<vertex_view> view(Memory& mem, address geometry, const layout& types)
	{
		if (!geometry || word(mem, geometry) != types.geometry_vtable)
		{
			return std::nullopt;
		}
		const auto va = word(mem, geometry + 12);
		if (!va || !*va || word(mem, *va) != types.vertex_vtable)
		{
			return std::nullopt;
		}
		const auto count = word(mem, *va + 12);
		const auto format = word(mem, *va + 28);
		const auto stride = byte(mem, *va + 56);
		const auto offset = byte(mem, *va + 61);
		if (!count || !format || !stride || !offset)
		{
			return std::nullopt;
		}

		// Basic legacy position/normal/color, weight/index counts and up to eight UVs.
		// Point sprites, tangent/binormal extensions and reserved flags stay retail.
		constexpr address known_flags = 0x000f0ff7;
		const unsigned indices = (*format >> 8) & 15;
		const unsigned weights = (*format >> 4) & 15;
		const unsigned uvs = (*format >> 16) & 15;
		if ((*format & ~known_flags) || !(*format & 1) || indices > 4 || weights > 4 || uvs > 8)
		{
			return std::nullopt;
		}
		if (!indices)
		{
			return vertex_view{0, *count, *stride, *offset, 0};
		}

		// The concrete legacy array packs the index slots into one four-byte field.
		if (*stride < 4 || *offset > *stride - 4)
		{
			return std::nullopt;
		}
		const auto memory = word(mem, *va + 8);
		if (!memory || !*memory)
		{
			return std::nullopt;
		}
		const auto data = word(mem, *memory + 0x50);
		if (!data || (!*data && *count))
		{
			return std::nullopt;
		}
		// The last vertex's index field has to end inside the 32-bit address space.
		const auto end = std::uint64_t(*data) + (*count ? std::uint64_t(*count - 1) * *stride + *offset + 4 : 0);
		if (end > std::numeric_limits<address>::max())
		{
			return std::nullopt;
		}
		return vertex_view{*data, *count, *stride, *offset, indices};
	}

	// The incoming geometry against a candidate: vertex by vertex, every packed index field.
	template <typename Memory>
	comparison compare(Memory& mem, address incoming, address candidate, const layout& types)
	{
		const auto a = view(mem, incoming, types);
		const auto b = view(mem, candidate, types);
		if (!a || !b || a->count != b->count || a->index_count != b->index_count)
		{
			return comparison::unsupported;
		}
		if (!a->index_count)
		{
			return comparison::equal;
		}
		bool different = false;
		for (address i = 0; i < a->count; ++i)
		{
			const auto x = word(mem, a->data + i * a->stride + a->index_offset);
			const auto y = word(mem, b->data + i * b->stride + b->index_offset);
			if (!x || !y)
			{
				return comparison::unsupported;
			}
			different |= *x != *y;
		}
		return different ? comparison::different : comparison::equal;
	}

	// `jmp target` written at `at`, and a nop for the sixth byte of the two instructions it replaces.
	inline std::array<std::uint8_t, 6> jump(address at, address target)
	{
		std::array<std::uint8_t, 6> bytes{0xe9, 0, 0, 0, 0, 0x90};
		const address relative = target - at - 5;
		std::memcpy(bytes.data() + 1, &relative, 4);
		return bytes;
	}

	// Authored x86 bridge, shared with execution tests. EDI=incoming, ESI=candidate.
	// The callback returns nonzero to keep the retail comparison, zero to advance.
	// It saves the general registers, the flags and the x87/SSE state (fxsave, on a 16-byte aligned
	// block of the stack) around the call, and puts them all back on both ways out.
	inline std::vector<std::uint8_t> bridge(address callback, address resume, address reject)
	{
		std::vector<std::uint8_t> b{
			0x9c,                            // pushfd
			0x60,                            // pushad
			0x8b, 0xc4,                      // mov eax, esp
			0x81, 0xec, 0x10, 0x02, 0, 0,    // sub esp, 0x210
			0x83, 0xe4, 0xf0,                // and esp, -16
			0x89, 0x84, 0x24, 0, 0x02, 0, 0, // mov [esp + 0x200], eax
			0x0f, 0xae, 0x04, 0x24,          // fxsave [esp]
			0x56,                            // push esi (candidate)
			0x57,                            // push edi (incoming)
			0xb8,                            // mov eax, callback
		};
		const auto append_word = [&](address v)
		{
			for (unsigned n = 0; n < 4; ++n)
			{
				b.push_back(static_cast<std::uint8_t>(v >> (8 * n)));
			}
		};
		append_word(callback);
		b.insert(b.end(), {
			0xff, 0xd0,       // call eax
			0x83, 0xc4, 0x08, // add esp, 8
			0x85, 0xc0,       // test eax, eax
			0x75, 0,          // jnz keep (the distance is filled in below)
		});
		const auto branch = b.size() - 1;
		const auto restore = [&]
		{
			b.insert(b.end(), {
				0x0f, 0xae, 0x0c, 0x24,          // fxrstor [esp]
				0x8b, 0xa4, 0x24, 0, 0x02, 0, 0, // mov esp, [esp + 0x200]
				0x61,                            // popad
				0x9d,                            // popfd
			});
		};

		// Zero: the next candidate.
		restore();
		b.push_back(0x68); // push reject
		append_word(reject);
		b.push_back(0xc3); // ret
		b[branch] = static_cast<std::uint8_t>(b.size() - (branch + 1));

		// Nonzero (keep): the two instructions the jump displaced, then back into the vertex loop.
		restore();
		b.insert(b.end(), {0x8b, 0x44, 0x24, 0x14, 0x33, 0xf6}); // guarded displaced instructions
		b.push_back(0x68); // push resume
		append_word(resume);
		b.push_back(0xc3); // ret
		return b;
	}
}
