#pragma once

// Byte layouts (tools/lcsave/layout.py): every OLD_* table of XMen2.exe's save (SPEC-old-save section 1) and
// the fixed parts of the LC container (NEW_*), as packed structs with the reference's field order and widths.
// Everything little-endian; the sizes are the ones layout.py asserts. A `char[n]` field is the reference's
// 'cstr' (NUL-padded text): the struct keeps the raw bytes, so bytes after the NUL survive a read and write
// as the reference's '<name>_raw' does.

#include "../entity_capacity_layout.hpp"

#include <cstdint>
#include <string_view>
#include <utility>
#include <vector>

namespace lcsave
{
	using u8 = std::uint8_t;
	using u16 = std::uint16_t;
	using i16 = std::int16_t;
	using u32 = std::uint32_t;
	using i32 = std::int32_t;
	using u64 = std::uint64_t;
	using f32 = float;

	// ---------------------------------------------------------------------------------------------- old save
	constexpr u32 old_payload = 0x2fc00;            // slot payload length (envelope u32 at file offset 0x80)
	constexpr u32 old_envelope = 0x84;              // 0x80 description + u32 length
	constexpr u32 old_slot_size = old_envelope + old_payload;
	constexpr u32 old_profile_payload = 0x228;
	constexpr u32 old_tag = 52;                     // marker, 50 text bytes, marker
	constexpr u8 old_tag_marker = 0x0a;
	constexpr u32 old_def_state_pairs = 160;
	constexpr u32 old_talent_entries = 19;
	constexpr u32 old_unlocked_slots = 298;
	constexpr u32 old_restore_records = 22;
	constexpr u32 old_side_frames = 2;
	constexpr u32 old_dr_courses = 75;
	constexpr u32 old_dr_grades = 8;
	constexpr u32 old_dr_course_bytes = 26;
	constexpr u32 old_zone_string_slots = 0x800;
	constexpr u32 old_zone_arena = 0x3800;
	constexpr u32 old_zone_states = 768;
	constexpr u32 old_zone_ordinals = 256;
	constexpr u32 old_zone_dynamic = 300;
	constexpr u32 old_zone_smart = 768;
	constexpr u32 old_fog_bytes = 0x34c;
	constexpr u32 old_zone_record = 0x55e0;
	constexpr u32 old_script_zone_vars = 32;
	constexpr u32 old_script_game_vars = 100;
	constexpr u32 old_high_scores = 10;
	constexpr u32 old_power_name = 20;      // a power slot's name field (at most 19 bytes and the NUL, 0x4b84d8)
	constexpr u8 power_usable = 1;          // bit 0 of a power slot's flag byte; also the byte of a slot the new file omits

#pragma pack(push, 1)
	// items section head (old 1.4) up to the carried-item count
	struct OldItemHead
	{
		u8 def_count;
		u8 def_state[old_def_state_pairs * 2];         // pairs {quantity, flag} by definition index
		u32 tech_bits;
		u32 health_items;
		u32 energy_items;
		u32 key_counts[6];                             // KEY1-3, KEYCARD1-3 by definition name
	};
	// item instance fixed head (old 1.5); then three lists of {u16 record, u32 value} with u32 counts
	struct OldInstanceHead
	{
		u8 definition;
		u8 name_a;                                     // instance+0x2c, 0xff = none
		u8 name_b;                                     // instance+0x2d, 0xff = none
		u8 special;                                    // instance+0x2f, 0xff = none
		u8 location;                                   // instance+0x2e
	};
	struct OldEnhElement
	{
		u16 record;
		u32 value;
	};
	// CAttributes scalars (old 1.6 a)
	struct OldAttrScalars
	{
		u8 trait_base[4];
		u8 trait_pending[4];
		u8 trait_bonus[4];
		u8 trait_bonus2[4];
		u16 skill_points;
		u16 trait_points;
		u8 level;
		u8 ai_level;
		u8 ai_attack;
		u8 pad17;
	};
	// talent heap entry (old 1.6 b)
	struct OldTalentEntry
	{
		u16 id;
		u8 rank_bits;
		u8 pending_cap_bits;
		u8 base_bits;
		u8 byte5;
		u16 pad6;
	};
	struct OldTalentHeapTail
	{
		u32 bitmap;
		u32 count;
	};
	// equipment tail (old 1.6 c), after the equipped instances
	struct OldEquipTail
	{
		f32 health;
		f32 energy;
		i32 player_seat;
		u8 vitals_valid;
		u8 pad0d[3];
		u32 experience;
		u8 ack_level;
		u8 health_boosts;
		u8 energy_boosts;
		u8 levels_purchased;
		u8 points_bought;
		u8 respec_count;
		u8 skirmish_boost;
		u8 flags;
	};
	// powers (old 1.6 d): four slots of a NUL-terminated name and a flag byte (bit 0 = usable: CStats+0xc4 + 0x15 n + 0x14,
	// get 0x4b83b0, set 0x4b83e0; the power handler 0x4fd4de refuses a slot without it), and a pad byte
	struct OldPowerSlot
	{
		char name[old_power_name];
		u8 flags;
	};
	struct OldPowers
	{
		OldPowerSlot power[4];
		u8 pad54;
	};
	// costume (old 1.6 e)
	struct OldCostume
	{
		u8 costume;
		u32 costume_mask;
	};
	// game block (old 1.8)
	struct OldGameBlock
	{
		f32 timer_threshold;
		f32 timer_remaining;
		f32 timer_after_add;
		f32 timer_last_add;
		char timer_script[0x40];
		f32 recall_cooldown;
		i16 xtreme_charges;
		i16 xtreme_pips;
		char loading_name[0x20];
		char recall_zone[0x20];
		f32 recall_pos[3];
		u8 act;
		u8 unused_a5[6];
		u8 pad_ab;
		u32 trivia[8];
		u32 value_608;
		u32 difficulty;
		f32 play_time;
		u8 flags_d8;
		u8 flags_d9;
		u8 flags_da;
		u8 pad_db;
	};
	// mission snapshot (old 1.9)
	struct OldSnapshot
	{
		char zone[0x40];
		f32 seat_pose[24];
		u32 pad_a0;
		char seat_name[4][0x20];
		u8 pad_124;
		u8 objectives[300];
		u8 objective_301;
		u16 pad_252;
		f32 anchor_pos[3];
		f32 anchor_orient[3];
	};
	struct OldSideTail
	{
		u32 pad_4d8;
		u32 count;
	};
	// Danger Room course record (old 1.10)
	struct OldDrCourse
	{
		u8 flags;
		u8 tier0;
		u8 tier1;
		u8 tier2;
		u32 largest_hit;
		u16 largest_hit_hero;
		u16 least_damage;
		u16 least_damage_hero;
		u16 best_time;
		u8 best_combo;
		u8 most_health_packs;
		u16 most_health_packs_hero;
		u8 most_energy_packs;
		u16 most_energy_packs_hero;
		u8 most_defeats;
		u16 most_defeats_hero;
	};
	struct OldReview
	{
		u32 unlocked[16];                              // 5 x 3 dwords used + 1 pad dword each
		u32 seen[16];
	};
	// zone string table (old 1.12)
	struct OldZoneStrings
	{
		u32 generation;
		u32 offsets[old_zone_string_slots];
		u32 used;
		u8 arena[old_zone_arena];
	};
	// entity state record (old 1.13)
	struct OldStateRecord
	{
		u32 def_name;
		u32 inst_name;
		u8 payload[8];
		u8 flags_a;
		u8 flags_b;
		i16 position[3];
	};
	// profile (old 1.15)
	struct OldHighScore
	{
		u8 course;
		u8 pad1[3];
		u16 party_a[6];
		u32 count_a;
		u16 party_b[6];
		u32 count_b;
		u32 score;
	};
	struct OldProfileHead
	{
		u8 options[3];
		char player_name[0x21];
	};
	struct OldProfileTail
	{
		u32 pad_1b4;
		u32 score_count;
		u32 char_unlock[10];
		u32 review[15];
		u32 pad_220;
		u8 highest_level;
		u8 flags_229;
		u8 pad_22a[2];
	};
	struct OldScriptVar
	{
		char name[12];
		u8 value_be[4];                                // value: 4 big-endian bytes
	};

