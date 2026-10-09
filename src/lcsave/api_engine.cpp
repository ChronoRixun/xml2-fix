// The engine side's additions to the public interface (lcsave.hpp, last block): the zone placement hash the
// tables hold, and the profile file's three directions. On the library's internals, in api.cpp's manner.

#include "internal.hpp"

#include <cstring>
#include <algorithm>
#include <filesystem>
#include <cmath>

namespace lcsave
{
	namespace
	{
		template <typename F> bool guarded(std::string& error, F&& f)
		{
			try
			{
				f();
				return true;
			}
			catch (const detail::Error& e)
			{
				error = e.message;
			}
			catch (const std::exception& e)
			{
				error = std::string("internal error: ") + e.what();
			}
			return false;
		}
	}

	bool zone_file_sha1(const Tables& tables, const std::string& zone, Sha1Digest& out)
	{
		if (!tables.loaded() || !tables.data->build)
		{
			return false;
		}
		const detail::ZoneNumbering* z = nullptr;
		std::string error;
		if (!guarded(error, [&] { z = tables.data->build->zone_numbering(zone); }) || !z)
		{
			return false;
		}
		std::memcpy(out.data(), z->sha1.data(), out.size());
		return true;
	}

	Result convert_old_profile_to_new(const std::vector<std::uint8_t>& old_file, const Tables& tables, const ConvertOptions& options)
	{
		Result r;
		if (!tables.loaded())
		{
			r.error = "tables not loaded";
			r.refused = 1;
			return r;
		}
		detail::ConvertState state;
		r.ok = guarded(r.error, [&] { r.bytes = detail::convert_profile(old_file, *tables.data, options, state); });
		if (!r.ok)
		{
			r.refused = 1;
			r.bytes.clear();
		}
		r.ledger = r.ok ? "converted (profile)" : "refused: " + r.error;
		return r;
	}

	Result old_profile_to_new(const std::vector<std::uint8_t>& old_stream, const Tables& tables, const NewFileContext& context)
	{
		Result r;
		if (!tables.loaded())
		{
			r.error = "tables not loaded";
			return r;
		}
		detail::ConvertState state;
		r.ok = guarded(r.error, [&] { r.bytes = detail::profile_to_new(old_stream, *tables.data, context, state); });
		if (!r.ok)
		{
			r.refused = 1;
			r.bytes.clear();
		}
		return r;
	}

	Result new_to_old_profile(const std::vector<std::uint8_t>& new_file, const Tables& tables)
	{
		Result r;
		if (!tables.loaded())
		{
			r.error = "tables not loaded";
			return r;
		}
		r.ok = guarded(r.error, [&] { r.bytes = detail::reverse_profile(new_file, *tables.data); });
		if (!r.ok)
		{
			r.refused = 1;
		}
		return r;
	}
	bool conversation_digest(const Tables& tables, std::array<std::uint8_t, 32>& out, std::string& error)
	{
		error.clear();
		return guarded(error, [&]
		{
			if (!tables.loaded() || !tables.data->build) detail::fail("conversation digest needs build tables");
			const auto root = std::filesystem::path(tables.data->build->data_root) / L"Conversations";
			std::vector<std::pair<std::string, std::filesystem::path>> paths;
			if (std::filesystem::exists(root)) for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
				if (entry.is_regular_file()) paths.emplace_back(py_lower(entry.path().lexically_relative(root).generic_string(), Enc::latin1), entry.path());
			std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
			std::vector<std::uint8_t> input;
			std::string previous;
			for (const auto& [name, path] : paths)
			{
				if (name == previous) detail::fail("duplicate conversation path after case normalization");
				previous = name;
				std::vector<std::uint8_t> bytes;
				if (!detail::read_file_bytes(path.wstring(), bytes)) detail::fail("conversation file cannot be read");
				const auto hash = sha1(bytes.data(), bytes.size());
				input.insert(input.end(), name.begin(), name.end()); input.push_back(0);
				input.insert(input.end(), hash.begin(), hash.end());
			}
			out = sha256(input.data(), input.size());
		});
	}

