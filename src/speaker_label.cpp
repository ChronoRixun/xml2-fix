#include "speaker_label.hpp"
#include "ini.hpp"
#include "log.hpp"
#include "game_version.hpp"
#include "limits_rules.hpp"
#include "xml1_hook_memory.hpp"
#include "xml1_hook_transaction.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace speaker_label
{
	namespace
	{
		bool installed = false;
		int relabelled = 0;

		// The line parser's common part (0x458820, __thiscall on the conversation system, ret 0xc): the node's
		// fields, the <line> data node, a flag; true when the line was kept. Its text (+0) and textb (+8) are the
		// game's interned strings (0x4566b0 writes the pair), both already through the speaker pass (0x456d00).
		constexpr std::uint32_t parse_va = 0x458820;
		constexpr std::uint32_t attribute_va = 0x5649c0; // __thiscall const char* (name), ret 4: the value or null
		constexpr std::uint32_t intern_va = 0x4566b0;    // __cdecl pair* (pair* out, const char*): the game's string
		constexpr std::uint32_t first_site = 0x458da8;   // the line parser 0x458d10's two calls of 0x458820
		constexpr std::uint32_t child_site = 0x458e7b;
		constexpr std::size_t text_limit = 0x400;        // the parser's own text buffers
		constexpr std::size_t label_limit = 64;

		using parse_t = bool(__thiscall*)(void*, void*, void*, int);
		using attribute_t = const char*(__thiscall*)(void*, const char*);
		using intern_t = void*(__cdecl*)(void*, const char*);

		void relabel_field(unsigned char* fields, std::size_t offset, void* node, const char* attribute, const char* label)
		{
			const char* raw = reinterpret_cast<attribute_t>(attribute_va)(node, attribute);
			const char* parsed = *reinterpret_cast<const char**>(fields + offset);
			std::string text;
			if (!relabel(parsed, raw, label, text)) return;
			std::uint32_t pair[2]{};
			reinterpret_cast<intern_t>(intern_va)(pair, text.c_str());
			if (!pair[0]) return;
			std::memcpy(fields + offset, pair, sizeof(pair));
			if (++relabelled <= 3) logger::write("ClassicSpeakers: \"%.40s\" labelled \"%s\"", text.c_str(), label);
		}

		bool __fastcall parse_line(void* system, void*, void* fields, void* node, int flag)
		{
			const bool kept = reinterpret_cast<parse_t>(parse_va)(system, fields, node, flag);
			if (!kept || !fields || !node) return kept;
			const char* label = reinterpret_cast<attribute_t>(attribute_va)(node, "speakerLabel");
			if (!label || !*label) return kept;
			auto* bytes = static_cast<unsigned char*>(fields);
			relabel_field(bytes, 0, node, "text", label);
			relabel_field(bytes, 8, node, "textb", label);
			return kept;
		}

		std::vector<unsigned char> call_bytes(std::uint32_t site, const void* target)
		{
			std::vector<unsigned char> bytes{0xe8, 0, 0, 0, 0};
			const auto delta = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(target)) - (site + 5);
			std::memcpy(bytes.data() + 1, &delta, 4);
			return bytes;
		}
	}

	bool relabel(const char* parsed, const char* raw, const char* label, std::string& out)
	{
		if (!parsed || !raw || !label || raw[0] != '%') return false;
		const std::size_t label_size = std::strlen(label);
		if (!label_size || label_size >= label_limit || std::strpbrk(label, "%\n")) return false;
		// The token: '%', up to 27 name characters, '%' (0x456d00's own limit), optionally followed by ": ".
		const char* close = std::strchr(raw + 1, '%');
		if (!close || close == raw + 1 || close - raw - 1 > 27) return false;
		const char* rest = close + 1;
		if (rest[0] == ':' && rest[1] == ' ') rest += 2;
		// The game resolved it: the text starts with the display name and "\n", then the raw rest unchanged
		// (anything else - an unresolved token left verbatim, a second token replaced further on - is left alone).
		const char* newline = std::strchr(parsed, '\n');
		if (parsed[0] == '%' || !newline || newline == parsed || static_cast<std::size_t>(newline - parsed) >= label_limit)
			return false;
		if (std::strcmp(newline + 1, rest) != 0) return false;
		if (label_size + std::strlen(newline) >= text_limit) return false;
		out.assign(label);
		out += newline;
		return true;
	}

	void install(HMODULE game)
	{
		if (installed || !ini::flag(L"Game", L"ClassicSpeakers", false)) return;
		if (reinterpret_cast<std::uintptr_t>(game) != 0x400000)
		{
			logger::write("ClassicSpeakers: refused (image base %p, not 0x400000)", static_cast<void*>(game));
			return;
		}
		const limits_rules::guard guards[]{
			{first_site, "e873faffff", "the line parser's first-line call of 0x458820"},
			{child_site, "e8a0f9ffff", "the line parser's child-line call of 0x458820"},
			{parse_va, "81ec0c0c0000a1f8386f0055", "the line parser's common part"},
			{0x458c37, "e874daffff8b08890e8b5004", "text interned into the node's +0 (0x4566b0)"},
			{0x458cd4, "e8d7d9ffff8b08894e088b5004", "textb interned into the node's +8 (0x4566b0)"},
			{attribute_va, "5355568be9578b7d0c8d4701", "a data node's attribute by name"},
			{intern_va, "a0b87a710083ec0884c0742a", "the game's string intern"},
		};
		xml1_hook_transaction::memory memory;
		for (const auto& g : guards)
		{
			std::array<unsigned char, 16> bytes{};
			if (!memory.read(g.va, bytes.data(), g.hex.size() / 2) || !limits_rules::matches(bytes.data(), g.hex))
			{
				logger::write("ClassicSpeakers: guard refused at %08X (%s)", g.va, g.what);
				return;
			}
		}
		const std::vector<xml1_hook_transaction::patch> patches{
			{first_site, {0xe8, 0x73, 0xfa, 0xff, 0xff}, call_bytes(first_site, reinterpret_cast<const void*>(&parse_line))},
			{child_site, {0xe8, 0xa0, 0xf9, 0xff, 0xff}, call_bytes(child_site, reinterpret_cast<const void*>(&parse_line))},
		};
		bool safe = true;
		installed = xml1_hook_transaction::apply(memory, patches, &safe);
		if (!safe) game_version::refuse_startup();
		logger::write("ClassicSpeakers: a line's speakerLabel replaces its speaker label, key unchanged (installed %d)", int(installed));
	}
}
