// Native heard marks have four consumption lanes per alphabetically ranked file.
// 0x45cb5a enumerates the file rank; 0x45c56e / 0x45c679 use rank*4 + consumed starts.
#include "internal.hpp"
#include "xmlb.hpp"
#include <algorithm>
#include <set>
#include <cstring>
#include <Windows.h>
#include <filesystem>
#include <cwctype>

namespace lcsave::detail
{

	// A completion buffer is already in memory when polled on a frame. Directory
	// modification notifications alone are not content changes (enumeration emits
	// those too); names, file bytes and overflow/failure all invalidate the pin.
	struct ConversationDependency
	{
		HANDLE directory = INVALID_HANDLE_VALUE;
		OVERLAPPED pending{};
		alignas(DWORD) std::array<std::uint8_t, 65536> buffer{};
		std::set<std::wstring> directories;
		DWORD flags = 0;
		bool subtree = false, active = false;
		std::wstring root;
		static std::wstring normalized(std::wstring name)
		{
			for (auto& c : name) c = static_cast<wchar_t>(std::towlower(c));
			std::replace(name.begin(), name.end(), L'\\', L'/'); return name;
		}
		~ConversationDependency()
		{
			if (directory != INVALID_HANDLE_VALUE)
			{
				if (active)
				{
					CancelIoEx(directory, &pending);
					DWORD ignored = 0; GetOverlappedResult(directory, &pending, &ignored, TRUE);
				}
				CloseHandle(directory);
			}
			if (pending.hEvent) CloseHandle(pending.hEvent);
		}
		void arm()
		{
			const auto event = pending.hEvent; pending = {}; pending.hEvent = event;
			ResetEvent(pending.hEvent);
			if (!ReadDirectoryChangesW(directory, buffer.data(), static_cast<DWORD>(buffer.size()),
				subtree, flags, nullptr, &pending, nullptr))
				fail("conversation provenance: dependency notification cannot be armed");
			active = true;
		}
		void start(const std::filesystem::path& path, bool recursive, DWORD filter)
		{
			root = path.wstring(); subtree = recursive; flags = filter;
			directory = CreateFileW(path.c_str(), FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
				nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
			pending.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
			if (directory == INVALID_HANDLE_VALUE || !pending.hEvent)
				fail("conversation provenance: dependency cannot be watched");
			arm();
		}
		bool changed()
		{
			const DWORD result = WaitForSingleObject(pending.hEvent, 0);
			if (result == WAIT_TIMEOUT) return false;
			if (result != WAIT_OBJECT_0) return true;
			DWORD size = 0;
			const bool completed = GetOverlappedResult(directory, &pending, &size, FALSE) != FALSE;
			active = false;
			if (!completed || !size) return true; // buffer overflow is not a stable snapshot
			for (std::size_t offset = 0; offset < size;)
			{
				constexpr auto header = offsetof(FILE_NOTIFY_INFORMATION, FileName);
				if (size - offset < header) return true;
				const auto* item = reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(buffer.data() + offset);
				if (item->FileNameLength % sizeof(wchar_t) || item->FileNameLength > size - offset - header) return true;
				const auto name = normalized(std::wstring(item->FileName, item->FileNameLength / sizeof(wchar_t)));
				if (item->Action != FILE_ACTION_MODIFIED || !directories.contains(name)) return true;
				if (!item->NextEntryOffset) break;
				if (item->NextEntryOffset < header + item->FileNameLength || item->NextEntryOffset >= size - offset) return true;
				offset += item->NextEntryOffset;
			}
			arm(); // asynchronously re-arm after only harmless directory metadata notices
			return false;
		}
	};
	namespace
	{
		std::string attr(const XNode& node, const char* key)
		{
			for (auto i = node.attrs.rbegin(); i != node.attrs.rend(); ++i)
				if (py_lower(i->first, Enc::latin1) == key) return i->second;
			return {};
		}
		void collect(const XNode& node, std::set<std::string>& files)
		{
			auto name = py_lower(attr(node, "filename"), Enc::latin1);
			std::replace(name.begin(), name.end(), '\\', '/');
			if (name.starts_with("packages/")) fail("conversation mapping: nested package provenance cannot be pinned");
			if (name.starts_with("conversations/"))
			{
				if (py_lower(node.tag, Enc::latin1) != "xml") fail("conversation mapping: unsupported package resource");
				for (const auto* ext : {".engb", ".xmlb", ".eng", ".xml"})
					if (name.ends_with(ext)) { name.resize(name.size() - std::strlen(ext)); break; }
				if (name.find("..") != std::string::npos || name.find(':') != std::string::npos)
					fail("conversation mapping: unsupported conversation path");
				files.insert(name.substr(14));
			}
			for (const auto& child : node.children) collect(child, files);
		}
	}
	namespace
	{
		std::string folded_zone(std::string zone)
		{
			zone = py_lower(zone, Enc::cp1252);
			std::replace(zone.begin(), zone.end(), '\\', '/');
			if (zone.empty() || zone.front() == '/' || zone.find(':') != std::string::npos ||
				zone.find("..") != std::string::npos) fail("conversation mapping: invalid zone path");
			return zone;
		}
		std::wstring lower_path(std::wstring path)
		{
			for (auto& c : path) c = static_cast<wchar_t>(std::towlower(c));
			std::replace(path.begin(), path.end(), L'\\', L'/');
			return path;
		}
		std::filesystem::path package_path(const Build& build, const std::string& zone)
		{
			std::wstring wide; for (const unsigned char c : folded_zone(zone)) wide.push_back(c);
			return std::filesystem::path(build.data_root) / L"Packages/generated/maps" / (wide + L".pkgb");
		}
		std::vector<Hash> read_conversation_bits(const std::filesystem::path& path)
		{
			std::vector<std::uint8_t> bytes;
			if (!read_file_bytes(path.wstring(), bytes)) fail("conversation mapping: effective zone package missing");
			std::set<std::string> files;
			collect(decode_xmlb(bytes.data(), bytes.size()), files);
			if (files.size() > 32) fail("conversation mapping: more than 32 loaded files");
			std::vector<Hash> bits;
			for (const auto& name : files) for (int lane = 0; lane < 4; ++lane)
				bits.push_back(key_hash({name, std::to_string(lane)}, Enc::latin1));
			return bits;
		}
		const ConversationPackage& lookup_package(ConversationCache& cache, const std::string& zone)
		{
			if (!cache.engine_required || !cache.engine_ready)
				fail("conversation provenance is not prepared; LC capture/restore refused");
			cache.engine_ready = false; // Notification/callback exceptions must leave a sticky refusal.
			bool unchanged = cache.sources_unchanged && cache.sources_unchanged();
			for (const auto& watch : cache.dependencies)
				unchanged &= !watch->changed();
			if (!unchanged)
			{
				cache.engine_ready = false;
				fail("conversation packages, ranking, mods or lookup context changed during play; restart before saving/loading");
			}
			cache.engine_ready = true;
			const auto found = cache.packages.find(folded_zone(zone));
			if (found == cache.packages.end()) fail("conversation provenance: zone package was not pinned; LC capture/restore refused");
			return found->second;
		}
		struct Notifications
		{
			std::vector<std::shared_ptr<ConversationDependency>> handles;
			void watch(const std::filesystem::path& root, bool subtree, DWORD flags)
			{
				auto watch = std::make_shared<ConversationDependency>();
				watch->start(root, subtree, flags);
				if (subtree) for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
					if (entry.is_directory()) watch->directories.insert(ConversationDependency::normalized(entry.path().lexically_relative(root).wstring()));
				handles.push_back(std::move(watch));
			}
		};
		void no_reparse(const std::filesystem::path& root)
		{
			const auto attributes = GetFileAttributesW(root.c_str());
			if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
				fail("conversation provenance: reparse/unreadable dependency cannot be pinned");
			for (const auto& entry : std::filesystem::recursive_directory_iterator(root))
			{
				const auto a = GetFileAttributesW(entry.path().c_str());
				if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_REPARSE_POINT))
					fail("conversation provenance: reparse/unreadable dependency cannot be pinned");
			}
		}
	}
	ConversationPackage pinned_conversation_package(const Build& build, const std::string& zone)
	{
		auto& cache = *build.conversations;
		std::lock_guard lock(cache.mutex);
		return lookup_package(cache, zone);
	}
	std::vector<Hash> conversation_bits(const Build& build, const std::string& zone)
	{
		{
			auto& cache = *build.conversations;
			std::lock_guard lock(cache.mutex);
			if (cache.engine_required) return lookup_package(cache, zone).bits;
		}
		// Standalone conversion can read current packages. Engine tables never take this path.
		return read_conversation_bits(package_path(build, zone));
	}
	void pin_conversation_packages(Build& build, const ConversationSources& sources)
	{
		auto& cache = *build.conversations;
		std::lock_guard lock(cache.mutex);
		if (cache.engine_required) fail("conversation provenance preparation already attempted; restart with fresh tables");
		cache.engine_required = true; // A failed preparation must never fall back to disk in restore.
		cache.engine_ready = false;
		if (!sources.resolve || !sources.unchanged || !sources.unchanged())
			fail("conversation provenance: effective mod lookup is not ready for this game folder/working directory");
		const auto root = std::filesystem::absolute(build.data_root).lexically_normal();
		if (sources.lookup_root.empty() || lower_path(std::filesystem::absolute(sources.lookup_root).lexically_normal().wstring()) != lower_path(root.wstring()))
			fail("conversation provenance: tables and effective lookup use different game folders");
		const auto packages = root / L"Packages";
		const auto maps = packages / L"generated/maps";
		if (!std::filesystem::is_directory(maps)) fail("conversation provenance: zone packages are missing");
		Notifications watches;
		constexpr DWORD changes = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
			FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE;
		// Root directory changes also cover adding/removing/replacing dependency roots (including mods).
		watches.watch(root, false, FILE_NOTIFY_CHANGE_DIR_NAME);
		if (GetFileAttributesW(root.c_str()) & FILE_ATTRIBUTE_REPARSE_POINT)
			fail("conversation provenance: reparse build root cannot be pinned");
		for (const auto* name : {L"Packages", L"Conversations", L"mods"})
		{
			const auto folder = root / name;
			if (!std::filesystem::exists(folder)) continue;
			watches.watch(folder, true, changes);
			no_reparse(folder);
		}
		std::set<std::string> zones;
		for (const auto& entry : std::filesystem::recursive_directory_iterator(maps))
			if (entry.is_regular_file() && lower_path(entry.path().extension().wstring()) == L".pkgb")
			{
				auto rel = entry.path().lexically_relative(maps); rel.replace_extension();
				zones.insert(folded_zone(rel.generic_string()));
			}
		for (const auto& [relative, winner] : sources.mod_files)
		{
			const auto rel = lower_path(relative);
			if (rel.starts_with(L"conversations/"))
				fail("conversation provenance: conversation-file mod overrides have unsupported branch topology; disable the override");
			if (rel.starts_with(L"packages/generated/maps/"))
			{
				if (!rel.ends_with(L".pkgb")) fail("conversation provenance: non-PKGB map package override cannot be pinned");
				const auto zone = rel.substr(24, rel.size() - 24 - 5);
				std::string narrow; for (const auto c : zone)
				{
					if (c > 127) fail("conversation provenance: non-ASCII mod zone path cannot be pinned");
					narrow.push_back(static_cast<char>(c));
				}
				zones.insert(folded_zone(narrow));
			}
		}
		std::map<std::string, ConversationPackage> pinned;
		const auto package_prefix = lower_path(packages.wstring()) + L"/";
		const auto mod_prefix = lower_path((root / L"mods").wstring()) + L"/";
		for (const auto& zone : zones)
		{
			const auto effective = std::filesystem::absolute(sources.resolve(package_path(build, zone).wstring())).lexically_normal();
			const auto normalized = lower_path(effective.wstring());
			if (!normalized.starts_with(package_prefix) && !normalized.starts_with(mod_prefix))
				fail("conversation provenance: effective package lies outside watched package/mod roots");
			ConversationPackage package;
			package.effective_path = effective.wstring();
			package.bits = read_conversation_bits(effective);
			package.digest = conversation_package_digest(key_hash1(zone, Enc::latin1), package.bits);
			pinned.emplace(zone, std::move(package));
		}
		if (pinned.empty()) fail("conversation provenance: no zone packages could be pinned");
		if (!sources.unchanged()) fail("conversation provenance: mod lookup changed during preparation");
		for (std::size_t i = 0; i < watches.handles.size(); ++i)
			if (watches.handles[i]->changed())
				fail("conversation provenance: dependencies changed during preparation (watch " + std::to_string(i) + ")");
		cache.sources_unchanged = sources.unchanged;
		cache.packages = std::move(pinned);
		cache.dependencies = std::move(watches.handles);
		watches.handles.clear();
		cache.engine_ready = true;
	}

	Hash conversation_package_digest(const Hash& zone, const std::vector<Hash>& bits)
	{
		const char prefix[8] = {'L','C','C','O','N','V','2',0};
		std::vector<std::uint8_t> raw(prefix, prefix + 8);
		raw.insert(raw.end(), zone.begin(), zone.end());
		const u32 count = static_cast<u32>(bits.size());
		for (int shift = 0; shift < 32; shift += 8) raw.push_back(static_cast<std::uint8_t>(count >> shift));
		for (const auto& identity : bits) raw.insert(raw.end(), identity.begin(), identity.end());
		return sha256(raw.data(), raw.size());
	}
	void restore_conversation(const Value& record, const std::string& zone, const TablesData& t,
		u32 (&words)[5], std::vector<std::string>& notes)
	{
		const auto& conv = record["conversation"].as_list();
		if (conv.size() != 5) fail("pack expected 5 items for packing");
		bool any = false;
		for (int k = 0; k < 5; ++k) { words[k] = static_cast<u32>(conv[k].as_int()); any |= words[k] != 0; }
		if (!any) return;
		const auto target = conversation_bits(*t.build, zone);
		const auto current_digest = conversation_package_digest(key_hash1(zone, Enc::latin1), target);
		const auto& recorded = record["conversation_digest"].as_bytes();
		if (recorded.size() != 32) fail("conversation mapping: invalid digest");
		Hash digest{}; std::memcpy(digest.data(), recorded.data(), 32);
		if (digest == current_digest) return;
		const auto source = t.conversation_tables.find({digest, key_hash1(zone, Enc::latin1)});
		if (source == t.conversation_tables.end())
		{
			notes.push_back("zone " + zone + ": conversation provenance unknown: heard marks dropped");
			std::fill(std::begin(words), std::end(words), 0);
			return;
		}
		std::map<Hash, std::size_t> positions;
		for (std::size_t k = 0; k < target.size(); ++k)
			if (!positions.emplace(target[k], k).second) fail("conversation mapping: duplicate identity");
		u32 out[5]{};
		for (std::size_t bit = 0; bit < 160; ++bit)
		{
			if (!(words[bit / 32] & (u32(1) << (bit % 32)))) continue;
			if (bit >= source->second.size())
			{
				notes.push_back("zone " + zone + ": conversation bit " + std::to_string(bit) + ": historical identity unknown: heard mark dropped");
				continue;
			}
			const auto& identity = source->second[bit];
			const auto found = positions.find(identity);
			if (found == positions.end())
				notes.push_back("zone " + zone + ": conversation identity " + hex(identity) + ": absent in current package: heard mark dropped");
			else out[found->second / 32] |= u32(1) << (found->second % 32);
		}
		if (!std::equal(std::begin(out), std::end(out), std::begin(words)) && std::any_of(std::begin(out), std::end(out), [](u32 w) { return w != 0; }))
			notes.push_back("zone " + zone + ": conversation identities remapped to current package");
		std::copy(std::begin(out), std::end(out), std::begin(words));
	}
}