	// ---------------------------------------------------------------------------------------------- new container
	struct NewHeader
	{
		u8 magic[8];
		u8 kind;
		u8 header_size_8;
		u16 major;
		u16 minor;
		u16 reserved_0e;
		u32 required_features;
		u32 optional_features;
		u32 file_length;
		u32 section_count;
		u32 section_table_offset;
		u32 writer_version;
		u32 content_version;
		u32 catalogue_revision;
		u64 campaign_serial;
		u32 body_crc;
		u32 save_sequence;
		u8 reserved_40[0x40];
		u32 legacy_length_guard;
		u32 header_crc;
	};
	struct NewSectionEntry
	{
		u8 tag[4];
		u8 major;
		u8 minor;
		u16 flags;
		u32 offset;
		u32 length;
		u32 records;
		u32 crc;
	};
#pragma pack(pop)

	// zone record (old 1.13): u32 name handle, the state records, then this tail. The arrays are sized by the
	// native layout the record was read with / is written for (NativeLayout below; retail: 256 / 300 / 768); a
	// record of one layout is never written as another. Read and written field by field (oldsave.cpp).
	struct OldZoneRecordTail
	{
		u32 pad_4804 = 0;
		u32 state_count = 0;
		std::vector<u16> ordinal_map;
		u32 pad_4a0c = 0;
		std::vector<u16> dynamic;
		u32 pad_4c68 = 0;
		u32 dynamic_count = 0;
		std::vector<u16> smart;
		u32 pad_5270 = 0;
		u32 smart_count = 0;
		u8 fog[old_fog_bytes]{};
		u32 conversation[5]{};
		f32 time_stamp = 0;
		u8 flag = 0;
		u8 pad_55dd[3]{};
	};

