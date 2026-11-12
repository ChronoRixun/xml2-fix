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

	// Observers are for native barriers/fault probes; normal callers leave this null.
	enum class transaction_step { lease_wait, lease_acquired, scratch_verified, before_replace, published };
	using transaction_observer = void (*)(transaction_step, void*);
	inline void observe(transaction_observer after, transaction_step step, void* context)
	{ if (after) after(step, context); }
	constexpr DWORD save_lease_wait_ms = 3000;

	// Resolve aliases before choosing the lease. For a new file, resolve its existing parent directory instead.
	// Use the resolved path for ALL subsequent I/O, so a changed directory junction cannot redirect the transaction.
	inline bool canonical_destination(const std::string& path, std::string& canonical, std::string& error)
	{
		const DWORD needed = GetFullPathNameA(path.c_str(), 0, nullptr, nullptr);
		if (!needed) { error = "can't resolve save path"; return false; }
		std::vector<char> full(needed);
		const auto length = GetFullPathNameA(path.c_str(), needed, full.data(), nullptr);
		if (!length || length >= needed)
		{ error = "can't resolve save path"; return false; }
		std::string absolute(full.data()), leaf;
		HANDLE h = CreateFileA(absolute.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
		                      nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
		if (h == INVALID_HANDLE_VALUE)
		{
			const auto code = GetLastError();
			if (code != ERROR_FILE_NOT_FOUND)
			{ error = "can't resolve save destination (error " + std::to_string(code) + ")"; return false; }
			const auto slash = absolute.find_last_of("\\/");
			if (slash == std::string::npos || slash + 1 == absolute.size())
			{ error = "save destination has no filename"; return false; }
			leaf = absolute.substr(slash + 1);
			// Ordinary Win32 paths silently trim these characters; refuse ambiguous/extended spellings.
			if (leaf.back() == '.' || leaf.back() == ' ' || leaf.find(':') != std::string::npos)
			{ error = "ambiguous save filename; replacement refused"; return false; }
			h = CreateFileA(absolute.substr(0, slash + 1).c_str(), FILE_READ_ATTRIBUTES,
			                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
			                FILE_FLAG_BACKUP_SEMANTICS, nullptr);
			if (h == INVALID_HANDLE_VALUE)
			{ error = "can't resolve save directory (error " + std::to_string(GetLastError()) + ")"; return false; }
		}
		const auto size = GetFinalPathNameByHandleA(h, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
		std::vector<char> resolved(size ? size + 1 : 1);
		const auto got = size ? GetFinalPathNameByHandleA(h, resolved.data(), static_cast<DWORD>(resolved.size()),
		                                               FILE_NAME_NORMALIZED | VOLUME_NAME_DOS) : 0;
		CloseHandle(h);
		if (!got || got >= resolved.size())
		{ error = "can't canonicalize save destination; replacement refused"; return false; }
		canonical.assign(resolved.data(), got);
		if (!leaf.empty())
		{
			if (canonical.back() != '\\') canonical += '\\';
			canonical += leaf;
		}
		return true;
	}

	class destination_lease
	{
		HANDLE handle_ = INVALID_HANDLE_VALUE;
		std::string path_;
	public:
		destination_lease() = default;
		destination_lease(const destination_lease&) = delete;
		destination_lease& operator=(const destination_lease&) = delete;
		~destination_lease() { if (handle_ != INVALID_HANDLE_VALUE) CloseHandle(handle_); }
		const std::string& path() const { return path_; }
		bool held() const { return handle_ != INVALID_HANDLE_VALUE; }
		bool acquire(const std::string& path, std::string& error, DWORD wait_ms = save_lease_wait_ms,
		             transaction_observer after = nullptr, void* context = nullptr)
		{
			if (held()) { error = "save lease already held"; return false; }
			if (!canonical_destination(path, path_, error)) return false;
			const auto lease_path = path_ + ".lock";
			const auto start = GetTickCount64();
			for (;;)
			{
				// Never delete this file: closing/crashing releases ownership, and OPEN_ALWAYS reuses it.
				HANDLE h = CreateFileA(lease_path.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_ALWAYS,
				                       FILE_ATTRIBUTE_NORMAL, nullptr);
				DWORD code = GetLastError();
				if (h != INVALID_HANDLE_VALUE)
				{
					OVERLAPPED at{};
					// Also require a byte lock. If a Wine/filesystem implementation ignores sharing, the lock
					// must still exclude writers. Unsupported locking refuses; there is no unlocked fallback.
					if (LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &at))
					{
						handle_ = h;
						observe(after, transaction_step::lease_acquired, context);
						return true;
					}
					code = GetLastError();
					CloseHandle(h);
				}
				if (code != ERROR_SHARING_VIOLATION && code != ERROR_LOCK_VIOLATION)
				{ error = "can't acquire save transaction lease (error " + std::to_string(code) + "); nothing written"; return false; }
				observe(after, transaction_step::lease_wait, context);
				if (GetTickCount64() - start >= wait_ms)
				{ error = "another writer is saving this file; save transaction lease timed out; try saving again"; return false; }
				Sleep(20);
			}
		}
	};

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

	// Only a successful CREATE_NEW grants cleanup ownership. Stale/foreign files are never truncated or deleted.
	class owned_scratch
	{
		std::string path_;
		bool owned_ = false;
	public:
		owned_scratch() = default;
		owned_scratch(const owned_scratch&) = delete;
		owned_scratch& operator=(const owned_scratch&) = delete;
		~owned_scratch() { if (owned_) DeleteFileA(path_.c_str()); }
		const std::string& path() const { return path_; }
		void published() { owned_ = false; }
		bool create(const std::string& target, const std::vector<std::uint8_t>& bytes,
		            std::string& error, failure_point fault)
		{
			if (owned_) { error = "save scratch already owned"; return false; }
			if (injected(fault, failure_point::create, error)) return false;
			static volatile LONG sequence = 0;
			HANDLE h = INVALID_HANDLE_VALUE;
			for (unsigned attempt = 0; attempt < 32; ++attempt)
			{
				path_ = target + ".tmp." + std::to_string(GetCurrentProcessId()) + "." +
				        std::to_string(GetTickCount64()) + "." + std::to_string(static_cast<ULONG>(InterlockedIncrement(&sequence)));
				h = CreateFileA(path_.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
				if (h != INVALID_HANDLE_VALUE) { owned_ = true; break; }
				const auto code = GetLastError();
				if (code != ERROR_FILE_EXISTS && code != ERROR_ALREADY_EXISTS)
				{ error = "can't create save scratch (error " + std::to_string(code) + ")"; return false; }
			}
			if (!owned_) { error = "can't reserve a unique save scratch name"; return false; }
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
				if (error.empty()) error = "write/flush failed for " + path_;
				return false;
			}
			std::vector<std::uint8_t> check;
			ok = !injected(fault, failure_point::verify, error) && save_format_rules::read_file(path_, check, error) && check == bytes;
			if (!ok)
			{
				if (error.empty()) error = "verification failed for " + path_;
			}
			return ok;
		}
	};

	// Protected copies: <file>.newer, then <file>.newer.2 .. .newer.9. A copy with identical bytes under any of these
	// names is reused; a different original takes the first free name and never replaces an existing copy. A save
	// whose destination AND rotated .bak are both protected (two different files from a preview or newer build, such
	// as a carried-over profile) needs two names; with one name every later save refused (0.6.0-beta.6 carry-over).
	// The names are bounded: when all hold different bytes the write refuses. A legacy <name>.tmp also refuses until
	// inspected/removed. Owned CREATE_NEW scratch and a rename without REPLACE prevent racing publishers.
	constexpr int protected_copy_names = 9;
	inline std::string protected_copy_path(const std::string& path, const int index)
	{ return path + ".newer" + (index <= 1 ? std::string() : "." + std::to_string(index)); }

	inline bool preserve_newer(const std::string& path, const std::vector<std::uint8_t>& original,
							   std::string& error, failure_point fault = failure_point::none)
	{
		error.clear();
		std::vector<std::uint8_t> check;
		std::string free_path;
		for (int index = 1; index <= protected_copy_names; ++index)
		{
			const auto candidate = protected_copy_path(path, index);
			if (exists(candidate))
			{
				std::string read_error;
				if (save_format_rules::read_file(candidate, check, read_error) && check == original) return true;
			}
			else if (free_path.empty()) free_path = candidate;
		}
		if (free_path.empty())
		{
			error = "different or unreadable protected copies already use every name " + protected_copy_path(path, 1) +
			        " to " + protected_copy_path(path, protected_copy_names) + "; original kept";
			return false;
		}
		if (exists(free_path + ".tmp"))
		{ error = "unowned legacy protected scratch at " + free_path + ".tmp; original kept"; return false; }
		owned_scratch tmp;
		if (!tmp.create(free_path, original, error, fault)) return false;
		if (injected(fault, failure_point::newer_publish, error) ||
			!MoveFileExA(tmp.path().c_str(), free_path.c_str(), MOVEFILE_WRITE_THROUGH))
		{
			if (error.empty()) error = "can't publish " + free_path + " (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		tmp.published();
		if (!save_format_rules::read_file(free_path, check, error) || check != original)
		{
			error = "protected original did not verify at " + free_path;
			return false;
		}
		return true;
	}

	// LC only: generic ledger/debug-dump behavior stays in write_atomically. The old destination stays in place
	// until the final atomic replacement; a failure at any earlier step leaves it byte-identical.
	inline bool write_lc_under_lease(const destination_lease& lease, const std::vector<std::uint8_t>& bytes,
									std::string& error, failure_point fault = failure_point::none,
									transaction_observer after = nullptr, void* context = nullptr)
	{
		error.clear();
		if (!lease.held()) { error = "save transaction lease is not held; nothing written"; return false; }
		const auto& path = lease.path();
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
		owned_scratch tmp;
		if (!tmp.create(path, bytes, error, fault)) return false;
		observe(after, transaction_step::scratch_verified, context);
		if (intact)
		{
			// An earlier DLL may have left the only newer original in .bak. Preserve it before rotating it.
			const auto backup = path + ".bak";
			if (exists(backup))
			{
				std::vector<std::uint8_t> prior_backup;
				if (!save_format_rules::read_file(backup,prior_backup,error)) return false;
				const auto backup_info = inspect(prior_backup.data(),prior_backup.size(),0);
				if (backup_info.status != file_status::damaged &&
					protected_source(backup_info,new_info) &&
					!preserve_newer(path,prior_backup,error,fault)) return false;
			}
			owned_scratch backup_tmp;
			if (!backup_tmp.create(path + ".bak", original, error, fault)) return false;
			if (injected(fault, failure_point::backup_publish, error) ||
				!MoveFileExA(backup_tmp.path().c_str(), (path+".bak").c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			{
				if (error.empty()) error = "can't publish backup (error " + std::to_string(GetLastError()) + ")";
				return false;
			}
			backup_tmp.published();
		}
		// Keep the final check too: tools/older DLLs may not participate in the lease.
		observe(after, transaction_step::before_replace, context);
		std::vector<std::uint8_t> check;
		if (had_original != exists(path) ||
			(had_original && (!save_format_rules::read_file(path,check,error) || check != original)))
		{
			error = "destination changed during write; replacement refused";
			return false;
		}
		if (injected(fault, failure_point::replace, error) ||
			!MoveFileExA(tmp.path().c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		{
			if (error.empty()) error = "can't replace destination (error " + std::to_string(GetLastError()) + ")";
			return false;
		}
		tmp.published();
		observe(after, transaction_step::published, context);
		// Never let the caller commit sequence/retained state just because a rename succeeded.
		if (!save_format_rules::read_file(path, check, error) || check != bytes)
		{ error = "published save did not verify; in-memory save state was not committed"; return false; }
		return true;
	}

	inline bool write_lc_atomically(const std::string& path, const std::vector<std::uint8_t>& bytes,
		                               std::string& error, failure_point fault = failure_point::none,
		                               transaction_observer after = nullptr, void* context = nullptr,
		                               DWORD wait_ms = save_lease_wait_ms)
	{
		error.clear();
		destination_lease lease;
		return lease.acquire(path, error, wait_ms, after, context) &&
		       write_lc_under_lease(lease, bytes, error, fault, after, context);
	}

	// Automatic legacy import selects a free slot before conversion. Another process may fill it meanwhile.
	// Join the regular save lease, then require BOTH names to be absent; never replace a newly saved campaign.
	inline bool import_lc_if_absent(const std::string& path, const std::vector<std::uint8_t>& bytes,
	                                std::string& error, transaction_observer after = nullptr, void* context = nullptr)
	{
		error.clear();
		destination_lease lease;
		if (!lease.acquire(path, error, save_lease_wait_ms, after, context)) return false;
		for (const auto& candidate : {lease.path(), lease.path() + ".bak"})
		{
			const auto attributes = GetFileAttributesA(candidate.c_str());
			if (attributes != INVALID_FILE_ATTRIBUTES)
			{ error = "chosen import destination is no longer empty (primary or backup exists); nothing written"; return false; }
			if (GetLastError() != ERROR_FILE_NOT_FOUND)
			{ error = "can't confirm that the import destination is empty; nothing written"; return false; }
		}
		return write_lc_under_lease(lease, bytes, error, failure_point::none, after, context);
	}
}
