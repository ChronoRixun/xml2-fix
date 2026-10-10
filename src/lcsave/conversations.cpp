// Native heard marks have four consumption lanes per alphabetically ranked file.
// 0x45cb5a enumerates the file rank; 0x45c56e / 0x45c679 use rank*4 + consumed starts.
#include "internal.hpp"
#include "xmlb.hpp"
#include <algorithm>
#include <set>
#include <cstring>

namespace lcsave::detail
{
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
			if (name.starts_with("conversations/"))
			{
				if (py_lower(node.tag, Enc::latin1) != "xml") fail("conversation mapping: unsupported package resource");
				for (const auto* ext : {".engb", ".xmlb", ".eng", ".xml"})
					if (name.ends_with(ext)) { name.resize(name.size() - std::strlen(ext)); break; }
				files.insert(name.substr(14));
			}
			for (const auto& child : node.children) collect(child, files);
		}
	}
	std::vector<Hash> conversation_bits(const Build& build, const std::string& zone)
	{
		auto rel = py_lower(zone, Enc::cp1252);
		std::replace(rel.begin(), rel.end(), '/', '\\');
		std::wstring wide; for (const unsigned char c : rel) wide.push_back(c);
		const auto path = build.data_root + L"\\Packages\\generated\\maps\\" + wide + L".pkgb";
		std::vector<std::uint8_t> bytes;
		if (!read_file_bytes(path, bytes)) fail("conversation mapping: zone package missing");
		std::set<std::string> files;
		collect(decode_xmlb(bytes.data(), bytes.size()), files);
		if (files.size() > 32) fail("conversation mapping: more than 32 loaded files");
		std::vector<Hash> bits;
		for (const auto& name : files) for (int lane = 0; lane < 4; ++lane)
		{
			const auto slot = std::to_string(lane);
			bits.push_back(key_hash({name, slot}, Enc::latin1));
		}
		return bits;
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
