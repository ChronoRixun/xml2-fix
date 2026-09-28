#pragma once

// The engine limit adjuster's decisions, kept apart from the patching so xml2_test can check them
// against a copy of XMen2.exe: what [Limits] in xml2-fix.ini asks for, the layout of each relocated
// structure for a given cap, and every instruction of XMen2.exe that carries a cap-dependent number,
// with its retail bytes (compared before anything is written), where the number sits in it, its
// retail value and what it becomes.
//
// Every address is the retail build's (XMen2.exe sha1 7d95cdb4a9a599b982147a3389ea7aff211cb82b,
// image base 0x400000, no relocations). The research is in the xml1-port repository,
// research/limits/actor-table.md (sections 5, 7) and igb-cache.md (section 8.1); the rows below were
// regenerated from the exe with capstone, which found four fields of the name table the notes had
// missed (0x55a6c0, 0x55a6c7, 0x55ae88, 0x55af40) and that its node count is one field seen from two
// bases. Two structures:
//
// - The actor table, CAnimMotionCache: a static object at 0x7b05e8 with 40 slots for actor skins and
//   animation databases. When all 40 are in use, precache (0x56b1c0) quietly returns NULL, a
//   CModelActor stores it, and the next animation faults at 0x5743bb. Raised to N (at most 127:
//   sixteen of its compares are `cmp r/m32, imm8`, sign-extended) by moving it into a block of the
//   DLL: the game's own constructor, destructor and methods work on the block once their 74
//   displacements and immediates, three references to the object and two calls are patched.
// - The global resource name table, ratl::map_os<string_vs<68>, SCacheHandle, 450>: every record of
//   the actor table, the IGB cache, the textures, the playfields and the motion paths registers a
//   name there, and when it is full precache returns NULL the same way. Raised to M (451 to 4096)
//   by 71 fields; the DLL builds the table with the game's own constructor and stores it where the
//   game's getter looks, before the game asks for it.

