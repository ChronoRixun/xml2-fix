// The engine side's additions to the public interface (lcsave.hpp, last block): the zone placement hash the
// tables hold, and the profile file's three directions. On the library's internals, in api.cpp's manner.

#include "internal.hpp"

#include <Windows.h>
#include <cstring>
#include <algorithm>
#include <filesystem>
#include <cmath>

namespace lcsave::detail
{
	ConversationCache::~ConversationCache()
	{
		if (change_notification) FindCloseChangeNotification(change_notification);
	}
}

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

	int trivia_question_count(const Tables& tables, const int act)
	{
		if (!tables.loaded() || !tables.data->build || act < 1) return 0;
		const auto& questions = tables.data->build->T.TRIV;
		int count = 0;
		for (auto it = questions.lower_bound({act, 0}); it != questions.end() && it->first.first == act; ++it)
			count = std::max(count, it->first.second + 1);
		return count;
	}

	int trivia_bit(const Tables& tables, const int act, const int question)
	{
		if (!tables.loaded() || !tables.data->build) return -1;
		const auto& bits = tables.data->build->T.trivia_bits;
		const auto found = bits.find({act, question});
		return found == bits.end() ? -1 : found->second;
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
	namespace
	{
		std::vector<detail::ConversationFile> conversation_files(const std::filesystem::path& root)
		{
			std::vector<detail::ConversationFile> files;
			std::map<std::uint32_t, bool> versioned_volumes;
			if (!std::filesystem::exists(root)) return files;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
			{
				if (!entry.is_regular_file()) continue;
				detail::ConversationFile file;
				file.name = py_lower(entry.path().lexically_relative(root).generic_string(), Enc::latin1);
				file.path = entry.path().wstring();
				// Attribute-only access avoids reopening every unchanged file for data on
				// the game/network thread. ChangeTime also detects restored LastWriteTime.
				const HANDLE h = CreateFileW(file.path.c_str(), FILE_READ_ATTRIBUTES,
					FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
					FILE_ATTRIBUTE_NORMAL, nullptr);
				if (h == INVALID_HANDLE_VALUE) detail::fail("conversation file attributes cannot be read");
				FILE_BASIC_INFO basic{};
				BY_HANDLE_FILE_INFORMATION info{};
				const bool has_basic = GetFileInformationByHandleEx(h, FileBasicInfo, &basic, sizeof basic) != FALSE;
				const bool ok = GetFileInformationByHandle(h, &info) != FALSE;
				if (ok && !versioned_volumes.contains(info.dwVolumeSerialNumber))
				{
					wchar_t filesystem[32]{};
					const bool known = GetVolumeInformationByHandleW(h, nullptr, 0, nullptr, nullptr, nullptr,
						filesystem, static_cast<DWORD>(std::size(filesystem))) != FALSE;
					versioned_volumes[info.dwVolumeSerialNumber] = known &&
						(_wcsicmp(filesystem, L"NTFS") == 0 || _wcsicmp(filesystem, L"ReFS") == 0);
				}
				CloseHandle(h);
				if (!ok) detail::fail("conversation file version cannot be read");
				file.size = (static_cast<std::uint64_t>(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
				file.file_id = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
				file.volume = info.dwVolumeSerialNumber;
				file.write_time = static_cast<std::uint64_t>(basic.LastWriteTime.QuadPart);
				file.change_time = static_cast<std::uint64_t>(basic.ChangeTime.QuadPart);
				file.versioned = has_basic && basic.ChangeTime.QuadPart > 0 && file.file_id != 0 &&
					versioned_volumes[info.dwVolumeSerialNumber];
				files.push_back(std::move(file));
			}
			std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) { return a.name < b.name; });
			for (std::size_t i = 1; i < files.size(); ++i)
				if (files[i-1].name == files[i].name) detail::fail("duplicate conversation path after case normalization");
			return files;
		}
	}

	bool conversation_digest(const Tables& tables, std::array<std::uint8_t, 32>& out, std::string& error)
	{
		error.clear();
		return guarded(error, [&]
		{
			if (!tables.loaded() || !tables.data->build) detail::fail("conversation digest needs build tables");
			const auto root = std::filesystem::path(tables.data->build->data_root) / L"Conversations";
			auto& cache = *tables.data->build->conversations;
			std::lock_guard lock(cache.mutex);
			if (cache.engine_required) detail::fail("historical tree digests cannot label live package provenance");
			// Engine tables are pinned before play. Never enumerate or refresh on a frame.
			if (cache.change_notification)
			{
				if (!cache.valid || WaitForSingleObject(cache.change_notification, 0) != WAIT_TIMEOUT)
				{
					cache.valid = false;
					detail::fail("conversation files changed during play; restart to prepare this build");
				}
				out = cache.digest; return;
			}
			const auto files = conversation_files(root);
			if (cache.valid && files == cache.files) { out = cache.digest; return; }
			// Publish only a complete, stable snapshot; a failed refresh must never
			// fall back to an older digest. The digest bytes and path ordering are unchanged.
			cache.valid = false;
			std::vector<std::uint8_t> input;
			for (const auto& file : files)
			{
				std::vector<std::uint8_t> bytes;
				if (!detail::read_file_bytes(file.path, bytes)) detail::fail("conversation file cannot be read");
				const auto hash = sha1(bytes.data(), bytes.size());
				input.insert(input.end(), file.name.begin(), file.name.end()); input.push_back(0);
				input.insert(input.end(), hash.begin(), hash.end());
			}
			if (files != conversation_files(root)) detail::fail("conversation files changed while hashing");
			cache.digest = sha256(input.data(), input.size());
			cache.files = files;
			// Other filesystems still get the established full-hash behavior, without
			// assuming their timestamps can identify a content-preserving-time edit.
			cache.valid = std::all_of(files.begin(), files.end(), [](const auto& file) { return file.versioned; });
			out = cache.digest;
		});
	}

	bool prepare_conversation_digest(const Tables& tables, std::string& error)
	{
		error.clear();
		return guarded(error, [&]
		{
			if (!tables.loaded() || !tables.data->build) detail::fail("conversation digest needs build tables");
			auto& cache = *tables.data->build->conversations;
			const auto root = std::filesystem::path(tables.data->build->data_root) / L"Conversations";
			// Watch before hashing so mutations during preparation cannot escape the pin.
			const HANDLE notification = FindFirstChangeNotificationW(root.c_str(), TRUE,
				FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES |
				FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SECURITY);
			if (notification == INVALID_HANDLE_VALUE) detail::fail("conversation changes cannot be monitored");
			std::unique_ptr<void, decltype(&FindCloseChangeNotification)> owned(notification, &FindCloseChangeNotification);
			std::array<std::uint8_t, 32> digest{};
			if (!conversation_digest(tables, digest, error)) detail::fail(error);
			std::lock_guard lock(cache.mutex);
			if (WaitForSingleObject(notification, 0) != WAIT_TIMEOUT)
				detail::fail("conversation files changed while preparing identity");
			if (cache.change_notification) FindCloseChangeNotification(cache.change_notification);
			cache.change_notification = owned.release();
			cache.digest = digest;
			cache.valid = true;
		});
	}

	bool conversation_digest(const Tables& tables, const std::string& zone, std::array<std::uint8_t, 32>& out, std::string& error)
	{
		error.clear();
		return guarded(error, [&]
		{
			if (!tables.loaded() || !tables.data->build) detail::fail("conversation digest needs build tables");
			out = detail::conversation_package_digest(key_hash1(zone, Enc::latin1), detail::conversation_bits(*tables.data->build, zone));
		});
	}

	bool prepare_conversation_packages(const Tables& tables, const ConversationSources& sources, std::string& error)
	{
		error.clear();
		return guarded(error, [&]
		{
			if (!tables.loaded() || !tables.data->build) detail::fail("conversation preparation needs build tables");
			detail::pin_conversation_packages(*tables.data->build, sources);
		});
	}

	bool prepared_conversation_digest(const Tables& tables, const std::string& zone,
		std::array<std::uint8_t, 32>& out, std::string& error)
	{
		error.clear();
		return guarded(error, [&]
		{
			if (!tables.loaded() || !tables.data->build) detail::fail("conversation capture needs build tables");
			out = detail::pinned_conversation_package(*tables.data->build, zone).digest;
		});
	}

	bool loaded_zone_metadata(const std::vector<std::uint8_t>& file, const Tables& tables,
		std::vector<ZoneCaptureMetadata>& out, std::string& error)
	{
		return loaded_zone_metadata(file, tables, out, error, retail_layout, nullptr);
	}

	bool loaded_zone_metadata(const std::vector<std::uint8_t>& file, const Tables& tables,
		std::vector<ZoneCaptureMetadata>& out, std::string& error, const NativeLayout& layout, const TypedRegistry* registry)
	{
		out.clear(); error.clear();
		const bool ok = guarded(error, [&]
		{
			if (!tables.loaded() || !tables.data->build) detail::fail("loaded metadata needs build tables");
			std::vector<std::string> notes;
			const auto native = detail::reverse_slot(file, *tables.data, nullptr, notes, layout, registry, nullptr);
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
				if (!conversation_digest(tables, metadata.zone, metadata.conversation_digest, error)) detail::fail(error);
				if (f.data("META")["origin"].as_int() == 1 || record["conversation_digest"].as_bytes() == std::string(32, '\0'))
					metadata.capture_flags |= 2; // earlier candidates wrote zero despite unobserved provenance
				const auto* numbering = tables.data->build->zone_numbering(metadata.zone);
				if (!numbering) continue; // no placement metadata: discard this record cleanly
				const auto* prefix = tables.data->build->zone_prefix(metadata.zone,record["content_hash"].as_bytes(),*numbering);
				// equivalent maps: the capture describes the current map, as an exact-hash record does
				if (prefix && prefix->placements == numbering->placement) prefix = nullptr;
				metadata.placement_sha1 = numbering->sha1;
				if (prefix) metadata.source_placement_sha1 = prefix->old;
				const auto restored = std::find_if(native.zones.begin(), native.zones.end(), [&](const auto& z)
				{ return detail::zone_string(*native.zone_strings, z.name_handle) == metadata.zone; });
				// Metadata must describe the reconstructed map, including promoted legacy placements.
				bool invalid = false;
				for (std::size_t k = 0; k < restored->tail.ordinal_map.size(); ++k)
				{
					const auto idx = restored->tail.ordinal_map[k];
					if (!idx) continue;
					if (k >= numbering->placement.size() || (prefix && k >= prefix->placements.size()) || idx >= restored->states.size())
					{ invalid = true; break; }
					PlacedEntityMetadata entity;
					entity.ordinal = static_cast<std::uint32_t>(k + 1);
					entity.placement_index = numbering->placement[k];
					if (prefix) entity.source_placement_index = prefix->placements[k];
					entity.definition_name = detail::zone_string(*native.zone_strings, restored->states[idx].def_name);
					entity.instance_name = detail::zone_string(*native.zone_strings, restored->states[idx].inst_name);
					metadata.entities.push_back(std::move(entity));
				}
				if (invalid) continue;
				if (prefix) for (std::size_t k = prefix->placements.size(); k < numbering->names.size(); ++k)
				{
					if (k >= numbering->placement.size() || k >= numbering->name_text.size()) { invalid = true; break; }
					PlacedEntityMetadata fresh;
					fresh.ordinal=static_cast<std::uint32_t>(k+1);fresh.placement_index=numbering->placement[k];
					fresh.definition_name=numbering->name_text[k].first;fresh.instance_name=numbering->name_text[k].second;
					metadata.fresh_entities.push_back(std::move(fresh));
				}
				if (invalid) continue;
				out.push_back(std::move(metadata));
			}
		});
		if (!ok) out.clear();
		return ok;
	}

	bool native_zone_metadata(const std::vector<std::uint8_t>& stream, const Tables& tables,
		std::vector<ZoneCaptureMetadata>& out, std::string& error)
	{
		return native_zone_metadata(stream, tables, out, error, retail_layout);
	}

	bool native_zone_metadata(const std::vector<std::uint8_t>& stream, const Tables& tables,
		std::vector<ZoneCaptureMetadata>& out, std::string& error, const NativeLayout& native)
	{
		out.clear(); error.clear();
		// Reuse the strict same-build native translator to validate numbering/names and scalar ranges.
		// This temporary container is never written or installed; it supplies inferred associations only.
		NewFileContext context;
		const auto translated=old_stream_to_new(stream,tables,context,native);
		if(!translated.ok){error=translated.error;return false;}
		// Do not reverse this temporary file: reverse computes conversation digests, which can
		// stall an online join on a cold filesystem. The translator already verified these names.
		const bool ok=guarded(error,[&]
		{
			std::vector<std::uint8_t> image = stream;
			if (image.size() == native.payload)
			{
				image.assign(old_envelope, 0);
				const std::uint32_t size = native.payload;
				std::memcpy(image.data()+0x80, &size, 4);
				image.insert(image.end(), stream.begin(), stream.end());
			}
			auto fresh = detail::native_fresh_tail(detail::read_slot(image.data(), image.size(), native), *tables.data->build);
			const auto file=detail::read_file(translated.bytes.data(),translated.bytes.size(),kind_campaign);
			detail::validate_native_sections(file,native);
			for(const auto& record:file.data("ZONE")["records"].as_list())
			{
				const auto zone=tables.data->p_zone.find(static_cast<std::uint32_t>(record["zone"].as_int()));
				if(zone==tables.data->p_zone.end())detail::fail("unknown native zone metadata");
				const auto provenance = fresh.find(zone->second);
				if (provenance != fresh.end())
				{
					out.push_back(std::move(provenance->second)); fresh.erase(provenance); continue;
				}
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
			if (!fresh.empty()) detail::fail("native fresh-tail record was not translated");
		});
		if(!ok)out.clear();return ok;
	}

	bool native_load_demand(const std::vector<std::uint8_t>& stream, NativeLoadDemand& out, std::string& error)
	{
		return native_load_demand(stream, out, error, retail_layout);
	}

	bool native_load_demand(const std::vector<std::uint8_t>& stream, NativeLoadDemand& out, std::string& error, const NativeLayout& native)
	{
		out = {}; error.clear();
		return guarded(error, [&]
		{
			std::vector<std::uint8_t> image = stream;
			if (stream.size() == native.payload)
			{
				image.assign(old_envelope, 0);
				const std::uint32_t size = native.payload;
				std::memcpy(image.data() + 0x80, &size, 4);
				image.insert(image.end(), stream.begin(), stream.end());
			}
			const auto old = detail::read_slot(image.data(), image.size(), native);
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
		return validate_native_stream(stream, tables, error, retail_layout);
	}

	bool validate_native_stream(const std::vector<std::uint8_t>& stream, const Tables& tables, std::string& error, const NativeLayout& native)
	{
		NativeLoadDemand demand;
		if (!tables.loaded()) { error = "tables not loaded"; return false; }
		if (!native_load_demand(stream, demand, error, native)) return false;
		if (demand.instances > 256 || demand.attached_enhancements > 375 || demand.world_items > 150)
		{ error = "native instance/enhancement/world pool exhausted"; return false; }
		NewFileContext context;
		const auto result = old_stream_to_new(stream, tables, context, native);
		error = result.error;
		return result.ok;
	}

}
