#pragma once

#include "save_format_rules.hpp"
#include <map>

namespace save_guard_rules
{
	using namespace save_format_rules;

	// Fix and importer versions share the fix's semver. Other writer kinds have no comparable epoch.
	inline bool newer_writer(std::uint32_t source, std::uint32_t current)
	{
		const auto kind = static_cast<std::uint8_t>(source);
		return (kind != writer_kind_fix && kind != writer_kind_import) || (source >> 8) > (current >> 8);
	}

	// A save that needs only opt-in settings this build has (Actors=96, with or without the W2 layout), switched off on
	// this start: name the setting and what to do. When Actors=96 is set but did not install, say why instead.
	inline std::string settings_save_message(const std::uint32_t needs)
	{
		const bool w2 = (needs & lc_feature_native_w2) != 0;
		std::string text = w2 ? "This save was made with 96 actors and the W2 save layout turned on. "
		                      : "This save was made with 96 actors turned on. ";
		if (!lc_actor_capacity96_requested)
			return text + (w2 ? "Turn on Actors=96 and SaveLayout=w2" : "Turn on Actors=96") + " ([Limits] in xml2-fix.ini) and restart to load it.";
		text += "Actors=96 is in xml2-fix.ini but did not start";
		if (!lc_actor_capacity96_failure.empty()) text += " (" + lc_actor_capacity96_failure + ")";
		return text + (w2 ? "; fix that, turn on SaveLayout=w2 ([Limits] in xml2-fix.ini) and restart to load it."
		                  : "; fix that and restart to load it.");
	}

	inline std::string newer_save_message(const inspection& source)
	{
		if (source.needs_settings & lc_feature_actor_capacity96) return settings_save_message(source.needs_settings);
		if (source.needs_layout) return source.why; // retain the named, actionable W2 refusal
		return "This save was made by a newer Legends Classic build - update to load it";
	}

	inline bool protected_source(const inspection& source, const inspection& replacement)
	{
		return source.status == file_status::needs_newer ||
		       newer_writer(source.writer_version,replacement.writer_version) ||
		       source.content_version > replacement.content_version ||
		       source.catalogue_revision > replacement.catalogue_revision;
	}

	class refusal_latches
	{
		std::map<std::string, std::string> refused_;
	public:
		void refuse(const std::string& path, const std::string& why) { refused_[path] = why; }
		std::string reason(const std::string& path) const
		{
			const auto it = refused_.find(path);
			return it == refused_.end() ? std::string() : it->second;
		}
		void successful_load(const std::string& path) { refused_.erase(path); }
		void explicit_delete(const std::string& path) { refused_.erase(path); }
	};

	inline bool startup_allowed(bool lc, bool exclusion_installed) { return !lc || exclusion_installed; }
	inline bool online_allowed(bool lc, bool version_verified, bool gameplay_required = false, bool gameplay_verified = false)
	{ return (!lc || version_verified) && (!gameplay_required || gameplay_verified); }
	inline bool registry_full(unsigned names, unsigned objects, unsigned handles, unsigned capacity = 100)
	{ return names >= capacity || objects >= capacity || handles >= capacity; }

	enum class failure_point { none, create, write, flush, verify, newer_publish, backup_publish, replace };

	inline bool injected(failure_point chosen, failure_point at, std::string& error)
	{
		if (chosen != at) return false;
		error = "injected file-operation failure " + std::to_string(static_cast<int>(at));
		return true;
	}

	inline bool exists(const std::string& path)
	{ return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES; }

	// The native device-info count controls whether Load Game opens the populated LC list.
	// Count occupied slots once, including recoverable backups and intact refused files.
	inline unsigned count_lc_slots(const std::string& directory)
	{
		unsigned count = 0;
		for (int slot = 0; slot < slot_count; ++slot)
		{
			const auto path = directory + slot_file(slot);
			if (exists(path) || exists(path + ".bak")) ++count;
		}
		return count;
	}

