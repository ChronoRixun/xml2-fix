// The team-combo popup guard (combo_heads): every retail byte it relies on, and its show on a fake HUD.
// Usage: xml2_combo_heads_test <XMen2.exe>
#include "combo_heads.hpp"
#include "combo_heads_rules.hpp"
#include "ini.hpp"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

namespace ini
{
	std::optional<std::string> text(const wchar_t*, const wchar_t*) { return std::nullopt; }
	bool flag(const wchar_t*, const wchar_t*, const bool fallback) { return fallback; }
}
namespace logger
{
	unsigned lines = 0;
	void write(const char*, ...) { ++lines; }
}
namespace game_version
{
	void refuse_startup() {}
}

namespace
{
	using namespace combo_heads_rules;
	std::vector<std::uint8_t> hud(widget_offset + 0x100);
	std::uint8_t* widget() { return hud.data() + widget_offset; }
	unsigned hides = 0, originals = 0;
	int hide_argument = -1;
	std::uint32_t head_a = 0, head_b = 0;
	std::uint8_t visible_after = 1;
	const void* seen[2]{};
	int seen_type = -1, seen_amount = -1;
	const char* seen_text = nullptr;

	void __fastcall fake_hide(void* self, void*, const int shown)
	{
		assert(self == widget());
		++hides;
		hide_argument = shown;
		widget()[visible_offset] = static_cast<std::uint8_t>(shown);
	}
	std::uintptr_t widget_table[8]{};

	// The game's show as the dump saw it: heads written into +0x1c / +0x20, then visible.
	void __fastcall fake_show(void* self, void*, const void* a, const void* b, const int type, const int amount, const char* text)
	{
		++originals;
		if (!self)
		{
			return;
		}
		assert(self == hud.data());
		seen[0] = a; seen[1] = b; seen_type = type; seen_amount = amount; seen_text = text;
		std::memcpy(widget() + head_a_offset, &head_a, 4);
		std::memcpy(widget() + head_b_offset, &head_b, 4);
		widget()[visible_offset] = visible_after;
	}

	void scenario(const std::uint32_t a, const std::uint32_t b, const std::uint8_t visible, const unsigned want_hides)
	{
		head_a = a; head_b = b; visible_after = visible;
		const unsigned before = hides, logged = logger::lines;
		int ha = 1, hb = 2;
		combo_heads::show(hud.data(), nullptr, &ha, &hb, 1, 7, "Batter Up");
		assert(seen[0] == &ha && seen[1] == &hb && seen_type == 1 && seen_amount == 7 && std::strcmp(seen_text, "Batter Up") == 0);
		assert(hides - before == want_hides);
		if (want_hides)
		{
			assert(hide_argument == 0 && widget()[visible_offset] == 0); // the draw (0x5a6515) now skips it
			assert(logger::lines == logged + 1);
		}
		else
		{
			assert(widget()[visible_offset] == visible);
		}
	}

	std::vector<std::uint8_t> map_image(const char* path)
	{
		std::ifstream file(path, std::ios::binary);
		const std::vector<std::uint8_t> raw((std::istreambuf_iterator<char>(file)), {});
		assert(raw.size() > 0x1000);
		auto u16 = [&](std::size_t p) { std::uint16_t n; std::memcpy(&n, raw.data() + p, 2); return n; };
		auto u32 = [&](std::size_t p) { std::uint32_t n; std::memcpy(&n, raw.data() + p, 4); return n; };
		const auto pe = u32(0x3c), sections = pe + 24 + u16(pe + 20);
		std::vector<std::uint8_t> image(0x800000);
		for (unsigned i = 0; i < u16(pe + 6); ++i)
		{
			const auto row = sections + 40 * i, rva = u32(row + 12), size = u32(row + 16), at = u32(row + 20);
			assert(rva + size <= image.size() && at + size <= raw.size());
			std::memcpy(image.data() + rva, raw.data() + at, size);
		}
		return image;
	}
}

int main(const int argc, char** argv)
{
	assert(argc == 2);
	// 1. Every guarded retail byte matches XMen2.exe; a changed byte in any guard is refused.
	auto image = map_image(argv[1]);
	assert(first_mismatch(image.data()) == nullptr);
	for (const auto& g : guards)
	{
		auto& byte = image[g.va - limits_rules::image_base];
		byte ^= 0xff;
		assert(first_mismatch(image.data()) == &g);
		byte ^= 0xff;
	}
	// The slot holds the thunk's address, and the widget's vt+0x10 the hide the guard calls.
	std::uint32_t slot = 0, hide_slot = 0;
	std::memcpy(&slot, image.data() + (hud_show_slot - limits_rules::image_base), 4);
	std::memcpy(&hide_slot, image.data() + (0x69dbac + hide_slot_offset - limits_rules::image_base), 4);
	assert(slot == retail_show && hide_slot == 0x5a6450);

	// 2. missing_heads on raw widgets.
	std::uint8_t w[0x40]{};
	assert(missing_heads(w) == 0); // not visible
	w[visible_offset] = 1;
	assert(missing_heads(w) == 3);
	w[head_a_offset] = 0xd4;
	assert(missing_heads(w) == 2); // the owner's dump: +0x1c 0x7d44d4, +0x20 null
	w[head_b_offset + 3] = 0x10;
	assert(missing_heads(w) == 0);

	// 3. The fix's show on a fake HUD.
	widget_table[hide_slot_offset / 4] = reinterpret_cast<std::uintptr_t>(&fake_hide);
	const std::uintptr_t table = reinterpret_cast<std::uintptr_t>(widget_table);
	std::memcpy(widget(), &table, 4);
	combo_heads::set_original_for_test(&fake_show);
	scenario(0x7d44d4, 0x7d4500, 1, 0); // both heads: the popup shows as the game's
	scenario(0x7d44d4, 0, 1, 1);        // the owner's crash: second head null -> hidden
	scenario(0, 0x7d4500, 1, 1);        // first head null -> hidden
	scenario(0, 0, 1, 1);               // both null -> hidden
	scenario(0, 0, 0, 0);               // the game refused the show (bad handles): nothing to hide
	assert(combo_heads::hidden_count() == 3 && originals == 5);
	combo_heads::show(nullptr, nullptr, nullptr, nullptr, 0, 0, nullptr); // no HUD: the original alone
	assert(originals == 6);
	std::puts("combo heads: OK");
	return 0;
}