	bool loaded_zone_metadata(const std::vector<std::uint8_t>& file, const Tables& tables,
		std::vector<ZoneCaptureMetadata>& out, std::string& error)
	{
		out.clear(); error.clear();
		const bool ok = guarded(error, [&]
		{
			if (!tables.loaded() || !tables.data->build) detail::fail("loaded metadata needs build tables");
			std::vector<std::string> notes;
			const auto native = detail::reverse_slot(file, *tables.data, nullptr, notes);
			const auto f = detail::read_file(file.data(), file.size(), kind_campaign);
			for (const auto& record : f.data("ZONE")["records"].as_list())
			{
				const auto found = tables.data->p_zone.find(static_cast<std::uint32_t>(record["zone"].as_int()));
				if (found == tables.data->p_zone.end()) detail::fail("unknown zone metadata");
				bool applied = false;
				for (const auto& z : native.zones) if (detail::zone_string(*native.zone_strings, z.name_handle) == found->second) applied = true;
				if (!applied) continue;
				ZoneCaptureMetadata metadata;
				metadata.zone = found->second;
				metadata.time_stamp = static_cast<float>(record["time_stamp"].as_real());
				metadata.capture_flags = static_cast<std::uint32_t>(record["capture_flags"].as_int());
				std::memcpy(metadata.placement_sha1.data(), record["content_hash"].as_bytes().data(), 20);
				std::memcpy(metadata.conversation_digest.data(), record["conversation_digest"].as_bytes().data(), 32);
				if (f.data("META")["origin"].as_int() == 1 || std::all_of(metadata.conversation_digest.begin(), metadata.conversation_digest.end(), [](u8 b) { return b == 0; }))
					metadata.capture_flags |= 2; // earlier candidates wrote zero despite unobserved provenance
				const auto* numbering = tables.data->build->zone_numbering(metadata.zone);
				const auto restored = std::find_if(native.zones.begin(), native.zones.end(), [&](const auto& z)
				{ return detail::zone_string(*native.zone_strings, z.name_handle) == metadata.zone; });
				// Metadata must describe the reconstructed map, including promoted legacy placements.
				for (std::size_t k = 0; k < old_zone_ordinals; ++k)
				{
					const auto idx = restored->tail.ordinal_map[k];
					if (!idx) continue;
					PlacedEntityMetadata entity;
					entity.ordinal = static_cast<std::uint32_t>(k + 1);
					entity.placement_index = numbering->placement.at(k);
					entity.definition_name = detail::zone_string(*native.zone_strings, restored->states[idx].def_name);
					entity.instance_name = detail::zone_string(*native.zone_strings, restored->states[idx].inst_name);
					metadata.entities.push_back(std::move(entity));
				}
				out.push_back(std::move(metadata));
			}
		});
		if (!ok) out.clear();
		return ok;
	}

	bool native_zone_metadata(const std::vector<std::uint8_t>& stream, const Tables& tables,
		std::vector<ZoneCaptureMetadata>& out, std::string& error)
	{
		out.clear(); error.clear();
		// Reuse the strict same-build native translator to validate numbering/names and scalar ranges.
		// This temporary container is never written or installed; it supplies inferred associations only.
		NewFileContext context;
		const auto translated=old_stream_to_new(stream,tables,context);
		if(!translated.ok){error=translated.error;return false;}
		// Do not reverse this temporary file: reverse computes conversation digests, which can
		// stall an online join on a cold filesystem. The translator already verified these names.
		const bool ok=guarded(error,[&]
		{
			const auto file=detail::read_file(translated.bytes.data(),translated.bytes.size(),kind_campaign);
			detail::validate_native_sections(file);
			for(const auto& record:file.data("ZONE")["records"].as_list())
			{
				const auto zone=tables.data->p_zone.find(static_cast<std::uint32_t>(record["zone"].as_int()));
				if(zone==tables.data->p_zone.end())detail::fail("unknown native zone metadata");
				ZoneCaptureMetadata metadata;metadata.zone=zone->second;
				metadata.time_stamp=static_cast<float>(record["time_stamp"].as_real());metadata.capture_flags=2;
				std::memcpy(metadata.placement_sha1.data(),record["content_hash"].as_bytes().data(),20);
				const auto& names=record["names"].as_list();
				for(const auto& state:record["states"].as_list())if(state["list"].as_int()==1)
				{
					PlacedEntityMetadata entity;
					entity.ordinal=static_cast<std::uint32_t>(state["ordinal"].as_int());
					entity.placement_index=static_cast<std::uint32_t>(state["placement_index"].as_int());
					entity.definition_name=names.at(static_cast<std::size_t>(state["def_name"].as_int())).as_text();
					entity.instance_name=names.at(static_cast<std::size_t>(state["inst_name"].as_int())).as_text();
					metadata.entities.push_back(std::move(entity));
				}
				out.push_back(std::move(metadata));
			}
		});
		if(!ok)out.clear();return ok;
	}

	bool native_load_demand(const std::vector<std::uint8_t>& stream, NativeLoadDemand& out, std::string& error)
	{
		out = {}; error.clear();
		return guarded(error, [&]
		{
			std::vector<std::uint8_t> image = stream;
			if (stream.size() == old_payload)
			{
				image.assign(old_envelope, 0);
				const std::uint32_t size = old_payload;
				std::memcpy(image.data() + 0x80, &size, 4);
				image.insert(image.end(), stream.begin(), stream.end());
			}
			const auto old = detail::read_slot(image.data(), image.size());
			auto count = [&](const detail::OldInstance& instance)
			{
				++out.instances;
				for (const auto& list : instance.lists) out.attached_enhancements += static_cast<std::uint32_t>(list.size());
			};
			for (const auto& instance : old.carried) count(instance);
			for (const auto& world : old.world) count(world.instance);
			for (const auto& hero : old.heroes) for (const auto& instance : hero.equipment.equipped) count(instance);
			// Every restore block is serialized and read even when its valid flag is clear. Include it conservatively.
			for (const auto& restore : old.restore) for (const auto& instance : restore.equipment.equipped) count(instance);
			out.world_items = static_cast<std::uint32_t>(old.world.size());
		});
	}

	bool validate_native_stream(const std::vector<std::uint8_t>& stream, const Tables& tables, std::string& error)
	{
		NativeLoadDemand demand;
		if (!tables.loaded()) { error = "tables not loaded"; return false; }
		if (!native_load_demand(stream, demand, error)) return false;
		if (demand.instances > 256 || demand.attached_enhancements > 375 || demand.world_items > 150)
		{ error = "native instance/enhancement/world pool exhausted"; return false; }
		NewFileContext context;
		const auto result = old_stream_to_new(stream, tables, context);
		error = result.error;
		return result.ok;
	}

}
