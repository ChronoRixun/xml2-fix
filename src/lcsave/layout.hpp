#pragma once

// Byte layouts (tools/lcsave/layout.py): every OLD_* table of XMen2.exe's save (SPEC-old-save section 1) and
// the fixed parts of the LC container (NEW_*), as packed structs with the reference's field order and widths.
// Everything little-endian; the sizes are the ones layout.py asserts. A `char[n]` field is the reference's
// 'cstr' (NUL-padded text): the struct keeps the raw bytes, so bytes after the NUL survive a read and write
// as the reference's '<name>_raw' does.

#include <cstdint>

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
	// zone record (old 1.13): u32 name handle, 768 state records, then this tail
	struct OldZoneRecordTail
	{
		u32 pad_4804;
		u32 state_count;
		u16 ordinal_map[old_zone_ordinals];
		u32 pad_4a0c;
		u16 dynamic[old_zone_dynamic];
		u32 pad_4c68;
		u32 dynamic_count;
		u16 smart[old_zone_smart];
		u32 pad_5270;
		u32 smart_count;
		u8 fog[old_fog_bytes];
		u32 conversation[5];
		f32 time_stamp;
		u8 flag;
		u8 pad_55dd[3];
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
	static_assert(4 + old_zone_states * sizeof(OldStateRecord) + sizeof(OldZoneRecordTail) == old_zone_record);
	static_assert(sizeof(OldProfileHead) + old_high_scores * sizeof(OldHighScore) + sizeof(OldProfileTail) == old_profile_payload);
	static_assert(sizeof(OldScriptVar) == 16);
	static_assert(sizeof(NewHeader) == new_header_size && sizeof(NewSectionEntry) == new_section_entry_size);
}