	constexpr u8 new_magic[8] = {'L', 'C', 'S', 'A', 'V', 'E', '1', 0};
	constexpr u32 new_header_size = 0x88;
	constexpr u32 new_section_entry_size = 24;
	constexpr u16 section_required = 1;
	constexpr u16 section_keep = 2;
	constexpr u16 section_ext_droppable = 8;
	constexpr u8 kind_campaign = 1, kind_profile = 2, kind_transfer = 3;
	constexpr u32 optional_keys_present = 1;

	static_assert(sizeof(OldItemHead) == 1 + 320 + 12 + 24);
	static_assert(sizeof(OldInstanceHead) == 5 && sizeof(OldEnhElement) == 6);
	static_assert(sizeof(OldAttrScalars) == 0x18 && sizeof(OldTalentEntry) == 8 && sizeof(OldTalentHeapTail) == 8);
	static_assert(sizeof(OldEquipTail) == 0x1c && sizeof(OldPowers) == 85 && sizeof(OldCostume) == 5);
	static_assert(sizeof(OldGameBlock) == 0xdc && sizeof(OldSnapshot) == 0x26c && sizeof(OldSideTail) == 8);
	static_assert(sizeof(OldDrCourse) == old_dr_course_bytes && sizeof(OldReview) == 128);
	static_assert(sizeof(OldZoneStrings) == 0x5808 && sizeof(OldStateRecord) == 0x18);
	static_assert(sizeof(OldProfileHead) + old_high_scores * sizeof(OldHighScore) + sizeof(OldProfileTail) == old_profile_payload);
	static_assert(sizeof(OldScriptVar) == 16);
	static_assert(sizeof(NewHeader) == new_header_size && sizeof(NewSectionEntry) == new_section_entry_size);

	// ---------------------------------------------------------------------------------------------- native layouts
	// The native in-memory / stream layout of the engine's save manager (PHASE-A-PLAN section 6; round6/layouts.json
	// "native_save", mirrored by tools/lcsave/layout.py NativeLayout): the zone record (state records, ordinal map,
	// dynamic and smart lists), the five-record manager, the stream payload the serializer fills and the wrapper the
	// game allocates, and the <= 200-byte chunking of the campaign transfer (packets 47/48). Every count and offset
	// the zone-record, zone-strings, manager, stream and transfer code uses comes from one of these descriptors,
	// chosen EXPLICITLY by the caller: a layout is never inferred from a file's or a packet's size. retail_layout is
	// the retail executable's (the old_* constants above, the old-save importer's only layout); e10a2_layout is the
	// entity-capacity build's (4096 states including the sentinel, 1024 ordinals / dynamic / smart, record 0x19b88,
	// manager 0x80c24, payload 0x200000, usable end 0x200000, wrapper 0x200078, no origin trailer). The zone string
	// table (0x800 slots, 0x3800 arena) and the fog (0x34c) are the same in both (layouts.json changes neither).
	constexpr u32 feature_entity_layout_contiguous10 = 1;      // header required_features bit 0: typed ids are ten-bit tokens
	constexpr u32 known_required_features = feature_entity_layout_contiguous10;   // every required bit this code implements
	constexpr u32 typed_generation_max = entity_capacity_layout::generation_max;   // 0x3fff: the largest minted generation
	constexpr u32 transfer_chunk = 200;                        // the largest chunk of a 47/48 campaign transfer
	constexpr u32 native_manager_records = 5;
	constexpr u32 native_mission_bytes = sizeof(OldSnapshot);  // 0x26c
	constexpr u32 native_wrapper_slack = 0x78;                 // the wrapper the game allocates: payload + description/cursor words

