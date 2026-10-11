#pragma once

#include "keys.hpp"
#include <mutex>
#include <map>
#include <memory>
#include <functional>
#include <string>
#include <vector>

namespace lcsave::detail
{
	// A file version includes change time and identity: replacing a file or preserving
	// its write time after an edit must not reuse the earlier content digest.
	struct ConversationFile
	{
		std::string name;
		std::wstring path;
		std::uint64_t size = 0, write_time = 0, change_time = 0, file_id = 0;
		std::uint32_t volume = 0;
		bool versioned = false;
		bool operator==(const ConversationFile&) const = default;
	};
	struct ConversationDependency;
	struct ConversationPackage
	{
		std::vector<Hash> bits;
		Hash digest{};
		std::wstring effective_path;
	};
	struct ConversationCache
	{
		~ConversationCache();
		std::mutex mutex;
		void* change_notification = nullptr;
		bool valid = false;
		std::vector<ConversationFile> files;
		Hash digest{};
		bool engine_required = false, engine_ready = false;
		std::vector<std::shared_ptr<ConversationDependency>> dependencies;
		std::function<bool()> sources_unchanged;
		std::map<std::string, ConversationPackage> packages;
	};
}