	// A scratch file is flushed and compared byte for byte before it may become a backup or the destination.
	inline bool durable_scratch(const std::string& path, const std::vector<std::uint8_t>& bytes,
								std::string& error, failure_point fault, DWORD disposition = CREATE_ALWAYS)
	{
		if (injected(fault, failure_point::create, error)) return false;
		const HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
		if (h == INVALID_HANDLE_VALUE)
		{
			error = "can't create " + path + " (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		std::size_t done = 0;
		bool ok = !injected(fault, failure_point::write, error);
		while (ok && done < bytes.size())
		{
			DWORD put = 0;
			ok = WriteFile(h, bytes.data() + done, static_cast<DWORD>(std::min<std::size_t>(bytes.size()-done, 1u<<20)), &put, nullptr) && put;
			done += put;
		}
		ok = ok && !injected(fault, failure_point::flush, error) && FlushFileBuffers(h);
		CloseHandle(h);
		if (!ok)
		{
			if (error.empty()) error = "write/flush failed for " + path;
			DeleteFileA(path.c_str());
			return false;
		}
		std::vector<std::uint8_t> check;
		ok = !injected(fault, failure_point::verify, error) && save_format_rules::read_file(path, check, error) && check == bytes;
		if (!ok)
		{
			if (error.empty()) error = "verification failed for " + path;
			DeleteFileA(path.c_str());
		}
		return ok;
	}

	// Collision policy: identical .newer is reused; different bytes refuse, never replace it. A stale .newer.tmp
	// also refuses until inspected/removed. CREATE_NEW and a rename without REPLACE prevent racing publishers.
	inline bool preserve_newer(const std::string& path, const std::vector<std::uint8_t>& original,
							   std::string& error, failure_point fault = failure_point::none)
	{
		error.clear();
		const auto protected_path = path + ".newer";
		std::vector<std::uint8_t> check;
		if (exists(protected_path))
		{
			if (save_format_rules::read_file(protected_path, check, error) && check == original) return true;
			error = "a different or unreadable " + protected_path + " already exists; original kept";
			return false;
		}
		const auto tmp = protected_path + ".tmp";
		if (!durable_scratch(tmp, original, error, fault, CREATE_NEW)) return false;
		if (injected(fault, failure_point::newer_publish, error) ||
			!MoveFileExA(tmp.c_str(), protected_path.c_str(), MOVEFILE_WRITE_THROUGH))
		{
			if (error.empty()) error = "can't publish " + protected_path + " (error " + std::to_string(GetLastError()) + ")";
			DeleteFileA(tmp.c_str());
			return false;
		}
		if (!save_format_rules::read_file(protected_path, check, error) || check != original)
		{
			error = "protected original did not verify at " + protected_path;
			return false;
		}
		return true;
	}

	// LC only: generic ledger/import behavior stays in write_atomically. The old destination stays in place
	// until the final atomic replacement; a failure at any earlier step leaves it byte-identical.
	inline bool write_lc_atomically(const std::string& path, const std::vector<std::uint8_t>& bytes,
									std::string& error, failure_point fault = failure_point::none)
	{
		error.clear();
		const auto new_info = inspect(bytes.data(), bytes.size(), 0);
		if (new_info.status != file_status::ok)
		{
			error = "the replacement is not a supported LC file";
			return false;
		}
		std::vector<std::uint8_t> original;
		const bool had_original = exists(path);
		bool intact = false;
		if (had_original)
		{
			if (!save_format_rules::read_file(path, original, error)) return false;
			const auto old_info = inspect(original.data(), original.size(), 0);
			intact = old_info.status != file_status::damaged;
			if (old_info.kind && old_info.kind != new_info.kind)
			{
				error = "the destination has the wrong LC kind; original kept";
				return false;
			}
			if (intact && protected_source(old_info,new_info) &&
				!preserve_newer(path, original, error, fault)) return false;
		}
		const auto tmp = path + ".tmp";
		if (!durable_scratch(tmp, bytes, error, fault)) return false;
		if (intact)
		{
			// An earlier DLL may have left the only newer original in .bak. Preserve it before rotating it.
			const auto backup = path + ".bak";
			if (exists(backup))
			{
				std::vector<std::uint8_t> prior_backup;
				if (!save_format_rules::read_file(backup,prior_backup,error)) { DeleteFileA(tmp.c_str()); return false; }
				const auto backup_info = inspect(prior_backup.data(),prior_backup.size(),0);
				if (backup_info.status != file_status::damaged &&
					protected_source(backup_info,new_info) &&
					!preserve_newer(path,prior_backup,error,fault)) { DeleteFileA(tmp.c_str()); return false; }
			}
			const auto backup_tmp = path + ".bak.tmp";
			if (!durable_scratch(backup_tmp, original, error, fault)) { DeleteFileA(tmp.c_str()); return false; }
			if (injected(fault, failure_point::backup_publish, error) ||
				!MoveFileExA(backup_tmp.c_str(), (path+".bak").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				if (error.empty()) error = "can't publish backup (error " + std::to_string(GetLastError()) + ")";
				DeleteFileA(backup_tmp.c_str()); DeleteFileA(tmp.c_str()); return false;
			}
		}
		// Refuse concurrent destination changes instead of backing up one file and replacing another.
		std::vector<std::uint8_t> check;
		if (had_original != exists(path) ||
			(had_original && (!save_format_rules::read_file(path,check,error) || check != original)))
		{
			error = "destination changed during write; replacement refused";
			DeleteFileA(tmp.c_str()); return false;
		}
		if (injected(fault, failure_point::replace, error) ||
			!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			if (error.empty()) error = "can't replace destination (error " + std::to_string(GetLastError()) + ")";
			DeleteFileA(tmp.c_str()); return false;
		}
		return true;
	}
}