	struct NativeLayout
	{
		const char* id;             // "RETAIL" / "E10A2"
		u32 feature_bits;           // the required_features marker this layout writes / accepts (0 or bit 0)
		u32 index_bits;             // 8 retail, 10 contiguous
		u32 states, ordinals, dynamic, smart;
		u32 record_count;
		// zone record offsets (from the record: u32 name handle at 0, state records at 4)
		u32 state_count, ordinals_offset, dynamic_offset, dynamic_count, smart_offset, smart_count, fog, conversation, time_stamp, flag, record_size;
		// the manager (records at records_offset, then a pad word, the record count, the mission snapshot, flag, time)
		u32 records_offset, manager_count, mission, manager_flag, manager_time, manager_size;
		// the stream
		u32 payload, usable_end, origin_trailer_bytes, wrapper, envelope, slot_size;
		u32 zone_string_slots, zone_arena, fog_bytes;
		u32 transfer_chunks;

		constexpr bool contiguous() const { return (feature_bits & feature_entity_layout_contiguous10) != 0; }
		// (offset, length) of transfer chunk `index` of the payload; the last chunk is short; {0, 0} outside
		constexpr std::pair<u32, u32> chunk_bounds(u32 index) const
		{
			if (index >= transfer_chunks) return {0, 0};
			const u32 off = index * transfer_chunk;
			return {off, payload - off < transfer_chunk ? payload - off : transfer_chunk};
		}
		constexpr u32 record_offset(u32 index) const { return index < record_count ? records_offset + index * record_size : 0; }
	};

	constexpr NativeLayout make_native_layout(const char* id, u32 feature_bits, u32 index_bits, u32 states, u32 ordinals, u32 dynamic, u32 smart, u32 payload)
	{
		NativeLayout n{};
		n.id = id; n.feature_bits = feature_bits; n.index_bits = index_bits;
		n.states = states; n.ordinals = ordinals; n.dynamic = dynamic; n.smart = smart; n.record_count = native_manager_records;
		u32 p = 4 + states * static_cast<u32>(sizeof(OldStateRecord)) + 4;   // pad word after the states
		n.state_count = p; p += 4;
		n.ordinals_offset = p; p += 2 * ordinals + 4;
		n.dynamic_offset = p; p += 2 * dynamic + 4;
		n.dynamic_count = p; p += 4;
		n.smart_offset = p; p += 2 * smart + 4;
		n.smart_count = p; p += 4;
		n.fog = p; p += old_fog_bytes;
		n.conversation = p; p += 5 * 4;
		n.time_stamp = p; p += 4;
		n.flag = p; p += 4;                                                   // flag byte + 3 pad bytes
		n.record_size = p;
		n.records_offset = 0;
		u32 m = native_manager_records * p + 4;
		n.manager_count = m; m += 4;
		n.mission = m; m += native_mission_bytes;
		n.manager_flag = m; m += 4;
		n.manager_time = m; m += 4;
		n.manager_size = m;
		n.payload = payload; n.usable_end = payload; n.origin_trailer_bytes = 0; n.wrapper = payload + native_wrapper_slack;
		n.envelope = old_envelope; n.slot_size = old_envelope + payload;
		n.zone_string_slots = old_zone_string_slots; n.zone_arena = old_zone_arena; n.fog_bytes = old_fog_bytes;
		n.transfer_chunks = (payload + transfer_chunk - 1) / transfer_chunk;
		return n;
	}
	constexpr NativeLayout retail_layout = make_native_layout("RETAIL", 0, 8, old_zone_states, old_zone_ordinals, old_zone_dynamic, old_zone_smart, old_payload);
	constexpr NativeLayout e10a2_layout = make_native_layout("E10A2", feature_entity_layout_contiguous10, 10, 4096, 1024, 1024, 1024, 0x200000);
	// the descriptor by id ("RETAIL", "E10A2"); nullptr otherwise
	constexpr const NativeLayout* native_layout(std::string_view id)
	{
		return id == "RETAIL" ? &retail_layout : id == "E10A2" ? &e10a2_layout : nullptr;
	}

