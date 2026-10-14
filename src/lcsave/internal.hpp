#pragma once

// lcsave internals shared by the translation units: the old save model (oldsave.py), the record codec
// (container.py), the tables a conversion needs (catalogue.py, legacy.py, binds.py) and the converter's pieces.
// Errors inside the library are thrown as detail::Error and turned into result structs at the public API.

#include "keys.hpp"
#include "layout.hpp"
#include "lcsave.hpp"
#include "value.hpp"
#include "conversation_cache.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace lcsave::detail
{
	// ---------------------------------------------------------------------------------------------- old save model
	// reader ceilings for the unbounded old counts (oldsave.py)
	constexpr u32 ceil_instances = 4096;
	constexpr u32 ceil_enh_per_list = 64;
	constexpr u32 ceil_equipped = 3;
	constexpr u32 ceil_hero_blocks = 64;
	constexpr u32 ceil_zone_records = 5;
	constexpr u32 ceil_world_slot = 150;

	struct OldInstance
	{
		OldInstanceHead head{};
		std::vector<OldEnhElement> lists[3];      // prefixes, own, suffixes
	};
	struct OldAttributes
	{
		OldAttrScalars scalars{};
		OldTalentEntry talents[old_talent_entries]{};
		OldTalentHeapTail tail{};
	};
	struct OldEquipment
	{
		std::vector<OldInstance> equipped;
		OldEquipTail tail{};
	};
	struct OldHero
	{
		OldAttributes attributes;
		OldEquipment equipment;
		OldPowers powers{};
		OldCostume costume{};
	};
	struct OldRestore
	{
		OldAttributes attributes;
		OldEquipment equipment;
	};
	struct OldWorld
	{
		u32 slot = 0;
		OldInstance instance;
	};
	struct OldZone
	{
		u32 name_handle = 0;
		std::vector<OldStateRecord> states;       // native.states (retail 768)
		OldZoneRecordTail tail{};                 // arrays of native.ordinals / dynamic / smart

		OldZone() : OldZone(retail_layout) {}
		explicit OldZone(const NativeLayout& native) { resize(native); }
		void resize(const NativeLayout& native)
		{
			states.assign(native.states, OldStateRecord{});
			tail.ordinal_map.assign(native.ordinals, 0);
			tail.dynamic.assign(native.dynamic, 0);
			tail.smart.assign(native.smart, 0);
		}
		// the record has the array sizes of `native` (a record read with one layout is never written as another)
		bool matches(const NativeLayout& native) const
		{
			return states.size() == native.states && tail.ordinal_map.size() == native.ordinals &&
				tail.dynamic.size() == native.dynamic && tail.smart.size() == native.smart;
		}
	};
	struct OldProfile
	{
		OldProfileHead head{};
		OldHighScore high_scores[old_high_scores]{};
		OldProfileTail tail{};
	};
	struct OldSlot
	{
		u8 description_raw[0x80]{};
		std::string begin_tag;                    // the tag text (rebuilt by the writer)
		std::string end_tag;
		OldItemHead items{};
		std::vector<OldInstance> carried;
		std::vector<OldWorld> world;
		std::vector<OldHero> heroes;
		i16 unlocked[old_unlocked_slots]{};
		u32 unlocked_count = 0;
		u8 restore_flag = 0;
		std::vector<OldRestore> restore;          // 22
		OldGameBlock game{};
		OldSnapshot side_frames[old_side_frames]{};
		OldSideTail side{};
		std::vector<OldDrCourse> courses;
		std::vector<u8> course_residue;
		u8 grades[old_dr_grades]{};
		OldReview review{};
		std::unique_ptr<OldZoneStrings> zone_strings = std::make_unique<OldZoneStrings>();
		std::vector<OldZone> zones;
		OldSnapshot mission{};
		OldProfile profile;
		OldScriptVar zone_vars[old_script_zone_vars]{};
		OldScriptVar game_vars[old_script_game_vars]{};
		u32 town_centres = 0;
		std::vector<u8> slack;                    // empty = None (all zero)

		OldSlot() = default;
		OldSlot(const OldSlot& o);
		OldSlot& operator=(const OldSlot& o);
	};

	// text of a fixed NUL-padded field (cp1252 bytes up to the first NUL)
	std::string cstr(const char* field, std::size_t size);
	template <std::size_t N> std::string cstr(const char (&field)[N]) { return cstr(field, N); }
	void set_cstr(char* field, std::size_t size, const std::string& text, Enc enc = Enc::cp1252);   // NUL-padded; fails when it does not fit
	template <std::size_t N> void set_cstr(char (&field)[N], const std::string& text, Enc enc = Enc::cp1252) { set_cstr(field, N, text, enc); }

	// the slot image of the native layout `native` (explicit: a file of another layout's size is refused, never
	// re-interpreted); every zone record of the model must match the layout it is written for
	OldSlot read_slot(const std::uint8_t* data, std::size_t size, const NativeLayout& native = retail_layout);
	std::vector<std::uint8_t> write_slot(const OldSlot& m, const NativeLayout& native = retail_layout);
	// settings.dat (0x84 + 0x228): the description and the profile block (oldsave.read_profile_file / write_profile_file)
	OldProfile read_profile_file(const std::uint8_t* data, std::size_t size, std::string& description);
	std::vector<std::uint8_t> write_profile_file(const std::string& description, const OldProfile& p);
	std::string zone_string(const OldZoneStrings& table, u32 handle);
	u32 script_var_value(const OldScriptVar& v);

	// ---------------------------------------------------------------------------------------------- container
	struct Field
	{
		const char* name;
		const char* type;
	};
	struct Schema
	{
		const Field* fields;
		std::size_t count;
	};
	const Schema* record_schema(std::string_view name);
	const Schema* packed_schema(std::string_view name);
	struct SectionSchema
	{
		const char* tag;
		Schema head;
		Schema lists;           // (list name, element type)
	};
	const SectionSchema* section_schema(std::string_view tag);

	struct SectionOut
	{
		std::string tag;
		u8 major = 1;
		u8 minor = 0;
		u16 flags = 0;
		Value data; // bytes = retained opaque payload
		u32 retained_records = 0;
	};
	struct HeaderIn
	{
		u32 writer_version = 0;
		u32 content_version = 0;
		u32 catalogue_revision = 0;
		u64 campaign_serial = 0;
		u32 save_sequence = 0;
		u32 optional_features = 0;
		u32 required_features = 0;    // the writer's native layout decides (feature_bits); a bit this code does not implement is refused
	};
	std::string encode_value(std::string_view type, const Value& v);
	std::string encode_record(std::string_view name, const Value& v);
	std::string encode_section(const std::string& tag, const Value& data);
	Value decode_section(const std::string& tag, const std::string& payload);
	u8 section_minor(std::string_view tag);     // the newest minor of a section this code knows (SPEC 1.4)
	std::vector<std::uint8_t> write_file(u8 kind, const std::vector<SectionOut>& sections, const HeaderIn& header);

	struct SectionIn
	{
		NewSectionEntry entry{};
		bool known = false;
		Value data;             // none for an unknown section
		std::string payload;
	};
	struct FileIn
	{
		NewHeader header{};
		std::vector<std::string> order;
		std::map<std::string, SectionIn> sections;
		const Value& data(const char* tag) const;
	};
	FileIn read_file(const std::uint8_t* data, std::size_t size, int expect_kind = -1);

	// ---------------------------------------------------------------------------------------------- tables
	struct HashKey
	{
		std::size_t operator()(const Hash& h) const
		{
			std::size_t v = 0;
			for (int k = 0; k < 8; ++k) v = v << 8 | h[k];
			return v;
		}
	};

	// one era's positional tables (legacy.py Era, as legacy.lcm carries them: catalogue ids by position; 0 = None)
	struct EraTables
	{
		int cv = 0;
		std::string release;
		std::vector<u32> STAT, ITEM, ENHC, NAMA, TRIT, TALN_shared, MSNF, OBJV, DRCS, DRGR, XPNT, ZONE;
		std::vector<u32> ITEM_cat;                              // NAMB category of each definition (0 none, 1 belt, 2 gloves, 3 armor)
		std::map<u32, std::vector<u32>> NAMB;                   // category -> ids by position
		std::map<u32, std::vector<u32>> TALN_hero;              // stats index -> talent ids by talent-file position
		std::map<u32, std::map<u32, u32>> COST;                 // stats index -> slot -> id
		std::map<std::pair<int, int>, u32> REVW, TRIV;          // (category, position) / (act, position) -> id
		std::vector<std::vector<u32>> ITEM_own, TRIT_own;       // own enhancement ids per definition / special item
		u32 prefix[2]{}, suffix[2]{}, trit[2]{}, own[2]{};
		bool trivia_compact = false;
		std::map<std::pair<int, int>, int> trivia_bits;       // current native mapping only; legacy TRIV remains unchanged
		u32 hero_count = 0;
		bool has_digest = false;
		Hash conversation_digest{};
		struct ZoneDelta
		{
			std::vector<u32> map;
			std::vector<u32> ambiguous;
		};
		std::map<u32, ZoneDelta> zones;                         // zone id -> era ordinal map
		std::map<u32, std::vector<u32>> holdable;               // STAT id -> shared talent positions the herostat lists

		const std::vector<u32>* taln_hero(u32 stats_index) const;
		const std::map<u32, u32>* cost(u32 stats_index) const;
		const ZoneDelta* zone_map(u32 zone) const;
	};

	struct CatEntry
	{
		unsigned domain = 0;
		u32 id = 0;
		u32 attributes = 0;
		bool retired = false;
		u32 successor = 0;
		std::string key;                                        // canonical key bytes ("" = unknown to the writing build)
		std::vector<std::string> aliases;
	};
	struct Catalogue
	{
		u32 revision = 0;
		u32 content_version = 0;
		std::vector<CatEntry> entries;
		std::unordered_map<Hash, std::size_t, HashKey> by_hash[17];
		std::unordered_map<u32, std::size_t> by_id;
		void index();
		u32 lookup(unsigned domain, const Hash& h) const;      // 0 = None
		const CatEntry* entry_of(u32 id) const;
	};

	struct ZonePrefix
	{
		Hash zone{}, digest{};
		Sha1 old{}, current{};
		std::vector<u32> placements;
		std::vector<std::pair<Hash,Hash>> fresh_aliases, ambiguous_aliases;
	};
	struct ZoneNumbering
	{
		Sha1 sha1{};
		std::vector<std::pair<Hash, Hash>> names;              // (definition, instance) key hashes in ordinal order
		std::vector<std::pair<std::string, std::string>> name_text;
		std::vector<u32> placement;                            // placement index in ordinal order
		std::vector<std::string> problems;
		std::map<std::size_t, Hash> prefix_digests;             // verified from the opened placement bytes
	};

	// one parsed .lcbind, kept as read (tests compare it with the builder's Python reader)
	struct BindRow
	{
		u32 group = 0;
		u32 position = 0;
		u32 id = 0;
		std::string key;
	};
	struct BindList
	{
		std::string tag;
		u32 rule = 0;
		std::vector<BindRow> rows;
	};
	struct BindFileRead
	{
		std::string path;                                      // relative to the bind root, '/' separated
		std::string sha1;                                      // 20 bytes
		std::vector<BindList> lists;
	};

	// the player's build as its bind files describe it (binds.Binds through the catalogue)
	struct Build
	{
		mutable std::vector<BindFileRead> bind_files;          // as read (load_bind records each file once)
		std::wstring bind_root;
		std::wstring data_root;
		std::shared_ptr<ConversationCache> conversations = std::make_shared<ConversationCache>();
		EraTables T;                                           // the build's own numbering (the saver's direction)
		std::vector<std::string> stat;                         // canonical key by stats index ("" = None)
		std::vector<std::string> item;                         // canonical key by definition index
		std::vector<std::string> course;
		u32 hero_count = 0;
		std::vector<std::vector<u16>> herostat_talents;        // by stats index: talent ids the herostat lists
		std::vector<u8> herostat_xpexempt;                     // by stats index: 1 = the entry is xpexempt (no level processing)
		std::vector<std::string> herostat_autospend;           // by stats index: the autospend class name, lower case ("" = none)
		// zone numbering, read lazily from the placement files
		mutable std::map<std::string, std::optional<ZoneNumbering>> zones;
		std::vector<ZonePrefix> zone_prefixes;
		const ZoneNumbering* zone_numbering(const std::string& zone) const;
		const ZonePrefix* zone_prefix(const std::string& zone, const std::string& old, const ZoneNumbering& current) const;
	};

	std::string fresh_tail_text(const std::string& zone, const ZonePrefix& prefix, const ZoneNumbering& numbering);
	std::map<std::string, ZoneCaptureMetadata> native_fresh_tail(const OldSlot& native, const Build& build);

	struct TablesData
	{
		std::map<std::pair<Hash, Hash>, std::vector<Hash>> conversation_tables;
		Catalogue catalogue;
		EraTables reference;
		std::vector<EraTables> eras;                           // newest first
		u32 legacy_map_id = 0;
		u32 legacy_catalogue_revision = 0;
		u32 check_heroes = 0, check_courses = 0, check_grades = 0, check_objectives = 0;
		std::unique_ptr<Build> build;
		const EraTables* by_cv(int cv) const;
		// id -> position for the reverse direction (convert.PositionTables), from the build
		std::unordered_map<u32, u32> p_stat, p_item, p_enh, p_nama, p_namb, p_trit, p_tal, p_cost, p_obj, p_course, p_grade, p_xp;
		std::unordered_map<u32, std::pair<int, int>> p_review, p_triv;
		std::unordered_map<u32, std::string> p_zone;
		// the key text of an id as the dumps print it (the canonical key with its escapes undone), or nullptr
		const std::string* name_of(u32 id) const;
		std::unordered_map<u32, std::string> names;
	};

	std::vector<Hash> conversation_bits(const Build& build, const std::string& zone);
	ConversationPackage pinned_conversation_package(const Build& build, const std::string& zone);
	void pin_conversation_packages(Build& build, const ConversationSources& sources);
	Hash conversation_package_digest(const Hash& zone, const std::vector<Hash>& bits);
	void restore_conversation(const Value& record, const std::string& zone, const TablesData& tables,
		u32 (&words)[5], std::vector<std::string>& notes);
	std::string unescape_key(const std::string& canonical);
	bool read_catalogue(const std::vector<std::uint8_t>& file, Catalogue& out, std::string& error);
	bool read_legacy(const std::vector<std::uint8_t>& file, TablesData& out, std::string& error);
	bool read_binds(const std::wstring& bind_root, const std::wstring& data_root, TablesData& out, std::string& error);
	bool read_file_bytes(const std::wstring& path, std::vector<std::uint8_t>& out);
	std::wstring data_file(const std::wstring& folder, const std::wstring& name);

	// ---------------------------------------------------------------------------------------------- converter
	struct Dropped
	{
		std::int64_t zone_records = 0, world_items = 0, zone_vars = 0, other = 0, talents = 0, states = 0;
	};
	struct ConvertState
	{
		std::vector<std::string> notes;            // cp1252 bytes
		Dropped dropped;
		int start_era = 0;
		int items_era = 0;
		std::vector<int> zone_eras;
		u32 parked = 0;
	};
	const EraTables* detect_talent_era(const OldSlot& old, const std::vector<const EraTables*>& eras, const EraTables& ref,
		std::vector<std::pair<std::int64_t, std::int64_t>>& unexplained);
	std::vector<std::uint8_t> convert_slot(const std::vector<std::uint8_t>& old_bytes, const TablesData& t,
		const ConvertOptions& options, ConvertState& state);
	void validate_native_sections(const FileIn& file, const NativeLayout& native = retail_layout); // semantic preflight, no conversation I/O
	void validate_native_tables(const TablesData& tables); // installed engine budgets, not reader ceilings
	// LC campaign -> the old model laid out for `native`. The file's required-feature marker decides
	// (decide_features): a feature-1 file is refused on a retail layout; a feature-0 file on a contiguous layout has
	// its registered typed fields migrated (registry required); a feature-1 file on a contiguous layout has them validated.
	OldSlot reverse_slot(const std::vector<std::uint8_t>& new_bytes, const TablesData& t, const OldSlot* tmpl,
		std::vector<std::string>& notes, const NativeLayout& native = retail_layout, const TypedRegistry* registry = nullptr,
		MigrationReport* report = nullptr);
	std::vector<std::uint8_t> stream_to_new(const std::vector<std::uint8_t>& old_stream, const TablesData& t,
		const NewFileContext& context, ConvertState& state, const NativeLayout& native = retail_layout);
	// the typed entity-id adapter over a file's SVAR lists (typed_registry.cpp; tools/lcsave/typed_registry.py
	// migrate_script_vars): the only translation of saved integers, at the LC -> native boundary
	void migrate_script_vars(Value& zone_vars, Value& game_vars, u32 required_features, const NativeLayout& native,
		const TypedRegistry* registry, MigrationReport& report);
	std::string migration_note(const MigrationReport& report);   // "" when nothing was migrated or validated
	extern const char* const no_registry;                        // the refusal of a legacy file without a registry
	// the profile file (kind 2): the converter's and the saver's direction, and the reverse (engine side, save_format.cpp)
	std::vector<std::uint8_t> convert_profile(const std::vector<std::uint8_t>& old_bytes, const TablesData& t,
		const ConvertOptions& options, ConvertState& state);
	std::vector<std::uint8_t> profile_to_new(const std::vector<std::uint8_t>& old_stream, const TablesData& t,
		const NewFileContext& context, ConvertState& state);
	std::vector<std::uint8_t> reverse_profile(const std::vector<std::uint8_t>& new_bytes, const TablesData& t);
	Value dump_old(const OldSlot& old, const TablesData& t, int start_cv, int items_cv, bool native = false);
	Value dump_new(const std::vector<std::uint8_t>& file, const TablesData& t);
}
