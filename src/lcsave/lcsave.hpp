#pragma once

// lcsave: the Legends Classic save format ("LC save", format 1) and the one-time converter from XMen2.exe's own
// save format - the C++ translation of the reference implementation tools/lcsave of legends-classic
// (CONVERTER-PORT.md; SPEC-new-save revision 2.2, SPEC-old-save revision 2). Byte for byte: the same old file
// and tables give the same converted file and the same canonical dump as the Python.
//
// Nothing here throws: every call returns a result with an error string. Table loading and conversation
// identity validation read build files; the conversion and reverse APIs never write player files.
//
// Tables: catalogue.lcc (ids), legacy.lcm (what every builder release numbered how) and the bind files of the
// player's build (names and the running build's numbering). load_tables verifies them (SPEC 3.5 static part:
// file CRCs, the SHA-1 of every data file a bind file describes, every bind's id against the catalogue); the
// runtime proofs (registration traces, live counts) are the engine side's.

#include "layout.hpp"

#include <array>
#include <functional>
#include <utility>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace lcsave
{
	namespace detail
	{
		struct TablesData;
	}

	// ---- the native layout and the Phase A file marker (PHASE-A-PLAN sections 4 and 6) ----------------------------
	// layout.hpp: `NativeLayout`, `retail_layout` (the retail executable's save manager) and `e10a2_layout` (the
	// entity-capacity build's). The engine selects its layout once (the installed entity descriptor) and passes it
	// to every stream call below; the overloads without a layout are the retail ones (byte-identical to before).
	// A file's header `required_features` bit 0 (feature_entity_layout_contiguous10) marks typed entity ids as
	// ten-bit tokens: the E10A2 writers emit it, the offline converters and the retail engine keep 0.

	struct FeatureDecision
	{
		bool ok = false;                    // false: the file cannot be applied to this layout (reason says why)
		std::string reason;
		bool migrate_typed_fields = false;  // a feature-0 (legacy) file applied to a contiguous layout: migrate at the boundary
		std::uint32_t required_features = 0;
	};
	// pure: what `native` does with a header's required_features (unknown bit -> refused; feature 1 on retail ->
	// refused "needs the entity-capacity build"; feature 0 on E10A2 -> migrate; else apply as is)
	FeatureDecision decide_features(std::uint32_t required_features, const NativeLayout& native);
	// the same over an LC file (validated by the container reader first; a damaged file is refused with its reason)
	FeatureDecision decide_features(const std::vector<std::uint8_t>& new_file, const NativeLayout& native);
	extern const char* const downgrade_refused;   // the retail reader's reason for a feature-1 file
	extern const char* const w2_refused;          // a contiguous pre-W2 layout's reason for a bit-1 (NATIVE_W2) file
	// the name and the minimum build of a required-feature bit this code knows (nullptr for another bit): the save menu
	// and the log name the build a file needs (limits-wave2.md section 7, "Named refusal")
	const char* required_feature_name(std::uint32_t bit);
	const char* required_feature_needs(std::uint32_t bit);

	// The typed entity-id registry (tools/lcsave/typed_registry.py; the builder's tools/xml1build/typed_entity_ids.py
	// writes it): the (scope, key) script variables that hold World tokens, bound to the scripts tree they were
	// derived from by a fingerprint. The engine verifies the registry against its own scripts tree before it enables
	// legacy migration; a registry that does not verify means legacy (feature-0) loads are refused, not guessed.
	struct TypedRegistry
	{
		std::uint32_t version = 0;
		std::string fingerprint;            // 64 hex digits
		std::uint32_t script_files = 0;
		std::vector<std::pair<std::string, std::string>> keys;   // (scope "Zone"/"Game", key), sorted
		bool typed(const std::string& scope, const std::string& key) const;
	};
	bool load_typed_registry(const std::vector<std::uint8_t>& file, TypedRegistry& out, std::string& error);
	// the effective fingerprint of a scripts tree: SHA-256 over "relative-posix-path\0sha256\n" of every *.py file,
	// in the tree's path order (lower-cased path components); refuses non-ASCII paths and unreadable files
	bool scripts_fingerprint(const std::wstring& scripts_root, std::string& fingerprint, std::uint32_t& files, std::string& error);
	// ok when the registry reads and its fingerprint is the scripts tree's (same files, same bytes); reason otherwise
	bool verify_registry(const TypedRegistry& registry, const std::wstring& scripts_root, std::string& reason);
	bool verify_registry(const std::vector<std::uint8_t>& registry_file, const std::wstring& scripts_root, std::string& reason);

	struct MigrationReport
	{
		enum class Mode : std::uint8_t { none, migrate, validate };
		Mode mode = Mode::none;
		std::uint32_t typed_fields = 0;        // registered (scope, key) values seen in the file
		std::uint32_t translated = 0;          // non-null values translated to ten-bit tokens (migrate)
		std::uint32_t normalized_to_null = 0;  // non-null values that became null (either mode)
		std::uint32_t validated = 0;           // values checked against the ten-bit domain (validate)
		std::string line() const;              // "mode=... typed_fields=... translated=... normalized_to_null=... validated=..."
	};

	struct Tables
	{
		std::shared_ptr<const detail::TablesData> data;
		bool loaded() const { return data != nullptr; }
		// counts the engine side compares with its live tables (SPEC 3.5); 0 when not loaded
		std::uint32_t catalogue_revision() const;
		std::uint32_t content_version() const;       // the era model's reference content version
		// the build's herostat entry at stats index `stats_index` is xpexempt (XMen2.exe's level processing 0x4bbae0
		// returns before giving anything: CStats+0x2ad bit 0x20); false for an index the herostat doesn't have
		bool xp_exempt(std::uint32_t stats_index) const;
		// the stats entry's canonical catalogue key (for log lines), "" when the build has none at that index
		std::string stats_key(std::uint32_t stats_index) const;
		// the herostat entry's autospend class name, lower case ("" = none / no such index)
		std::string hero_autospend(std::uint32_t stats_index) const;
		std::uint32_t hero_count() const;            // herostat entries of the build
		std::uint32_t stats_count() const;           // stats table size including the reserved index 0
		std::uint32_t item_count() const;
		std::uint32_t course_count() const;
		std::uint32_t objective_count() const;
	};

	// build_dir: the build (game) folder. Reads <build>\Data\lcsave\catalogue.lcc, <build>\Data\lcsave\legacy.lcm,
	// the .lcbind files beside the data files and (lazily, per zone) <build>\Maps\<zone>.lcent.
	bool load_tables(const std::wstring& build_dir, Tables& tables, std::string& error);
	// the same with the bind files in another folder tree than the data files they describe (tests, tools)
	bool load_tables(const std::wstring& data_dir, const std::wstring& bind_dir, Tables& tables, std::string& error);
	// Validate every build talent position before play, including talents absent
	// from this save. Positions and saved u16 ids follow the selected descriptor.
	bool validate_talent_positions(const Tables&, const NativeLayout&, std::string& error);

	// Data\autospend as XMen2.exe loads it (parser 0x43d0c0): the first four CharacterClass entries (0x43d18d), the
	// first four <bonus> rules of each (0x43d373): start and rate unsigned bytes, the four deltas signed bytes in the
	// record's order strength, speed, body, mind (= the class-bonus bytes CStats+0x10..0x13); a missing number is 0.
	constexpr std::size_t autospend_class_limit = 4;
	constexpr std::size_t autospend_rule_limit = 4;
	struct AutospendRule
	{
		std::uint8_t start = 0;
		std::uint8_t rate = 0;
		std::int8_t delta[4]{};
	};
	struct AutospendClass
	{
		std::string name;              // lower case
		std::vector<AutospendRule> rules;
	};
	bool read_autospend(const std::wstring& build_dir, std::vector<AutospendClass>& out, std::string& error);

	struct Result
	{
		bool ok = false;
		std::string error;
		std::vector<std::uint8_t> bytes;
		std::string ledger;          // one line: result, start era, items era, the five counters (+ notes count)
		std::uint32_t parked = 0;
		std::uint32_t refused = 0;   // 1 when the file was refused (error says why)
	};

	struct ConvertOptions
	{
		std::string original_name = "saveslot.save";   // LGCY original_name (the old file's name)
		std::uint8_t slot = 0;                         // META slot
		bool fixed_saved_at = false;                   // META saved_at: the clock, or this FILETIME value
		std::uint64_t saved_at = 0;
		bool fixed_campaign_serial = false;            // header: the first 8 bytes of the original's SHA-1, or this
		std::uint64_t campaign_serial = 0;
		// the writer the file names: the reference converter's by default, so that the output is byte-identical to the
		// Python's (SPEC 1.1: kind 1); XML2 Fix's own import may write kind 2 and its own text instead
		std::uint32_t writer_version = 0x00010001;      // 0.1.0, kind 1
		std::string writer = "lcsave 0.1.0";
	};

	// old slot file (0x84 + 0x2fc00 bytes) -> LC campaign file (convert.convert_slot)
	Result convert_old_to_new(const std::vector<std::uint8_t>& old_file, const Tables& tables);
	Result convert_old_to_new(const std::vector<std::uint8_t>& old_file, const Tables& tables, const ConvertOptions& options);

	// Runtime rule-52 verification predicate. Default is absent (migration disabled); the economy
	// installer binds its live verified state after agreement registration. The callback must outlive loads.
	// This is converter policy only: catalogue attributes alone never enable the migration.
	void set_xml1_economy_verifier(bool (*verified)());
	bool item_own_rebuilt_enabled();

	// LC campaign file -> the internal stream the engine's loader reads (convert.reverse_slot through the running
	// build's tables): the whole old slot image, 0x84 + 0x2fc00 bytes (description + length + payload); the payload
	// alone is bytes [0x84, end). Dropped fields are written as zero. Zone records whose placement file changed are
	// not applied (the Result's ledger lists them).
	Result new_to_old_stream(const std::vector<std::uint8_t>& new_file, const Tables& tables);
	// the same for an explicit native layout: the stream is `native.slot_size` bytes. `registry` is required for a
	// feature-0 file on a contiguous layout (refused without it); `report` receives the migration / validation counts
	// (also one ledger line). The marker is never stripped: a feature-1 file on the retail layout is refused.
	Result new_to_old_stream(const std::vector<std::uint8_t>& new_file, const Tables& tables, const NativeLayout& native,
		const TypedRegistry* registry = nullptr, MigrationReport* report = nullptr);

	// What a save written by the game carries besides the stream (SPEC 1.1 header, 2.2 META)
	// Ordinal is the native map slot used by this capture (or reconstructed by the loader).
	struct PlacedEntityMetadata
	{
		std::uint32_t ordinal = 0, placement_index = 0;
		std::string definition_name, instance_name;
		std::uint32_t source_placement_index = 0; // only used with source_placement_sha1
	};
	struct ZoneCaptureMetadata
	{
		std::string zone;
		float time_stamp = 0;
		std::array<std::uint8_t, 20> placement_sha1{};
		std::array<std::uint8_t, 32> conversation_digest{};
		std::uint32_t capture_flags = 2; // bit 0 incomplete; bit 1 unobserved legacy provenance
		std::vector<PlacedEntityMetadata> entities;
		// Ephemeral compatibility metadata, never new durable fields. Cached unvisited records retain
		// their original hash/placement indices until an actual current-map capture replaces them.
		std::array<std::uint8_t, 20> source_placement_sha1{};
		std::vector<PlacedEntityMetadata> fresh_entities;
	};
	bool loaded_zone_metadata(const std::vector<std::uint8_t>& file, const Tables& tables,
		std::vector<ZoneCaptureMetadata>& out, std::string& error);
	bool loaded_zone_metadata(const std::vector<std::uint8_t>& file, const Tables& tables,
		std::vector<ZoneCaptureMetadata>& out, std::string& error, const NativeLayout& native, const TypedRegistry* registry = nullptr);
	// Same-build native transfers lack an LC sidecar; infer validated current-table associations
	// and recover host-authenticated append provenance from the transient native sentinel.
	bool native_zone_metadata(const std::vector<std::uint8_t>& stream, const Tables& tables,
		std::vector<ZoneCaptureMetadata>& out, std::string& error);
	bool native_zone_metadata(const std::vector<std::uint8_t>& stream, const Tables& tables,
		std::vector<ZoneCaptureMetadata>& out, std::string& error, const NativeLayout& native);
	bool conversation_digest(const Tables& tables, std::array<std::uint8_t, 32>& out, std::string& error);
	// Native heard provenance includes the zone package ranking; the overload above is legacy-only.
	bool conversation_digest(const Tables& tables, const std::string& zone, std::array<std::uint8_t, 32>& out, std::string& error);

	// Engine-only: pin a verified digest before play, refusing later tree changes without scanning.
	bool prepare_conversation_digest(const Tables& tables, std::string& error);
	struct ConversationSources
	{
		std::wstring lookup_root; // executable root used by the actual mod lookup
		std::vector<std::pair<std::wstring, std::wstring>> mod_files;
		std::function<std::wstring(const std::wstring&)> resolve;
		std::function<bool()> unchanged; // verifies the loader index generation without I/O
	};
	// Native capture/restore preparation: effective zone packages, not the historical tree hash.
	bool prepare_conversation_packages(const Tables& tables, const ConversationSources& sources, std::string& error);
	bool prepared_conversation_digest(const Tables& tables, const std::string& zone,
		std::array<std::uint8_t, 32>& out, std::string& error);
	struct NativeLoadDemand
	{
		std::uint32_t instances = 0, attached_enhancements = 0, world_items = 0;
	};
	// Conservative demand: all serialized hero and restore equipment, carried and world instances.
	bool native_load_demand(const std::vector<std::uint8_t>& stream, NativeLoadDemand& out, std::string& error);
	bool native_load_demand(const std::vector<std::uint8_t>& stream, NativeLoadDemand& out, std::string& error, const NativeLayout& native);
	bool validate_native_stream(const std::vector<std::uint8_t>& stream, const Tables& tables, std::string& error);
	bool validate_native_stream(const std::vector<std::uint8_t>& stream, const Tables& tables, std::string& error, const NativeLayout& native);

	struct NewFileContext
	{
		std::string description;                       // META description (cp1252; the card's description text)
		std::uint64_t saved_at = 0;                    // FILETIME (UTC) of the write
		std::uint8_t slot = 0;
		std::uint8_t origin = 0;                      // 0 native, 2 own online campaign
		std::uint64_t campaign_serial = 0;             // random at a campaign's first save, then copied
		std::uint32_t save_sequence = 1;               // 1 for the first save, + 1 on every later one
		std::uint32_t writer_version = 0x01040000;     // major, minor, patch, kind (0 = XML2 Fix)
		std::string writer = "XML2 Fix";               // META writer
		std::vector<std::string> mods;                 // META mods, in load order
		bool has_conversation_digest = false;          // legacy hint; fresh native records derive per-zone package provenance
		std::uint8_t conversation_digest[32]{};
		std::vector<ZoneCaptureMetadata> zone_captures;
		bool require_capture_metadata = false;
		std::vector<std::uint8_t> retained_file;
		// header optional-feature bits the caller vouches for (SPEC-new-save 1.1, "ignored when unknown"); bit 0 (KEYS)
		// is the writer's own. XML2 Fix sets bit 1 / bit 2 while [Game] LevelSkillPoints / LevelStatPoints=xml1 is
		// installed (save_format_rules.hpp, the fifth-level catch-up).
		std::uint32_t optional_features = 0;
	};

	// the engine's stream (the whole old slot image, or the 0x2fc00 payload alone) -> LC campaign file, numbered
	// by the running build (the saver's direction; origin 0, no LGCY section)
	Result old_stream_to_new(const std::vector<std::uint8_t>& old_stream, const Tables& tables, const NewFileContext& context);
	// the same for an explicit native layout (the whole slot image or the payload alone, of `native` - never
	// inferred from the size); the header's required-feature marker is the layout's (E10A2: bit 0)
	Result old_stream_to_new(const std::vector<std::uint8_t>& old_stream, const Tables& tables, const NewFileContext& context,
		const NativeLayout& native);

	// canonical JSON (dump.py: indent 1, sorted keys) of either format, detected by the magic. An old file is
	// read with the eras the converter chooses (0.1.10's when it refuses). "" when the file can not be read.
	std::string dump(const std::vector<std::uint8_t>& file, const Tables& tables);
	std::string dump(const std::vector<std::uint8_t>& file, const Tables& tables, std::string& error);

	// The representability probe (limits-wave2.md section 7): "" when a build of the older layout `target` (E10A2, the
	// pre-W2 release layout) can apply the LC campaign `file` exactly, else the first failing rule by name. The W2 writer
	// sets required-feature bit 1 exactly when this is not "". Runs the target's reverse translation (its pools, widths,
	// positions, zone-record count and talent multiplier; definition indices stay per build) and the engine constraints
	// that translation does not check (objective index <= 255, enhancement record < 512, zone records the target would
	// not apply). No engine, no file is written.
	std::string representable(const std::vector<std::uint8_t>& file, const Tables& tables, const NativeLayout& target);

	// "add missing herostat talents on load": every talent the running build's herostat lists for a hero that the
	// stream's talent heap of that hero lacks (hero blocks only), in herostat order. A herostat name is resolved in the
	// hero's own talent file first, then in shared_talents. On the real saves this includes the powers of heroes
	// whose heap the engine never filled (not yet joined); `shared` tells the shared talents (grab) apart.
	struct TalentAdd
	{
		std::uint32_t hero_block = 0;      // hero block index in the stream (stats index - 1)
		std::uint32_t stats_index = 0;
		std::uint16_t talent_id = 0;       // the engine's talent id to insert
		std::uint8_t rank = 0;             // rank bits (0)
		std::uint8_t base = 0;             // base rank (0)
		bool shared = false;               // a shared talent (id below 100); otherwise one of the hero's own (a power)
	};
	bool missing_talents(const std::vector<std::uint8_t>& old_stream, const Tables& tables, std::vector<TalentAdd>& out);
	bool missing_talents(const std::vector<std::uint8_t>& old_stream, const Tables& tables, std::vector<TalentAdd>& out,
		std::string& error);
	bool missing_talents(const std::vector<std::uint8_t>& old_stream, const Tables& tables, std::vector<TalentAdd>& out,
		std::string& error, const NativeLayout& native);

	// ---- the engine side's additions (save_format.cpp; implemented in api_engine.cpp on the library's internals) ----

	using Sha1Digest = std::array<std::uint8_t, 20>;

	// The SHA-1 the tables hold for a zone's placement file (its .lcent, or the file itself when the tables read
	// it); false when the build has no such zone. The fix logs it beside the hash of the bytes the engine opened
	// (SPEC 3.8, N14 c).
	// Current native trivia mapping; rule-1 builds retain the retail sparse layout.
	int trivia_bit(const Tables& tables, int act, int question);
	int trivia_question_count(const Tables& tables, int act);
	bool zone_file_sha1(const Tables& tables, const std::string& zone, Sha1Digest& out);

	// The profile file (SPEC 1.5 profile.lcsave, kind 2: META, KEYS, PRFO, PRFP, PRFS): the same three directions
	// as a campaign, over the 0x228-byte profile block. "Old image" = 0x80 description + u32 0x228 + the block,
	// exactly a stock settings.dat.
	Result convert_old_profile_to_new(const std::vector<std::uint8_t>& old_file, const Tables& tables, const ConvertOptions& options);
	Result old_profile_to_new(const std::vector<std::uint8_t>& old_stream, const Tables& tables, const NewFileContext& context);
	Result new_to_old_profile(const std::vector<std::uint8_t>& new_file, const Tables& tables);
	// the same with the engine's layout: the profile holds no layout-sized field, but the build's table budgets (item
	// definitions 160 / 1024) are the installed layout's
	Result old_profile_to_new(const std::vector<std::uint8_t>& old_stream, const Tables& tables, const NewFileContext& context, const NativeLayout& native);
	Result new_to_old_profile(const std::vector<std::uint8_t>& new_file, const Tables& tables, const NativeLayout& native);
}