	// the descriptors against round6/layouts.json "native_save" (E10A2) and its "retail_import" block (RETAIL)
	static_assert(retail_layout.state_count == 18440 && retail_layout.ordinals_offset == 18444 && retail_layout.dynamic_offset == 18960 &&
	              retail_layout.dynamic_count == 19564 && retail_layout.smart_offset == 19568 && retail_layout.smart_count == 21108 &&
	              retail_layout.fog == 21112 && retail_layout.conversation == 21956 && retail_layout.time_stamp == 21976 &&
	              retail_layout.flag == 21980 && retail_layout.record_size == 21984);
	static_assert(retail_layout.manager_count == 109924 && retail_layout.mission == 109928 && retail_layout.manager_flag == 110548 &&
	              retail_layout.manager_time == 110552 && retail_layout.manager_size == 110556);
	static_assert(retail_layout.record_size == old_zone_record && retail_layout.payload == old_payload && retail_layout.slot_size == old_slot_size &&
	              retail_layout.wrapper == 0x2fc78 && retail_layout.usable_end == 0x2fc00);
	static_assert(e10a2_layout.state_count == 98312 && e10a2_layout.ordinals_offset == 98316 && e10a2_layout.dynamic_offset == 100368 &&
	              e10a2_layout.dynamic_count == 102420 && e10a2_layout.smart_offset == 102424 && e10a2_layout.smart_count == 104476 &&
	              e10a2_layout.fog == 104480 && e10a2_layout.conversation == 105324 && e10a2_layout.time_stamp == 105344 &&
	              e10a2_layout.flag == 105348 && e10a2_layout.record_size == 105352);
	static_assert(e10a2_layout.manager_count == 526764 && e10a2_layout.mission == 526768 && e10a2_layout.manager_flag == 527388 &&
	              e10a2_layout.manager_time == 527392 && e10a2_layout.manager_size == 527396);
	static_assert(e10a2_layout.record_size == 0x19b88 && e10a2_layout.manager_size == 0x80c24 && e10a2_layout.payload == 0x200000 &&
	              e10a2_layout.usable_end == 0x200000 && e10a2_layout.wrapper == 0x200078 && e10a2_layout.origin_trailer_bytes == 0 &&
	              e10a2_layout.slot_size == 0x84 + 0x200000);
	static_assert(retail_layout.transfer_chunks == 978 && retail_layout.chunk_bounds(977).first == 977 * 200 && retail_layout.chunk_bounds(977).second == 184);
	static_assert(e10a2_layout.transfer_chunks == 10486 && e10a2_layout.chunk_bounds(10485).second == 152 && e10a2_layout.chunk_bounds(10486).second == 0);
	static_assert(e10a2_layout.index_bits == entity_capacity_layout::raised_index_bits && retail_layout.index_bits == entity_capacity_layout::stock_index_bits);

	// ---- typed entity-id fields (PHASE-A-PLAN section 4). The migration IS entity_capacity_layout::migrate_typed_field
	// (the one contract header); tools/lcsave/layout.py mirrors it. A retail token is (generation << 8) | index, a
	// contiguous token (generation << 10) | index: null (0) when the value is not a positive retail token, its
	// generation is 0, or its generation exceeds the operational maximum 0x3fff.
	constexpr u32 migrate_typed_field(u32 retail_value) { return entity_capacity_layout::migrate_typed_field(retail_value); }
	// a feature-1 typed field (already ten-bit): the value when its generation is 1..0x3fff, else 0. Never shifts.
	constexpr u32 validate_typed_field(u32 contiguous_value)
	{
		if (!entity_capacity_layout::raised_tokens.positive_retail_token(contiguous_value)) return 0;
		const u32 g = entity_capacity_layout::raised_tokens.generation({contiguous_value});
		if (g == 0 || g > typed_generation_max) return 0;
		return contiguous_value;
	}
	static_assert(migrate_typed_field(0x101) == 0x401 && migrate_typed_field(0x401) == 0x1001 && migrate_typed_field(0x400001) == 0 &&
	              migrate_typed_field(0) == 0 && migrate_typed_field(0x80000000u) == 0 && migrate_typed_field(0x3fff01) == 0xfffc01 &&
	              migrate_typed_field(0x45) == 0 && migrate_typed_field(0xffffff) == 0);
	static_assert(validate_typed_field(0x501) == 0x501 && validate_typed_field(0x1000001) == 0 && validate_typed_field(0xffffff) == 0xffffff &&
	              validate_typed_field(0) == 0 && validate_typed_field(0x80000001u) == 0);
}
