#include "confirmation_release_rules.hpp"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv)
{
	using namespace confirmation_release_rules;
	int checks = 0, failures = 0;
	auto check = [&](bool ok, const char* name) { ++checks; if (!ok) { ++failures; std::printf("FAIL %s\n", name); } };
	const source enter{1, 28}, keypad{1, 156}, pad1{3, 22}, pad2{4, 22}, mouse{2, 9};
	const std::array one{enter}, other{pad2};
	const std::array together{enter, pad2};
	latch state;
	physical_state input;
	input.keys[28] = 128;
	check(state.allows(one), "first confirmation allowed");
	state.consume(one);
	for (int frames : {5, 31, 94, 600})
	{
		for (int i = 0; i < frames; ++i) state.sample(input);
		check(!state.allows(one), "replacement popup cannot outwait held confirmation");
	}
	check(state.allows(other) && state.allows(together), "another player's source remains available");
	state.consume(other);
	input.pads[1].buttons[1] = 128;
	state.sample(input);
	check(!state.allows(together), "both simultaneous answers consumed independently");
	input.keys[28] = 0;
	state.sample(input);
	input.keys[28] = 128;
	check(state.allows(one) && !state.allows(other), "fresh key accepted while other player's pad remains held");
	state.consume(one);
	input.pads[1].buttons[1] = 0;
	state.sample(input);
	check(state.allows(other) && !state.allows(one), "release only clears its own source");
	check(state.available(keypad) && state.available(pad1) && state.available(mouse), "same action on other devices is independent");
	const std::array button{pad1}, click{mouse};
	input.pads[0].buttons[1] = 128;
	input.mouse[0] = 128;
	state.consume(button); state.consume(click); state.sample(input);
	check(!state.allows(button) && !state.allows(click), "pad and mouse confirmations stay consumed while held");
	input.pads[0].buttons[1] = 0; input.mouse[0] = 0; state.sample(input);
	check(state.allows(button) && state.allows(click), "pad and mouse release permit a new press");
	state.consume(one); state.consume(button); state.consume(click);
	input.keys[28] = 0;
	input.keyboard_known = input.mouse_known = input.pad_known[0] = false;
	state.sample(input);
	check(!state.allows(one) && !state.allows(button) && !state.allows(click), "failed reads and zeroed buffers cannot rearm consumed input");
	check(state.allows(other), "unknown consumed input cannot steal another source's fresh press");
	input.keyboard_known = input.mouse_known = input.pad_known[0] = true;
	state.sample(input);
	check(state.allows(one) && state.allows(button) && state.allows(click), "successful released state rearms after input read failure");
	check(!supported({0, 0}) && !supported({1, 256}) && !supported({2, 8}) && !supported({3, 50}) && !supported({13, 22}), "invalid and unsupported controls cannot index latch");
	check(supported({12, 49}) && supported({1, 255}) && supported({2, 11}), "supported bounds");
	input.pads[0].axes[0] = 800;
	check(down(input, {3, 1}) && !down(input, {3, 2}), "pad axis direction");
	input.pads[0].axes[0] = -800;
	check(!down(input, {3, 1}) && down(input, {3, 2}), "opposite pad axis direction");
	input.pads[0].pov = 9000;
	check(down(input, {3, 17}) && !down(input, {3, 18}), "pad hat right");
	input.pads[0].pov = 0xffffffff;
	check(!down(input, {3, 20}), "neutral hat is released");
	check(!enabled(std::nullopt) && !enabled("garbage") && !enabled("off"), "feature stays opt-in and refuses invalid value");
	check(enabled("YES ; enabled") && enabled("1") && !enabled("0"), "feature follows common ini switch rules");
	check(eligible_player(2, 2, 16, 0, false, 0, false, true, true) &&
	      !eligible_player(2, 1, 16, 0, false, 0, false, true, true), "owned popup only uses its owner");
	check(eligible_player(2, 0, 0, 16, false, 2, false, true, false), "owned popup retains player one's native fallback");
	check(!eligible_player(2, 0, 16, 0, false, 2, false, true, true), "inactive fallback cannot steal another player's acceptance");
	check(eligible_player(-1, 1, 16, 0, false, 0, false, true, false) &&
	      !eligible_player(-1, 1, 16, 0, false, 0, false, false, false), "shared popup respects menu participation");
	check(!eligible_player(-1, 1, 16, 0, false, 0, true, true, true) &&
	      eligible_player(-1, 0, 16, 0, false, 0, true, true, true), "remote active player restriction");
	check(eligible_player(-1, 1, 16, 0, true, 0, false, false, true) &&
	      !eligible_player(-1, 1, 16, 0, true, 0, false, true, false), "aggregate popup follows native aggregate permission");
	check(!eligible_player(0, 0, 0, 0, false, 0, false, true, true), "no native accept bit cannot create acceptance");
	// Synthetic image, never a fixture containing game data. Corrupt every protected byte
	// separately: no other guard or partial installation can hide a mismatch.
	std::vector<std::uint8_t> image(0x300000, 0);
	for (const auto& g : guards)
		for (std::size_t i = 0; i < g.hex.size() / 2; ++i)
			image[g.va - image_base + i] = static_cast<std::uint8_t>(nibble(g.hex[i * 2]) * 16 + nibble(g.hex[i * 2 + 1]));
	check(first_mismatch(image.data()) == nullptr, "synthetic compatible image");
	for (const auto& g : guards)
		for (std::size_t i = 0; i < g.hex.size() / 2; ++i)
		{
			auto& b = image[g.va - image_base + i]; b ^= 0x80;
			check(first_mismatch(image.data()) == &g, "each corrupt guard byte refuses hook");
			b ^= 0x80;
		}
	for (const auto site : {raw_site, accept_site, close_site, keyboard_site, keyboard_retry_site, mouse_site})
	{
		const auto bytes = jump(site, 0x12345678);
		std::uint32_t displacement = 0; std::memcpy(&displacement, bytes.data() + 1, 4);
		check(bytes[0] == 0xe9 && site + 5 + displacement == 0x12345678 && bytes[5] == 0x90 && bytes[6] == 0x90, "hook branch target and padding");
	}
	// Optional local retail copy: map PE sections as data, never execute its entry point.
	if (argc == 2)
	{
		std::ifstream file(argv[1], std::ios::binary);
		std::vector<char> bytes((std::istreambuf_iterator<char>(file)), {});
		check(bytes.size() >= sizeof(IMAGE_DOS_HEADER), "retail PE present");
		if (bytes.size() >= sizeof(IMAGE_DOS_HEADER))
		{
			const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
			if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 || static_cast<std::size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > bytes.size()) return 1;
			const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(bytes.data() + dos->e_lfanew);
			check(pe->Signature == IMAGE_NT_SIGNATURE && pe->OptionalHeader.ImageBase == image_base, "retail image base");
			// Retail's SizeOfImage is not page-rounded, while its last raw section is.
			std::vector<std::uint8_t> mapped((static_cast<std::size_t>(pe->OptionalHeader.SizeOfImage) + 0xfff) & ~std::size_t(0xfff), 0);
			const auto* section = IMAGE_FIRST_SECTION(pe);
			for (unsigned i = 0; i < pe->FileHeader.NumberOfSections; ++i)
			{
				if (static_cast<std::size_t>(section[i].PointerToRawData) + section[i].SizeOfRawData > bytes.size() || static_cast<std::size_t>(section[i].VirtualAddress) + section[i].SizeOfRawData > mapped.size()) return 1;
				std::memcpy(mapped.data() + section[i].VirtualAddress, bytes.data() + section[i].PointerToRawData, section[i].SizeOfRawData);
			}
			const auto* g = first_mismatch(mapped.data());
			if (g) std::printf("retail mismatch 0x%08X %s\n", g->va, g->what);
			check(!g, "all hook dependencies match local retail PE");
		}
	}
	std::printf("confirmation release: %d checks, %d failures\n", checks, failures);
	return failures ? 1 : 0;
}
