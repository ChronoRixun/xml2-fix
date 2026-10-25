#include "classic_objectives_rules.hpp"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>
#include <Windows.h>

// Pure rules of [Game] ClassicObjectives with invented objective lists; the optional argument is an owned
// XMen2.exe image whose bytes are compared with every guard (read from the file, never executed).
int main(int argc, char** argv)
{
	using namespace classic_objectives_rules;
	int failures = 0;
	auto check = [&](bool ok, const char* what) { if (!ok) { ++failures; std::printf("FAIL: %s\n", what); } };
	using list = std::vector<objective>;
	const objective done{true, false, true}, open{true, false, false}, hidden{true, true, false},
		child{false, false, false}, side{false, false, false, true};

	// A hidden major in the middle no longer leaves a hole: the visible ones fill consecutive slots.
	const list holes{done, done, done, hidden, open, open};
	check(fills_slot(holes, 0, 0) && fills_slot(holes, 2, 0), "visible majors fill slots");
	check(!fills_slot(holes, 3, 0), "a hidden major fills no slot");
	check(fills_slot(holes, 4, 0), "the major after a hidden one still fills a slot");
	const auto small = plan_for(holes);
	check(small.offset == 0 && small.focus == 3, "few objectives: no window, open on the first unticked (slot 4)");

	// More visible majors than slots: the window starts at the current objective, or late enough to fill the slots.
	list many(12, done);
	many[9] = open;
	many[10] = open;
	const auto late = plan_for(many);
	check(late.offset == 6 && late.focus == 3, "a late current objective: the six-slot window ends at the last objective");
	check(!fills_slot(many, 5, late.offset) && fills_slot(many, 6, late.offset) && fills_slot(many, 11, late.offset),
	      "the window skips exactly the first offset visible majors");
	list early(12, open);
	early[0] = done;
	const auto front = plan_for(early);
	check(front.offset == 1 && front.focus == 0, "an early current objective opens on slot 1 with the window at it");

	// Children and secondaries never take a major slot; a visible secondary keeps slot 7 for its own page.
	const list mixed{child, open, side, open, open, open, open, open, open, open};
	check(!fills_slot(mixed, 0, 0) && !fills_slot(mixed, 2, 0), "children and secondaries fill no major slot");
	check(major_slots(mixed) == native_slots - 1 && major_slots(holes) == native_slots - 1, "slot 7 is always the optional page");
	// Owen's case (in game, 1280x720): eight visible majors, five complete, current the sixth -> slots 3..8 of the
	// visible list, the current one in slot 4.
	list owen{done, done, hidden, done, done, done, open, open, open};
	const auto o = plan_for(owen);
	check(o.offset == 2 && o.focus == 3, "eight visible majors: the current one opens in slot 4");
	const auto with_side = plan_for(mixed);
	check(with_side.offset == 0 && with_side.focus == 0, "the first open major opens");

	// Everything complete: the last objective; nothing visible: the native choice.
	const list finished{done, done, hidden, done};
	const auto end = plan_for(finished);
	check(end.offset == 0 && end.focus == 2, "all complete: open on the last visible objective");
	check(plan_for(list{hidden, child}).focus == -1 && plan_for(list{}).focus == -1, "nothing visible keeps the native choice");
	check(!fills_slot(holes, 99, 0), "an index past the list fills nothing");

	char name[remembered_focus_size];
	check(slot_name(0, name) && std::strcmp(name, "icon01") == 0, "slot 1 is icon01");
	check(slot_name(6, name) && std::strcmp(name, "icon07") == 0, "slot 7 is icon07");
	check(!slot_name(7, name) && !slot_name(-1, name), "no name outside the seven selectors");

	if (argc > 1)
	{
		std::ifstream file(argv[1], std::ios::binary);
		std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)), {});
		auto read = [&](std::size_t offset, void* target, std::size_t size)
		{
			if (offset > bytes.size() || size > bytes.size() - offset) return false;
			std::memcpy(target, bytes.data() + offset, size);
			return true;
		};
		IMAGE_DOS_HEADER dos{};
		IMAGE_NT_HEADERS32 nt{};
		if (!read(0, &dos, sizeof dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < 0 ||
			!read(static_cast<std::size_t>(dos.e_lfanew), &nt, sizeof nt) || nt.Signature != IMAGE_NT_SIGNATURE ||
			nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC || nt.OptionalHeader.ImageBase != 0x400000)
		{
			check(false, "valid supported PE32 image for guard verification");
		}
		else
		{
			const auto headers = static_cast<std::size_t>(dos.e_lfanew) + 4 + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader;
			for (const auto& g : guards)
			{
				bool matched = false;
				for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i)
				{
					IMAGE_SECTION_HEADER section{};
					if (!read(headers + i * sizeof section, &section, sizeof section)) break;
					const auto rva = g.va - nt.OptionalHeader.ImageBase;
					const auto count = g.hex.size() / 2;
					if (rva < section.VirtualAddress || rva - section.VirtualAddress > section.SizeOfRawData ||
						count > section.SizeOfRawData - (rva - section.VirtualAddress)) continue;
					std::vector<unsigned char> actual(count);
					if (!read(std::size_t(section.PointerToRawData) + rva - section.VirtualAddress, actual.data(), count)) break;
					auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
					matched = true;
					for (std::size_t j = 0; j < count; ++j)
						matched = matched && actual[j] == digit(g.hex[2 * j]) * 16 + digit(g.hex[2 * j + 1]);
					break;
				}
				if (!matched) std::printf("guard mismatch: %08X %s\n", g.va, g.what);
				check(matched, "retail guard bytes");
			}
		}
	}
	std::printf("%s (%d failures)\n", failures ? "FAILED" : "PASS", failures);
	return failures ? 1 : 0;
}
