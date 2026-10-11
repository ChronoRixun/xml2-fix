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
