#pragma once
#include "limits_rules.hpp"
#include <array>
namespace harm_single_chain_rules
{
	using limits_rules::guard;
	constexpr std::uint32_t tail_site=0x439c00, tail_resume=0x439c0f;
	constexpr std::uint32_t request_site=0x41a8da, request_resume=0x41a8e1;
	inline constexpr std::array<unsigned char,15> tail_original{0x8a,0x96,0xc4,0,0,0,0x8a,0x86,0x60,3,0,0,0x80,0xe2,0x7f};
	inline constexpr std::array<unsigned char,7> request_original{0x80,0xa6,0xc4,0,0,0,0x7f};
	inline constexpr std::array<guard,8> guards{{
		{0x4398c5,"8b178dae640300005568784468008bcfff521c8b178d86680300005068704468008bcfff521c","on and off timer field layout"},
		{0x6842ec,"90a84100109f410000aa4100909a4300","ordinary harm activation vtable"},
		{0x41a820,"51568bf18b4604c1e814a80174508a86c400000084c07846","enabled and pending request gate"},
		{0x41b8de,"8b96a80000008b3e6a015152e8c1481400d95c240883c408568bceff97c8000000","random-interval base repeat scheduling"},
		{tail_site,"8a96c40000008a866003000080e27fa8108896c4000000","harm completion clears pending after repeat scheduling"},
		{request_site,"80a6c40000007f5ec20c00","immediate request clears pending after harm returns"},
		{0x41a5c1,"8a86c4000000894c2404247f","delayed callback clears pending before activation"},
		{0x41b90a,"8b0651568bceff90c8000000","base action schedules its repeat"},
	}};
	inline const guard* first_mismatch(const std::uint8_t* image)
	{
		for(const auto& g:guards)if(!limits_rules::matches(image+g.va-limits_rules::image_base,g.hex))return &g;
		return nullptr;
	}
}
