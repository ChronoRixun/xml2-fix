// Transient native-transfer provenance. No LC schema, native size or entity state is added.
#include "internal.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <set>

namespace lcsave::detail
{
	std::string fresh_tail_text(const std::string& zone, const ZonePrefix& prefix, const ZoneNumbering& numbering)
	{
		std::string text = "LCFT1|" + hex(key_hash1(py_lower(zone, Enc::cp1252), Enc::cp1252)) + "|" +
			hex(prefix.old) + "|" + hex(numbering.sha1) + "|";
		for (const auto placement : prefix.placements)
		{
			if (text.back() != '|') text += ',';
			text += std::to_string(placement);
		}
		return text;
	}

	std::map<std::string, ZoneCaptureMetadata> native_fresh_tail(const OldSlot& native, const Build& build)
	{
		std::map<std::string, ZoneCaptureMetadata> out;
		for (const auto& record : native.zones)
		{
			const auto& sentinel = record.states.at(0);
			if (std::memcmp(sentinel.payload, "LCFRESH1", 8)) continue;
			auto bad = [] { fail("invalid native fresh-tail provenance"); };
			if (!record.tail.state_count || sentinel.inst_name || sentinel.flags_a || sentinel.flags_b ||
			    sentinel.position[0] || sentinel.position[1] || sentinel.position[2]) bad();
			const auto zone = py_lower(zone_string(*native.zone_strings, record.name_handle), Enc::cp1252);
			const auto text = zone_string(*native.zone_strings, sentinel.def_name);
			std::vector<std::string> fields;
			for (std::size_t start = 0;;)
			{
				const auto end = text.find('|', start);
				fields.push_back(text.substr(start, end == std::string::npos ? end : end-start));
				if (end == std::string::npos) break;
				start = end+1;
			}
			if (fields.size() != 5 || fields[0] != "LCFT1" || fields[1] != hex(key_hash1(zone, Enc::cp1252))) bad();
			const auto* numbering = build.zone_numbering(zone);
			if (!numbering || !numbering->problems.empty() || fields[3] != hex(numbering->sha1)) bad();
			Sha1 source{};
			if (!from_hex(fields[2], source.data(), source.size()) || fields[2] != hex(source) ||
			    source == Sha1{} || source == numbering->sha1) bad();
			std::vector<u32> placements;
			std::set<u32> unique;
			for (std::size_t start = 0;;)
			{
				const auto end = fields[4].find(',', start);
				const auto part = fields[4].substr(start, end == std::string::npos ? end : end-start);
				u32 placement = 0;
				const auto parsed = std::from_chars(part.data(), part.data()+part.size(), placement);
				if (parsed.ec != std::errc() || parsed.ptr != part.data()+part.size() ||
				    part != std::to_string(placement) || !unique.insert(placement).second) bad();
				placements.push_back(placement);
				if (placements.size() >= numbering->names.size() || placements.size() > record.tail.ordinal_map.size()) bad();
				if (end == std::string::npos) break;
				start = end+1;
			}
			if (std::any_of(record.tail.ordinal_map.begin()+placements.size(), record.tail.ordinal_map.end(),
			    [](const auto index) { return index != 0; }) || out.count(zone)) bad();
			ZoneCaptureMetadata metadata;
			metadata.zone = zone; metadata.time_stamp = record.tail.time_stamp; metadata.capture_flags = 2;
			metadata.placement_sha1 = numbering->sha1; metadata.source_placement_sha1 = source;
			for (std::size_t k = 0; k < placements.size(); ++k)
			{
				const auto index = record.tail.ordinal_map[k];
				if (!index) continue;
				const auto& state = record.states.at(index);
				PlacedEntityMetadata entity;
				entity.ordinal = static_cast<u32>(k+1); entity.placement_index = numbering->placement.at(k);
				entity.source_placement_index = placements[k];
				entity.definition_name = zone_string(*native.zone_strings, state.def_name);
				entity.instance_name = zone_string(*native.zone_strings, state.inst_name);
				if (std::make_pair(key_hash1(entity.definition_name, Enc::cp1252), key_hash1(entity.instance_name, Enc::cp1252)) !=
				    numbering->names.at(k)) bad();
				metadata.entities.push_back(std::move(entity));
			}
			for (std::size_t k = placements.size(); k < numbering->names.size(); ++k)
			{
				PlacedEntityMetadata entity;
				entity.ordinal = static_cast<u32>(k+1); entity.placement_index = numbering->placement.at(k);
				entity.definition_name = numbering->name_text.at(k).first;
				entity.instance_name = numbering->name_text.at(k).second;
				metadata.fresh_entities.push_back(std::move(entity));
			}
			out.emplace(zone, std::move(metadata));
		}
		return out;
	}
}