#include <Windows.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace limits_rules
{
	constexpr DWORD image_base = 0x400000;

	// ---- [Limits] in xml2-fix.ini -------------------------------------------------------------------

	constexpr int stock_actor_slots = 40;
	constexpr int max_actor_slots = 127; // the imm8 compares hold 0x7f at most
	constexpr int stock_resource_names = 450;
	constexpr int max_resource_names = 4096;
	constexpr int default_resource_names = 1024; // what ActorSlots brings along when ResourceNames is absent

	// Every record of the actor table takes a name, so the name table has to grow by at least the
	// actor table's growth. (The IGB cache's raise, when it comes, adds its own growth here.)
	constexpr int names_needed(const int actor_slots)
	{
		return stock_resource_names + std::max(0, actor_slots - stock_actor_slots);
	}

	inline std::string_view trimmed(std::string_view text)
	{
		const auto first = text.find_first_not_of(" \t");
		if (first == std::string_view::npos)
		{
			return {};
		}
		return text.substr(first, text.find_last_not_of(" \t") - first + 1);
	}

	// What the ini says, without an inline "; comment" (the profile API keeps those).
	inline std::string_view value_text(std::string_view text)
	{
		if (const auto semicolon = text.find(';'); semicolon != std::string_view::npos)
		{
			text = text.substr(0, semicolon);
		}
		return trimmed(text);
	}

	// A count: decimal digits only (no sign, no hex), at most six of them.
	inline std::optional<int> parse_count(const std::string_view text)
	{
		const auto value = value_text(text);
		if (value.empty() || value.size() > 6 || !std::ranges::all_of(value, [](const char c) { return c >= '0' && c <= '9'; }))
		{
			return std::nullopt;
		}
		int result = 0;
		for (const char c : value)
		{
			result = result * 10 + (c - '0');
		}
		return result;
	}

	struct choice
	{
		int actor_slots = stock_actor_slots;
		int resource_names = stock_resource_names;
		std::vector<std::string> notes; // for the log, in order
	};

	// ActorSlots: 41..127 raises the actor table, 40 is the game's own, anything else is logged and
	// left stock. ResourceNames: 451..4096 raises the name table, 450 is the game's own, anything else
	// is logged and ignored. ActorSlots above 40 needs a bigger name table: without a (usable)
	// ResourceNames it gets 1024, and a ResourceNames too small for it is raised to what it needs.
	inline choice decide(const std::optional<std::string_view> actor_slots, const std::optional<std::string_view> resource_names)
	{
		choice result;
		if (actor_slots)
		{
			const auto value = parse_count(*actor_slots);
			if (value && *value >= stock_actor_slots && *value <= max_actor_slots)
			{
				result.actor_slots = *value;
			}
			else
			{
				result.notes.push_back("ActorSlots=" + std::string(value_text(*actor_slots)) + " isn't a number from 41 to 127 (40 is the game's own) - the actor table stays at 40 slots");
			}
		}
		bool names_set = false;
		if (resource_names)
		{
			const auto value = parse_count(*resource_names);
			if (value && *value >= stock_resource_names && *value <= max_resource_names)
			{
				result.resource_names = *value;
				names_set = true;
			}
			else
			{
				result.notes.push_back("ResourceNames=" + std::string(value_text(*resource_names)) + " isn't a number from 451 to 4096 (450 is the game's own) - ignored");
			}
		}
		if (result.actor_slots > stock_actor_slots)
		{
			const int needed = names_needed(result.actor_slots);
			if (!names_set)
			{
				result.resource_names = std::max(default_resource_names, needed);
				result.notes.push_back("ActorSlots=" + std::to_string(result.actor_slots) + " needs a bigger resource name table (every actor record takes a name) - ResourceNames=" +
				                       std::to_string(result.resource_names) + ", as no ResourceNames says otherwise");
			}
			else if (result.resource_names < needed)
			{
				result.notes.push_back("ResourceNames=" + std::to_string(result.resource_names) + " is too few for ActorSlots=" + std::to_string(result.actor_slots) +
				                       " (450 plus one per actor slot over 40) - using " + std::to_string(needed));
				result.resource_names = needed;
			}
		}
		return result;
	}

	// ---- Layouts ------------------------------------------------------------------------------------

	constexpr DWORD dwords_for_bits(const int bits)
	{
		return static_cast<DWORD>((bits + 31) / 32);
	}

	// CAnimMotionCache: its vtable at +0, then a fixed-capacity pool S at +4 - N records of 0x2e0
	// bytes, the "allocated" bitmap, a ring of N+1 free indices with its write position, read position
	// and count, the "live" bitmap, the live count, N ids ((generation << shift) | index), the id mask
	// and the shift. Offsets are S-relative. The cache's methods reach the live count and the mask
	// this-relative, which is S-relative + 4: the S-relative ids and shift (live and ids, mask and
	// shift stay adjacent, so one number serves both). The pool constructor zeroes only two dwords of
	// each bitmap, so a bigger pool must be handed zero-filled memory. actor_layout_for(40) is the
	// retail layout (object 0x7470 bytes: its init guard 0x7b7a58 sits right after it).
	struct actor_layout
	{
		int slots = 0;
		DWORD bitmap_words = 0;
		DWORD bitmap_a = 0;
		DWORD ring = 0;
		DWORD ring_write = 0;
		DWORD ring_read = 0;
		DWORD ring_count = 0;
		DWORD bitmap_b = 0;
		DWORD live = 0;
		DWORD ids = 0;
		DWORD mask = 0;
		DWORD shift = 0;
		DWORD pool_size = 0;
		DWORD object_size = 0; // vtable + pool
		DWORD id_mask = 0;     // what the pool constructor derives from N-1 (0x56ac5d-0x56ac6e)
		DWORD id_shift = 0;
	};

	constexpr DWORD actor_record_size = 0x2e0;

	constexpr actor_layout actor_layout_for(const int slots)
	{
		actor_layout l;
		const auto n = static_cast<DWORD>(slots);
		l.slots = slots;
		l.bitmap_words = dwords_for_bits(slots);
		l.bitmap_a = n * actor_record_size;
		l.ring = l.bitmap_a + l.bitmap_words * 4;
		l.ring_write = l.ring + (n + 1) * 4;
		l.ring_read = l.ring_write + 4;
		l.ring_count = l.ring_read + 4;
		l.bitmap_b = l.ring_count + 4;
		l.live = l.bitmap_b + l.bitmap_words * 4;
		l.ids = l.live + 4;
		l.mask = l.ids + n * 4;
		l.shift = l.mask + 4;
		l.pool_size = l.shift + 4;
		l.object_size = l.pool_size + 4;
		// do { mask = mask * 2 | 1; ++shift; } while (seed >>= 1);
		DWORD seed = n - 1;
		do
		{
			l.id_mask = l.id_mask * 2 | 1;
			++l.id_shift;
			seed >>= 1;
		} while (seed);
		return l;
	}

	// ratl::map_os<string_vs<68>, SCacheHandle, M>, a heap object the getter 0x55af80 keeps at
	// 0x7ac244: vtable, tree root and last-allocated node at +0/+4/+8; the node pool P at +0xc (M tree
	// nodes of 0x50 bytes, 4 bytes, a ring of M+1 free node indices with its write position, read
	// position and count, the node bitmap, the node count); then M SCacheHandle entries of 16 bytes
	// (name, group, owner, id; entry i belongs to node i) and the entry bitmap. P-relative offsets for
	// the pool, map-relative for the rest. The node count is the map's size: the pool reaches it
	// P-relative, isFull and add map-relative (retail 0x93f8 and 0x9404, the same dword). releaseGroup
	// and remove address an entry's owner as (index + (entries + 8) / 16) * 16, so the entries start
	// at 8 mod 16 (retail 0x9408, the dword after the count; a bigger table may need up to 12 bytes
	// between them). name_layout_for(450) is the retail layout (0xb064 bytes).
	struct name_layout
	{
		int capacity = 0;
		DWORD bitmap_words = 0;
		DWORD node_ring = 0;
		DWORD node_ring_write = 0;
		DWORD node_ring_read = 0;
		DWORD node_ring_count = 0;
		DWORD node_bitmap = 0;
		DWORD node_live = 0;
		DWORD count = 0; // map-relative from here
		DWORD entries = 0;
		DWORD entry_bitmap = 0;
		DWORD size = 0;
	};

	constexpr DWORD name_node_size = 0x50;
	constexpr DWORD name_pool_offset = 0xc;
	constexpr DWORD name_entry_size = 16;

	constexpr name_layout name_layout_for(const int capacity)
	{
		name_layout l;
		const auto m = static_cast<DWORD>(capacity);
		l.capacity = capacity;
		l.bitmap_words = dwords_for_bits(capacity);
		l.node_ring = m * name_node_size + 4;
		l.node_ring_write = l.node_ring + (m + 1) * 4;
		l.node_ring_read = l.node_ring_write + 4;
		l.node_ring_count = l.node_ring_read + 4;
		l.node_bitmap = l.node_ring_count + 4;
		l.node_live = l.node_bitmap + l.bitmap_words * 4;
		l.count = name_pool_offset + l.node_live;
		const DWORD pool_end = l.count + 4;
		l.entries = pool_end + (24 - pool_end % 16) % 16; // the first offset from pool_end that is 8 mod 16
		l.entry_bitmap = l.entries + m * name_entry_size;
		l.size = l.entry_bitmap + l.bitmap_words * 4;
		return l;
	}

	// ---- What each patched number is ----------------------------------------------------------------

	enum class actor_field : std::uint8_t
	{
		slots,               // N: the cap check, the bound of every index loop, the "none" sentinel
		last_slot,           // N-1: the ring's wrap, the mask/shift seed
		bitmap_a,            // S-relative, as the layout
		ring,
		ring_write,
		ring_read,
		ring_count,
		bitmap_b,
		live,
		ids,
		mask,
		shift,
		live_via_cache,      // this-relative (the cache's methods): live + 4
		mask_via_cache,      // mask + 4
		ring_read_via_ring,  // ring-relative (0x455a50)
		ring_count_via_ring,
		object,              // the object's address (imm32)
		clone_call,          // rel32 of a call to the clone of 0x4554e0
	};

	enum class name_field : std::uint8_t
	{
		capacity,                 // M
		last_index,               // M-1: the ring's wrap
		bitmap_words,             // the rep stosd counts that clear a bitmap
		table_size,               // the getter's allocation
		node_ring,                // P-relative
		node_ring_write,
		node_ring_read,
		node_ring_count,
		node_bitmap,
		node_live,
		ring_write_via_ring,      // ring-relative
		ring_read_via_ring,
		ring_count_via_ring,
		count,                    // map-relative
		entries,
		entry_group,              // entries + 4
		entry_id,                 // entries + 0xc
		entry_owner_index,        // (entries + 8) / 16
		entry_bitmap,
		entry_bitmap_via_entries, // entries-relative (M * 16)
	};

	constexpr std::uint32_t value_of(const actor_field field, const actor_layout& l)
	{
		switch (field)
		{
		case actor_field::slots: return static_cast<std::uint32_t>(l.slots);
		case actor_field::last_slot: return static_cast<std::uint32_t>(l.slots - 1);
		case actor_field::bitmap_a: return l.bitmap_a;
		case actor_field::ring: return l.ring;
		case actor_field::ring_write: return l.ring_write;
		case actor_field::ring_read: return l.ring_read;
		case actor_field::ring_count: return l.ring_count;
		case actor_field::bitmap_b: return l.bitmap_b;
		case actor_field::live: return l.live;
		case actor_field::ids: return l.ids;
		case actor_field::mask: return l.mask;
		case actor_field::shift: return l.shift;
		case actor_field::live_via_cache: return l.live + 4;
		case actor_field::mask_via_cache: return l.mask + 4;
		case actor_field::ring_read_via_ring: return l.ring_read - l.ring;
		case actor_field::ring_count_via_ring: return l.ring_count - l.ring;
		default: return 0; // object, clone_call: addresses, see reference_value
		}
	}

	constexpr std::uint32_t value_of(const name_field field, const name_layout& l)
	{
		switch (field)
		{
		case name_field::capacity: return static_cast<std::uint32_t>(l.capacity);
		case name_field::last_index: return static_cast<std::uint32_t>(l.capacity - 1);
		case name_field::bitmap_words: return l.bitmap_words;
		case name_field::table_size: return l.size;
		case name_field::node_ring: return l.node_ring;
		case name_field::node_ring_write: return l.node_ring_write;
		case name_field::node_ring_read: return l.node_ring_read;
		case name_field::node_ring_count: return l.node_ring_count;
		case name_field::node_bitmap: return l.node_bitmap;
		case name_field::node_live: return l.node_live;
		case name_field::ring_write_via_ring: return l.node_ring_write - l.node_ring;
		case name_field::ring_read_via_ring: return l.node_ring_read - l.node_ring;
		case name_field::ring_count_via_ring: return l.node_ring_count - l.node_ring;
		case name_field::count: return l.count;
		case name_field::entries: return l.entries;
		case name_field::entry_group: return l.entries + 4;
		case name_field::entry_id: return l.entries + 0xc;
		case name_field::entry_owner_index: return (l.entries + 8) / name_entry_size;
		case name_field::entry_bitmap: return l.entry_bitmap;
		case name_field::entry_bitmap_via_entries: return static_cast<std::uint32_t>(l.capacity) * name_entry_size;
		}
		return 0;
	}

	// ---- Retail bytes -------------------------------------------------------------------------------

	constexpr int hex_digit(const char c)
	{
		return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
	}

	constexpr std::uint8_t hex_byte(const std::string_view hex, const std::size_t index)
	{
		return static_cast<std::uint8_t>(hex_digit(hex[index * 2]) * 16 + hex_digit(hex[index * 2 + 1]));
	}

	constexpr std::size_t hex_size(const std::string_view hex)
	{
		return hex.size() / 2;
	}

	constexpr bool valid_hex(const std::string_view hex)
	{
		if (hex.empty() || hex.size() % 2)
		{
			return false;
		}
		for (const char c : hex)
		{
			if (hex_digit(c) < 0)
			{
				return false;
			}
		}
		return true;
	}

	// Whether the bytes at `at` are the hex string's.
	inline bool matches(const std::uint8_t* at, const std::string_view hex)
	{
		for (std::size_t i = 0; i < hex_size(hex); ++i)
		{
			if (at[i] != hex_byte(hex, i))
			{
				return false;
			}
		}
		return true;
	}

	// One number in one instruction of XMen2.exe: the instruction's address and retail bytes, where the
	// number sits in them, how wide it is (1 = an imm8, 4 = a disp32 or imm32) and its retail value.
	template <typename Field>
	struct site
	{
		DWORD va;
		std::string_view hex;
		std::uint8_t offset;
		std::uint8_t size;
		std::uint32_t retail;
		Field field;
	};

	using actor_site = site<actor_field>;
	using name_site = site<name_field>;

	// Retail bytes that aren't patched but have to be what they are (code the patch relies on).
	struct guard
	{
		DWORD va;
		std::string_view hex;
		const char* what;
	};

	// ---- The actor table's sites (every N-dependent field of the 14 functions that use the pool) ----

	inline constexpr std::array<actor_site, 74> actor_sites{{
		// ring<40>::pop (0x455a50; its only caller is takeFreeIndex, 0x56b669)
		{0x455a50, "8b81a8000000", 2, 4, 0xa8, actor_field::ring_read_via_ring},
		{0x455a57, "8981a8000000", 2, 4, 0xa8, actor_field::ring_read_via_ring},
		{0x455a5d, "83f828", 2, 1, 0x28, actor_field::slots},
		{0x455a60, "8b81ac000000", 2, 4, 0xac, actor_field::ring_count_via_ring},
		{0x455a68, "c781a800000000000000", 2, 4, 0xa8, actor_field::ring_read_via_ring},
		{0x455a73, "8981ac000000", 2, 4, 0xac, actor_field::ring_count_via_ring},
		// pool destructor (0x56aab0), its bitmap scan inlined
		{0x56aab3, "8b8300730000", 2, 4, 0x7300, actor_field::bitmap_a},
		{0x56aaba, "8dbb00730000", 2, 4, 0x7300, actor_field::bitmap_a},
		{0x56aacc, "83f928", 2, 1, 0x28, actor_field::slots},
		{0x56ab0d, "83f928", 2, 1, 0x28, actor_field::slots},
		{0x56ab32, "83f828", 2, 1, 0x28, actor_field::slots},
		{0x56ab56, "83f828", 2, 1, 0x28, actor_field::slots},
		{0x56ab98, "83f828", 2, 1, 0x28, actor_field::slots},
		// ring fill (0x56abb0)
		{0x56abb4, "89b1ac730000", 2, 4, 0x73ac, actor_field::ring_write},
		{0x56abba, "89b1b0730000", 2, 4, 0x73b0, actor_field::ring_read},
		{0x56abc0, "89b1b4730000", 2, 4, 0x73b4, actor_field::ring_count},
		{0x56abd0, "8b81ac730000", 2, 4, 0x73ac, actor_field::ring_write},
		{0x56abd6, "8bb9b4730000", 2, 4, 0x73b4, actor_field::ring_count},
		{0x56abde, "83f828", 2, 1, 0x28, actor_field::slots},
		{0x56abe1, "8981ac730000", 2, 4, 0x73ac, actor_field::ring_write},
		{0x56abe7, "89b9b4730000", 2, 4, 0x73b4, actor_field::ring_count},
		{0x56abef, "89b1ac730000", 2, 4, 0x73ac, actor_field::ring_write},
		{0x56abf5, "b827000000", 1, 4, 0x27, actor_field::last_slot},
		{0x56abfd, "89948108730000", 3, 4, 0x7308, actor_field::ring},
		{0x56ac05, "83fa28", 2, 1, 0x28, actor_field::slots},
		// pool constructor (0x56ac10)
		{0x56ac15, "8d8e00730000", 2, 4, 0x7300, actor_field::bitmap_a},
		{0x56ac23, "89beac730000", 2, 4, 0x73ac, actor_field::ring_write},
		{0x56ac29, "89beb0730000", 2, 4, 0x73b0, actor_field::ring_read},
		{0x56ac2f, "89beb4730000", 2, 4, 0x73b4, actor_field::ring_count},
		{0x56ac37, "8d86b8730000", 2, 4, 0x73b8, actor_field::bitmap_b},
		{0x56ac44, "89bec0730000", 2, 4, 0x73c0, actor_field::live},
		{0x56ac51, "89be64740000", 2, 4, 0x7464, actor_field::mask},
		{0x56ac57, "89be68740000", 2, 4, 0x7468, actor_field::shift},
		{0x56ac5d, "b927000000", 1, 4, 0x27, actor_field::last_slot},
		{0x56ac70, "898664740000", 2, 4, 0x7464, actor_field::mask},
		{0x56ac76, "899668740000", 2, 4, 0x7468, actor_field::shift},
		{0x56ac7e, "8d96c4730000", 2, 4, 0x73c4, actor_field::ids},
		{0x56ac92, "8b8e68740000", 2, 4, 0x7468, actor_field::shift},
		{0x56aca8, "83f828", 2, 1, 0x28, actor_field::slots},
		// unbindAll, vtable slot 6 (0x56b010)
		{0x56b021, "8d8eb8730000", 2, 4, 0x73b8, actor_field::bitmap_b},
		{0x56b02c, "83f828", 2, 1, 0x28, actor_field::slots},
		{0x56b065, "83f928", 2, 1, 0x28, actor_field::slots},
		// iterator step (0x56b070)
		{0x56b080, "8d8bb8730000", 2, 4, 0x73b8, actor_field::bitmap_b},
		// get, slot 4 (0x56b0a0)
		{0x56b16b, "8b8668740000", 2, 4, 0x7468, actor_field::mask_via_cache},
		// precache, slot 5 (0x56b1c0): the id mask, the cap check, the id
		{0x56b2a8, "8b8368740000", 2, 4, 0x7468, actor_field::mask_via_cache},
		{0x56b2c3, "83bbc473000028", 2, 4, 0x73c4, actor_field::live_via_cache},
		{0x56b2c3, "83bbc473000028", 6, 1, 0x28, actor_field::slots},
		{0x56b42e, "8b8487c4730000", 3, 4, 0x73c4, actor_field::ids},
		// alloc (0x56b5f0)
		{0x56b5fe, "8d948e00730000", 3, 4, 0x7300, actor_field::bitmap_a},
		// takeFreeIndex (0x56b630)
		{0x56b634, "8b8eb0730000", 2, 4, 0x73b0, actor_field::ring_read},
		{0x56b63a, "8d8608730000", 2, 4, 0x7308, actor_field::ring},
		{0x56b655, "8b8c96b8730000", 3, 4, 0x73b8, actor_field::bitmap_b},
		{0x56b65c, "8d9496b8730000", 3, 4, 0x73b8, actor_field::bitmap_b},
		{0x56b66e, "ff86c0730000", 2, 4, 0x73c0, actor_field::live},
		// release, slot 0 (0x56b730)
		{0x56b735, "8bb168740000", 2, 4, 0x7468, actor_field::mask_via_cache},
		// bumpGeneration (0x56b760)
		{0x56b766, "8b8868740000", 2, 4, 0x7468, actor_field::shift},
		{0x56b774, "01b490c4730000", 3, 4, 0x73c4, actor_field::ids},
		{0x56b77d, "899490c4730000", 3, 4, 0x73c4, actor_field::ids},
		{0x56b784, "8b8868740000", 2, 4, 0x7468, actor_field::shift},
		{0x56b793, "89b490c4730000", 3, 4, 0x73c4, actor_field::ids},
		// free (0x56b7a0)
		{0x56b7bf, "8b8c86b8730000", 3, 4, 0x73b8, actor_field::bitmap_b},
		{0x56b7c6, "8d8486b8730000", 3, 4, 0x73b8, actor_field::bitmap_b},
		{0x56b7d3, "8b86ac730000", 2, 4, 0x73ac, actor_field::ring_write},
		{0x56b7d9, "8b96b4730000", 2, 4, 0x73b4, actor_field::ring_count},
		{0x56b7e1, "83f828", 2, 1, 0x28, actor_field::slots},
		{0x56b7e4, "8986ac730000", 2, 4, 0x73ac, actor_field::ring_write},
		{0x56b7ea, "8996b4730000", 2, 4, 0x73b4, actor_field::ring_count},
		{0x56b7f2, "c786ac73000000000000", 2, 4, 0x73ac, actor_field::ring_write},
		{0x56b7fc, "b827000000", 1, 4, 0x27, actor_field::last_slot},
		{0x56b804, "89bc8608730000", 3, 4, 0x7308, actor_field::ring},
		{0x56b80b, "8b86c0730000", 2, 4, 0x73c0, actor_field::live},
		{0x56b813, "8986c0730000", 2, 4, 0x73c0, actor_field::live},
		// destroy record (0x56b820)
		{0x56b848, "8b8c8600730000", 3, 4, 0x7300, actor_field::bitmap_a},
		{0x56b84f, "8d848600730000", 3, 4, 0x7300, actor_field::bitmap_a},
	}};

	// The object's address in the getter (0x56b8e0: the constructor's `this`, the value it returns)
	// and in the atexit thunk (the destructor's `this`); the two calls of the 40-bit findNext that
	// scan the pool's live bitmap (unbindAll and the iterator step), which go to the clone instead.
	// Nothing else in the image refers to 0x7b05e8..0x7b7a57. The init guard 0x7b7a58 stays where it
	// is, outside the object, so the once-only construction and its unwind funclet work unchanged.
	constexpr DWORD actor_object_retail = 0x7b05e8;
	constexpr DWORD find_next_40_va = 0x4554e0;

	inline constexpr std::array<actor_site, 5> actor_references{{
		{0x56b027, "e8b4a4eeff", 1, 4, 0xffeea4b4, actor_field::clone_call}, // unbindAll
		{0x56b086, "e855a4eeff", 1, 4, 0xffeea455, actor_field::clone_call}, // iterator step
		{0x56b90a, "b9e8057b00", 1, 4, actor_object_retail, actor_field::object}, // getter: mov ecx, object (the constructor's this)
		{0x56b92c, "b8e8057b00", 1, 4, actor_object_retail, actor_field::object}, // getter: mov eax, object (returned)
		{0x67e160, "b9e8057b00", 1, 4, actor_object_retail, actor_field::object}, // atexit thunk: mov ecx, object; jmp 0x56b870
	}};

	// The getter as a whole (the guard dword, the constructor call, the atexit registration), the
	// atexit thunk, and ring<40>::pop's only call.
	inline constexpr std::array<guard, 3> actor_guards{{
		{0x56b8e0, "64a1000000008a0d587a7b006aff68de63670050b80100000084c86489250000000075250905587a7b00b9e8057b00c744240800000000e8b4f3ffff6860e16700e8f867100083c4048b0c24b8e8057b0064890d0000000083c40cc3",
		 "the actor table's getter (0x56b8e0)"},
		{0x67e160, "b9e8057b00e906d7eeff", "the actor table's atexit thunk (0x67e160)"},
		{0x56b669, "e8e2a3eeff", "takeFreeIndex's call of ring<40>::pop (0x56b669)"},
	}};

	constexpr DWORD actor_init_guard = 0x7b7a58; // bit 0: the getter has built the object

	// bitset<40>::findNext (0x4554e0): 0x8f bytes, position-independent (short jumps only - no E8/E9
	// and no 0F 8x byte anywhere in it - no calls, no globals). A second, unrelated 40-bit set uses it
	// (0x45ced8), so the pool's two callers get a copy with N in its five places instead.
	struct clone_field
	{
		std::uint8_t offset; // in the function
		std::uint8_t size;
		std::uint32_t retail;
		actor_field field;
	};

	struct function_clone
	{
		DWORD va;
		std::string_view hex;                   // the whole function, retail
		std::array<clone_field, 5> fields;
		std::array<std::uint8_t, 4> rel32{};    // offsets of rel32 operands (calls, jumps) to re-aim when moved
		std::size_t rel32_count = 0;
	};

	inline constexpr function_clone find_next_40{
		find_next_40_va,
		"8b44240483f828568bf17c09b8280000005ec20800538a5c24108bc8c1f90584db8b148e7502f7d28bc883e11fd3ea85d2751d83e0e083c02083f8287d478bd0c1fa0584db8b14967502f7d285d274e3f7c2ffff0000750683c010c1ea1084d2750683c008c1ea08f6c20f750683c004c1ea04f6c2017508d1ea40f6c20174f883f8287c05b8280000005b5ec20800",
		{{
			{0x06, 1, 0x28, actor_field::slots}, // cmp eax, 0x28 (the start index)
			{0x0d, 4, 0x28, actor_field::slots}, // mov eax, 0x28 (none)
			{0x3b, 1, 0x28, actor_field::slots}, // cmp eax, 0x28 (next word)
			{0x82, 1, 0x28, actor_field::slots}, // cmp eax, 0x28 (the found index)
			{0x86, 4, 0x28, actor_field::slots}, // mov eax, 0x28 (none)
		}},
		{},
		0,
	};

	// ---- The name table's sites (every M-dependent field of its 17 functions) ------------------------

	inline constexpr std::array<name_site, 71> name_sites{{
		// bitset<450>::findNext (0x55a600; its only caller is clearAll, 0x55a729)
		{0x55a604, "3dc2010000", 1, 4, 0x1c2, name_field::capacity},
		{0x55a60e, "b8c2010000", 1, 4, 0x1c2, name_field::capacity},
		{0x55a63b, "3dc2010000", 1, 4, 0x1c2, name_field::capacity},
		{0x55a688, "3dc2010000", 1, 4, 0x1c2, name_field::capacity},
		{0x55a68f, "b8c2010000", 1, 4, 0x1c2, name_field::capacity},
		// isFull (0x55a6a0)
		{0x55a6a0, "8b9104940000", 2, 4, 0x9404, name_field::count},
		{0x55a6a8, "81fac2010000", 2, 4, 0x1c2, name_field::capacity},
		// the entry bitmap's clearAll (0x55a6c0; ecx = the entries, from 0x55af40)
		{0x55a6c0, "8b81201c0000", 2, 4, 0x1c20, name_field::entry_bitmap_via_entries},
		{0x55a6c7, "8db9201c0000", 2, 4, 0x1c20, name_field::entry_bitmap_via_entries},
		{0x55a6d9, "81fac2010000", 2, 4, 0x1c2, name_field::capacity},
		{0x55a719, "81fac2010000", 2, 4, 0x1c2, name_field::capacity},
		{0x55a72e, "3dc2010000", 1, 4, 0x1c2, name_field::capacity},
		{0x55a735, "b90f000000", 1, 4, 0xf, name_field::bitmap_words},
		// node ring constructor (0x55a740; ecx = the ring)
		{0x55a744, "89880c070000", 2, 4, 0x70c, name_field::ring_write_via_ring},
		{0x55a74a, "898810070000", 2, 4, 0x710, name_field::ring_read_via_ring},
		{0x55a750, "898814070000", 2, 4, 0x714, name_field::ring_count_via_ring},
		// node alloc (0x55a810; ecx = P)
		{0x55a812, "8b82b4930000", 2, 4, 0x93b4, name_field::node_ring_read},
		{0x55a818, "8b8482a48c0000", 3, 4, 0x8ca4, name_field::node_ring},
		{0x55a825, "8db48abc930000", 3, 4, 0x93bc, name_field::node_bitmap},
		{0x55a83b, "8bb2b4930000", 2, 4, 0x93b4, name_field::node_ring_read},
		{0x55a844, "81f9c2010000", 2, 4, 0x1c2, name_field::capacity},
		{0x55a84a, "89b2b4930000", 2, 4, 0x93b4, name_field::node_ring_read},
		{0x55a852, "c782b493000000000000", 2, 4, 0x93b4, name_field::node_ring_read},
		{0x55a85c, "ff8ab8930000", 2, 4, 0x93b8, name_field::node_ring_count},
		{0x55a862, "8b8af8930000", 2, 4, 0x93f8, name_field::node_live},
		{0x55a86a, "898af8930000", 2, 4, 0x93f8, name_field::node_live},
		// node ring push (0x55a880; ecx = the ring)
		{0x55a880, "8b810c070000", 2, 4, 0x70c, name_field::ring_write_via_ring},
		{0x55a886, "8b9114070000", 2, 4, 0x714, name_field::ring_count_via_ring},
		{0x55a88e, "3dc2010000", 1, 4, 0x1c2, name_field::capacity},
		{0x55a893, "89810c070000", 2, 4, 0x70c, name_field::ring_write_via_ring},
		{0x55a899, "899114070000", 2, 4, 0x714, name_field::ring_count_via_ring},
		{0x55a8a5, "c7810c07000000000000", 2, 4, 0x70c, name_field::ring_write_via_ring},
		{0x55a8b1, "b8c1010000", 1, 4, 0x1c1, name_field::last_index},
		// node ring fill (0x55a8d0; ecx = P)
		{0x55a8d4, "89b1b0930000", 2, 4, 0x93b0, name_field::node_ring_write},
		{0x55a8da, "89b1b4930000", 2, 4, 0x93b4, name_field::node_ring_read},
		{0x55a8e0, "89b1b8930000", 2, 4, 0x93b8, name_field::node_ring_count},
		{0x55a8f0, "8b81b0930000", 2, 4, 0x93b0, name_field::node_ring_write},
		{0x55a8f6, "8bb9b8930000", 2, 4, 0x93b8, name_field::node_ring_count},
		{0x55a8fe, "3dc2010000", 1, 4, 0x1c2, name_field::capacity},
		{0x55a903, "8981b0930000", 2, 4, 0x93b0, name_field::node_ring_write},
		{0x55a909, "89b9b8930000", 2, 4, 0x93b8, name_field::node_ring_count},
		{0x55a911, "89b1b0930000", 2, 4, 0x93b0, name_field::node_ring_write},
		{0x55a917, "b8c1010000", 1, 4, 0x1c1, name_field::last_index},
		{0x55a91f, "899481a48c0000", 3, 4, 0x8ca4, name_field::node_ring},
		{0x55a927, "81fac2010000", 2, 4, 0x1c2, name_field::capacity},
		// node pool constructor (0x55aa00; esi = P)
		{0x55aa07, "b9c2010000", 1, 4, 0x1c2, name_field::capacity},
		{0x55aa19, "8d8ea48c0000", 2, 4, 0x8ca4, name_field::node_ring},
		{0x55aa26, "b90f000000", 1, 4, 0xf, name_field::bitmap_words},
		{0x55aa2b, "8dbebc930000", 2, 4, 0x93bc, name_field::node_bitmap},
		{0x55aa35, "8986f8930000", 2, 4, 0x93f8, name_field::node_live},
		// erase (0x55aa50)
		{0x55aa7b, "8d849728b00000", 3, 4, 0xb028, name_field::entry_bitmap},
		// lookup (0x55ab00)
		{0x55ab3d, "8d843008940000", 3, 4, 0x9408, name_field::entries},
		// releaseGroup (0x55ab60)
		{0x55ab92, "8b84310c940000", 3, 4, 0x940c, name_field::entry_group},
		{0x55aba8, "8d9541090000", 2, 4, 0x941, name_field::entry_owner_index},
		{0x55abba, "8b8714940000", 2, 4, 0x9414, name_field::entry_id},
		{0x55abc8, "8b8f08940000", 2, 4, 0x9408, name_field::entries},
		{0x55ac18, "8d849628b00000", 3, 4, 0xb028, name_field::entry_bitmap},
		// remove (0x55ac60)
		{0x55accd, "39ba0c940000", 2, 4, 0x940c, name_field::entry_group},
		{0x55acd5, "0541090000", 1, 4, 0x941, name_field::entry_owner_index},
		{0x55ace9, "8b9214940000", 2, 4, 0x9414, name_field::entry_id},
		// free node, vtable slot 4 (0x55ada0 -> 0x55adb0; esi = P)
		{0x55adb8, "8d8ebc930000", 2, 4, 0x93bc, name_field::node_bitmap},
		{0x55adcc, "8d8ea48c0000", 2, 4, 0x8ca4, name_field::node_ring},
		{0x55add7, "ff8ef8930000", 2, 4, 0x93f8, name_field::node_live},
		// add (0x55adf0): the full check, the entry, its bitmap bit (entries-relative)
		{0x55ae07, "81be04940000c2010000", 2, 4, 0x9404, name_field::count},
		{0x55ae07, "81be04940000c2010000", 6, 4, 0x1c2, name_field::capacity},
		{0x55ae82, "8d9608940000", 2, 4, 0x9408, name_field::entries},
		{0x55ae88, "8db48a201c0000", 3, 4, 0x1c20, name_field::entry_bitmap_via_entries},
		// constructor (0x55af00)
		{0x55af22, "8dbe28b00000", 2, 4, 0xb028, name_field::entry_bitmap},
		{0x55af28, "b90f000000", 1, 4, 0xf, name_field::bitmap_words},
		// clear (0x55af40, from the atexit free 0x55af50): add ecx, entries
		{0x55af40, "81c108940000", 2, 4, 0x9408, name_field::entries},
		// getter (0x55af80): the allocation
		{0x55af8d, "6864b00000", 1, 4, 0xb064, name_field::table_size},
	}};

	// The getter and the constructor as a whole: the DLL builds the table with that constructor and
	// stores it where the getter looks, so the getter never allocates one (nor registers the free).
	constexpr DWORD name_table_pointer = 0x7ac244;
	constexpr DWORD name_table_constructor = 0x55af00; // __thiscall, returns this
	constexpr DWORD name_table_vtable = 0x69a708;

	inline constexpr std::array<guard, 2> name_guards{{
		{0x55af00, "568bf1578d4e0cc74604ffffff3fc74608ffffffffc706f0a66900e8e0faffff33c08dbe28b00000b90f000000f3ab5fc70608a769008bc65ec3",
		 "the resource name table's constructor (0x55af00)"},
		{0x55af80, "a144c27a0085c075376a0f6a0e6864b00000e84956000083c40c85c074098bc8e85bffffffeb0233c06850af5500a344c27a00e8986deaffa144c27a0083c404c3",
		 "the resource name table's getter (0x55af80)"},
	}};

	// TODO(IGB cache): CIGBInfoCache2's 200 records (igb-cache.md sections 1-7: a static object at
	// 0x7b7fb0, 88 fields, a clone of bitset<200>::findNext 0x490d70) are the third structure. Its
	// sites go here, as actor_sites do; its growth adds to names_needed, since every IGB record takes a
	// name too; and limits::install raises it between the name table and the actor table.

	// ---- The counters the test pipe's status reports ------------------------------------------------

	constexpr DWORD actor_live_retail = actor_object_retail + 4 + 0x73c0; // 0x7b79ac
	constexpr DWORD motion_pool_pointer = 0x7b05bc; // the motion pool (500 entries, one per animation of every resident animation database)
	constexpr DWORD motion_pool_live = 0x46e0;
	constexpr int motion_pool_capacity = 500;
	constexpr DWORD igb_live_retail = 0x7b7fb0 + 0x73ec; // CIGBInfoCache2's live count (not relocated yet)
	constexpr int igb_capacity = 200;

	inline constexpr std::array<guard, 2> motion_counter_guards{{
		{0x56a3e3, "81bfe0460000f4010000", "the motion pool's full check (0x56a3e3)"},
		{0x56a480, "a1bc057b00", "the motion pool's getter (0x56a480)"},
	}};

	inline constexpr std::array<guard, 2> igb_counter_guards{{
		{0x56ede5, "81bbec730000c8000000", "the IGB cache's cap check (0x56ede5)"},
		{0x56fa2e, "b8b07f7b00", "the IGB cache's getter (0x56fa2e)"},
	}};

	// ---- The writes ---------------------------------------------------------------------------------

	// A number to write into the image, little-endian, `size` bytes at `va`.
	struct operand_write
	{
		DWORD va;
		std::uint8_t size;
		std::uint32_t value;
	};

	// The rel32 of a call at `va` that is to land on `target`.
	constexpr std::uint32_t rel32(const DWORD va, const DWORD target)
	{
		return target - (va + 5);
	}

	constexpr std::uint32_t reference_value(const actor_site& s, const DWORD object, const DWORD clone)
	{
		return s.field == actor_field::object ? object : rel32(s.va, clone);
	}

	// Everything the actor table's raise writes into XMen2.exe for `layout`, with the object and the
	// clone at those addresses.
	inline std::vector<operand_write> actor_writes(const actor_layout& layout, const DWORD object, const DWORD clone)
	{
		std::vector<operand_write> writes;
		for (const auto& s : actor_sites)
		{
			writes.push_back({s.va + s.offset, s.size, value_of(s.field, layout)});
		}
		for (const auto& s : actor_references)
		{
			writes.push_back({s.va + s.offset, s.size, reference_value(s, object, clone)});
		}
		return writes;
	}

	inline std::vector<operand_write> name_writes(const name_layout& layout)
	{
		std::vector<operand_write> writes;
		for (const auto& s : name_sites)
		{
			writes.push_back({s.va + s.offset, s.size, value_of(s.field, layout)});
		}
		return writes;
	}

	// `image` is where XMen2.exe's image base is (0x400000 in the game).
	inline void apply_write(std::uint8_t* image, const operand_write& w)
	{
		std::memcpy(image + (w.va - image_base), &w.value, w.size);
	}

	// `code` is a copy of code made at `source_va`, to run at `dest_va`: its rel32 operands (at
	// `offsets`) are re-aimed at the targets they had. A rel32 counts from the end of its instruction,
	// which moved by dest_va - source_va.
	inline void relocate_rel32(std::uint8_t* code, const std::span<const std::uint8_t> offsets, const DWORD source_va, const DWORD dest_va)
	{
		for (const auto offset : offsets)
		{
			std::uint32_t rel = 0;
			std::memcpy(&rel, code + offset, sizeof(rel));
			rel += source_va - dest_va;
			std::memcpy(code + offset, &rel, sizeof(rel));
		}
	}

	// The clone: `code` holds a copy of the function, to run at `dest_va`. Its N fields get the
	// layout's values, its rel32 operands (none in findNext) keep their targets.
	inline void finish_clone(std::uint8_t* code, const function_clone& clone, const actor_layout& layout, const DWORD dest_va)
	{
		for (const auto& f : clone.fields)
		{
			const std::uint32_t value = value_of(f.field, layout);
			std::memcpy(code + f.offset, &value, f.size);
		}
		relocate_rel32(code, std::span<const std::uint8_t>(clone.rel32.data(), clone.rel32_count), clone.va, dest_va);
	}
}
