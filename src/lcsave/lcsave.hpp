#pragma once

// lcsave: the Legends Classic save format ("LC save", format 1) and the one-time converter from XMen2.exe's own
// save format - the C++ translation of the reference implementation tools/lcsave of legends-classic
// (CONVERTER-PORT.md; SPEC-new-save revision 2.2, SPEC-old-save revision 2). Byte for byte: the same old file
// and tables give the same converted file and the same canonical dump as the Python.
//
// Nothing here throws: every call returns a result with an error string. Nothing touches the disk except
// load_tables (which reads the build's catalogue, era model and bind files; docs/lcsave-formats.md).
//
// Tables: catalogue.lcc (ids), legacy.lcm (what every builder release numbered how) and the bind files of the
// player's build (names and the running build's numbering). load_tables verifies them (SPEC 3.5 static part:
// file CRCs, the SHA-1 of every data file a bind file describes, every bind's id against the catalogue); the
// runtime proofs (registration traces, live counts) are the engine side's.

#include <array>
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

	struct Tables
	{
		std::shared_ptr<const detail::TablesData> data;
		bool loaded() const { return data != nullptr; }
		// counts the engine side compares with its live tables (SPEC 3.5); 0 when not loaded
		std::uint32_t catalogue_revision() const;
		std::uint32_t content_version() const;       // the era model's reference content version
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

	// LC campaign file -> the internal stream the engine's loader reads (convert.reverse_slot through the running
	// build's tables): the whole old slot image, 0x84 + 0x2fc00 bytes (description + length + payload); the payload
	// alone is bytes [0x84, end). Dropped fields are written as zero. Zone records whose placement file changed are
	// not applied (the Result's ledger lists them).
	Result new_to_old_stream(const std::vector<std::uint8_t>& new_file, const Tables& tables);

	// What a save written by the game carries besides the stream (SPEC 1.1 header, 2.2 META)
	// Ordinal is the native map slot used by this capture (or reconstructed by the loader).
	struct PlacedEntityMetadata
	{
		std::uint32_t ordinal = 0, placement_index = 0;
		std::string definition_name, instance_name;
	};
	struct ZoneCaptureMetadata
	{
		std::string zone;
		float time_stamp = 0;
		std::array<std::uint8_t, 20> placement_sha1{};
		std::array<std::uint8_t, 32> conversation_digest{};
		std::uint32_t capture_flags = 2; // bit 0 incomplete; bit 1 unobserved legacy provenance
		std::vector<PlacedEntityMetadata> entities;
	};
	bool loaded_zone_metadata(const std::vector<std::uint8_t>& file, const Tables& tables,
		std::vector<ZoneCaptureMetadata>& out, std::string& error);
	// Same-build native transfers lack an LC sidecar; infer only validated current-table associations.
	bool native_zone_metadata(const std::vector<std::uint8_t>& stream, const Tables& tables,
		std::vector<ZoneCaptureMetadata>& out, std::string& error);
	bool conversation_digest(const Tables& tables, std::array<std::uint8_t, 32>& out, std::string& error);
	struct NativeLoadDemand
	{
		std::uint32_t instances = 0, attached_enhancements = 0, world_items = 0;
	};
	// Conservative demand: all serialized hero and restore equipment, carried and world instances.
	bool native_load_demand(const std::vector<std::uint8_t>& stream, NativeLoadDemand& out, std::string& error);
	bool validate_native_stream(const std::vector<std::uint8_t>& stream, const Tables& tables, std::string& error);

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
		bool has_conversation_digest = false;          // the running build's conversation digest (SPEC 2.12)
		std::uint8_t conversation_digest[32]{};
		std::vector<ZoneCaptureMetadata> zone_captures;
		bool require_capture_metadata = false;
		std::vector<std::uint8_t> retained_file;
	};

	// the engine's stream (the whole old slot image, or the 0x2fc00 payload alone) -> LC campaign file, numbered
	// by the running build (the saver's direction; origin 0, no LGCY section)
	Result old_stream_to_new(const std::vector<std::uint8_t>& old_stream, const Tables& tables, const NewFileContext& context);

	// canonical JSON (dump.py: indent 1, sorted keys) of either format, detected by the magic. An old file is
	// read with the eras the converter chooses (0.1.10's when it refuses). "" when the file can not be read.
	std::string dump(const std::vector<std::uint8_t>& file, const Tables& tables);
	std::string dump(const std::vector<std::uint8_t>& file, const Tables& tables, std::string& error);

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

	// ---- the engine side's additions (save_format.cpp; implemented in api_engine.cpp on the library's internals) ----

	using Sha1Digest = std::array<std::uint8_t, 20>;

	// The SHA-1 the tables hold for a zone's placement file (its .lcent, or the file itself when the tables read
	// it); false when the build has no such zone. The fix logs it beside the hash of the bytes the engine opened
	// (SPEC 3.8, N14 c).
	bool zone_file_sha1(const Tables& tables, const std::string& zone, Sha1Digest& out);

	// The profile file (SPEC 1.5 profile.lcsave, kind 2: META, KEYS, PRFO, PRFP, PRFS): the same three directions
	// as a campaign, over the 0x228-byte profile block. "Old image" = 0x80 description + u32 0x228 + the block,
	// exactly a stock settings.dat.
	Result convert_old_profile_to_new(const std::vector<std::uint8_t>& old_file, const Tables& tables, const ConvertOptions& options);
	Result old_profile_to_new(const std::vector<std::uint8_t>& old_stream, const Tables& tables, const NewFileContext& context);
	Result new_to_old_profile(const std::vector<std::uint8_t>& new_file, const Tables& tables);
}
