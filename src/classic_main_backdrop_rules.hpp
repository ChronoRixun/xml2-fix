#pragma once

#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <string_view>

namespace classic_main_backdrop_rules
{
	using address = std::uint32_t;
	constexpr address configure_rva = 0x6a780;
	constexpr address shader_vtable_rva = 0x7a698;
	constexpr address disable_lighting_rva = 0x14fba4;
	constexpr address append_rva = 0x477a0;
	constexpr address zone_buffer = 0x72a758;
	// Three pushes and mov edi,[esp+0x10]; no relative instruction in the trampoline.
	inline constexpr std::array<std::uint8_t, 9> prologue{0x53, 0x56, 0x57, 0x8b, 0x7c, 0x24, 0x10, 0x8b, 0xf1};
	constexpr std::size_t displaced = 7;

	// WinMain's main loop (0x403541: app = 0x401b00(); while (app->frame()) ...) falls through to the
	// first teardown call, 0x5f5720, before the game releases its scenes and calls igReleaseAlchemy
	// (0x4035c4), which frees Alchemy's memory. The room pin must be dropped at that call: a room
	// directory still retained past it keeps the Cerebro igDx8EnvironmentMapShaders alive, so their
	// cleanup (libIGSg +0x69b40) never clears the class's static attribute refs, and libIGSg's DLL-detach
	// destructor (+0x69540) then decrements a ref in freed memory (access violation at +0x6955e).
	constexpr address loop_exit_site = 0x40354a;
	constexpr address loop_exit_call = 0x403551;
	constexpr address loop_exit_target = 0x5f5720;
	// call [edx+0xc]; test al,al; jne 0x403541; call 0x5f5720
	inline constexpr std::array<std::uint8_t, 12> loop_exit_bytes{
		0xff, 0x52, 0x0c, 0x84, 0xc0, 0x75, 0xf0, 0xe8, 0xca, 0x21, 0x1f, 0x00};

	// The five bytes of `call target` placed at `from`.
	inline std::array<std::uint8_t, 5> call_to(address from, address target)
	{
		const address relative = target - from - 5;
		return {0xe8, std::uint8_t(relative), std::uint8_t(relative >> 8), std::uint8_t(relative >> 16), std::uint8_t(relative >> 24)};
	}

	// The room asset directory the hook retains. capture() says which reference to take and which to
	// drop for a newly loaded room; close() hands back the last held one at game shutdown and refuses
	// every later capture, so no reference outlives the engine.
	struct room_pin
	{
		address held = 0;
		bool closed = false;
		struct step { address retain = 0, release = 0; };

		step capture(address directory)
		{
			if (closed || !directory || directory == held) return {};
			const step result{directory, held};
			held = directory;
			return result;
		}

		address close()
		{
			closed = true;
			const address last = held;
			held = 0;
			return last;
		}
	};

	inline bool in_scene(std::string_view current, std::string_view wanted, std::string_view model,
	                     std::span<const std::string_view> allowed, float reflection, bool opaque)
	{
		if (current != wanted || !opaque || !std::isfinite(reflection) || std::fabs(reflection - 0.1f) > 1e-6f) return false;
		const auto slash = model.find_last_of("/\\");
		if (slash != std::string_view::npos) model.remove_prefix(slash + 1);
		for (auto name : allowed) if (model == name) return true;
		return false;
	}

	struct list_plan
	{
		address begin = 0, end = 0;
		bool add_begin = false, add_end = false;
	};

	template <typename Memory>
	bool word(Memory& memory, address where, address& value)
	{
		return where >= 0x10000 && memory.read(where, &value, sizeof value);
	}

	// igDirectory::getRef (Core RVA 0x296a0): resolved sharing list if present,
	// otherwise each directory entry's object at +0x18. Check membership afresh;
	// labels and zone names alone never establish ownership.
	template <typename Memory>
	bool owned_by_directory(Memory& memory, address directory, address shader, address object_entry_vtable)
	{
		address count = 0, capacity = 0, entries = 0, refs = 0, resolved = 0;
		if (!word(memory, directory + 8, count) || !count || count > 4096 ||
		    !word(memory, directory + 12, capacity) || capacity < count || capacity > 8192 ||
		    !word(memory, directory + 16, entries) || !word(memory, directory + 0x2c, refs)) return false;
		if (refs)
		{
			address resolved_count = 0;
			if (!word(memory, refs + 8, resolved_count) || resolved_count != count ||
			    !word(memory, refs + 12, capacity) || capacity < count || capacity > 8192 ||
			    !word(memory, refs + 16, resolved)) return false;
		}
		bool found = false;
		for (address index = 0; index < count; ++index)
		{
			address entry = 0, object = 0, table = 0;
			if (!word(memory, entries + 4 * index, entry)) return false;
			if (!word(memory, entry, table)) return false;
			if (table != object_entry_vtable) continue; // memory and external entries are not owned objects
			if (refs) { if (!word(memory,resolved+4*index,object)) return false; }
			else if (!word(memory, entry + 0x18, object)) return false;
			found |= object == shader;
		}
		return found;
	}

	template <typename Memory>
	bool first_list(Memory& memory, address container, address diffuse, address lighting,
	                address& first, bool& missing)
	{
		address count = 0, capacity = 0, data = 0;
		if (!word(memory, container + 8, count) || count != 2 ||
		    !word(memory, container + 12, capacity) || capacity < count || capacity > 16 ||
		    !word(memory, container + 16, data) || !word(memory, data, first)) return false;
		if (!word(memory, first + 8, count) || count < 3 || count > 16 ||
		    !word(memory, first + 12, capacity) || capacity < count || capacity > 64 ||
		    !word(memory, first + 16, data)) return false;
		bool has_diffuse = false, has_lighting = false;
		for (address i = 0; i < count; ++i)
		{
			address attr = 0;
			if (!word(memory, data + 4 * i, attr) || attr < 0x10000) return false;
			has_diffuse |= attr == diffuse;
			has_lighting |= attr == lighting;
		}
		missing = !has_lighting;
		return has_diffuse;
	}

	template <typename Memory>
	bool plan(Memory& memory, address shader, address lighting, list_plan& result)
	{
		address diffuse = 0, begin = 0, end = 0;
		list_plan candidate;
		if (lighting < 0x10000 || !word(memory, shader + 0x24, diffuse) || diffuse < 0x10000 ||
		    !word(memory, shader + 0x40, begin) || !word(memory, shader + 0x44, end) ||
		    !first_list(memory, begin, diffuse, lighting, candidate.begin, candidate.add_begin) ||
		    !first_list(memory, end, diffuse, lighting, candidate.end, candidate.add_end) ||
		    candidate.begin == candidate.end) return false;
		result = candidate; // no partial plan when either container is unsupported
		return true;
	}
}
